cmake_minimum_required(VERSION 3.22)

project(Glue VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Options
option(GLUE_BUILD_STANDALONE "Also build a standalone app for testing"                     OFF)
option(GLUE_BUILD_TESTS      "Build the GBusCompTests console target"                      ON)
option(GLUE_COPY_AFTER_BUILD "Install the plugin into the user plugin folder after building" OFF)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.4)")

# ------------------------------------------------------------------------------
# DSP tests (pure C++, no JUCE)
if(GLUE_BUILD_TESTS)
    add_executable(GBusCompTests Tests/GBusCompTests.cpp Source/GBusComp.cpp)
    target_include_directories(GBusCompTests PRIVATE Source)
endif()

# ------------------------------------------------------------------------------
# JUCE
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Plugin
set(GLUE_FORMATS VST3)
if(GLUE_BUILD_STANDALONE)
    list(APPEND GLUE_FORMATS Standalone)
endif()

# The VST3 manifest helper has to run on the build machine, which isn't possible
# when cross-compiling.
if(CMAKE_CROSSCOMPILING)
    set(GLUE_VST3_MANIFEST FALSE)
else()
    set(GLUE_VST3_MANIFEST TRUE)
endif()

juce_add_plugin(Glue
    PRODUCT_NAME                "Glue"
    COMPANY_NAME                "Glue Project"
    BUNDLE_ID                   com.glueproject.glue
    PLUGIN_MANUFACTURER_CODE    Glpj
    PLUGIN_CODE                 Glu1
    FORMATS                     ${GLUE_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics
    VST3_AUTO_MANIFEST          ${GLUE_VST3_MANIFEST}
    COPY_PLUGIN_AFTER_BUILD     ${GLUE_COPY_AFTER_BUILD})

target_sources(Glue PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/CustomLookAndFeel.cpp
    Source/UI/GlueComponents.cpp
    Source/GBusComp.cpp)

target_include_directories(Glue PRIVATE Source)

target_compile_definitions(Glue PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Glue
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
