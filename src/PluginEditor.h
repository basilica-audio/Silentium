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
// crop swap, a subtle vent-glow cross-blend, the two AnalogMeter children's
// needle/LED/glow, and (v0.3.9, item 4) nine per-knob rotating INNER discs.
// Toggles remain PASSIVE controls - a plain juce::ToggleButton per toggle,
// used purely for mouse handling + APVTS attachment, with no visible
// rotation of its own.
//
// v0.3.9 (item 4, gate-approved INNER-DISC variant): the 9 knobs are STILL
// baked into master-05 at their 12 o'clock rest pose, and each knob's
// juce::Slider is still fully transparent/invisible on its own (mouse+APVTS
// only, no custom paint()) - but this editor's own paint() now additionally
// draws a small per-knob rotating disc (resources/gui/knob-disc-N-inner.png,
// N=0..8) on top of the baked plate at that knob's own live rotation angle.
// This is NOT the double-knob artifact Yves rejected in an earlier
// iteration: each disc's alpha is cut at 80% of its own fitted radius (see
// PluginEditor.cpp's knobDiscLayout docs), so only the pointer notch + inner
// knurl are visible and rotate - the baked outer rim AND its specular
// crescent highlight stay part of the static master-05 plate underneath,
// unrotated, at every angle. See PluginEditor.cpp's paint()/resized() for
// the per-knob draw-centre table and repaint-on-value-change wiring.
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

    // Test/preview-only (item 5 proof): recomputes the vent-glow mix using
    // the SAME production code path timerCallback() uses on its own next
    // 30Hz tick (updateVentGlowMix(), see the .cpp) - i.e. the real idle-
    // breathing wander + signal-driven push + fast flicker jitter, read
    // from ACTUAL wall-clock time - and repaints the vent-bank region.
    // Unlike setVentGlowMixForPreview() above (which sets an arbitrary
    // FINAL mix value directly, bypassing the computation entirely), this
    // lets tests/gui/EditorSnapshotTests.cpp prove the idle-breathing
    // wander is genuinely time-varying by calling this twice with a real
    // (short) wall-clock gap in between, without needing a running message
    // loop to pump this editor's own 30Hz juce::Timer. Normal operation
    // only ever reaches updateVentGlowMix() via timerCallback().
    void recomputeVentGlowForPreview() noexcept;

    // Test/preview-only (item 5 proof): sets the vent-glow idle-breathing
    // clock (ventGlowStartTimeSeconds) so "elapsed time" reads as (very
    // close to) elapsedSeconds, then immediately recomputes ventGlowMix and
    // repaints - the vent-glow equivalent of AnalogMeter::
    // setFlickerElapsedSecondsForPreview() (see that method's own docs for
    // why an ABSOLUTE simulated elapsed time, rather than a relative offset
    // or a real wall-clock sleep, is what makes this proof reproducible).
    // Normal operation never calls this.
    void setVentGlowElapsedSecondsForPreview (double elapsedSeconds) noexcept;

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

    // Shared by timerCallback() and recomputeVentGlowForPreview() (see that
    // method's docs above) - computes ventGlowMix from the processor's
    // current input-level reading (idle-breathing wander + signal-driven
    // push + fast flicker jitter) using real wall-clock time. Does NOT
    // repaint itself - callers repaint ventGlowRepaintBounds explicitly.
    void updateVentGlowMix() noexcept;

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

    // Org emblem (Basilica Audio rose-window medallion) - v0.3.8 addition,
    // drawn as its own overlay by this editor's paint() (see
    // PluginEditorLayout.h's orgEmblem docs for placement/provenance). Not
    // part of the master-05 baked render.
    juce::Image orgEmblemImage;

    basilica::presets::PresetBar presetBar;
    juce::TextButton scaleButton;
    int scaleStepIndex = 0; // 0 = 100%, 1 = 150%, 2 = 200%

    basilica::gui::AnalogMeter gainReductionMeter;
    basilica::gui::AnalogMeter inputLevelMeter;

    static constexpr int numKnobs = 9;
    std::array<Knob, numKnobs> knobs;

    // v0.3.9 (item 4): the 9 rotating INNER-disc overlay images
    // (resources/gui/knob-disc-N-inner.png, loaded in the constructor),
    // index-matched to `knobs`/knobLayout - see PluginEditor.cpp's
    // knobDiscLayout table for each disc's own manifest-measured draw
    // centre/diameter and paint()'s draw call for the live rotation.
    std::array<juce::Image, numKnobs> knobDiscImages;

    // Per-knob on-screen repaint bounds at the current scale step
    // (recomputed in resized(), same convention as ventGlowRepaintBounds
    // above) - each knob's juce::Slider triggers a repaint of just its own
    // entry here via onValueChange (see the constructor), rather than a
    // full-plate repaint, so dragging one knob never forces a redraw of the
    // other eight discs' worth of AffineTransform work.
    std::array<juce::Rectangle<int>, numKnobs> knobDiscRepaintBounds;

    // Footer toggles (Duck, Listen) - passive juce::ToggleButton instances;
    // the visible up/down state is drawn by paint()'s master-05/master-06
    // crop swap, not by these components themselves (see this file's
    // top-of-file docs).
    static constexpr int numToggles = 2;
    std::array<Toggle, numToggles> toggles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilentiumAudioProcessorEditor)
};
