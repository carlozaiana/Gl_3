cmake_minimum_required(VERSION 3.22)

project(SmoothScope VERSION 1.0.0)

# --- Architecture & Deployment Configuration ---
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")

# --- Dependencies ---
# We use FetchContent to get JUCE 7 (Stable)
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(juce)

# --- Plugin Definition ---
juce_add_plugin(SmoothScope
    COMPANY_NAME "ArchitectLabs"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    FORMATS VST3
    PRODUCT_NAME "SmoothScope"
)

# --- Source Files ---
target_sources(SmoothScope
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
)

# --- Compile Definitions & Linker Flags ---
target_compile_definitions(SmoothScope PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

juce_generate_juce_header(SmoothScope)

# Ensure strict standard compliance
set_target_properties(SmoothScope PROPERTIES CXX_STANDARD 17)