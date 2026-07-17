#include "FilmstripKnob.h"
#include "BasilicaLookAndFeel.h"
#include "ImageDensity.h"

namespace basilica::gui
{
    FilmstripKnob::FilmstripKnob (juce::Image strip1xIn, juce::Image strip2xIn, int numFramesIn)
        : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox),
          strip1x (std::move (strip1xIn)),
          strip2x (std::move (strip2xIn)),
          numFrames (juce::jmax (1, numFramesIn))
    {
        // Rotation extent must match the asset's own -135..+135deg render
        // sweep (knob-brass-v1/README.md) so the frame the mouse position
        // maps to always matches the frame actually drawn.
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                             juce::MathConstants<float>::pi * 2.75f,
                             true);

        setMouseDragSensitivity (normalDragSensitivity);
        setScrollWheelEnabled (true);

        // No built-in JUCE text box - values are shown via the suite's
        // separate JUCE-drawn label pass (BasilicaLookAndFeel), see
        // PluginEditor's layout table.
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    }

    FilmstripKnob::~FilmstripKnob() = default;

    int FilmstripKnob::frameIndexForValue (double normalisedValue, int numFramesIn) noexcept
    {
        const auto clamped = juce::jlimit (0.0, 1.0, normalisedValue);
        return juce::jlimit (0, numFramesIn - 1, (int) std::lround (clamped * (double) (numFramesIn - 1)));
    }

    const juce::Image& FilmstripKnob::imageForCurrentWidth() const noexcept
    {
        const auto native1xWidth = strip1x.isValid() ? strip1x.getWidth() : (strip2x.getWidth() / 2);
        return basilica::gui::pickImageForWidth (strip1x, strip2x, native1xWidth, getWidth());
    }

    void FilmstripKnob::paint (juce::Graphics& g)
    {
        const auto& strip = imageForCurrentWidth();

        if (! strip.isValid())
            return;

        const auto frameHeight = strip.getHeight() / numFrames;
        const auto frameIndex = frameIndexForValue (valueToProportionOfLength (getValue()), numFrames);

        g.drawImage (strip,
                     0, 0, getWidth(), getHeight(),
                     0, frameIndex * frameHeight, strip.getWidth(), frameHeight);

        // A-01 fix (WCAG 2.4.7 Focus Visible): this paint() override fully
        // replaces juce::Slider::paint(), so nothing else in the render path
        // ever draws a keyboard-focus indicator - see BasilicaLookAndFeel.h's
        // paintFocusRing() docs.
        if (hasKeyboardFocus (true))
            paintFocusRing (g, getLocalBounds().toFloat(), FocusRingShape::ellipse);
    }

    void FilmstripKnob::mouseDown (const juce::MouseEvent& e)
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? fineDragSensitivity : normalDragSensitivity);
        Slider::mouseDown (e);
    }

    void FilmstripKnob::mouseDrag (const juce::MouseEvent& e)
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? fineDragSensitivity : normalDragSensitivity);
        Slider::mouseDrag (e);
    }
}
