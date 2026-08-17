#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "PitchShifter.h"

/** Filtro de un polo. Deliberadamente simple: dentro de un lazo de
    realimentacion un filtro de orden alto es facil que se vuelva inestable, y
    aqui solo queremos oscurecer las repeticiones, no cirugia espectral. */
class OnePole
{
public:
    void setCutoff (float hz, double sampleRate) noexcept
    {
        const float x = -2.0f * 3.14159265f * hz / static_cast<float> (sampleRate);
        g = std::clamp (1.0f - std::exp (x), 0.0f, 1.0f);
    }

    void reset() noexcept { z = 0.0f; }

    float lowpass (float x) noexcept { z += g * (x - z); return z; }

private:
    float z = 0.0f, g = 1.0f;
};

//==============================================================================
/**
    Reverse delay por granos solapados al 50 %.

    IDEA
    ----
    "Reverse" es no-causal: para reproducir algo al reves hay que haberlo
    grabado antes. Asi que el efecto es siempre: grabar una ventana de L
    muestras -> leerla hacia atras. El desfase inherente es L. Eso NO es
    latencia a compensar, es el efecto.

    POR QUE DOS GRANOS
    ------------------
    La version ingenua (ping-pong entre dos buffers) produce un click audible
    en cada costura. Aqui hay dos cabezas de lectura invertidas desfasadas L/2,
    con ventana sin/cos:

        gA = sin(pi * phaseA / L)
        gB = sin(pi * phaseB / L) = cos(pi * phaseA / L)   [phaseB = phaseA + L/2]

    y sin^2 + cos^2 = 1 -> potencia constante. Cada grano nace y muere en
    amplitud exactamente 0, asi que la costura es inaudible.

    POR QUE CADA GRANO TIENE SU PROPIA DURACION (len)
    -------------------------------------------------
    Si al mover Length reposicionas los DOS granos a la vez, uno esta a mitad de
    vuelo con la ventana en amplitud alta, y saltarle el puntero de lectura
    produce un click enorme (medido en su dia: 1.30 de salto, mas fuerte que la
    propia senal). Cada grano solo adopta la longitud nueva EN SU PROPIA
    COSTURA, donde su ventana vale 0. Eso desalinea el desfase L/2, asi que el
    grano B se reengancha ajustando la DURACION de su siguiente grano, nunca su
    posicion: un grano que empieza y acaba en 0 nunca produce discontinuidad.

    LAZO DE REALIMENTACION
    ----------------------
    fb -> paso alto -> paso bajo -> saturacion -> de vuelta al buffer.
    La saturacion va SOLO aqui; la senal seca entra limpia. Ademas de dar
    caracter, es lo que garantiza estabilidad: tanh acota la salida pase lo
    que pase, asi que el lazo no puede divergir aunque el feedback este al
    maximo y el filtro este resonando.

    LECTURA FRACCIONARIA
    --------------------
    La posicion de lectura es un float con interpolacion lineal:

        pos = start - phase * rate + modOffset

    Con rate=1 y modOffset=0 la posicion cae en enteros exactos, frac vale 0 y
    el resultado es identico a la version con indices enteros. O sea que esto
    no cambia el sonido por defecto; solo habilita dos cosas:

      - modOffset  -> wow y flutter (modular la posicion = modular la afinacion)
      - rate       -> pitch shifting

    Sobre rate conviene saber una cosa: esta topologia (granos con ventana,
    solapados al 50 %, con crossfade de potencia constante) ES la de un pitch
    shifter granular tipo PSOLA. La unica diferencia con uno convencional es que
    aqui el incremento es negativo. Cambiar rate a r desplaza la afinacion en
    12*log2(r) semitonos, y el crossfade que ya existe absorbe las
    discontinuidades.

    La limitacion honesta: el tamano de grano esta atado a Length, y un buen
    pitch shifting quiere granos de 30-80 ms. Con Length en 1500 ms el shifting
    sonaria muy embarrado.

    POR QUE EL BUFFER ES 4*maxLength
    --------------------------------
    Cada grano lee hasta len*rate muestras hacia atras mientras la escritura
    avanza otras len hacia delante. Con la correccion de reenganche len llega a
    1.25*L y con rate hasta 2 (una octava arriba) el span de lectura llega a
    2.5*L, mas 1.25*L de escritura: 3.75*L. Por eso 4*maxLength y no 3, que era
    lo justo para rate=1.
*/
class ReverseDelay
{
public:
    /** Reserva memoria. Llamar SOLO desde prepareToPlay, JAMAS desde processBlock. */
    void prepare (int maxLengthSamples, int initialLengthSamples, double sr)
    {
        sampleRate = sr;
        maxLength  = std::max (2, maxLengthSamples);
        bufLen     = maxLength * 4;
        buffer.assign (static_cast<size_t> (bufLen), 0.0f);

        length = pendingLength = std::clamp (initialLengthSamples, 2, maxLength);
        shimmer.prepare (sampleRate);
        setShimmer (0.0f, 12.0f);
        setFeedbackTone (20.0f, 20000.0f);
        setDrive (1.0f);
        rate      = 1.0f;
        modOffset = 0.0f;
        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        lastOut  = 0.0f;
        hpFilter.reset();
        lpFilter.reset();
        shimmer.reset();

        frozen = false;
        a = { 0,          0, length };
        b = { length / 2, 0, length };   // desfasado media ventana
    }

    /** FREEZE / DRONE.

        Congelar es literalmente dejar de escribir Y dejar de avanzar writePos.
        Cada grano nuevo captura el mismo `start`, asi que cicla las mismas L
        muestras invertidas indefinidamente.

        Tres propiedades salen gratis por construccion:

          - Sin clicks. Los granos conservan su ventana, que sigue naciendo y
            muriendo en 0, asi que congelar a mitad de grano es limpio: los
            granos en vuelo terminan de leer contenido real y el siguiente ya
            arranca sobre el buffer congelado.
          - Sin riesgo de nivel. No estamos realimentando, estamos dejando de
            escribir: no hay acumulacion posible y el techo de maxFeedback ni
            entra en juego. Es la forma SEGURA de tener sustain infinito.
          - Se puede seguir tocando encima. La senal seca no toca el buffer.
    */
    void setFrozen (bool shouldFreeze) noexcept { frozen = shouldFreeze; }
    bool isFrozen() const noexcept              { return frozen; }

    /** Se aplica en la costura de cada grano, no al instante. */
    void setLength (int newLengthSamples) noexcept
    {
        pendingLength = std::clamp (newLengthSamples, 2, maxLength);
    }

    /** Velocidad de lectura. 1 = normal. Desplaza la afinacion 12*log2(rate)
        semitonos: 2 es una octava arriba, 0.5 una octava abajo. */
    void setReadRate (float r) noexcept { rate = std::clamp (r, 0.25f, 2.0f); }

    /** Desplazamiento de la posicion de lectura, en muestras. Modularlo con un
        LFO da wow y flutter.

        IMPORTANTE: los dos granos comparten este offset a proposito. Modularlos
        por separado haria que el crossfade sumase dos copias con afinaciones
        distintas, lo que produce filtrado en peine. */
    void setModulation (float samples) noexcept { modOffset = samples; }

    /** Shimmer: parte de la realimentacion pasa por un pitch shifter, asi que
        cada repeticion sube (o baja) de altura y se van apilando octavas.

        amount 0..1 mezcla entre realimentacion normal y desplazada. */
    void setShimmer (float amount, float semitones) noexcept
    {
        shimmerAmount = std::clamp (amount, 0.0f, 1.0f);
        shimmer.setSemitones (semitones);
    }

    void setFeedbackTone (float highPassHz, float lowPassHz) noexcept
    {
        hpFilter.setCutoff (highPassHz, sampleRate);
        lpFilter.setCutoff (lowPassHz,  sampleRate);
    }

    /** drive >= 1. A mas drive, mas ganancia contra el techo de tanh. */
    void setDrive (float d) noexcept
    {
        drive = std::max (1.0f, d);
    }

    /** TECHO DURO DE REALIMENTACION. Valor obtenido midiendo, no estimando.

        El lazo tiene ganancia >1 por dos motivos que se acumulan:
          1. Cada muestra escrita se lee DOS veces, una por cada grano.
          2. La ventana sin/cos es de potencia constante, no de amplitud: para
             contenido coherente (y una autooscilacion lo es) gA+gB promedia
             ~1.27, no 1.

        Barrido sobre todas las combinaciones de filtro, drive y longitud,
        midiendo el nivel 30 s y 60 s despues de cortar la senal de entrada:

            fb 0.50 -> 0.0068 -> 0.00004   decae
            fb 0.60 -> 0.0674 -> 0.0051    decae            <-- limite
            fb 0.65 -> 0.1734 -> 0.0417    -12 dB / 30 s, demasiado lento
            fb 0.70 -> 0.3631 -> 0.2312    se sostiene indefinidamente
            fb 1.00 -> se queda en 3.14 de amplitud (+10 dBFS)

        Se clampea AQUI y no solo en el rango del parametro, porque un host
        puede automatizar fuera de rango o cargar un preset de otra version.

        Para sustain infinito lo correcto NO es subir esto, sino un modo Freeze
        (dejar de escribir entrada y ciclar el buffer), que es seguro por
        construccion. */
    static constexpr float maxFeedback = 0.60f;

    /** El shimmer HACE EL LAZO MAS ESTABLE, no menos. Es contraintuitivo pero
        tiene sentido: el pitch shifter empuja la energia hacia arriba en cada
        pasada, y arriba se escapa (el paso bajo la atenua y lo que supera
        Nyquist desaparece). Sin shimmer la energia se queda dando vueltas.

        Barrido con el tope interno abierto, nivel 30 s tras cortar la senal:

            shimmer 0.00 -> seguro hasta fb 0.60  (0.70 ya sostiene 0.139)
            shimmer 0.50 -> seguro hasta fb 0.80
            shimmer 0.80 -> seguro hasta fb 0.80
            shimmer 1.00 -> seguro hasta fb 0.70  (0.80 sostiene 0.045)

        La relacion no es monotona, asi que se usa la regla lineal conservadora
        0.60 + 0.10*shimmer, que queda por debajo del limite medido en TODOS los
        puntos. Sin trucos: un limite ajustado a la curva real seria mas
        permisivo pero fragil ante cualquier cambio futuro del lazo. */
    float currentMaxFeedback() const noexcept
    {
        return maxFeedback + 0.10f * shimmerAmount;
    }

    /** feedback normalizado 0..1 (no porcentaje). */
    float process (float input, float feedback) noexcept
    {
        if (! frozen)
        {
            float fb = std::clamp (feedback, 0.0f, currentMaxFeedback()) * lastOut;

            fb = fb - hpFilter.lowpass (fb);   // paso alto = senal - paso bajo
            fb = lpFilter.lowpass (fb);

            // El shifter va ANTES del saturador: asi el saturador sigue siendo
            // el ultimo elemento del lazo y su techo de ganancia 1 continua
            // acotando todo lo que entra al buffer.
            if (shimmerAmount > 0.0001f)
                fb = fb + shimmerAmount * (shimmer.process (fb) - fb);

            fb = std::tanh (drive * fb) / drive;   // ganancia maxima = 1 exacta

            buffer[static_cast<size_t> (writePos)] = input + fb;
        }

        const float out = read (a) + read (b);
        lastOut = out;

        // Congelado, writePos NO avanza: los granos siguientes capturan siempre
        // el mismo punto de arranque y el bucle se cierra sobre si mismo.
        if (! frozen && ++writePos >= bufLen)
            writePos = 0;

        // --- grano A: es la referencia, adopta la longitud nueva sin mas ---
        if (++a.phase >= a.len)
        {
            a.phase = 0;
            a.start = writePos;
            length  = pendingLength;
            a.len   = length;
        }

        // --- grano B: se reengancha ajustando DURACION, nunca posicion ---
        if (++b.phase >= b.len)
        {
            b.phase = 0;
            b.start = writePos;

            const int err = a.phase - length / 2;          // en [-L/2, L/2)
            b.len = std::clamp (length - err,
                                std::max (2, (length * 3) / 4),
                                (length * 5) / 4);
        }

        return out;
    }

    /** Solo para la visualizacion de la GUI. */
    float phaseNormA() const noexcept
    {
        return a.len > 0 ? static_cast<float> (a.phase) / static_cast<float> (a.len) : 0.0f;
    }

private:
    struct Grain { int phase; int start; int len; };

    float read (const Grain& g) const noexcept
    {
        const float back = static_cast<float> (g.phase) * rate;

        // El offset de modulacion NO puede adelantar la lectura al punto de
        // arranque del grano: ahi la memoria todava no se ha escrito en este
        // ciclo y contiene audio de hace bufLen muestras (varios segundos).
        // Sin este clamp, una modulacion de +400 muestras producia un salto de
        // 4.1 veces el techo teorico justo al empezar cada grano.
        //
        // Al clampear a `back`, el offset entra en juego progresivamente desde
        // el arranque del grano, que es precisamente donde la ventana vale casi
        // 0, asi que la atenuacion de la modulacion ahi es inaudible.
        const float off = std::min (modOffset, back);

        float p = static_cast<float> (g.start) - back + off;

        // Un solo giro basta: |phase*rate + modOffset| < bufLen por construccion
        // (ver la nota sobre el tamano del buffer).
        while (p <  0.0f)                              p += static_cast<float> (bufLen);
        while (p >= static_cast<float> (bufLen))       p -= static_cast<float> (bufLen);

        const int   i0   = static_cast<int> (p);
        const float frac = p - static_cast<float> (i0);
        int i1 = i0 + 1;
        if (i1 >= bufLen) i1 = 0;

        const float s = buffer[static_cast<size_t> (i0)]
                      + frac * (buffer[static_cast<size_t> (i1)]
                              - buffer[static_cast<size_t> (i0)]);

        const float gain = std::sin (pi * static_cast<float> (g.phase)
                                        / static_cast<float> (g.len));
        return gain * s;
    }

    static constexpr float pi = 3.14159265358979f;

    std::vector<float> buffer;
    double sampleRate = 44100.0;
    int bufLen    = 0;
    int maxLength = 0;
    int writePos  = 0;

    int length        = 0;   // L activa
    int pendingLength = 0;   // L pedida por el usuario

    Grain a { 0, 0, 0 };
    Grain b { 0, 0, 0 };

    OnePole hpFilter, lpFilter;
    PitchShifter shimmer;
    float shimmerAmount = 0.0f;
    float drive = 1.0f;
    float rate = 1.0f, modOffset = 0.0f;
    float lastOut = 0.0f;
    bool  frozen = false;
};
