cmake_minimum_required(VERSION 3.22)

project(chops VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.15.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(chops
    PRODUCT_NAME "chops"
    COMPANY_NAME "maxgirkens"
    BUNDLE_ID com.maxgirkens.chops
    PLUGIN_MANUFACTURER_CODE Mxgk
    PLUGIN_CODE Chps
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(chops PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/engine/Engine.cpp
    src/engine/Voice.cpp
    src/model/Edits.cpp
    src/state/State.cpp
    src/ui/WaveDisplay.cpp)

target_compile_definitions(chops PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(chops
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- offline engine/state tests (no plugin wrapper) ---

enable_testing()

juce_add_console_app(chops_tests PRODUCT_NAME "chops_tests")

target_sources(chops_tests PRIVATE
    tests/EngineTests.cpp
    src/engine/Engine.cpp
    src/engine/Voice.cpp
    src/model/Edits.cpp
    src/state/State.cpp)

target_compile_definitions(chops_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_FLAC=1)

# No juce_recommended_warning_flags here: the tests assert bit-exact playback
# and round-trips with float == on purpose.
target_link_libraries(chops_tests
    PRIVATE
        juce::juce_audio_formats
        juce::juce_data_structures
    PUBLIC
        juce::juce_recommended_config_flags)

add_test(NAME chops_tests COMMAND chops_tests)
