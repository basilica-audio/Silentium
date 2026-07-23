#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "gui/Flicker.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

#include <utility>

namespace
{
    // Base (@1x, 100% scale) faceplate geometry lives in PluginEditorLayout.h
    // (slnt::layout) rather than here, so tests/gui/EditorLayoutTests.cpp can
    // assert layout invariants against the exact constants this file lays
    // components out with - see that header's docs.
    using namespace slnt::layout;

    struct KnobLayoutEntry
    {
        const char* parameterId;
        const char* labelText; // accessible name only - no baked text labels
        int centreX1x;
        int centreY1x;
    };

    // Signal-flow-grouped: row 1 is the primary gate shape (Threshold
    // through Range), row 2 is the voicing/refinement controls (Lookahead,
    // the sidechain filters, Knee) - same grouping ParameterLayout.cpp's own
    // comments use. Positions are the master render's own STAGGERED knob
    // centres (PluginEditorLayout.h's knobRow1X1x/knobRow2X1x).
    constexpr std::array<KnobLayoutEntry, 9> knobLayout {
        KnobLayoutEntry { ParamIDs::threshold, "Threshold", knobRow1X1x[0], knobRow1Y1x },
        KnobLayoutEntry { ParamIDs::attack, "Attack", knobRow1X1x[1], knobRow1Y1x },
        KnobLayoutEntry { ParamIDs::hold, "Hold", knobRow1X1x[2], knobRow1Y1x },
        KnobLayoutEntry { ParamIDs::release, "Release", knobRow1X1x[3], knobRow1Y1x },
        KnobLayoutEntry { ParamIDs::range, "Range", knobRow1X1x[4], knobRow1Y1x },
        KnobLayoutEntry { ParamIDs::lookahead, "Lookahead", knobRow2X1x[0], knobRow2Y1x },
        KnobLayoutEntry { ParamIDs::scHighpass, "SC HPF", knobRow2X1x[1], knobRow2Y1x },
        KnobLayoutEntry { ParamIDs::scLowpass, "SC LPF", knobRow2X1x[2], knobRow2Y1x },
        KnobLayoutEntry { ParamIDs::knee, "Knee", knobRow2X1x[3], knobRow2Y1x },
    };

    struct ToggleLayoutEntry
    {
        const char* parameterId;
        const char* labelText;
        int centreX1x;
    };

    constexpr std::array<ToggleLayoutEntry, 2> toggleLayout {
        ToggleLayoutEntry { ParamIDs::duck, "Duck", toggleX1x[0] },
        ToggleLayoutEntry { ParamIDs::listen, "Listen", toggleX1x[1] },
    };

    // Vent-glow ballistics/flicker (mirrors AnalogMeter's own bulb-glow
    // technique, see Flicker.h) - deliberately slower (150ms tau) than the
    // meters' 300ms dial ballistics would suggest sped up, and a SUBTLE
    // flicker amplitude within Yves' explicit +/-3-5% brief (never the
    // wider swing AnalogMeter's dial glow uses).
    constexpr float ventGlowTauSeconds = 0.15f;
    constexpr float ventGlowFlickerAmplitude = 0.04f;

    // Input-level range mapped to the vent-glow mix: below ventGlowFloorDb
    // the tubes read as idling (mix 0, master-glow-dim.png), at/above
    // ventGlowCeilingDb they read at their normal baked glow (mix 1,
    // master-05.png's own level - the hard ceiling Yves approved, see
    // PluginEditor.cpp's paint() docs). Deliberately independent of the two
    // AnalogMeter dials' own dB scale - this is a coarse "is there signal at
    // all" indicator, not a precision meter.
    constexpr float ventGlowFloorDb = -40.0f;
    constexpr float ventGlowCeilingDb = -6.0f;

    juce::Image loadImage (const char* data, int size)
    {
        return juce::ImageCache::getFromMemory (data, size);
    }

    // M2 i18n frame (.scaffold/specs/preset-system-m2.md): selects German
    // (resources/i18n/de.txt) or falls through to English, once, at editor
    // construction - see Localisation.h's docs. `presetBar` is a member
    // initialised via the constructor's initialiser list, and its own
    // constructor already calls TRANS() on every button label - member
    // initialisers run in declaration order regardless of the order
    // they're written in, so this helper (called from presetBar's own
    // initialiser expression below) is what actually guarantees
    // installLocalisation() runs before presetBar exists.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (SilentiumAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }

    // Non-parameter, per-session UI state: the stepped scale choice (0/1/2)
    // stored as a plain property directly on apvts.state.
    constexpr const char* uiScaleStepProperty = "uiScaleStep";

    // v0.3.4: both VU dial faces are now BAKED into master-05.png (see
    // AnalogMeter.h's docs) - AnalogMeter's Assets no longer carries a face
    // image, only the live overlay elements it still owns the draw for.
    //
    // v0.3.5: assets.needle now holds the pre-rendered needle FILMSTRIP
    // (needle-filmstrip-v1.png), not the old single-image
    // vu-needle-master-v3.png - see AnalogMeter.cpp's frame-index lookup.
    //
    // v0.3.6: no more assets.led - the peak LED's own image draw moved to
    // this editor (ledImage, loaded in the constructor below, drawn by
    // paint() - see AnalogMeter.h's v0.3.6 docs for why); AnalogMeter now
    // only owns the peak-hold/fade STATE MACHINE, read back via
    // AnalogMeter::peakLedAlpha().
    basilica::gui::AnalogMeter::Assets makeMeterAssets()
    {
        basilica::gui::AnalogMeter::Assets assets;
        assets.needle = loadImage (BinaryData::needlefilmstripv1_png, BinaryData::needlefilmstripv1_pngSize);
        return assets;
    }

    // Peak-LED sprite geometry inside led-master-diff.png's own 64x64
    // canvas: ledContentDiameterFraction is the bright bulb CORE disc's own
    // fraction of that canvas (18.0 master-03 px core / 64px canvas -
    // measured by analysis/led_diff/extract.py, see
    // PluginEditorLayout.h's ledCoreDiameter1x docs for the master-px
    // measurement this derives from), NOT the much larger soft halo (which
    // is deliberately left to overflow past the nominal draw size computed
    // below, via the asset's own alpha - same convention the old
    // AnalogMeter-owned LED used for led-v4.png). ledImageDrawSize1x is the
    // WHOLE image's own draw diameter @1x, back-derived so the CORE lands
    // at exactly ledCoreDiameter1x on screen.
    constexpr float ledContentDiameterFraction = 18.0f / 64.0f;
    constexpr float ledImageDrawSize1x = ledCoreDiameter1x / ledContentDiameterFraction;

    // Converts a layout-table rectangle (@1x plate-local units, the
    // PluginEditorLayout.h table's own coordinate frame) into the matching
    // rectangle within the MASTER render's own 1264x848 pixel space - i.e.
    // the source crop to sample from master-06.png/master-glow-dim.png,
    // both of which are full, un-cropped copies of the same master render
    // canvas as master-05.png. The @1x table is itself the master canvas
    // scaled by plateWidth1x / masterCanvasWidthPx (see
    // PluginEditorLayout.h's top-of-file docs), so this is simply that
    // scale factor's inverse.
    juce::Rectangle<int> toMasterPxRect (juce::Rectangle<int> local1x)
    {
        constexpr float inverseScale = (float) masterCanvasWidthPx / (float) plateWidth1x;
        return { juce::roundToInt ((float) local1x.getX() * inverseScale),
                juce::roundToInt ((float) local1x.getY() * inverseScale),
                juce::roundToInt ((float) local1x.getWidth() * inverseScale),
                juce::roundToInt ((float) local1x.getHeight() * inverseScale) };
    }
}

SilentiumAudioProcessorEditor::SilentiumAudioProcessorEditor (SilentiumAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit)),
      gainReductionMeter (makeMeterAssets(), "Gain Reduction meter", 0.0f, meterPivotXFraction, meterPivotYFraction),
      inputLevelMeter (makeMeterAssets(), "Input Level meter", 1.0f, meterPivotXFraction, meterPivotYFraction)
{
    setLookAndFeel (&lookAndFeel);

    // v0.3.4 MASTER-05 BASELINE ARCHITECTURE: exactly three faceplate
    // images, see paint() below for how each is used. master-05 alone bakes
    // everything static (plate, bevel, screws, rose, both empty VU faces,
    // all 9 knobs at rest, both toggles UP, both vents at normal glow) -
    // master-06 and master-glow-dim only ever contribute small, targeted
    // crops (a toggle's own zone; the vent-bank regions), never a full-plate
    // draw of their own.
    masterBaseline = loadImage (BinaryData::master05_png, BinaryData::master05_pngSize);
    masterToggleDown = loadImage (BinaryData::master06_png, BinaryData::master06_pngSize);
    masterGlowDim = loadImage (BinaryData::masterglowdim_png, BinaryData::masterglowdim_pngSize);
    ledImage = loadImage (BinaryData::ledmasterdiff_png, BinaryData::ledmasterdiff_pngSize);

    ventGlowStartTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    ventGlowSmoothedInputDb = audioProcessor.getInputLevelDb();

    // Creation order below doubles as the accessibility/keyboard focus
    // order (JUCE's default FocusTraverser walks children in z-order,
    // i.e. creation order, when no custom traverser is installed) - kept
    // deliberately matching the visual reading order: preset bar + scale
    // control, meters (GR then Input), knob grid row-by-row, then the two
    // footer toggles.
    addAndMakeVisible (presetBar);

    // A-05 fix (M3 a11y review): button text/title are set from
    // applyScaleStep() below, which runs once here at construction (with
    // the stored/default step) and again on every subsequent click.
    scaleButton.setComponentID ("scaleButton");
    scaleButton.onClick = [this] { cycleScale(); };
    addAndMakeVisible (scaleButton);

    addAndMakeVisible (gainReductionMeter);
    addAndMakeVisible (inputLevelMeter);

    // v0.3.4: the 9 knobs are BAKED into master-05 at their 12 o'clock rest
    // pose - RotatingImageKnob is gone, replaced by a plain, fully
    // transparent-draw juce::Slider per knob (mouse + APVTS + value popup
    // only, no visible rotation of its own). LookAndFeel_V4::drawRotarySlider
    // (JUCE 8.0.14) paints its background arc/value arc/thumb purely via
    // Slider::rotarySliderOutlineColourId/rotarySliderFillColourId/
    // thumbColourId with no hardcoded alpha override - setting all three to
    // transparentBlack here makes the control genuinely invisible without
    // needing a custom paint() override at all (verified against the JUCE
    // 8.0.14 source, not assumed). This is what structurally rules out the
    // double-knob artifact Yves rejected in an earlier iteration (there is
    // no second knob graphic drawn on top of the baked one - just an
    // invisible hit/drag surface).
    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                           juce::Slider::NoTextBox);
        knobs[i].slider->setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::transparentBlack);
        knobs[i].slider->setColour (juce::Slider::rotarySliderFillColourId, juce::Colours::transparentBlack);
        knobs[i].slider->setColour (juce::Slider::thumbColourId, juce::Colours::transparentBlack);
        configureKnob (knobs[i], entry.parameterId, entry.labelText);
    }

    // Footer toggles (Duck, Listen): BAKED into master-05 in the UP/on
    // position - a plain, fully transparent juce::ToggleButton per toggle
    // (mouse + APVTS only, no baked-in text). Its own default paint would
    // draw a checkbox-style tick box (LookAndFeel_V4::drawToggleButton,
    // JUCE 8.0.14) - ToggleButton::tickColourId/tickDisabledColourId/
    // textColourId set to transparentBlack neutralise that the same way as
    // the knobs above, no custom paint() needed. The VISIBLE up/down state
    // is drawn by this editor's own paint() (master-05/master-06 crop swap,
    // see below), never by these button components themselves.
    for (size_t i = 0; i < toggleLayout.size(); ++i)
    {
        auto& entry = toggleLayout[i];
        toggles[i].button = std::make_unique<juce::ToggleButton> (juce::String());
        toggles[i].button->setColour (juce::ToggleButton::tickColourId, juce::Colours::transparentBlack);
        toggles[i].button->setColour (juce::ToggleButton::tickDisabledColourId, juce::Colours::transparentBlack);
        toggles[i].button->setColour (juce::ToggleButton::textColourId, juce::Colours::transparentBlack);
        configureToggle (toggles[i], entry.parameterId, entry.labelText);
    }

    setResizable (false, false);

    const auto storedStep = (int) audioProcessor.apvts.state.getProperty (uiScaleStepProperty, 0);
    applyScaleStep (juce::jlimit (0, (int) scaleSteps.size() - 1, storedStep));

    startTimerHz (30);
}

SilentiumAudioProcessorEditor::~SilentiumAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SilentiumAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider->setPopupDisplayEnabled (true, true, this);
    knob.slider->setTitle (labelText);
    knob.slider->setName (labelText);
    addAndMakeVisible (*knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        const auto defaultValue = param->getNormalisableRange().convertFrom0to1 (param->getDefaultValue());
        knob.slider->setDoubleClickReturnValue (true, defaultValue);
    }

    // SliderAttachment MUST be constructed before the textFromValueFunction
    // override below, not after: JUCE 8.0.14's SliderParameterAttachment
    // constructor (juce_ParameterAttachments.cpp:128) itself assigns
    // `slider.textFromValueFunction` as part of wiring the attachment -
    // setting our own function BEFORE this point would be silently
    // clobbered the moment the attachment is created.
    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, *knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        // A-02 fix (M3 a11y review): every parameter declares its unit via
        // .withLabel() in ParameterLayout.cpp (dB/ms/Hz) - feed that into
        // both the popup value display and the accessibility value string.
        knob.slider->textFromValueFunction = [param] (double v)
        {
            return param->getText (param->convertTo0to1 ((float) v), 0) + " " + param->getLabel();
        };
        knob.slider->updateText();
    }
}

void SilentiumAudioProcessorEditor::configureToggle (Toggle& toggle, const juce::String& parameterId, const juce::String& labelText)
{
    toggle.button->setTitle (labelText);
    toggle.button->setName (labelText);
    addAndMakeVisible (*toggle.button);

    toggle.attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, parameterId, *toggle.button);
}

void SilentiumAudioProcessorEditor::cycleScale()
{
    applyScaleStep ((scaleStepIndex + 1) % (int) scaleSteps.size());
}

void SilentiumAudioProcessorEditor::applyScaleStep (int newStepIndex)
{
    scaleStepIndex = juce::jlimit (0, (int) scaleSteps.size() - 1, newStepIndex);
    audioProcessor.apvts.state.setProperty (uiScaleStepProperty, scaleStepIndex, nullptr);

    const auto percentText = juce::String ((int) (scaleSteps[(size_t) scaleStepIndex] * 100.0f)) + "%";
    scaleButton.setButtonText (percentText);

    // A-05 fix (M3 a11y review): an explicitly-set AccessibilityHandler
    // title always wins over the button's own text for screen readers.
    scaleButton.setTitle ("Window scale, " + percentText);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];

    setSize ((int) std::lround ((float) baseEditorWidth * scale),
             (int) std::lround ((float) baseEditorHeight * scale));
}

void SilentiumAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [scale] (float v) { return v * scale; };

    // The top strip is an integrated dark header band (matching the
    // near-black plate) with a thin warm gold rule under it.
    const auto stripHeight = (float) topStripHeight1x * scale;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17141a), 0.0f, 0.0f,
                                             juce::Colour (0xff0b090d), 0.0f, stripHeight, false));
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), stripHeight));
    g.setColour (juce::Colour (0xff5a4420));
    g.fillRect (juce::Rectangle<float> (0.0f, stripHeight - 1.0f * scale, (float) getWidth(), 1.0f * scale));

    const auto plateOrigin = juce::Point<float> (0.0f, stripHeight + (float) topStripGap1x * scale);
    const auto plateBounds = juce::Rectangle<float> (plateOrigin.x, plateOrigin.y,
                                                      (float) plateWidth1x * scale, (float) plateHeight1x * scale);

    // Converts a layout-table rectangle (@1x plate-local units) into
    // on-screen pixel coordinates at the editor's current scale step.
    const auto toScreenRect = [&] (juce::Rectangle<int> local1x)
    {
        return juce::Rectangle<float> (plateOrigin.x + s ((float) local1x.getX()),
                                       plateOrigin.y + s ((float) local1x.getY()),
                                       s ((float) local1x.getWidth()),
                                       s ((float) local1x.getHeight()));
    };

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // 1. Baseline plate: master-05.png alone, filling the plate bounds. This
    // single image bakes the obsidian plate, brass bevel, 4 corner screws,
    // rose flourish, both VU dial faces (empty), all 9 knobs at rest, both
    // toggles UP, and both tube-vent grilles at normal glow - nothing else
    // is drawn for any of those elements. When every toggle is ON and the
    // vent-glow mix is at its ceiling (steps 2-3 below both become no-ops),
    // this is the ENTIRE plate render, exactly matching master-05.png.
    if (masterBaseline.isValid())
        g.drawImage (masterBaseline, plateBounds, juce::RectanglePlacement::centred, false);

    // 2. Toggle-Zone overlay: for each toggle that is OFF, blit that
    // toggle's own zone crop from master-06.png (toggles pointing DOWN) over
    // the master-05 background just drawn - independently per toggle, so
    // one can be down while the other stays up. ON toggles are a no-op
    // (master-05's own UP artwork already shows through unchanged).
    if (masterToggleDown.isValid())
    {
        for (size_t i = 0; i < toggleLayout.size(); ++i)
        {
            if (toggles[i].button->getToggleState())
                continue;

            const auto zoneLocal1x = juce::Rectangle<int> (toggleZoneSize1x, toggleZoneSize1x)
                                          .withCentre ({ toggleLayout[i].centreX1x, toggleY1x });
            const auto destRect = toScreenRect (zoneLocal1x);
            const auto srcRect = toMasterPxRect (zoneLocal1x);

            g.drawImage (masterToggleDown,
                        juce::roundToInt (destRect.getX()), juce::roundToInt (destRect.getY()),
                        juce::roundToInt (destRect.getWidth()), juce::roundToInt (destRect.getHeight()),
                        srcRect.getX(), srcRect.getY(), srcRect.getWidth(), srcRect.getHeight());
        }
    }

    // 3. Vent-glow layer (SUBTLE - Yves-mandated ceiling, see this editor's
    // header docs): the only two frames are master-glow-dim.png (low
    // signal) and master-05.png itself (the approved baseline "normal"
    // glow, already drawn in step 1). ventGlowMix in [0,1] - computed in
    // timerCallback() from the processor's input-level reading with slow
    // ballistics plus a small flicker jitter, or set directly via
    // setVentGlowMixForPreview() for tests/snapshots - cross-blends the TWO
    // vent-bank regions ONLY between those two frames. At mix=1 the alpha
    // below is exactly 0 (a genuine no-op, not just a very faint draw), so
    // the resting look is pixel-identical to master-05.png: the glow can
    // never exceed that baked level, because there is no third, brighter
    // frame to draw at all.
    const auto dimAlpha = juce::jlimit (0.0f, 1.0f, 1.0f - ventGlowMix);

    if (masterGlowDim.isValid() && dimAlpha > 0.001f)
    {
        for (const auto& zoneLocal1x : { ventLBankBounds1x, ventRBankBounds1x })
        {
            const auto destRect = toScreenRect (zoneLocal1x);
            const auto srcRect = toMasterPxRect (zoneLocal1x);

            juce::Graphics::ScopedSaveState saveState (g);
            g.setOpacity (dimAlpha);
            g.drawImage (masterGlowDim,
                        juce::roundToInt (destRect.getX()), juce::roundToInt (destRect.getY()),
                        juce::roundToInt (destRect.getWidth()), juce::roundToInt (destRect.getHeight()),
                        srcRect.getX(), srcRect.getY(), srcRect.getWidth(), srcRect.getHeight());
        }
    }

    // 4. Peak LEDs (v0.3.6) - a SMALL red lamp sitting ON THE PLATE, outside
    // each dial's own bezel, at its upper-left (per Yves' master-03
    // reference - see PluginEditorLayout.h's ledLCentre1x/ledRCentre1x docs
    // for the extraction/measurement this position comes from). Drawn HERE,
    // not by the AnalogMeter children (whose own bounds only cover the dial
    // face itself, well short of this position) - alpha comes from each
    // meter's own peak-hold/fade state machine via peakLedAlpha(), skipped
    // entirely (no draw call) at/near zero, same convention the old
    // AnalogMeter-owned LED draw used.
    if (ledImage.isValid())
    {
        const auto drawSize = s (ledImageDrawSize1x);

        for (const auto& entry : { std::pair { ledLCentre1x, gainReductionMeter.peakLedAlpha() },
                                    std::pair { ledRCentre1x, inputLevelMeter.peakLedAlpha() } })
        {
            const auto alpha = entry.second;
            if (alpha <= 0.001f)
                continue;

            const auto centre = juce::Point<float> (plateOrigin.x + s (entry.first.x),
                                                     plateOrigin.y + s (entry.first.y));

            juce::Graphics::ScopedSaveState saveState (g);
            g.setOpacity (alpha);
            g.drawImage (ledImage, juce::Rectangle<float> (drawSize, drawSize).withCentre (centre));
        }
    }

    // (VU needle/glow overlays are separate AnalogMeter child components,
    // drawn after this method returns - see resized() for their bounds.
    // Everything else - rose flourish, screws, knob discs, both VU faces,
    // tube-vent structure - stays BAKED in master-05, no draw calls for any
    // of it.)
}

void SilentiumAudioProcessorEditor::resized()
{
    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [scale] (int v) { return (int) std::lround ((float) v * scale); };

    auto bounds = getLocalBounds();
    auto topStrip = bounds.removeFromTop (s (topStripHeight1x));

    scaleButton.setBounds (topStrip.removeFromRight (s (scaleButtonWidth1x)).reduced (0, s (2)));
    presetBar.setBounds (topStrip.reduced (0, s (2)));

    // Everything below is expressed in plate-local coordinates (the base
    // @1x table in PluginEditorLayout.h), then offset by the top strip +
    // gap and scaled.
    const auto toPlatePoint = [&] (juce::Point<int> plateLocal)
    {
        return juce::Point<int> (s (plateLocal.x),
                                 s (topStripHeight1x + topStripGap1x) + s (plateLocal.y));
    };

    // Each AnalogMeter's bounds are sized/positioned so its needle/glow
    // overlays land on the plate's baked dial faces (see
    // PluginEditorLayout.h's meterComponentSize1x/meterLTopLeft1x/
    // meterRTopLeft1x docs). The peak LEDs are NOT inside these bounds (see
    // ledLRepaintBounds/ledRRepaintBounds below) - they sit on the plate
    // outside each dial's bezel and are drawn/repainted independently.
    const auto meterSize = s (meterComponentSize1x);
    gainReductionMeter.setBounds (toPlatePoint (meterLTopLeft1x).x, toPlatePoint (meterLTopLeft1x).y, meterSize, meterSize);
    inputLevelMeter.setBounds (toPlatePoint (meterRTopLeft1x).x, toPlatePoint (meterRTopLeft1x).y, meterSize, meterSize);

    // Knobs: explicit STAGGERED centres baked into the master render. Each
    // Slider's own bounds overlap its baked knob disc exactly.
    const auto knobDiam = s (knobDiameter1x);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider->setBounds (juce::Rectangle<int> (knobDiam, knobDiam)
                                        .withCentre (toPlatePoint ({ entry.centreX1x, entry.centreY1x })));
    }

    // Two footer toggles (Duck, Listen): the button's own hit-test bounds
    // match the (generous) toggle-zone size, not the tighter measured
    // toggle diameter, for a comfortable click target consistent with the
    // paint()-drawn crop-swap zone.
    const auto toggleZoneSizePx = s (toggleZoneSize1x);

    for (size_t i = 0; i < toggleLayout.size(); ++i)
    {
        toggles[i].button->setBounds (juce::Rectangle<int> (toggleZoneSizePx, toggleZoneSizePx)
                                          .withCentre (toPlatePoint ({ toggleLayout[i].centreX1x, toggleY1x })));
    }

    // Vent-glow repaint region: the union of both vent banks' own bounds,
    // slightly expanded, so timerCallback()'s per-tick repaint() call only
    // invalidates this area rather than the whole plate.
    const auto toPlateRect = [&] (juce::Rectangle<int> local1x)
    {
        return juce::Rectangle<int> (toPlatePoint (local1x.getPosition()), toPlatePoint (local1x.getBottomRight()));
    };

    ventGlowRepaintBounds = toPlateRect (ventLBankBounds1x).getUnion (toPlateRect (ventRBankBounds1x)).expanded (s (4));

    // v0.3.6: the two peak-LED regions' own repaint bounds - a square box
    // matching paint()'s own draw rect (centre +/- half the LED image's own
    // draw size, which already contains the full soft halo via the asset's
    // own alpha - see ledImageDrawSize1x's docs above), plus a couple of
    // scaled px of margin for anti-aliasing at the sprite's own edge.
    const auto toLedRect = [&] (juce::Point<float> centre1x)
    {
        const auto drawSize = juce::roundToInt ((ledImageDrawSize1x + 4.0f) * scale);
        const auto centrePx = toPlatePoint ({ (int) std::lround (centre1x.x), (int) std::lround (centre1x.y) });
        return juce::Rectangle<int> (drawSize, drawSize).withCentre (centrePx);
    };

    ledLRepaintBounds = toLedRect (ledLCentre1x);
    ledRRepaintBounds = toLedRect (ledRCentre1x);
}

void SilentiumAudioProcessorEditor::timerCallback()
{
    gainReductionMeter.setTargetDb (audioProcessor.getGainReductionDb());
    inputLevelMeter.setTargetDb (audioProcessor.getInputLevelDb());

    // Vent-glow mix: slow (150ms) ballistic follow of the input level,
    // mapped to [0,1] across ventGlowFloorDb..ventGlowCeilingDb, then a
    // small flicker jitter (+/-4%, within Yves' +/-3-5% brief) - see this
    // file's top-of-file docs for why this range is independent of the
    // meters' own dB scale.
    constexpr float dt = 1.0f / 30.0f;
    ventGlowSmoothedInputDb = basilica::gui::AnalogMeter::stepBallistics (
        ventGlowSmoothedInputDb, audioProcessor.getInputLevelDb(), dt, ventGlowTauSeconds);

    const auto baseMix = juce::jlimit (0.0f, 1.0f,
        juce::jmap (ventGlowSmoothedInputDb, ventGlowFloorDb, ventGlowCeilingDb, 0.0f, 1.0f));

    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto flicker = basilica::gui::flickerMultiplier (now, ventGlowStartTimeSeconds, 0.0f, ventGlowFlickerAmplitude);
    ventGlowMix = juce::jlimit (0.0f, 1.0f, baseMix * flicker);

    repaint (ventGlowRepaintBounds);

    // v0.3.6: the peak LEDs are drawn by THIS editor's own paint() now (see
    // that method's docs), not by the AnalogMeter children - their alpha
    // animates every tick via each meter's own peak-hold/fade state machine
    // even when the ballistic-smoothed needle reading itself is settled, so
    // this editor must explicitly repaint their two small regions each tick
    // too (partial repaints, same convention as ventGlowRepaintBounds
    // above - never a full-plate repaint).
    repaint (ledLRepaintBounds);
    repaint (ledRRepaintBounds);
}

void SilentiumAudioProcessorEditor::setVentGlowMixForPreview (float t) noexcept
{
    ventGlowMix = juce::jlimit (0.0f, 1.0f, t);
    repaint (ventGlowRepaintBounds);
}
