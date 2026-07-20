#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-reusable rotary knob backed by a SINGLE photoreal master-reference
// image (rather than FilmstripKnob's pre-rendered Blender filmstrip of many
// discrete rotated frames): the image itself is rotated live via
// juce::AffineTransform every paint(), the same technique AnalogMeter
// already uses for its needle.
//
// v0.3.3: introduced alongside the faceplate-silentium-v3 "master-04"
// component-assembly GUI rework specifically because no filmstrip render
// exists yet for the new knob-v4.png asset (only FilmstripKnob's older
// knob_brass_v2_strip_*.png family has one, from a visually different
// render generation) - reusing that older filmstrip would have reintroduced
// exactly the render-generation mismatch ("Frankenstein" look) Yves
// rejected in earlier iterations. Single-image rotation is the documented
// trade-off: a true multi-frame Blender filmstrip re-lights each frame's
// specular highlight individually as the knob turns, which this can't
// replicate (the highlight rotates rigidly WITH the knob instead of staying
// anchored to the scene's own light source) - acceptable for a brushed-
// metal knob with a fairly diffuse highlight, flagged here for a possible
// future filmstrip re-render.
namespace basilica::gui
{
    class RotatingImageKnob : public juce::Slider
    {
    public:
        // knobImage: the master-ref knob asset (already alpha-cut to a
        // clean circular disc - see PluginEditor.cpp's asset-loading docs),
        // drawn filling this component's own bounds every paint() at a
        // rotation matching the current value. contentDiameterFraction is
        // the disc's own diameter as a fraction of the full square image
        // canvas (the asset may have a small margin around the disc) - used
        // so this component's bounds can be sized to the disc's true
        // on-screen diameter rather than the image's raw canvas size.
        RotatingImageKnob (juce::Image knobImage, float contentDiameterFraction);
        ~RotatingImageKnob() override;

        void paint (juce::Graphics& g) override;

        // Shift = fine adjustment: overridden purely to retune
        // setMouseDragSensitivity() per the current modifier state before
        // forwarding to the base Slider implementation, which reads that
        // sensitivity live on every drag event (JUCE 8.0.14,
        // juce::Slider::Pimpl::pixelsForFullDragExtent) - not a full custom
        // drag implementation. Identical pattern to FilmstripKnob.
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;

        // dB/normalised-proportion -> absolute rotation in degrees,
        // clockwise from straight up - exposed for unit testing. Matches
        // the same -135..+135deg full sweep FilmstripKnob's rotaryParameters
        // use, so the two components feel identical to drag even though
        // this one doesn't call setRotaryParameters() itself (the angle is
        // computed directly rather than read back from JUCE's internal
        // rotary state, since juce::Slider exposes no public getter for the
        // interpolated angle at the current value).
        static float angleForProportionDegrees (double normalisedValue) noexcept;

    private:
        const juce::Image& imageForCurrentWidth() const noexcept { return knobImage; }

        juce::Image knobImage;
        float contentDiameterFraction = 1.0f;

        static constexpr float minAngleDeg = -135.0f;
        static constexpr float maxAngleDeg = 135.0f;

        static constexpr int normalDragSensitivity = 200;
        static constexpr int fineDragSensitivity = normalDragSensitivity * 8;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RotatingImageKnob)
    };
}
