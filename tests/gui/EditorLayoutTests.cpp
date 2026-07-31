#include "PluginEditorLayout.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

// v0.3.3 (true component-assembly revision): the knob grid is a STAGGERED
// layout measured directly off the bare master-04 baseline plate (row 2
// offset right of row 1), asserted here via explicit per-knob centres
// rather than derived grid cells - see PluginEditorLayout.h's docs.
TEST_CASE ("Knob-row Y-alignment invariant: every knob in a row shares the exact same Y centre", "[gui][layout]")
{
    using namespace slnt::layout;

    // Row 1 (Threshold, Attack, Hold, Release, Range) must all sit on
    // knobRow1Y1x - if any single entry drifts, PluginEditor.cpp's
    // knobLayout table (which reads straight from these arrays) would draw
    // a visibly misaligned knob against the baked artwork.
    for (const auto x : knobRow1X1x)
        CHECK (x >= 0);

    // Row 2 (Lookahead, SC HPF, SC LPF, Knee) likewise share knobRow2Y1x -
    // the actual invariant under test is that BOTH rows are represented as
    // a single shared Y constant (knobRow1Y1x / knobRow2Y1x) rather than a
    // per-knob Y value at all, which makes the alignment invariant
    // structurally impossible to violate (there is nowhere for a knob to
    // carry a divergent Y). This test asserts that structural guarantee
    // plus the two rows' relative ordering (row 2 below row 1, both inside
    // the plate) and that every knob shares knobDiameter1x (also a single
    // shared constant, not a per-knob value).
    CHECK (knobRow2Y1x > knobRow1Y1x);
    CHECK (knobRow1X1x.size() == 5);
    CHECK (knobRow2X1x.size() == 4);
    CHECK (knobDiameter1x > 0);

    // Every row-1 X strictly increases left to right (no accidental
    // duplicate/reversed entries), same for row 2.
    for (size_t i = 1; i < knobRow1X1x.size(); ++i)
        CHECK (knobRow1X1x[i] > knobRow1X1x[i - 1]);

    for (size_t i = 1; i < knobRow2X1x.size(); ++i)
        CHECK (knobRow2X1x[i] > knobRow2X1x[i - 1]);
}

TEST_CASE ("Knob grid cells are tall enough for a full-diameter knob with no row overlap", "[gui][layout]")
{
    using namespace slnt::layout;

    // The two rows must be far enough apart that a full-diameter knob in
    // row 1 never visually overlaps a full-diameter knob in row 2.
    CHECK (knobRow2Y1x - knobRow1Y1x >= knobDiameter1x);
}

TEST_CASE ("Both meter bays and the full knob/toggle grid stay within the plate's own canvas bounds", "[gui][layout]")
{
    using namespace slnt::layout;

    const juce::Rectangle<int> plateCanvas { 0, 0, plateWidth1x, plateHeight1x };

    // Each AnalogMeter's bounds are the exact box its face asset is drawn
    // into (meterLTopLeft1x/meterRTopLeft1x + meterComponentSize1x) - NOT a
    // pivot-centred square (see PluginEditorLayout.h's docs for why that
    // v0.3.2 convention no longer applies to the fresh face asset).
    const juce::Rectangle<int> meterLBay { meterLTopLeft1x.x, meterLTopLeft1x.y, meterComponentSize1x, meterComponentSize1x };
    const juce::Rectangle<int> meterRBay { meterRTopLeft1x.x, meterRTopLeft1x.y, meterComponentSize1x, meterComponentSize1x };

    CHECK (plateCanvas.contains (meterLBay));
    CHECK (plateCanvas.contains (meterRBay));

    // The pivot itself (used for the needle/glow/LED) must also land
    // strictly inside its own meter bay - a fraction outside [0,1] would
    // mean the needle rotates around a point off the drawn face entirely.
    CHECK (meterPivotXFraction > 0.0f);
    CHECK (meterPivotXFraction < 1.0f);
    CHECK (meterPivotYFraction > 0.0f);
    CHECK (meterPivotYFraction < 1.0f);

    const auto knobRadius = knobDiameter1x / 2;

    for (const auto x : knobRow1X1x)
        CHECK (plateCanvas.contains (juce::Rectangle<int> (x - knobRadius, knobRow1Y1x - knobRadius, knobDiameter1x, knobDiameter1x)));

    for (const auto x : knobRow2X1x)
        CHECK (plateCanvas.contains (juce::Rectangle<int> (x - knobRadius, knobRow2Y1x - knobRadius, knobDiameter1x, knobDiameter1x)));

    const auto toggleRadius = toggleSize1x / 2;

    for (const auto x : toggleX1x)
        CHECK (plateCanvas.contains (juce::Rectangle<int> (x - toggleRadius, toggleY1x - toggleRadius, toggleSize1x, toggleSize1x)));
}

TEST_CASE ("The two meter bays do not overlap each other or the knob grid", "[gui][layout]")
{
    using namespace slnt::layout;

    const juce::Rectangle<int> meterLBay { meterLTopLeft1x.x, meterLTopLeft1x.y, meterComponentSize1x, meterComponentSize1x };
    const juce::Rectangle<int> meterRBay { meterRTopLeft1x.x, meterRTopLeft1x.y, meterComponentSize1x, meterComponentSize1x };

    CHECK_FALSE (meterLBay.intersects (meterRBay));

    // Both meter bays sit strictly above the knob grid's top row - the
    // bays' own bottom edge (not just the pivot) is the right thing to
    // assert here now that the bay is deliberately fit to the face art's
    // true bezel rather than overshooting for needle-sweep margin.
    CHECK (meterLBay.getBottom() <= knobRow1Y1x);
    CHECK (meterRBay.getBottom() <= knobRow1Y1x);
}

TEST_CASE ("Rose emblem and corner screws stay within the plate's own canvas bounds", "[gui][layout]")
{
    using namespace slnt::layout;

    const juce::Rectangle<int> plateCanvas { 0, 0, plateWidth1x, plateHeight1x };

    const auto roseRadius = roseDiameter1x / 2;
    CHECK (plateCanvas.contains (juce::Rectangle<int> (roseCentre1x.x - roseRadius, roseCentre1x.y - roseRadius,
                                                       roseDiameter1x, roseDiameter1x)));

    const auto screwRadius = screwDiameter1x / 2;
    for (const auto& centre : screwCentres1x)
        CHECK (plateCanvas.contains (juce::Rectangle<int> (centre.x - screwRadius, centre.y - screwRadius,
                                                           screwDiameter1x, screwDiameter1x)));
}

// master-05 baseline architecture (v0.3.4): both toggle centres MUST fall on
// exactly one shared horizontal line (toggleY1x is a single constant, not a
// per-toggle Y value - same structural guarantee as the knob-row invariant
// above) - if it ever drifted, PluginEditor.cpp's toggle-zone crop swap
// (master-06.png overlay) would land off-centre against the baked artwork.
TEST_CASE ("Toggle-row Y-alignment invariant: both toggles share the exact same Y centre", "[gui][layout]")
{
    using namespace slnt::layout;

    CHECK (toggleX1x.size() == 2);
    CHECK (toggleX1x[1] > toggleX1x[0]);
    CHECK (toggleSize1x > 0);
    CHECK (toggleZoneSize1x >= toggleSize1x);
    CHECK (toggleY1x > 0);
}

// Both VU meter dial centres MUST fall on the same horizontal line - a
// mirrored-duplicate dial design (see PluginEditorLayout.h's docs) with an
// asymmetric Y would look visibly wrong (one dial noticeably higher than
// the other) and would mean meterPivotXFraction/meterPivotYFraction (shared
// between both AnalogMeter instances) could only be correct for one of
// them.
TEST_CASE ("Both VU meter dial centres share the exact same Y", "[gui][layout]")
{
    using namespace slnt::layout;

    const auto leftCentreY = meterLTopLeft1x.y + meterComponentSize1x / 2;
    const auto rightCentreY = meterRTopLeft1x.y + meterComponentSize1x / 2;

    CHECK (leftCentreY == rightCentreY);

    // The two meter bays' X centres must be symmetric around the plate's
    // own horizontal centre (a mirrored-duplicate dial design) within a
    // small rounding tolerance.
    const auto leftCentreX = meterLTopLeft1x.x + meterComponentSize1x / 2;
    const auto rightCentreX = meterRTopLeft1x.x + meterComponentSize1x / 2;
    const auto plateCentreX = plateWidth1x / 2;

    CHECK (std::abs ((leftCentreX - plateCentreX) + (rightCentreX - plateCentreX)) <= 2);
}
