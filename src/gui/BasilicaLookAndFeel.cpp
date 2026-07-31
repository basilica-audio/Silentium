#include "BasilicaLookAndFeel.h"
#include "ImageDensity.h"

#include <BinaryData.h>

namespace
{
    // A-03 fix (M3 a11y review): the original single-tone gold
    // (0xffd9b869) measured 2.45:1 against the faceplate's lighter gunmetal
    // panel tone - fails WCAG 1.4.3's 4.5:1 minimum for normal-size text.
    // The fix: brighter gold over an opaque dark "recess" backing chip
    // behind each caption's actual text bounds (see drawLabel() below) -
    // guaranteeing the rendered text always sits on a KNOWN, fully opaque
    // background. See tests/gui/BasilicaLookAndFeelContrastTests.cpp for
    // the WCAG contrast-ratio check on this exact colour pair (~12.8:1).
    // Since v0.3.1 the faceplate's own control labels are engraved into the
    // plate PNG and no longer drawn as juce::Labels, but this label path
    // still styles every remaining JUCE label (alert windows, file
    // choosers), so the guarantee stays load-bearing.
    const juce::Colour goldText { 0xfff0d38c };
    const juce::Colour engraveShadow { 0xcc000000 };
    const juce::Colour engraveHighlight { 0x40fff4d6 };
    const juce::Colour labelBackingChip { 0xff17110c };

    constexpr int backingChipPaddingX = 4;
    constexpr int backingChipPaddingY = 2;
    constexpr float backingChipCornerSize = 3.0f;

    // A-01 fix: bright gold distinct from (and higher-contrast than) the
    // caption gold above - the focus ring must stay legible against both
    // the dark gunmetal panel and the brighter brass control art. A thin
    // dark halo behind the gold ring keeps it readable on any background.
    const juce::Colour focusRingGold { 0xffffd24c };
    const juce::Colour focusRingHalo { 0xcc000000 };

    // v0.3.1 preset-bar reskin: warm-gold lettering on the button's baked
    // near-black FACE panel (button-brass-v1 renders a recessed dark face
    // inside the brass rim SPECIFICALLY for this - measured on the rendered
    // asset, no ink colour on bare midtone brass could clear WCAG's 4.5:1).
    // buttonFaceBrightest is a deliberately conservative bound: the actual
    // measured brightest text-region tone across all four asset files is
    // luminance 0.032, this constant is ~0.055 - if the real asset ever
    // renders brighter than this headroom, the contrast test still holds
    // the 4.5:1 line.
    const juce::Colour buttonText { 0xfff0d38c };
    const juce::Colour buttonFaceBrightest { 0xff46403a };

    // Recessed preset-name display window (componentID "presetNameDisplay"):
    // dark glass over the same backing tone the label chips use, warm gold
    // lettering - reads as an engraved nameplate window in the brass bar.
    const juce::Colour displayWindowFill { 0xff17110c };
    const juce::Colour displayWindowEdge { 0xff3a2c14 };

    // 3-slice cap width in the @1x button asset (button_brass_v1_*_40x28:
    // 10px cap + 20px stretchable middle + 10px cap).
    constexpr int buttonCapWidth1x = 10;
    constexpr int buttonAssetH1x = 28;

    juce::Typeface::Ptr loadTypeface (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }

    // The embedded EB Garamond faces, created once and shared. Static-local
    // (not namespace-scope) so creation happens on first use, safely after
    // JUCE's own initialisation.
    juce::Typeface::Ptr serifRegular()
    {
        static juce::Typeface::Ptr t = loadTypeface (BinaryData::EBGaramondRegular_ttf,
                                                     BinaryData::EBGaramondRegular_ttfSize);
        return t;
    }

    juce::Typeface::Ptr serifSemiBold()
    {
        static juce::Typeface::Ptr t = loadTypeface (BinaryData::EBGaramondSemiBold_ttf,
                                                     BinaryData::EBGaramondSemiBold_ttfSize);
        return t;
    }
}

namespace basilica::gui
{
    BasilicaLookAndFeel::BasilicaLookAndFeel()
    {
        setColour (juce::Label::textColourId, goldText);
        setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

        // Popup menus (the preset browser) join the dark-plate world rather
        // than staying at LookAndFeel_V4's default grey.
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff141013));
        setColour (juce::PopupMenu::textColourId, goldText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3a2c14));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xfffff0c8));

        buttonNormal1x = juce::ImageCache::getFromMemory (BinaryData::button_brass_v1_normal_40x28_png,
                                                          BinaryData::button_brass_v1_normal_40x28_pngSize);
        buttonNormal2x = juce::ImageCache::getFromMemory (BinaryData::button_brass_v1_normal_80x56_png,
                                                          BinaryData::button_brass_v1_normal_80x56_pngSize);
        buttonHover1x = juce::ImageCache::getFromMemory (BinaryData::button_brass_v1_hover_40x28_png,
                                                         BinaryData::button_brass_v1_hover_40x28_pngSize);
        buttonHover2x = juce::ImageCache::getFromMemory (BinaryData::button_brass_v1_hover_80x56_png,
                                                         BinaryData::button_brass_v1_hover_80x56_pngSize);
    }

    juce::Font BasilicaLookAndFeel::getSerifFont (float height, bool semiBold)
    {
        auto typeface = semiBold ? serifSemiBold() : serifRegular();

        // Defensive: fall back to the platform serif if the embedded face
        // ever failed to parse, rather than silently rendering nothing.
        if (typeface == nullptr)
            return juce::Font (juce::FontOptions {}
                                   .withName (juce::Font::getDefaultSerifFontName())
                                   .withHeight (height));

        return juce::Font (juce::FontOptions { typeface }.withHeight (height));
    }

    juce::Font BasilicaLookAndFeel::getLabelFont (juce::Label& label)
    {
        juce::ignoreUnused (label);
        return getSerifFont (15.0f, true);
    }

    juce::Font BasilicaLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
    {
        juce::ignoreUnused (button);
        return getSerifFont (juce::jmin (16.0f, (float) buttonHeight * 0.62f), true);
    }

    juce::Colour BasilicaLookAndFeel::getLabelTextColour() noexcept { return goldText; }
    juce::Colour BasilicaLookAndFeel::getLabelBackingChipColour() noexcept { return labelBackingChip; }
    juce::Colour BasilicaLookAndFeel::getButtonTextColour() noexcept { return buttonText; }
    juce::Colour BasilicaLookAndFeel::getButtonFaceBrightestColour() noexcept { return buttonFaceBrightest; }

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
        // textArea per the label's own justification.
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
        // highlight offset up-left, both drawn behind the main gold fill.
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

    void BasilicaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                     const juce::Colour& backgroundColour,
                                                     bool shouldDrawButtonAsHighlighted,
                                                     bool shouldDrawButtonAsDown)
    {
        juce::ignoreUnused (backgroundColour);

        const auto bounds = button.getLocalBounds();
        const auto boundsF = bounds.toFloat();

        if (button.getComponentID() == "presetNameDisplay")
        {
            // Recessed dark display window: opaque fill (the gold lettering
            // in drawButtonText sits on this KNOWN background - same
            // contrast guarantee as the label backing chips), a darker top
            // edge line reading as the recess shadow, a warmer bottom edge
            // as the lit lip.
            g.setColour (displayWindowFill);
            g.fillRoundedRectangle (boundsF, 4.0f);

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawLine (boundsF.getX() + 4.0f, boundsF.getY() + 1.0f,
                        boundsF.getRight() - 4.0f, boundsF.getY() + 1.0f, 1.5f);

            g.setColour (displayWindowEdge);
            g.drawRoundedRectangle (boundsF.reduced (0.5f), 4.0f, 1.0f);

            if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
            {
                g.setColour (juce::Colour (0x22ffd24c));
                g.fillRoundedRectangle (boundsF, 4.0f);
            }
        }
        else
        {
            const auto native1x = buttonNormal1x.isValid() ? buttonNormal1x.getWidth() : 40;
            const auto& normal = basilica::gui::pickImageForWidth (buttonNormal1x, buttonNormal2x,
                                                                   native1x, bounds.getWidth());
            const auto& hover = basilica::gui::pickImageForWidth (buttonHover1x, buttonHover2x,
                                                                  native1x, bounds.getWidth());
            const auto& img = shouldDrawButtonAsHighlighted && hover.isValid() ? hover : normal;

            if (img.isValid())
            {
                // 3-slice: the caps keep their aspect (scaled by the height
                // ratio only), the middle strip stretches to fill.
                const auto srcH = img.getHeight();
                const auto scaleToAsset = (float) srcH / (float) buttonAssetH1x;
                const auto srcCap = juce::roundToInt ((float) buttonCapWidth1x * scaleToAsset);
                const auto dstCap = juce::jmin (bounds.getWidth() / 2,
                                                juce::roundToInt ((float) buttonCapWidth1x
                                                                  * (float) bounds.getHeight() / (float) buttonAssetH1x));

                g.drawImage (img, 0, 0, dstCap, bounds.getHeight(),
                             0, 0, srcCap, srcH);
                g.drawImage (img, bounds.getWidth() - dstCap, 0, dstCap, bounds.getHeight(),
                             img.getWidth() - srcCap, 0, srcCap, srcH);
                g.drawImage (img, dstCap, 0, bounds.getWidth() - dstCap * 2, bounds.getHeight(),
                             srcCap, 0, img.getWidth() - srcCap * 2, srcH);
            }
            else
            {
                // Asset missing (should never happen in a shipped build):
                // draw a plain dark rounded rect so the button stays
                // visible/operable (and the gold text legible) rather than
                // invisible.
                g.setColour (buttonFaceBrightest);
                g.fillRoundedRectangle (boundsF, 4.0f);
            }

            if (shouldDrawButtonAsDown)
            {
                // "pressed" is a runtime darken over the baked material (the
                // asset family deliberately has no third baked state - a
                // click is momentary).
                g.setColour (juce::Colours::black.withAlpha (0.30f));
                g.fillRoundedRectangle (boundsF, 4.0f);
            }

            if (! button.isEnabled())
            {
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRoundedRectangle (boundsF, 4.0f);
            }
        }

        // This image-draw path fully replaces LookAndFeel_V4's own focus
        // indication - restore it explicitly (WCAG 2.4.7, same rule as the
        // filmstrip components).
        if (button.hasKeyboardFocus (true))
            paintFocusRing (g, boundsF, FocusRingShape::roundedRectangle);
    }

    void BasilicaLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
    {
        juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        const auto font = getTextButtonFont (button, button.getHeight());
        g.setFont (font);

        const auto isDisplay = button.getComponentID() == "presetNameDisplay";
        const auto alpha = button.isEnabled() ? 1.0f : 0.5f;

        const auto textArea = button.getLocalBounds().reduced (6, 2);

        // Both surfaces are warm gold on a baked near-black background (the
        // display window's fill, or the button's recessed dark face panel) -
        // see BasilicaLookAndFeelContrastTests.cpp for both contrast pairs.
        // A subtle dark offset underneath gives the engraved read.
        if (! isDisplay)
        {
            g.setColour (juce::Colours::black.withAlpha (0.6f * alpha));
            g.drawFittedText (button.getButtonText(), textArea.translated (1, 1),
                              juce::Justification::centred, 1);
        }

        g.setColour ((isDisplay ? goldText : buttonText).withMultipliedAlpha (alpha));
        g.drawFittedText (button.getButtonText(), textArea, juce::Justification::centred, 1);
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
        // too, not just the dark gunmetal panel.
        g.setColour (focusRingHalo);
        drawShape (g, ringBounds, haloStrokeWidth);

        g.setColour (focusRingGold);
        drawShape (g, ringBounds, ringStrokeWidth);
    }
}
