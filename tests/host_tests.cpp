// ============================================================================
//  Tests a nivel de PROCESADOR.
//
//  Entre la suite de DSP (motores sin JUCE) y pluginval (contrato con el host)
//  no habia nada que probase el AudioProcessor en si: bloques raros, bypass
//  con cola, presets que cargan y guardan sin cambiar, MIDI -> parametro,
//  estado que sobrevive al viaje de ida y vuelta. Los dos bugs de bloques (0
//  muestras y bloques mayores que samplesPerBlock) vivian justo ahi.
//
//  Enlaza JUCE y el codigo del plugin, pero no instancia el editor ni abre
//  ventanas: un ScopedJuceInitialiser_GUI basta para el MessageManager, que
//  hace falta porque el MIDI se aplica por AsyncUpdater.
//
//    cmake --build build --config Release --target ReverseVerbHostTests
//    build\Release\ReverseVerbHostTests.exe
// ============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    int passed = 0, failed = 0;

    void check (const char* name, bool ok, const std::string& detail)
    {
        std::printf ("%s  %-46s %s\n", ok ? "[ ok ]" : "[FAIL]", name, detail.c_str());
        std::fflush (stdout);
        (ok ? passed : failed)++;
    }

    std::string f (double v, int dec = 3)
    {
        char b[64]; std::snprintf (b, sizeof (b), "%.*f", dec, v); return b;
    }

    /** Procesa `n` muestras de la senal dada y devuelve la salida. */
    std::vector<float> run (ReverseVerbProcessor& p, int n, int block, float (*gen) (int), bool bypass = false)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int start = 0; start < n; start += block)
        {
            const int len = juce::jmin (block, n - start);
            buf.setSize (2, len, false, false, true);
            for (int i = 0; i < len; ++i) { const float x = gen (start + i); buf.setSample (0, i, x); buf.setSample (1, i, x); }
            if (bypass) p.processBlockBypassed (buf, midi); else p.processBlock (buf, midi);
            for (int i = 0; i < len; ++i) out.push_back (buf.getSample (0, i));
        }
        return out;
    }

    float silence (int)   { return 0.0f; }
    float sine440 (int n) { return 0.5f * std::sin (2.0f * 3.14159265f * 440.0f * (float) n / 48000.0f); }

    bool allFinite (const std::vector<float>& v)
    {
        for (float x : v) if (! std::isfinite (x)) return false;
        return true;
    }

    float rms (const std::vector<float>& v, size_t from, size_t to)
    {
        double e = 0; for (size_t i = from; i < to && i < v.size(); ++i) e += v[i] * v[i];
        return (float) std::sqrt (e / (double) (to - from));
    }

    void setParam (ReverseVerbProcessor& p, const char* id, float value)
    {
        auto* rp = p.apvts.getParameter (id);
        rp->setValueNotifyingHost (rp->convertTo0to1 (value));
    }

    float getParam (ReverseVerbProcessor& p, const char* id)
    {
        auto* rp = p.apvts.getParameter (id);
        return rp->convertFrom0to1 (rp->getValue());
    }
}

//==============================================================================
void testBlockSizes()
{
    ReverseVerbProcessor p;
    p.prepareToPlay (48000.0, 256);
    setParam (p, "mix", 50.0f);

    // 0 muestras, bloques pequenos, y bloques MAYORES que los 256 anunciados.
    bool ok = true;
    for (int block : { 0, 1, 7, 64, 256, 257, 1000, 4096 })
    {
        if (block == 0)
        {
            juce::AudioBuffer<float> b (2, 0); juce::MidiBuffer m;
            p.processBlock (b, m);
            continue;
        }
        const auto y = run (p, block * 4, block, sine440);
        if (! allFinite (y)) ok = false;
    }
    check ("bloques de 0, pequenos y mayores que samplesPerBlock", ok, "todo finito, sin excepciones");

    // Un bloque grande tiene que sonar IGUAL que el mismo audio en bloques
    // pequenos (el troceado no puede cambiar el resultado).
    ReverseVerbProcessor a, b;
    for (auto* q : { &a, &b }) { q->prepareToPlay (48000.0, 256); setParam (*q, "mix", 50.0f); setParam (*q, "revamt", 0.0f); }
    const auto ya = run (a, 8192, 256,  sine440);
    const auto yb = run (b, 8192, 2048, sine440);
    float worst = 0.0f;
    for (size_t i = 0; i < ya.size(); ++i) worst = std::max (worst, std::fabs (ya[i] - yb[i]));
    check ("bloque grande == bloques pequenos", worst < 1.0e-4f, "diferencia maxima " + f (worst, 6));
}

void testBypassTail()
{
    ReverseVerbProcessor p;
    p.prepareToPlay (48000.0, 256);
    setParam (p, "mix", 100.0f);        // solo efecto: la cola se ve sin la seca
    setParam (p, "length", 200.0f);
    setParam (p, "feedback", 50.0f);

    run (p, 48000, 256, sine440);                              // 1 s de senal
    const auto tail = run (p, 48000, 256, silence, true);      // 1 s en BYPASS sin entrada

    // Con bypass con cola, el efecto sigue sonando al principio y se apaga.
    const float early = rms (tail, 0, 4800), late = rms (tail, 43200, 48000);
    check ("bypass deja agotar la cola", early > 0.01f && late < early * 0.5f,
           "RMS primeros 100 ms " + f (early, 4) + ", ultimos 100 ms " + f (late, 4));

    // Y en bypass la seca pasa a 0 dB aunque Mix este al 100 %.
    const auto dry = run (p, 4800, 256, sine440, true);
    const float dryRms = rms (dry, 2400, 4800);
    check ("bypass: la seca sale a 0 dB", std::fabs (20.0f * std::log10 (dryRms / 0.3536f)) < 1.0f,
           "RMS " + f (dryRms, 4) + " (seno de 0.5 = 0.354)");
}

void testStateRoundTrip()
{
    ReverseVerbProcessor p;
    p.prepareToPlay (48000.0, 256);
    setParam (p, "length", 1234.0f);
    setParam (p, "feedback", 42.0f);
    setParam (p, "freeze", 1.0f);
    setParam (p, "revshim", 33.0f);
    setParam (p, "tempo", 97.5f);

    juce::MemoryBlock state;
    p.getStateInformation (state);

    ReverseVerbProcessor q;
    q.prepareToPlay (48000.0, 256);
    q.setStateInformation (state.getData(), (int) state.getSize());

    bool ok = true; std::string diff;
    for (auto* rp : p.getParameters())
        if (auto* a = dynamic_cast<juce::RangedAudioParameter*> (rp))
        {
            const float va = a->getValue();
            const float vb = q.apvts.getParameter (a->paramID)->getValue();
            if (std::fabs (va - vb) > 1.0e-6f) { ok = false; diff += a->paramID.toStdString() + " "; }
        }
    check ("estado: ida y vuelta identica (todos los parametros)", ok,
           ok ? std::to_string (p.getParameters().size()) + " parametros" : "difieren: " + diff);

    // Y el nombre del preset y el flag de modificado sobreviven.
    juce::MemoryBlock s2; q.getStateInformation (s2);
    check ("estado: no queda 'modificado' tras cargar", ! q.getPresets().isDirty(), "isDirty = false");
}

void testPresets()
{
    ReverseVerbProcessor p;
    p.prepareToPlay (48000.0, 256);

    const int n = p.getNumPrograms();
    bool ok = true; std::string bad;
    for (int i = 0; i < n; ++i)
    {
        p.applyFactoryPreset (i);
        if (p.getCurrentProgram() != i || p.getPresets().isDirty()) { ok = false; bad += std::to_string (i) + " "; }

        // Cargar el preset dos veces no puede cambiar nada.
        juce::MemoryBlock s1, s2;
        p.getStateInformation (s1);
        p.applyFactoryPreset (i);
        p.getStateInformation (s2);
        if (s1 != s2) { ok = false; bad += "(idempotente:" + std::to_string (i) + ") "; }

        // Y ninguno produce NaN ni se dispara con 2 s de senal.
        const auto y = run (p, 96000, 512, sine440);
        if (! allFinite (y)) { ok = false; bad += "(nan:" + std::to_string (i) + ") "; }
        float peak = 0; for (float x : y) peak = std::max (peak, std::fabs (x));
        if (peak > 4.0f) { ok = false; bad += "(pico " + f (peak, 2) + " en " + std::to_string (i) + ") "; }
    }
    check ("los presets de fabrica cargan, son idempotentes y suenan finito", ok,
           ok ? std::to_string (n) + " presets" : "fallan: " + bad);

    // El tempo es de sesion: un preset no lo toca.
    setParam (p, "tempo", 133.0f);
    p.applyFactoryPreset (0);
    check ("los presets no tocan el tempo", std::fabs (getParam (p, "tempo") - 133.0f) < 0.01f,
           "tempo tras cargar preset: " + f (getParam (p, "tempo"), 1));
}

void testMidi()
{
    ReverseVerbProcessor p;
    p.prepareToPlay (48000.0, 256);
    auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (30); };

    juce::AudioBuffer<float> buf (2, 256);
    buf.clear();

    // --- learn: el siguiente CC se queda con Freeze ---
    p.getMidi().startLearn ("freeze");
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0); p.processBlock (buf, m); }
    pump();
    check ("MIDI learn asigna el CC", p.getMidi().ccFor ("freeze") == 64 && getParam (p, "freeze") < 0.5f,
           "CC " + std::to_string (p.getMidi().ccFor ("freeze")) + ", y al aprender NO se aplica");

    // --- momentaneo: 127 -> on, 0 -> off ---
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0); p.processBlock (buf, m); }
    pump();
    const bool on = getParam (p, "freeze") > 0.5f;
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0); p.processBlock (buf, m); }
    pump();
    const bool off = getParam (p, "freeze") < 0.5f;
    check ("MIDI momentaneo: pisar = on, soltar = off", on && off, on ? (off ? "ok" : "no suelta") : "no pisa");

    // --- toggle: cada flanco invierte ---
    p.getMidi().setMode ("freeze", MidiControl::Mode::toggle);
    for (int v : { 127, 0, 127, 0 })
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 64, v), 0); p.processBlock (buf, m); pump(); }
    check ("MIDI toggle: dos pulsaciones vuelven al inicio", getParam (p, "freeze") < 0.5f, "freeze = off");

    // --- continuo: CC 7 -> mix ---
    p.getMidi().startLearn ("mix");
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 7, 100), 0); p.processBlock (buf, m); }
    pump();
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 7, 64), 0); p.processBlock (buf, m); }
    pump();
    check ("MIDI continuo: CC 64/127 -> Mix ~50 %", std::fabs (getParam (p, "mix") - 50.4f) < 1.0f,
           "mix = " + f (getParam (p, "mix"), 1));

    // --- program change ---
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::programChange (1, 3), 0); p.processBlock (buf, m); }
    pump();
    check ("MIDI program change -> preset", p.getCurrentProgram() == 3, "programa " + std::to_string (p.getCurrentProgram()));

    // --- tap tempo: 4 taps a 500 ms = 120 BPM ---
    p.getMidi().startLearn (MidiControl::kTapTempo);
    { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 80, 127), 0); p.processBlock (buf, m); }
    pump();
    setParam (p, "tempo", 60.0f);
    for (int t = 0; t < 4; ++t)
    {
        for (int k = 0; k < 24000 / 256; ++k) { juce::MidiBuffer m; p.processBlock (buf, m); }   // 500 ms
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::controllerEvent (1, 80, 0),   0);
        m.addEvent (juce::MidiMessage::controllerEvent (1, 80, 127), 10);
        p.processBlock (buf, m);
        pump();
    }
    const float bpm = getParam (p, "tempo");
    check ("MIDI tap tempo: 4 taps a 500 ms -> 120 BPM", std::fabs (bpm - 120.0f) < 1.0f, "tempo = " + f (bpm, 1));

    // --- los mapeos viajan en el estado ---
    juce::MemoryBlock state; p.getStateInformation (state);
    ReverseVerbProcessor q; q.prepareToPlay (48000.0, 256);
    q.setStateInformation (state.getData(), (int) state.getSize());
    check ("MIDI: los mapeos se guardan con el proyecto",
           q.getMidi().ccFor ("freeze") == 64 && q.getMidi().ccFor ("mix") == 7
           && q.getMidi().modeFor ("freeze") == MidiControl::Mode::toggle,
           "freeze CC " + std::to_string (q.getMidi().ccFor ("freeze")) + " (toggle), mix CC " + std::to_string (q.getMidi().ccFor ("mix")));
}

void testOutputGainAndBalance()
{
    ReverseVerbProcessor p;
    p.prepareToPlay (48000.0, 256);
    setParam (p, "mix", 0.0f);          // solo seca: el trim se mide limpio
    setParam (p, "outgain", -6.0f);
    const auto y = run (p, 9600, 256, sine440);
    const float r = rms (y, 4800, 9600);
    const float dB = 20.0f * std::log10 (r / 0.3536f);
    check ("trim de salida: -6 dB son -6 dB", std::fabs (dB + 6.0f) < 0.3f, "medido " + f (dB, 2) + " dB");

    // En bypass el trim no actua.
    const auto yb = run (p, 9600, 256, sine440, true);
    const float dBb = 20.0f * std::log10 (rms (yb, 4800, 9600) / 0.3536f);
    check ("trim de salida: en bypass no actua", std::fabs (dBb) < 0.3f, "medido " + f (dBb, 2) + " dB");
}

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("\n--- PROCESADOR --------------------------------------------------\n");
    testBlockSizes();
    testBypassTail();
    testStateRoundTrip();
    testPresets();
    testMidi();
    testOutputGainAndBalance();

    std::printf ("\n%d pasaron, %d fallaron\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
