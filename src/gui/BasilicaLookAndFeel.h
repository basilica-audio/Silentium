#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-wide INTERIM label styling: gold/parchment serif text with a subtle
// engraved look (a dark shadow offset down-right + a faint warm highlight
// offset up-left, reading as lettering recessed into the stone/gunmetal
// faceplate), drawn directly by JUCE - NOT the final Blender-rendered
// faceplate labels.
//
// Per .scaffold/gui-assets/faceplate-silentium-v1/README.md's "Why no text /
// no exact knob layout" section, real per-control labels must eventually be
// actual Blender text objects baked into the faceplate PNG (image-gen/JUCE
// text cannot reliably match the engraved brand look pixel-for-pixel), which
// requires each plugin's parameter layout to be locked for GUI purposes
// first. Until that follow-up pass, PluginEditor's single layout table
// positions both the FilmstripKnob/FilmstripToggle controls AND their
// juce::Label captions from the SAME coordinates, so when the labels are
// later baked into the faceplate art, only the juce::Label instances are
// removed (setVisible(false) or deleted) - no control moves.
namespace basilica::gui
{
    class BasilicaLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        BasilicaLookAndFeel();

        juce::Font getLabelFont (juce::Label& label) override;
        void drawLabel (juce::Graphics& g, juce::Label& label) override;

        // A-03 fix (M3 a11y review): the exact colour pair drawLabel() paints
        // the caption text over its opaque backing chip with, exposed so
        // tests/gui/BasilicaLookAndFeelContrastTests.cpp can compute the real
        // WCAG 1.4.3 contrast ratio against the SAME colours actually
        // rendered, rather than a second hand-copied pair that could
        // silently drift out of sync.
        static juce::Colour getLabelTextColour() noexcept;
        static juce::Colour getLabelBackingChipColour() noexcept;

    private:
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
    // plugin.
    //
    // `shape` selects an elliptical ring for round controls (FilmstripKnob)
    // or a rounded-rectangle ring for rectangular ones (FilmstripToggle).
    enum class FocusRingShape
    {
        ellipse,
        roundedRectangle
    };

    void paintFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds, FocusRingShape shape);
}
