#include "BasilicaLookAndFeel.h"

namespace
{
    // A-03 fix (M3 a11y review): the original single-tone gold
    // (0xffd9b869) measured 2.45:1 against the faceplate's lighter gunmetal
    // panel tone (~#787372) and 4.37:1 against its darker tone (~#514D4D) -
    // both fail WCAG 1.4.3's 4.5:1 minimum for normal-size (14px) text.
    // Verified analytically that no warm-gold hue can clear 4.5:1 against
    // the lighter panel tone without desaturating to near-white (even a
    // 5%-saturation near-white gold only just clears 4.5:1), which would
    // abandon the "antique gold on charcoal" brand look. Instead: brighten
    // the gold somewhat AND paint an opaque dark "recess" backing chip
    // behind each caption's actual text bounds (see drawLabel() below,
    // labelBackingChip) - this reads as lettering engraved into a shadowed
    // recess, consistent with the existing engrave-shadow aesthetic, while
    // guaranteeing the rendered text always sits on a KNOWN, fully opaque
    // background regardless of whatever faceplate/meter art is underneath.
    // See tests/gui/BasilicaLookAndFeelContrastTests.cpp for the WCAG
    // relative-luminance contrast-ratio check on this exact colour pair
    // (computed ratio: ~12.8:1, comfortably above the 4.5:1 floor).
    const juce::Colour goldText { 0xfff0d38c };
    const juce::Colour engraveShadow { 0xcc000000 };
    const juce::Colour engraveHighlight { 0x40fff4d6 };
    const juce::Colour labelBackingChip { 0xff17110c };

    constexpr int backingChipPaddingX = 4;
    constexpr int backingChipPaddingY = 2;
    constexpr float backingChipCornerSize = 3.0f;

    // A-01 fix: bright gold distinct from (and higher-contrast than) the
    // caption gold above - the focus ring must stay legible against both
    // the dark gunmetal panel and the brighter brass control art, so it is
    // deliberately more saturated/lighter than goldText rather than reusing
    // it. A thin dark halo is drawn just behind/outside the gold ring so it
    // reads clearly regardless of what's directly behind the control too.
    const juce::Colour focusRingGold { 0xffffd24c };
    const juce::Colour focusRingHalo { 0xcc000000 };
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

    juce::Colour BasilicaLookAndFeel::getLabelTextColour() noexcept { return goldText; }
    juce::Colour BasilicaLookAndFeel::getLabelBackingChipColour() noexcept { return labelBackingChip; }

    void BasilicaLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
    {
        g.fillAll (label.findColour (juce::Label::backgroundColourId));

        if (label.isBeingEdited())
            return;

        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const auto font = getLabelFont (label);
        const auto textArea = label.getBorderSize().subtractedFrom (label.getLocalBounds());
        const auto numLines = juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight()));
        const auto& text = label.getText();

        g.setFont (font);

        // A-03 fix: an opaque backing chip sized to the actual text bounds
        // (not the whole, much-wider label bounds), positioned within
        // textArea per the label's own justification - see this file's
        // top-of-file docs.
        if (text.isNotEmpty())
        {
            const auto textWidth = juce::jmin (textArea.getWidth(), juce::GlyphArrangement::getStringWidthInt (font, text));
            const auto textHeight = juce::jmin (textArea.getHeight(), (int) std::ceil (font.getHeight()));

            const auto chipSize = juce::Rectangle<int> (textWidth + backingChipPaddingX * 2,
                                                         textHeight + backingChipPaddingY * 2);
            const auto positionedChip = label.getJustificationType().appliedToRectangle (chipSize, textArea);

            g.setColour (labelBackingChip.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (positionedChip.toFloat(), backingChipCornerSize);
        }

        // Engraved look: a dark shadow offset down-right and a faint warm
        // highlight offset up-left, both drawn behind the main gold fill -
        // reads as lettering recessed into the stone/gunmetal faceplate even
        // though this text is JUCE-drawn, not baked into the faceplate PNG
        // yet (see this header's class-level docs).
        g.setColour (engraveShadow.withMultipliedAlpha (alpha));
        g.drawFittedText (text, textArea.translated (1, 1), label.getJustificationType(),
                          numLines, label.getMinimumHorizontalScale());

        g.setColour (engraveHighlight.withMultipliedAlpha (alpha));
        g.drawFittedText (text, textArea.translated (-1, -1), label.getJustificationType(),
                          numLines, label.getMinimumHorizontalScale());

        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.drawFittedText (text, textArea, label.getJustificationType(),
                          numLines, label.getMinimumHorizontalScale());
    }

    void paintFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds, FocusRingShape shape)
    {
        constexpr float ringInset = 2.0f;
        constexpr float haloStrokeWidth = 4.0f;
        constexpr float ringStrokeWidth = 2.0f;
        constexpr float roundedRectCornerSize = 4.0f;

        const auto ringBounds = bounds.reduced (ringInset);

        const auto drawShape = [shape] (juce::Graphics& graphics, juce::Rectangle<float> shapeBounds, float strokeWidth)
        {
            if (shape == FocusRingShape::ellipse)
                graphics.drawEllipse (shapeBounds, strokeWidth);
            else
                graphics.drawRoundedRectangle (shapeBounds, roundedRectCornerSize, strokeWidth);
        };

        // Dark halo drawn first (slightly larger/thicker) so the bright gold
        // ring on top of it stays legible against light or busy backgrounds
        // too, not just the dark gunmetal panel - a common accessible-focus-
        // ring technique (light ring + dark outline, or vice versa) that
        // guarantees visibility regardless of what's directly underneath.
        g.setColour (focusRingHalo);
        drawShape (g, ringBounds, haloStrokeWidth);

        g.setColour (focusRingGold);
        drawShape (g, ringBounds, ringStrokeWidth);
    }
}
