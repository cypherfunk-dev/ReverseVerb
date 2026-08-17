#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

/**
    Pitch shifter granular de dos granos solapados al 50 %.

    POR QUE UNA ETAPA APARTE Y NO REUTILIZAR EL MOTOR DE REVERSE
    -----------------------------------------------------------
    El motor de reverse ya es topologicamente un pitch shifter granular: basta
    cambiar su rate. El problema es que su tamano de grano esta atado al
    parametro Length, y un buen pitch shifting quiere granos de 30-80 ms. Con
    Length en 1500 ms el resultado seria un warble lento e inutilizable.

    Esta clase tiene su propio tamano de grano, independiente de todo lo demas.

    COMO FUNCIONA
    -------------
    Una linea de retardo donde la lectura avanza a distinta velocidad que la
    escritura. La distancia `d` entre ambas deriva (1-rate) muestras por
    muestra; al salirse del rango se envuelve un grano entero.

    POR QUE UN CROSSFADE CORTO Y NO SOLAPE AL 50 %
    ----------------------------------------------
    En ReverseDelay los dos granos leen regiones distintas y decorrelacionadas,
    asi que la ventana correcta es la de POTENCIA constante (sin/cos).

    Aqui es al reves: los dos granos leen el MISMO material con un desfase fijo.
    Sumarlos con solape al 50 % los hace interferir como un filtro en peine.
    Medido con la primera version, que usaba sin/cos: con +12 semitonos sobre un
    seno de 220 Hz, la portadora de 440 Hz quedaba SUPRIMIDA y aparecian dos
    bandas laterales a 420 y 453 Hz. Sonaba a modulacion en anillo, no a octava.

    La solucion estandar es un crossfade CORTO y de amplitud constante: la mayor
    parte del grano suena una sola cabeza limpia, y solo durante el cruce (15 %
    aqui) hay dos sonando. La interferencia sigue existiendo, pero dura poco y
    no define el timbre.

    ARTEFACTOS, HONESTAMENTE
    ------------------------
    Un shifter granular no es transparente. Queda un temblor a la frecuencia de
    envoltura de grano que en material sostenido se oye como un ligero coro
    metalico. Es inherente al metodo: los shifters transparentes usan analisis
    de fase (phase vocoder), que cuesta ordenes de magnitud mas CPU y no cabe
    dentro de un lazo de realimentacion.

    Para shimmer esto no molesta. El sonido shimmer ES un poco irreal.
*/
class PitchShifter
{
public:
    void prepare (double sampleRate, float grainMs = 60.0f)
    {
        grainLen = std::max (64, static_cast<int> (grainMs * 0.001f
                                                 * static_cast<float> (sampleRate)));
        xfade    = std::max (16, grainLen * 15 / 100);   // 15 % de cruce
        bufLen   = grainLen * 4;
        buf.assign (static_cast<size_t> (bufLen), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        writePos = 0;
        d = 0.0f;
    }

    /** Desplazamiento en semitonos. 0 = sin cambio. */
    void setSemitones (float st) noexcept
    {
        rate = std::pow (2.0f, std::clamp (st, -24.0f, 24.0f) / 12.0f);
    }

    float process (float x) noexcept
    {
        buf[static_cast<size_t> (writePos)] = x;

        // La distancia entre lectura y escritura deriva; al envolverse, la
        // ventana del grano vale exactamente 0, asi que no hay discontinuidad.
        d += 1.0f - rate;
        while (d <  0.0f)                            d += static_cast<float> (grainLen);
        while (d >= static_cast<float> (grainLen))   d -= static_cast<float> (grainLen);

        const float gl = static_cast<float> (grainLen);
        const float xf = static_cast<float> (xfade);

        // Fuera del cruce suena UNA sola cabeza, limpia. Durante el cruce se
        // mezcla linealmente con la cabeza anterior (un grano por detras).
        float out;
        if (d < xf)
        {
            const float g = d / xf;                    // 0 -> 1
            out = g * readFrac (d + kBase)
                + (1.0f - g) * readFrac (d + gl + kBase);
        }
        else
        {
            out = readFrac (d + kBase);
        }

        if (++writePos >= bufLen)
            writePos = 0;

        return out;
    }

private:
    float readFrac (float back) const noexcept
    {
        float p = static_cast<float> (writePos) - back;
        while (p <  0.0f)                          p += static_cast<float> (bufLen);
        while (p >= static_cast<float> (bufLen))   p -= static_cast<float> (bufLen);

        const int   i0   = static_cast<int> (p);
        const float frac = p - static_cast<float> (i0);
        int i1 = i0 + 1;
        if (i1 >= bufLen) i1 = 0;

        return buf[static_cast<size_t> (i0)]
             + frac * (buf[static_cast<size_t> (i1)] - buf[static_cast<size_t> (i0)]);
    }

    // Margen minimo para que la lectura nunca alcance a la escritura.
    static constexpr float kBase = 2.0f;

    std::vector<float> buf;
    int   bufLen = 0, grainLen = 0, xfade = 0, writePos = 0;
    float d = 0.0f, rate = 1.0f;
};
