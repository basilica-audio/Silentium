#include <catch2/catch_test_macros.hpp>

#include "gui/FilmstripKnob.h"
#include "gui/FilmstripToggle.h"

// Pure frame-index math for the two filmstrip components (src/gui/). No
// juce::Image/juce::Slider/juce::Button state is exercised here - just the
// static mapping functions - so these run in any environment, headless or
// not.
TEST_CASE ("FilmstripKnob::frameIndexForValue edges and midpoint", "[gui]")
{
    using basilica::gui::FilmstripKnob;

    SECTION ("value 0 selects the first frame")
    {
        CHECK (FilmstripKnob::frameIndexForValue (0.0, 128) == 0);
    }

    SECTION ("value 1 selects the last frame")
    {
        CHECK (FilmstripKnob::frameIndexForValue (1.0, 128) == 127);
    }

    SECTION ("value 0.5 selects the middle frame")
    {
        CHECK (FilmstripKnob::frameIndexForValue (0.5, 128) == 64);
    }

    SECTION ("out-of-range values are clamped, not read out of bounds")
    {
        CHECK (FilmstripKnob::frameIndexForValue (-0.5, 128) == 0);
        CHECK (FilmstripKnob::frameIndexForValue (1.5, 128) == 127);
    }

    SECTION ("a single-frame strip always selects frame 0")
    {
        CHECK (FilmstripKnob::frameIndexForValue (0.0, 1) == 0);
        CHECK (FilmstripKnob::frameIndexForValue (1.0, 1) == 0);
    }
}

TEST_CASE ("FilmstripToggle::frameIndexFor matches the asset's frame table", "[gui]")
{
    using basilica::gui::FilmstripToggle;

    // toggle-brass-v1/README.md's table: 0=off, 1=on, 2=off+hover, 3=on+hover.
    CHECK (FilmstripToggle::frameIndexFor (false, false) == 0);
    CHECK (FilmstripToggle::frameIndexFor (true, false) == 1);
    CHECK (FilmstripToggle::frameIndexFor (false, true) == 2);
    CHECK (FilmstripToggle::frameIndexFor (true, true) == 3);
}
