#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "Interp.h"
#include "PitchShifter.h"

/**
    Reverb de placa (Dattorro, "Effect Design Part 1", JAES 1997).

    POR QUE ESTE Y NO juce::Reverb
    ------------------------------
    juce::Reverb es Freeverb: ocho combs y cuatro allpass sin modulacion. En
    colas cortas va bien; en colas largas -que es lo que pide un plugin
    ambiental- los combs sin modular se oyen metalicos y con "ping" de modos
    fijos. Ademas no tiene pre-delay y no se puede abrir: el shimmer canonico
    (pitch shift DENTRO del lazo del reverb) era imposible.

    ESTRUCTURA
    ----------
    entrada -> pre-delay -> paso bajo de ancho de banda -> 4 allpass de
    difusion -> TANQUE.

    El tanque es un "8": dos mitades que se realimentan cruzadas. Cada mitad
    es: allpass MODULADO -> delay largo -> paso bajo (damping) -> x decay ->
    [shimmer] -> allpass -> delay largo -> a la otra mitad. Las salidas L y R
    son sumas de taps repartidos por las cuatro lineas largas y los dos
    allpass, con signos alternos: por eso salen decorrelacionadas y la
    imagen es ancha sin hacer nada mas.

    Los allpass modulados son lo que hace que la cola no suene a tubo: mueven
    los modos del tanque despacio, y una cola larga se vuelve lisa en vez de
    metalica. Es el "Mod" del panel.

    ESTEREO
    -------
    El diseno original suma L+R a mono y lo mete en las dos mitades. Aqui L
    entra en la mitad izquierda y R en la derecha. Con entrada mono es
    identico al original (las dos mitades reciben lo mismo); con entrada
    estereo -Detune, ping-pong- la cola conserva la anchura de la entrada
    ademas de la suya propia.

    SHIMMER CANONICO
    ----------------
    Un pitch shifter por mitad, entre el x decay y el segundo allpass. Cada
    vuelta del tanque sube (o baja) shimPitch semitonos y las octavas se van
    apilando sobre una cola que sigue siendo una cola de reverb, que es el
    sonido Eventide/Strymon. Es distinto del shimmer del reverse, que apila
    octavas sobre el swell invertido; los dos conviven.

    LONGITUDES
    ----------
    Las del paper estan a 29761 Hz. Se escalan a la frecuencia de muestreo en
    prepare(), incluidos los taps de salida y la excursion de modulacion, asi
    que el reverb suena igual a 44.1 que a 96 kHz.

    DECAY
    -----
    Size 0..1 se mapea a decay 0.25 + 0.65 * size^1.6. Con cada mitad del
    tanque durando ~0.275 s, eso da RT60 de ~1.3 s (size 0), ~3.5 s (size
    0.7) y ~17 s (size 1). Elegido para que los presets existentes, afinados
    con Freeverb, queden en un rango parecido.
*/
class PlateReverb
{
public:
    void prepare (double sampleRate)
    {
        sr    = sampleRate;
        scale = static_cast<float> (sampleRate / 29761.0);

        const int preMax = static_cast<int> (0.2 * sampleRate) + 8;   // 200 ms
        preL.prepare (preMax);
        preR.prepare (preMax);
        preGlide = 1.0f - std::exp (-1.0f / (0.06f * static_cast<float> (sampleRate)));

        // Difusion de entrada
        inApL[0].prepare (L (142), 0.75f);  inApR[0].prepare (L (142), 0.75f);
        inApL[1].prepare (L (107), 0.75f);  inApR[1].prepare (L (107), 0.75f);
        inApL[2].prepare (L (379), 0.625f); inApR[2].prepare (L (379), 0.625f);
        inApL[3].prepare (L (277), 0.625f); inApR[3].prepare (L (277), 0.625f);

        // Tanque. Los allpass modulados llevan margen para la excursion.
        modExcursion = kMaxExcursion * scale;
        modApL.prepare (L (672) + static_cast<int> (modExcursion) + 4, -0.7f);
        modApR.prepare (L (908) + static_cast<int> (modExcursion) + 4, -0.7f);
        modApL.n = L (672);
        modApR.n = L (908);

        delL1.prepare (L (4453));  delR1.prepare (L (4217));
        apL.prepare   (L (1800), 0.5f);
        apR.prepare   (L (2656), 0.5f);
        delL2.prepare (L (3720));  delR2.prepare (L (3163));

        shimL.prepare (sampleRate);
        shimR.prepare (sampleRate);

        // Ancho de banda de entrada: ~12 kHz, escala-invariante.
        bwCoef = 1.0f - std::exp (-2.0f * kPi * 12000.0f / static_cast<float> (sampleRate));

        // Paso bajo de la rama del shimmer: cuatro polos a 5 kHz (-24 dB/oct).
        // Sin el, con shimmer alto y poco damping cada vuelta sube una octava
        // y en dos segundos la cola es siseo a 14 kHz (medido). Con un polo
        // seguia en 12.7 kHz y con dos el equilibrio quedaba en 7 kHz (solo
        // -3.8 dB por vuelta ahi): contra una octava por vuelta hace falta
        // pendiente de verdad. Con cuatro a 6 kHz se apilaba en la esquina
        // (56 % de la energia en 7 kHz). A 5 kHz: -17 dB por vuelta en 7 kHz,
        // -6.5 en 3.5 kHz, asi que la energia se queda en 2-4 kHz: aire, no
        // ruido. Es lo que hace que un shimmer suene sedoso.
        shimLpCoef = 1.0f - std::exp (-2.0f * kPi * 5000.0f / static_cast<float> (sampleRate));

        setDamping (damping01);

        lfoInc = 2.0f * kPi / static_cast<float> (sampleRate);

        // Taps de salida (paper, escalados)
        tL[0] = L (266);  tL[1] = L (2974); tL[2] = L (1913); tL[3] = L (1996);
        tL[4] = L (1990); tL[5] = L (187);  tL[6] = L (1066);
        tR[0] = L (353);  tR[1] = L (3627); tR[2] = L (1228); tR[3] = L (2673);
        tR[4] = L (2111); tR[5] = L (335);  tR[6] = L (121);

        setPreDelayMs (0.0f);
        preCur = preTarget;
        reset();
    }

    void reset() noexcept
    {
        preL.clear(); preR.clear();
        for (auto& a : inApL) a.line.clear();
        for (auto& a : inApR) a.line.clear();
        modApL.line.clear(); modApR.line.clear();
        delL1.clear(); delR1.clear(); delL2.clear(); delR2.clear();
        apL.line.clear(); apR.line.clear();
        shimL.reset(); shimR.reset();
        bwL = bwR = 0.0f;
        dampL = dampR = 0.0f;
        shimLpL.reset(); shimLpR.reset();
        fbL = fbR = 0.0f;
        lfo1 = 0.0f; lfo2 = 1.5708f;
    }

    /** size 0..1 -> decay del tanque (ver nota DECAY). */
    void setSize (float size) noexcept
    {
        const float s = std::clamp (size, 0.0f, 1.0f);
        decay = 0.25f + 0.65f * std::pow (s, 1.6f);
    }

    /** 0 = brillante, 1 = muy oscuro. Se define como frecuencia de corte
        (20 kHz -> 1 kHz, logaritmico) y no como coeficiente por muestra, para
        que oscurezca lo mismo a 44.1 que a 96 kHz. */
    void setDamping (float d) noexcept
    {
        damping01 = std::clamp (d, 0.0f, 1.0f);
        const float fc = 20000.0f * std::pow (0.05f, damping01);
        dampCoef = 1.0f - std::exp (-2.0f * kPi * fc / static_cast<float> (sr));
    }

    void setPreDelayMs (float ms) noexcept
    {
        if (preL.len < 16)
            return;   // antes de prepare() no hay linea

        const float maxS = static_cast<float> (preL.len - 8);
        preTarget = std::clamp (ms * 0.001f * static_cast<float> (sr), 3.0f, maxS);
    }

    /** Profundidad de los allpass modulados, 0..1. */
    void setModulation (float m) noexcept { modAmount = std::clamp (m, 0.0f, 1.0f); }

    void setShimmer (float amount, float semitones) noexcept
    {
        shimAmount = std::clamp (amount, 0.0f, 1.0f);
        shimL.setSemitones (semitones);
        shimR.setSemitones (semitones);
    }

    /** Procesa un par de muestras. Devuelve SOLO la senal humeda. */
    void process (float& l, float& r) noexcept
    {
        // --- pre-delay con glide (cambiarlo en caliente barre, no chasca) ---
        preCur += preGlide * (preTarget - preCur);
        float xl = preL.readFrac (preCur);
        float xr = preR.readFrac (preCur);
        preL.write (l);
        preR.write (r);

        // --- ancho de banda + difusion ---
        bwL += bwCoef * (xl - bwL);  xl = bwL;
        bwR += bwCoef * (xr - bwR);  xr = bwR;
        for (auto& a : inApL) xl = a.process (xl);
        for (auto& a : inApR) xr = a.process (xr);

        // --- LFOs de los allpass modulados (cuadratura, ~1 Hz) ---
        lfo1 += lfoInc * 0.93f; if (lfo1 >= 2.0f * kPi) lfo1 -= 2.0f * kPi;
        lfo2 += lfoInc * 1.07f; if (lfo2 >= 2.0f * kPi) lfo2 -= 2.0f * kPi;
        const float exc = modAmount * modExcursion;
        const float excL = exc * std::sin (lfo1);
        const float excR = exc * std::sin (lfo2);

        // --- tanque: mitad izquierda (con lo que salio de la derecha) ---
        float a = xl + fbR;
        if (! std::isfinite (a)) a = 0.0f;
        a = modApL.processMod (a, excL);
        a = delL1.process (a);
        dampL += dampCoef * (a - dampL);  a = dampL;
        a *= decay;
        if (shimAmount > 0.0001f)
            a += shimAmount * (shimLpL.process (shimL.process (a), shimLpCoef) - a);
        a = apL.process (a);
        a = delL2.process (a);
        fbL = a;

        // --- mitad derecha (con lo que acaba de salir de la izquierda) ---
        float b = xr + fbL;
        if (! std::isfinite (b)) b = 0.0f;
        b = modApR.processMod (b, excR);
        b = delR1.process (b);
        dampR += dampCoef * (b - dampR);  b = dampR;
        b *= decay;
        if (shimAmount > 0.0001f)
            b += shimAmount * (shimLpR.process (shimR.process (b), shimLpCoef) - b);
        b = apR.process (b);
        b = delR2.process (b);
        fbR = b;

        // --- taps de salida (paper) ---
        const float yl = delR1.read (tL[0]) + delR1.read (tL[1]) - apR.line.read (tL[2])
                       + delR2.read (tL[3]) - delL1.read (tL[4]) - apL.line.read (tL[5])
                       - delL2.read (tL[6]);
        const float yr = delL1.read (tR[0]) + delL1.read (tR[1]) - apL.line.read (tR[2])
                       + delL2.read (tR[3]) - delR1.read (tR[4]) - apR.line.read (tR[5])
                       - delR2.read (tR[6]);

        l = kOutGain * yl;
        r = kOutGain * yr;
    }

private:
    static constexpr float kPi = 3.14159265358979f;
    static constexpr float kMaxExcursion = 32.0f;   // muestras a 29761 Hz, a Mod = 100 %

    // Ganancia de salida: 0.6 es el del paper. El 0.85 iguala la energia
    // total con la que daba juce::Reverb al mismo porcentaje (medido con un
    // burst de ruido de 1 s en 9 combinaciones de size/damp: la placa salia
    // entre un 10 y un 20 % mas fuerte), para que los presets no cambien de
    // volumen al cambiar de motor. La cola en si queda ~1.5x mas presente
    // que en Freeverb: es una placa, decae mas despacio, y es a proposito.
    static constexpr float kOutGain = 0.6f * 0.85f;

    int L (int base) const noexcept
    {
        return std::max (4, static_cast<int> (static_cast<float> (base) * scale + 0.5f));
    }

    /** Linea de retardo circular. Se lee ANTES de escribir en cada muestra,
        asi que read(len) es la muestra de hace exactamente len. */
    struct Line
    {
        std::vector<float> buf;
        int len = 0, pos = 0;

        void prepare (int n)
        {
            len = std::max (4, n) + 4;   // +4: margen para Hermite (i+2)
            buf.assign (static_cast<size_t> (len), 0.0f);
            pos = 0;
        }

        void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); }

        float read (int back) const noexcept
        {
            int i = pos - back;
            if (i < 0) i += len;
            return buf[static_cast<size_t> (i)];
        }

        float readFrac (float back) const noexcept
        {
            float p = static_cast<float> (pos) - back;
            while (p < 0.0f) p += static_cast<float> (len);
            return readHermite (buf, len, p);
        }

        void write (float x) noexcept
        {
            buf[static_cast<size_t> (pos)] = x;
            if (++pos >= len) pos = 0;
        }
    };

    /** Delay puro de longitud fija n: devuelve lo de hace n y guarda x. */
    struct Delay
    {
        Line line;
        int  n = 0;

        void prepare (int len) { n = len; line.prepare (len); }
        void clear() noexcept  { line.clear(); }
        float read (int back) const noexcept { return line.read (back); }

        float process (float x) noexcept
        {
            const float y = line.read (n);
            line.write (x);
            return y;
        }
    };

    /** Allpass de Schroeder: v = x + g*d; y = d - g*v. Modulo 1 a todas las
        frecuencias. Los taps intermedios (para la salida) se leen de `line`. */
    struct Allpass
    {
        Line  line;
        int   n = 0;
        float g = 0.0f;

        void prepare (int len, float coef) { n = len; g = coef; line.prepare (len); }

        float process (float x) noexcept
        {
            const float d = line.read (n);
            const float v = x + g * d;
            line.write (v);
            return d - g * v;
        }

        /** Con la lectura desplazada `exc` muestras (fraccionario). */
        float processMod (float x, float exc) noexcept
        {
            const float d = line.readFrac (static_cast<float> (n) + exc);
            const float v = x + g * d;
            line.write (v);
            return d - g * v;
        }
    };

    double sr    = 44100.0;
    float  scale = 1.0f;

    Line  preL, preR;
    float preTarget = 3.0f, preCur = 3.0f, preGlide = 0.001f;

    float bwCoef = 0.5f, bwL = 0.0f, bwR = 0.0f;
    Allpass inApL[4], inApR[4];

    Allpass modApL, modApR;
    Delay   delL1, delR1, delL2, delR2;
    Allpass apL, apR;
    float   dampL = 0.0f, dampR = 0.0f;
    float   fbL = 0.0f, fbR = 0.0f;

    PitchShifter shimL, shimR;
    float shimAmount = 0.0f, shimLpCoef = 0.5f;

    /** Cuatro polos en cascada, mismo coeficiente. */
    struct Lp4
    {
        float z[4] {};
        void  reset() noexcept { z[0] = z[1] = z[2] = z[3] = 0.0f; }
        float process (float x, float g) noexcept
        {
            for (auto& s : z) { s += g * (x - s); x = s; }
            return x;
        }
    };
    Lp4 shimLpL, shimLpR;

    float decay = 0.5f, damping01 = 0.4f, dampCoef = 0.5f, modAmount = 0.0f;
    float modExcursion = 32.0f;
    float lfo1 = 0.0f, lfo2 = 1.5708f, lfoInc = 0.0f;

    int tL[7] {}, tR[7] {};
};
