#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/AnalogMeter.h"

#include <catch2/catch_test_macros.hpp>

// GUI smoke tests for the master-05 baseline editor (src/PluginEditor.h,
// src/gui/). juce::ScopedJuceInitialiser_GUI is installed once for the
// whole test binary in tests/TestMain.cpp, so Components/Timers are safe to
// construct here even though this is a headless console executable with no
// running message loop (timers simply never fire, which is fine - these
// tests only exercise synchronous construction/paint/destruction).
TEST_CASE ("Editor constructs, lays out, and destroys cleanly", "[gui]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    {
        SilentiumAudioProcessorEditor editor (processor);

        CHECK (editor.getWidth() > 0);
        CHECK (editor.getHeight() > 0);
    }
    // editor destroyed here - JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
    // (used throughout src/gui/ and on the editor itself) asserts at process
    // exit in Debug builds if any tagged instance was ever leaked, so a
    // clean run of this whole test binary is itself the leak check.
}

namespace
{
    // Local copy of EditorAccessibilityTests.cpp's findChildByTitle helper -
    // deliberately not shared/exported between test files (each Catch2
    // .cpp here is meant to be readable standalone), see that file's own
    // docs for why a flat, non-recursive scan is sufficient (every control
    // this touches is a direct child of the editor, never nested in a
    // sub-container).
    template <typename ComponentType>
    ComponentType* findChildByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
            if (auto* typed = dynamic_cast<ComponentType*> (parent.getChildComponent (i)))
                if (typed->getTitle() == title)
                    return typed;

        return nullptr;
    }

    // Configures a deliberately "alive-looking" state before snapshotting -
    // Yves' explicit request for the master-05 baseline revision: Duck OFF
    // (exercises the master-06 toggle-zone crop-swap overlay), Listen ON
    // (stays at master-05's own baked UP artwork), the two VU needles at
    // different, non-zero positions with the Input meter's peak LED lit,
    // the vent-glow mix partway between master-glow-dim and master-05's own
    // ceiling, and the knob grid at varied positions rather than every
    // control sitting at a uniform 12 o'clock rest pose (the knobs
    // themselves never visibly rotate - see PluginEditor.h's docs - but
    // exercising real, varied APVTS values here is still worthwhile: it's
    // what the accessible value-popup text and the SliderAttachment wiring
    // actually reflect).
    //
    // AnalogMeter::setTargetDb() + this component's own ~300ms ballistic
    // ramp, and the editor's own timerCallback()-driven vent-glow ballistics,
    // would need real timer ticks pumped through a running message loop to
    // actually reach these values - this headless test binary has no such
    // loop (see the top-of-file docs), so setImmediateDbForPreview() /
    // setVentGlowMixForPreview() (test/preview-only, see AnalogMeter.h's and
    // PluginEditor.h's docs) seed the ballistic-smoothed readings, the
    // peak-LED state, and the vent-glow mix directly instead.
    void configureLiveLookingState (SilentiumAudioProcessorEditor& editor)
    {
        if (auto* gr = findChildByTitle<basilica::gui::AnalogMeter> (editor, "Gain Reduction meter"))
            gr->setImmediateDbForPreview (-7.0f);

        if (auto* input = findChildByTitle<basilica::gui::AnalogMeter> (editor, "Input Level meter"))
            input->setImmediateDbForPreview (2.0f); // >= AnalogMeter::peakLedThresholdDb - lights the peak LED

        if (auto* duck = findChildByTitle<juce::ToggleButton> (editor, "Duck"))
            duck->setToggleState (false, juce::dontSendNotification); // OFF -> master-06 crop at this toggle

        if (auto* listen = findChildByTitle<juce::ToggleButton> (editor, "Listen"))
            listen->setToggleState (true, juce::dontSendNotification); // ON -> master-05's own baked UP artwork

        editor.setVentGlowMixForPreview (0.6f); // between master-glow-dim and master-05's own ceiling

        struct KnobValue
        {
            const char* label;
            double normalisedValue; // 0..1 proportion of the slider's own range, deliberately varied (not all 0.5)
        };

        const KnobValue knobValues[] = {
            { "Threshold", 0.30 }, { "Attack", 0.70 }, { "Hold", 0.15 },
            { "Release", 0.55 }, { "Range", 0.85 }, { "Lookahead", 0.40 },
            { "SC HPF", 0.60 }, { "SC LPF", 0.25 }, { "Knee", 0.75 },
        };

        for (const auto& kv : knobValues)
            if (auto* knob = findChildByTitle<juce::Slider> (editor, kv.label))
                knob->setValue (knob->proportionOfLengthToValue (kv.normalisedValue), juce::dontSendNotification);
    }
}

TEST_CASE ("Editor snapshot at 100% is non-blank and is written for PR review", "[gui]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    SilentiumAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    configureLiveLookingState (editor);

    // SoftwareImageType (rather than the default NativeImageType) avoids any
    // dependency on an actual native graphics context/window, which keeps
    // this robust on headless CI runners.
    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});

    REQUIRE (snapshot.isValid());
    CHECK (snapshot.getWidth() == editor.getWidth());
    CHECK (snapshot.getHeight() == editor.getHeight());

    // Non-blank: sample a small grid of points and confirm they are not all
    // identical to the top-left corner - a completely blank/solid-fill
    // render (e.g. every asset failing to decode) would fail this.
    const auto reference = snapshot.getPixelAt (0, 0);
    bool foundDifference = false;

    for (int y = 0; y < snapshot.getHeight() && ! foundDifference; y += juce::jmax (1, snapshot.getHeight() / 20))
        for (int x = 0; x < snapshot.getWidth() && ! foundDifference; x += juce::jmax (1, snapshot.getWidth() / 20))
            if (snapshot.getPixelAt (x, y) != reference)
                foundDifference = true;

    CHECK (foundDifference);

    // Written for local/PR review (see docs/gui-preview.png, a committed
    // static copy of a run of this test) - path is relative to the test
    // binary's current working directory, which `ctest --test-dir build`
    // sets to the build directory, landing this at build/gui-preview.png.
    juce::PNGImageFormat pngFormat;
    const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile ("gui-preview.png");

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (outFile.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        CHECK (pngFormat.writeImageToStream (snapshot, *stream));
    }
}
