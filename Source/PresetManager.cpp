#include "PresetManager.h"

//==============================================================================
// ORDEN: los presets nuevos SIEMPRE se anaden al final. El host guarda el
// indice de programa dentro del proyecto, asi que insertar uno en medio
// desplazaria todos los siguientes y las sesiones ya guardadas cargarian un
// preset distinto al que el usuario eligio. Agrupar por estilo es mas bonito;
// no romper los proyectos de nadie es mas importante.
//
// Divisiones:  0=1/1  1=1/2  2=1/4.  3=1/4  4=1/4T  5=1/8.  6=1/8  7=1/8T
//              8=1/16 9=1/16T
// Routing:     0=Off  1=Delay->Rev  2=Rev->Delay  3=Paralelo
// revpost:     1=reverb DESPUES del reverse (swell)  0=ANTES (succion)
//
// Cada preset lista solo lo que le importa. El resto vuelve a su valor por
// defecto antes de aplicar, asi que no hay herencia accidental entre presets.
//==============================================================================

namespace
{
    struct PV { const char* id; float value; };

    struct Factory
    {
        const char* name;
        std::initializer_list<PV> values;
    };

    const Factory kFactory[] =
    {
        //================================ SWELLS AMBIENTALES ==================
        { "Swell clasico", {
            { "length", 700 }, { "highpass", 60 }, { "lowpass", 12000 },
            { "revamt", 55 }, { "revsize", 80 }, { "revdamp", 35 },
            { "mix", 45 }, { "duck", 55 }, { "duckrel", 320 },
            { "wow", 18 }, { "detune", 6 } } },

        { "Nube lenta", {
            { "length", 1400 }, { "feedback", 35 }, { "drive", 1.5f },
            { "highpass", 120 }, { "lowpass", 7000 },
            { "revamt", 75 }, { "revsize", 90 }, { "revdamp", 30 },
            { "mix", 60 }, { "duck", 65 }, { "duckrel", 500 },
            { "wow", 30 }, { "flutter", 12 }, { "detune", 12 } } },

        { "Respiracion", {
            { "length", 950 }, { "feedback", 20 },
            { "highpass", 80 }, { "lowpass", 9000 },
            { "routing", 2 }, { "dtime", 500 }, { "dfeed", 30 },
            { "dhigh", 200 }, { "dlow", 5000 }, { "pingpong", 1 },
            { "revamt", 60 }, { "revsize", 85 }, { "revdamp", 45 },
            { "mix", 50 }, { "duck", 60 }, { "duckrel", 400 } } },

        //================================ RITMICOS ============================
        { "Swell al pulso", {
            { "sync", 1 }, { "division", 1 }, { "feedback", 15 }, { "drive", 1.5f },
            { "highpass", 100 }, { "lowpass", 11000 },
            { "routing", 2 }, { "dsync", 1 }, { "ddiv", 6 }, { "dfeed", 40 },
            { "pingpong", 1 },
            { "revamt", 35 }, { "revsize", 70 }, { "revdamp", 45 },
            { "mix", 50 }, { "duck", 50 }, { "duckrel", 250 } } },

        { "Tresillos invertidos", {
            { "sync", 1 }, { "division", 4 }, { "feedback", 30 }, { "drive", 2 },
            { "highpass", 150 }, { "lowpass", 8000 },
            { "routing", 1 }, { "dsync", 1 }, { "ddiv", 7 }, { "dfeed", 45 },
            { "pingpong", 1 },
            { "revamt", 30 }, { "revsize", 65 }, { "revdamp", 50 },
            { "mix", 55 }, { "duck", 45 }, { "duckrel", 200 } } },

        { "Corcheas cruzadas", {
            { "sync", 1 }, { "division", 6 }, { "feedback", 25 },
            { "highpass", 120 }, { "lowpass", 12000 },
            { "routing", 3 }, { "dsync", 1 }, { "ddiv", 5 }, { "dfeed", 50 },
            { "pingpong", 1 },
            { "revamt", 25 }, { "revsize", 60 }, { "revdamp", 50 },
            { "mix", 50 }, { "duck", 40 }, { "duckrel", 220 } } },

        //================================ GRANULAR ============================
        { "Granular metalico", {
            { "length", 55 }, { "feedback", 45 }, { "drive", 4 },
            { "highpass", 250 }, { "lowpass", 6000 },
            { "revamt", 25 }, { "revsize", 60 }, { "revdamp", 55 },
            { "mix", 55 }, { "duck", 25 }, { "duckrel", 120 } } },

        { "Textura de vidrio", {
            { "length", 40 }, { "feedback", 50 }, { "drive", 2 },
            { "highpass", 400 }, { "lowpass", 14000 },
            { "revamt", 40 }, { "revsize", 75 }, { "revdamp", 25 },
            { "mix", 60 }, { "duck", 20 }, { "duckrel", 100 } } },

        { "Motor roto", {
            { "length", 75 }, { "feedback", 55 }, { "drive", 7 },
            { "highpass", 300 }, { "lowpass", 3500 },
            { "routing", 2 }, { "dtime", 90 }, { "dfeed", 55 }, { "pingpong", 1 },
            { "revamt", 20 }, { "revsize", 55 }, { "revdamp", 60 },
            { "mix", 65 }, { "duck", 15 }, { "duckrel", 90 },
            { "wow", 55 }, { "flutter", 45 }, { "driveenv", 55 } } },

        //================================ SUTILES =============================
        { "Profundidad discreta", {
            { "length", 320 },
            { "highpass", 200 }, { "lowpass", 8000 },
            { "revamt", 30 }, { "revsize", 60 }, { "revdamp", 55 },
            { "mix", 18 }, { "duck", 70 }, { "duckrel", 280 } } },

        { "Cola de voz", {
            { "length", 480 }, { "feedback", 10 },
            { "highpass", 250 }, { "lowpass", 6500 },
            { "revamt", 40 }, { "revsize", 70 }, { "revdamp", 60 },
            { "mix", 22 }, { "duck", 80 }, { "duckrel", 350 } } },

        //================================ REFERENCIAS =========================
        // Puntos de partida inspirados en el PAPEL que el reverse/delay juega
        // en esos discos. No son emulaciones: el sonido de una banda sale del
        // instrumento, el ampli y la mezcla. Esto es un eslabon.

        { "Ref: Guthrie Wash", {          // Cocteau Twins / Slowdive
            { "length", 850 }, { "feedback", 30 }, { "drive", 1.5f },
            { "highpass", 180 }, { "lowpass", 9000 },
            { "routing", 2 }, { "dtime", 420 }, { "dfeed", 35 },
            { "dhigh", 250 }, { "dlow", 5500 }, { "pingpong", 1 },
            { "revamt", 80 }, { "revsize", 88 }, { "revdamp", 30 },
            { "mix", 65 }, { "duck", 35 }, { "duckrel", 380 },
            { "wow", 22 }, { "detune", 20 } } },

        { "Ref: Bow & Swell", {           // Sigur Ros / post-rock
            { "length", 1600 }, { "feedback", 40 },
            { "highpass", 100 }, { "lowpass", 8000 },
            { "revamt", 85 }, { "revsize", 92 }, { "revdamp", 25 },
            { "mix", 70 }, { "duck", 75 }, { "duckrel", 600 },
            { "wow", 15 }, { "detune", 10 }, { "shimmer", 35 } } },

        { "Ref: Greenwood Reverse", {     // Radiohead. revpost=0: la cola entra
            { "sync", 1 }, { "division", 6 },   // al buffer y se invierte con el
            { "feedback", 35 }, { "drive", 5 },
            { "highpass", 220 }, { "lowpass", 4500 },
            { "routing", 1 }, { "dsync", 1 }, { "ddiv", 8 }, { "dfeed", 50 },
            { "dhigh", 180 }, { "dlow", 6000 },
            { "revamt", 25 }, { "revsize", 55 }, { "revdamp", 60 }, { "revpost", 0 },
            { "mix", 55 }, { "duck", 30 }, { "duckrel", 150 },
            { "flutter", 30 }, { "driveenv", 40 } } },

        { "Ref: Dotted Edge", {           // U2. Aqui manda el DELAY, el reverse
            { "length", 120 },            // es solo condimento
            { "highpass", 300 }, { "lowpass", 12000 },
            { "routing", 2 }, { "dsync", 1 }, { "ddiv", 5 }, { "dfeed", 55 },
            { "dhigh", 200 }, { "dlow", 9000 }, { "pingpong", 1 },
            { "revamt", 20 }, { "revsize", 60 }, { "revdamp", 50 },
            { "mix", 40 }, { "duck", 45 }, { "duckrel", 180 } } },

        //================================ SHOEGAZE ============================
        // OJO al ducking: aqui va BAJO, al reves que en todo lo demas. En
        // shoegaze el desenfoque ES la estetica -- la nota y la cola tienen que
        // solaparse en una pasta. Un ducking alto separaria las dos cosas y te
        // dejaria un efecto pulcro, que es exactamente lo contrario.
        //
        // Y el Low Cut alto no es opcional: sin el, con el mix por encima del
        // 60 % los graves se acumulan y el muro se vuelve barro.

        { "Shoe: Muro", {                 // My Bloody Valentine-ish
            { "length", 380 }, { "feedback", 45 }, { "drive", 3 },
            { "highpass", 220 }, { "lowpass", 9000 },
            { "routing", 3 }, { "dtime", 300 }, { "dfeed", 45 },
            { "dhigh", 250 }, { "dlow", 7000 }, { "pingpong", 1 },
            { "revamt", 70 }, { "revsize", 85 }, { "revdamp", 35 },
            { "mix", 72 }, { "duck", 10 }, { "duckrel", 200 },
            { "wow", 25 }, { "flutter", 20 }, { "detune", 22 },
            { "dmoddepth", 30 }, { "dmodrate", 0.8f } } },

        { "Shoe: Souvlaki", {             // Slowdive: vidrioso y largo
            { "length", 1100 }, { "feedback", 35 }, { "drive", 1.5f },
            { "highpass", 150 }, { "lowpass", 10000 },
            { "routing", 2 }, { "dtime", 560 }, { "dfeed", 30 },
            { "dhigh", 200 }, { "dlow", 6000 }, { "pingpong", 1 },
            { "revamt", 82 }, { "revsize", 90 }, { "revdamp", 28 },
            { "mix", 62 }, { "duck", 25 }, { "duckrel", 450 },
            { "wow", 20 }, { "detune", 16 }, { "shimmer", 20 } } },

        { "Shoe: Nowhere", {              // Ride: mas empuje y ritmo
            { "sync", 1 }, { "division", 6 }, { "feedback", 40 }, { "drive", 4 },
            { "highpass", 250 }, { "lowpass", 7500 },
            { "routing", 1 }, { "dsync", 1 }, { "ddiv", 8 }, { "dfeed", 45 },
            { "dhigh", 220 }, { "dlow", 8000 }, { "pingpong", 1 },
            { "revamt", 45 }, { "revsize", 70 }, { "revdamp", 45 },
            { "mix", 58 }, { "duck", 20 }, { "duckrel", 160 },
            { "flutter", 22 }, { "detune", 14 } } },

        //================================ NUEVAS FUNCIONES ====================
        // Construidos alrededor de tape, shimmer, ducking dinamico y chorus.
        // Al FINAL del array, como todos los anadidos (ver nota de arriba).

        { "Catedral (shimmer)", {
            { "length", 1200 }, { "feedback", 55 },
            { "highpass", 140 }, { "lowpass", 9000 },
            { "shimmer", 75 }, { "shimpitch", 12 },
            { "wow", 15 }, { "detune", 12 },
            { "revamt", 80 }, { "revsize", 92 }, { "revdamp", 30 },
            { "mix", 60 }, { "duck", 60 }, { "duckrel", 500 } } },

        { "Octavas al pulso", {
            { "sync", 1 }, { "division", 3 }, { "feedback", 50 },
            { "highpass", 180 }, { "lowpass", 10000 },
            { "shimmer", 60 }, { "shimpitch", 12 },
            { "routing", 2 }, { "dsync", 1 }, { "ddiv", 6 }, { "dfeed", 40 },
            { "pingpong", 1 }, { "dmoddepth", 25 }, { "dmodrate", 0.5f },
            { "revamt", 40 }, { "revsize", 75 }, { "revdamp", 40 },
            { "mix", 55 }, { "duck", 45 }, { "duckrel", 260 } } },

        { "Subterraneo", {                // shimpitch negativo: baja una octava
            { "length", 900 }, { "feedback", 45 }, { "drive", 3 },
            { "highpass", 60 }, { "lowpass", 5000 },
            { "shimmer", 55 }, { "shimpitch", -12 },
            { "wow", 30 }, { "flutter", 15 },
            { "revamt", 55 }, { "revsize", 80 }, { "revdamp", 55 },
            { "mix", 55 }, { "duck", 50 }, { "duckrel", 400 } } },

        { "Cinta muerta", {               // wow y flutter al limite
            { "length", 420 }, { "feedback", 50 }, { "drive", 5 },
            { "highpass", 220 }, { "lowpass", 4000 },
            { "wow", 85 }, { "flutter", 70 }, { "detune", 25 },
            { "routing", 2 }, { "dtime", 300 }, { "dfeed", 45 },
            { "dhigh", 250 }, { "dlow", 3500 }, { "dmoddepth", 55 }, { "dmodrate", 1.4f },
            { "revamt", 30 }, { "revsize", 65 }, { "revdamp", 65 },
            { "mix", 60 }, { "duck", 30 }, { "duckrel", 220 } } },

        { "Dinamico (toca fuerte)", {     // el drive lo pone la pulsacion
            { "length", 600 }, { "feedback", 40 }, { "drive", 1.2f },
            { "driveenv", 85 },
            { "highpass", 150 }, { "lowpass", 11000 },
            { "wow", 12 }, { "detune", 8 },
            { "revamt", 45 }, { "revsize", 75 }, { "revdamp", 40 },
            { "mix", 50 }, { "duck", 55 }, { "duckrel", 200 } } },

        { "Drone (pulsa Freeze)", {       // preparado para congelar en vivo
            { "length", 1500 }, { "feedback", 45 },
            { "highpass", 90 }, { "lowpass", 8000 },
            { "shimmer", 30 }, { "shimpitch", 12 },
            { "wow", 25 }, { "detune", 18 },
            { "revamt", 70 }, { "revsize", 90 }, { "revdamp", 35 },
            { "mix", 65 }, { "duck", 25 }, { "duckrel", 600 } } },
    };

    constexpr int kNumFactory = static_cast<int> (sizeof (kFactory) / sizeof (kFactory[0]));
}

//==============================================================================
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    // Se escucha TODO para saber si el usuario toco algo tras cargar un preset.
    // Los parametros ya existen aqui: apvts se construye antes que este objeto.
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->paramID, this);
}

PresetManager::~PresetManager()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->paramID, this);
}

void PresetManager::parameterChanged (const juce::String&, float)
{
    if (! loading.load())
        dirty.store (true);
}

//==============================================================================
int PresetManager::getNumFactoryPresets() { return kNumFactory; }

juce::String PresetManager::getFactoryPresetName (int index)
{
    if (juce::isPositiveAndBelow (index, kNumFactory))
        return kFactory[index].name;

    return {};
}

void PresetManager::setParam (const juce::String& id, float rawValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (rawValue));
}

void PresetManager::resetToDefaults()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
}

void PresetManager::loadFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumFactory))
        return;

    loading.store (true);
    resetToDefaults();

    for (const auto& v : kFactory[index].values)
        setParam (v.id, v.value);

    currentName = kFactory[index].name;
    loading.store (false);
    dirty.store (false);
}

//==============================================================================
juce::File PresetManager::getUserPresetDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("ReverseVerb")
                   .getChildFile ("Presets");

    if (! dir.exists())
        dir.createDirectory();

    return dir;
}

juce::StringArray PresetManager::getUserPresetNames() const
{
    juce::StringArray names;

    for (const auto& f : getUserPresetDir().findChildFiles (juce::File::findFiles, false,
                                                            "*.rvpreset"))
        names.add (f.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

bool PresetManager::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());

    if (clean.isEmpty())
        return false;

    if (auto xml = apvts.copyState().createXml())
    {
        const auto file = getUserPresetDir().getChildFile (clean + ".rvpreset");

        if (xml->writeTo (file))
        {
            currentName = clean;
            dirty.store (false);
            return true;
        }
    }

    return false;
}

bool PresetManager::loadUserPreset (const juce::String& name)
{
    const auto file = getUserPresetDir().getChildFile (name + ".rvpreset");

    if (auto xml = juce::XmlDocument::parse (file))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            loading.store (true);
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            loading.store (false);
            dirty.store (false);
            currentName = name;
            return true;
        }
    }

    return false;
}

bool PresetManager::deleteUserPreset (const juce::String& name)
{
    return getUserPresetDir().getChildFile (name + ".rvpreset").deleteFile();
}
