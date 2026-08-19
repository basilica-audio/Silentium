#pragma once

#include "BasilicaLookAndFeel.h"
#include "KeyboardSteps.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The editor's transparent knob hit-surface (see PluginEditor.h: since
// v0.3.4 the knob artwork is baked into the master plate render and each
// knob is an invisible juce::Slider laid over it), upgraded from a plain
// juce::Slider for keyboard operability (issue #5 / M3 a11y review
// follow-ups A-09/A-10, WCAG 2.1.1 + 2.4.7):
//
//   - juce::Slider::init() ships with setWantsKeyboardFocus(false) in JUCE
//     8.0.14 (juce_Slider.cpp:1461), so a plain Slider is unreachable by
//     Tab and its keyPressed() never fires. The constructor here opts back
//     in, putting every knob into the editor's focus traversal (which
//     follows child creation order - see the PluginEditor constructor).
//
//   - keyPressed() adds WAI-ARIA-style stepping (Arrow 1%, Shift+Arrow
//     fine, PageUp/Down 10%, Home/End extremes) via handleSliderKeyPress()
//     - see KeyboardSteps.h for why the base-class behaviour is unusable
//     with this suite's finely-quantised parameter ranges.
//
//   - paint() draws the suite's shared focus ring (A-01 pattern,
//     BasilicaLookAndFeel::paintFocusRing) when the knob holds keyboard
//     focus: the control is otherwise fully transparent (all three rotary
//     colour IDs are transparentBlack, so LookAndFeel_V4::drawRotarySlider
//     paints nothing visible), which would leave keyboard focus with no
//     visible indicator at all (WCAG 2.4.7 Focus Visible).
namespace basilica::gui
{
    class KnobSlider : public juce::Slider
    {
    public:
        KnobSlider (juce::Slider::SliderStyle style, juce::Slider::TextEntryBoxPosition textBoxPosition)
            : juce::Slider (style, textBoxPosition)
        {
            setWantsKeyboardFocus (true);
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            return handleSliderKeyPress (*this, key) || juce::Slider::keyPressed (key);
        }

        void paint (juce::Graphics& g) override
        {
            juce::Slider::paint (g);

            if (hasKeyboardFocus (true))
                paintFocusRing (g, getLocalBounds().toFloat(), FocusRingShape::ellipse);
        }
    };
}
