#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>

// Suite-reusable analog-style meter: a static face + needle image rotated
// live via juce::AffineTransform around the face's baked pivot rivet - both
// pre-rendered assets from the nano-banana-approved vu-nano-v1 family (see
// .scaffold/gui-assets/vu-nano-v1/README.md, promoted here from Silentium as
// the reusable Basilica Audio VU component, v0.3.2). Unlike the earlier
// vu-brass-v1/vu-dome-v1 families, vu-nano-v1 ships a SINGLE 1024x1024 tier
// per layer (no @1x/@2x pair, no separate glass decal - the face's own baked
// highlight carries that read) and the needle is authored at rest pointing
// STRAIGHT UP (0 deg / 12 o'clock), so JUCE applies the measured dB->angle
// value directly as the rotation - no "rest angle" subtraction.
namespace basilica::gui
{
    class AnalogMeter : public juce::Component, private juce::Timer
    {
    public:
        struct Assets
        {
            juce::Image face;
            juce::Image needle;
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
        // interpolated across the asset's own measured tick table (see the
        // .cpp - copied verbatim from .scaffold/gui-assets/vu-nano-v1/
        // vu-metadata.json's tick_angle_at_db, the ground truth for where
        // this face's engraved arc ticks actually sit, measured by
        // analyze_face.py) and clamped beyond the table's ends. Exposed for
        // unit testing. Degrees are clockwise from straight-up (12 o'clock) -
        // this IS the needle's absolute rotation angle (the needle asset's
        // own rest pose is 0 deg / straight up), unlike vu-dome-v1's table
        // which needed a rest-angle delta subtracted first.
        static float tickAngleDegreesForDb (float db) noexcept;

        // vu-nano-v1's visible dial (bezel outer edge) spans face_diameter_px
        // / canvas_size_px = 808.5 / 1024 of the rendered canvas
        // (vu-metadata.json), with a transparent margin around it (see
        // .scaffold/gui-assets/vu-nano-v1/mask_face.py - the approved face
        // render is fully OPAQUE with a baked black background; mask_face.py
        // derives the shippable transparent-margin PNG this component
        // actually draws, matching this component's own bay-overhang
        // convention). A layout that wants the visible dial to fill a given
        // rectangle must size this component 1/contentFractionOfCanvas
        // larger than that rectangle, centred on it (the margin is
        // transparent and this component never intercepts mouse events, so
        // the overhang is harmless). See SilentiumAudioProcessorEditor::resized().
        static constexpr float contentFractionOfCanvas = 808.5f / 1024.0f;

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

        Assets assets;
        juce::String title;

        std::atomic<float> targetDb { -100.0f };
        float smoothedDb = -100.0f;

        // Needle pivot as a fraction of the layer canvas - measured directly
        // on vu-face-no-needle.png by analyze_face.py (brass-hub centroid,
        // HSV hue/saturation thresholding), see vu-metadata.json's
        // pivot_frac_xy. MUST stay in sync with the face/needle assets: if
        // either is re-rendered, re-run analyze_face.py and update both this
        // pair and the tick table together.
        static constexpr float pivotXFraction = 0.499838f;
        static constexpr float pivotYFraction = 0.706359f;

        static constexpr double timerHz = 30.0;
        static constexpr float ballisticsTauSeconds = 0.3f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalogMeter)
    };
}
