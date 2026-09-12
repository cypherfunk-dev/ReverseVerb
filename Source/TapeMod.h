#pragma once

#include <cmath>

/**
    Wow y flutter de cinta.

    QUE MODULA
    ----------
    Devuelve un desplazamiento en MUESTRAS que se suma a la posicion de lectura.
    Modular la posicion de lectura es modular la afinacion: es lo mismo que hace
    una cinta cuando el motor no gira perfectamente constante.

    POR QUE VARIAS SINUSOIDES
    -------------------------
    Un solo LFO senoidal suena a vibrato de sintetizador, no a cinta. La cinta
    real tiene varias componentes a frecuencias no relacionadas entre si, lo que
    hace que el patron no se repita de forma reconocible. Aqui hay dos por cada
    banda, con frecuencias inconmensurables (0.53/0.81 y 7.3/11.7): el batido
    entre ellas tarda minutos en repetirse.

    PROFUNDIDAD EN CENTS, NO EN MUESTRAS
    ------------------------------------
    La desviacion de afinacion que produce modular un retardo depende de la
    DERIVADA del desplazamiento, no de su amplitud: un LFO lento necesita mucha
    mas profundidad que uno rapido para desafinar lo mismo. Por eso la
    profundidad se especifica en cents y se convierte a muestras dividiendo por
    la frecuencia del LFO. Asi "wow al 50 %" desafina lo mismo a cualquier
    sample rate y suena igual en 44.1 que en 96 kHz.
*/
class TapeMod
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;

        // cents -> ratio -> muestras, dividiendo por la frecuencia del LFO.
        // Se calcula aqui y no por muestra: son dos pow() que no cambian nunca.
        auto depthSamples = [sr] (float cents, float hz)
        {
            const float ratio = std::pow (2.0f, cents / 1200.0f) - 1.0f;
            return ratio * static_cast<float> (sr) / (6.28318530718f * hz);
        };
        wowDepth  = depthSamples (kWowMaxCents,  kWowHz1);
        flutDepth = depthSamples (kFlutMaxCents, kFlutHz1);

        reset();
    }

    void reset() noexcept
    {
        // Fases iniciales distintas para que no arranquen todas alineadas.
        p1 = 0.0f; p2 = 1.7f; p3 = 3.1f; p4 = 5.2f;
    }

    /** wow y flutter normalizados 0..1. Devuelve el offset en muestras. */
    float next (float wow, float flutter) noexcept
    {
        const float sr = static_cast<float> (sampleRate);
        const float twoPi = 6.28318530718f;

        auto advance = [&] (float& ph, float hz)
        {
            ph += twoPi * hz / sr;
            if (ph >= twoPi) ph -= twoPi;
            return std::sin (ph);
        };

        const float w1 = advance (p1, kWowHz1);
        const float w2 = advance (p2, kWowHz2);
        const float f1 = advance (p3, kFlutHz1);
        const float f2 = advance (p4, kFlutHz2);

        const float wowS  = wow     * wowDepth;
        const float flutS = flutter * flutDepth;

        return wowS  * (0.65f * w1 + 0.35f * w2)
             + flutS * (0.60f * f1 + 0.40f * f2);
    }

private:
    // Frecuencias inconmensurables a proposito: el patron no se repite.
    static constexpr float kWowHz1  = 0.53f,  kWowHz2  = 0.81f;
    static constexpr float kFlutHz1 = 7.30f,  kFlutHz2 = 11.70f;

    // Topes realistas de una cinta en mal estado.
    static constexpr float kWowMaxCents  = 45.0f;
    static constexpr float kFlutMaxCents = 14.0f;

    double sampleRate = 44100.0;
    float wowDepth = 0.0f, flutDepth = 0.0f;   // muestras a profundidad maxima
    float p1 = 0.0f, p2 = 1.7f, p3 = 3.1f, p4 = 5.2f;
};
