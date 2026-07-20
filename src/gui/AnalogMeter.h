#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>

// Suite-reusable analog-style VU meter: draws its own dial FACE, an
// incandescent pilot-lamp glow, a peak LED, and the rotating needle - all
// composited live on top of whatever background sits behind this component
// (Silentium's bare obsidian faceplate, see PluginEditor.cpp).
//
// v0.3.3 (this revision): TRUE COMPONENT ASSEMBLY, per Yves' final art
// direction - every visual element is a standalone master-reference asset
// (.scaffold/gui-assets/faceplate-silentium-v3/, see that folder's
// vu-face-no-led.png/led-master-ref.png) rather than baked into one master
// faceplate PNG. This replaces v0.3.2's "face baked into the shared
// background, only the needle is live" design, because the new bare
// baseline plate (master-04-empty.png) has genuinely EMPTY dial voids with
// no face artwork at all.
//
// Because the fresh vu-face-v4.png asset's own hub (the point the needle
// rotates around) does NOT sit at the exact centre of its square canvas
// (measured ~50%/63% across/down, not 50%/50%), this component's pivot
// fraction is a CONFIGURABLE constructor parameter again (unlike v0.3.2's
// hardcoded 0.5/0.5 "bounds always centred on the pivot" convention, which
// only worked because that revision deliberately never drew a face image).
// The component's bounds are simply "the box the face image is drawn into,
// scaled/positioned so the face's own bezel matches the plate's dial void"
// - PluginEditorLayout.h/PluginEditor.cpp compute that box directly from
// the master-04 measurements, not from any pivot-centring rule.
namespace basilica::gui
{
    class AnalogMeter : public juce::Component, private juce::Timer
    {
    public:
        struct Assets
        {
            // The dial face (ticks, "VU" wordmark, red zone, hub/anchor bar
            // - everything except the needle and the peak LED). Drawn
            // filling this component's full local bounds (see paint()) -
            // callers must therefore size/position the component itself so
            // that mapping lands the face's own bezel on the plate's dial
            // void (PluginEditorLayout.h's meterL/RTopLeft1x + meterComponentSize1x).
            // May be left default/invalid (skips the face draw entirely) -
            // kept for standalone testability without a real asset.
            juce::Image face;
            juce::Image needle;

            // Small red peak-indicator LED (with its own soft halo baked
            // in) - alpha 0 normally, flashed to alpha 1 by the peak
            // detector below (see setTargetDb()/timerCallback()). May be
            // left default/invalid (skips the LED entirely).
            juce::Image led;
        };

        // pivotXFraction/pivotYFraction: where the needle/glow/LED pivot
        // sits, as a fraction of this component's own local bounds (0,0 =
        // top-left, 1,1 = bottom-right) - measured once against the face
        // asset (see PluginEditor.cpp's makeMeterAssets()) and identical for
        // both meters (mirrored-duplicate dial design). Defaults to (0.5,
        // 0.5) for callers that don't care (e.g. the ballistics/
        // accessibility unit tests, which never call paint()).
        //
        // flickerSeedIn: per-instance phase offset for the incandescent
        // glow's flicker (see paint()/timerCallback()) so that two meters
        // sharing the same class never flicker in lockstep - pass a
        // different value per instance (e.g. 0.0f / 1.0f).
        AnalogMeter (Assets assetsIn, juce::String accessibleTitle, float flickerSeedIn = 0.0f,
                    float pivotXFraction = 0.5f, float pivotYFraction = 0.5f);
        ~AnalogMeter() override;

        // Thread-safe (plain atomic store): the instantaneous value in dB,
        // written from the audio thread every processBlock() call. Ballistic
        // smoothing is applied separately, on the GUI thread's timer - never
        // here, so this is real-time safe to call from anywhere.
        void setTargetDb (float newTargetDb) noexcept { targetDb.store (newTargetDb, std::memory_order_relaxed); }

        // Test/preview-only: seeds BOTH the raw target and the ballistic-
        // smoothed reading to the same value immediately, bypassing the
        // ~300ms ramp, and synchronises the peak-LED state machine to match
        // (LED fully lit if db is in the red zone, off otherwise) - normal
        // operation (setTargetDb() + this component's own 30Hz timer) never
        // calls this. Used by tests/gui/EditorSnapshotTests.cpp to render a
        // "live-looking" snapshot without needing to pump 300+ms of real
        // timer ticks through a headless test binary's message loop (which
        // has none - see that test's own docs).
        void setImmediateDbForPreview (float db) noexcept;

        void paint (juce::Graphics& g) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        // One-pole ballistic integration step, exposed as a pure/static
        // function so it is directly unit-testable (see
        // tests/gui/AnalogMeterBallisticsTests.cpp's step-response test)
        // without needing a running juce::Timer/message loop. tauSeconds
        // ~0.3s approximates a real VU meter coil's mechanical inertia (see
        // the basilica-gui-design skill).
        static float stepBallistics (float currentSmoothed, float target, float dtSeconds, float tauSeconds) noexcept;

        // dB -> face-relative rotation angle in degrees, piecewise-linearly
        // interpolated across the master render's own measured tick table
        // (see the .cpp) and clamped beyond the table's ends. Exposed for
        // unit testing. Degrees are clockwise from straight-up (12 o'clock)
        // - this IS the needle's absolute rotation angle (the needle
        // asset's own rest pose is 0 deg / straight up).
        static float tickAngleDegreesForDb (float db) noexcept;

        // Peak threshold (dBFS) above which the LED lights - exposed so
        // PluginEditor/tests can reason about the same constant this
        // component's own timerCallback() uses.
        static constexpr float peakLedThresholdDb = 0.0f;

    private:
        // A-07 fix (M3 a11y review): read-only accessibility value
        // interface, so AT users can query the current ballistic-smoothed
        // reading on demand (VoiceOver's "read value" gesture / NVDA's
        // report-value key) - see the .cpp for the implementation and
        // createAccessibilityHandler() below. Deliberately NOT wired to any
        // live/continuous announcement: this component's own 30 Hz repaint
        // timer must never trigger AT notifications, which would produce
        // constant chatter far worse than the previous silence.
        class MeterValueInterface;

        void timerCallback() override;
        float currentFlickerMultiplier() const noexcept;

        Assets assets;
        juce::String title;

        std::atomic<float> targetDb { -100.0f };
        float smoothedDb = -100.0f;

        const float pivotXFraction;
        const float pivotYFraction;

        // Incandescent pilot-lamp glow geometry (Yves' brief): centred
        // slightly ABOVE the pivot (offset expressed as a fraction of the
        // component half-size), radius as a fraction of the half-size,
        // tapering to fully transparent.
        static constexpr float glowCentreOffsetYFraction = -0.18f;
        static constexpr float glowRadiusFraction = 0.62f;
        static constexpr float glowAlphaCentre = 0.38f;
        static constexpr float glowAlphaMid = 0.16f;

        // Peak LED geometry: offset from the pivot (fraction of the
        // component half-size, same convention as the glow above) - "upper
        // left of the dial" per Yves' brief - and drawn diameter (fraction
        // of the component's own full size, since the LED asset is a small
        // fixed-size indicator rather than something that should scale with
        // the dial's overall proportions the way the glow does).
        static constexpr float ledCentreOffsetXFraction = -0.43f;
        static constexpr float ledCentreOffsetYFraction = -0.55f;
        static constexpr float ledDiameterFraction = 0.14f;
        // Native content geometry inside led-v4.png's 1024x1024 canvas
        // (measured: the bright bulb sphere, ignoring its much larger soft
        // halo which is fine/desirable to let overflow past the nominal
        // draw diameter) - see PluginEditor.cpp's asset docs for the same
        // family of constants for the other master-ref assets.
        static constexpr float ledContentDiameterFraction = 315.0f / 1024.0f;

        // Peak-hold + linear fade state machine (Yves' brief: 200ms full-
        // alpha hold once the signal drops back below 0dB, then a 500ms
        // linear fade to fully off) - driven every timerCallback() tick
        // from the RAW instantaneous targetDb (not the ballistic-smoothed
        // dial reading), matching how a real peak LED reacts to
        // instantaneous transients rather than the VU coil's slow average.
        float ledHoldRemainingSeconds = 0.0f;
        float ledAlpha = 0.0f;
        static constexpr float ledHoldSeconds = 0.2f;
        static constexpr float ledFadeSeconds = 0.5f;

        // Needle draw size, as a fraction of the component's own full size
        // - decoupled from the face-fit scale (unlike v0.3.2, where the
        // needle was stretched to fill the WHOLE component because the
        // component was deliberately sized to match the needle's own reach
        // exactly). Tuned so the needle tip lands on the face asset's own
        // tick arc (see PluginEditor.cpp's docs for the measurement this
        // was derived from).
        static constexpr float needleSizeFraction = 0.84f;

        // Flicker: sum of three low-frequency sine layers at deliberately
        // non-harmonic frequencies (see Flicker.h) - amplitudeFraction is
        // the peak deviation from the base alpha values above (+-4%, within
        // Yves' +-3-5% brief). flickerPhaseSeed offsets each sine layer's
        // phase per-instance so two AnalogMeters never flicker in lockstep.
        float flickerPhaseSeed = 0.0f;
        double startTimeSeconds = 0.0;
        static constexpr float flickerAmplitudeFraction = 0.04f;

        static constexpr double timerHz = 30.0;
        static constexpr float ballisticsTauSeconds = 0.3f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalogMeter)
    };
}
