#include "PluginEditorLayout.h"

#include <catch2/catch_test_macros.hpp>

// A-04 fix (M3 a11y review): the control-bay knob grid must never start
// above the meter bays' bottom edge, else row-1 knob labels get drawn over
// the still-opaque VU meter dial area (both a usability defect and,
// separately, WCAG 1.4.3's worst-case contrast failure - see
// BasilicaLookAndFeelContrastTests.cpp). Asserted directly against the same
// slnt::layout constants PluginEditor.cpp lays components out with (see
// PluginEditorLayout.h), so this test and the actual layout can never
// silently drift apart.
TEST_CASE ("Control bay starts at or below both meter bays' bottom edge", "[gui][layout]")
{
    using namespace slnt::layout;

    CHECK (controlBay1x.getY() >= meterLBay1x.getBottom());
    CHECK (controlBay1x.getY() >= meterRBay1x.getBottom());
}

TEST_CASE ("Control bay ends at or above the aux bay's top edge", "[gui][layout]")
{
    using namespace slnt::layout;

    CHECK (controlBay1x.getBottom() <= auxBay1x.getY());
}

TEST_CASE ("Header bay starts at or below the plate's top edge and above the meter bays", "[gui][layout]")
{
    using namespace slnt::layout;

    CHECK (headerBay1x.getY() >= 0);
    CHECK (headerBay1x.getBottom() <= meterLBay1x.getY());
}

TEST_CASE ("Knob grid cells are tall enough for a label plus a full-diameter knob with no overlap", "[gui][layout]")
{
    using namespace slnt::layout;

    const auto cellH = controlBay1x.getHeight() / gridRows;
    CHECK (cellH - knobLabelHeight1x >= knobDiameter1x);
}

TEST_CASE ("Every laid-out bay stays within the plate's own canvas bounds", "[gui][layout]")
{
    using namespace slnt::layout;

    const juce::Rectangle<int> plateCanvas { 0, 0, plateWidth1x, plateHeight1x };

    for (const auto& bay : { headerBay1x, meterLBay1x, meterRBay1x, controlBay1x, auxBay1x })
        CHECK (plateCanvas.contains (bay));
}
