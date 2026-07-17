#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "gui/AnalogMeter.h"
#include "gui/BasilicaLookAndFeel.h"
#include "gui/FilmstripKnob.h"
#include "gui/FilmstripToggle.h"
#include "presets/PresetBar.h"

class SilentiumAudioProcessor;

// M3 GUI pilot: the suite's first photoreal skeuomorphic editor, built from
// the reusable src/gui/ component family (FilmstripKnob, FilmstripToggle,
// AnalogMeter, BasilicaLookAndFeel) plus the pre-rendered faceplate PNG (see
// .scaffold/gui-assets/faceplate-silentium-v1/README.md). Every visible
// control is wired to a real APVTS parameter or a real metering value - no
// dead decoration, per the basilica-gui-design skill's binding spec.
//
// Layout: a single "knobLayout" table (see PluginEditor.cpp) positions every
// FilmstripKnob AND its juce::Label caption from the SAME base-resolution
// coordinates the faceplate's engraved control-bay grid was authored
// against, so a later pass that bakes real per-control text into the
// faceplate art (see BasilicaLookAndFeel.h's docs) only needs to hide/remove
// the juce::Label instances - no control moves.
//
// Window scaling is STEPPED (100/150/200%, a UA-style corner control next to
// the preset bar, persisted as a plain property on the APVTS state tree -
// not a free/continuous resize, because the backing art is pre-rendered at
// fixed density tiers (see src/gui/ImageDensity.h).
class SilentiumAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit SilentiumAudioProcessorEditor (SilentiumAudioProcessor& processorToEdit);
    ~SilentiumAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    // Re-reads the processor's metering atomics and feeds AnalogMeter -
    // driven by this editor's own juce::Timer (same pattern PresetBar
    // already uses) so the audio thread never touches GUI components
    // directly (see PluginProcessor::getGainReductionDb()/getInputLevelDb()).
    // AnalogMeter's own internal timer then does the actual ~300ms
    // ballistic integration independently of this refresh rate.
    void timerCallback() override;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        std::unique_ptr<basilica::gui::FilmstripKnob> slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        std::unique_ptr<basilica::gui::FilmstripToggle> button;
        juce::Label label;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText);
    void applyScaleStep (int newStepIndex);
    void cycleScale();

    SilentiumAudioProcessor& audioProcessor;

    basilica::gui::BasilicaLookAndFeel lookAndFeel;

    juce::Image facePlateImage1x, facePlateImage2x;
    juce::Image brandIconImage;

    basilica::presets::PresetBar presetBar;
    juce::TextButton scaleButton;
    int scaleStepIndex = 0; // 0 = 100%, 1 = 150%, 2 = 200%

    basilica::gui::AnalogMeter gainReductionMeter;
    basilica::gui::AnalogMeter inputLevelMeter;

    static constexpr int numKnobs = 9;
    std::array<Knob, numKnobs> knobs;

    static constexpr int numToggles = 2;
    std::array<Toggle, numToggles> toggles;

    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilentiumAudioProcessorEditor)
};
