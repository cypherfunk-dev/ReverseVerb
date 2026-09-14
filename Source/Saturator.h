#pragma once

#include <algorithm>
#include <cmath>

/**
    Saturador tanh(d*x)/d con oversampling 2x, para usar DENTRO de un lazo de
    realimentacion muestra a muestra.

    POR QUE
    -------
    tanh(8*x) sobre una senal a 4 kHz genera armonicos impares hasta el 11 y
    mas alla (es casi una onda cuadrada). A 44.1 kHz el 7o (28 kHz) se pliega
    a 16 kHz, el 9o (36 kHz) a 8 kHz y el 11o (44 kHz) a 100 Hz, todos a
    -17..-21 dB de la fundamental: basura inarmonica que ademas vuelve a
    entrar al lazo. Medido en la suite (testSaturatorAliasing).

    POR QUE NO juce::dsp::Oversampling
    ----------------------------------
    Trabaja por bloques; aqui el saturador esta dentro de un lazo que se cierra
    en cada muestra. Habria que correr el motor entero a 2x, con el doble de
    memoria y de CPU en todo, para arreglar un solo nodo.

    COMO
    ----
    Subida a 2x: la muestra alineada y un punto medio interpolado con
    Catmull-Rom ([-1 9 9 -1]/16). tanh en las dos. Bajada a 1x: half-band con
    el mismo nucleo (1/2 en la alineada, [-1 9 9 -1]/32 en los cuatro medios
    puntos vecinos). Es un half-band corto, no un FIR de 64 taps: rebaja los
    aliasing del orden de 20 dB por cuatro multiplicaciones mas por muestra,
    que es lo que se puede pagar dentro del lazo.

    Introduce 3 muestras de retardo (2 de la subida, 1 de la bajada: las dos
    necesitan una muestra de "futuro"). Frente a una ventana de cientos de ms
    es nada, y el lazo ya tenia miles.

    GANANCIA
    --------
    tanh(d*x)/d tiene ganancia maxima 1 para cualquier d, y los dos nucleos
    suman 1 con respuesta que no supera la unidad, asi que la cadena tampoco:
    el techo de estabilidad del lazo no cambia (lo comprueba la suite).
*/
class Saturator
{
public:
    void reset() noexcept
    {
        x1 = x2 = x3 = 0.0f;
        aPrev = m1 = m2 = m3 = 0.0f;
    }

    /** d >= 1. */
    void setDrive (float d) noexcept { drive = std::max (1.0f, d); }

    float process (float x0) noexcept
    {
        // Subida: alineada = x[n-2]; medio punto entre x[n-2] y x[n-1], con
        // x[n-3] y x[n] de vecinos.
        const float mid = (-x3 + 9.0f * x2 + 9.0f * x1 - x0) * (1.0f / 16.0f);
        const float a   = std::tanh (drive * x2)  / drive;
        const float m   = std::tanh (drive * mid) / drive;
        x3 = x2; x2 = x1; x1 = x0;

        // Bajada, centrada en la alineada ANTERIOR (aPrev), que ya tiene sus
        // dos medios puntos por cada lado: m3 m2 [aPrev] m1 m
        const float y = 0.5f * aPrev + (-m3 + 9.0f * m2 + 9.0f * m1 - m) * (1.0f / 32.0f);

        m3 = m2; m2 = m1; m1 = m; aPrev = a;
        return y;
    }

private:
    float drive = 1.0f;
    float x1 = 0.0f, x2 = 0.0f, x3 = 0.0f;          // historial de entrada
    float aPrev = 0.0f, m1 = 0.0f, m2 = 0.0f, m3 = 0.0f;   // flujo 2x saturado
};
