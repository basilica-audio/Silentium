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
// This is Silentium-specific, art-authored geometry (derived from
// .scaffold/gui-assets/render_faceplate.py's Blender scene coordinates), not
// a suite-reusable pattern like src/gui/'s FilmstripKnob/FilmstripToggle/
// AnalogMeter/BasilicaLookAndFeel component family - each sibling plugin's
// own faceplate has a different bay layout and will get its own equivalent
// header when it reaches its M3 GUI pass.
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

    // A-04 fix (M3 a11y review): row-1 knob labels used to collide with the
    // meter bays' bottom edge - the old meterLBay1x/meterRBay1x bottom edge
    // (128 + 158 = 286) sat 21px BELOW the old controlBay1x top (265), so
    // row-1 captions were drawn directly over the still-opaque VU meter
    // dial area (visually confirmed in the pre-fix docs/gui-preview.png,
    // and the direct cause of A-03's worst-case ~1.4:1 contrast reading).
    // Both bays are now sized/positioned together with an explicit margin
    // between every pair of adjacent bays - see
    // tests/gui/EditorLayoutTests.cpp's layout-invariant tests, which assert
    // directly against these constants:
    //   meterLBay1x/meterRBay1x bottom (128 + 150 = 278)
    //     -> 6px margin ->
    //   controlBay1x top (284), bottom (284 + 216 = 500)
    //     -> 7px margin ->
    //   auxBay1x top (507)
    const juce::Rectangle<int> meterLBay1x { 145, 128, 286, 150 };
    const juce::Rectangle<int> meterRBay1x { 469, 128, 286, 150 };

    const juce::Rectangle<int> controlBay1x { 82, 284, 736, 216 };
    const juce::Rectangle<int> auxBay1x { 109, 507, 682, 44 };

    // Extra strip above the plate art for the preset bar + scale control -
    // interactive text/menus don't fit the plate's own thin engraved aux
    // strip at any legible size, so they live in their own band instead (the
    // plate's aux bay is used purely for the Duck/Listen toggles).
    constexpr int topStripHeight1x = 32;
    constexpr int topStripGap1x = 6;
    constexpr int scaleButtonWidth1x = 64;

    constexpr int baseEditorWidth = plateWidth1x;
    constexpr int baseEditorHeight = topStripHeight1x + topStripGap1x + plateHeight1x;

    constexpr std::array<float, 3> scaleSteps { 1.0f, 1.5f, 2.0f };

    // Control-bay knob grid: 5 columns x 2 rows (9 knobs used, row 2's 5th
    // cell left empty) - the control bay is wide and shallow, so a 5-wide
    // grid keeps each knob close to its original v0.1/v0.2 ~100px visual
    // size instead of a cramped 3x3 grid.
    constexpr int gridCols = 5;
    constexpr int gridRows = 2;
    constexpr int knobLabelHeight1x = 16;
    constexpr int knobDiameter1x = 90;
}
