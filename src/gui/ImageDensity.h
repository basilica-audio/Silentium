#pragma once

#include <juce_graphics/juce_graphics.h>

// Suite-reusable helper for the Basilica Audio GUI component family
// (FilmstripKnob, FilmstripToggle, AnalogMeter): every pre-rendered Blender
// asset ships as two density tiers, @1x and @2x (see
// .scaffold/gui-assets/*/README.md), and the editor uses STEPPED window
// scaling (100/150/200%, see PluginEditor) rather than free resize - so the
// right tier to draw is a simple function of the component's current pixel
// width versus the @1x asset's native width, not the desktop/display scale
// factor (which stepped, prerendered-asset UIs deliberately ignore, per the
// basilica-gui-design skill: "no free resize with prerendered assets").
namespace basilica::gui
{
    // Returns image2x once the component is drawn measurably larger than the
    // @1x asset's own native size (a small margin avoids flapping between
    // tiers right at the boundary), image1x otherwise. Falls back to
    // whichever image is valid if the other one was never supplied/loaded.
    inline const juce::Image& pickImageForWidth (const juce::Image& image1x,
                                                  const juce::Image& image2x,
                                                  int native1xWidth,
                                                  int currentDrawWidth) noexcept
    {
        if (! image2x.isValid())
            return image1x;

        if (! image1x.isValid())
            return image2x;

        constexpr float tierSwitchMargin = 1.25f;
        return (float) currentDrawWidth > (float) native1xWidth * tierSwitchMargin ? image2x : image1x;
    }
}
