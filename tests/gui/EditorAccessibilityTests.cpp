#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// M3 a11y review follow-up tests (A-01/A-02/A-05/A-07): assert the actual
// AccessibilityHandler-level behaviour the review flagged, not just that the
// editor constructs without crashing (EditorSnapshotTests.cpp already covers
// that). juce::ScopedJuceInitialiser_GUI is installed once for the whole
// test binary in tests/TestMain.cpp, so constructing Components is safe here
// even though this is a headless console executable with no running message
// loop or native window/peer.
//
// Deliberately calls createAccessibilityHandler() directly rather than the
// more commonly used getAccessibilityHandler(): the latter (JUCE 8.0.14
// juce_Component.cpp:3323-3326) only returns a handler once the component
// has a live native window peer (getWindowHandle() != nullptr), which this
// headless, no-message-loop test binary never has. createAccessibilityHandler()
// is public API specifically meant to be safely callable/overridable
// independent of any live OS accessibility bridge (see its own docs in
// juce_Component.h) - callers a step removed from the OS bridge (like this
// test) are exactly the documented exception to "should rarely be called
// directly".
namespace
{
    // All knob/toggle/scale-button controls are direct children of the
    // editor itself (see PluginEditor.cpp's addAndMakeVisible calls - none
    // of them live inside a further nested sub-container), so a flat
    // (non-recursive) scan of direct children is sufficient and avoids
    // needing any additional test-only accessors on the editor.
    template <typename ComponentType>
    ComponentType* findChildByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            if (auto* typed = dynamic_cast<ComponentType*> (parent.getChildComponent (i)))
                if (typed->getTitle() == title)
                    return typed;
        }

        return nullptr;
    }

    // juce::Button::createAccessibilityHandler() (unlike juce::Slider's) is
    // declared PROTECTED (JUCE 8.0.14 juce_Button.h) - calling it through a
    // juce::ToggleButton*/juce::Button* would fail to compile even though
    // it's the exact same public virtual originally declared on juce::Component.
    // Per the C++ standard's access-control-for-virtual-calls rule
    // ([class.access.virt]), access is checked against the STATIC type used
    // to name the call, not the dynamic override - calling through a
    // juce::Component& (where the function is public) compiles, and virtual
    // dispatch still correctly invokes the most-derived override at
    // runtime. Used uniformly for all component types tested here for
    // consistency.
    std::unique_ptr<juce::AccessibilityHandler> createHandlerForTest (juce::Component& component)
    {
        return component.createAccessibilityHandler();
    }
}

TEST_CASE ("Knob accessibility value strings include their declared unit", "[gui][a11y]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    SilentiumAudioProcessorEditor editor (processor);

    struct Expectation
    {
        const char* label;
        const char* unitSuffix;
    };

    // One representative knob per unit declared in ParameterLayout.cpp
    // (.withLabel("dB"/"ms"/"Hz")) - A-02's gap was units being dropped
    // entirely, not a per-parameter formatting detail, so this doesn't need
    // to be exhaustive over all 9 knobs to catch a regression.
    const Expectation expectations[] = {
        { "Threshold", "dB" },
        { "Hold", "ms" },
        { "SC HPF", "Hz" },
    };

    for (const auto& expectation : expectations)
    {
        // v0.3.4: the 9 knobs are a plain, fully transparent-draw
        // juce::Slider (see PluginEditor.h's docs) rather than any custom
        // Slider subclass - the accessibility wiring under test
        // (SliderAttachment's textFromValueFunction, set identically in
        // PluginEditor.cpp's configureKnob() regardless of the concrete
        // Slider type) is unaffected by that change.
        auto* knob = findChildByTitle<juce::Slider> (editor, expectation.label);
        REQUIRE (knob != nullptr);

        const auto handler = createHandlerForTest (*knob);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);

        const auto valueText = valueInterface->getCurrentValueAsString();
        INFO ("knob \"" << expectation.label << "\" accessible value = \"" << valueText.toStdString() << "\"");
        CHECK (valueText.endsWith (expectation.unitSuffix));
    }
}

TEST_CASE ("Toggle accessible name matches its visual label and exposes a checkable state", "[gui][a11y]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    SilentiumAudioProcessorEditor editor (processor);

    auto* toggle = findChildByTitle<juce::ToggleButton> (editor, "Duck");
    REQUIRE (toggle != nullptr);
    CHECK (toggle->getTitle() == "Duck");

    const auto handler = createHandlerForTest (*toggle);
    REQUIRE (handler != nullptr);

    // juce::ToggleButton's own constructor calls setClickingTogglesState(true)
    // (JUCE 8.0.14 juce_ToggleButton.cpp), so juce::Button::isToggleable()
    // is true and the base juce::Button AccessibilityHandler correctly
    // exposes checkable/checked state regardless of the exact reported role
    // (see A-06, deferred - role is a separate, non-blocking nuance from
    // state, which already works).
    CHECK (handler->getCurrentState().isCheckable());
}

TEST_CASE ("Scale button's accessible title reflects the current scale percentage, not a static string", "[gui][a11y]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    SilentiumAudioProcessorEditor editor (processor);

    auto* scaleButton = dynamic_cast<juce::TextButton*> (editor.findChildWithID ("scaleButton"));
    REQUIRE (scaleButton != nullptr);

    // A-05 fix: at construction (100% step), the title must already contain
    // the CURRENT percentage, not the old static "Window scale" that never
    // updated.
    CHECK (scaleButton->getTitle().contains ("100%"));

    // Cycle the scale via the SAME onClick callback a mouse/keyboard/AT
    // click would invoke (PluginEditor.cpp wires scaleButton.onClick to
    // cycleScale()) - called directly rather than via triggerClick(), which
    // only posts an async command message (JUCE 8.0.14
    // juce_Button.cpp:359-362) that would need a running message loop to
    // ever actually fire, which this headless test binary doesn't have.
    REQUIRE (scaleButton->onClick);
    scaleButton->onClick();

    CHECK (scaleButton->getButtonText() == "150%");
    CHECK (scaleButton->getTitle().contains ("150%"));
    CHECK_FALSE (scaleButton->getTitle().contains ("100%"));
}

// Issue #5 (keyboard navigation) + M3 a11y review follow-ups A-09/A-10:
// juce::Slider ships with setWantsKeyboardFocus(false) in JUCE 8.0.14
// (juce_Slider.cpp:1461, Slider::init), so a plain-Slider knob is silently
// unreachable by Tab and never receives key events at all - and even when
// focused, the base keyPressed (juce_Slider.cpp:1029) steps by the raw
// parameter interval (0.01 dB on an 80 dB Threshold range) and ignores
// Shift entirely. basilica::gui::KnobSlider fixes all three; these tests
// pin that contract at the editor level.

TEST_CASE ("Every interactive control is keyboard-focusable", "[gui][a11y]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    SilentiumAudioProcessorEditor editor (processor);

    int slidersSeen = 0, togglesSeen = 0;

    for (int i = 0; i < editor.getNumChildComponents(); ++i)
    {
        auto* child = editor.getChildComponent (i);

        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            ++slidersSeen;
            INFO ("knob \"" << slider->getTitle().toStdString() << "\"");
            CHECK (slider->getWantsKeyboardFocus());
        }
        else if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child))
        {
            ++togglesSeen;
            INFO ("toggle \"" << toggle->getTitle().toStdString() << "\"");
            CHECK (toggle->getWantsKeyboardFocus());
        }
    }

    // All 9 knobs and both footer toggles must be present AND focusable -
    // a zero-match loop must not pass vacuously.
    CHECK (slidersSeen == 9);
    CHECK (togglesSeen == 2);

    auto* scaleButton = editor.findChildWithID ("scaleButton");
    REQUIRE (scaleButton != nullptr);
    CHECK (scaleButton->getWantsKeyboardFocus());
}

TEST_CASE ("Arrow keys step knobs by a practical amount, Shift+Arrow steps finer", "[gui][a11y]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    SilentiumAudioProcessorEditor editor (processor);

    // Threshold: linear -80..0 dB, 0.01 dB interval (ParameterLayout.cpp) -
    // the exact case A-10 flagged as impractical under the base-class
    // behaviour (8000 presses for a full sweep).
    auto* knob = findChildByTitle<juce::Slider> (editor, "Threshold");
    REQUIRE (knob != nullptr);

    knob->setValue (-40.0, juce::sendNotificationSync);

    // keyPressed() is called through Component& for the same
    // [class.access.virt] reason documented on createHandlerForTest().
    juce::Component& knobAsComponent = *knob;

    // Plain Right = 1% of the 80 dB range = 0.8 dB.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (knob->getValue() == Catch::Approx (-39.2).margin (1.0e-6));

    // Shift+Right = 0.1% = 0.08 dB (the keyboard analog of Shift-drag).
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                          juce::ModifierKeys::shiftModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (-39.12).margin (1.0e-6));

    // Plain Left steps back down symmetrically.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (knob->getValue() == Catch::Approx (-39.92).margin (1.0e-6));

    // PageDown = 10% = 8 dB.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::pageDownKey)));
    CHECK (knob->getValue() == Catch::Approx (-47.92).margin (1.0e-6));

    // Home/End jump to the range extremes (WAI-ARIA slider pattern).
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (knob->getValue() == Catch::Approx (-80.0));
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    // margin() rather than bare Approx: the value round-trips through the
    // float parameter, and Approx's relative epsilon collapses to zero at 0.0.
    CHECK (knob->getValue() == Catch::Approx (0.0).margin (1.0e-4));
}

TEST_CASE ("Ctrl/Cmd-modified arrow presses are left to the host", "[gui][a11y]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    SilentiumAudioProcessorEditor editor (processor);

    auto* knob = findChildByTitle<juce::Slider> (editor, "Threshold");
    REQUIRE (knob != nullptr);

    knob->setValue (-40.0, juce::sendNotificationSync);
    juce::Component& knobAsComponent = *knob;

    CHECK_FALSE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                              juce::ModifierKeys::ctrlModifier, 0)));
    CHECK_FALSE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                              juce::ModifierKeys::commandModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (-40.0));
}
