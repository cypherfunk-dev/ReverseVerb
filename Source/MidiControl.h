#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <map>

class ReverseVerbProcessor;

/**
    Control por MIDI: CC -> parametro (con MIDI Learn), Program Change ->
    preset de fabrica, y tap tempo por CC.

    POR QUE NADA SE APLICA EN EL HILO DE AUDIO
    ------------------------------------------
    setValueNotifyingHost() avisa al host, y en VST3 eso (performEdit) tiene
    que salir del hilo de interfaz. Asi que processBlock solo mete los eventos
    en un FIFO sin bloqueos y dispara un AsyncUpdater; toda la logica -tabla
    de mapeos, learn, tap tempo, program change- corre en el hilo de mensajes.
    La latencia extra es la del bucle de mensajes, unos pocos ms: para un
    pedal es indistinguible.

    Consecuencia comoda: la tabla de mapeos solo la toca un hilo, asi que es
    un std::map normal, sin atomicos.

    EL TAP TEMPO SE MIDE EN MUESTRAS
    --------------------------------
    Cada evento lleva la posicion en muestras (contador del procesador +
    offset en el bloque). Medir el tiempo al recibir el evento en el hilo de
    mensajes meteria el jitter del bucle, que a 120 BPM es un 1-2 % de error.
    En muestras es exacto.

    MODOS PARA BOOLEANOS
    --------------------
    - Momentaneo: el parametro sigue al CC (>=64 on). Con un pedal de sustain
      (127 al pisar, 0 al soltar) es "pisar = Freeze". El caso de uso estrella.
    - Toggle: cada flanco de subida invierte el parametro. Para pedaleras que
      mandan 127 en cada pulsacion.
*/
class MidiControl : private juce::AsyncUpdater
{
public:
    enum class Mode { continuous, momentary, toggle };

    /** Destino especial para el tap tempo (no es un parametro). */
    static const juce::String kTapTempo;

    explicit MidiControl (ReverseVerbProcessor&);
    ~MidiControl() override;

    void prepare (double sampleRate) noexcept { sr = sampleRate; }

    // --- hilo de audio ---------------------------------------------------
    /** Encola los eventos del bloque. `blockStart` es la posicion en muestras
        de la primera muestra del bloque. Nunca bloquea ni reserva memoria. */
    void push (const juce::MidiBuffer& midi, juce::int64 blockStart) noexcept;

    // --- hilo de mensajes ------------------------------------------------
    /** El siguiente CC que llegue se asigna a `target` (paramID o kTapTempo). */
    void startLearn (const juce::String& target);
    void cancelLearn();
    bool isLearning (const juce::String& target) const { return learnTarget == target; }
    juce::String learningTarget() const                 { return learnTarget; }

    /** CC asignado a `target`, o -1. */
    int  ccFor (const juce::String& target) const;
    void clearMapping (const juce::String& target);
    Mode modeFor (const juce::String& target) const;
    void setMode (const juce::String& target, Mode);

    /** Ultimo CC recibido y cuando (para que la GUI pueda parpadear). */
    int lastCC() const noexcept { return lastCcSeen.load(); }

    // --- estado ----------------------------------------------------------
    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement&);

private:
    struct Event
    {
        juce::uint8 type;     // 0xB0 CC, 0xC0 program change
        juce::uint8 number;
        juce::uint8 value;
        juce::int64 sample;
    };

    struct Mapping
    {
        juce::String target;
        Mode         mode = Mode::continuous;
        juce::uint8  last = 0;   // ultimo valor, para detectar flancos
    };

    void handleAsyncUpdate() override;
    void apply (const Event&);
    void applyToParam (juce::RangedAudioParameter&, Mapping&, juce::uint8 value);
    void tap (juce::int64 sample);
    static bool isBoolParam (const juce::RangedAudioParameter&);

    ReverseVerbProcessor& proc;
    double sr = 44100.0;

    static constexpr int kFifoSize = 512;
    juce::AbstractFifo fifo { kFifoSize };
    std::array<Event, kFifoSize> events {};

    std::map<int, Mapping> byCC;                 // solo hilo de mensajes
    juce::String learnTarget;                    // solo hilo de mensajes
    std::atomic<int> lastCcSeen { -1 };

    // tap tempo
    std::array<juce::int64, 8> taps {};
    int numTaps = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiControl)
};
