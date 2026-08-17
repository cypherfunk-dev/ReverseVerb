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
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAtt> att;
    };

    void addKnob (Knob&, const juce::String& paramID, const juce::String& text);
    void layoutRow (Knob* const* knobs, int count, int y);

    /** Activa/desactiva controles segun sync, ruteo y si hay estereo de verdad.
        Va en un Timer porque el numero de canales lo decide el host y puede
        cambiar sin que el usuario toque nada en la GUI. */
    void updateEnablement();
    void timerCallback() override { updateEnablement(); }

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

    juce::ToggleButton syncButton   { "Sync" };
    juce::ToggleButton freezeButton { "Freeze" };
    juce::ToggleButton dSyncButton { "Sync" };
    juce::ToggleButton pingButton  { "Ping-Pong" };
    juce::ToggleButton postButton  { "Reverb despues del reverse" };
    juce::ComboBox     divisionBox, dDivisionBox, routingBox;

    juce::ComboBox   presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" },
                     saveBtn { "Guardar" }, delBtn { "Borrar" };
    std::unique_ptr<juce::AlertWindow> dialog;

    // Los presets de usuario empiezan en este id para no chocar con los de
    // fabrica, que ocupan 1..N.
    static constexpr int userIdBase = 1000;

    std::unique_ptr<ButtonAtt> syncAtt, dSyncAtt, pingAtt, postAtt, freezeAtt;
    std::unique_ptr<ComboAtt>  divAtt, dDivAtt, routeAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverseVerbEditor)
};
