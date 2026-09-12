#pragma once

#include <algorithm>
#include <cmath>

/**
    Envolvente sobre la senal SECA que atenua el efecto mientras hay entrada.

    Es lo que separa un reverse reverb de juguete de uno usable: sin esto, el
    swell tapa justo la nota que lo provoca. Con esto, el efecto se aparta
    mientras tocas y crece en los huecos.

    Ataque rapido (que se aparte de inmediato) y release ajustable (que vuelva
    al ritmo de la frase).

    DOS LECTURAS DE LA MISMA ENVOLVENTE
    -----------------------------------
    follow() devuelve la envolvente en escala ABSOLUTA: tocar fuerte es tocar
    cerca de 0 dBFS. Es lo que quiere Drive Env, porque ahi la dinamica real
    de la pulsacion es la gracia.

    process() (el ducking) la devuelve RELATIVA al pico reciente de la entrada.
    Antes usaba la absoluta con un x3 fijo, y eso hacia que "Duck 100 %"
    atenuase todo con una senal a -9 dBFS pero solo un tercio con una DI a
    -20 dBFS: dos usuarios con distinta ganancia de entrada decian que el Duck
    "hace mucho" o "no hace nada". Normalizar por un pico lento (release ~2 s)
    hace que el ducking dependa de CUANDO tocas y no de cuanto nivel entra.
    Hay un suelo a -26 dBFS para que el ruido de fondo no cuente como senal.
*/
class Ducker
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        setRelease (200.0f);
        att     = coef (5.0f);
        peakRel = coef (2000.0f);
        env  = 0.0f;
        peak = 0.0f;
    }

    void setRelease (float ms) noexcept { rel = coef (ms); }
    void reset() noexcept { env = 0.0f; peak = 0.0f; }

    /** amount 0..1. Devuelve el factor de ganancia a aplicar al procesado.
        Relativo al pico reciente de la entrada (ver nota de arriba). */
    float process (float inputAbs, float amount) noexcept
    {
        advance (inputAbs);

        if (inputAbs > peak) peak = inputAbs;
        else                 peak += peakRel * (inputAbs - peak);

        const float ref = std::max (kFloor, peak);
        return 1.0f - amount * std::min (1.0f, env / ref);
    }

    /** Avanza la envolvente y devuelve su valor normalizado 0..1 en escala
        ABSOLUTA (0 dBFS - 9.5 dB ya es "a tope"). La usa Drive Env, donde la
        dinamica real de la pulsacion es lo que se quiere seguir. */
    float follow (float inputAbs) noexcept
    {
        advance (inputAbs);
        return std::min (1.0f, env * 3.0f);
    }

private:
    void advance (float inputAbs) noexcept
    {
        env += (inputAbs > env ? att : rel) * (inputAbs - env);
    }

    float coef (float ms) const noexcept
    {
        return 1.0f - std::exp (-1.0f / (0.001f * std::max (0.1f, ms)
                                       * static_cast<float> (sampleRate)));
    }

    static constexpr float kFloor = 0.05f;   // -26 dBFS

    double sampleRate = 44100.0;
    float att = 0.1f, rel = 0.001f, peakRel = 0.0001f;
    float env = 0.0f, peak = 0.0f;
};
