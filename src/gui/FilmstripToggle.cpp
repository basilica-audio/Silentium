#include "FilmstripToggle.h"
#include "BasilicaLookAndFeel.h"
#include "ImageDensity.h"

namespace basilica::gui
{
    FilmstripToggle::FilmstripToggle (const juce::String& buttonName, juce::Image strip1xIn, juce::Image strip2xIn)
        : juce::Button (buttonName),
          strip1x (std::move (strip1xIn)),
          strip2x (std::move (strip2xIn))
    {
        // A click flips getToggleState(); AudioProcessorValueTreeState::
        // ButtonAttachment (see PluginEditor.cpp) only requires the Button
        // base-class toggle interface, so this works identically to
        // juce::ToggleButton from the attachment's point of view.
        setClickingTogglesState (true);
    }

    FilmstripToggle::~FilmstripToggle() = default;

    int FilmstripToggle::frameIndexFor (bool isOn, bool isHovered) noexcept
    {
        return (isHovered ? 2 : 0) + (isOn ? 1 : 0);
    }

    const juce::Image& FilmstripToggle::imageForCurrentWidth() const noexcept
    {
        const auto native1xWidth = strip1x.isValid() ? strip1x.getWidth() : (strip2x.getWidth() / 2);
        return basilica::gui::pickImageForWidth (strip1x, strip2x, native1xWidth, getWidth());
    }

    void FilmstripToggle::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/)
    {
        const auto& strip = imageForCurrentWidth();

        if (! strip.isValid())
            return;

        // The asset only encodes 4 states (no separate "pressed" frame -
        // see the README's design notes); a brief mouse-down is visually
        // absorbed by the hover frame until the click registers and the
        // toggle state itself flips.
        const auto frameIndex = frameIndexFor (getToggleState(), shouldDrawButtonAsHighlighted);
        const auto frameHeight = strip.getHeight() / numFrames;

        g.drawImage (strip,
                     0, 0, getWidth(), getHeight(),
                     0, frameIndex * frameHeight, strip.getWidth(), frameHeight);

        // A-01 fix (WCAG 2.4.7 Focus Visible): this paintButton() override
        // fully replaces juce::Button's default drawing, so nothing else in
        // the render path ever draws a keyboard-focus indicator - see
        // BasilicaLookAndFeel.h's paintFocusRing() docs.
        if (hasKeyboardFocus (true))
            paintFocusRing (g, getLocalBounds().toFloat(), FocusRingShape::roundedRectangle);
    }
}
