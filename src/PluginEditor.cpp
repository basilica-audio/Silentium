#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "gui/ImageDensity.h"
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
        const char* labelText;
        int col;
        int row;
    };

    // Signal-flow-grouped: row 1 is the primary gate shape (Threshold
    // through Range), row 2 is the voicing/refinement controls (Lookahead,
    // the sidechain filters, Knee) - the same grouping ParameterLayout.cpp's
    // own comments use.
    constexpr std::array<KnobLayoutEntry, 9> knobLayout {
        KnobLayoutEntry { ParamIDs::threshold, "Threshold", 0, 0 },
        KnobLayoutEntry { ParamIDs::attack, "Attack", 1, 0 },
        KnobLayoutEntry { ParamIDs::hold, "Hold", 2, 0 },
        KnobLayoutEntry { ParamIDs::release, "Release", 3, 0 },
        KnobLayoutEntry { ParamIDs::range, "Range", 4, 0 },
        KnobLayoutEntry { ParamIDs::lookahead, "Lookahead", 0, 1 },
        KnobLayoutEntry { ParamIDs::scHighpass, "SC HPF", 1, 1 },
        KnobLayoutEntry { ParamIDs::scLowpass, "SC LPF", 2, 1 },
        KnobLayoutEntry { ParamIDs::knee, "Knee", 3, 1 },
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
    // installLocalisation() runs before presetBar exists, not an
    // installLocalisation() call in the constructor *body*, which would run
    // too late.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (SilentiumAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }

    // Non-parameter, per-session UI state: the stepped scale choice (0/1/2)
    // stored as a plain property directly on apvts.state. This ValueTree is
    // exactly what getStateInformation()/setStateInformation() serialise (see
    // PluginProcessor.cpp), so a property set here round-trips through host
    // session save/reload the same way the registered parameters do, without
    // needing its own parameter (a scale step is a view choice, not
    // something that should be host-automatable or appear in a DAW's
    // parameter list).
    constexpr const char* uiScaleStepProperty = "uiScaleStep";

    basilica::gui::AnalogMeter::Assets makeMeterAssets()
    {
        basilica::gui::AnalogMeter::Assets assets;
        assets.face1x = loadImage (BinaryData::vu_brass_face_480x270_png, BinaryData::vu_brass_face_480x270_pngSize);
        assets.face2x = loadImage (BinaryData::vu_brass_face_960x540_png, BinaryData::vu_brass_face_960x540_pngSize);
        assets.needle1x = loadImage (BinaryData::vu_brass_needle_480x270_png, BinaryData::vu_brass_needle_480x270_pngSize);
        assets.needle2x = loadImage (BinaryData::vu_brass_needle_960x540_png, BinaryData::vu_brass_needle_960x540_pngSize);
        assets.glass1x = loadImage (BinaryData::vu_brass_glass_480x270_png, BinaryData::vu_brass_glass_480x270_pngSize);
        assets.glass2x = loadImage (BinaryData::vu_brass_glass_960x540_png, BinaryData::vu_brass_glass_960x540_pngSize);
        return assets;
    }
}

SilentiumAudioProcessorEditor::SilentiumAudioProcessorEditor (SilentiumAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit)),
      gainReductionMeter (makeMeterAssets(), "Gain Reduction meter"),
      inputLevelMeter (makeMeterAssets(), "Input Level meter")
{
    setLookAndFeel (&lookAndFeel);

    facePlateImage1x = loadImage (BinaryData::faceplate_silentium_900x600_png, BinaryData::faceplate_silentium_900x600_pngSize);
    facePlateImage2x = loadImage (BinaryData::faceplate_silentium_1800x1200_png, BinaryData::faceplate_silentium_1800x1200_pngSize);
    brandIconImage = loadImage (BinaryData::icon256_png, BinaryData::icon256_pngSize);

    // Creation order below doubles as the accessibility/keyboard focus
    // order (JUCE's default FocusTraverser walks children in z-order,
    // i.e. creation order, when no custom traverser is installed) - kept
    // deliberately matching the visual reading order: header/scale control,
    // preset bar, meters (GR then Input), knob grid row-by-row, then the
    // two footer toggles.
    titleLabel.setText ("Silentium", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setFont (juce::Font (juce::FontOptions {}
                                        .withName (juce::Font::getDefaultSerifFontName())
                                        .withHeight (26.0f)
                                        .withStyle ("Bold")));
    titleLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (presetBar);

    // A-05 fix (M3 a11y review): button text/title are set from
    // applyScaleStep() below, which runs once here at construction (with
    // the stored/default step) and again on every subsequent click, so the
    // accessible name always reflects the CURRENT scale instead of a static
    // "Window scale" that never updates (see applyScaleStep()'s docs).
    // componentID is set purely so tests/gui/EditorAccessibilityTests.cpp
    // can find this button without depending on its (now dynamic) title.
    scaleButton.setComponentID ("scaleButton");
    scaleButton.onClick = [this] { cycleScale(); };
    addAndMakeVisible (scaleButton);

    addAndMakeVisible (gainReductionMeter);
    addAndMakeVisible (inputLevelMeter);

    const auto knobStrip1x = loadImage (BinaryData::knob_brass_strip_160px_128f_png, BinaryData::knob_brass_strip_160px_128f_pngSize);
    const auto knobStrip2x = loadImage (BinaryData::knob_brass_strip_320px_128f_png, BinaryData::knob_brass_strip_320px_128f_pngSize);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider = std::make_unique<basilica::gui::FilmstripKnob> (knobStrip1x, knobStrip2x, 128);
        configureKnob (knobs[i], entry.parameterId, entry.labelText);
    }

    const auto toggleStrip1x = loadImage (BinaryData::toggle_brass_strip_100px_4f_png, BinaryData::toggle_brass_strip_100px_4f_pngSize);
    const auto toggleStrip2x = loadImage (BinaryData::toggle_brass_strip_200px_4f_png, BinaryData::toggle_brass_strip_200px_4f_pngSize);

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

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (knob.label);

    // SliderAttachment MUST be constructed before the textFromValueFunction
    // override below, not after: JUCE 8.0.14's SliderParameterAttachment
    // constructor (juce_ParameterAttachments.cpp:128) itself assigns
    // `slider.textFromValueFunction = [&param] (double v) { return
    // param.getText (...); }` (no unit) as part of wiring the attachment -
    // setting our own function BEFORE this point would be silently
    // clobbered the moment the attachment is created.
    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, *knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        // A-02 fix (M3 a11y review): every parameter declares its unit via
        // .withLabel() in ParameterLayout.cpp (dB/ms/Hz), but that metadata
        // was never read by the GUI layer - SliderAttachment's own
        // textFromValueFunction (see above) formats the value but drops the
        // unit entirely. This feeds BOTH the popup value display
        // (setPopupDisplayEnabled above) and the accessibility value string
        // (juce_Slider.cpp:1811's
        // SliderAccessibilityHandler::ValueInterface::getCurrentValueAsString()
        // calls Slider::getTextFromValue(), which calls this same function),
        // so one fix here covers both surfaces. Still uses the parameter's
        // own getText() (not just a raw suffix) so the reported precision/
        // rounding matches what the host itself would display.
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

    toggle.label.setText (labelText, juce::dontSendNotification);
    toggle.label.setJustificationType (juce::Justification::centredLeft);
    toggle.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (toggle.label);

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
    // title always wins over the button's own text for screen readers
    // (JUCE 8.0.14 juce_ButtonAccessibilityHandler.h:67-75), so a title set
    // once at construction and never updated would silently strand AT users
    // on "Window scale" forever, with no way to learn the plugin is now at
    // 150%/200%. Re-setting the title here, alongside the visible text, on
    // every step change (construction included, since this runs from the
    // constructor too) keeps both surfaces in sync.
    scaleButton.setTitle ("Window scale, " + percentText);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    setSize ((int) std::lround ((float) baseEditorWidth * scale),
             (int) std::lround ((float) baseEditorHeight * scale));
}

void SilentiumAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto plateBounds = juce::Rectangle<float> (0.0f, (float) topStripHeight1x * scale + (float) topStripGap1x * scale,
                                                      (float) plateWidth1x * scale, (float) plateHeight1x * scale);

    const auto& plateImage = basilica::gui::pickImageForWidth (facePlateImage1x, facePlateImage2x,
                                                               plateWidth1x, (int) plateBounds.getWidth());
    if (plateImage.isValid())
        g.drawImage (plateImage, plateBounds);

    if (brandIconImage.isValid())
    {
        const auto d = (float) roundelRadius1x * 1.7f * scale;
        const auto cx = (float) roundelCentre1x.x * scale;
        const auto cy = plateBounds.getY() + (float) roundelCentre1x.y * scale;
        g.drawImage (brandIconImage, juce::Rectangle<float> (d, d).withCentre ({ cx, cy }));
    }
}

void SilentiumAudioProcessorEditor::resized()
{
    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [scale] (int v) { return (int) std::lround ((float) v * scale); };

    auto bounds = getLocalBounds();
    auto topStrip = bounds.removeFromTop (s (topStripHeight1x));

    scaleButton.setBounds (topStrip.removeFromRight (s (scaleButtonWidth1x)));
    presetBar.setBounds (topStrip);

    // Everything below is expressed in plate-local coordinates (the base
    // @1x table above), then offset by the top strip + gap and scaled.
    const auto toPlateRect = [&] (juce::Rectangle<int> plateLocal)
    {
        return juce::Rectangle<int> (s (plateLocal.getX()),
                                     s (topStripHeight1x + topStripGap1x) + s (plateLocal.getY()),
                                     s (plateLocal.getWidth()),
                                     s (plateLocal.getHeight()));
    };

    titleLabel.setBounds (toPlateRect (headerBay1x.withWidth (roundelCentre1x.x - headerBay1x.getX() - roundelRadius1x - 8)));

    // The VU layers' dial content only occupies the central half of their
    // canvas (see AnalogMeter::contentFractionOfCanvas) - expand each
    // meter's bounds around its bay's centre so the VISIBLE dial fills the
    // engraved bay. The overhang is fully transparent and mouse-transparent.
    const auto expandMeterBounds = [] (juce::Rectangle<int> bay)
    {
        const auto factor = 1.0f / basilica::gui::AnalogMeter::contentFractionOfCanvas;
        return bay.withSizeKeepingCentre ((int) std::lround ((float) bay.getWidth() * factor),
                                          (int) std::lround ((float) bay.getHeight() * factor));
    };

    gainReductionMeter.setBounds (expandMeterBounds (toPlateRect (meterLBay1x)));
    inputLevelMeter.setBounds (expandMeterBounds (toPlateRect (meterRBay1x)));

    const auto controlBay = toPlateRect (controlBay1x);
    const auto cellW = controlBay.getWidth() / gridCols;
    const auto cellH = controlBay.getHeight() / gridRows;
    const auto knobDiam = s (knobDiameter1x);
    const auto labelH = s (knobLabelHeight1x);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        const auto cellX = controlBay.getX() + entry.col * cellW;
        const auto cellY = controlBay.getY() + entry.row * cellH;

        knobs[i].label.setBounds (cellX, cellY, cellW, labelH);
        knobs[i].slider->setBounds (juce::Rectangle<int> (knobDiam, knobDiam)
                                        .withCentre ({ cellX + cellW / 2, cellY + labelH + (cellH - labelH) / 2 }));
    }

    const auto auxBay = toPlateRect (auxBay1x);
    const auto toggleSize = juce::jmin (auxBay.getHeight() - s (4), s (34));
    const auto togglePairWidth = auxBay.getWidth() / (int) toggleLayout.size();

    for (size_t i = 0; i < toggleLayout.size(); ++i)
    {
        const auto pairX = auxBay.getX() + (int) i * togglePairWidth;
        const auto toggleBounds = juce::Rectangle<int> (toggleSize, toggleSize)
                                       .withCentre ({ pairX + toggleSize, auxBay.getCentreY() });

        toggles[i].button->setBounds (toggleBounds);
        toggles[i].label.setBounds (toggleBounds.getRight() + s (6), toggleBounds.getY(),
                                    togglePairWidth - toggleSize - s (12), toggleSize);
    }
}

void SilentiumAudioProcessorEditor::timerCallback()
{
    gainReductionMeter.setTargetDb (audioProcessor.getGainReductionDb());
    inputLevelMeter.setTargetDb (audioProcessor.getInputLevelDb());
}
