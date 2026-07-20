#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "gui/Flicker.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

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
    };

    constexpr std::array<ToggleLayoutEntry, 2> toggleLayout {
        ToggleLayoutEntry { ParamIDs::duck, "Duck" },
        ToggleLayoutEntry { ParamIDs::listen, "Listen" },
    };

    // Native content-diameter fractions for the master-ref assets this
    // editor draws directly (not via AnalogMeter/RotatingImageKnob, which
    // carry their own) - see CMakeLists.txt's asset-list comment for how
    // each *-v4.png was derived from its *-master-ref.png source render.
    // knob-v4.png's own disc content (the knurled brass disc, excluding its
    // drop shadow) occupies this fraction of its 1024x1024 canvas, measured
    // via the same bounding-box technique as the other v4 assets.
    constexpr float knobContentDiameterFraction = 768.0f / 1024.0f;

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

    // v0.3.3: both meters now draw a real dial FACE and a peak LED (see
    // AnalogMeter.h's docs for why the face/pivot handling changed from
    // v0.3.2) in addition to the needle. vu-face-v4.png and led-v4.png are
    // shared (both meters mirror the same dial design/LED), the needle
    // asset is unchanged from v0.3.2.
    basilica::gui::AnalogMeter::Assets makeMeterAssets()
    {
        basilica::gui::AnalogMeter::Assets assets;
        assets.face = loadImage (BinaryData::vufacev4_png, BinaryData::vufacev4_pngSize);
        assets.needle = loadImage (BinaryData::vuneedlemasterv3_png, BinaryData::vuneedlemasterv3_pngSize);
        assets.led = loadImage (BinaryData::ledv4_png, BinaryData::ledv4_pngSize);
        return assets;
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

    // Static/decorative layers - see this file's top-of-file docs and
    // paint() below for the compositing order/technique for each.
    faceplateBaseImage = loadImage (BinaryData::faceplatesilentiumv4base_png, BinaryData::faceplatesilentiumv4base_pngSize);
    reflectionImage = loadImage (BinaryData::reflectionv4_png, BinaryData::reflectionv4_pngSize);
    tubeGlowImage = loadImage (BinaryData::tubeglowv4_png, BinaryData::tubeglowv4_pngSize);
    roseEmblemImage = loadImage (BinaryData::roseemblemv4_png, BinaryData::roseemblemv4_pngSize);
    screwImage = loadImage (BinaryData::screwv4_png, BinaryData::screwv4_pngSize);

    ventFlickerStartTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;

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

    // v0.3.3: the 9 knobs are drawn from a SINGLE master-ref image
    // (knob-v4.png), rotated live via RotatingImageKnob rather than the
    // older FilmstripKnob's pre-rendered rotation strip - see
    // RotatingImageKnob.h's docs for why (no filmstrip render exists yet
    // for this asset generation).
    const auto knobImage = loadImage (BinaryData::knobv4_png, BinaryData::knobv4_pngSize);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider = std::make_unique<basilica::gui::RotatingImageKnob> (knobImage, knobContentDiameterFraction);
        configureKnob (knobs[i], entry.parameterId, entry.labelText);
    }

    // Footer toggles (Duck, Listen): still the OLDER toggle_brass_v2_strip_*
    // filmstrip asset - flagged honestly in this file's top-of-file docs and
    // the revision's handoff notes as the one element that did NOT get a
    // fresh master-ref render this iteration (none was commissioned/
    // supplied). Kept fully functional (real ButtonAttachment) rather than
    // removed, since Duck/Listen are real parameters a user needs direct
    // GUI access to, not just automation-lane access.
    const auto toggleStrip1x = loadImage (BinaryData::toggle_brass_v2_strip_40px_4f_png,
                                          BinaryData::toggle_brass_v2_strip_40px_4f_pngSize);
    const auto toggleStrip2x = loadImage (BinaryData::toggle_brass_v2_strip_80px_4f_png,
                                          BinaryData::toggle_brass_v2_strip_80px_4f_pngSize);

    for (size_t i = 0; i < toggleLayout.size(); ++i)
    {
        auto& entry = toggleLayout[i];
        toggles[i].button = std::make_unique<basilica::gui::FilmstripToggle> (entry.labelText, toggleStrip1x, toggleStrip2x);
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

    const auto toPlatePointF = [&] (juce::Point<int> plateLocal)
    {
        return juce::Point<float> (plateOrigin.x + s ((float) plateLocal.x),
                                   plateOrigin.y + s ((float) plateLocal.y));
    };

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // 1. Bare baseline plate (obsidian, screws/tube-vent grille/dial voids
    // already baked in by the render - only the interactive dial
    // faces/needles/LEDs, the knobs, the rose emblem, and the tube-glow
    // flicker are missing).
    if (faceplateBaseImage.isValid())
        g.drawImage (faceplateBaseImage, plateBounds, juce::RectanglePlacement::centred, false);

    // 2. Softbox reflection - a full-canvas overlay pre-aligned to the
    // base plate's own diagonal highlight (reflection-master-ref.png's own
    // provenance: "matches the master's diagonal upper-left reflection
    // exactly"), baked to a DELIBERATELY LOW alpha gain (0.08x, see
    // CMakeLists.txt's asset docs) before embedding - at full/native
    // brightness this layer badly overexposed the plate (visually
    // confirmed and rejected during this revision's own asset prep, before
    // any JUCE code was written), because the bare plate already bakes its
    // own version of this same highlight; drawn here purely as a subtle
    // reinforcement, not a second independent highlight.
    if (reflectionImage.isValid())
        g.drawImage (reflectionImage, plateBounds, juce::RectanglePlacement::centred, false);

    // 3. Tube-vent glow flicker: 2 independently-flickering instances per
    // side (4 total), additively reinforcing the warm glow already baked
    // into 2 of the 5 slots per vent bank in the base plate render. Uses
    // the SAME multi-sine flicker technique as AnalogMeter's own dial glow
    // (see Flicker.h) - repainted every 30Hz tick from timerCallback(),
    // restricted to ventGlowRepaintBounds so this doesn't force a full
    // 900x604 plate redraw every frame.
    if (tubeGlowImage.isValid())
    {
        const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        const auto drawSize = juce::Point<float> (s ((float) tubeGlowDrawWidth1x), s ((float) tubeGlowDrawHeight1x));

        const auto drawTube = [&] (juce::Point<int> bankCentre1x, int xOffset1x, float phaseSeed)
        {
            const auto centre = toPlatePointF ({ bankCentre1x.x + xOffset1x, bankCentre1x.y });
            const auto flicker = basilica::gui::flickerMultiplier (now, ventFlickerStartTimeSeconds, phaseSeed, 0.04f);

            juce::Graphics::ScopedSaveState saveState (g);
            g.setOpacity (juce::jlimit (0.0f, 1.0f, flicker));
            g.drawImage (tubeGlowImage, juce::Rectangle<float> (drawSize.x, drawSize.y).withCentre (centre));
        };

        drawTube (ventLBankCentre1x, -tubeGlowInstanceXOffset1x, 0.0f);
        drawTube (ventLBankCentre1x, tubeGlowInstanceXOffset1x, 1.0f);
        drawTube (ventRBankCentre1x, -tubeGlowInstanceXOffset1x, 2.0f);
        drawTube (ventRBankCentre1x, tubeGlowInstanceXOffset1x, 3.0f);
    }

    // 4. Rose emblem, centred between the two VU dials.
    if (roseEmblemImage.isValid())
    {
        const auto canvasDrawSize = s ((float) roseDiameter1x) / roseContentDiameterFraction;
        g.drawImage (roseEmblemImage,
                    juce::Rectangle<float> (canvasDrawSize, canvasDrawSize).withCentre (toPlatePointF (roseCentre1x)));
    }

    // (VU dial faces/needles/LEDs, the knobs, and the toggles are all
    // separate child Components, drawn after this method returns - see
    // resized() for their bounds.)

    // 5. Four corner screws, drawn LAST so they sit on top of everything
    // else that might overlap their (tiny) corner footprint.
    if (screwImage.isValid())
    {
        const auto canvasDrawSize = s ((float) screwDiameter1x) / screwContentDiameterFraction;

        for (const auto& centre1x : screwCentres1x)
            g.drawImage (screwImage,
                        juce::Rectangle<float> (canvasDrawSize, canvasDrawSize).withCentre (toPlatePointF (centre1x)));
    }
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

    // Each AnalogMeter's bounds are the exact box its face asset is drawn
    // into (see PluginEditorLayout.h's meterComponentSize1x/meterLTopLeft1x/
    // meterRTopLeft1x docs) - NOT centred on the needle pivot, unlike
    // v0.3.2's convention (the fresh face asset's own hub isn't at its
    // canvas centre, see AnalogMeter.h's docs).
    const auto meterSize = s (meterComponentSize1x);
    gainReductionMeter.setBounds (toPlatePoint (meterLTopLeft1x).x, toPlatePoint (meterLTopLeft1x).y, meterSize, meterSize);
    inputLevelMeter.setBounds (toPlatePoint (meterRTopLeft1x).x, toPlatePoint (meterRTopLeft1x).y, meterSize, meterSize);

    // Knobs: explicit STAGGERED centres baked into the master render.
    const auto knobDiam = s (knobDiameter1x);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider->setBounds (juce::Rectangle<int> (knobDiam, knobDiam)
                                        .withCentre (toPlatePoint ({ entry.centreX1x, entry.centreY1x })));
    }

    // Two footer toggles (Duck, Listen).
    const auto toggleSize = s (toggleSize1x);

    for (size_t i = 0; i < toggleLayout.size(); ++i)
    {
        toggles[i].button->setBounds (juce::Rectangle<int> (toggleSize, toggleSize)
                                          .withCentre (toPlatePoint ({ toggleX1x[i], toggleY1x })));
    }

    // Tube-vent glow repaint region: a generous rectangle spanning both
    // banks' two instances plus the draw size, so timerCallback()'s
    // per-tick repaint() call only invalidates this area rather than the
    // whole plate.
    const auto ventPad = juce::jmax (s (tubeGlowDrawWidth1x), s (tubeGlowDrawHeight1x)) / 2 + s (4);
    const auto ventL = toPlatePoint (ventLBankCentre1x);
    const auto ventR = toPlatePoint (ventRBankCentre1x);
    ventGlowRepaintBounds = juce::Rectangle<int> (ventL, juce::Point<int> (1, 1))
                                .getUnion (juce::Rectangle<int> (ventR, juce::Point<int> (1, 1)))
                                .expanded (ventPad + s (tubeGlowInstanceXOffset1x));
}

void SilentiumAudioProcessorEditor::timerCallback()
{
    gainReductionMeter.setTargetDb (audioProcessor.getGainReductionDb());
    inputLevelMeter.setTargetDb (audioProcessor.getInputLevelDb());

    repaint (ventGlowRepaintBounds);
}
