#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>

// Suite-reusable analog-style meter: a static face + glass overlay
// (pre-rendered Blender PNGs, see .scaffold/gui-assets/vu-brass-v1/README.md)
// with a needle image rotated live via juce::AffineTransform around the
// face's baked pivot rivet. The needle image is rendered once, at rest,
// pointing at the face's lowest scale tick - JUCE only ever applies an
// ADDITIONAL rotation delta on top of that baked rest orientation, so at the
// lowest tick the needle draws with zero extra rotation and lands exactly on
// the rivet mark the asset was authored against.
namespace basilica::gui
{
    class AnalogMeter : public juce::Component, private juce::Timer
    {
    public:
        struct Assets
        {
            juce::Image face1x, face2x;
            juce::Image needle1x, needle2x;
            juce::Image glass1x, glass2x;
        };

        AnalogMeter (Assets assetsIn, juce::String accessibleTitle);
        ~AnalogMeter() override;

        // Thread-safe (plain atomic store): the instantaneous value in dB,
        // written from the audio thread every processBlock() call. Ballistic
        // smoothing is applied separately, on the GUI thread's timer - never
        // here, so this is real-time safe to call from anywhere.
        void setTargetDb (float newTargetDb) noexcept { targetDb.store (newTargetDb, std::memory_order_relaxed); }

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
        // interpolated across the asset's own baked tick table (see the .cpp
        // - copied verbatim from render_vu_meter.py's TICKS, the ground
        // truth for where the engraved arc's ticks actually sit) and clamped
        // beyond the table's ends. Exposed for unit testing.
        static float tickAngleDegreesForDb (float db) noexcept;

        // vu-brass-v1's dial content (the cream face plane) occupies exactly
        // the CENTRAL HALF of the rendered canvas in both axes - the plane
        // is 1.6x0.9 world units on a 3.2x1.8 canvas (render_vu_meter.py's
        // build_face()/build_glass() plane scale vs ortho_scale 3.2) - with
        // fully transparent margins around it. A layout that wants the
        // visible dial to fill a given rectangle must therefore size this
        // component 1/contentFractionOfCanvas (= 2x) larger than that
        // rectangle, centred on it (the margins are transparent and this
        // component never intercepts mouse events, so the overhang is
        // harmless). See SilentiumAudioProcessorEditor::resized().
        static constexpr float contentFractionOfCanvas = 0.5f;

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
        const juce::Image& faceForCurrentWidth() const noexcept;
        const juce::Image& needleForCurrentWidth() const noexcept;
        const juce::Image& glassForCurrentWidth() const noexcept;

        Assets assets;
        juce::String title;

        std::atomic<float> targetDb { -100.0f };
        float smoothedDb = -100.0f;

        // Needle pivot as a fraction of the layer canvas, derived from
        // render_vu_meter.py's own scene: PIVOT world (0, -0.40) on a canvas
        // spanning world y [-0.9, 0.9] under the orthographic camera ->
        // fraction from the top = (0.9 + 0.40) / 1.8 = 13/18. VERIFIED
        // against the shipped pixels (ImageMagick alpha bounding box of
        // vu_brass_needle_480x270.png: 77x62+170+140, whose hub-disc centre
        // - hub radius 7.5px at this canvas size - lands at ~(239.5, 194.5),
        // i.e. fractions (0.499, 0.720)). NOTE: this deliberately does NOT
        // use vu-brass-v1/README.md's stated "pixel (0.5w, 0.86h)" - that
        // value does not match either the generator script's math or the
        // actual rendered pixels and appears to be an error in the README
        // (flagged in docs/gui-components.md for an upstream fix).
        static constexpr float pivotXFraction = 0.5f;
        static constexpr float pivotYFraction = 13.0f / 18.0f;

        static constexpr double timerHz = 30.0;
        static constexpr float ballisticsTauSeconds = 0.3f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalogMeter)
    };
}
