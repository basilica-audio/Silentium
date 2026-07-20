#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "gui/AnalogMeter.h"
#include "gui/BasilicaLookAndFeel.h"
#include "gui/FilmstripToggle.h"
#include "gui/RotatingImageKnob.h"
#include "presets/PresetBar.h"

class SilentiumAudioProcessor;

// v0.3.1 visual overhaul editor: photoreal skeuomorphic UI built from the
// reusable src/gui/ component family. Every visible control is wired to a
// real APVTS parameter or a real metering value - no dead decoration.
//
// v0.3.3 (this revision): TRUE COMPONENT ASSEMBLY, per Yves' final art
// direction and superseding v0.3.2's single baked master faceplate. The
// plate is now the BARE baseline render
// (resources/gui/faceplate-silentium-v4-base.png) with every other visible
// element - the softbox reflection, the tube-vent glow flicker, the rose
// emblem, both VU dial faces, the peak LEDs, all 9 knobs, and the 4 corner
// screws - composited as its own standalone master-reference asset, in
// paint() (the static/decorative layers) or by dedicated child components
// (AnalogMeter for the two dials incl. needle/glow/LED, RotatingImageKnob
// for the 9 knobs). See PluginEditorLayout.h for the measured geometry and
// this file's .cpp for the asset-loading/compositing docs (including which
// of the two alpha-recovery techniques - circular cutout vs luminance-
// derived glow - each asset needed, and the one component (the two footer
// toggles) that could NOT be ported to a fresh master-ref asset this
// revision).
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
    // ballistic integration independently of this refresh rate. Also
    // repaints the tube-vent glow region every tick, since that flicker is
    // drawn directly in this editor's own paint() rather than by a child
    // component with its own timer (see .cpp).
    void timerCallback() override;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        std::unique_ptr<basilica::gui::RotatingImageKnob> slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Toggle
    {
        std::unique_ptr<basilica::gui::FilmstripToggle> button;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText);
    void applyScaleStep (int newStepIndex);
    void cycleScale();

    // The tube-vent glow region's on-screen bounds at the current scale
    // step, recomputed in resized() and used by both paint() (to draw it)
    // and timerCallback() (to repaint just that region each tick, rather
    // than the whole plate, for the flicker animation).
    juce::Rectangle<int> ventGlowRepaintBounds;

    SilentiumAudioProcessor& audioProcessor;

    basilica::gui::BasilicaLookAndFeel lookAndFeel;

    // Static/decorative layers, drawn in paint() (see .cpp) - the bare
    // baseline plate, the softbox reflection, the tube-glow flicker source,
    // the rose emblem, and the 4 corner screws. None of these are
    // interactive, so none need to be a full juce::Component.
    juce::Image faceplateBaseImage;
    juce::Image reflectionImage;
    juce::Image tubeGlowImage;
    juce::Image roseEmblemImage;
    juce::Image screwImage;

    double ventFlickerStartTimeSeconds = 0.0;

    basilica::presets::PresetBar presetBar;
    juce::TextButton scaleButton;
    int scaleStepIndex = 0; // 0 = 100%, 1 = 150%, 2 = 200%

    basilica::gui::AnalogMeter gainReductionMeter;
    basilica::gui::AnalogMeter inputLevelMeter;

    static constexpr int numKnobs = 9;
    std::array<Knob, numKnobs> knobs;

    // Footer toggles (Duck, Listen) - still the OLDER FilmstripToggle/
    // toggle_brass_v2_strip_*.png asset family, not a fresh master-ref
    // render (see this file's top-of-file docs and .cpp for why).
    static constexpr int numToggles = 2;
    std::array<Toggle, numToggles> toggles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilentiumAudioProcessorEditor)
};
