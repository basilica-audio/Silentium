#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-reusable keyboard stepping for sliders (M3 a11y review follow-ups
// A-09 + A-10, WCAG 2.1.1 Keyboard).
//
// JUCE 8.0.14's own juce::Slider::Pimpl::keyPressed (juce_Slider.cpp:1029)
// has two gaps this helper closes:
//
//   1. It returns false the moment ANY modifier key is down, so Shift+Arrow
//      does nothing at all - there is no keyboard analog to the suite's
//      Shift-drag fine-adjustment mouse mode (A-09).
//
//   2. Its step size is the parameter's raw NormalisableRange interval
//      (via the accessibility value interface's range, or getStepSize()'s
//      identical fallback - both resolve to slider.getInterval() when an
//      interval is set). Suite parameters declare intervals as VALUE
//      QUANTISATION (e.g. Threshold: 0.01 dB over an 80 dB range), so a
//      plain arrow press would need thousands of repeats to sweep the
//      range (A-10).
//
// This helper follows the WAI-ARIA slider pattern instead: Arrow = 1% of
// the range, Shift+Arrow = 0.1% (fine), PageUp/PageDown = 10%, Home/End =
// range extremes. Steps are taken in the slider's PROPORTIONAL domain
// (valueToProportionOfLength / proportionOfLengthToValue) so skewed and
// logarithmic ranges sweep perceptually uniformly, exactly like a mouse
// drag does. Slider::setValue() then snaps the result to the parameter's
// legal interval grid (Pimpl::constrainedValue -> NormalisableRange::
// snapToLegalValue, JUCE 8.0.14), so quantisation is never violated; if
// that snap would collapse a fine step to zero, the helper falls back to
// exactly one interval in the intended direction so a key press is never
// silently swallowed.
namespace basilica::gui
{
    // Handles arrow/page/home/end key presses for a single-value slider.
    // Returns true if the key was consumed (the slider's value was set).
    // Call from a juce::Slider subclass's keyPressed() BEFORE deferring to
    // the base class.
    inline bool handleSliderKeyPress (juce::Slider& slider, const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
        const bool shiftOnly = mods.isShiftDown()
                               && ! mods.isCtrlDown()
                               && ! mods.isAltDown()
                               && ! mods.isCommandDown();

        // Ctrl/Alt/Cmd combinations are host shortcuts - never consume them.
        if (mods.isAnyModifierKeyDown() && ! shiftOnly)
            return false;

        const auto range = slider.getRange();

        if (! (range.getLength() > 0.0) || slider.isTwoValue() || slider.isThreeValue())
            return false;

        const auto keyCode = key.getKeyCode();

        if (keyCode == juce::KeyPress::homeKey)
        {
            slider.setValue (range.getStart(), juce::sendNotificationSync);
            return true;
        }

        if (keyCode == juce::KeyPress::endKey)
        {
            slider.setValue (range.getEnd(), juce::sendNotificationSync);
            return true;
        }

        const bool isIncrement = keyCode == juce::KeyPress::rightKey || keyCode == juce::KeyPress::upKey;
        const bool isDecrement = keyCode == juce::KeyPress::leftKey || keyCode == juce::KeyPress::downKey;
        const bool isPageUp = keyCode == juce::KeyPress::pageUpKey;
        const bool isPageDown = keyCode == juce::KeyPress::pageDownKey;

        if (! (isIncrement || isDecrement || isPageUp || isPageDown))
            return false;

        const double proportionStep = (isPageUp || isPageDown) ? 0.1
                                    : shiftOnly                ? 0.001
                                                               : 0.01;
        const double direction = (isIncrement || isPageUp) ? 1.0 : -1.0;

        const auto currentValue = slider.getValue();
        const auto currentProportion = slider.valueToProportionOfLength (currentValue);
        const auto targetProportion = juce::jlimit (0.0, 1.0, currentProportion + direction * proportionStep);

        slider.setValue (slider.proportionOfLengthToValue (targetProportion), juce::sendNotificationSync);

        // Interval snapping collapsed the step (fine step finer than the
        // parameter's own quantisation): take exactly one interval instead.
        if (juce::approximatelyEqual (slider.getValue(), currentValue)
            && slider.getInterval() > 0.0
            && ! juce::approximatelyEqual (targetProportion, currentProportion))
        {
            slider.setValue (currentValue + direction * slider.getInterval(), juce::sendNotificationSync);
        }

        return true;
    }
}
