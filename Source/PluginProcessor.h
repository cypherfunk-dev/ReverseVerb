#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "ReverseDelay.h"
#include "StereoDelay.h"
#include "Ducker.h"
#include "TapeMod.h"
#include "PresetManager.h"
#include <atomic>
#include <vector>

class ReverseVerbProcessor : public juce::AudioProcessor
{
public:
    enum Routing { routeOff = 0, routePre, routePost, routeParallel };

    ReverseVerbProcessor();
    ~ReverseVerbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override   { return false; }
    bool producesMidi() const override  { return false; }
    bool isMidiEffect() const override  { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    /** Fuerza la carga de un preset de fabrica (la usa la GUI). A diferencia
        de setCurrentProgram, esta no hace cortocircuito si el indice coincide. */
    void applyFactoryPreset (int index);

    PresetManager& getPresets() noexcept { return presetManager; }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // --- solo para la GUI (se leen desde el hilo de mensajes) ---
    std::atomic<float> visPhase    { 0.0f };
    std::atomic<float> visLengthMs { 500.0f };
    std::atomic<float> visDelayMs  { 375.0f };
    std::atomic<float> visBpm      { 0.0f };
    std::atomic<float> visDuckGain { 1.0f };
    std::atomic<bool>  visStereo    { true };   // false -> el ping-pong no puede actuar
    std::atomic<bool>  visFrozen    { false };
    std::atomic<float> visDrive     { 1.0f };   // drive efectivo tras la envolvente

    // Medidores de pico. Caen despacio para que se puedan leer sin parpadeo.
    std::atomic<float> visInPeak    { 0.0f };
    std::atomic<float> visOutPeak   { 0.0f };

    static juce::StringArray divisionNames();
    static juce::StringArray routingNames();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Resuelve ms a partir de un par (slider, sync+division). */
    float resolveMs (std::atomic<float>* msParam,
                     std::atomic<float>* syncParam,
                     std::atomic<float>* divParam) const;

    PresetManager presetManager { apvts };
    int currentProgram = 0;

    std::vector<ReverseDelay> delays;      // un estado por canal
    StereoDelay               echo;
    juce::Reverb              reverb;
    Ducker                    ducker;      // atenua el efecto
    Ducker                    driveEnv;    // empuja el drive del lazo
    TapeMod                   tape;        // wow y flutter, compartido por canales

    juce::AudioBuffer<float> wetBuffer, parBuffer;   // reservados en prepareToPlay
    std::vector<float>       driveCurve;             // drive por muestra
    std::vector<float>       modCurve;               // wow/flutter por muestra
    juce::SmoothedValue<float> mixSmoothed;

    float revFbSm = 0.0f, echoFbSm = 0.0f;           // suavizado por bloque

    std::atomic<float>* pLength   = nullptr;
    std::atomic<float>* pSync     = nullptr;
    std::atomic<float>* pDivision = nullptr;
    std::atomic<float>* pFeedback = nullptr;
    std::atomic<float>* pDrive    = nullptr;
    std::atomic<float>* pDriveEnv = nullptr;
    std::atomic<float>* pFreeze   = nullptr;
    std::atomic<float>* pWow      = nullptr;
    std::atomic<float>* pFlutter  = nullptr;
    std::atomic<float>* pDetune   = nullptr;
    std::atomic<float>* pShimmer  = nullptr;
    std::atomic<float>* pShimPitch= nullptr;
    std::atomic<float>* pDModRate = nullptr;
    std::atomic<float>* pDModDepth= nullptr;
    std::atomic<float>* pHighPass = nullptr;
    std::atomic<float>* pLowPass  = nullptr;

    std::atomic<float>* pDTime    = nullptr;
    std::atomic<float>* pDSync    = nullptr;
    std::atomic<float>* pDDiv     = nullptr;
    std::atomic<float>* pDFeed    = nullptr;
    std::atomic<float>* pDHigh    = nullptr;
    std::atomic<float>* pDLow     = nullptr;
    std::atomic<float>* pPingPong = nullptr;
    std::atomic<float>* pRouting  = nullptr;

    std::atomic<float>* pRevAmt   = nullptr;
    std::atomic<float>* pRevSize  = nullptr;
    std::atomic<float>* pRevDamp  = nullptr;
    std::atomic<float>* pRevPost  = nullptr;

    std::atomic<float>* pMix      = nullptr;
    std::atomic<float>* pDuck     = nullptr;
    std::atomic<float>* pDuckRel  = nullptr;

    float  dModPhase = 0.0f;      // LFO del chorus del delay

    double currentSampleRate = 44100.0;
    double hostBpm           = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverseVerbProcessor)
};
