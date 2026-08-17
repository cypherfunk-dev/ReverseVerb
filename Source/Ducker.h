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
*/
class Ducker
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        setRelease (200.0f);
        att = coef (5.0f);
        env = 0.0f;
    }

    void setRelease (float ms) noexcept { rel = coef (ms); }
    void reset() noexcept { env = 0.0f; }

    /** amount 0..1. Devuelve el factor de ganancia a aplicar al procesado. */
    float process (float inputAbs, float amount) noexcept
    {
        return 1.0f - amount * follow (inputAbs);
    }

    /** Avanza la envolvente y devuelve su valor normalizado 0..1.
        Se expone aparte porque la misma envolvente sirve para dos cosas
        distintas: atenuar el efecto (ducking) y empujar el drive del lazo. */
    float follow (float inputAbs) noexcept
    {
        env += (inputAbs > env ? att : rel) * (inputAbs - env);
        return std::min (1.0f, env * 3.0f);
    }

private:
    float coef (float ms) const noexcept
    {
        return 1.0f - std::exp (-1.0f / (0.001f * std::max (0.1f, ms)
                                       * static_cast<float> (sampleRate)));
    }

    double sampleRate = 44100.0;
    float att = 0.1f, rel = 0.001f, env = 0.0f;
};
