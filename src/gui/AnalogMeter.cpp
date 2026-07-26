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

    // v0.3.11: needle-from-master.png manifest - copied verbatim from
    // resources/gui/needle-from-master.json's provenance record, hardcoded
    // here per the suite's convention (paint() below must not touch the
    // filesystem). A SINGLE 288x288 RGBA sprite recovered by diffing
    // master-03-raw.png (has needles) against master-05.png (this build's
    // own faceplate baseline, has none) - genuine master pixels, never
    // hand-drawn/Blender-modelled/resampled (round-trip-verified: pasted
    // back at its own pivot with zero rotation, it diffs to a 0.69/255
    // mean residual against the source master inside the needle body).
    //
    // Deliberately NOT pre-rotated to a canonical "straight up" pose during
    // extraction - doing so would resample and soften the master's own
    // pixels, exactly what the suite's master-fidelity rule forbids. The
    // sprite therefore sits at its own bakedAngleDeg as delivered, and
    // paint() must apply (targetDeg - bakedAngleDeg) as the LIVE rotation
    // each frame, not targetDeg alone - getting this backwards would park
    // the needle ~40deg away from every tick it should be pointing at.
    constexpr float needleSpriteCanvasPx = 288.0f;
    constexpr float needleBakedAngleDeg = -39.563260406580696f;

    static_assert (needleSpriteCanvasPx > 0.0f);
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

        // Primary (fast) flicker - Flicker.h's standard 3-layer table,
        // +-7% (flickerAmplitudeFraction), this instance's own phase seed.
        const auto fast = basilica::gui::flickerMultiplier (now, startTimeSeconds, flickerPhaseSeed, flickerAmplitudeFraction);

        // v0.3.9 (item 5): a second, much SLOWER drift layer (Flicker.h's
        // slowDriftLayers table, +-3% flickerDriftAmplitudeFraction) ADDED
        // on top of the fast multiplier's own 1.0-centred value (not
        // multiplied - multiplying two independent +-1 centred quantities
        // would halve the effective amplitude when both sit near their
        // troughs simultaneously, which is not how a lamp filament's slow
        // thermal drift plus fast micro-flicker actually combine). The
        // "-1.0f" strips the +1.0 centring flickerMultiplier() itself
        // returns, leaving just the drift term's own +-flickerDrift
        // AmplitudeFraction contribution. flickerDriftPhaseOffset keeps the
        // slow layer's phase distinct from the fast layer's even when both
        // share the same flickerPhaseSeed.
        const auto slow = basilica::gui::flickerMultiplier (now, startTimeSeconds, flickerPhaseSeed + flickerDriftPhaseOffset,
                                                             flickerDriftAmplitudeFraction, basilica::gui::slowDriftLayers)
                           - 1.0f;

        return fast + slow;
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

    void AnalogMeter::setFlickerElapsedSecondsForPreview (double elapsedSeconds) noexcept
    {
        startTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0 - elapsedSeconds;
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

        // The dial face (ticks, "VU" wordmark, red zone, hub/anchor bar) is
        // BAKED into master-05.png (Silentium's single faceplate baseline,
        // see PluginEditor.cpp's paint()) - no draw call for it here.
        // PluginEditorLayout.h positions/sizes this component so the
        // overlays below still land correctly on that baked artwork.
        const auto pivotX = bounds.getWidth() * pivotXFraction;
        const auto pivotY = bounds.getHeight() * pivotYFraction;
        const auto halfSize = 0.5f * juce::jmin (bounds.getWidth(), bounds.getHeight());

        // 1. Incandescent pilot-lamp glow - drawn UNDER the needle, matching
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

        // 2. Peak LED - v0.3.6: moved OUT of this component entirely. Per
        // Yves' master-03 reference the LED sits ON THE PLATE, outside this
        // dial's own bezel/bounds (upper-left) - PluginEditor now draws it
        // as its own overlay at the measured plate-level centre (see
        // PluginEditorLayout.h's ledLCentre1x/ledRCentre1x and
        // PluginEditor.cpp's paint()), reading this component's ledAlpha via
        // peakLedAlpha() each tick. The peak-hold/fade state machine itself
        // (ledAlpha/ledHoldRemainingSeconds, driven from timerCallback()/
        // setImmediateDbForPreview() below) still lives here - it owns the
        // dB data, only the DRAW moved.

        // 3. Needle - v0.3.11: a SINGLE master-extracted sprite
        // (needle-from-master.png), rotated live about its own canvas
        // centre with a juce::AffineTransform, then translated to this
        // meter's pivot - replaces the v0.3.5-v0.3.7 pre-rotated 96-frame
        // filmstrip lookup (see this file's top-of-file manifest docs for
        // why: the sprite was deliberately NOT rotated to a canonical pose
        // during extraction, so a live rotation - not a frame-index blit -
        // is what preserves its master pixels unresampled at rest).
        //
        // CRITICAL: rotationToApply = targetDeg - needleBakedAngleDeg. The
        // sprite's own rod already sits at needleBakedAngleDeg (its pose in
        // the master render it was cut from), so drawing it with
        // targetDeg's own value as the rotation would double-apply that
        // baked pose and park the needle ~40deg away from the tick it
        // should point at - verified against the -20..+3dB tick table
        // above: at 0dB (targetDeg = 18.02), rotationToApply =
        // 18.02 - (-39.5633) = 57.58deg, which is exactly the live turn
        // that brings the sprite's own -39.56deg rest pose onto the dial's
        // 0dB tick.
        if (assets.needle.isValid())
        {
            const auto needleDrawSize = needleSizeFraction * juce::jmin (bounds.getWidth(), bounds.getHeight());
            const auto spriteScale = needleDrawSize / needleSpriteCanvasPx;

            const auto targetDeg = tickAngleDegreesForDb (smoothedDb);
            const auto rotationToApplyDeg = targetDeg - needleBakedAngleDeg;
            const auto rotationRadians = juce::degreesToRadians (rotationToApplyDeg);

            const auto imageHalfW = 0.5f * (float) assets.needle.getWidth();
            const auto imageHalfH = 0.5f * (float) assets.needle.getHeight();

            const auto transform = juce::AffineTransform::translation (-imageHalfW, -imageHalfH)
                                        .scaled (spriteScale)
                                        .rotated (rotationRadians)
                                        .translated (pivotX, pivotY);

            g.drawImageTransformed (assets.needle, transform, false);
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
