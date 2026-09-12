#include "MidiControl.h"
#include "PluginProcessor.h"

const juce::String MidiControl::kTapTempo { "@tap" };

MidiControl::MidiControl (ReverseVerbProcessor& p) : proc (p) {}

MidiControl::~MidiControl()
{
    cancelPendingUpdate();
}

//==============================================================================
void MidiControl::push (const juce::MidiBuffer& midi, juce::int64 blockStart) noexcept
{
    if (midi.isEmpty())
        return;

    bool any = false;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        Event ev;

        if (m.isController())
        {
            ev.type   = 0xB0;
            ev.number = static_cast<juce::uint8> (m.getControllerNumber());
            ev.value  = static_cast<juce::uint8> (m.getControllerValue());
        }
        else if (m.isProgramChange())
        {
            ev.type   = 0xC0;
            ev.number = static_cast<juce::uint8> (m.getProgramChangeNumber());
            ev.value  = 0;
        }
        else
        {
            continue;
        }

        ev.sample = blockStart + meta.samplePosition;

        // Si el FIFO esta lleno se pierde el evento. Antes que bloquear el
        // hilo de audio, y 512 CCs sin que el hilo de mensajes respire no es
        // un caso que merezca mas.
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
        {
            events[static_cast<size_t> (scope.startIndex1)] = ev;
            any = true;
        }
    }

    if (any)
        triggerAsyncUpdate();
}

//==============================================================================
void MidiControl::handleAsyncUpdate()
{
    while (fifo.getNumReady() > 0)
    {
        const auto scope = fifo.read (1);
        if (scope.blockSize1 > 0)
            apply (events[static_cast<size_t> (scope.startIndex1)]);
    }
}

bool MidiControl::isBoolParam (const juce::RangedAudioParameter& p)
{
    return dynamic_cast<const juce::AudioParameterBool*> (&p) != nullptr;
}

void MidiControl::apply (const Event& ev)
{
    if (ev.type == 0xC0)
    {
        proc.applyFactoryPreset (ev.number);
        return;
    }

    const int cc = ev.number;
    lastCcSeen.store (cc);

    // --- learn: el primer CC que llega se queda con el destino ------------
    if (learnTarget.isNotEmpty())
    {
        // Un destino solo puede tener un CC; se limpia el anterior.
        clearMapping (learnTarget);

        Mapping m;
        m.target = learnTarget;
        if (learnTarget != kTapTempo)
            if (auto* p = proc.apvts.getParameter (learnTarget))
                m.mode = isBoolParam (*p) ? Mode::momentary : Mode::continuous;

        m.last = ev.value;
        byCC[cc] = m;
        learnTarget.clear();

        // Al aprender no se aplica el valor: si has pisado un pedal para
        // asignarlo a Freeze, no quieres congelar ya.
        return;
    }

    auto it = byCC.find (cc);
    if (it == byCC.end())
        return;

    auto& m = it->second;

    if (m.target == kTapTempo)
    {
        if (ev.value >= 64 && m.last < 64)
            tap (ev.sample);
    }
    else if (auto* p = proc.apvts.getParameter (m.target))
    {
        applyToParam (*p, m, ev.value);
    }

    m.last = ev.value;
}

void MidiControl::applyToParam (juce::RangedAudioParameter& p, Mapping& m, juce::uint8 value)
{
    float norm = -1.0f;

    switch (m.mode)
    {
        case Mode::continuous:
            norm = static_cast<float> (value) / 127.0f;
            break;

        case Mode::momentary:
            norm = value >= 64 ? 1.0f : 0.0f;
            break;

        case Mode::toggle:
            if (value >= 64 && m.last < 64)
                norm = p.getValue() >= 0.5f ? 0.0f : 1.0f;
            break;
    }

    if (norm < 0.0f)
        return;

    // Con gesto, para que el host lo pueda grabar como automatizacion.
    p.beginChangeGesture();
    p.setValueNotifyingHost (norm);
    p.endChangeGesture();
}

//==============================================================================
void MidiControl::tap (juce::int64 sample)
{
    // Mas de 2 s desde el ultimo tap: es una serie nueva.
    if (numTaps > 0 && sample - taps[static_cast<size_t> (numTaps - 1)] > static_cast<juce::int64> (2.0 * sr))
        numTaps = 0;

    if (numTaps == static_cast<int> (taps.size()))
    {
        for (size_t i = 1; i < taps.size(); ++i)
            taps[i - 1] = taps[i];
        --numTaps;
    }

    taps[static_cast<size_t> (numTaps++)] = sample;

    if (numTaps < 2)
        return;

    // Media de los ultimos 4 intervalos como mucho: suficiente para que un
    // tap torpe no lo desvie todo y pocos para que reaccione a un cambio.
    const int intervals = juce::jmin (4, numTaps - 1);
    double total = 0.0;
    for (int i = 0; i < intervals; ++i)
        total += static_cast<double> (taps[static_cast<size_t> (numTaps - 1 - i)]
                                    - taps[static_cast<size_t> (numTaps - 2 - i)]);

    const double avgSamples = total / intervals;
    const double bpm = 60.0 * sr / avgSamples;

    if (auto* p = proc.apvts.getParameter ("tempo"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (bpm)));
        p->endChangeGesture();
    }
}

//==============================================================================
void MidiControl::startLearn (const juce::String& target) { learnTarget = target; }
void MidiControl::cancelLearn()                            { learnTarget.clear(); }

int MidiControl::ccFor (const juce::String& target) const
{
    for (const auto& [cc, m] : byCC)
        if (m.target == target)
            return cc;
    return -1;
}

void MidiControl::clearMapping (const juce::String& target)
{
    for (auto it = byCC.begin(); it != byCC.end();)
        it = (it->second.target == target) ? byCC.erase (it) : std::next (it);
}

MidiControl::Mode MidiControl::modeFor (const juce::String& target) const
{
    for (const auto& [cc, m] : byCC)
        if (m.target == target)
            return m.mode;
    return Mode::continuous;
}

void MidiControl::setMode (const juce::String& target, Mode mode)
{
    for (auto& [cc, m] : byCC)
        if (m.target == target)
            m.mode = mode;
}

//==============================================================================
std::unique_ptr<juce::XmlElement> MidiControl::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("MIDI");

    for (const auto& [cc, m] : byCC)
    {
        auto* e = xml->createNewChildElement ("CC");
        e->setAttribute ("number", cc);
        e->setAttribute ("target", m.target);
        e->setAttribute ("mode",   static_cast<int> (m.mode));
    }

    return xml;
}

void MidiControl::fromXml (const juce::XmlElement& xml)
{
    byCC.clear();

    for (auto* e : xml.getChildWithTagNameIterator ("CC"))
    {
        const int cc = e->getIntAttribute ("number", -1);
        if (! juce::isPositiveAndBelow (cc, 128))
            continue;

        Mapping m;
        m.target = e->getStringAttribute ("target");
        m.mode   = static_cast<Mode> (juce::jlimit (0, 2, e->getIntAttribute ("mode", 0)));

        if (m.target.isNotEmpty())
            byCC[cc] = m;
    }
}
