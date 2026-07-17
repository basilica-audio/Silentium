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

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BasilicaLookAndFeel)
    };
}
