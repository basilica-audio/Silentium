#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-wide Basilica look & feel (v0.3.1 visual overhaul).
//
// TYPOGRAPHY: EB Garamond (embedded via BinaryData, resources/fonts/ - OFL
// licensed, chosen over Cormorant Garamond in the overhaul's proof round for
// holding stroke weight at 12-14px label sizes). All JUCE-drawn text (preset
// bar buttons, preset-name display, alert windows, slider value popups) uses
// it; the faceplate's own control labels are ENGRAVED into the plate PNG by
// the Blender pipeline (same family), so the two text worlds match.
//
// PRESET BAR RESKIN: juce::TextButtons are drawn as brass hardware caps by
// 3-slicing the button-brass-v1 renders (left cap / stretched middle / right
// cap, so one asset serves every button width), with hover as a separate
// baked material state and pressed as a runtime darken. The preset-NAME
// button (componentID "presetNameDisplay", set by PresetBar) is drawn as a
// recessed dark display window with warm gold lettering instead - a
// nameplate, not a button cap - while keeping full Button semantics
// (click-to-open-menu, keyboard access, accessible title) untouched.
namespace basilica::gui
{
    class BasilicaLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        BasilicaLookAndFeel();

        juce::Font getLabelFont (juce::Label& label) override;
        void drawLabel (juce::Graphics& g, juce::Label& label) override;

        juce::Font getTextButtonFont (juce::TextButton& button, int buttonHeight) override;
        void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                    const juce::Colour& backgroundColour,
                                    bool shouldDrawButtonAsHighlighted,
                                    bool shouldDrawButtonAsDown) override;
        void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

        // A-03 fix (M3 a11y review): the exact colour pair drawLabel() paints
        // the caption text over its opaque backing chip with, exposed so
        // tests/gui/BasilicaLookAndFeelContrastTests.cpp can compute the real
        // WCAG 1.4.3 contrast ratio against the SAME colours actually
        // rendered, rather than a second hand-copied pair that could
        // silently drift out of sync.
        static juce::Colour getLabelTextColour() noexcept;
        static juce::Colour getLabelBackingChipColour() noexcept;

        // v0.3.1: the equivalent guarantee for the reskinned preset-bar
        // button text - warm-gold lettering over the button asset's baked
        // near-black FACE panel (measured: no ink colour on bare midtone
        // brass can clear WCAG 1.4.3's 4.5:1, so the asset carries a dark
        // face specifically as the text surface). The face colour returned
        // here is a conservative BRIGHTEST-tone bound with headroom over
        // the measured asset (see the .cpp); asserted in
        // BasilicaLookAndFeelContrastTests.cpp. The preset-name display
        // reuses the gold/backing-chip pair above.
        static juce::Colour getButtonTextColour() noexcept;
        static juce::Colour getButtonFaceBrightestColour() noexcept;

        // The suite serif, resolved from the embedded EB Garamond faces
        // (never a system font - see this header's docs). Exposed so the
        // editor and sibling components reuse the exact same faces for any
        // direct text drawing.
        static juce::Font getSerifFont (float height, bool semiBold = false);

    private:
        juce::Image buttonNormal1x, buttonNormal2x, buttonHover1x, buttonHover2x;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BasilicaLookAndFeel)
    };

    // A-01 fix (M3 a11y review, WCAG 2.4.7 Focus Visible): shared keyboard-
    // focus indicator for the suite's filmstrip-rendered controls.
    // FilmstripKnob::paint() and FilmstripToggle::paintButton() both fully
    // override their base class's own paint path (see each header's docs),
    // so neither ever reaches LookAndFeel_V4::drawRotarySlider/
    // drawButtonBackground's own focus handling (JUCE 8.0.14) - this free
    // function is the shared replacement, called directly at the end of each
    // component's own paint() once `hasKeyboardFocus (true)` is true, so the
    // fix lives in one place and is inherited by every sibling plugin that
    // copies this component family rather than being re-discovered per
    // plugin. Since v0.3.1 BasilicaLookAndFeel::drawButtonBackground() calls
    // it too (the brass-image draw path replaces LookAndFeel_V4's own focus
    // indication just like the filmstrip components do).
    //
    // `shape` selects an elliptical ring for round controls (FilmstripKnob)
    // or a rounded-rectangle ring for rectangular ones (FilmstripToggle,
    // preset-bar buttons).
    enum class FocusRingShape
    {
        ellipse,
        roundedRectangle
    };

    void paintFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds, FocusRingShape shape);
}
