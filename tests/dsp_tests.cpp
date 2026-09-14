//==============================================================================
//  Suite de tests del DSP de ReverseVerb.
//
//  Los motores (ReverseDelay, StereoDelay, Ducker, OnePole) son headers puros
//  sin JUCE, asi que esto compila y corre en segundos sin arrastrar el
//  framework. Ese es justo el motivo de haberlos mantenido independientes.
//
//  POR QUE EXISTE ESTO
//  -------------------
//  Los bugs que aparecieron durante el desarrollo NO se detectan de oido:
//    - Un saturador con ganancia oculta de +21 dB dentro del lazo.
//    - Un lazo que se autooscilaba y sostenia +10 dBFS indefinidamente.
//    - Un click de 1.30 de amplitud al mover un slider.
//  Los tres se manifiestan como "a veces suena raro y no se por que", y es
//  facil culpar a la interfaz o al DAW antes que al algoritmo.
//
//  USO
//  ---
//    ReverseVerbTests            barrido rapido (segundos)
//    ReverseVerbTests --full     barrido exhaustivo (minutos)
//
//  Devuelve codigo de salida != 0 si algo falla.
//==============================================================================

#include "ReverseDelay.h"
#include "StereoDelay.h"
#include "Ducker.h"
#include "TapeMod.h"
#include "PitchShifter.h"
#include "Interp.h"
#include "PlateReverb.h"
#include "Saturator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;

int  passed = 0, failed = 0;
bool fullSweep = false;

std::string f (double v, int dec = 5)
{
    char b[64];
    std::snprintf (b, sizeof b, "%.*f", dec, v);
    return b;
}

void check (const char* name, bool ok, const std::string& detail)
{
    std::printf ("%s  %-44s %s\n", ok ? "[ ok ]" : "[FAIL]", name, detail.c_str());
    std::fflush (stdout);
    (ok ? passed : failed)++;
}

/** El barrido exhaustivo tarda minutos. Sin esto pareceria colgado. */
void progress (int done, int total)
{
    if (! fullSweep) return;
    std::printf ("\r        barriendo combinacion %d/%d...", done, total);
    std::fflush (stdout);
}

void clearProgress()
{
    if (! fullSweep) return;
    std::printf ("\r%*s\r", 50, "");
    std::fflush (stdout);
}

void section (const char* title)
{
    std::printf ("\n--- %s %s\n", title, std::string (62 - std::strlen (title), '-').c_str());
    std::fflush (stdout);
}

/** Ruido reproducible: los tests tienen que dar el mismo resultado siempre. */
struct Noise
{
    unsigned s = 1;
    float next() { s = s * 1664525u + 1013904223u; return ((s >> 8) & 0xFFFF) / 32768.0f - 1.0f; }
};

//==============================================================================
//  REVERSE
//==============================================================================

/** Con un seno, una lectura invertida correcta sigue siendo un seno suave, asi
    que cualquier discontinuidad destaca. El techo teorico es la derivada del
    seno por sqrt(2), porque se suman dos granos. */
void testReverseClicks()
{
    const int sr = 48000, L = sr / 10, N = sr * 6;
    ReverseDelay d;
    d.prepare (sr * 2, L, sr);

    std::vector<float> y (N);
    for (int n = 0; n < N; ++n)
        y[n] = d.process (0.8f * (float) std::sin (2.0 * kPi * 100.0 * n / sr), 0.0f);

    const double theo = 0.8 * 2.0 * kPi * 100.0 / sr * std::sqrt (2.0);
    double mj = 0.0;
    for (int n = 3 * L + 1; n < N; ++n)
        mj = std::max (mj, (double) std::fabs (y[n] - y[n - 1]));

    check ("sin clicks en la costura", mj < theo * 1.5,
           "salto " + f (mj) + " <= " + f (theo * 1.5) + " (teorico " + f (theo) + ")");
}

/** La ventana sin/cos debe dar potencia constante: sin^2 + cos^2 = 1. */
void testReverseConstantPower()
{
    const int sr = 48000, L = sr / 10, N = sr * 6;
    ReverseDelay d;
    d.prepare (sr * 2, L, sr);
    Noise rng;

    std::vector<float> y (N);
    for (int n = 0; n < N; ++n)
        y[n] = d.process (0.5f * rng.next(), 0.0f);

    const int blk = L / 8;
    double mn = 1e9, mx = -1e9;
    for (int s = 3 * L; s + blk < N; s += blk)
    {
        double acc = 0.0;
        for (int i = 0; i < blk; ++i) acc += (double) y[s + i] * y[s + i];
        const double r = std::sqrt (acc / blk);
        mn = std::min (mn, r); mx = std::max (mx, r);
    }

    const double ripple = 20.0 * std::log10 (mx / mn);
    check ("potencia constante", ripple < 1.5, "rizado " + f (ripple, 2) + " dB < 1.50 dB");
}

/** Que de verdad invierta: correlacion contra la entrada invertida debe ganar
    con holgura a la correlacion contra la entrada directa. */
void testReverseActuallyReverses()
{
    const int sr = 48000, L = sr / 10, N = sr * 4;
    ReverseDelay d;
    d.prepare (sr * 2, L, sr);
    Noise rng;

    std::vector<float> x (N), y (N);
    for (int n = 0; n < N; ++n) x[n] = rng.next();
    for (int n = 0; n < N; ++n) y[n] = d.process (x[n], 0.0f);

    const int t0 = 3 * L, M = L / 2;
    std::vector<double> seg (M);
    double mean = 0.0;
    for (int i = 0; i < M; ++i) { seg[i] = y[t0 + i]; mean += seg[i]; }
    mean /= M;
    double segNorm = 0.0;
    for (int i = 0; i < M; ++i) { seg[i] -= mean; segNorm += seg[i] * seg[i]; }
    segNorm = std::sqrt (segNorm);

    auto bestCorr = [&] (bool reversed)
    {
        double best = 0.0;
        for (int lag = 0; lag < 2 * L; ++lag)
        {
            const int base = t0 - lag;
            if (base < 0) break;

            double m = 0.0;
            for (int i = 0; i < M; ++i) m += x[base + (reversed ? M - 1 - i : i)];
            m /= M;

            double dot = 0.0, nrm = 0.0;
            for (int i = 0; i < M; ++i)
            {
                const double w = x[base + (reversed ? M - 1 - i : i)] - m;
                dot += seg[i] * w; nrm += w * w;
            }
            nrm = std::sqrt (nrm);
            if (nrm > 0.0 && segNorm > 0.0)
                best = std::max (best, std::fabs (dot / (segNorm * nrm)));
        }
        return best;
    };

    const double rev = bestCorr (true), fwd = bestCorr (false);
    check ("invierte de verdad", rev > 0.5 && rev > 2.0 * fwd,
           "corr invertida " + f (rev, 3) + " vs directa " + f (fwd, 3));
}

/** Mover Length no debe producir un salto. Aqui vivia el peor bug del
    proyecto: reposicionar los dos granos a la vez daba un click de 1.30. */
void testReverseLengthChange()
{
    const int sr = 48000, L = sr / 10, N = sr * 6;
    ReverseDelay d;
    d.prepare (sr * 2, L, sr);

    std::vector<float> y (N);
    for (int n = 0; n < N; ++n)
    {
        if (n == N / 2) d.setLength (L * 3);
        y[n] = d.process (0.8f * (float) std::sin (2.0 * kPi * 100.0 * n / sr), 0.0f);
    }

    const double theo = 0.8 * 2.0 * kPi * 100.0 / sr * std::sqrt (2.0);
    double mj = 0.0;
    for (int n = N / 2 - L; n < N - 1; ++n)
        mj = std::max (mj, (double) std::fabs (y[n + 1] - y[n]));

    check ("sin glitch al cambiar Length", mj < theo * 1.6,
           "salto " + f (mj) + " <= " + f (theo * 1.6));
}

/** El lazo tiene ganancia > 1 por diseno (cada muestra se lee dos veces y la
    ventana es de potencia, no de amplitud). El clamp interno de 0.60 es lo que
    lo mantiene decayendo. Se pide 1.0 a proposito: el DSP debe capar solo. */
void testReverseStability()
{
    const int sr = 48000;
    const std::vector<float> hps = fullSweep ? std::vector<float>{20.f,200.f,2000.f} : std::vector<float>{20.f,200.f};
    const std::vector<float> lps = fullSweep ? std::vector<float>{200.f,2000.f,20000.f} : std::vector<float>{2000.f,20000.f};
    const std::vector<float> drs = fullSweep ? std::vector<float>{1.f,2.f,4.f,8.f} : std::vector<float>{1.f,8.f};
    const std::vector<int>   lns = fullSweep ? std::vector<int>{sr/20, sr/2, sr*2} : std::vector<int>{sr/20, sr/2};

    const int secs = fullSweep ? 62 : 32;
    const int tailFrom = (secs - 1) * sr;
    const int totalCombos = (int) (hps.size() * lps.size() * drs.size() * lns.size());
    int combo = 0;
    double worstTail = 0.0;
    bool   bad = false;

    for (float hp : hps) for (float lp : lps) for (float dr : drs) for (int L : lns)
    {
        progress (++combo, totalCombos);
        ReverseDelay d;
        d.prepare (sr * 2, L, sr);
        d.setFeedbackTone (hp, lp);
        d.setDrive (dr);

        Noise rng; double tail = 0.0;
        for (int n = 0; n < secs * sr; ++n)
        {
            const float x = n < sr * 2 ? rng.next() : 0.0f;
            const float y = d.process (x, 1.0f);          // se pide el maximo
            if (! std::isfinite (y)) bad = true;
            if (n >= tailFrom) tail = std::max (tail, (double) std::fabs (y));
        }
        worstTail = std::max (worstTail, tail);
    }
    clearProgress();

    check ("lazo decae con feedback al maximo", ! bad && worstTail < 0.02,
           "pico tras " + std::to_string (secs - 2) + " s de silencio: " + f (worstTail));
}

/** Freeze debe sostener indefinidamente SIN decaer y SIN crecer. Que no crezca
    es lo importante: es la diferencia entre congelar (no escribir) y subir el
    feedback a 1 (realimentar), que es lo que se autooscila. */
void testFreeze()
{
    const int sr = 48000, L = sr / 4;
    ReverseDelay d;
    d.prepare (sr * 2, L, sr);

    Noise rng;
    for (int n = 0; n < sr * 2; ++n) d.process (0.5f * rng.next(), 0.0f);

    d.setFrozen (true);

    double r1 = 0.0, r2 = 0.0; int c1 = 0, c2 = 0;
    bool bad = false;
    for (int n = 0; n < sr * 20; ++n)
    {
        const float y = d.process (0.0f, 0.0f);      // sin entrada
        if (! std::isfinite (y)) bad = true;
        if (n >= sr * 1  && n < sr * 2)  { r1 += (double) y * y; ++c1; }
        if (n >= sr * 19)                { r2 += (double) y * y; ++c2; }
    }
    r1 = std::sqrt (r1 / c1);
    r2 = std::sqrt (r2 / c2);

    const double drift = 20.0 * std::log10 (r2 / std::max (1e-12, r1));
    check ("freeze sostiene sin decaer ni crecer",
           ! bad && r1 > 0.01 && std::fabs (drift) < 0.5,
           "RMS " + f (r1, 4) + " -> " + f (r2, 4) + " tras 18 s (deriva "
           + f (drift, 3) + " dB)");
}

/** Con rate=1 y sin modulacion, la lectura fraccionaria debe caer en enteros
    exactos: la interpolacion no debe cambiar NADA del comportamiento anterior.
    Esta es la garantia de que la refactorizacion es transparente. */
void testFractionalTransparent()
{
    const int sr = 48000, L = sr / 10, N = sr * 4;

    auto run = [&] (bool withMod)
    {
        ReverseDelay d;
        d.prepare (sr * 2, L, sr);
        if (withMod) d.setModulation (0.0f);      // explicitamente neutro

        std::vector<float> y (N);
        Noise rng;
        for (int n = 0; n < N; ++n) y[n] = d.process (0.5f * rng.next(), 0.0f);
        return y;
    };

    const auto a = run (false), b = run (true);
    double maxDiff = 0.0;
    for (int n = 0; n < N; ++n) maxDiff = std::max (maxDiff, (double) std::fabs (a[n] - b[n]));

    check ("lectura fraccionaria transparente a rate=1", maxDiff == 0.0,
           "diferencia maxima " + f (maxDiff, 9));
}

/** Modular la posicion de lectura no debe producir clicks. Es el requisito para
    que wow y flutter sean utilizables. */
void testModulationNoClicks()
{
    const int sr = 48000, L = sr / 10, N = sr * 6;
    ReverseDelay d;
    d.prepare (sr * 2, L, sr);

    std::vector<float> y (N);
    bool bad = false;
    for (int n = 0; n < N; ++n)
    {
        // wow de 1 Hz con 133 muestras de profundidad (~30 cents a 48 kHz)
        d.setModulation (133.0f * (float) std::sin (2.0 * kPi * 1.0 * n / sr));
        y[n] = d.process (0.8f * (float) std::sin (2.0 * kPi * 100.0 * n / sr), 0.0f);
        if (! std::isfinite (y[n])) bad = true;
    }

    // El techo sube un poco respecto al caso sin modular: la modulacion cambia
    // la frecuencia instantanea, y con ella la pendiente maxima.
    const double theo = 0.8 * 2.0 * kPi * 100.0 / sr * std::sqrt (2.0);
    double mj = 0.0;
    for (int n = 3 * L + 1; n < N; ++n)
        mj = std::max (mj, (double) std::fabs (y[n] - y[n - 1]));

    check ("modulacion sin clicks", ! bad && mj < theo * 2.5,
           "salto " + f (mj) + " <= " + f (theo * 2.5));
}

/** rate desplaza la afinacion. Es la prueba de que la infraestructura de pitch
    shifting funciona: leyendo al doble de velocidad, un seno de 200 Hz debe
    salir a ~400 Hz. Se mide contando cruces por cero. */
void testReadRatePitch()
{
    const int sr = 48000, L = sr / 10, N = sr * 4;
    const double fIn = 200.0;

    auto measureHz = [&] (float rate)
    {
        ReverseDelay d;
        d.prepare (sr * 2, L, sr);
        d.setReadRate (rate);

        std::vector<float> y (N);
        for (int n = 0; n < N; ++n)
            y[n] = d.process ((float) std::sin (2.0 * kPi * fIn * n / sr), 0.0f);

        // Cruces por cero ascendentes en regimen estable, con umbral para no
        // contar ruido cerca de las costuras donde la ventana vale casi 0.
        int crossings = 0;
        const int from = 4 * L, to = N - L;
        for (int n = from + 1; n < to; ++n)
            if (y[n - 1] < -0.02f && y[n] >= -0.02f) ++crossings;

        return crossings * (double) sr / (double) (to - from);
    };

    const double h1 = measureHz (1.0f);
    const double h2 = measureHz (2.0f);

    const bool ok = std::fabs (h1 - fIn) < fIn * 0.15
                 && std::fabs (h2 - 2.0 * fIn) < 2.0 * fIn * 0.15;

    check ("rate desplaza la afinacion", ok,
           "rate 1.0 -> " + f (h1, 1) + " Hz, rate 2.0 -> " + f (h2, 1)
           + " Hz (entrada " + f (fIn, 0) + " Hz)");
}

/** Una modulacion POSITIVA no debe adelantar la lectura al arranque del grano:
    ahi la memoria contiene audio de hace bufLen muestras y saltar a el produce
    una discontinuidad.

    Este test nacio de un bug real: con +400 muestras de offset el salto maximo
    era 4.1 veces el techo teorico, siempre en la fase < offset del grano. Los
    tests de wow/flutter con profundidades realistas NO lo detectaban porque la
    ventana vale poco ahi y el artefacto quedaba enmascarado. */
void testModulationNeverReadsAhead()
{
    const int sr = 48000, L = sr / 10, N = sr * 8;

    auto maxJump = [&] (float mod)
    {
        ReverseDelay d;
        d.prepare (sr * 2, L, sr);

        std::vector<float> y (N);
        for (int n = 0; n < N; ++n)
        {
            d.setModulation (mod);
            y[n] = d.process (0.8f * (float) std::sin (2.0 * kPi * 100.0 * n / sr), 0.0f);
        }

        double mj = 0.0;
        for (int n = 4 * L + 1; n < N; ++n)
            mj = std::max (mj, (double) std::fabs (y[n] - y[n - 1]));
        return mj;
    };

    const double theo = 0.8 * 2.0 * kPi * 100.0 / sr * std::sqrt (2.0);
    const double mj   = maxJump (400.0f);      // offset positivo grande

    check ("modulacion positiva no lee por delante", mj < theo * 1.3,
           "salto " + f (mj) + " <= " + f (theo * 1.3) + " (sin el clamp: 0.06120)");
}

/** El modulador de cinta debe estar acotado y no repetirse de forma
    reconocible. Un patron corto suena a vibrato de sintetizador, no a cinta. */
void testTapeMod()
{
    const int sr = 48000;
    TapeMod t;
    t.prepare (sr);

    float peak = 0.0f;
    bool bad = false;
    std::vector<float> seq (sr * 30);
    for (int n = 0; n < (int) seq.size(); ++n)
    {
        seq[n] = t.next (1.0f, 1.0f);
        if (! std::isfinite (seq[n])) bad = true;
        peak = std::max (peak, std::fabs (seq[n]));
    }

    // Autocorrelacion a 1 s: si el patron se repitiera cada segundo, esto
    // saldria cerca de 1.
    const int lag = sr, M = sr * 5;
    double dot = 0.0, n1 = 0.0, n2 = 0.0;
    for (int i = 0; i < M; ++i)
    {
        const double a = seq[sr * 10 + i], b = seq[sr * 10 + i + lag];
        dot += a * b; n1 += a * a; n2 += b * b;
    }
    const double corr = std::fabs (dot / std::sqrt (std::max (1e-12, n1 * n2)));

    check ("wow/flutter acotado y no periodico",
           ! bad && peak < 400.0f && corr < 0.7,
           "pico " + f (peak, 1) + " muestras, autocorrelacion a 1 s " + f (corr, 3));
}

/** Detune debe mover la afinacion en la cantidad pedida, y en sentidos
    opuestos segun el signo. Es lo que ensancha la imagen estereo. */
void testDetune()
{
    const int sr = 48000, L = sr / 10, N = sr * 4;
    const double fIn = 300.0;

    auto measureHz = [&] (float cents)
    {
        ReverseDelay d;
        d.prepare (sr * 2, L, sr);
        d.setReadRate (std::pow (2.0f, cents / 1200.0f));

        std::vector<float> y (N);
        for (int n = 0; n < N; ++n)
            y[n] = d.process ((float) std::sin (2.0 * kPi * fIn * n / sr), 0.0f);

        int crossings = 0;
        const int from = 4 * L, to = N - L;
        for (int n = from + 1; n < to; ++n)
            if (y[n - 1] < -0.02f && y[n] >= -0.02f) ++crossings;

        return crossings * (double) sr / (double) (to - from);
    };

    // +700 cents = quinta justa = x1.498
    const double up   = measureHz ( 700.0f);
    const double down = measureHz (-700.0f);

    const bool ok = up > fIn * 1.35 && up < fIn * 1.65
                 && down > fIn * 0.55 && down < fIn * 0.80;

    check ("detune desplaza en ambos sentidos", ok,
           "+700c -> " + f (up, 1) + " Hz, -700c -> " + f (down, 1)
           + " Hz (entrada " + f (fIn, 0) + " Hz)");
}

/** El shifter debe desplazar la altura la cantidad pedida. Se mide con el pico
    espectral, no con cruces por cero: el temblor granular anade y quita cruces
    y falsea la medida en varios por ciento. */
void testPitchShifter()
{
    const int sr = 48000, N = sr * 3;

    auto peakHz = [&] (const std::vector<float>& y, double lo, double hi)
    {
        double best = 0.0, bf = 0.0;
        for (double fq = lo; fq <= hi; fq += 1.0)
        {
            const double w = 2.0 * kPi * fq / sr, c = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;
            for (int i = sr / 2; i < N; ++i) { const double s0 = y[i] + c * s1 - s2; s2 = s1; s1 = s0; }
            const double m = s1 * s1 + s2 * s2 - c * s1 * s2;
            if (m > best) { best = m; bf = fq; }
        }
        return bf;
    };

    double worst = 0.0;
    for (float st : { 0.0f, 7.0f, 12.0f, -12.0f })
    {
        PitchShifter p;
        p.prepare (sr);
        p.setSemitones (st);

        std::vector<float> y (N);
        for (int n = 0; n < N; ++n) y[n] = p.process ((float) std::sin (2.0 * kPi * 220.0 * n / sr));

        const double e  = 220.0 * std::pow (2.0, st / 12.0);
        const double hz = peakHz (y, e * 0.85, e * 1.15);
        worst = std::max (worst, std::fabs (1200.0 * std::log2 (hz / e)));
    }

    // 60 cents de margen: un shifter granular no es transparente, y el error
    // depende de la frecuencia de entrada por la interferencia del crossfade.
    check ("pitch shifter desplaza la altura", worst < 60.0,
           "peor error " + f (worst, 1) + " cents (limite del metodo granular)");

    // Ganancia: dentro de un lazo (shimmer) una perdida por pasada se
    // acumula y las octavas superiores se apagan antes de nacer. Se mide el
    // RMS de salida contra el de entrada en varias frecuencias.
    std::string gains; double worstDb = 0.0;
    for (float st : { 12.0f, -12.0f })
    for (double hz : { 220.0, 880.0, 3520.0 })
    {
        PitchShifter p;
        p.prepare (sr);
        p.setSemitones (st);
        double ei = 0, eo = 0;
        for (int n = 0; n < N; ++n)
        {
            const float x = (float) std::sin (2.0 * kPi * hz * n / sr);
            const float y = p.process (x);
            if (n >= sr / 2) { ei += x * x; eo += y * y; }
        }
        const double db = 10.0 * std::log10 (eo / ei);
        worstDb = std::max (worstDb, std::fabs (db));
        gains += f (st, 0) + "st@" + f (hz, 0) + ":" + f (db, 1) + "dB ";
    }
    check ("pitch shifter: ganancia unidad", worstDb < 2.0, gains);
}

/** El shimmer no debe desestabilizar el lazo. Al contrario: empuja la energia
    hacia arriba, donde se escapa. El tope de feedback sube con el. */
void testShimmerStability()
{
    const int sr = 48000;
    double worst = 0.0;
    bool bad = false;

    for (float sh : { 0.5f, 1.0f })
        for (float lp : { 2000.0f, 20000.0f })
            for (int L : { sr / 20, sr / 2 })
            {
                ReverseDelay d;
                d.prepare (sr * 2, L, sr);
                d.setFeedbackTone (20.0f, lp);
                d.setShimmer (sh, 12.0f);

                Noise rng; double tail = 0.0;
                for (int n = 0; n < sr * 32; ++n)
                {
                    const float x = n < sr * 2 ? rng.next() : 0.0f;
                    const float y = d.process (x, 1.0f);      // se pide el maximo
                    if (! std::isfinite (y)) bad = true;
                    if (n >= sr * 31) tail = std::max (tail, (double) std::fabs (y));
                }
                worst = std::max (worst, tail);
            }

    check ("shimmer no desestabiliza el lazo", ! bad && worst < 0.02,
           "pico tras 30 s de silencio: " + f (worst));
}

//==============================================================================
//  DELAY
//==============================================================================

/** Aqui cada muestra se lee una vez y sin ventana: ganancia de lazo = fb, asi
    que el techo puede ser mucho mas alto (0.95). El ping-pong no lo empeora
    porque el viaje L->R->L tiene ganancia fb^2. */
void testDelayStability()
{
    const int sr = 48000;
    const std::vector<float> hps = fullSweep ? std::vector<float>{20.f,200.f,2000.f} : std::vector<float>{20.f,500.f};
    const std::vector<float> lps = fullSweep ? std::vector<float>{200.f,2000.f,20000.f} : std::vector<float>{1000.f,20000.f};
    const std::vector<int>   dts = fullSweep ? std::vector<int>{sr/50, sr/2, sr*2} : std::vector<int>{sr/50, sr/2};

    const int secs = fullSweep ? 62 : 32;
    const int tailFrom = (secs - 1) * sr;
    const int totalCombos = (int) (2 * hps.size() * lps.size() * dts.size());
    int combo = 0;
    double worstTail = 0.0;
    bool   bad = false;

    for (bool pp : { false, true })
    for (float hp : hps) for (float lp : lps) for (int D : dts)
    {
        progress (++combo, totalCombos);
        StereoDelay d;
        d.prepare (sr * 2, sr, 2, (float) D);
        d.setTone (hp, lp);
        d.setPingPong (pp);

        Noise rng; double tail = 0.0;
        for (int n = 0; n < secs * sr; ++n)
        {
            const float x = n < sr * 2 ? rng.next() : 0.0f;
            float l = x, r = -x;
            d.process (l, r, 1.0f);                        // se pide el maximo
            if (! std::isfinite (l) || ! std::isfinite (r)) bad = true;
            if (n >= tailFrom) tail = std::max (tail, (double) std::max (std::fabs (l), std::fabs (r)));
        }
        worstTail = std::max (worstTail, tail);
    }
    clearProgress();

    check ("lazo decae con feedback al maximo", ! bad && worstTail < 0.05,
           "pico tras " + std::to_string (secs - 2) + " s de silencio: " + f (worstTail));
}

/** El retardo real debe coincidir con el pedido nada mas preparar, sin barrido
    inicial: un glide al cargar el plugin se oiria como un sweep en la primera
    nota de la sesion. */
void testDelayTimeAccuracy()
{
    const int sr = 48000, D = sr / 4;      // 250 ms
    StereoDelay d;
    d.prepare (sr * 2, sr, 2, (float) D);

    int peakAt = -1; float peak = 0.0f;
    for (int n = 0; n < sr; ++n)
    {
        float l = (n == 0) ? 1.0f : 0.0f, r = l;
        d.process (l, r, 0.0f);
        if (std::fabs (l) > peak) { peak = std::fabs (l); peakAt = n; }
    }

    const int err = std::abs (peakAt - D);
    check ("retardo exacto al preparar", err <= 2,
           "pico en " + std::to_string (peakAt) + ", esperado " + std::to_string (D)
           + " (error " + std::to_string (err) + ")");
}

/** La modulacion del delay debe producir variacion de tono sin clicks, y con
    L y R en cuadratura para que abra la imagen. */
void testDelayModulation()
{
    const int sr = 48000, D = sr / 4, N = sr * 4;
    StereoDelay d;
    d.prepare (sr * 2, sr, 2, (float) D);

    std::vector<float> yl (N), yr (N);
    bool bad = false;
    const float depth = 0.008f * sr;          // 8 ms, el maximo del plugin

    for (int n = 0; n < N; ++n)
    {
        const float ph = 2.0f * (float) kPi * 0.6f * n / sr;
        d.setModulation (depth * std::sin (ph), depth * std::cos (ph));

        float l = (float) std::sin (2.0 * kPi * 300.0 * n / sr), r = l;
        d.process (l, r, 0.3f);
        yl[n] = l; yr[n] = r;
        if (! std::isfinite (l) || ! std::isfinite (r)) bad = true;
    }

    // Sin clicks: el salto no debe superar mucho el de un seno de 300 Hz.
    const double theo = 2.0 * kPi * 300.0 / sr;
    double mj = 0.0;
    for (int n = D + 100; n < N; ++n)
        mj = std::max (mj, (double) std::fabs (yl[n] - yl[n - 1]));

    // L y R deben diferir: si fueran identicos, la cuadratura no se aplica.
    double diff = 0.0;
    for (int n = N / 2; n < N; ++n) diff += std::fabs (yl[n] - yr[n]);
    diff /= (N / 2);

    check ("modulacion del delay: sin clicks y estereo", ! bad && mj < theo * 3.0 && diff > 0.01,
           "salto " + f (mj) + " <= " + f (theo * 3.0) + ", diferencia L/R " + f (diff, 4));
}

/** El ping-pong debe ALTERNAR canales. Con un impulso solo en L, la topologia
    correcta es: eco 1 en L (n=D), eco 2 cruzado a R (n=2D), eco 3 otra vez en
    L. Sin ping-pong, todos se quedan en L. */
void testPingPong()
{
    const int sr = 48000, D = sr / 10;

    auto run = [&] (bool pp, double& l1, double& r1, double& l2, double& r2)
    {
        StereoDelay d;
        d.prepare (sr * 2, sr, 2, (float) D);
        d.setPingPong (pp);

        l1 = r1 = l2 = r2 = 0.0;
        for (int n = 0; n < sr; ++n)
        {
            float l = (n == 0) ? 1.0f : 0.0f, r = 0.0f;
            d.process (l, r, 0.7f);

            if (std::abs (n -     D) < 60) { l1 += l * l; r1 += r * r; }
            if (std::abs (n - 2 * D) < 60) { l2 += l * l; r2 += r * r; }
        }
    };

    auto fmt = [] (double a, double b) { return f (a, 4) + "/" + f (b, 4); };

    double l1, r1, l2, r2;

    run (true, l1, r1, l2, r2);
    check ("ping-pong alterna canales", l1 > r1 * 4.0 + 1e-9 && r2 > l2 * 4.0 + 1e-9,
           "eco1 L/R " + fmt (l1, r1) + "   eco2 L/R " + fmt (l2, r2));

    run (false, l1, r1, l2, r2);
    check ("sin ping-pong se queda en su canal", l1 > r1 * 4.0 + 1e-9 && l2 > r2 * 4.0 + 1e-9,
           "eco1 L/R " + fmt (l1, r1) + "   eco2 L/R " + fmt (l2, r2));
}


//==============================================================================
//  SATURADOR, FREEZE RELEASE, LOW CUT 12 dB
//==============================================================================

/** Energia en una banda de +-3 % alrededor de hz sobre y (Goertzel). */
static double bandEnergy (const std::vector<float>& y, double sr, double hz)
{
    double e = 0.0;
    for (double fq = hz * 0.97; fq <= hz * 1.03; fq += hz * 0.003)
    {
        const double w = 2.0 * kPi * fq / sr, c = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (float x : y) { const double s0 = x + c * s1 - s2; s2 = s1; s1 = s0; }
        e += s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    return e;
}

/** tanh(8x) sobre un seno de 4 kHz: el 9o armonico (36 kHz) se pliega a
    8.1 kHz y el 7o (28 kHz) a 16.1 kHz. El saturador a 2x tiene que rebajar
    esos aliasing de forma clara sin tocar la fundamental. */
void testSaturatorAliasing()
{
    const double sr = 44100.0, f0 = 4000.0;
    const int N = (int) sr;

    std::vector<float> naive (N), over (N);
    Saturator sat; sat.setDrive (8.0f);
    for (int n = 0; n < N; ++n)
    {
        const float x = 0.5f * (float) std::sin (2.0 * kPi * f0 * n / sr);
        naive[n] = std::tanh (8.0f * x) / 8.0f;
        over[n]  = sat.process (x);
    }

    auto dB = [] (double a, double b) { return 10.0 * std::log10 (a / b); };
    const double fundN = bandEnergy (naive, sr, f0),      fundO = bandEnergy (over, sr, f0);
    const double a9N   = bandEnergy (naive, sr, 8100.0),  a9O   = bandEnergy (over, sr, 8100.0);
    const double a7N   = bandEnergy (naive, sr, 16100.0), a7O   = bandEnergy (over, sr, 16100.0);

    const double alias9Naive = dB (a9N, fundN), alias9Over = dB (a9O, fundO);
    const double alias7Naive = dB (a7N, fundN), alias7Over = dB (a7O, fundO);
    const double fundLoss    = dB (fundO, fundN);

    check ("saturador 2x: menos aliasing, misma fundamental",
           alias9Naive - alias9Over > 12.0 && alias7Naive - alias7Over > 6.0 && std::fabs (fundLoss) < 1.0,
           "9o armonico plegado a 8.1 kHz: " + f (alias9Naive, 1) + " -> " + f (alias9Over, 1) + " dB; "
           + "7o a 16.1 kHz: " + f (alias7Naive, 1) + " -> " + f (alias7Over, 1) + " dB; "
           + "fundamental " + f (fundLoss, 2) + " dB");
}

/** Al soltar Freeze con release, el colchon se apaga en vez de cortarse:
    sigue sonando poco despues, esta casi extinguido al final del release, y
    no hay saltos. Con release 0 se va en una ventana, como siempre. */
void testFreezeRelease()
{
    const int sr = 48000, L = sr / 5;   // 200 ms
    int worstAt = -1;
    auto run = [&] (float releaseSec, double& early, double& late, float& worstJump, double& ref)
    {
        ReverseDelay d;
        d.prepare (sr * 2, L, sr);
        d.setFreezeRelease (releaseSec);

        // Seno y no ruido: con ruido blanco la diferencia entre muestras
        // consecutivas es ruido (+-1.2 medido) y un click no se distingue.
        // Con un seno la derivada natural es ~0.03 por muestra. 223 Hz y una
        // duracion no redonda para que la costura NO caiga en un cruce por
        // cero (con 220 Hz y 1 s exacto caia siempre y el click se escondia).
        // Feedback 0 para aislar el release: con 0.3 el propio feedback del
        // usuario mantiene la cola y enmascara la diferencia.
        for (int n = 0; n < sr + 1000; ++n)
            d.process (0.5f * (float) std::sin (2.0 * kPi * 223.0 * n / sr), 0.0f);   // 1 s de material
        d.setFrozen (true);
        double e = 0; float prev = 0.0f;
        for (int n = 0; n < sr; ++n) { const float y = d.process (0.0f, 0.0f); e += y * y; prev = y; }
        ref = std::sqrt (e / sr);                                          // nivel congelado
        d.setFrozen (false);

        double e1 = 0, e2 = 0; worstJump = 0.0f;
        const int w = sr / 10;                                             // ventanas de 100 ms
        for (int n = 0; n < sr * 3; ++n)
        {
            const float y = d.process (0.0f, 0.0f);
            if (std::fabs (y - prev) > worstJump) { worstJump = std::fabs (y - prev); worstAt = n; }
            prev = y;
            if (n >= 2 * L && n < 2 * L + w)          e1 += y * y;        // justo tras 2 ventanas
            if (n >= sr * 3 - w)                      e2 += y * y;        // al final (3 s)
        }
        early = std::sqrt (e1 / w); late = std::sqrt (e2 / w);
    };

    double ref, early, late; float jump;
    run (1.5f, early, late, jump, ref);
    const double earlyDb = 20.0 * std::log10 (early / ref), lateDb = 20.0 * std::log10 (late / ref);

    double ref0, early0, late0; float jump0;
    run (0.0f, early0, late0, jump0, ref0);
    const double early0Db = 20.0 * std::log10 (early0 / ref0 + 1e-12);

    // Con 1.5 s de release la curva es -60 dB/1.5 s: a los 0.4 s (2 ventanas
    // de 200 ms) toca -16 dB. Con 0 s, a esa altura ya no queda nada.
    check ("freeze release: se apaga en vez de cortarse",
           earlyDb > -22.0 && earlyDb < -10.0 && lateDb < -40.0 && early0Db < -40.0
           && jump < 0.1f && jump0 < 0.1f,
           "con 1.5 s: tras 2 ventanas " + f (earlyDb, 1) + " dB, a los 3 s " + f (lateDb, 1)
           + " dB, salto max " + f (jump, 3) + "; con 0 s: tras 2 ventanas " + f (early0Db, 1)
           + " dB, salto max " + f (jump0, 3) + " en la muestra " + std::to_string (worstAt)
           + " (L=" + std::to_string (L) + ")");
}

/** Con el Low Cut a 12 dB/oct los graves del lazo se apagan antes. */
void testLowCutSteep()
{
    const int sr = 48000, L = sr / 4;
    auto tailAt100Hz = [&] (bool steep)
    {
        ReverseDelay d;
        d.prepare (sr * 2, L, sr);
        d.setFeedbackTone (300.0f, 20000.0f);
        d.setLowCutSteep (steep);
        std::vector<float> y;
        for (int n = 0; n < sr * 3; ++n)
        {
            const float x = n < sr ? 0.5f * (float) std::sin (2.0 * kPi * 100.0 * n / sr) : 0.0f;
            const float o = d.process (x, 0.6f);
            if (n >= sr * 2) y.push_back (o);
        }
        return bandEnergy (y, sr, 100.0);
    };
    const double one = tailAt100Hz (false), two = tailAt100Hz (true);
    const double dB = 10.0 * std::log10 (two / one);
    check ("low cut 12 dB: los graves del lazo decaen antes", dB < -6.0,
           "100 Hz en la cola con 2 polos vs 1: " + f (dB, 1) + " dB");
}

//==============================================================================
//  REVERB (placa de Dattorro)
//==============================================================================

/** Nivel de la cola tras `secs` segundos de silencio para una configuracion. */
static double plateTail (double sr, float size, float damp, float mod, float shim, float st, int secs,
                         bool sines = false)
{
    PlateReverb rv;
    rv.prepare (sr);
    rv.setSize (size); rv.setDamping (damp); rv.setModulation (mod);
    rv.setShimmer (shim, st);

    Noise rng;
    const int burst = (int) sr;
    const int total = (int) sr * (secs + 1);
    double peak = 0.0;
    for (int n = 0; n < total; ++n)
    {
        float l = 0.0f;
        if (n < burst)
        {
            // Ruido blanco tiene la mitad de su energia por encima de 22 kHz a
            // 96k y el filtro de entrada se la come: para comparar sample
            // rates hay que excitar con algo de banda limitada.
            if (sines)
                for (float hz : { 200.0f, 1000.0f, 3000.0f })
                    l += 0.15f * std::sin (2.0f * 3.14159265f * hz * (float) n / (float) sr);
            else
                l = 0.5f * rng.next();
        }
        float r = l;
        rv.process (l, r);
        if (n >= total - (int) sr)
            peak = std::max (peak, (double) std::max (std::fabs (l), std::fabs (r)));
    }
    return peak;
}

/** Con todo al maximo (size 1, sin damping, mod y shimmer a tope, arriba y
    abajo) la cola tiene que seguir bajando: 30 s despues de cortar la
    entrada no puede quedar mas de -40 dB. */
void testPlateStability()
{
    const double sr = 44100.0;
    const std::vector<float> sizes = { 0.7f, 1.0f };
    const std::vector<float> damps = { 0.0f, 0.8f };
    const std::vector<float> mods  = fullSweep ? std::vector<float>{ 0.f, 0.5f, 1.f } : std::vector<float>{ 0.f, 1.f };
    struct Sh { float amt, st; };
    const std::vector<Sh> shims = { { 0.f, 12.f }, { 1.f, 12.f }, { 1.f, -12.f }, { 0.5f, 7.f } };

    const int totalCombos = (int) (sizes.size() * damps.size() * mods.size() * shims.size());
    int combo = 0;
    double worst = 0.0; std::string worstAt;

    for (float sz : sizes) for (float dp : damps) for (float md : mods) for (auto sh : shims)
    {
        progress (++combo, totalCombos);
        const double p = plateTail (sr, sz, dp, md, sh.amt, sh.st, 30);
        if (p > worst) { worst = p; worstAt = "size " + f (sz, 1) + " damp " + f (dp, 1) + " mod " + f (md, 1)
                                          + " shim " + f (sh.amt, 1) + "@" + f (sh.st, 0); }
    }
    if (fullSweep) std::printf ("\r%60s\r", "");

    check ("placa: la cola siempre decae", worst < 0.01,
           "peor pico 30 s tras cortar: " + f (worst, 5) + " (" + worstAt + ")");
}

/** El pre-delay tiene que ser exacto: nada sale antes. */
void testPlatePreDelay()
{
    const double sr = 48000.0;
    PlateReverb rv;
    rv.prepare (sr);
    rv.setSize (0.5f); rv.setPreDelayMs (100.0f);

    // el glide del pre-delay tarda unos ms en asentarse: se deja correr en silencio
    for (int n = 0; n < (int) sr; ++n) { float l = 0.0f, r = 0.0f; rv.process (l, r); }

    const int pre = (int) (0.1 * sr);
    int first = -1;
    for (int n = 0; n < pre * 2; ++n)
    {
        float l = n == 0 ? 1.0f : 0.0f, r = l;
        rv.process (l, r);
        if (first < 0 && (std::fabs (l) > 1e-6f || std::fabs (r) > 1e-6f)) first = n;
    }

    // El primer tap de salida esta a 266 muestras (a 29761 Hz) del tanque.
    const int earliestTap = (int) (266.0 * sr / 29761.0);
    check ("placa: pre-delay exacto", first >= pre && first <= pre + earliestTap + 8,
           "primera salida en " + std::to_string (first) + ", pre-delay " + std::to_string (pre)
           + " + primer tap " + std::to_string (earliestTap));
}

/** Entrada mono, salida ancha: L y R de la cola tienen que estar decorreladas. */
void testPlateStereo()
{
    const double sr = 44100.0;
    PlateReverb rv;
    rv.prepare (sr);
    rv.setSize (0.8f); rv.setDamping (0.3f);

    Noise rng;
    double ll = 0, rr = 0, lr = 0;
    for (int n = 0; n < (int) sr * 3; ++n)
    {
        float l = n < (int) sr / 4 ? 0.5f * rng.next() : 0.0f, r = l;
        rv.process (l, r);
        if (n > (int) sr / 2) { ll += l * l; rr += r * r; lr += l * r; }
    }
    const double corr = lr / std::sqrt (ll * rr);
    check ("placa: salida estereo decorrelada", std::fabs (corr) < 0.3 && ll > 0 && rr > 0,
           "correlacion L/R de la cola " + f (corr, 3));
}

/** Con shimmer, la energia de la cola tiene que subir de octava: se mide con
    Goertzel en 220 Hz y sus octavas sobre el ultimo segundo. Sin shimmer
    todo queda en 220; con shimmer las octavas superiores tienen que dominar,
    pero el paso bajo de la rama debe impedir que se escape a siseo. */
void testPlateShimmer()
{
    const double sr = 44100.0;
    const std::vector<double> octaves = { 220, 440, 880, 1760, 3520, 7040, 14080 };

    auto bands = [&] (float shim)
    {
        PlateReverb rv;
        rv.prepare (sr);
        rv.setSize (1.0f); rv.setDamping (0.0f); rv.setShimmer (shim, 12.0f);

        std::vector<float> tail;
        for (int n = 0; n < (int) sr * 4; ++n)
        {
            float l = n < (int) sr ? 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * (float) n / (float) sr) : 0.0f;
            float r = l;
            rv.process (l, r);
            if (n >= (int) sr * 3) tail.push_back (l);
        }

        // Energia por BANDA (+-7 % alrededor de cada octava), no en el bin
        // exacto: el shifter granular tiene +-20 cents de error, que a 880 Hz
        // son +-12 Hz, y un Goertzel de 1 s (1 Hz de resolucion) no los veria.
        std::vector<double> e;
        for (double hz : octaves)
        {
            double band = 0.0;
            for (double fq = hz * 0.93; fq <= hz * 1.07; fq += hz * 0.005)
            {
                const double w = 2.0 * 3.14159265358979 * fq / sr, c = 2.0 * std::cos (w);
                double s1 = 0, s2 = 0;
                for (float x : tail) { const double s0 = x + c * s1 - s2; s2 = s1; s1 = s0; }
                band += s1 * s1 + s2 * s2 - c * s1 * s2;
            }
            e.push_back (band);
        }
        const double total = std::accumulate (e.begin(), e.end(), 0.0) + 1e-30;
        for (auto& v : e) v /= total;
        return e;
    };

    const auto dry = bands (0.0f), half = bands (0.5f), full = bands (1.0f);

    auto dist = [&] (const std::vector<double>& e)
    {
        std::string d;
        for (size_t i = 0; i < octaves.size(); ++i)
            d += f (octaves[i], 0) + ":" + f (100.0 * e[i], 0) + "% ";
        return d;
    };

    // Al 50 % en cada vuelta la mitad se queda y la mitad sube: tiene que
    // haber varias octavas sonando a la vez (el "acorde" del shimmer).
    int octavesAlive = 0;
    for (size_t i = 0; i < 5; ++i) if (half[i] > 0.05) ++octavesAlive;

    check ("placa: shimmer al 50 % apila octavas", dry[0] > 0.9 && octavesAlive >= 3,
           "sin shimmer " + f (100.0 * dry[0], 0) + "% en 220 Hz; al 50 %: " + dist (half));

    // Al 100 % TODO sube cada vuelta: en 2 s ya no queda nada en 220. Lo que
    // se comprueba es que el paso bajo de la rama lo retiene por debajo del
    // siseo en vez de dejarlo escapar a 14 kHz (que es lo que pasaba sin el).
    check ("placa: shimmer al 100 % no se escapa a siseo", full[6] < 0.05 && full[5] + full[6] < 0.4,
           "al 100 %: " + dist (full));
}

/** Escalado de longitudes: la cola tiene que durar lo mismo a 44.1 y a 96 kHz. */
void testPlateSampleRateInvariance()
{
    const double a = plateTail (44100.0, 0.8f, 0.3f, 0.5f, 0.0f, 12.0f, 3, true);
    const double b = plateTail (96000.0, 0.8f, 0.3f, 0.5f, 0.0f, 12.0f, 3, true);
    const double dB = 20.0 * std::log10 (a / b);
    check ("placa: igual a 44.1 y 96 kHz", std::fabs (dB) < 1.5,
           "nivel a los 3 s: 44.1k " + f (a, 5) + ", 96k " + f (b, 5) + " (" + f (dB, 2) + " dB)");
}

void testPlateNoNaN()
{
    PlateReverb rv;
    rv.prepare (48000.0);
    rv.setSize (1.0f); rv.setModulation (1.0f); rv.setShimmer (1.0f, 12.0f); rv.setPreDelayMs (200.0f);

    bool bad = false;
    for (int n = 0; n < 48000 * 2; ++n)
    {
        float l = n < 100 ? 1e6f : (n == 200 ? std::numeric_limits<float>::quiet_NaN() : (n % 1000 == 0 ? 1e-30f : 0.0f));
        float r = l;
        rv.process (l, r);
        if (n > 300 && (! std::isfinite (l) || ! std::isfinite (r))) bad = true;
    }
    check ("placa: sin NaN/Inf con extremos y NaN inyectado", ! bad,
           bad ? "salida no finita" : "entrada 1e6, 1e-30 y un NaN; salida finita");
}

//==============================================================================
//  AUXILIARES
//==============================================================================

void testDucker()
{
    const int sr = 48000;
    Ducker duck;
    duck.prepare (sr);
    duck.setRelease (200.0f);

    float g = 1.0f;
    for (int n = 0; n < sr / 20; ++n) g = duck.process (1.0f, 1.0f);   // 50 ms de señal
    const float attacked = g;

    for (int n = 0; n < sr; ++n) g = duck.process (0.0f, 1.0f);        // 1 s de silencio
    const float released = g;

    check ("ducker atenua y vuelve", attacked < 0.1f && released > 0.95f,
           "con senal " + f (attacked, 3) + ", tras silencio " + f (released, 3));

    // El ducking es RELATIVO al nivel de entrada: una DI a -20 dBFS tiene que
    // atenuar igual que una senal a 0 dBFS. Antes, con el x3 fijo, a 0.1 de
    // amplitud solo se llegaba a un tercio de la atenuacion pedida.
    Ducker quiet;
    quiet.prepare (sr);
    quiet.setRelease (200.0f);
    for (int n = 0; n < sr / 20; ++n) g = quiet.process (0.1f, 1.0f);
    const float quietAttacked = g;

    // ...pero el ruido de fondo (por debajo de -26 dBFS) NO cuenta como senal.
    Ducker noise;
    noise.prepare (sr);
    for (int n = 0; n < sr / 20; ++n) g = noise.process (0.005f, 1.0f);
    const float noiseAttacked = g;

    check ("ducker independiente del nivel", quietAttacked < 0.1f && noiseAttacked > 0.85f,
           "a -20 dBFS " + f (quietAttacked, 3) + ", con ruido a -46 dBFS " + f (noiseAttacked, 3));
}

/** La cubica tiene que ser exacta en los enteros (si no, el reverse a rate=1
    dejaria de ser transparente) y mucho mejor que la lineal entre ellos. */
void testHermite()
{
    // Seno a 0.3 rad/muestra (~2.1 kHz a 44.1k): bastante agudo para que la
    // lineal se note.
    const int N = 64;
    std::vector<float> buf ((size_t) N);
    for (int i = 0; i < N; ++i) buf[(size_t) i] = std::sin (0.3f * (float) i);

    float worstInt = 0.0f, worstCubic = 0.0f, worstLinear = 0.0f;
    for (int i = 4; i < N - 4; ++i)
    {
        worstInt = std::max (worstInt, std::fabs (readHermite (buf, N, (float) i) - buf[(size_t) i]));

        for (int k = 1; k < 8; ++k)
        {
            const float p     = (float) i + (float) k / 8.0f;
            const float ideal = std::sin (0.3f * p);
            const float lin   = buf[(size_t) i] + ((float) k / 8.0f) * (buf[(size_t) i + 1] - buf[(size_t) i]);
            worstCubic  = std::max (worstCubic,  std::fabs (readHermite (buf, N, p) - ideal));
            worstLinear = std::max (worstLinear, std::fabs (lin - ideal));
        }
    }

    check ("interpolacion cubica: exacta en enteros, mejor que lineal",
           worstInt == 0.0f && worstCubic < worstLinear * 0.2f,
           "error en enteros " + f (worstInt, 9) + ", cubica " + f (worstCubic, 5)
           + " vs lineal " + f (worstLinear, 5));
}

/** Un NaN que entre UNA vez no puede quedarse a vivir en el lazo. */
void testNaNFlush()
{
    const int sr = 48000;
    ReverseDelay rd; rd.prepare (sr, sr / 100, sr);
    StereoDelay  sd; sd.prepare (sr, sr, 2, 100.0f);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    rd.process (nan, 0.5f);
    float l = nan, r = nan;
    sd.process (l, r, 0.5f);

    bool bad = false;
    for (int n = 0; n < sr; ++n)
    {
        const float a = rd.process (0.0f, 0.5f);
        l = 0.0f; r = 0.0f;
        sd.process (l, r, 0.5f);
        if (! std::isfinite (a) || ! std::isfinite (l) || ! std::isfinite (r)) bad = true;
    }

    check ("un NaN de entrada no se queda en el lazo", ! bad,
           bad ? "el lazo sigue devolviendo NaN un segundo despues"
               : "salida finita durante 1 s tras inyectar NaN");
}

void testOnePole()
{
    const int sr = 48000;
    const double fc = 1000.0;
    OnePole lp;
    lp.setCutoff ((float) fc, sr);

    double peak = 0.0;
    for (int n = 0; n < sr; ++n)
    {
        const float y = lp.lowpass ((float) std::sin (2.0 * kPi * fc * n / sr));
        if (n > sr / 2) peak = std::max (peak, (double) std::fabs (y));
    }

    const double db = 20.0 * std::log10 (peak);
    check ("paso bajo: -3 dB en el corte", db < -2.0 && db > -4.5,
           f (db, 2) + " dB en fc (esperado ~-3)");
}

/** Denormales y valores extremos: nada debe producir NaN ni Inf. */
void testNoNaN()
{
    const int sr = 48000;
    ReverseDelay rd; rd.prepare (sr, sr / 100, sr);
    rd.setFeedbackTone (2000.0f, 200.0f);   // HP por encima de LP: caso degenerado
    rd.setDrive (8.0f);

    StereoDelay sd; sd.prepare (sr, sr, 2, 3.0f);
    sd.setTone (2000.0f, 200.0f);

    bool bad = false;
    for (int n = 0; n < sr * 4; ++n)
    {
        const float x = (n < 100) ? 1e6f : ((n % 1000 == 0) ? 1e-30f : 0.0f);
        const float a = rd.process (x, 1.0f);
        float l = x, r = -x;
        sd.process (l, r, 1.0f);
        if (! std::isfinite (a) || ! std::isfinite (l) || ! std::isfinite (r)) bad = true;
    }

    check ("sin NaN/Inf con valores extremos", ! bad,
           "entrada 1e6 y 1e-30, filtros invertidos, feedback al maximo");
}

} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    // NO se usa setvbuf aqui. En MSVC, setvbuf con buffer nullptr exige que
    // size este entre 2 e INT_MAX; pasarle 0 dispara el invalid parameter
    // handler, que en Release ABORTA el proceso sin imprimir nada. En glibc ese
    // mismo 0 es valido, asi que el fallo solo aparecia al compilar con MSVC.
    //
    // Se hace fflush explicito en check(), section() y progress(), que es
    // portable y no depende de las reglas de validacion de ninguna plataforma.

    for (int i = 1; i < argc; ++i)
        if (std::strcmp (argv[i], "--full") == 0) fullSweep = true;

    const auto t0 = std::chrono::steady_clock::now();

    std::printf ("ReverseVerb - tests de DSP  (%s)\n",
                 fullSweep ? "barrido exhaustivo, varios minutos"
                           : "barrido rapido, usa --full para el completo");
    std::fflush (stdout);

    section ("REVERSE");
    testReverseClicks();
    testReverseConstantPower();
    testReverseActuallyReverses();
    testReverseLengthChange();
    testReverseStability();
    testFreeze();
    testFractionalTransparent();
    testModulationNoClicks();
    testReadRatePitch();
    testModulationNeverReadsAhead();
    testTapeMod();
    testDetune();
    testPitchShifter();
    testShimmerStability();

    section ("DELAY");
    testDelayStability();
    testDelayTimeAccuracy();
    testPingPong();
    testDelayModulation();

    section ("SATURADOR Y FREEZE");
    testSaturatorAliasing();
    testFreezeRelease();
    testLowCutSteep();

    section ("REVERB");
    testPlateStability();
    testPlatePreDelay();
    testPlateStereo();
    testPlateShimmer();
    testPlateSampleRateInvariance();
    testPlateNoNaN();

    section ("AUXILIARES");
    testDucker();
    testHermite();
    testNaNFlush();
    testOnePole();
    testNoNaN();

    const auto secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("\n%d pasaron, %d fallaron   (%.1f s)\n", passed, failed, secs);
    std::fflush (stdout);
    return failed == 0 ? 0 : 1;
}
