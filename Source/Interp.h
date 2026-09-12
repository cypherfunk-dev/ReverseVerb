#pragma once

#include <cmath>
#include <vector>

/**
    Interpolacion de Hermite (Catmull-Rom) de 4 puntos para lecturas
    fraccionarias de un buffer circular.

    POR QUE CUBICA Y NO LINEAL
    --------------------------
    La lineal es exacta en los enteros y por eso el reverse a rate=1 sin
    modulacion suena identico con cualquiera de las dos (hay un test bit a bit
    que lo comprueba). Pero en cuanto la posicion de lectura cae entre muestras
    -wow, flutter, detune, shimmer, el chorus del delay- la lineal actua como
    un paso bajo que va y viene con la fase fraccionaria: apaga agudos y mete
    un ligero "rasposo" modulado. La cubica reduce ese error en un orden de
    magnitud por cuatro multiplicaciones mas por muestra.

    Hermite en t=0 devuelve x0 EXACTO (c0 = x0), asi que en posiciones enteras
    sigue siendo transparente.

    Se necesitan las muestras en i-1, i, i+1, i+2. Quien llama es responsable
    de que esas cuatro sean validas (no pisen la escritura); ver las notas en
    cada motor.
*/
inline float hermite4 (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

/** Lee `buf` (circular, de `bufLen` muestras) en la posicion fraccionaria
    `p`, que ya tiene que estar en [0, bufLen). */
inline float readHermite (const std::vector<float>& buf, int bufLen, float p) noexcept
{
    const int   i0   = static_cast<int> (p);
    const float frac = p - static_cast<float> (i0);

    int im1 = i0 - 1; if (im1 <  0)      im1 += bufLen;
    int i1  = i0 + 1; if (i1  >= bufLen) i1  -= bufLen;
    int i2  = i0 + 2; if (i2  >= bufLen) i2  -= bufLen;

    return hermite4 (buf[static_cast<size_t> (im1)],
                     buf[static_cast<size_t> (i0)],
                     buf[static_cast<size_t> (i1)],
                     buf[static_cast<size_t> (i2)],
                     frac);
}
