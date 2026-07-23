#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "gui/AnalogMeter.h"
#include "gui/BasilicaLookAndFeel.h"
#include "presets/PresetBar.h"

class SilentiumAudioProcessor;

// v0.3.4 MASTER-05 BASELINE ARCHITECTURE: a full replacement of the three
// prior "component composition" attempts (v0.3.1's bare JUCE-drawn
// background, v0.3.2's single baked master, v0.3.3's true component
// assembly of many standalone master-reference PNGs), per Yves' explicit
// rejection of that Frankenstein result. master-05.png is now the SOLE
// faceplate: obsidian plate, brass bevel, 4 corner screws, rose flourish,
// both VU dial faces at rest, all 9 knobs at 12 o'clock, the 2 toggles UP,
// and both tube-vent grilles at normal glow are ALL baked into that one
// image (see PluginEditor.cpp's paint() docs for the exact z-order of the
// small set of dynamic overlays drawn on top of it: per-toggle master-06
// crop swap, a subtle vent-glow cross-blend, and the two AnalogMeter
// children's needle/LED/glow). Knobs and toggles are now PASSIVE controls -
// a transparent juce::Slider per knob and a plain juce::ToggleButton per
// toggle, used purely for mouse handling + APVTS attachment, with no custom
// paint()/visible rotation of their own (the double-image artifact of
// overlaying a rotating control on top of a baked one, rejected in an
// earlier iteration, is structurally impossible this way).
class SilentiumAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit SilentiumAudioProcessorEditor (SilentiumAudioProcessor& processorToEdit);
    ~SilentiumAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Test/preview-only: sets the vent-glow cross-blend mix directly,
    // bypassing the normal ballistics + flicker jitter timerCallback()
    // computes from the processor's own input-level reading - mirrors
    // AnalogMeter::setImmediateDbForPreview()'s rationale (see that
    // method's docs): this headless-test-friendly, message-loop-independent
    // hook is what lets tests/gui/EditorSnapshotTests.cpp render a specific
    // "live-looking" vent-glow state without pumping real timer ticks
    // through a running message loop this test binary doesn't have. Normal
    // operation never calls this.
    void setVentGlowMixForPreview (float t) noexcept;

private:
    // Re-reads the processor's metering atomics and feeds AnalogMeter -
    // driven by this editor's own juce::Timer (same pattern PresetBar
    // already uses) so the audio thread never touches GUI components
    // directly (see PluginProcessor::getGainReductionDb()/getInputLevelDb()).
    // AnalogMeter's own internal timer then does the actual ~300ms
    // ballistic integration independently of this refresh rate. Also
    // recomputes the vent-glow mix (from the input level reading) and
    // repaints the two vent-bank regions each tick, since that cross-blend
    // is drawn directly in this editor's own paint() rather than by a child
    // component with its own timer (see .cpp).
    void timerCallback() override;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText);
    void applyScaleStep (int newStepIndex);
    void cycleScale();

    // Current vent-glow cross-blend mix in [0,1] (0 = master-glow-dim.png,
    // 1 = master-05.png's own baked "normal" glow - the hard ceiling Yves
    // approved; see PluginEditor.cpp's paint() docs). Recomputed from the
    // processor's input-level reading in timerCallback() with slow
    // ballistics plus a small flicker jitter, read back by paint().
    float ventGlowMix = 1.0f;
    float ventGlowSmoothedInputDb = -100.0f;
    double ventGlowStartTimeSeconds = 0.0;

    // The two vent-bank regions' on-screen bounds at the current scale
    // step, recomputed in resized() and used by timerCallback() to repaint
    // just those regions each tick, rather than the whole plate.
    juce::Rectangle<int> ventGlowRepaintBounds;

    // v0.3.6: the two peak-LED regions' own on-screen bounds (recomputed in
    // resized(), same convention as ventGlowRepaintBounds above) - the LED
    // draw itself lives in this editor's paint() now (see
    // PluginEditorLayout.h's ledLCentre1x/ledRCentre1x docs for why it moved
    // out of AnalogMeter), so this editor's own timerCallback() must
    // explicitly repaint these two small regions each tick to animate the
    // peak-hold/fade alpha AnalogMeter::peakLedAlpha() reports.
    juce::Rectangle<int> ledLRepaintBounds;
    juce::Rectangle<int> ledRRepaintBounds;

    SilentiumAudioProcessor& audioProcessor;

    basilica::gui::BasilicaLookAndFeel lookAndFeel;

    // The single faceplate baseline (master-05.png) and its two dynamic
    // overlay sources (master-06.png for toggle-DOWN crops,
    // master-glow-dim.png for the low end of the vent-glow cross-blend) -
    // see paint() in the .cpp for how each is used. None of these are
    // interactive, so none need to be a full juce::Component.
    juce::Image masterBaseline;
    juce::Image masterToggleDown;
    juce::Image masterGlowDim;

    // v0.3.6: the peak-LED sprite (led-master-diff.png, extracted directly
    // from master-03-raw.png's own baked lit LEDs - see
    // PluginEditorLayout.h's ledLCentre1x/ledRCentre1x docs) - this editor's
    // own paint() draws it twice (once per meter) at the measured plate-
    // level centres, reading each AnalogMeter's peakLedAlpha() for the
    // opacity. No longer owned by AnalogMeter (see that class's v0.3.6 docs
    // for why).
    juce::Image ledImage;

    basilica::presets::PresetBar presetBar;
    juce::TextButton scaleButton;
    int scaleStepIndex = 0; // 0 = 100%, 1 = 150%, 2 = 200%

    basilica::gui::AnalogMeter gainReductionMeter;
    basilica::gui::AnalogMeter inputLevelMeter;

    static constexpr int numKnobs = 9;
    std::array<Knob, numKnobs> knobs;

    // Footer toggles (Duck, Listen) - passive juce::ToggleButton instances;
    // the visible up/down state is drawn by paint()'s master-05/master-06
    // crop swap, not by these components themselves (see this file's
    // top-of-file docs).
    static constexpr int numToggles = 2;
    std::array<Toggle, numToggles> toggles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilentiumAudioProcessorEditor)
};
