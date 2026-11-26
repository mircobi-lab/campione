cmake_minimum_required(VERSION 3.22)

# MBStudio91 Campione: player SoundFont 2 (anche SFZ e waveform Yamaha Motif/Montage) per pianoforte (VST3 / AU / Standalone), grafica MBStudio91.
# Motore: TinySoundFont (MIT, third_party/TinySoundFont). Progetto distribuito sotto AGPL v3 (vedi LICENSE.md).
project(MBStudio91Campione VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

set(JUCE_DIR "" CACHE PATH "Percorso di una copia locale di JUCE")
if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(CAMPIONE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND CAMPIONE_FORMATS AU)
endif()

juce_add_plugin(MBStudio91Campione
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/campione-1024.png"
    COMPANY_NAME "MBStudio91"
    COMPANY_WEBSITE "https://mbstudio91.fun"
    BUNDLE_ID "fun.mbstudio91.campione"
    PRODUCT_NAME "MBStudio91 Campione"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Mbst
    PLUGIN_CODE Cmp1
    FORMATS ${CAMPIONE_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    HARDENED_RUNTIME_ENABLED TRUE)

include(MBStudioUI/MBStudioUI.cmake)
mbstudio_ui_add(MBStudio91Campione)

target_sources(MBStudio91Campione PRIVATE
    Source/Params.cpp
    Source/TsfAccess.cpp
    Source/SoundFontEngine.cpp
    Source/Sf2Builder.cpp
    Source/SfzToSf2.cpp
    Source/YamahaToSf2.cpp
    Source/Banks.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_include_directories(MBStudio91Campione PRIVATE third_party/TinySoundFont third_party/stb_vorbis)
# codice di terze parti: niente avvisi dalle regole severe di JUCE
set_source_files_properties(Source/TsfAccess.cpp PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")

if(TARGET MBStudio91Campione_Standalone)
    target_sources(MBStudio91Campione_Standalone PRIVATE Source/StandaloneApp.cpp)
    target_compile_definitions(MBStudio91Campione_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
endif()

target_compile_definitions(MBStudio91Campione PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_JACK=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(MBStudio91Campione
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# prova offline: carica un SF2, suona un accordo e scrive un WAV (non fa parte del plugin)
option(CAMPIONE_BUILD_TOOLS "Compila lo strumento di render di prova" OFF)
if(CAMPIONE_BUILD_TOOLS)
    juce_add_console_app(campione_render PRODUCT_NAME "campione_render")
    target_sources(campione_render PRIVATE tools/render.cpp Source/TsfAccess.cpp Source/SoundFontEngine.cpp Source/Sf2Builder.cpp Source/SfzToSf2.cpp Source/YamahaToSf2.cpp Source/Banks.cpp)
    target_include_directories(campione_render PRIVATE Source third_party/TinySoundFont third_party/stb_vorbis)
    target_compile_definitions(campione_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(campione_render PRIVATE juce::juce_audio_basics juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)
endif()
