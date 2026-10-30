cmake_minimum_required(VERSION 3.22)
project(PhaseLock VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE checkout if you have one (cmake -DJUCE_DIR=path/to/JUCE),
# otherwise JUCE 8.0.4 is downloaded automatically.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(PL_FORMATS VST3)
if(APPLE)
    list(APPEND PL_FORMATS AU)
endif()

juce_add_plugin(PhaseLock
    COMPANY_NAME "Johan"
    PLUGIN_MANUFACTURER_CODE Jhan
    PLUGIN_CODE PhLk
    FORMATS ${PL_FORMATS}
    PRODUCT_NAME "Phase Lock"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PhaseLock PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    PhaseAnalyzer.cpp)

target_compile_definitions(PhaseLock PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Read source files as UTF-8 on Windows
if(MSVC)
    target_compile_options(PhaseLock PRIVATE /utf-8)
endif()

target_link_libraries(PhaseLock
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
