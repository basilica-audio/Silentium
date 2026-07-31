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

    // v0.3.9 (item 4, gate-approved INNER-DISC variant): per-knob rotating-
    // disc draw geometry, copied verbatim from .scaffold/gui-assets/
    // faceplate-silentium-v3/knob-discs-manifest.json's per-knob
    // refinedCentre1xPx/diameter1xPx fields (NOT parsed from that .json at
    // runtime - paint()/resized() below must not touch the filesystem).
    // INDEX-MATCHED to knobLayout/knobs above (same 0..8 physical knob
    // order - Threshold..Range then Lookahead..Knee - verified against
    // knobLayout's own layout-table centres, which agree to within ~1.7px
    // @1x). Deliberately NOT the same numbers as knobRow1X1x/knobRow2X1x/
    // knobRow1Y1x/knobRow2Y1x above: those integer layout-table centres
    // position the INTERACTIVE (transparent) juce::Slider hit-area, which
    // tests/gui/EditorLayoutTests.cpp's Layout-Invariante tests assert
    // row-Y equality and uniform diameter against - this table is the
    // baked disc art's own sub-pixel measured centre/diameter, used only
    // for the VISUAL rotating-disc draw below, never for hit-testing.
    struct KnobDiscEntry
    {
        float centreX1x;
        float centreY1x;
        float diameter1x; // full fitted disc diameter (the notch/knurl content is already alpha-cut to 80% of this within the asset - see the .png's own provenance docs in CMakeLists.txt)
    };

    constexpr std::array<KnobDiscEntry, 9> knobDiscLayout {
        KnobDiscEntry { 270.427f, 380.150f, 52.520f }, // Threshold
        KnobDiscEntry { 360.071f, 380.293f, 52.060f }, // Attack
        KnobDiscEntry { 449.359f, 380.293f, 52.090f }, // Hold
        KnobDiscEntry { 538.932f, 380.364f, 52.160f }, // Release
        KnobDiscEntry { 628.718f, 379.723f, 52.962f }, // Range
        KnobDiscEntry { 315.427f, 454.272f, 52.230f }, // Lookahead
        KnobDiscEntry { 404.786f, 454.628f, 52.162f }, // SC HPF
        KnobDiscEntry { 494.573f, 454.699f, 51.938f }, // SC LPF
        KnobDiscEntry { 584.003f, 454.272f, 52.636f }, // Knee
    };

    // Knob-disc canvas -> @1x scale factor. The 9 knob-disc-N-inner.png
    // crops are native, UNRESAMPLED 1:1 MASTER-PX crops (85x85, see
    // CMakeLists.txt's asset docs and the manifest's own "scaleChain" note)
    // - unlike knobDiscLayout's centreX1x/centreY1x/diameter1x fields
    // above, the RAW IMAGE PIXELS are not already expressed in this file's
    // @1x plate coordinate system, so drawing them at the correct on-screen
    // size requires this conversion. Identical to
    // plateWidth1x/masterCanvasWidthPx (900/1264 = 0.712025 = the
    // manifest's own "masterPxToOneXPx") - the SAME master->@1x factor
    // toMasterPxRect() above already uses (in its inverse direction) for
    // every other master-render-derived asset in this file. Deliberately
    // NOT `entry.diameter1x / imageWidthPx` (85px) - diameter1x is the
    // FITTED DISC's own diameter, a small fraction of the full 85px canvas
    // (contentDiameterFraction ~=0.86 per the manifest), so dividing by the
    // whole canvas width there would undersize the drawn disc by roughly
    // 1/0.86 =~ 15% - exactly the bug an earlier draft of this revision
    // shipped, which left a sliver of the static master-05 art (including
    // its OWN baked rest-pose notch) visible just inside the rotating
    // disc's own edge at every non-rest rotation angle.
    constexpr float knobDiscCanvasToOneXScale = (float) plateWidth1x / (float) masterCanvasWidthPx;

    // Normalised slider proportion [0,1] -> rotation angle in degrees,
    // clockwise from straight up - 0.5 = 12 o'clock = the baked rest pose
    // (matches every knob's own drawn position in master-05.png, so a knob
    // left at its default value shows no visible seam between the rotating
    // inner disc and the static baked art around it). Range approved
    // against the rotation proof (item 4 brief) - wider than
    // RotatingImageKnob's older -135..+135deg sweep (a different, now-
    // unused single-image-rotation component, see that class's own docs).
    constexpr float knobDiscMinAngleDeg = -140.0f;
    constexpr float knobDiscMaxAngleDeg = 140.0f;

    float knobDiscAngleDegrees (double normalisedValue) noexcept
    {
        const auto clamped = juce::jlimit (0.0, 1.0, normalisedValue);
        return knobDiscMinAngleDeg + (float) clamped * (knobDiscMaxAngleDeg - knobDiscMinAngleDeg);
    }

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
    // fast-flicker amplitude on top of the idle-breathing baseline below
    // (never the wider swing AnalogMeter's dial glow uses for its own
    // primary layer).
    constexpr float ventGlowTauSeconds = 0.15f;
    constexpr float ventGlowFlickerAmplitude = 0.04f;

    // Input-level range mapped to the vent-glow mix: below ventGlowFloorDb
    // the tubes read as idling (SIGNAL-DRIVEN push = 0, see
    // ventGlowIdleBreath* below for why the mix itself is NOT 0 at idle),
    // at/above ventGlowCeilingDb they read at their normal baked glow (mix
    // 1, master-05.png's own level - the hard ceiling Yves approved, see
    // PluginEditor.cpp's paint() docs). Deliberately independent of the two
    // AnalogMeter dials' own dB scale - this is a coarse "is there signal at
    // all" indicator, not a precision meter.
    constexpr float ventGlowFloorDb = -40.0f;
    constexpr float ventGlowCeilingDb = -6.0f;

    // v0.3.9 (item 5 fix): idle-breathing baseline. The ORIGINAL vent-glow
    // mix was `signalDrivenPush * fastFlicker` - at true silence the
    // signal-driven push is exactly 0, and 0 times any flicker multiplier
    // is still 0, which is exactly why the vents read as "not flickering at
    // all" at idle (Yves' explicit complaint). This slow, non-harmonic
    // multi-sine wander (Flicker.h's slowDriftLayers table - the SAME
    // "two time scales" idea item 5 also applies to the VU bulb glow) is
    // unconditional and ADDED to the signal-driven push in
    // updateVentGlowMix() below, so idle now breathes across
    // ventGlowIdleBreathCentre +/- ventGlowIdleBreathHalfRange = 0.55..0.80
    // on its own, with the signal still free to push further toward the
    // t=1.0 ceiling under load (the final jlimit(0,1) there is the SAME
    // hard master-05 ceiling as before - there is still no third, brighter
    // frame to draw, so this can never exceed the approved baked level).
    //
    // v0.3.10 (final sign-off pass, "etwas deutlicher"/"a bit more
    // noticeable" - Yves): both the centre and half-range widened again
    // (0.675/0.125 -> 0.65/0.20) for a deeper, more visible idle breath -
    // the hard jlimit(0,1) ceiling above still guarantees this can never
    // read brighter than master-05's own baked "normal" glow.
    constexpr float ventGlowIdleBreathCentre = 0.65f;
    constexpr float ventGlowIdleBreathHalfRange = 0.20f; // 0.65 +/- 0.20 = 0.45..0.85

    // Distinct phase seed from the fast-flicker call below (phaseSeed 0.0f)
    // and from each AnalogMeter's own phase seeds (0.0f/1.0f, a different
    // visual element entirely) - the three independently-flickering
    // elements (2 VU bulbs + this vent-glow idle wander) must never read as
    // correlated/synchronised.
    constexpr float ventGlowIdlePhaseSeed = 5.0f;

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
    // v0.3.11: assets.needle now holds the SINGLE master-extracted sprite
    // (needle-from-master.png), not a pre-rendered filmstrip - see
    // AnalogMeter.cpp's live juce::AffineTransform rotation. No more
    // assets.hubOccluder either (removed this revision - the new sprite's
    // own alpha already leaves master-05's baked hub-cap art unmodified
    // near the pivot, see AnalogMeter.h's top-of-file docs).
    //
    // v0.3.6: no more assets.led - the peak LED's own image draw moved to
    // this editor (ledImage, loaded in the constructor below, drawn by
    // paint() - see AnalogMeter.h's v0.3.6 docs for why); AnalogMeter now
    // only owns the peak-hold/fade STATE MACHINE, read back via
    // AnalogMeter::peakLedAlpha().
    basilica::gui::AnalogMeter::Assets makeMeterAssets()
    {
        basilica::gui::AnalogMeter::Assets assets;
        assets.needle = loadImage (BinaryData::needlefrommaster_png, BinaryData::needlefrommaster_pngSize);
        return assets;
    }

    // v0.3.11: led-from-master.png's own 58x58 canvas is a native,
    // UNRESAMPLED 1:1 master-px crop (same convention as
    // knobDiscCanvasToOneXScale/toMasterPxRect() below - NOT the old
    // led-master-diff.png's back-derived "core content fraction of a
    // differently-sized canvas" convention, which no longer applies to
    // this new, independently re-measured sprite). ledImageDrawSize1x is
    // therefore simply that native canvas size scaled by the SAME
    // master->@1x factor every other master-derived asset in this file
    // uses (plateWidth1x/masterCanvasWidthPx, 900/1264).
    constexpr float ledSpriteCanvasPx = 58.0f;
    constexpr float ledImageDrawSize1x = ledSpriteCanvasPx * (float) plateWidth1x / (float) masterCanvasWidthPx;

    // Org emblem sprite geometry inside org-emblem-basilica-v1.png's own
    // 1024x1024 canvas: orgEmblemContentDiameterFraction
    // (PluginEditorLayout.h) is the visible medallion's own diameter as a
    // fraction of that full canvas - orgEmblemImageDrawSize1x is the WHOLE
    // image's own draw diameter @1x, back-derived so the medallion itself
    // lands at exactly orgEmblem.diameter1x on screen. Not constexpr (unlike
    // ledImageDrawSize1x above): orgEmblem is a plain `const` (its own
    // juce::Point member has no constexpr constructor in JUCE 8.0.14, see
    // PluginEditorLayout.h's top-of-file docs), so this can only be a
    // runtime-initialised `const` too - still computed exactly once, at
    // static-init time, well before the constructor/paint() ever run.
    const float orgEmblemImageDrawSize1x = orgEmblem.diameter1x / orgEmblemContentDiameterFraction;

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
      gainReductionMeter (makeMeterAssets(), "Gain Reduction meter", 0.0f, meterLPivotXFraction, meterLPivotYFraction),
      inputLevelMeter (makeMeterAssets(), "Input Level meter", 1.0f, meterRPivotXFraction, meterRPivotYFraction)
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
    ledImage = loadImage (BinaryData::ledfrommaster_png, BinaryData::ledfrommaster_pngSize);
    orgEmblemImage = loadImage (BinaryData::orgemblem_png, BinaryData::orgemblem_pngSize);

    // v0.3.9 (item 4): the 9 rotating knob-disc overlays, index-matched to
    // knobLayout/knobDiscLayout above (0=Threshold..8=Knee) - a local
    // {data,size} table rather than a named anonymous-namespace array,
    // since BinaryData's per-asset `Size` constants are non-constexpr
    // (namespace-scope `const int`, not usable in a constexpr array
    // initialiser at file scope) but are perfectly fine read here at
    // runtime, once, in the constructor.
    const struct
    {
        const char* data;
        int size;
    } knobDiscBinaryAssets[9] = {
        { BinaryData::knobdisc0inner_png, BinaryData::knobdisc0inner_pngSize },
        { BinaryData::knobdisc1inner_png, BinaryData::knobdisc1inner_pngSize },
        { BinaryData::knobdisc2inner_png, BinaryData::knobdisc2inner_pngSize },
        { BinaryData::knobdisc3inner_png, BinaryData::knobdisc3inner_pngSize },
        { BinaryData::knobdisc4inner_png, BinaryData::knobdisc4inner_pngSize },
        { BinaryData::knobdisc5inner_png, BinaryData::knobdisc5inner_pngSize },
        { BinaryData::knobdisc6inner_png, BinaryData::knobdisc6inner_pngSize },
        { BinaryData::knobdisc7inner_png, BinaryData::knobdisc7inner_pngSize },
        { BinaryData::knobdisc8inner_png, BinaryData::knobdisc8inner_pngSize },
    };

    for (size_t i = 0; i < knobDiscImages.size(); ++i)
        knobDiscImages[i] = loadImage (knobDiscBinaryAssets[i].data, knobDiscBinaryAssets[i].size);

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

        // v0.3.9 (item 4): repaint just this knob's own rotating-disc region
        // when its value changes - SliderAttachment (constructed inside
        // configureKnob() above) wires itself via juce::Slider::addListener,
        // never Slider::onValueChange (verified against JUCE 8.0.14's
        // juce_ParameterAttachments.cpp), so setting onValueChange here is
        // never clobbered by/never clobbers the attachment. Captures `i` by
        // value (this loop's own size_t, not the KnobLayoutEntry reference),
        // so each of the 9 lambdas closes over its own distinct index.
        knobs[i].slider->onValueChange = [this, i] { repaint (knobDiscRepaintBounds[i]); };
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
    // vent-glow mix is at its ceiling (steps 4-5 below both become no-ops),
    // this is the ENTIRE plate render EXCEPT the rotating knob discs (step 2
    // - a genuinely new, always-drawn overlay, see that step's own docs for
    // why it never creates a visible seam against master-05.png underneath).
    if (masterBaseline.isValid())
        g.drawImage (masterBaseline, plateBounds, juce::RectanglePlacement::centred, false);

    // 2. Rotating knob discs (v0.3.9, item 4, gate-approved INNER-DISC
    // variant) - drawn immediately on top of the baseline plate, one per
    // knob, at that knob's own manifest-measured centre (knobDiscLayout
    // above - sub-pixel, NOT the integer layout-table centres the
    // interactive juce::Slider hit-areas use) and current rotation angle.
    // Each disc's own alpha is already cut to 80% of its fitted radius (see
    // CMakeLists.txt's asset docs), so only the pointer notch + inner
    // knurl are visible/rotating here - the baked outer rim + specular
    // crescent underneath (part of masterBaseline, drawn in step 1 above)
    // are never touched, which is what structurally rules out a co-rotating
    // highlight or a double-knob artifact. The full 85x85 native-master-px
    // canvas is drawn at knobDiscCanvasToOneXScale (NOT diameter1x/85 - see
    // that constant's own docs for why that would undersize the disc) - the
    // 80% cutoff lives entirely in the asset's own alpha, not in this draw
    // call's scale.
    for (size_t i = 0; i < knobDiscLayout.size(); ++i)
    {
        if (! knobDiscImages[i].isValid() || knobs[i].slider == nullptr)
            continue;

        const auto& entry = knobDiscLayout[i];
        const auto& image = knobDiscImages[i];

        const auto centre = juce::Point<float> (plateOrigin.x + s (entry.centreX1x), plateOrigin.y + s (entry.centreY1x));

        const auto proportion = knobs[i].slider->valueToProportionOfLength (knobs[i].slider->getValue());
        const auto angleDeg = knobDiscAngleDegrees (proportion);
        const auto radians = juce::degreesToRadians (angleDeg);

        const auto imageHalfW = (float) image.getWidth() * 0.5f;
        const auto imageHalfH = (float) image.getHeight() * 0.5f;
        const auto imageScale = s (knobDiscCanvasToOneXScale);

        const auto transform = juce::AffineTransform::translation (-imageHalfW, -imageHalfH)
                                    .scaled (imageScale)
                                    .rotated (radians)
                                    .translated (centre.x, centre.y);

        g.drawImageTransformed (image, transform);
    }

    // 3. Org emblem overlay (Basilica Audio rose-window medallion) - drawn
    // right after the baseline plate + knob discs and before every other
    // overlay, so it always sits UNDER the toggle-zone/vent-glow/LED layers
    // (none of which spatially overlap it, but this keeps z-order intent
    // explicit).
    // Not baked into master-05 (see PluginEditorLayout.h's orgEmblem docs
    // for why/provenance/size-and-position derivation). A soft drop shadow
    // is drawn FIRST, offset down-right (light source upper-left, matching
    // every other mounted element's shadow convention) so the medallion
    // reads as sitting ON the plate like the VU bezels do, not floating -
    // deliberately restrained (single radial-gradient ellipse, alpha capped
    // well below a "floating" look), per the suite's GUI-BASELINE rule. The
    // image itself is drawn at a slightly reduced opacity (not full 1.0),
    // the same "restrained, not pasted-on" integration the shadow is meant
    // to achieve - genuinely round (see the asset's own extraction docs),
    // no additional bezel/ring/frame drawn around it here or baked into the
    // asset (an explicit standing user rule).
    if (orgEmblemImage.isValid())
    {
        const auto emblemCentre = juce::Point<float> (plateOrigin.x + s (orgEmblem.centre1x.x),
                                                       plateOrigin.y + s (orgEmblem.centre1x.y));
        const auto emblemRadius = s (orgEmblem.diameter1x) * 0.5f;

        const auto shadowCentre = emblemCentre.translated (s (4.0f), s (4.0f));
        const auto shadowRadius = emblemRadius * 1.18f;

        juce::ColourGradient orgShadowGradient (juce::Colours::black.withAlpha (0.35f),
                                                shadowCentre.x, shadowCentre.y,
                                                juce::Colours::transparentBlack,
                                                shadowCentre.x + shadowRadius, shadowCentre.y,
                                                true);
        orgShadowGradient.addColour (0.7, juce::Colours::black.withAlpha (0.16f));

        g.setGradientFill (orgShadowGradient);
        g.fillEllipse (juce::Rectangle<float> (shadowRadius * 2.0f, shadowRadius * 2.0f).withCentre (shadowCentre));

        const auto drawSize = s (orgEmblemImageDrawSize1x);

        juce::Graphics::ScopedSaveState orgEmblemSaveState (g);
        g.setOpacity (0.92f);
        g.drawImage (orgEmblemImage, juce::Rectangle<float> (drawSize, drawSize).withCentre (emblemCentre));
    }

    // 4. Toggle-Zone overlay: for each toggle that is OFF, blit that
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

    // 5. Vent-glow layer (SUBTLE - Yves-mandated ceiling, see this editor's
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

    // 6. Peak LEDs (v0.3.6) - a SMALL red lamp sitting ON THE PLATE, outside
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
    // Everything else - rose flourish, screws, the knob discs' own baked
    // OUTER rim + specular crescent, both VU faces, tube-vent structure -
    // stays BAKED in master-05, no draw calls for any of it; only each
    // knob's small INNER notch/knurl disc (step 2 above) is a live overlay.)
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

    // v0.3.9 (item 4): the 9 rotating knob discs' own repaint bounds - same
    // "square box at the manifest centre, plus a few scaled px of AA
    // margin" convention as toLedRect() above, sized from each knob's own
    // fitted diameter (knobDiscLayout, not the shared knobDiameter1x layout
    // constant) so a knob whose baked disc happens to be a couple of px
    // larger than average still gets a fully-covering repaint region.
    const auto toKnobDiscRect = [&] (const KnobDiscEntry& entry)
    {
        const auto drawSize = juce::roundToInt ((entry.diameter1x + 4.0f) * scale);
        const auto centrePx = toPlatePoint ({ (int) std::lround (entry.centreX1x), (int) std::lround (entry.centreY1x) });
        return juce::Rectangle<int> (drawSize, drawSize).withCentre (centrePx);
    };

    for (size_t i = 0; i < knobDiscLayout.size(); ++i)
        knobDiscRepaintBounds[i] = toKnobDiscRect (knobDiscLayout[i]);
}

void SilentiumAudioProcessorEditor::updateVentGlowMix() noexcept
{
    // Slow (150ms) ballistic follow of the input level, mapped to [0,1]
    // across ventGlowFloorDb..ventGlowCeilingDb - the SIGNAL-DRIVEN
    // component only (see this file's top-of-file docs for why this range
    // is independent of the meters' own dB scale).
    constexpr float dt = 1.0f / 30.0f;
    ventGlowSmoothedInputDb = basilica::gui::AnalogMeter::stepBallistics (
        ventGlowSmoothedInputDb, audioProcessor.getInputLevelDb(), dt, ventGlowTauSeconds);

    const auto signalPush = juce::jlimit (0.0f, 1.0f,
        juce::jmap (ventGlowSmoothedInputDb, ventGlowFloorDb, ventGlowCeilingDb, 0.0f, 1.0f));

    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;

    // v0.3.9 (item 5 fix): idle-breathing baseline - unconditional (not
    // gated by signal level at all), Flicker.h's slowDriftLayers table,
    // its own distinct phase seed. flickerMultiplier() with amplitude 1.0f
    // returns 1.0 +/- (a weighted sine sum guaranteed inside [-1,1], since
    // the layer weights sum to 1.0 - see Flicker.h's own docs); subtracting
    // 1.0f strips that centring, leaving a raw wander term in [-1,1] to
    // scale by ventGlowIdleBreathHalfRange around its own centre.
    const auto idleWanderUnit = basilica::gui::flickerMultiplier (
        now, ventGlowStartTimeSeconds, ventGlowIdlePhaseSeed, 1.0f, basilica::gui::slowDriftLayers) - 1.0f;
    const auto idleBreath = ventGlowIdleBreathCentre + ventGlowIdleBreathHalfRange * idleWanderUnit;

    // Fast flicker jitter - Flicker.h's standard (faster) 3-layer table,
    // own phase seed (0.0f, distinct from ventGlowIdlePhaseSeed above and
    // from each AnalogMeter's own 0.0f/1.0f seeds).
    const auto flicker = basilica::gui::flickerMultiplier (now, ventGlowStartTimeSeconds, 0.0f, ventGlowFlickerAmplitude);

    // Idle breathing is ADDED to the signal-driven push (not multiplied,
    // and not max()'d - a loud signal genuinely pushes the glow further
    // past wherever the idle breath happens to sit at that instant, exactly
    // matching a real tube amp's glow: the idle warmth doesn't vanish just
    // because a transient briefly pushes it toward full), then the whole
    // sum is scaled by the fast flicker jitter and clamped to the SAME hard
    // [0,1] ceiling as before - master-05's own baked "normal" glow level
    // can still never be exceeded (no third, brighter frame exists to draw).
    ventGlowMix = juce::jlimit (0.0f, 1.0f, (idleBreath + signalPush) * flicker);
}

void SilentiumAudioProcessorEditor::timerCallback()
{
    gainReductionMeter.setTargetDb (audioProcessor.getGainReductionDb());
    inputLevelMeter.setTargetDb (audioProcessor.getInputLevelDb());

    updateVentGlowMix();
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

void SilentiumAudioProcessorEditor::recomputeVentGlowForPreview() noexcept
{
    updateVentGlowMix();
    repaint (ventGlowRepaintBounds);
}

void SilentiumAudioProcessorEditor::setVentGlowElapsedSecondsForPreview (double elapsedSeconds) noexcept
{
    ventGlowStartTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0 - elapsedSeconds;
    updateVentGlowMix();
    repaint (ventGlowRepaintBounds);
}

void SilentiumAudioProcessorEditor::setVentGlowMixForPreview (float t) noexcept
{
    ventGlowMix = juce::jlimit (0.0f, 1.0f, t);
    repaint (ventGlowRepaintBounds);
}
