#pragma once

#include "ReverseDelay.h"   // reutiliza OnePole
#include <algorithm>
#include <cmath>
#include <vector>

/**
    Delay estereo con ping-pong, filtro y saturacion suave (tanh) en el lazo.

    TIEMPO FRACCIONARIO Y SUAVIZADO
    -------------------------------
    El tiempo de retardo se lee con interpolacion lineal y se suaviza con un
    polo. Al mover el knob, el delay hace un barrido de tono estilo cinta en
    vez de un click. Es la solucion clasica al problema de cambiar el tiempo en
    caliente, y ademas suena bien, asi que no hay razon para hacer otra cosa.

    ESTABILIDAD
    -----------
    A diferencia del reverse, aqui cada muestra se lee UNA sola vez y sin
    ventana: la ganancia del lazo es exactamente fb. Con fb<1 es
    incondicionalmente estable, por eso el techo (0.95) puede ser mucho mas
    alto que el del reverse (0.60).

    El ping-pong cruza la realimentacion entre canales. Eso no empeora la
    estabilidad: el viaje de ida y vuelta L->R->L tiene ganancia fb^2, o sea
    menos que el lazo directo.
*/
class StereoDelay
{
public:
    static constexpr float maxFeedback = 0.95f;

    /** initialDelaySamples evita un glide al arrancar: sin el, `current`
        empezaria en maxDelay/2 y barreria hasta el valor pedido, lo que se oye
        como un sweep de cinta en la primera nota tras cargar el plugin. */
    void prepare (int maxDelaySamples, double sr, int numChannels, float initialDelaySamples)
    {
        sampleRate = sr;
        maxDelay   = std::max (2, maxDelaySamples);
        bufLen     = maxDelay + 4;
        channels   = std::clamp (numChannels, 1, 2);

        for (int c = 0; c < 2; ++c)
            buf[c].assign (static_cast<size_t> (bufLen), 0.0f);

        // constante de tiempo ~60 ms para el glide del retardo
        glide  = 1.0f - std::exp (-1.0f / (0.060f * static_cast<float> (sampleRate)));
        target = current = std::clamp (initialDelaySamples, 2.0f, static_cast<float> (maxDelay));

        setTone (20.0f, 20000.0f);
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (buf[c].begin(), buf[c].end(), 0.0f);
            hp[c].reset();
            lp[c].reset();
        }
        writePos = 0;
        current  = target;
        mod[0] = mod[1] = 0.0f;
    }

    void setDelaySamples (float s) noexcept
    {
        target = std::clamp (s, 2.0f, static_cast<float> (maxDelay));
    }

    void setTone (float hpHz, float lpHz) noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            hp[c].setCutoff (hpHz, sampleRate);
            lp[c].setCutoff (lpHz, sampleRate);
        }
    }

    void setPingPong (bool b) noexcept { pingPong = b; }

    /** Desplazamiento de la lectura en muestras, por canal. Se aplica DESPUES
        del suavizado del tiempo: el glide sirve para que mover el knob no
        chasquee, pero filtraria por completo una modulacion de varios Hz.

        L y R por separado a proposito: desfasarlos 90 grados abre la imagen
        estereo, que es la mitad de la gracia de un chorus. */
    void setModulation (float modL, float modR) noexcept
    {
        mod[0] = modL;
        mod[1] = modR;
    }
    /** Procesa un par de muestras in-place. Si es mono, r se ignora. */
    void process (float& l, float& r, float feedback) noexcept
    {
        current += (target - current) * glide;

        float rd[2] = { 0.0f, 0.0f };
        for (int c = 0; c < channels; ++c)
        {
            const float dist = std::clamp (current + mod[c],
                                           2.0f, static_cast<float> (maxDelay));
            rd[c] = readFrac (c, dist);
        }

        const float fb   = std::clamp (feedback, 0.0f, maxFeedback);
        const float in[2] = { l, channels > 1 ? r : l };

        for (int c = 0; c < channels; ++c)
        {
            // Ping-pong: la realimentacion entra por el canal contrario, asi
            // las repeticiones van alternando de lado.
            const int src = (pingPong && channels > 1) ? (1 - c) : c;

            float f = fb * rd[src];
            f = f - hp[c].lowpass (f);        // paso alto
            f = lp[c].lowpass (f);            // paso bajo
            f = std::tanh (f);                // saturacion suave, ganancia max 1

            // Un NaN que entre una sola vez (del host, de un filtro) se
            // quedaria dando vueltas en el lazo para siempre. Se corta aqui,
            // en lo unico que se escribe al buffer.
            float w = in[c] + f;
            if (! std::isfinite (w)) w = 0.0f;
            buf[c][static_cast<size_t> (writePos)] = w;
        }

        if (++writePos >= bufLen)
            writePos = 0;

        l = rd[0];
        if (channels > 1)
            r = rd[1];
    }

private:
    float readFrac (int c, float d) const noexcept
    {
        float rp = static_cast<float> (writePos) - d;
        while (rp < 0.0f) rp += static_cast<float> (bufLen);

        const int   i0   = static_cast<int> (rp);
        const float frac = rp - static_cast<float> (i0);
        int i1 = i0 + 1;
        if (i1 >= bufLen) i1 -= bufLen;

        const auto& b = buf[static_cast<size_t> (c)];
        return b[static_cast<size_t> (i0)]
             + frac * (b[static_cast<size_t> (i1)] - b[static_cast<size_t> (i0)]);
    }

    std::vector<float> buf[2];
    OnePole hp[2], lp[2];

    double sampleRate = 44100.0;
    int bufLen = 0, maxDelay = 0, writePos = 0, channels = 2;
    float target = 0.0f, current = 0.0f, glide = 0.01f;
    float mod[2] = { 0.0f, 0.0f };
    bool pingPong = false;
};
