cmake_minimum_required(VERSION 3.24)

# Must be set before the first project() call to take effect.
# CI overrides CMAKE_OSX_ARCHITECTURES explicitly for release/universal builds;
# local builds are left at the native architecture.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target" FORCE)

# JUCE's build tooling (juceaide) and some CMake find-modules it triggers
# (e.g. FindThreads) expect the C toolchain to be available; declaring it
# here at the top level avoids it being enabled implicitly deeper inside
# JUCE's CMake helpers, which has been observed to break Ninja's generate
# step (missing CMAKE_C_COMPILE_OBJECT rule variable).
project(Silentium VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/CPM.cmake)

# CPMAddPackage automatically respects the CPM_SOURCE_CACHE environment
# variable if it is set, reusing a shared package cache across builds/checkouts.
if(DEFINED ENV{CPM_SOURCE_CACHE})
    message(STATUS "Silentium: using CPM_SOURCE_CACHE at $ENV{CPM_SOURCE_CACHE}")
endif()

CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)

# ==============================================================================
# Plugin target
# ==============================================================================

juce_add_plugin(Silentium
    COMPANY_NAME "Yves Vogl"
    BUNDLE_ID com.yvesvogl.silentium
    PLUGIN_MANUFACTURER_CODE Yvsv
    PLUGIN_CODE Slnt
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Silentium"
    ICON_BIG "docs/assets/icon.png"
    COPY_PLUGIN_AFTER_BUILD TRUE
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
)

# ==============================================================================
# Embedded assets: M2 preset system factory presets (presets/factory/*.json)
# and the M2 i18n frame's German translation (resources/i18n/de.txt). See
# src/presets/PresetManager.h and src/presets/Localisation.h - both consume
# these purely via BinaryData:: symbols passed in from PluginProcessor.cpp/
# PluginEditor.cpp, never by including BinaryData.h themselves, so they stay
# portable across the suite (see basilica-audio/nave's
# docs/preset-system-notes.md, the M2 pilot's replication recipe).
# ==============================================================================

juce_add_binary_data(SilentiumBinaryData SOURCES
    presets/factory/default.json
    presets/factory/surgicalMute.json
    presets/factory/naturalDecay.json
    presets/factory/pickAttackFocus.json
    presets/factory/diKeyedWorkflow.json
    presets/factory/ambientSustain.json
    presets/factory/chugLock.json
    presets/factory/duckUnderLead.json
    presets/factory/listenCheck.json
    resources/i18n/de.txt
    # v0.3.1 photoreal GUI assets (resources/gui/, copied from the suite's
    # Blender pre-render pipeline at .scaffold/gui-assets/ - see each asset
    # family's README there for provenance/verification). The repo is
    # self-contained: these copies are the ones the plugin ships with.
    # knob_brass_v2_strip_*.png (the earlier filmstrip-rotation knob asset)
    # is no longer embedded as of v0.3.3 - the 9 knobs now draw
    # knob-v4.png (below) via a single-image JUCE rotation instead (see
    # RotatingImageKnob.h's docs for why: no v4-generation filmstrip render
    # exists yet, and reusing the v2 filmstrip's different render generation
    # reintroduced a visible style mismatch against the new master-ref
    # assets). The PNG remains on disk unreferenced, not deleted.
    resources/gui/toggle_brass_v2_strip_40px_4f.png
    resources/gui/toggle_brass_v2_strip_80px_4f.png
    resources/gui/button_brass_v1_normal_40x28.png
    resources/gui/button_brass_v1_normal_80x56.png
    resources/gui/button_brass_v1_hover_40x28.png
    resources/gui/button_brass_v1_hover_80x56.png
    # v0.3.2 (single-master-render revision, now superseded): the old
    # single-image faceplate-silentium-v3.png (obsidian plate + baked
    # knobs/toggles/rose all in one PNG) remains on disk unreferenced (not
    # deleted, in case of rollback) but is no longer embedded.
    #
    # v0.3.3 (this revision): true COMPONENT ASSEMBLY - every element is now
    # a standalone master-reference asset composited in PluginEditor.cpp/
    # AnalogMeter.cpp rather than one baked image, per Yves' final art
    # direction (.scaffold/gui-assets/faceplate-silentium-v3/, master-04
    # generation - see PluginEditorLayout.h's docs for the measured geometry
    # each asset is positioned against). All of the PNGs below except
    # faceplate-silentium-v4-base.png and vu-needle-master-v3.png were
    # RE-DERIVED from their *-master-ref.png source renders (same directory)
    # by this revision's own analysis scripts
    # (.scaffold/gui-assets/faceplate-silentium-v3/analysis/extract_rose_v4.py
    # for the rose emblem) - every *-master-ref.png source render ships on a
    # PURE BLACK background with no alpha channel, so each was converted
    # ONCE, OFFLINE (numpy/PIL, not at plugin runtime), into a proper RGBA
    # PNG before embedding - the shipped resources/gui/*-v4.png files below
    # are already the converted RGBA result; PluginEditor.cpp/AnalogMeter.cpp
    # just draw them directly:
    #   - knob-v4/screw-v4/vu-face-v4: a measured CIRCULAR alpha cutout (the
    #     object is solid/opaque matter that must occlude whatever sits
    #     behind it - a luminance-derived alpha would wrongly turn the
    #     object's own shadowed crevices semi-transparent).
    #   - led-v4/tube-glow-v4/reflection-v4: alpha = max(R,G,B) of the
    #     source pixel (a true emissive glow/reflection asset, where
    #     brightness and "how much this should show" are the same thing by
    #     construction - draws correctly with ordinary SrcOver compositing,
    #     no real additive blend mode needed).
    #   - rose-emblem-v4: colour-thresholded (gold vs the source's dark
    #     squircle mounting plate) rather than the earlier iteration's
    #     circular-crop extraction, which left the squircle's background
    #     disc intact - see analysis/extract_rose_v4.py.
    resources/gui/faceplate-silentium-v4-base.png
    resources/gui/reflection-v4.png
    resources/gui/tube-glow-v4.png
    resources/gui/rose-emblem-v4.png
    resources/gui/vu-face-v4.png
    resources/gui/led-v4.png
    resources/gui/knob-v4.png
    resources/gui/screw-v4.png
    resources/gui/vu-needle-master-v3.png
    # Suite typography: EB Garamond (OFL, resources/fonts/OFL-EBGaramond.txt)
    # for every JUCE-drawn text surface.
    resources/fonts/EBGaramond-Regular.ttf
    resources/fonts/EBGaramond-SemiBold.ttf
)

# ==============================================================================
# SharedCode: an INTERFACE target carrying the plugin's sources, definitions,
# and library links so that both the plugin targets (AU/VST3/Standalone) and
# the Tests executable compile the exact same code (Pamplejuce pattern, see
# https://github.com/sudara/pamplejuce), avoiding ODR violations between them.
# ==============================================================================

add_library(SharedCode INTERFACE)

target_compile_features(SharedCode INTERFACE cxx_std_20)

file(GLOB_RECURSE SilentiumSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

target_sources(SharedCode INTERFACE ${SilentiumSources})

target_include_directories(SharedCode INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(SharedCode INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # AGPLv3 usage permits disabling the splash screen.
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# Add any extra juce:: modules a specific plugin needs (e.g. juce_audio_formats
# for reading impulse-response files) to this list.
target_link_libraries(SharedCode INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    SilentiumBinaryData
)

target_link_libraries(Silentium PRIVATE SharedCode)

# ==============================================================================
# Tests
# ==============================================================================

enable_testing()
set(CTEST_OUTPUT_ON_FAILURE ON)

CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.15.2
)

file(GLOB_RECURSE SilentiumTestSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h")

add_executable(Tests ${SilentiumTestSources})

target_compile_features(Tests PRIVATE cxx_std_20)

target_link_libraries(Tests PRIVATE SharedCode Catch2::Catch2)

# Copy over the plugin target's compile definitions (JucePlugin_* macros etc.)
# so plugin code compiles identically when built as part of the Tests binary.
target_compile_definitions(Tests PRIVATE
    $<TARGET_PROPERTY:Silentium,COMPILE_DEFINITIONS>
    JUCE_MODAL_LOOPS_PERMITTED=1
)

set_target_properties(Tests PROPERTIES XCODE_GENERATE_SCHEME ON)

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(SilentiumDiscoveryMode PRE_TEST)
else()
    set(SilentiumDiscoveryMode POST_BUILD)
endif()

catch_discover_tests(Tests DISCOVERY_MODE ${SilentiumDiscoveryMode})
