#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <memory>

//==============================================================================
/** Knob de arco. Nada exotico: arco de fondo, arco de valor y puntero. */
class KnobLNF : public juce::LookAndFeel_V4
{
public:
    KnobLNF();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

//==============================================================================
/** Slider y ToggleButton que ceden el clic DERECHO a un callback (el menu de
    MIDI Learn) en vez de arrastrar o conmutar. Sin esto, un Slider sin menu
    propio empieza un arrastre con el boton derecho, y un Button dispara el
    clic al soltarlo. */
struct MidiSlider : public juce::Slider
{
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDrag (e); }
    void mouseUp   (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseUp (e); }
};

struct MidiToggle : public juce::ToggleButton
{
    using juce::ToggleButton::ToggleButton;
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::ToggleButton::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ToggleButton::mouseUp (e); }
};

//==============================================================================
/** Agujero de gusano. La estetica es deliberada, pero NO es decoracion pura:

      - Las dos particulas que orbitan la boca son los dos granos. Su brillo es
        literalmente sin(pi*fase/L), la ganancia real de su ventana. Si alguna
        vez dejan de cruzarse a media intensidad, el bug se ve antes de oirse.
      - La torsion del tunel sigue al feedback.
      - El halo del nucleo sigue al reverb, y se apaga cuando el ducking actua.

    Solo repinta este componente a 30 Hz; el resto de la ventana es estatico.
    Importa: una GUI que repinta mucho compite con el hilo de audio y en
    maquinas justas se oye como cortes. */
class WormholeView : public juce::Component,
                     private juce::Timer
{
public:
    explicit WormholeView (ReverseVerbProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    ReverseVerbProcessor& proc;
    std::atomic<float>* pFeedback = nullptr;
    std::atomic<float>* pRevAmt   = nullptr;
    std::atomic<float>* pFreeze   = nullptr;
    float tunnel = 0.0f, slow = 0.0f;
};

//==============================================================================
/** Medidores de pico de entrada y salida.

    Con feedback alto, shimmer y saturacion en el lazo es muy facil pasarse de
    nivel sin notarlo, porque las colas largas suben despacio. El oido se
    acostumbra antes de que suene claramente mal. */
class Meters : public juce::Component,
               private juce::Timer
{
public:
    explicit Meters (ReverseVerbProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    void drawBar (juce::Graphics&, juce::Rectangle<float>, const char* label, float peak);

    ReverseVerbProcessor& proc;
};

/** Contenedor de tamano FIJO (el "tamano de diseno"). El editor lo escala con
    una transformacion, asi la ventana es redimensionable sin recalcular ni un
    solo rectangulo, y se ve igual con cualquier escalado de Windows. */
class BackPanel : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
};

//==============================================================================
class ReverseVerbEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit ReverseVerbEditor (ReverseVerbProcessor&);
    ~ReverseVerbEditor() override;

    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        MidiSlider   slider;
        juce::Label  label;
        juce::String paramID, baseText;   // el texto sin el sufijo " · CC n"
        std::unique_ptr<SliderAtt> att;
    };

    void addKnob (Knob&, const juce::String& paramID, const juce::String& text);
    void layoutRow (Knob* const* knobs, int count, int y);

    /** Activa/desactiva controles segun sync, ruteo y si hay estereo de verdad.
        Va en un Timer porque el numero de canales lo decide el host y puede
        cambiar sin que el usuario toque nada en la GUI. */
    void updateEnablement();

    /** Mantiene el combo de presets en sintonia con el procesador: si el host
        cambia de programa desde su propio menu, o se carga un proyecto, el
        nombre tiene que actualizarse solo. Y si el usuario toca un knob tras
        cargar un preset, se marca con un asterisco. */
    void syncPresetDisplay();
    int  presetIdForName (const juce::String& name) const;

    /** Menu de clic derecho: MIDI Learn / quitar / modo (para booleanos). */
    void showMidiMenu (const juce::String& target);
    /** " · CC 64", " · learn" o nada, para pegar a etiquetas y botones. */
    juce::String midiSuffix (const juce::String& target) const;
    /** Refresca los sufijos MIDI de todos los knobs y botones. */
    void syncMidiLabels();

    void timerCallback() override { updateEnablement(); syncPresetDisplay(); syncMidiLabels(); }

    void refreshPresetList (int idToSelect = 0);
    void stepPreset (int delta);
    void showSaveDialog();
    void showDeleteDialog();

    ReverseVerbProcessor& proc;
    KnobLNF   lnf;
    BackPanel content;
    std::unique_ptr<WormholeView> wormhole;
    std::unique_ptr<Meters>       meters;

    Knob length, revFb, drive, driveEnv, revLow, revHigh;
    Knob dTime, dFb, dLow, dHigh, dMod, dModRate;
    Knob wow, flutter, detune, shimmer, shimPitch;
    Knob revAmt, revSize, revDamp, mix, duck, duckRel;

    MidiToggle syncButton   { "Sync" };
    MidiToggle freezeButton { "Freeze" };
    MidiToggle dSyncButton  { "Sync" };
    MidiToggle pingButton   { "Ping-Pong" };
    MidiToggle postButton   { "Reverb despues del reverse" };
    juce::ComboBox divisionBox, dDivisionBox, routingBox;

    // Tempo manual. Solo se ve cuando el host no da BPM (Standalone): con host
    // el valor sale en el visualizador y este control sobraria. Su clic
    // derecho asigna el TAP TEMPO, no el valor.
    MidiSlider   tempoSlider;
    juce::Label  tempoLabel;
    std::unique_ptr<SliderAtt> tempoAtt;

    juce::ComboBox   presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" },
                     saveBtn { "Guardar" }, delBtn { "Borrar" };
    std::unique_ptr<juce::AlertWindow> dialog;

    // Los presets de usuario empiezan en este id para no chocar con los de
    // fabrica, que ocupan 1..N.
    static constexpr int userIdBase = 1000;

    // Lo ultimo que se pinto en el combo, para no repintarlo 4 veces por segundo.
    juce::String shownName;
    bool         shownDirty = false;

    std::unique_ptr<ButtonAtt> syncAtt, dSyncAtt, pingAtt, postAtt, freezeAtt;
    std::unique_ptr<ComboAtt>  divAtt, dDivAtt, routeAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverseVerbEditor)
};
