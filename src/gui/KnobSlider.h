#pragma once

#include "BasilicaLookAndFeel.h"
#include "ImageDensity.h"
#include "KeyboardSteps.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The editor's knob control, upgraded from a plain juce::Slider for keyboard
// operability (issue #5 / M3 a11y review follow-ups A-09/A-10, WCAG 2.1.1 +
// 2.4.7), with two draw modes:
//
//   - TRANSPARENT (default): the plate knobs' artwork is baked into the
//     master plate render (see PluginEditor.h - since v0.3.4 each plate knob
//     is an invisible hit/drag surface laid over the baked art, with the
//     editor drawing the small rotating inner disc itself). paint() defers
//     to juce::Slider::paint(), which draws nothing visible because all
//     three rotary colour IDs are transparentBlack.
//
//   - FILMSTRIP (issue #33 aux control bay): setFilmstrip() supplies a
//     pre-rendered vertical filmstrip (the knob-brass-v2 render family,
//     N frames sweeping -135deg..+135deg, frame 0 at the TOP), and paint()
//     draws the frame matching the slider's normalised value instead - the
//     artwork itself is never rotated at runtime; every angle is a
//     distinct, individually lit/shadowed Blender render (the same approach
//     the retired FilmstripKnob used before the v0.3.4 master-05 baseline
//     replaced the plate knobs; removed as dead code in #37, revived here
//     as a mode of the ONE knob class rather than a parallel second class,
//     so the keyboard/a11y fixes below can never fork between knob kinds).
//
// Shared behaviour in both modes:
//
//   - juce::Slider::init() ships with setWantsKeyboardFocus(false) in JUCE
//     8.0.14 (juce_Slider.cpp:1461), so a plain Slider is unreachable by
//     Tab and its keyPressed() never fires. The constructor here opts back
//     in, putting every knob into the editor's focus traversal (which
//     follows child creation order - see the PluginEditor constructor).
//
//   - keyPressed() adds WAI-ARIA-style stepping (Arrow 1%, Shift+Arrow
//     fine, PageUp/Down 10%, Home/End extremes) via handleSliderKeyPress()
//     - see KeyboardSteps.h for why the base-class behaviour is unusable
//     with this suite's finely-quantised parameter ranges.
//
//   - Shift-drag = fine mouse adjustment (A-09's mouse-side convention,
//     the analog of the Shift+Arrow keyboard fine step): mouseDown/
//     mouseDrag retune setMouseDragSensitivity() per the current modifier
//     state before forwarding to the base Slider implementation, which
//     reads that sensitivity live on every drag event (JUCE 8.0.14,
//     juce::Slider::Pimpl::pixelsForFullDragExtent) - not a full custom
//     drag implementation. The non-fine base sensitivity is whatever the
//     slider was constructed with (JUCE's default), so plain drags behave
//     exactly as before this override existed.
//
//   - paint() draws the suite's shared focus ring (A-01 pattern,
//     BasilicaLookAndFeel::paintFocusRing) when the knob holds keyboard
//     focus (WCAG 2.4.7 Focus Visible) - in transparent mode the control
//     would otherwise have no visible focus indicator at all.
namespace basilica::gui
{
    class KnobSlider : public juce::Slider
    {
    public:
        KnobSlider (juce::Slider::SliderStyle style, juce::Slider::TextEntryBoxPosition textBoxPosition)
            : juce::Slider (style, textBoxPosition),
              baseDragSensitivity (getMouseDragSensitivity())
        {
            setWantsKeyboardFocus (true);
        }

        // Switches this knob into filmstrip draw mode. strip1xIn/strip2xIn:
        // vertical filmstrips, frame 0 at the top, both holding exactly
        // numFramesIn frames; either may be an invalid/default Image if
        // that density tier isn't available (see ImageDensity.h's fallback
        // behaviour - the aux bay deliberately ships only the @1x knob
        // strip, whose 160px native frame already exceeds the largest
        // 200%-scale draw size, see CMakeLists.txt's asset docs).
        void setFilmstrip (juce::Image strip1xIn, juce::Image strip2xIn, int numFramesIn)
        {
            strip1x = std::move (strip1xIn);
            strip2x = std::move (strip2xIn);
            numFrames = juce::jmax (1, numFramesIn);
            repaint();
        }

        // Pure, side-effect-free frame selection: clamps normalisedValue to
        // [0, 1] before scaling, so an out-of-range caller (shouldn't happen
        // via Slider's own normalisation, but defensive) still lands on a
        // valid frame instead of reading out of bounds. Exposed for unit
        // testing (tests/gui/FilmstripControlTests.cpp).
        static int frameIndexForValue (double normalisedValue, int numFramesIn) noexcept
        {
            const auto clamped = juce::jlimit (0.0, 1.0, normalisedValue);
            return juce::jlimit (0, numFramesIn - 1, (int) std::lround (clamped * (double) (numFramesIn - 1)));
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            return handleSliderKeyPress (*this, key) || juce::Slider::keyPressed (key);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            applyDragSensitivityFor (e.mods);
            juce::Slider::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            applyDragSensitivityFor (e.mods);
            juce::Slider::mouseDrag (e);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& strip = filmstripForCurrentWidth();

            if (strip.isValid())
            {
                const auto frameHeight = strip.getHeight() / numFrames;
                const auto frameIndex = frameIndexForValue (valueToProportionOfLength (getValue()), numFrames);

                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                g.drawImage (strip,
                             0, 0, getWidth(), getHeight(),
                             0, frameIndex * frameHeight, strip.getWidth(), frameHeight);
            }
            else
            {
                juce::Slider::paint (g);
            }

            if (hasKeyboardFocus (true))
                paintFocusRing (g, getLocalBounds().toFloat(), FocusRingShape::ellipse);
        }

    private:
        void applyDragSensitivityFor (const juce::ModifierKeys& mods)
        {
            setMouseDragSensitivity (mods.isShiftDown() ? baseDragSensitivity * fineDragFactor
                                                        : baseDragSensitivity);
        }

        const juce::Image& filmstripForCurrentWidth() const noexcept
        {
            const auto native1xWidth = strip1x.isValid() ? strip1x.getWidth()
                                                         : (strip2x.isValid() ? strip2x.getWidth() / 2 : 1);
            return basilica::gui::pickImageForWidth (strip1x, strip2x, native1xWidth, getWidth());
        }

        juce::Image strip1x, strip2x;
        int numFrames = 1;

        // Shift-fine drag needs 8x the pixels for a full-range sweep,
        // matching the retired FilmstripKnob's own factor (and the suite's
        // "hold Shift for finer control" convention).
        const int baseDragSensitivity;
        static constexpr int fineDragFactor = 8;
    };
}
