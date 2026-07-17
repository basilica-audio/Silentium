#include "gui/BasilicaLookAndFeel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

// A-03 fix (M3 a11y review, WCAG 1.4.3 Contrast (Minimum), AA): pure-function
// WCAG relative-luminance contrast-ratio check on the exact colour pair
// BasilicaLookAndFeel::drawLabel() actually paints caption text over (its
// opaque backing chip), fetched via BasilicaLookAndFeel::getLabelTextColour()/
// getLabelBackingChipColour() rather than a second hand-copied colour
// literal that could silently drift out of sync with the ones actually
// rendered.
namespace
{
    // WCAG 2.x relative luminance formula (https://www.w3.org/TR/WCAG21/#dfn-relative-luminance),
    // applied to a juce::Colour's sRGB channels.
    double relativeLuminanceChannel (juce::uint8 sRGB8)
    {
        const auto c = (double) sRGB8 / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
    }

    double relativeLuminance (juce::Colour colour)
    {
        return 0.2126 * relativeLuminanceChannel (colour.getRed())
             + 0.7152 * relativeLuminanceChannel (colour.getGreen())
             + 0.0722 * relativeLuminanceChannel (colour.getBlue());
    }

    // WCAG contrast ratio formula (https://www.w3.org/TR/WCAG21/#dfn-contrast-ratio):
    // (L1 + 0.05) / (L2 + 0.05), L1 the lighter of the two relative
    // luminances - order-independent by construction (max/min), matching
    // the formula's own definition.
    double contrastRatio (juce::Colour a, juce::Colour b)
    {
        const auto lA = relativeLuminance (a);
        const auto lB = relativeLuminance (b);
        const auto lighter = std::max (lA, lB);
        const auto darker = std::min (lA, lB);
        return (lighter + 0.05) / (darker + 0.05);
    }
}

TEST_CASE ("WCAG contrast ratio helper matches known reference values", "[gui][a11y]")
{
    // Black vs white is the canonical maximum-contrast pair: exactly 21:1.
    CHECK (contrastRatio (juce::Colours::black, juce::Colours::white) == Catch::Approx (21.0).margin (0.01));

    // Identical colours are always exactly 1:1 (no contrast at all).
    CHECK (contrastRatio (juce::Colours::grey, juce::Colours::grey) == Catch::Approx (1.0).margin (0.001));

    // Order of the two colours must not matter (the formula is symmetric).
    const auto a = juce::Colour (0xfff0d38c);
    const auto b = juce::Colour (0xff17110c);
    CHECK (contrastRatio (a, b) == Catch::Approx (contrastRatio (b, a)).margin (1.0e-9));
}

TEST_CASE ("BasilicaLookAndFeel's label text/backing-chip colour pair clears WCAG AA 4.5:1", "[gui][a11y]")
{
    using basilica::gui::BasilicaLookAndFeel;

    const auto textColour = BasilicaLookAndFeel::getLabelTextColour();
    const auto backingColour = BasilicaLookAndFeel::getLabelBackingChipColour();

    INFO ("text colour = " << textColour.toDisplayString (true).toStdString());
    INFO ("backing chip colour = " << backingColour.toDisplayString (true).toStdString());

    // WCAG 1.4.3 (AA): normal-size text (this suite's 14px caption font is
    // below the ~18.66px/14pt-bold "large text" threshold, so the stricter
    // 4.5:1 floor applies, not 3:1) must clear 4.5:1 against its background.
    CHECK (contrastRatio (textColour, backingColour) >= 4.5);

    // The backing chip must itself be fully opaque - a translucent chip
    // would make the REAL rendered contrast depend on whatever faceplate/
    // meter art sits underneath it, defeating the point of this guarantee
    // (see BasilicaLookAndFeel.cpp's drawLabel() docs).
    CHECK (backingColour.isOpaque());
}
