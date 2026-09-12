cmake_minimum_required(VERSION 3.22)

project(ReverseVerb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE -------------------------------------------------------------------
# Se descarga sola la primera vez que configuras (tarda unos minutos).
# 8.0.12 es la ultima 8.x estable. Si quieres probar la rama 9, cambia el tag
# a 9.0.1, pero ojo: salio en agosto 2026 y puede traer cambios de API.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Plugin -----------------------------------------------------------------
juce_add_plugin(ReverseVerb
    COMPANY_NAME                "Carlos"
    PLUGIN_MANUFACTURER_CODE    Crls          # 4 chars, primera en mayuscula
    PLUGIN_CODE                 Rvd1          # 4 chars, unico por plugin
    IS_SYNTH                    FALSE
    # TRUE: CC -> parametros (MIDI Learn), Program Change -> presets y tap
    # tempo. En Ableton se le manda MIDI desde una pista MIDI con "MIDI To"
    # apuntando a la pista de audio > ReverseVerb.
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # FALSE a proposito: copiar a C:/Program Files/Common Files/VST3 exige
    # permisos de administrador y hace fallar la build entera por un paso que
    # no es de compilacion. Para desarrollar es mejor apuntar el DAW a la
    # carpeta de build: cada recompilacion queda activa sin copiar nada.
    COPY_PLUGIN_AFTER_BUILD     FALSE
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "ReverseVerb")

target_sources(ReverseVerb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetManager.cpp
    Source/MidiControl.cpp)

target_include_directories(ReverseVerb PRIVATE Source)

target_compile_definitions(ReverseVerb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ReverseVerb
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- ASIO (opcional) --------------------------------------------------------
# El SDK de Steinberg no se puede redistribuir, por eso JUCE no lo trae de
# fabrica. Descargalo de steinberg.net/developers y apunta aqui a su carpeta.
# El if(EXISTS) es para que el proyecto siga compilando en una maquina que no
# lo tenga, en vez de romperse con un error de header no encontrado.
set(ASIO_SDK_DIR "C:/SDKs/asiosdk" CACHE PATH "Ruta al ASIO SDK (opcional)")

if(EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
    target_compile_definitions(ReverseVerb PUBLIC JUCE_ASIO=1)
    target_include_directories(ReverseVerb PRIVATE "${ASIO_SDK_DIR}/common")
    message(STATUS "ReverseVerb: ASIO activado desde ${ASIO_SDK_DIR}")
else()
    message(STATUS "ReverseVerb: sin ASIO SDK - el Standalone usara Windows Audio")
endif()

# --- Tests de DSP -----------------------------------------------------------
# Ejecutable aparte que NO enlaza JUCE. Los motores de DSP (ReverseDelay,
# StereoDelay, Ducker, OnePole) son headers puros a proposito, asi que se
# pueden probar en segundos sin arrastrar el framework ni instanciar un host.
#
#   cmake --build build --config Release --target ReverseVerbTests
#   build\Release\ReverseVerbTests.exe            barrido rapido (~4 s)
#   build\Release\ReverseVerbTests.exe --full     barrido exhaustivo
#
# Devuelve codigo de salida != 0 si algo falla.
add_executable(ReverseVerbTests tests/dsp_tests.cpp)
target_include_directories(ReverseVerbTests PRIVATE Source)
target_compile_features(ReverseVerbTests PRIVATE cxx_std_17)

# Los barridos de estabilidad son vergonzosamente paralelos (cada combinacion es
# una instancia independiente), asi que se reparten entre todos los nucleos.
find_package(Threads REQUIRED)
target_link_libraries(ReverseVerbTests PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(ReverseVerbTests PRIVATE /O2)
endif()
