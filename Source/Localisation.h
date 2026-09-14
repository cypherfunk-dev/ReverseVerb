#pragma once

#include <juce_core/juce_core.h>

/**
    Idioma de la interfaz.

    El idioma FUENTE del codigo es el ingles: todo texto que ve el usuario
    esta escrito en ingles y envuelto en TRANS(). Aqui vive la tabla al
    espanol, que se activa si el idioma del sistema empieza por "es". Anadir
    otro idioma es anadir otra tabla y otra rama en initLocalisation().

    Lo que NO se traduce, a proposito:
      - Los nombres de los parametros que ve el host ("Rev Feedback"...): los
        DAW no localizan y un proyecto tiene que leerse igual en cualquier
        maquina.
      - Los nombres de los presets ("Swell clasico", "Ref: Guthrie Wash"):
        son nombres propios y ademas viajan dentro del estado.
      - Los nombres de los controles (Length, Feedback, Drive...): son el
        vocabulario tecnico universal del audio y el README los usa tal cual.

    Se llama desde el constructor del procesador, antes de que exista el
    editor: los botones traducen su texto al construirse.

    Formato de la tabla: el de juce::LocalisedStrings. Una linea
    "language:", una "countries:", y despues "original" = "traduccion".
*/
inline void initLocalisation()
{
    static bool done = false;
    if (done) return;
    done = true;

    // Idioma del sistema ("es", "es-MX", "en-US"...). REVERSEVERB_LANG lo
    // fuerza (p. ej. "en" o "es") para probar sin cambiar el idioma de la
    // maquina.
    auto lang = juce::SystemStats::getUserLanguage();
    if (const auto forced = juce::SystemStats::getEnvironmentVariable ("REVERSEVERB_LANG", {}); forced.isNotEmpty())
        lang = forced;

    if (! lang.startsWithIgnoreCase ("es"))
        return;

    static const char* spanish = u8R"i18n(language: Español
countries: es mx ar cl co pe ve ec gt cu bo do hn py sv ni cr pa uy pr gq

"event horizon" = "horizonte de sucesos"
"Simple" = "Simple"
"Full" = "Completo"
"Save" = "Guardar"
"Delete" = "Borrar"
"Cancel" = "Cancelar"
"Factory" = "Fábrica"
"Yours" = "Tuyos"

"REVERSE   (drive and filters act inside its loop)" = "REVERSE   (drive y filtros actúan dentro de su lazo)"
"TAPE & PITCH   (act on the reverse engine)" = "TAPE Y PITCH   (actúan sobre el motor de reverse)"
"SPACE   (plate; Shimmer uses the Shim Pitch above)" = "SPACE   (placa; Shimmer usa el Shim Pitch de arriba)"
"What you don't see is still playing. Click to see it all." = "Lo que no se ve sigue sonando. Clic para verlo todo."
"default" = "por defecto"
"reverb BEFORE the reverse" = "reverb ANTES del reverse"

"Reverb after the reverse" = "Reverb después del reverse"
"Ping-Pong (mono)" = "Ping-Pong (mono)"
"Parallel" = "Paralelo"

"MIDI Learn" = "MIDI Learn"
"MIDI Learn (tap tempo)" = "MIDI Learn (tap tempo)"
"Cancel MIDI Learn" = "Cancelar MIDI Learn"
"Remove CC " = "Quitar CC "
"Momentary (press = on)" = "Momentáneo (pisar = on)"
"Toggle (each press flips it)" = "Toggle (cada pulsación invierte)"
" · learning..." = " · aprendiendo..."

"Save preset" = "Guardar preset"
"Preset name:" = "Nombre del preset:"
"Delete preset" = "Borrar preset"
"\"%s\" will be deleted from disk." = "Se borrará \"%s\" del disco."
)i18n";

    juce::LocalisedStrings::setCurrentMappings (
        new juce::LocalisedStrings (juce::String::fromUTF8 (spanish), false));
}
