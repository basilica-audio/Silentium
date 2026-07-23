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
    # Preset-bar / scale-button brass caps (unrelated to the faceplate
    # rework below - BasilicaLookAndFeel.cpp::drawButtonBackground() 3-slices
    # these for every juce::TextButton the suite draws, e.g. the preset bar's
    # navigation buttons and this editor's scale-cycle button). Deliberately
    # OUT OF SCOPE for the master-05 baseline revision (BasilicaLookAndFeel.{h,cpp}
    # is not in that revision's file whitelist) - kept embedded even though
    # an earlier draft of this revision's asset-removal list named them
    # alongside the (genuinely unrelated) toggle_brass_v2_strip_* footer-
    # toggle asset family; removing them would break BasilicaLookAndFeel.cpp's
    # compile.
    resources/gui/button_brass_v1_normal_40x28.png
    resources/gui/button_brass_v1_normal_80x56.png
    resources/gui/button_brass_v1_hover_40x28.png
    resources/gui/button_brass_v1_hover_80x56.png
    # v0.3.4 MASTER-05 BASELINE ARCHITECTURE (this revision) - a full
    # replacement of the three prior "component composition" GUI attempts
    # (v0.3.1's bare JUCE-drawn background, v0.3.2's single-master
    # faceplate, v0.3.3's true component assembly of many standalone
    # master-reference PNGs), per Yves' explicit rejection of that
    # Frankenstein result. Exactly THREE faceplate images now ship, copied
    # from .scaffold/gui-assets/faceplate-silentium-v3/ (repo-relative to
    # the suite root, one level above this repo):
    #   - master-05.png: the SOLE faceplate baseline. Bakes the obsidian
    #     plate, brass bevel, 4 corner screws, rose flourish, both VU dial
    #     faces (empty - no needle, no LED), all 9 knobs at 12 o'clock rest,
    #     both toggles UP/on, and both tube-vent grilles at normal glow -
    #     ALL of it, in one image. PluginEditor.cpp draws only this for the
    #     entire plate when every toggle is on and the vent-glow mix is at
    #     its ceiling.
    #   - master-06.png: identical to master-05 except both toggles point
    #     DOWN. PluginEditor.cpp blits ONLY a toggle's own small zone crop
    #     from this image (never the full plate) when that toggle is off.
    #   - master-glow-dim.png: identical to master-05 except the tube-vent
    #     glow is at a SUBTLE, dimmer intensity. PluginEditor.cpp
    #     cross-blends ONLY the two vent-bank region crops between this
    #     image (low signal) and master-05 itself (the approved ceiling -
    #     never a third, brighter "hot" frame; master-glow-hot.png/
    #     master-glow-very-hot.png exist in the scaffold but are
    #     DELIBERATELY NOT embedded - Yves rejected that look verbatim, see
    #     PluginEditor.cpp's paint() docs).
    # PluginEditorLayout.h's geometry table was re-measured directly against
    # master-05.png by
    # .scaffold/gui-assets/faceplate-silentium-v3/analysis/measure_master_05.py
    # (NOT copied from the v0.3.3/master-04 table - a different render
    # generation).
    resources/gui/master-05.png
    resources/gui/master-06.png
    resources/gui/master-glow-dim.png
    # Rotating VU needle overlay - v0.3.5 replaces the single-image
    # AffineTransform-rotated needle (vu-needle-master-v3.png, still on disk
    # but no longer embedded) with a pre-rendered FILMSTRIP: a vertical
    # stack of 96 already-rotated 480x480 frames (-32.0deg..+44.0deg,
    # 0.8deg/frame, rest/0deg = frame 40), each with its pivot at the
    # frame's exact centre. AnalogMeter::paint() now does a frame-index
    # lookup + straight source-rect blit instead of a live rotation - see
    # AnalogMeter.cpp's docs for the manifest constants (hardcoded there,
    # not parsed from resources/gui/needle-filmstrip-v1.json at runtime;
    # that .json ships alongside the PNG purely for provenance).
    resources/gui/needle-filmstrip-v1.png
    # Peak-LED overlay - v0.3.6 replaces led-v4.png (a hand-authored/
    # generated asset, drawn INSIDE the dial over the tick scale - wrong
    # position AND provenance per Yves' master-03 reference) with
    # led-master-diff.png: a clean 64x64 RGBA sprite EXTRACTED directly from
    # master-03-raw.png's own baked lit LED pixels (master-03 is the one
    # master render generation with both LEDs lit; master-05, this build's
    # faceplate baseline, has neither) via a registration + abs-diff
    # technique (analysis/led_diff/{register,extract,finalize}.py in the
    # gui-pipeline scratch dir, mirroring analysis/needle_diff/'s proven
    # needle-extraction approach) - no hand-drawing/generation involved, per
    # the suite's master-fidelity rule. led-v4.png stays on disk
    # (unreferenced) rather than being deleted here. Drawn by PluginEditor
    # (not AnalogMeter - the LED sits on the PLATE outside each dial's own
    # bezel, see PluginEditorLayout.h's ledLCentre1x/ledRCentre1x docs).
    resources/gui/led-master-diff.png
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
