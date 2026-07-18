#include "AnalogMeter.h"

namespace
{
    // Copied verbatim from .scaffold/gui-assets/vu-nano-v1/vu-metadata.json's
    // tick_angle_at_db (measured by analyze_face.py against
    // vu-face-no-needle.png - see that script's docstring for the polar
    // "unwrap" method). v0.3.2: the vu-nano-v1 asset family - this table is
    // NOT interchangeable with vu-dome-v1's old ~93-degree table even though
    // the two happen to look superficially similar (both classic VU arcs);
    // a side-by-side overlay confirmed the old table's rays land measurably
    // off this face's actual tick marks. Nine labelled ticks (-20 dB has the
    // widest sweep down to +3 dB), not evenly spaced - a hand-tuned classic
    // VU arc, not a physically derived curve.
    struct Tick
    {
        float db;
        float deg;
    };

    constexpr std::array<Tick, 9> ticks {
        Tick { -20.0f, -41.94f }, Tick { -10.0f, -27.69f }, Tick { -7.0f, -16.31f },
        Tick { -5.0f, -6.47f }, Tick { -3.0f, 3.19f }, Tick { 0.0f, 14.08f },
        Tick { 1.0f, 23.39f }, Tick { 2.0f, 32.71f }, Tick { 3.0f, 42.04f }
    };
}

namespace basilica::gui
{
    AnalogMeter::AnalogMeter (Assets assetsIn, juce::String accessibleTitle)
        : assets (std::move (assetsIn)), title (std::move (accessibleTitle))
    {
        setTitle (title);
        setDescription (title);

        // Pure display - never steals mouse events from controls that may
        // sit under this component's (partly transparent) bounds. Relevant
        // because vu-nano-v1's layers carry a transparent margin around the
        // dial content (see contentFractionOfCanvas in AnalogMeter.h), so
        // the component is deliberately laid out larger than the visible
        // dial.
        setInterceptsMouseClicks (false, false);

        startTimerHz ((int) timerHz);
    }

    AnalogMeter::~AnalogMeter()
    {
        stopTimer();
    }

    float AnalogMeter::tickAngleDegreesForDb (float db) noexcept
    {
        if (db <= ticks.front().db)
            return ticks.front().deg;

        if (db >= ticks.back().db)
            return ticks.back().deg;

        for (size_t i = 1; i < ticks.size(); ++i)
        {
            if (db <= ticks[i].db)
            {
                const auto& lo = ticks[i - 1];
                const auto& hi = ticks[i];
                const auto span = hi.db - lo.db;
                const auto t = span > 0.0f ? (db - lo.db) / span : 0.0f;
                return lo.deg + t * (hi.deg - lo.deg);
            }
        }

        return ticks.back().deg;
    }

    float AnalogMeter::stepBallistics (float currentSmoothed, float target, float dtSeconds, float tauSeconds) noexcept
    {
        if (tauSeconds <= 0.0f || dtSeconds <= 0.0f)
            return target;

        const auto alpha = 1.0f - std::exp (-dtSeconds / tauSeconds);
        return currentSmoothed + (target - currentSmoothed) * alpha;
    }

    void AnalogMeter::timerCallback()
    {
        const auto target = targetDb.load (std::memory_order_relaxed);
        const auto next = stepBallistics (smoothedDb, target, 1.0f / (float) timerHz, ballisticsTauSeconds);

        if (! juce::approximatelyEqual (next, smoothedDb))
        {
            smoothedDb = next;
            repaint();
        }
    }

    void AnalogMeter::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();

        if (assets.face.isValid())
            g.drawImage (assets.face, bounds);

        if (assets.needle.isValid())
        {
            const auto sx = bounds.getWidth() / (float) assets.needle.getWidth();
            const auto sy = bounds.getHeight() / (float) assets.needle.getHeight();
            const auto pivotX = bounds.getWidth() * pivotXFraction;
            const auto pivotY = bounds.getHeight() * pivotYFraction;

            // vu-nano-v1's needle is rendered at rest pointing straight up
            // (0 deg) - the measured tick angle IS the absolute rotation,
            // no rest-angle delta to subtract (unlike vu-dome-v1).
            const auto targetDeg = tickAngleDegreesForDb (smoothedDb);
            const auto radians = juce::degreesToRadians (targetDeg);

            const auto transform = juce::AffineTransform::scale (sx, sy)
                                        .rotated (radians, pivotX, pivotY);

            g.drawImageTransformed (assets.needle, transform);
        }
    }

    // A-07 fix (M3 a11y review): a read-only text value interface exposing
    // the current ballistic-smoothed reading, mirroring the shape of JUCE's
    // own ButtonValueInterface pattern (juce_ButtonAccessibilityHandler.h).
    // Reads the SAME smoothedDb the paint() method just drew, updated at
    // this component's own 30 Hz timer - queried on demand by AT clients,
    // never pushed/announced proactively.
    class AnalogMeter::MeterValueInterface final : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit MeterValueInterface (const AnalogMeter& ownerIn) noexcept : owner (ownerIn) {}

        bool isReadOnly() const override { return true; }

        juce::String getCurrentValueAsString() const override
        {
            return juce::String (owner.smoothedDb, 1) + " dB";
        }

        // Read-only: assistive-technology clients cannot set a meter
        // reading, so this is intentionally a no-op rather than throwing or
        // asserting - matches isReadOnly() == true's documented contract.
        void setValueAsString (const juce::String&) override {}

    private:
        const AnalogMeter& owner;
    };

    std::unique_ptr<juce::AccessibilityHandler> AnalogMeter::createAccessibilityHandler()
    {
        // Read-only display, not an interactive control - AccessibilityRole::
        // label (rather than Component's default ::unspecified) is the
        // closer semantic match for a screen reader. The value interface
        // (A-07 fix) lets AT clients query the current reading on demand
        // without this component ever pushing unsolicited announcements.
        return std::make_unique<juce::AccessibilityHandler> (
            *this,
            juce::AccessibilityRole::label,
            juce::AccessibilityActions {},
            juce::AccessibilityHandler::Interfaces { std::make_unique<MeterValueInterface> (*this) });
    }
}
