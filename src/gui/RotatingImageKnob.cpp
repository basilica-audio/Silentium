#include "RotatingImageKnob.h"
#include "BasilicaLookAndFeel.h"

namespace basilica::gui
{
    RotatingImageKnob::RotatingImageKnob (juce::Image knobImageIn, float contentDiameterFractionIn)
        : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox),
          knobImage (std::move (knobImageIn)),
          contentDiameterFraction (contentDiameterFractionIn > 0.0f ? contentDiameterFractionIn : 1.0f)
    {
        // Kept for correct mouse-drag angle mapping/feel even though paint()
        // computes its drawn angle directly (see angleForProportionDegrees())
        // rather than reading this back - the numeric range matches that
        // function's minAngleDeg/maxAngleDeg exactly (225deg=-135deg mod
        // 360, 495deg=135deg mod 360).
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

    RotatingImageKnob::~RotatingImageKnob() = default;

    float RotatingImageKnob::angleForProportionDegrees (double normalisedValue) noexcept
    {
        const auto clamped = juce::jlimit (0.0, 1.0, normalisedValue);
        return minAngleDeg + (float) clamped * (maxAngleDeg - minAngleDeg);
    }

    void RotatingImageKnob::paint (juce::Graphics& g)
    {
        const auto& image = imageForCurrentWidth();

        if (! image.isValid())
            return;

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        const auto bounds = getLocalBounds().toFloat();

        // The knob's own DISC content doesn't necessarily fill its whole
        // square canvas (see contentDiameterFraction's docs) - drawing the
        // full canvas at (drawnDiscDiameter / contentDiameterFraction) and
        // centring it on this component makes the disc itself land exactly
        // on bounds, with any canvas margin scaled proportionally outside
        // it (harmless: that margin is fully transparent).
        const auto canvasDrawSize = bounds.getWidth() / contentDiameterFraction;
        const auto imageHalfW = (float) image.getWidth() * 0.5f;
        const auto imageHalfH = (float) image.getHeight() * 0.5f;
        const auto scale = canvasDrawSize / (float) image.getWidth();

        const auto angleDeg = angleForProportionDegrees (valueToProportionOfLength (getValue()));
        const auto radians = juce::degreesToRadians (angleDeg);

        const auto transform = juce::AffineTransform::translation (-imageHalfW, -imageHalfH)
                                    .scaled (scale)
                                    .rotated (radians)
                                    .translated (bounds.getCentreX(), bounds.getCentreY());

        g.drawImageTransformed (image, transform);

        // A-01 fix parity with FilmstripKnob (WCAG 2.4.7 Focus Visible):
        // this paint() override fully replaces juce::Slider::paint(), so
        // nothing else in the render path ever draws a keyboard-focus
        // indicator.
        if (hasKeyboardFocus (true))
            paintFocusRing (g, getLocalBounds().toFloat(), FocusRingShape::ellipse);
    }

    void RotatingImageKnob::mouseDown (const juce::MouseEvent& e)
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? fineDragSensitivity : normalDragSensitivity);
        Slider::mouseDown (e);
    }

    void RotatingImageKnob::mouseDrag (const juce::MouseEvent& e)
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? fineDragSensitivity : normalDragSensitivity);
        Slider::mouseDrag (e);
    }
}
