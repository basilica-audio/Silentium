#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Silentium's own @1x faceplate/control-bay geometry table - lives in its
// own header, rather than as an anonymous-namespace block inside
// PluginEditor.cpp, so tests/gui/EditorLayoutTests.cpp can assert layout
// invariants (e.g. the control bay may never start above the meter bays'
// bottom edge - A-04 of the M3 a11y review) directly against the SAME
// numbers PluginEditor.cpp actually lays components out with, instead of a
// second hand-copied set of constants that could silently drift out of
// sync.
//
// v0.3.1 visual overhaul: this table was re-authored TOGETHER with
// .scaffold/gui-assets/render_faceplate_silentium_v2.py (the Blender script
// that bakes the engraved EB Garamond labels into the faceplate PNG) and
// faceplate-silentium-v2/layout-manifest.json - all three carry the same
// px numbers, and the engraved label positions are derived from the same
// integer-division grid math resized() uses, so baked labels and live
// controls cannot drift. If any number here changes, the faceplate must be
// re-rendered and the manifest updated in the same commit.
namespace slnt::layout
{
    // juce::Rectangle/Point's constructors are not constexpr (JUCE 8.0.14),
    // so the rects below are plain namespace-scope consts rather than true
    // constexpr - still zero-initialisation-order risk since they only
    // depend on integer literals.
    constexpr int plateWidth1x = 900;
    constexpr int plateHeight1x = 600;

    const juce::Rectangle<int> headerBay1x { 109, 46, 682, 71 };
    const juce::Point<int> roundelCentre1x { 450, 82 };
    constexpr int roundelRadius1x = 35;

    // v0.3.1: CIRCULAR meters replace the old 286x150 rectangular meter
    // bays - square 190px bays, dial centres (280, 217) and (620, 217),
    // symmetric about the plate centre. v0.3.2: the meter asset family
    // changed again (vu-dome-v1 -> vu-nano-v1, see AnalogMeter.h/.cpp) but
    // this bay geometry did NOT - the visible dial still fills exactly
    // 190px regardless of the backing asset's own content-fraction/margin,
    // see AnalogMeter::contentFractionOfCanvas's docs. Margins between
    // vertically adjacent bays stay explicit and are asserted by
    // tests/gui/EditorLayoutTests.cpp:
    //   headerBay bottom (46 + 71 = 117)
    //     -> 5px margin ->
    //   meter bays y 122, bottom (122 + 190 = 312)
    //     -> 7px margin ->
    //   controlBay top (319), bottom (319 + 205 = 524)
    //     -> 7px margin ->
    //   auxBay top (531), bottom (531 + 44 = 575)
    const juce::Rectangle<int> meterLBay1x { 185, 122, 190, 190 };
    const juce::Rectangle<int> meterRBay1x { 525, 122, 190, 190 };

    const juce::Rectangle<int> controlBay1x { 82, 319, 735, 205 };
    const juce::Rectangle<int> auxBay1x { 109, 531, 682, 44 };

    // Extra strip above the plate art for the preset bar + scale control -
    // interactive text/menus don't fit the plate's own thin engraved aux
    // strip at any legible size, so they live in their own band instead (the
    // plate's aux bay is used purely for the Duck/Listen toggles). Styled as
    // an integrated header strip by BasilicaLookAndFeel since v0.3.1 (brass
    // buttons + recessed preset-name display), not a raw-JUCE toolbar.
    constexpr int topStripHeight1x = 32;
    constexpr int topStripGap1x = 6;
    constexpr int scaleButtonWidth1x = 64;

    constexpr int baseEditorWidth = plateWidth1x;
    constexpr int baseEditorHeight = topStripHeight1x + topStripGap1x + plateHeight1x;

    constexpr std::array<float, 3> scaleSteps { 1.0f, 1.5f, 2.0f };

    // Control-bay knob grid: 5 columns x 2 rows (9 knobs used, row 2's 5th
    // cell left empty). cellW = 735/5 = 147 exactly; cellH = 205/2 = 102
    // (integer division, matching resized()). The top 16px of each cell is
    // the label band - since v0.3.1 the labels there are ENGRAVED INTO the
    // faceplate PNG (EB Garamond, gold inlay), not juce::Labels; the band
    // constant remains so the knob's vertical centring math (and this
    // header's contract with the Blender script) is unchanged.
    constexpr int gridCols = 5;
    constexpr int gridRows = 2;
    constexpr int knobLabelHeight1x = 16;
    constexpr int knobDiameter1x = 84;

    // v0.3.1: fixed toggle housing size (toggle-brass-v2 renders a complete
    // housed switch at 40px @1x / 80px @2x) - previously derived from the
    // aux bay height with a 34px cap.
    constexpr int toggleSize1x = 40;
}
