#pragma once

#include "BasilicaLookAndFeel.h"
#include "ImageDensity.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-reusable two-position switch backed by a 4-frame Blender filmstrip
// (the toggle-brass-v2 render family, re-embedded for issue #33's aux
// control bay): off / on / off+hover / on+hover, frame 0 at the top of the
// strip - the SAME verified frame table the retired FilmstripToggle used
// (removed as dead code in #37; its README's prose formula "state * 2 +
// hovered" does not reproduce its own table, see frameIndexFor()).
//
// Unlike that retired class (a plain juce::Button), this derives from
// juce::ToggleButton so the stock button AccessibilityHandler exposes a
// checkable/checked state out of the box (same a11y contract the editor's
// invisible plate toggles already rely on - see
// tests/gui/EditorAccessibilityTests.cpp), and juce::Button's constructor
// opts into keyboard focus/Space-Return activation by itself.
//
// For two-entry AudioParameterChoice parameters (Detector, SC Slope,
// Release Shape) the switch doubles as the choice control:
// juce::AudioProcessorValueTreeState::ButtonAttachment maps toggle state
// exactly onto choice index 0/1 (JUCE 8.0.14
// juce_ParameterAttachments.cpp:266, setValue -> setToggleState
// (newValue >= 0.5f, sendNotificationSync)). setOptionLabels() feeds the
// two choice names into the component's accessibility description, kept
// current from buttonStateChanged() - which JUCE 8.0.14 guarantees to run
// on EVERY toggle-state change, even dontSendNotification ones
// (juce_Button.cpp:209-212's else-branch), so the description can never go
// stale regardless of how the state was set (mouse, keyboard, host
// automation via the attachment, or a test).
namespace basilica::gui
{
    class FilmstripSwitch : public juce::ToggleButton
    {
    public:
        // strip1x/strip2x: vertical 4-frame filmstrips, frame 0 at the top.
        // Either may be an invalid/default Image if that density tier isn't
        // available - see ImageDensity.h's fallback behaviour.
        FilmstripSwitch (const juce::String& buttonName, juce::Image strip1xIn, juce::Image strip2xIn)
            : juce::ToggleButton (buttonName),
              strip1x (std::move (strip1xIn)),
              strip2x (std::move (strip2xIn))
        {
        }

        // The human-readable names of the two switch positions (off = choice
        // index 0, on = choice index 1) - fed into the accessibility
        // description so a screen reader announces the CURRENT option name
        // ("RMS"), not just an anonymous checked/unchecked state. Call after
        // any programmatic initial state is applied (e.g. right after the
        // APVTS ButtonAttachment is constructed).
        void setOptionLabels (const juce::String& offLabel, const juce::String& onLabel)
        {
            optionOffLabel = offLabel;
            optionOnLabel = onLabel;
            refreshDescription();
        }

        const juce::String& getOptionLabel (bool forOnState) const noexcept
        {
            return forOnState ? optionOnLabel : optionOffLabel;
        }

        // Frame index for a given (hovered, isOn) state pair - the retired
        // FilmstripToggle's own verified mapping (exposed for unit testing,
        // see tests/gui/FilmstripControlTests.cpp):
        //   0 = off,          1 = on,
        //   2 = off + hover,  3 = on + hover
        static int frameIndexFor (bool isOn, bool isHovered) noexcept
        {
            return (isHovered ? 2 : 0) + (isOn ? 1 : 0);
        }

        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/) override
        {
            const auto& strip = imageForCurrentWidth();

            if (strip.isValid())
            {
                // The asset only encodes 4 states (no separate "pressed"
                // frame); a brief mouse-down is visually absorbed by the
                // hover frame until the click registers and the toggle state
                // itself flips.
                const auto frameIndex = frameIndexFor (getToggleState(), shouldDrawButtonAsHighlighted);
                const auto frameHeight = strip.getHeight() / numFrames;

                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (strip,
                             0, 0, getWidth(), getHeight(),
                             0, frameIndex * frameHeight, strip.getWidth(), frameHeight);
            }

            // A-01 pattern (WCAG 2.4.7 Focus Visible): this paintButton()
            // override fully replaces the LookAndFeel toggle drawing, so
            // nothing else in the render path ever draws a keyboard-focus
            // indicator - see BasilicaLookAndFeel.h's paintFocusRing() docs.
            if (hasKeyboardFocus (true))
                paintFocusRing (g, getLocalBounds().toFloat(), FocusRingShape::roundedRectangle);
        }

        // Fires on EVERY button state change including dontSendNotification
        // toggle-state sets (see this class's top-of-file docs) - keeps the
        // accessibility description current and lets the owning editor
        // repaint any legend text it draws next to this switch.
        void buttonStateChanged() override
        {
            juce::ToggleButton::buttonStateChanged();
            refreshDescription();

            if (onToggleVisualChange != nullptr)
                onToggleVisualChange();
        }

        // Owner hook for repainting editor-drawn position legends around
        // this switch - distinct from juce::Button::onStateChange, which
        // does NOT fire for dontSendNotification state changes.
        std::function<void()> onToggleVisualChange;

    private:
        void refreshDescription()
        {
            if (optionOffLabel.isNotEmpty() || optionOnLabel.isNotEmpty())
                setDescription (getToggleState() ? optionOnLabel : optionOffLabel);
        }

        const juce::Image& imageForCurrentWidth() const noexcept
        {
            const auto native1xWidth = strip1x.isValid() ? strip1x.getWidth()
                                                         : (strip2x.isValid() ? strip2x.getWidth() / 2 : 1);
            return basilica::gui::pickImageForWidth (strip1x, strip2x, native1xWidth, getWidth());
        }

        juce::Image strip1x, strip2x;
        juce::String optionOffLabel, optionOnLabel;
        static constexpr int numFrames = 4;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilmstripSwitch)
    };
}
