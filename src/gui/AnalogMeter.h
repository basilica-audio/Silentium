#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>

// Suite-reusable analog-style VU meter: draws an incandescent pilot-lamp
// glow and the rotating needle, composited live on top of the baked dial
// face already present in Silentium's faceplate background (see
// PluginEditor.cpp). Also OWNS the peak-hold/fade state machine for the
// peak LED (it owns the dB data), even though - as of v0.3.6 - the LED's own
// IMAGE DRAW lives in PluginEditor instead (see peakLedAlpha() below and
// this header's Assets docs for why).
//
// v0.3.4: MASTER-05 BASELINE ARCHITECTURE, per Yves' final art direction,
// superseding v0.3.3's "true component assembly" (every visual element as
// its own standalone master-reference asset). master-05 bakes BOTH VU dial
// faces directly into the single faceplate image (ticks, "VU" wordmark, red
// zone, hub/anchor bar, brass bezel - everything except the needle and the
// peak LED, which stay live overlays) - so this component no longer owns or
// draws a face image at all. The pivot fraction remains a configurable
// constructor parameter (the baked face's own hub does not sit at the exact
// centre of the component's bounds - measured ~47.8%/66.6% across/down, see
// PluginEditorLayout.h's meterPivotXFraction/meterPivotYFraction docs) -
// PluginEditorLayout.h/PluginEditor.cpp compute this component's bounds
// directly from the master-05 measurements so the needle/glow land correctly
// on the already-baked dial.
//
// v0.3.6: the peak LED moved OUT of this component's own draw entirely.
// Per Yves' master-03 reference the LED is a SMALL lamp sitting ON THE
// PLATE, outside each dial's bezel (upper-left) - not inside the dial face
// this component's own bounds cover. A prior revision incorrectly drew a
// large LED inside the dial over the tick scale; PluginEditor now owns the
// LED asset + draw call at the correct plate-level position (see
// PluginEditorLayout.h's ledLCentre1x/ledRCentre1x), reading this
// component's own ledAlpha via peakLedAlpha() each tick.
//
// v0.3.11: the needle is drawn from a SINGLE master-extracted sprite
// (needle-from-master.png), rotated live via juce::AffineTransform, not a
// pre-rendered 96-frame filmstrip - Yves rejected the filmstrip's own
// source TWICE, both times because it had been modelled in Blender rather
// than recovered from the approved master render ("Du sollst weder mit
// Blender arbeiten noch sonst wie selbst was an der Nadel zeichnen. Die
// soll exakt so aussehen wie auf dem Master."). See AnalogMeter.cpp's
// bakedAngleDeg docs for why this sprite is rotated live instead of shipped
// as pre-rotated frames (the extraction deliberately did NOT rotate the
// sprite to a canonical "straight up" pose, since resampling would soften
// the master's own pixels). The v0.3.7 hub-occluder redraw is gone too:
// the new sprite's own alpha is already transparent within ~43 master px
// of the pivot (its damping-ring occlusion boundary), wide enough that
// master-05's own baked hub-cap/anchor-bar/boss art shows through
// unmodified there without any separate overlay draw.
namespace basilica::gui
{
    class AnalogMeter : public juce::Component, private juce::Timer
    {
    public:
        struct Assets
        {
            // v0.3.11: a SINGLE master-extracted needle sprite
            // (needle-from-master.png, 288x288 RGBA, pivot at its own exact
            // canvas centre (0.5, 0.5)) - paint() rotates it live about
            // that centre with a juce::AffineTransform. Replaces the
            // v0.3.5-v0.3.7 pre-rotated 96-frame filmstrip (a Blender
            // render, rejected - see this header's top-of-file docs) and
            // its v0.3.7 hub-occluder companion image (no longer needed,
            // see those same docs).
            juce::Image needle;

            // v0.3.6: the peak-LED IMAGE is no longer owned/drawn here - per
            // Yves' master-03 reference, the LED sits on the PLATE outside
            // this component's own dial-face bounds (upper-left of the
            // bezel), so PluginEditor now owns the LED asset + draw call
            // (see PluginEditor.cpp's paint()) while this component still
            // owns the peak-hold/fade STATE MACHINE (it owns the dB data -
            // see peakLedAlpha() below).
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

        // Test/preview-only (item 5 proof): sets this meter's own
        // flicker/glow "clock" (startTimeSeconds, the anchor
        // currentFlickerMultiplier() measures elapsed time FROM) so that
        // "elapsed time" at the NEXT paint() call reads as (very close to)
        // elapsedSeconds, regardless of real wall-clock time at the moment
        // this is called - i.e. an ABSOLUTE simulated position in the
        // flicker's own multi-sine cycle, not a relative shift. This is
        // what makes tests/gui/EditorSnapshotTests.cpp's item 5 proof fully
        // reproducible: neither a real wall-clock sleep NOR a relative
        // clock offset can guarantee a large delta between two captured
        // frames (the underlying flicker is a weighted sum of several
        // non-harmonic sine layers - not monotonic, so either approach can
        // land on a near-zero delta purely by chance, depending on
        // whatever real "now" happens to be when the test runs); calling
        // this with two independently pre-verified elapsedSeconds values
        // removes that dependency entirely. Repaints immediately. Normal
        // operation never calls this.
        void setFlickerElapsedSecondsForPreview (double elapsedSeconds) noexcept;

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

        // v0.3.6: the peak-hold/fade state machine (timerCallback()/
        // setImmediateDbForPreview()) still lives here (it owns the dB
        // data), but the LED image draw moved to PluginEditor (see this
        // component's own Assets docs above) - this getter is how the
        // editor reads the current alpha each paint() to draw its own LED
        // overlay in sync with this meter's peak state.
        float peakLedAlpha() const noexcept { return ledAlpha; }

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

        // v0.3.11: needle draw size, as a fraction of the component's own
        // full size - derived (not hand-tuned) from the sprite's own native
        // 288x288 master-px canvas scaled by the SAME master->@1x factor
        // every other master-derived asset in this suite uses
        // (plateWidth1x / masterCanvasWidthPx = 900/1264), then expressed
        // as a fraction of AnalogMeter's own @1x component size
        // (PluginEditorLayout.h's meterComponentSize1x = 255): 288 *
        // (900/1264) / 255 = 0.804170 - see AnalogMeter.cpp's
        // needleSpriteToOneXScale docs for the exact arithmetic (this
        // constant deliberately does NOT depend on PluginEditorLayout.h,
        // to keep this component suite-reusable - see this file's
        // top-of-file docs).
        static constexpr float needleSizeFraction = 0.804170f;

        // Flicker: sum of three low-frequency sine layers at deliberately
        // non-harmonic frequencies (see Flicker.h) - amplitudeFraction is
        // the peak deviation from the base alpha values above.
        // flickerPhaseSeed offsets each sine layer's phase per-instance so
        // two AnalogMeters never flicker in lockstep.
        //
        // v0.3.9 (item 5 fix): raised from the original +-4% (Yves' initial
        // +-3-5% brief) to +-7% - the smaller amplitude read as essentially
        // invisible at idle against the dial's own dark plate colour ("es
        // flackert auch nix", Yves' explicit complaint). A SECOND, much
        // slower drift layer (flickerDriftAmplitudeFraction,
        // Flicker.h's own slowDriftLayers table - periods ~7.5-32s, an
        // order of magnitude slower than the primary layers' ~0.9-3.4s) is
        // now ADDED on top in currentFlickerMultiplier(), giving the glow a
        // genuine two-time-scale "incandescent lamp" character (a slow
        // thermal-looking drift plus faster micro-flicker) rather than one
        // uniform sine mix - see that method's docs.
        //
        // v0.3.10 (final sign-off pass, "etwas deutlicher"/"a bit more
        // noticeable" - Yves): both amplitudes raised again, +-7%->+-10%
        // (fast layer) and +-3%->+-5% (slow drift layer), still clearly
        // below strobing and consistent with the incandescent character -
        // see tests/gui/EditorSnapshotTests.cpp's item 5 flicker-diff proof,
        // regenerated against these values.
        float flickerPhaseSeed = 0.0f;
        double startTimeSeconds = 0.0;
        static constexpr float flickerAmplitudeFraction = 0.10f;
        static constexpr float flickerDriftAmplitudeFraction = 0.05f;

        // Phase offset applied on top of flickerPhaseSeed for the slow
        // drift layer only, so it never shares the exact same per-layer
        // phase as the fast layer even when both instances' seeds are
        // small integers (0.0f/1.0f) - an arbitrary irrational-feeling
        // constant, same convention Flicker.h's own flickerMultiplier()
        // uses for its per-layer offsets.
        static constexpr float flickerDriftPhaseOffset = 9.1f;

        static constexpr double timerHz = 30.0;
        static constexpr float ballisticsTauSeconds = 0.3f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalogMeter)
    };
}
