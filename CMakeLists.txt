cmake_minimum_required(VERSION 3.21)
project(Sympathy VERSION 1.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable VST2 compatibility to prevent conflicts
add_definitions(-DJUCE_VST3_CAN_REPLACE_VST2=0)
add_definitions(-DJUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)

# ---- Vendored dependencies (copied into the tree, no network needed) ------
# JUCE 8.0.8
add_subdirectory(JUCE)

# clap-juce-extensions (bundles the CLAP SDK under clap-libs/)
add_subdirectory(clap-juce-extensions)

# ---- The plugin ------------------------------------------------------------
# VST3 everywhere; on macOS also build an Audio Unit (required for Logic /
# GarageBand, and the common format for Ableton Live on Mac).
set(SYMPATHY_PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND SYMPATHY_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(Sympathy
    COMPANY_NAME "Dewpoint AV"
    COMPANY_WEBSITE "https://dewpoint.live"
    COMPANY_EMAIL "anthony@dewpoint.live"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Dwpt
    PLUGIN_CODE Smpy
    FORMATS ${SYMPATHY_PLUGIN_FORMATS}
    PRODUCT_NAME "Sympathy"
    BUNDLE_ID "com.dewpoint.Sympathy"
)

# Add the CLAP format on top of the JUCE-generated VST3 target.
clap_juce_extensions_plugin(TARGET Sympathy
    CLAP_ID "live.dewpoint.sympathy"
    CLAP_FEATURES audio-effect reverb stereo)

# Generate JuceHeader.h and per-target config
juce_generate_juce_header(Sympathy)

# Embedded Monument Extended logotype fonts.
juce_add_binary_data(SympathyBinaryData
    SOURCES
        Source/MonumentExtendedUltrabold.otf
        Source/MonumentExtendedRegular.otf)

# ---- Source files ----------------------------------------------------------
target_sources(Sympathy
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ResonanceVisualizer.cpp
        Source/dsp/ResonatorEngine.cpp
        Source/dsp/RefractEngine.cpp
)

target_include_directories(Sympathy
    PRIVATE
        Source
)

target_compile_definitions(Sympathy
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# ---- Link JUCE modules -----------------------------------------------------
target_link_libraries(Sympathy
    PRIVATE
        SympathyBinaryData
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_data_structures
        juce::juce_events
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---- Compiler flags --------------------------------------------------------
if(MSVC)
    target_compile_options(Sympathy PRIVATE /W4)
else()
    target_compile_options(Sympathy PRIVATE -Wall -Wextra)
endif()

# ---- Packaging / staging (Windows) -----------------------------------------
if(WIN32)
    # Stage a flat copy of both formats into build/dist/ for the installer.
    # (Actual deployment to the CLAP/VST3 folders is handled by the Inno Setup
    # installer.)
    add_custom_command(TARGET Sympathy_CLAP POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Sympathy_CLAP>"
            "${CMAKE_BINARY_DIR}/dist/Sympathy.clap"
        COMMENT "Staging Sympathy.clap")

    add_custom_command(TARGET Sympathy_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/Sympathy_artefacts/$<CONFIG>/VST3/Sympathy.vst3"
            "${CMAKE_BINARY_DIR}/dist/Sympathy.vst3"
        COMMENT "Staging Sympathy.vst3")
endif()
