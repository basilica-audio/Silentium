#include "AnalogMeter.h"
#include "Flicker.h"

#include <cmath>

namespace
{
    // Copied verbatim from
    // .scaffold/gui-assets/faceplate-silentium-v3/faceplate-metadata.json's
    // per-meter "dB_angle_table_deg" (both meters share this same relative
    // table). Measured against the master-03 generation render's own baked
    // needle; NOT re-derived against the fresh vu-face-v4.png asset
    // (v0.3.3) - the brief's own provenance note for that asset states it
    // "matches master's VU exactly", and this table is expressed purely as
    // ANGLES (not pixel radii), which is scale/generation-independent as
    // long as the tick layout's angular design didn't change between
    // renders. Flagged here as an ASSUMPTION, not an independently
    // re-verified measurement - see this revision's handoff notes for the
    // visual check that was actually performed (the needle track close to
    // but not pixel-perfect on top of the ticks in the rendered preview).
    struct Tick
    {
        float db;
        float deg;
    };

    constexpr std::array<Tick, 9> ticks {
        Tick { -20.0f, -26.80f }, Tick { -10.0f, -15.40f }, Tick { -7.0f, -6.29f },
        Tick { -5.0f, 1.58f }, Tick { -3.0f, 9.31f }, Tick { 0.0f, 18.02f },
        Tick { 1.0f, 25.47f }, Tick { 2.0f, 32.92f }, Tick { 3.0f, 40.39f }
    };
}

namespace basilica::gui
{
    AnalogMeter::AnalogMeter (Assets assetsIn, juce::String accessibleTitle, float flickerSeedIn,
                              float pivotXFractionIn, float pivotYFractionIn)
        : assets (std::move (assetsIn)), title (std::move (accessibleTitle)),
          pivotXFraction (pivotXFractionIn), pivotYFraction (pivotYFractionIn),
          flickerPhaseSeed (flickerSeedIn)
    {
        setTitle (title);
        setDescription (title);

        // Pure display - never steals mouse events from controls that may
        // sit under this component's (partly transparent) bounds.
        setInterceptsMouseClicks (false, false);

        startTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;

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

    float AnalogMeter::currentFlickerMultiplier() const noexcept
    {
        const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        return basilica::gui::flickerMultiplier (now, startTimeSeconds, flickerPhaseSeed, flickerAmplitudeFraction);
    }

    void AnalogMeter::setImmediateDbForPreview (float db) noexcept
    {
        targetDb.store (db, std::memory_order_relaxed);
        smoothedDb = db;

        if (db >= peakLedThresholdDb)
        {
            ledHoldRemainingSeconds = ledHoldSeconds;
            ledAlpha = 1.0f;
        }
        else
        {
            ledHoldRemainingSeconds = 0.0f;
            ledAlpha = 0.0f;
        }

        repaint();
    }

    void AnalogMeter::timerCallback()
    {
        const auto target = targetDb.load (std::memory_order_relaxed);
        const auto dt = 1.0f / (float) timerHz;

        const auto next = stepBallistics (smoothedDb, target, dt, ballisticsTauSeconds);
        const auto dbChanged = ! juce::approximatelyEqual (next, smoothedDb);
        smoothedDb = next;

        // Peak-LED state machine (Yves' brief): full alpha while the RAW
        // (unsmoothed) reading is at/above 0dB and for a further 200ms hold
        // once it drops back below, then a 500ms linear fade to fully off -
        // driven from the instantaneous target, not the ballistic-smoothed
        // dial reading, matching a real peak LED's fast response.
        if (target >= peakLedThresholdDb)
        {
            ledHoldRemainingSeconds = ledHoldSeconds;
            ledAlpha = 1.0f;
        }
        else if (ledHoldRemainingSeconds > 0.0f)
        {
            ledHoldRemainingSeconds -= dt;
            ledAlpha = 1.0f;
        }
        else if (ledAlpha > 0.0f)
        {
            ledAlpha = juce::jmax (0.0f, ledAlpha - dt / ledFadeSeconds);
        }

        // The incandescent glow's flicker needs continuous repaints even
        // when the dB reading is stable - skip entirely when not on screen
        // (host bypass / hidden window), per Yves' brief, rather than
        // spending repaint cycles on an invisible component.
        if (isShowing())
            repaint();
        else if (dbChanged)
            repaint();
    }

    void AnalogMeter::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        // 1. Dial face (ticks, "VU" wordmark, red zone, hub/anchor bar) -
        // fills the whole component, which PluginEditor.cpp has already
        // sized/positioned so this lands the face's own bezel on the
        // plate's dial void.
        if (assets.face.isValid())
            g.drawImage (assets.face, bounds);

        const auto pivotX = bounds.getWidth() * pivotXFraction;
        const auto pivotY = bounds.getHeight() * pivotYFraction;
        const auto halfSize = 0.5f * juce::jmin (bounds.getWidth(), bounds.getHeight());

        // 2. Incandescent pilot-lamp glow - drawn UNDER the needle, matching
        // a grain-of-wheat pilot lamp sitting behind the dial just above the
        // hub. Flicker gently modulates both alpha stops in lockstep (a
        // single scalar multiplier keeps the two-stop gradient's relative
        // shape constant while its overall brightness breathes).
        {
            const auto flicker = currentFlickerMultiplier();
            const auto glowCx = pivotX;
            const auto glowCy = pivotY + glowCentreOffsetYFraction * halfSize;
            const auto glowRadius = glowRadiusFraction * halfSize;

            juce::ColourGradient glowGradient (
                juce::Colour::fromRGB (255, 200, 120).withAlpha (juce::jlimit (0.0f, 1.0f, glowAlphaCentre * flicker)),
                glowCx, glowCy,
                juce::Colours::transparentBlack,
                glowCx, glowCy + glowRadius,
                true);
            glowGradient.addColour (0.5, juce::Colour::fromRGB (255, 170, 90)
                                             .withAlpha (juce::jlimit (0.0f, 1.0f, glowAlphaMid * flicker)));

            g.setGradientFill (glowGradient);
            g.fillRect (bounds);
        }

        // 3. Peak LED (upper-left of the dial) - alpha-animated by the
        // peak-hold/fade state machine in timerCallback(), fully skipped
        // (no draw call at all) when its alpha is at/near zero so the
        // asset's own baked halo never leaves a faint always-on ring.
        if (assets.led.isValid() && ledAlpha > 0.001f)
        {
            const auto ledCx = pivotX + ledCentreOffsetXFraction * halfSize;
            const auto ledCy = pivotY + ledCentreOffsetYFraction * halfSize;
            const auto ledDrawSize = ledDiameterFraction * (2.0f * halfSize) / ledContentDiameterFraction;

            juce::Graphics::ScopedSaveState saveState (g);
            g.setOpacity (ledAlpha);
            g.drawImage (assets.led,
                        juce::Rectangle<float> (ledDrawSize, ledDrawSize).withCentre ({ ledCx, ledCy }));
        }

        // 4. Rotating needle.
        if (assets.needle.isValid())
        {
            const auto needleDrawSize = needleSizeFraction * juce::jmin (bounds.getWidth(), bounds.getHeight());
            const auto sx = needleDrawSize / (float) assets.needle.getWidth();
            const auto sy = needleDrawSize / (float) assets.needle.getHeight();

            // The needle asset is rendered at rest pointing straight up
            // (0 deg) with its own pivot dead-centre on its square canvas -
            // the measured tick angle IS the absolute rotation, no
            // rest-angle delta to subtract. drawImageTransformed scales
            // from the image's own (0,0) origin, so translate the pivot to
            // the origin first, scale/rotate, then translate back out to
            // this component's actual pivot position.
            const auto targetDeg = tickAngleDegreesForDb (smoothedDb);
            const auto radians = juce::degreesToRadians (targetDeg);

            const auto imageHalfW = (float) assets.needle.getWidth() * 0.5f;
            const auto imageHalfH = (float) assets.needle.getHeight() * 0.5f;

            const auto transform = juce::AffineTransform::translation (-imageHalfW, -imageHalfH)
                                        .scaled (sx, sy)
                                        .rotated (radians)
                                        .translated (pivotX, pivotY);

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
