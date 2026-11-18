cmake_minimum_required(VERSION 3.22)

project(PedalFaster VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if PF_JUCE_DIR is given, otherwise fetch the tag.
# ---------------------------------------------------------------------------
set(PF_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")
set(PF_JUCE_TAG "9.0.5" CACHE STRING "JUCE git tag to fetch")

if(PF_JUCE_DIR AND EXISTS "${PF_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${PF_JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${PF_JUCE_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Sources shared by the plugin and the test runner
# ---------------------------------------------------------------------------
set(PF_CORE_SOURCES
    src/dsp/EffectRegistry.cpp
    src/dsp/SignalChain.cpp
    src/dsp/AudioEngine.cpp
    src/dsp/Amps/AmpModels.cpp
    src/dsp/Amps/AmpSection.cpp
    src/dsp/Cabinets/Cabinet.cpp
    src/presets/FactoryPresets.cpp
    src/presets/PresetSerializer.cpp
    src/presets/PresetManager.cpp
    src/Parameters.cpp
    src/PluginProcessor.cpp)

set(PF_UI_SOURCES
    src/PluginEditor.cpp
    src/ui/Theme.cpp
    src/ui/Widgets.cpp
    src/ui/PresetBrowser.cpp
    src/ui/SignalChainView.cpp
    src/ui/ParameterEditor.cpp)

set(PF_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# ---------------------------------------------------------------------------
# Plugin: VST3 + Standalone
# ---------------------------------------------------------------------------
juce_add_plugin(PedalFaster
    COMPANY_NAME             "Pedal Faster"
    PRODUCT_NAME             "Pedal Faster"
    BUNDLE_ID                "com.pedalfaster.rig"
    PLUGIN_MANUFACTURER_CODE PdlF
    PLUGIN_CODE              PfRg
    FORMATS                  VST3 Standalone
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    VST3_CATEGORIES          Fx Distortion Delay Reverb
    COPY_PLUGIN_AFTER_BUILD  FALSE)

target_sources(PedalFaster PRIVATE ${PF_CORE_SOURCES} ${PF_UI_SOURCES})
target_include_directories(PedalFaster PRIVATE src)
target_compile_definitions(PedalFaster PUBLIC ${PF_DEFINES})
if(MSVC)
    target_compile_options(PedalFaster PRIVATE /bigobj /W3)
endif()

target_link_libraries(PedalFaster
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# ---------------------------------------------------------------------------
# Test runner (DSP, presets, integration):  --target PedalFasterTests
# ---------------------------------------------------------------------------
juce_add_console_app(PedalFasterTests PRODUCT_NAME "PedalFasterTests")
target_sources(PedalFasterTests PRIVATE
    tests/TestMain.cpp
    tests/TestUtils.h
    tests/dsp/DspTests.cpp
    tests/presets/PresetTests.cpp
    tests/integration/IntegrationTests.cpp
    tests/Calibrate.cpp
    ${PF_CORE_SOURCES} ${PF_UI_SOURCES})
target_include_directories(PedalFasterTests PRIVATE src tests)
target_compile_definitions(PedalFasterTests PRIVATE ${PF_DEFINES} JUCE_STANDALONE_APPLICATION=1 PF_TEST_BUILD=1)
if(MSVC)
    target_compile_options(PedalFasterTests PRIVATE /bigobj /W3)
endif()
target_link_libraries(PedalFasterTests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
