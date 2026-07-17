#include "BasilicaLookAndFeel.h"

namespace
{
    // Antique gold, matching the suite's brand system (see
    // ~/.claude/skills/basilica-gui-design/SKILL.md: "antique gold on
    // charcoal") - close to the knob/toggle assets' own brass hue
    // (~hsv(0.68,0.48,0.20) per knob-brass-v1/README.md's material notes).
    const juce::Colour goldText { 0xffd9b869 };
    const juce::Colour engraveShadow { 0xcc000000 };
    const juce::Colour engraveHighlight { 0x40fff4d6 };
}

namespace basilica::gui
{
    BasilicaLookAndFeel::BasilicaLookAndFeel()
    {
        setColour (juce::Label::textColourId, goldText);
        setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    }

    juce::Font BasilicaLookAndFeel::getLabelFont (juce::Label& label)
    {
        // Font::getDefaultSerifFontName() resolves to a real installed serif
        // on every CI platform this suite builds for (macOS/Windows), unlike
        // a hardcoded "Didot" (macOS-only) - see JUCE 8.0.14
        // juce_Font.h/getDefaultSerifFontName.
        juce::ignoreUnused (label);
        return juce::Font (juce::FontOptions {}
                                .withName (juce::Font::getDefaultSerifFontName())
                                .withHeight (14.0f)
                                .withStyle ("Bold"));
    }

    void BasilicaLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
    {
        g.fillAll (label.findColour (juce::Label::backgroundColourId));

        if (label.isBeingEdited())
            return;

        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const auto font = getLabelFont (label);
        const auto textArea = label.getBorderSize().subtractedFrom (label.getLocalBounds());
        const auto numLines = juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight()));

        g.setFont (font);

        // Engraved look: a dark shadow offset down-right and a faint warm
        // highlight offset up-left, both drawn behind the main gold fill -
        // reads as lettering recessed into the stone/gunmetal faceplate even
        // though this text is JUCE-drawn, not baked into the faceplate PNG
        // yet (see this header's class-level docs).
        g.setColour (engraveShadow.withMultipliedAlpha (alpha));
        g.drawFittedText (label.getText(), textArea.translated (1, 1), label.getJustificationType(),
                          numLines, label.getMinimumHorizontalScale());

        g.setColour (engraveHighlight.withMultipliedAlpha (alpha));
        g.drawFittedText (label.getText(), textArea.translated (-1, -1), label.getJustificationType(),
                          numLines, label.getMinimumHorizontalScale());

        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                          numLines, label.getMinimumHorizontalScale());
    }
}
