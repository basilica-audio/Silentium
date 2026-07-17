#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-reusable rotary knob backed by a pre-rendered Blender filmstrip (see
// .scaffold/gui-assets/knob-brass-v1/README.md): a single tall PNG stacking
// N frames of a knob rotating from -135deg to +135deg, frame 0 at the TOP.
// The slider's normalised value selects a frame; the knob artwork itself is
// never rotated/transformed at runtime (unlike AnalogMeter's needle) - every
// angle is a distinct, individually lit/shadowed Blender render, which is
// what makes the filmstrip approach photoreal in the first place.
namespace basilica::gui
{
    class FilmstripKnob : public juce::Slider
    {
    public:
        // strip1x/strip2x: vertical filmstrips, frame 0 at the top, both
        // holding exactly numFrames frames. Either may be an invalid/default
        // Image if that density tier isn't available for this asset - see
        // ImageDensity.h's fallback behaviour.
        FilmstripKnob (juce::Image strip1x, juce::Image strip2x, int numFrames);
        ~FilmstripKnob() override;

        void paint (juce::Graphics& g) override;

        // Shift = fine adjustment: overridden purely to retune
        // setMouseDragSensitivity() per the current modifier state before
        // forwarding to the base Slider implementation, which reads that
        // sensitivity live on every drag event (JUCE 8.0.14,
        // juce::Slider::Pimpl::pixelsForFullDragExtent) - not a full custom
        // drag implementation.
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;

        // Exposed for unit testing (see tests/gui/FilmstripFrameMathTests.cpp)
        // and used internally by paint(). Pure, side-effect-free: clamps
        // normalisedValue to [0, 1] before scaling, so an out-of-range caller
        // (shouldn't happen via Slider's own normalisation, but defensive)
        // still lands on a valid frame instead of reading out of bounds.
        static int frameIndexForValue (double normalisedValue, int numFrames) noexcept;

    private:
        const juce::Image& imageForCurrentWidth() const noexcept;

        juce::Image strip1x, strip2x;
        int numFrames = 1;

        // Base (non-fine) and Shift-fine mouse-drag sensitivities, in pixels
        // of vertical drag for a full-range sweep - see setMouseDragSensitivity().
        // The fine value is 8x the base, matching the "hold Shift for finer
        // control" convention used across the suite's other stepped controls.
        static constexpr int normalDragSensitivity = 200;
        static constexpr int fineDragSensitivity = normalDragSensitivity * 8;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilmstripKnob)
    };
}
