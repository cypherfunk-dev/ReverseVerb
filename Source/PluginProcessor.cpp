#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>

namespace
{
    /**
        AudioParameterBool con getValue() cuantizado.

        BUG QUE DETECTO PLUGINVAL
        -------------------------
        JUCE guarda dentro de AudioParameterBool el valor normalizado CRUDO que
        le manda el host:

            void setValue (float v) { value = v; ... }
            float getValue() const  { return value; }
            bool  get() const       { return value >= 0.5f; }

        Si el host escribe 0.4393, get() devuelve false (correcto) pero
        getValue() sigue devolviendo 0.4393.

        APVTS, en cambio, guarda en su ValueTree el valor ya cuantizado (0 o 1).
        Al restaurar el estado compara ese valor cuantizado con el que ya tiene,
        ve que ambos son 0, concluye que no hay cambio y NO llama a setValue.
        El float crudo se queda con el 0.4393 rancio.

        Consecuencia: el plugin SUENA bien, porque get() siempre da lo correcto.
        Pero el host lee un valor distinto del que guardo, lo que rompe la
        comparacion de estados y puede confundir al undo o al A/B del DAW.

        POR QUE SE SOBRESCRIBE getValue Y NO setValue
        ---------------------------------------------
        La solucion evidente seria cuantizar en setValue. No se puede: en JUCE
        setValue es PRIVADO. Sobrescribir un virtual privado es legal en C++
        (es la base del idiom NVI), pero llamar a la implementacion del padre
        desde la clase derivada no lo es, y hace falta llamarla para guardar el
        valor.

        Sobrescribir getValue si funciona: es igualmente virtual, y solo
        necesita get(), que es publico. El valor crudo interno sigue siendo el
        que sea, pero hacia fuera el parametro siempre reporta 0 o 1 exactos,
        que es justo lo que APVTS guarda. El viaje de ida y vuelta cuadra.
    */
    class SnappedBoolParameter : public juce::AudioParameterBool
    {
    public:
        using juce::AudioParameterBool::AudioParameterBool;

    private:
        float getValue() const override { return get() ? 1.0f : 0.0f; }
    };

    constexpr float kMinMs   = 20.0f;
    constexpr float kMaxMs   = 2000.0f;
    constexpr float kMaxDrive = 8.0f;
    constexpr int   kStateVersion = 1;

    // Multiplicadores en NEGRAS. Una negra dura 60000/BPM ms.
    const float kDivMul[] = { 4.0f, 2.0f, 1.5f, 1.0f, 2.0f/3.0f,
                              0.75f, 0.5f, 1.0f/3.0f, 0.25f, 1.0f/6.0f };
}

juce::StringArray ReverseVerbProcessor::divisionNames()
{
    return { "1/1", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16", "1/16T" };
}

juce::StringArray ReverseVerbProcessor::routingNames()
{
    return { "Off", "Delay -> Rev", "Rev -> Delay", "Paralelo" };
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ReverseVerbProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto lab = [] (const char* s) { return AudioParameterFloatAttributes().withLabel (s); };

    auto timeRange = [] { return NormalisableRange<float> (kMinMs, kMaxMs, 1.0f, 0.4f); };
    auto pctRange  = [] { return NormalisableRange<float> (0.0f, 100.0f, 0.1f); };

    // --- REVERSE ---
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "length", 1 }, "Length",
        timeRange(), 500.0f, lab ("ms")));
    l.add (std::make_unique<SnappedBoolParameter>  (ParameterID { "sync", 1 }, "Rev Sync", false));
    l.add (std::make_unique<AudioParameterChoice>(ParameterID { "division", 1 }, "Rev Div",
        divisionNames(), 3));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "feedback", 1 }, "Rev Feedback",
        pctRange(), 0.0f, lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive",
        NormalisableRange<float> (1.0f, kMaxDrive, 0.01f), 1.0f));
    // Cuanto empuja la envolvente de la senal seca al drive del lazo. Tocar
    // suave deja la cola limpia; atacar fuerte la satura.
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "driveenv", 1 }, "Drive Env",
        pctRange(), 0.0f, lab ("%")));
    l.add (std::make_unique<SnappedBoolParameter> (ParameterID { "freeze", 1 }, "Freeze", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "highpass", 1 }, "Rev Low Cut",
        NormalisableRange<float> (20.0f, 2000.0f, 1.0f, 0.3f), 20.0f, lab ("Hz")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lowpass", 1 }, "Rev High Cut",
        NormalisableRange<float> (200.0f, 20000.0f, 1.0f, 0.3f), 20000.0f, lab ("Hz")));

    // --- TAPE ---
    // Actuan sobre el motor de reverse: modulan su posicion de lectura.
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "wow", 1 }, "Wow",
        pctRange(), 0.0f, lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "flutter", 1 }, "Flutter",
        pctRange(), 0.0f, lab ("%")));
    // Desafina los canales en sentidos OPUESTOS: +c en L, -c en R. Ese batido
    // entre canales es lo que ensancha la imagen sin necesidad de un reverb.
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "detune", 1 }, "Detune",
        NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 0.0f, lab ("cents")));

    // Parte de la realimentacion pasa por un pitch shifter: cada pasada del
    // lazo sube de altura y se van apilando octavas.
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "shimmer", 1 }, "Shimmer",
        pctRange(), 0.0f, lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "shimpitch", 1 }, "Shim Pitch",
        NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 12.0f, lab ("st")));

    // --- DELAY ---
    l.add (std::make_unique<AudioParameterChoice>(ParameterID { "routing", 1 }, "Routing",
        routingNames(), 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dtime", 1 }, "Delay Time",
        timeRange(), 375.0f, lab ("ms")));
    l.add (std::make_unique<SnappedBoolParameter>  (ParameterID { "dsync", 1 }, "Dly Sync", false));
    l.add (std::make_unique<AudioParameterChoice>(ParameterID { "ddiv", 1 }, "Dly Div",
        divisionNames(), 6));   // 1/8 por defecto
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dfeed", 1 }, "Delay Feedback",
        pctRange(), 35.0f, lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dhigh", 1 }, "Dly Low Cut",
        NormalisableRange<float> (20.0f, 2000.0f, 1.0f, 0.3f), 20.0f, lab ("Hz")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dlow", 1 }, "Dly High Cut",
        NormalisableRange<float> (200.0f, 20000.0f, 1.0f, 0.3f), 20000.0f, lab ("Hz")));
    l.add (std::make_unique<SnappedBoolParameter>  (ParameterID { "pingpong", 1 }, "Ping-Pong", false));
    // Chorus en la cola del delay: modula su tiempo de lectura.
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dmoddepth", 1 }, "Dly Mod",
        pctRange(), 0.0f, lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dmodrate", 1 }, "Dly Rate",
        NormalisableRange<float> (0.05f, 8.0f, 0.01f, 0.4f), 0.6f, lab ("Hz")));

    // --- SPACE ---
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "revamt", 1 }, "Reverb",
        pctRange(), 0.0f, lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "revsize", 1 }, "Size",
        pctRange(), 70.0f, lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "revdamp", 1 }, "Damp",
        pctRange(), 40.0f, lab ("%")));
    l.add (std::make_unique<SnappedBoolParameter>  (ParameterID { "revpost", 1 }, "Reverb Post", true));

    // --- OUTPUT ---
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
        pctRange(), 50.0f, lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "duck", 1 }, "Duck",
        pctRange(), 0.0f, lab ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "duckrel", 1 }, "Duck Rel",
        NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.5f), 250.0f, lab ("ms")));

    return l;
}

//==============================================================================
ReverseVerbProcessor::ReverseVerbProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    pLength = get ("length");   pSync    = get ("sync");     pDivision = get ("division");
    pFeedback = get ("feedback"); pDrive  = get ("drive");
    pDriveEnv = get ("driveenv"); pFreeze = get ("freeze");
    pWow      = get ("wow");      pFlutter = get ("flutter"); pDetune = get ("detune");
    pShimmer  = get ("shimmer");  pShimPitch = get ("shimpitch");
    pDModRate = get ("dmodrate"); pDModDepth = get ("dmoddepth");
    pHighPass = get ("highpass"); pLowPass = get ("lowpass");

    pRouting = get ("routing");  pDTime   = get ("dtime");   pDSync = get ("dsync");
    pDDiv    = get ("ddiv");     pDFeed   = get ("dfeed");
    pDHigh   = get ("dhigh");    pDLow    = get ("dlow");    pPingPong = get ("pingpong");

    pRevAmt  = get ("revamt");   pRevSize = get ("revsize");
    pRevDamp = get ("revdamp");  pRevPost = get ("revpost");

    pMix     = get ("mix");      pDuck    = get ("duck");    pDuckRel = get ("duckrel");
}

juce::AudioProcessorEditor* ReverseVerbProcessor::createEditor()
{
    return new ReverseVerbEditor (*this);
}

//==============================================================================
float ReverseVerbProcessor::resolveMs (std::atomic<float>* msParam,
                                       std::atomic<float>* syncParam,
                                       std::atomic<float>* divParam) const
{
    if (syncParam->load() < 0.5f)
        return msParam->load();

    const int idx = juce::jlimit (0, 9, static_cast<int> (divParam->load()));
    const double quarterMs = 60000.0 / juce::jmax (20.0, hostBpm);

    return juce::jlimit (kMinMs, kMaxMs,
                         static_cast<float> (quarterMs * kDivMul[idx]));
}

//==============================================================================
void ReverseVerbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Se reserva SIEMPRE para el maximo, no para el valor actual. Asi mover un
    // slider nunca provoca una allocacion en el audio thread.
    const int maxSamples = static_cast<int> (kMaxMs * 0.001 * sampleRate) + 1;
    const int numCh      = juce::jmax (1, getTotalNumOutputChannels());

    const int initRev = static_cast<int> (resolveMs (pLength, pSync, pDivision) * 0.001f * sampleRate);

    delays.resize (static_cast<size_t> (numCh));
    for (auto& d : delays)
        d.prepare (maxSamples, initRev, sampleRate);

    visStereo.store (numCh > 1);

    echo.prepare (maxSamples, sampleRate, numCh,
                  resolveMs (pDTime, pDSync, pDDiv) * 0.001f * static_cast<float> (sampleRate));

    ducker.prepare (sampleRate);

    // Release corto y fijo: el drive tiene que seguir el ataque de la pulsacion,
    // no el fraseo. El del ducking si es ajustable porque hace otra cosa.
    driveEnv.prepare (sampleRate);
    driveEnv.setRelease (120.0f);

    driveCurve.assign (static_cast<size_t> (juce::jmax (1, samplesPerBlock)), 1.0f);
    modCurve  .assign (static_cast<size_t> (juce::jmax (1, samplesPerBlock)), 0.0f);

    tape.prepare (sampleRate);

    wetBuffer.setSize (numCh, juce::jmax (1, samplesPerBlock));
    parBuffer.setSize (numCh, juce::jmax (1, samplesPerBlock));
    wetBuffer.clear();
    parBuffer.clear();

    reverb.setSampleRate (sampleRate);
    reverb.reset();

    const float mix0 = pMix->load() * 0.01f;
    drySmoothed.reset (sampleRate, 0.02);
    wetSmoothed.reset (sampleRate, 0.02);
    drySmoothed.setCurrentAndTargetValue (1.0f - mix0);
    wetSmoothed.setCurrentAndTargetValue (mix0);

    revFbSm   = pFeedback->load() * 0.01f;
    echoFbSm  = pDFeed->load()    * 0.01f;
    revAmtSm  = pRevAmt->load()   * 0.01f;
    revSizeSm = pRevSize->load()  * 0.01f;
    revDampSm = pRevDamp->load()  * 0.01f;
    reverbActive = revAmtSm > 0.0001f;
    bypassed     = false;

    // NOTA DELIBERADA: no se llama a setLatencySamples().
    // El desfase de una ventana ES el efecto. Si lo reportaramos, el host
    // adelantaria el plugin para compensarlo y anularia el reverse.
}

bool ReverseVerbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    auto monoOrStereo = [] (const juce::AudioChannelSet& s)
    {
        return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo();
    };

    if (! monoOrStereo (in) || ! monoOrStereo (out))
        return false;

    // Se admite mono->mono, estereo->estereo y MONO->ESTEREO, que es el caso
    // real de una cadena de guitarra (DI mono -> ampli -> este plugin): sin el,
    // el ping-pong y la anchura del reverb no tendrian donde actuar.
    //
    // Lo que NO se admite es estereo->mono: perderiamos el ping-pong en la
    // suma, y el host sabe hacer esa mezcla mejor que nosotros.
    return out.size() >= in.size();
}

float ReverseVerbProcessor::blockSmooth (float current, float target,
                                         int numSamples, float seconds) const noexcept
{
    // Un polo evaluado una vez por bloque. El coeficiente sale del numero de
    // muestras del bloque, asi que 50 ms son 50 ms tanto a 64 como a 2048.
    const float k = 1.0f - std::exp (-static_cast<float> (numSamples)
                                     / (seconds * static_cast<float> (currentSampleRate)));
    return current + k * (target - current);
}

void ReverseVerbProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                 juce::MidiBuffer& midi)
{
    bypassed = true;
    processBlock (buffer, midi);
    bypassed = false;
}

void ReverseVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numCh      = juce::jmin (numOut, static_cast<int> (delays.size()));

    // Algunos hosts mandan bloques vacios (Reaper parado, renders offline).
    // Sin esto, mas abajo se leeria driveCurve[numSamples - 1] con indice -1.
    if (numSamples <= 0)
        return;

    // Bloque MAYOR de lo anunciado en prepareToPlay. FL Studio y algunos hosts
    // al congelar pistas lo hacen. Antes se devolvia la senal seca sin mezclar,
    // que con Mix al 50 % era un salto de +6 dB y un hueco en la cola. Ahora
    // se trocea al tamano reservado y cada trozo pasa por aqui otra vez.
    const int capacity = wetBuffer.getNumSamples();
    if (numSamples > capacity)
    {
        if (capacity <= 0)
            return;   // prepareToPlay no se ha llamado: no hay donde procesar

        for (int start = 0; start < numSamples; start += capacity)
        {
            const int len = juce::jmin (capacity, numSamples - start);
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(),
                                          buffer.getNumChannels(), start, len);
            processBlock (sub, midi);
        }
        return;
    }

    if (numIn == 1 && numOut > 1)
    {
        // MONO -> ESTEREO: se duplica la entrada ANTES de procesar. Si solo
        // limpiaramos los canales sobrantes, el ping-pong y el reverb estarian
        // trabajando contra silencio en el canal derecho.
        for (int ch = 1; ch < numOut; ++ch)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);
    }
    else
    {
        for (int ch = numIn; ch < numOut; ++ch)
            buffer.clear (ch, 0, numSamples);
    }

    if (numCh <= 0)
        return;

    // --- tempo del host --------------------------------------------------
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    const float revMs   = resolveMs (pLength, pSync,  pDivision);
    const float delayMs = resolveMs (pDTime,  pDSync, pDDiv);
    visLengthMs.store (revMs);
    visDelayMs.store (delayMs);
    visBpm.store (static_cast<float> (hostBpm));

    // --- ajustes de bloque -----------------------------------------------
    const int revSamples = juce::jlimit (2, static_cast<int> (kMaxMs * 0.001 * currentSampleRate),
                                         static_cast<int> (revMs * 0.001f * currentSampleRate));
    const float revHp = pHighPass->load();
    const float revLp = juce::jmax (pLowPass->load(), revHp * 1.05f);

    const bool frozen = pFreeze->load() > 0.5f;
    visFrozen.store (frozen);

    // Detune: los canales se desafinan en sentidos opuestos. rate = 2^(c/1200).
    const float detuneCents = pDetune->load();

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& d = delays[static_cast<size_t> (ch)];
        d.setLength (revSamples);
        d.setFeedbackTone (revHp, revLp);
        d.setFrozen (frozen);

        const float sign = (numCh > 1 && ch == 1) ? -1.0f : 1.0f;
        d.setReadRate (std::pow (2.0f, sign * detuneCents / 1200.0f));

        d.setShimmer (pShimmer->load() * 0.01f, pShimPitch->load());
    }

    // --- wow y flutter ----------------------------------------------------
    // UN SOLO modulador para todos los canales: una cinta real tiene un unico
    // transporte. Modular cada canal por separado sonaria a chorus, no a cinta.
    // (El ensanchado estereo lo hace Detune, que es otra cosa.)
    {
        const float wowAmt  = pWow->load()     * 0.01f;
        const float flutAmt = pFlutter->load() * 0.01f;
        const bool  active  = wowAmt > 0.0001f || flutAmt > 0.0001f;

        for (int n = 0; n < numSamples; ++n)
            modCurve[static_cast<size_t> (n)] = active ? tape.next (wowAmt, flutAmt) : 0.0f;
    }

    // --- drive dinamico ---------------------------------------------------
    // Se calcula por MUESTRA y no por bloque: a 512 muestras la granularidad
    // seria de 10 ms y el drive llegaria tarde al ataque de la pulsacion, que
    // es justo lo que se quiere seguir.
    //
    // Se precalcula en una curva en vez de dentro de runReverse porque las
    // rutas Pre/Post/Paralelo llaman a los motores en distinto orden, y la
    // envolvente debe avanzar exactamente una vez por muestra.
    const float baseDrive = pDrive->load();
    const float envAmount = pDriveEnv->load() * 0.01f;

    for (int n = 0; n < numSamples; ++n)
    {
        float dryAbs = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            dryAbs = juce::jmax (dryAbs, std::abs (buffer.getReadPointer (ch)[n]));

        driveCurve[static_cast<size_t> (n)] =
            baseDrive + (kMaxDrive - baseDrive) * envAmount * driveEnv.follow (dryAbs);
    }
    visDrive.store (driveCurve[static_cast<size_t> (numSamples - 1)]);

    const float dHp = pDHigh->load();
    echo.setDelaySamples (delayMs * 0.001f * static_cast<float> (currentSampleRate));
    echo.setTone (dHp, juce::jmax (pDLow->load(), dHp * 1.05f));
    echo.setPingPong (pPingPong->load() > 0.5f);

    // Suavizado por bloque. Deliberadamente NO uso SmoothedValue aqui: las
    // rutas Pre/Post/Paralelo llaman a los motores en distinto orden, y
    // getNextValue() se adelantaria un numero distinto de veces segun la ruta.
    revFbSm  = blockSmooth (revFbSm,  pFeedback->load() * 0.01f, numSamples, 0.05f);
    echoFbSm = blockSmooth (echoFbSm, pDFeed->load()    * 0.01f, numSamples, 0.05f);

    // En bypass la seca sube a 1 y la humeda conserva su nivel: la cola se
    // agota sola porque mas abajo el efecto recibe silencio en vez de entrada.
    const float mixTarget = pMix->load() * 0.01f;
    drySmoothed.setTargetValue (bypassed ? 1.0f : 1.0f - mixTarget);
    wetSmoothed.setTargetValue (mixTarget);

    // Reverb: los tres parametros suavizados, porque juce::Reverb los aplica
    // de golpe y automatizarlos daba escalones. Y si estaba en bypass (amount
    // a 0) y vuelve a subir, se resetea: si no, soltaba la cola rancia que
    // tenia dentro de cuando se apago.
    revAmtSm  = blockSmooth (revAmtSm,  pRevAmt->load()  * 0.01f, numSamples, 0.05f);
    revSizeSm = blockSmooth (revSizeSm, pRevSize->load() * 0.01f, numSamples, 0.05f);
    revDampSm = blockSmooth (revDampSm, pRevDamp->load() * 0.01f, numSamples, 0.05f);

    const float revAmt = revAmtSm;
    const bool  revOn  = revAmt > 0.0001f;
    if (revOn && ! reverbActive)
        reverb.reset();
    reverbActive = revOn;

    juce::Reverb::Parameters rp;
    rp.roomSize   = revSizeSm;
    rp.damping    = revDampSm;
    rp.width      = 1.0f;
    rp.freezeMode = 0.0f;
    // El wet interno de juce::Reverb se escala por 3, asi que 0.4 ya es
    // "reverb a tope" sin que pegue un salto de nivel.
    rp.wetLevel   = revAmt * 0.4f;
    rp.dryLevel   = 1.0f - revAmt;
    reverb.setParameters (rp);

    ducker.setRelease (pDuckRel->load());

    // --- helpers ---------------------------------------------------------
    auto applyReverb = [&] (juce::AudioBuffer<float>& b)
    {
        if (revAmt <= 0.0001f)
            return;

        if (numCh >= 2) reverb.processStereo (b.getWritePointer (0), b.getWritePointer (1), numSamples);
        else            reverb.processMono   (b.getWritePointer (0), numSamples);
    };

    auto runReverse = [&] (juce::AudioBuffer<float>& b)
    {
        for (int n = 0; n < numSamples; ++n)
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& dl = delays[static_cast<size_t> (ch)];
                dl.setDrive (driveCurve[static_cast<size_t> (n)]);
                dl.setModulation (modCurve[static_cast<size_t> (n)]);

                auto* w = b.getWritePointer (ch);
                w[n] = dl.process (w[n], revFbSm);
            }
    };

    // Chorus del delay. El LFO avanza dentro de runEcho porque esa lambda se
    // llama como mucho UNA vez por bloque, sea cual sea el ruteo. Ponerlo fuera
    // obligaria a otro buffer precalculado sin ganar nada.
    const float dModDepthSamples = pDModDepth->load() * 0.01f
                                 * 0.008f * static_cast<float> (currentSampleRate);  // hasta 8 ms
    const float dModInc = juce::MathConstants<float>::twoPi * pDModRate->load()
                        / static_cast<float> (currentSampleRate);

    auto runEcho = [&] (juce::AudioBuffer<float>& b)
    {
        auto* L = b.getWritePointer (0);
        auto* R = numCh > 1 ? b.getWritePointer (1) : nullptr;

        for (int n = 0; n < numSamples; ++n)
        {
            if (dModDepthSamples > 0.01f)
            {
                dModPhase += dModInc;
                if (dModPhase >= juce::MathConstants<float>::twoPi)
                    dModPhase -= juce::MathConstants<float>::twoPi;

                // R desfasado 90 grados: abre la imagen estereo.
                echo.setModulation (dModDepthSamples * std::sin (dModPhase),
                                    dModDepthSamples * std::cos (dModPhase));
            }
            else
            {
                echo.setModulation (0.0f, 0.0f);
            }

            float l = L[n];
            float r = R != nullptr ? R[n] : 0.0f;
            echo.process (l, r, echoFbSm);
            L[n] = l;
            if (R != nullptr) R[n] = r;
        }
    };

    // --- cadena ----------------------------------------------------------
    if (bypassed)
        wetBuffer.clear (0, numSamples);   // el efecto sigue corriendo, pero sin entrada
    else
        for (int ch = 0; ch < numCh; ++ch)
            wetBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    if (pRevPost->load() <= 0.5f)
        applyReverb (wetBuffer);          // reverb ANTES: la cola tambien se invierte

    switch (juce::jlimit (0, 3, static_cast<int> (pRouting->load())))
    {
        case routeOff:
            runReverse (wetBuffer);
            break;

        case routePre:                    // ecos ritmicos que luego se invierten
            runEcho (wetBuffer);
            runReverse (wetBuffer);
            break;

        case routePost:                   // el swell entero se repite
            runReverse (wetBuffer);
            runEcho (wetBuffer);
            break;

        case routeParallel:
            for (int ch = 0; ch < numCh; ++ch)
                parBuffer.copyFrom (ch, 0, wetBuffer, ch, 0, numSamples);

            runReverse (wetBuffer);
            runEcho (parBuffer);

            // 0.7 en cada rama: dos senales decorrelacionadas suman en
            // potencia, asi que sumarlas a 1.0 dispararia el nivel.
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* w = wetBuffer.getWritePointer (ch);
                const auto* p = parBuffer.getReadPointer (ch);
                for (int n = 0; n < numSamples; ++n)
                    w[n] = 0.7f * w[n] + 0.7f * p[n];
            }
            break;

        default:
            break;
    }

    if (pRevPost->load() > 0.5f)
        applyReverb (wetBuffer);          // reverb DESPUES: el swell clasico

    // --- ducking + mezcla -------------------------------------------------
    // El pico de entrada hay que leerlo AHORA: la mezcla final sobrescribe
    // `buffer` in-place y despues ya no queda rastro de la senal seca.
    std::array<float, 2> dryPeak { 0.0f, 0.0f };
    for (int ch = 0; ch < numCh && ch < 2; ++ch)
        dryPeak[static_cast<size_t> (ch)] = buffer.getMagnitude (ch, 0, numSamples);

    // En bypass no se duckea: la cola se agota tal cual, sin que la seca la
    // module.
    const float duckAmt = bypassed ? 0.0f : pDuck->load() * 0.01f;
    float lastDuck = 1.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const float dryGain = drySmoothed.getNextValue();
        const float wetGain = wetSmoothed.getNextValue();

        // El ducking mira la senal SECA, que es la que toca el musico.
        float dryAbs = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            dryAbs = juce::jmax (dryAbs, std::abs (buffer.getReadPointer (ch)[n]));

        const float duck = ducker.process (dryAbs, duckAmt);
        lastDuck = duck;

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* out = buffer.getWritePointer (ch);
            out[n] = out[n] * dryGain
                   + wetBuffer.getReadPointer (ch)[n] * wetGain * duck;
        }
    }

    visPhase.store (delays[0].phaseNormA());
    visDuckGain.store (lastDuck);

    // --- medidores --------------------------------------------------------
    // Pico del bloque con caida exponencial. La caida se aplica una vez por
    // bloque y no por muestra: es un indicador visual, no un detector.
    {
        float inPk = 0.0f, outPk = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            inPk  = juce::jmax (inPk,  dryPeak[static_cast<size_t> (ch)]);
            outPk = juce::jmax (outPk, buffer.getMagnitude (ch, 0, numSamples));
        }

        const float fall = std::pow (0.72f, static_cast<float> (numSamples) / 512.0f);
        visInPeak .store (juce::jmax (inPk,  visInPeak .load() * fall));
        visOutPeak.store (juce::jmax (outPk, visOutPeak.load() * fall));
    }
}

//==============================================================================
// PRESETS
//==============================================================================
int ReverseVerbProcessor::getNumPrograms()
{
    return PresetManager::getNumFactoryPresets();
}

const juce::String ReverseVerbProcessor::getProgramName (int index)
{
    return PresetManager::getFactoryPresetName (index);
}

void ReverseVerbProcessor::setCurrentProgram (int index)
{
    // TRAMPA CLASICA DE VST3: muchos hosts llaman a setCurrentProgram DESPUES
    // de restaurar el estado del proyecto. Si aplicaramos el preset sin mirar,
    // machacariamos los ajustes que el usuario guardo en su sesion y no
    // entenderia por que su cancion suena distinta al reabrirla.
    //
    // Por eso el indice de programa viaja DENTRO del estado (ver
    // get/setStateInformation) y aqui solo actuamos si de verdad ha cambiado.
    if (index == currentProgram || ! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;
    presetManager.loadFactoryPreset (index);
}

void ReverseVerbProcessor::applyFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;
    presetManager.loadFactoryPreset (index);
    updateHostDisplay();
}

//==============================================================================
void ReverseVerbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        // Version del formato de estado. Hoy no se usa para nada: APVTS ya
        // rellena con el default cualquier parametro que falte. Existe para
        // el dia que cambie el RANGO de un parametro y haya que migrar el
        // valor guardado, porque entonces sin esto no hay forma de saber de
        // que version viene el proyecto.
        xml->setAttribute ("version",    kStateVersion);
        xml->setAttribute ("program",    currentProgram);
        xml->setAttribute ("presetName", presetManager.currentName);
        copyXmlToBinary (*xml, destData);
    }
}

void ReverseVerbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            // Se lee el indice pero NO se aplica el preset: el estado que
            // acabamos de cargar ya trae los valores buenos.
            currentProgram = xml->getIntAttribute ("program", 0);
            presetManager.currentName = xml->getStringAttribute ("presetName", "Init");

            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            // Lo que se acaba de cargar ES el estado de referencia: no cuenta
            // como "modificado" respecto al preset.
            presetManager.markClean();
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReverseVerbProcessor();
}
