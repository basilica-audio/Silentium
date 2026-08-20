#include "gui/FilmstripSwitch.h"
#include "gui/KnobSlider.h"

#include <catch2/catch_test_macros.hpp>

// Issue #33 (aux control bay): pure frame-selection maths for the two
// filmstrip-backed control classes - the successor to the retired
// FilmstripFrameMathTests.cpp (removed with its dead component classes in
// #37; the maths came back into service with the aux bay, so its edge-case
// coverage comes back with it).

TEST_CASE ("KnobSlider::frameIndexForValue maps the normalised range onto the strip's frames", "[gui]")
{
    using basilica::gui::KnobSlider;

    constexpr int numFrames = 128;

    // Extremes land exactly on the first/last frame.
    CHECK (KnobSlider::frameIndexForValue (0.0, numFrames) == 0);
    CHECK (KnobSlider::frameIndexForValue (1.0, numFrames) == numFrames - 1);

    // Midpoint lands on the middle of the sweep (rounded).
    CHECK (KnobSlider::frameIndexForValue (0.5, numFrames) == 64);

    // Monotonic: a larger proportion never selects an earlier frame.
    int previous = 0;
    for (double v = 0.0; v <= 1.0; v += 0.01)
    {
        const auto frame = KnobSlider::frameIndexForValue (v, numFrames);
        CHECK (frame >= previous);
        previous = frame;
    }

    // Defensive clamping: out-of-range proportions stay on valid frames
    // instead of reading out of the strip's bounds.
    CHECK (KnobSlider::frameIndexForValue (-0.5, numFrames) == 0);
    CHECK (KnobSlider::frameIndexForValue (1.5, numFrames) == numFrames - 1);

    // Degenerate single-frame strip: every proportion selects frame 0.
    CHECK (KnobSlider::frameIndexForValue (0.0, 1) == 0);
    CHECK (KnobSlider::frameIndexForValue (1.0, 1) == 0);
}

TEST_CASE ("FilmstripSwitch::frameIndexFor reproduces the 4-frame state table", "[gui]")
{
    using basilica::gui::FilmstripSwitch;

    // The asset's own frame TABLE (toggle-brass README): 0 = off, 1 = on,
    // 2 = off+hover, 3 = on+hover. NOTE: the README's prose formula
    // ("state * 2 + hovered") does not reproduce this table - this method
    // is the verified mapping (see FilmstripSwitch.h).
    CHECK (FilmstripSwitch::frameIndexFor (false, false) == 0);
    CHECK (FilmstripSwitch::frameIndexFor (true, false) == 1);
    CHECK (FilmstripSwitch::frameIndexFor (false, true) == 2);
    CHECK (FilmstripSwitch::frameIndexFor (true, true) == 3);
}
