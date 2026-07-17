#include "AnalogMeter.h"
#include "ImageDensity.h"

namespace
{
    // Copied verbatim from .scaffold/gui-assets/render_vu_meter.py's TICKS
    // table (the actual generator of the face's engraved arc) - (dB, degrees
    // clockwise from straight-up, matching juce::AffineTransform::rotated's
    // "clockwise" convention once the source PNG is read as ordinary
    // pixel-space image data - verified against vu_brass_preview.png, whose
    // needle sweeps from lower-left (-20 dB) up through top-centre (0 dB) to
    // upper-right (+3 dB), i.e. clockwise as dB increases). Not evenly
    // spaced - this is a hand-tuned classic-VU arc (README's own words), not
    // a physically derived curve.
    struct Tick
    {
        float db;
        float deg;
    };

    constexpr std::array<Tick, 11> ticks {
        Tick { -20.0f, -52.0f }, Tick { -10.0f, -40.0f }, Tick { -7.0f, -33.0f },
        Tick { -5.0f, -26.0f }, Tick { -3.0f, -19.0f }, Tick { -2.0f, -14.0f },
        Tick { -1.0f, -8.0f }, Tick { 0.0f, 0.0f }, Tick { 1.0f, 9.0f },
        Tick { 2.0f, 18.0f }, Tick { 3.0f, 28.0f }
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
        // because vu-brass-v1's layers carry large transparent margins
        // around the dial content (see contentFractionOfCanvas in
        // AnalogMeter.h), so the component is deliberately laid out larger
        // than the visible dial.
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

    const juce::Image& AnalogMeter::faceForCurrentWidth() const noexcept
    {
        const auto native1xWidth = assets.face1x.isValid() ? assets.face1x.getWidth() : (assets.face2x.getWidth() / 2);
        return basilica::gui::pickImageForWidth (assets.face1x, assets.face2x, native1xWidth, getWidth());
    }

    const juce::Image& AnalogMeter::needleForCurrentWidth() const noexcept
    {
        const auto native1xWidth = assets.needle1x.isValid() ? assets.needle1x.getWidth() : (assets.needle2x.getWidth() / 2);
        return basilica::gui::pickImageForWidth (assets.needle1x, assets.needle2x, native1xWidth, getWidth());
    }

    const juce::Image& AnalogMeter::glassForCurrentWidth() const noexcept
    {
        const auto native1xWidth = assets.glass1x.isValid() ? assets.glass1x.getWidth() : (assets.glass2x.getWidth() / 2);
        return basilica::gui::pickImageForWidth (assets.glass1x, assets.glass2x, native1xWidth, getWidth());
    }

    void AnalogMeter::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();

        const auto& face = faceForCurrentWidth();
        if (face.isValid())
            g.drawImage (face, bounds);

        const auto& needle = needleForCurrentWidth();
        if (needle.isValid())
        {
            const auto sx = bounds.getWidth() / (float) needle.getWidth();
            const auto sy = bounds.getHeight() / (float) needle.getHeight();
            const auto pivotX = bounds.getWidth() * pivotXFraction;
            const auto pivotY = bounds.getHeight() * pivotYFraction;

            const auto restDeg = ticks.front().deg;
            const auto targetDeg = tickAngleDegreesForDb (smoothedDb);
            const auto deltaRadians = juce::degreesToRadians (targetDeg - restDeg);

            const auto transform = juce::AffineTransform::scale (sx, sy)
                                        .rotated (deltaRadians, pivotX, pivotY);

            g.drawImageTransformed (needle, transform);
        }

        const auto& glass = glassForCurrentWidth();
        if (glass.isValid())
            g.drawImage (glass, bounds);
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
