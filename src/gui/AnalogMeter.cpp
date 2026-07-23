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

    // Needle FILMSTRIP manifest (v0.3.5) - copied verbatim from
    // resources/gui/needle-filmstrip-v1.json's provenance record, hardcoded
    // here per Yves' brief rather than parsed from that .json at runtime
    // (paint()'s frame-index lookup below is real-time-safe and must not
    // touch the filesystem). A vertical stack of needleFrameCount already-
    // rotated needleFrameW x needleFrameH frames, ascending angle order,
    // each frame's own pivot at its exact centre (0.5, 0.5) - so drawing a
    // frame into a square destination box centred on this component's
    // pivot reproduces the old single-image AffineTransform rotation
    // exactly, without any live rotation.
    constexpr int needleFrameCount = 96;
    constexpr int needleFrameW = 480;
    constexpr int needleFrameH = 480;
    constexpr float needleMinDeg = -32.0f;
    [[maybe_unused]] constexpr float needleMaxDeg = 44.0f; // == needleMinDeg + (needleFrameCount - 1) * needleDegPerFrame, kept for provenance/documentation parity with the .json manifest
    constexpr float needleDegPerFrame = 0.8f;
    [[maybe_unused]] constexpr int needleRestFrameIndex = 40; // the filmstrip's own "straight up" pose - not consulted at runtime (frame index is always derived from the live angle), kept for provenance
    constexpr float needleHubXFraction = 0.5f;
    constexpr float needleHubYFraction = 0.5f;

    static_assert (needleRestFrameIndex >= 0 && needleRestFrameIndex < needleFrameCount);
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

        // 3. Needle - v0.3.5 FILMSTRIP lookup (replaces the single-image
        // live AffineTransform rotation): the needle asset is now a
        // vertical stack of already-rotated frames (see the anonymous
        // namespace's needleFrame* manifest constants above), so paint()
        // only has to pick the nearest frame for the current angle and
        // blit it - no rotation math at draw time. Each frame's own pivot
        // sits at its exact centre, so a square destination box centred on
        // this component's pivot reproduces the old rotation exactly.
        if (assets.needle.isValid())
        {
            const auto needleDrawSize = needleSizeFraction * juce::jmin (bounds.getWidth(), bounds.getHeight());

            const auto targetDeg = tickAngleDegreesForDb (smoothedDb);
            const auto frameIndex = juce::jlimit (0, needleFrameCount - 1,
                                                juce::roundToInt ((targetDeg - needleMinDeg) / needleDegPerFrame));

            const auto destX = juce::roundToInt (pivotX - needleHubXFraction * needleDrawSize);
            const auto destY = juce::roundToInt (pivotY - needleHubYFraction * needleDrawSize);
            const auto destSize = juce::roundToInt (needleDrawSize);

            g.drawImage (assets.needle,
                        destX, destY, destSize, destSize,
                        0, frameIndex * needleFrameH, needleFrameW, needleFrameH);
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
