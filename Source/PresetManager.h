#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Presets de fabrica (fijos, visibles en el menu del DAW) + presets de usuario
    (archivos XML en la carpeta del usuario).

    POR QUE DOS LISTAS SEPARADAS
    ----------------------------
    La lista de programas de VST3 tiene que tener tamano FIJO: si cambiara al
    guardar un preset nuevo, el host se desincronizaria y los indices guardados
    en sesiones antiguas apuntarian a otro sitio. Asi que:

      - Programas del host  = solo los de fabrica, numero constante.
      - Presets de usuario  = solo en el combo del plugin, en disco.

    Cada preset de fabrica define solo los parametros que le importan; el resto
    vuelve a su valor por defecto antes de aplicarlo. Es menos verboso y evita
    el bug de que un preset herede un parametro del preset anterior.
*/
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

    // --- fabrica ---
    static int           getNumFactoryPresets();
    static juce::String  getFactoryPresetName (int index);
    void                 loadFactoryPreset (int index);

    // --- usuario ---
    static juce::File    getUserPresetDir();
    juce::StringArray    getUserPresetNames() const;
    bool                 saveUserPreset (const juce::String& name);
    bool                 loadUserPreset (const juce::String& name);
    bool                 deleteUserPreset (const juce::String& name);

    juce::String currentName { "Init" };

private:
    void resetToDefaults();
    void setParam (const juce::String& id, float rawValue);

    juce::AudioProcessorValueTreeState& apvts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
