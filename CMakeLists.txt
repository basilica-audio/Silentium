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
    # Rotating VU needle overlay - v0.3.11 REPLACES the v0.3.5-v0.3.7
    # filmstrip approach (needle-filmstrip-v1.png/v2.png, a 96-frame Blender
    # render Yves rejected TWICE - "Du sollst weder mit Blender arbeiten
    # noch sonst wie selbst was an der Nadel zeichnen. Die soll exakt so
    # aussehen wie auf dem Master.") with needle-from-master.png: a single
    # 288x288 RGBA sprite recovered by diffing master-03-raw.png (has
    # needles) against master-05.png (this build's own faceplate baseline,
    # has none) - genuine master pixels, never hand-drawn/Blender-rendered/
    # resampled (see needle-from-master.json's own provenance record,
    # copied alongside for documentation only, and AnalogMeter.cpp's
    # bakedAngleDeg docs for why this sprite is drawn at a live
    # juce::AffineTransform rotation instead of a pre-rotated frame stack).
    # Round-trip-verified: pasted back at its own pivot with zero rotation,
    # it diffs to a 0.69/255 mean residual against the source master inside
    # the needle body. Alpha is transparent within ~43 master px of the
    # pivot (the sprite's own damping-ring occlusion boundary) - this is
    # WIDE ENOUGH that master-05's own baked hub-cap/anchor-bar/boss art
    # (see the crop in this revision's handoff notes) shows through
    # unmodified there, so - unlike the old filmstrip's v0.3.7 hub-occluder
    # workaround - no separate occluder redraw is needed any more (removed
    # this revision; vu-hub-occluder-v1.png stays on disk, unreferenced).
    resources/gui/needle-from-master.png
    # Peak-LED overlay - v0.3.11 REPLACES led-master-diff.png (extracted
    # against a stale registration pass whose measured centres put the LED
    # visibly on top of a footer toggle - Yves: "Diese PEAK LED ...
    # merkwürdigerweise auf einem der Kippschalter") with led-from-master.png:
    # a clean 58x58 RGBA sprite, same master-diff extraction technique
    # (analysis/led_diff/{register,extract}.py), re-measured per dial
    # (left AND right independently, not one sprite's position transferred
    # via a dial-radius fraction - see led-from-master.json's own
    # offsetFractionTransferErrorPx field for why that fraction is
    # unreliable, 58px error on the right dial) so each LED lands exactly
    # where master-03 bakes it: at the top of its own dial bezel. Drawn by
    # PluginEditor (not AnalogMeter - the LED sits on the PLATE outside
    # each dial's own bezel, see PluginEditorLayout.h's
    # ledLCentre1x/ledRCentre1x docs).
    resources/gui/led-from-master.png
    # Org emblem (Basilica Audio rose-window medallion) - v0.3.11 REPLACES
    # org-emblem.png (a generic/wrong rose-window asset with no guitar
    # silhouette - not the Basilica Audio mark at all, hence Yves: "Das
    # Logo in der Mitte fehlt" even though a medallion WAS being drawn)
    # with org-emblem-basilica-v1.png, extracted directly from the suite's
    # canonical brand/org.png (repo-relative to the suite root, one level
    # above this repo - the same source every other plugin's app icon is
    # cut from). brand/org.png is already circular artwork on a flat
    # near-black square canvas (no squircle backing to work around, unlike
    # the old asset's v2-plastic source) - extraction is a Kasa circle fit
    # (ray-cast edge points, std/mean 0.15% across 720 angles - outer ring
    # centre (511.58, 512.55), radius 390.87px in the source's own 1024x1024
    # canvas) plus a colour-threshold alpha (background/near-black ->
    # transparent, gold linework -> opaque, ~1px feather) intersected with a
    # hard circular cutoff at the fitted radius - see
    # PluginEditorLayout.h's org-emblem docs for the full derivation and
    # orgEmblemContentDiameterFraction. Stays perfectly round: no bezel/ring
    # is added by this extraction or by PluginEditor.cpp's draw call (an
    # explicit standing user rule - see that file's paint() docs for the
    # restrained-opacity + soft-shadow-only mounting treatment).
    resources/gui/org-emblem-basilica-v1.png
    # Rotating knob discs (v0.3.9, item 4) - INNER-DISC variant, gate-approved
    # 2026-07-25: nine 85x85 RGBA discs, one per knob, extracted directly from
    # master-05.png's own baked pixels (colour data is an unresampled 1:1
    # crop, per-knob centre from
    # .scaffold/gui-assets/faceplate-silentium-v3/knob-discs-manifest.json).
    # Alpha is cut at 80% of each knob's own fitted radius, so only the
    # pointer notch + inner knurl are visible when this disc is drawn
    # rotated live (PluginEditor.cpp's paint()) - the baked outer rim AND its
    # specular crescent stay part of the static master-05 plate underneath,
    # which is what rules out a co-rotating highlight (the defect the
    # PLAIN full-disc variant hit in the rotation proof; see the manifest's
    # own "innerVariant" record for the measured notch/highlight radii this
    # 80% cutoff was chosen from). The manifest .json itself is NOT embedded
    # (provenance only) - its per-knob centre/radius values are hardcoded as
    # constants in PluginEditor.cpp's knobDiscLayout table instead.
    resources/gui/knob-disc-0-inner.png
    resources/gui/knob-disc-1-inner.png
    resources/gui/knob-disc-2-inner.png
    resources/gui/knob-disc-3-inner.png
    resources/gui/knob-disc-4-inner.png
    resources/gui/knob-disc-5-inner.png
    resources/gui/knob-disc-6-inner.png
    resources/gui/knob-disc-7-inner.png
    resources/gui/knob-disc-8-inner.png
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
