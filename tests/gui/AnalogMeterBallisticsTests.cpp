#include "gui/AnalogMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// AnalogMeter's ballistic integration and dB->tick-angle mapping are pure,
// static functions (see AnalogMeter.h's docs) precisely so they're testable
// without a running juce::Timer/message loop.
TEST_CASE ("AnalogMeter::stepBallistics step response", "[gui]")
{
    using basilica::gui::AnalogMeter;

    SECTION ("non-positive dt or tau snaps straight to target (defensive floor, never divides by zero)")
    {
        CHECK (AnalogMeter::stepBallistics (-20.0f, 0.0f, 0.0f, 0.3f) == Catch::Approx (0.0f));
        CHECK (AnalogMeter::stepBallistics (-20.0f, 0.0f, 1.0f / 30.0f, 0.0f) == Catch::Approx (0.0f));
    }

    SECTION ("one time constant of stepping reaches ~63% of the way to target")
    {
        constexpr float tau = 0.3f;
        constexpr float dt = 1.0f / 30.0f; // 30 Hz, matching AnalogMeter's own timer rate
        constexpr float start = -20.0f;
        constexpr float target = 0.0f;

        auto smoothed = start;
        const auto numSteps = (int) std::lround (tau / dt);

        for (int i = 0; i < numSteps; ++i)
            smoothed = AnalogMeter::stepBallistics (smoothed, target, dt, tau);

        // Exact analytic value after N discrete steps at dt intervals:
        // target - (target - start) * (1 - dt/tau)^N is NOT what a
        // continuous-time one-pole reaches - use the discrete recurrence's
        // own closed form instead so the test is exact, not just "close".
        auto expected = start;
        for (int i = 0; i < numSteps; ++i)
            expected += (target - expected) * (1.0f - std::exp (-dt / tau));

        CHECK (smoothed == Catch::Approx (expected).margin (1.0e-4f));

        // Sanity bound: a one-pole low-pass never overshoots a step input,
        // and one full time constant should have covered clearly more than
        // half (but not all) of the distance to target.
        const auto fractionCovered = (smoothed - start) / (target - start);
        CHECK (fractionCovered > 0.5f);
        CHECK (fractionCovered < 1.0f);
    }

    SECTION ("repeated stepping monotonically approaches target without overshoot")
    {
        constexpr float tau = 0.3f;
        constexpr float dt = 1.0f / 30.0f;
        constexpr float target = 3.0f;

        auto smoothed = -100.0f;
        auto previous = smoothed;

        for (int i = 0; i < 300; ++i)
        {
            smoothed = AnalogMeter::stepBallistics (smoothed, target, dt, tau);
            CHECK (smoothed >= previous);
            CHECK (smoothed <= target);
            previous = smoothed;
        }

        CHECK (smoothed == Catch::Approx (target).margin (0.01f));
    }
}

TEST_CASE ("AnalogMeter::tickAngleDegreesForDb interpolates the baked tick table", "[gui]")
{
    using basilica::gui::AnalogMeter;

    // Exact table points (render_vu_meter.py's TICKS, copied into AnalogMeter.cpp).
    CHECK (AnalogMeter::tickAngleDegreesForDb (-20.0f) == Catch::Approx (-52.0f));
    CHECK (AnalogMeter::tickAngleDegreesForDb (0.0f) == Catch::Approx (0.0f));
    CHECK (AnalogMeter::tickAngleDegreesForDb (3.0f) == Catch::Approx (28.0f));

    SECTION ("midpoint between two adjacent ticks interpolates linearly")
    {
        // -10 -> -40deg, -7 -> -33deg; -8.5 is exactly halfway.
        CHECK (AnalogMeter::tickAngleDegreesForDb (-8.5f) == Catch::Approx (-36.5f));
    }

    SECTION ("values beyond the table clamp to the nearest end, never extrapolate")
    {
        CHECK (AnalogMeter::tickAngleDegreesForDb (-60.0f) == Catch::Approx (-52.0f));
        CHECK (AnalogMeter::tickAngleDegreesForDb (12.0f) == Catch::Approx (28.0f));
    }
}
