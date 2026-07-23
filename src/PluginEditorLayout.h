#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Silentium's own @1x faceplate/control-bay geometry table - lives in its
// own header, rather than as an anonymous-namespace block inside
// PluginEditor.cpp, so tests/gui/EditorLayoutTests.cpp can assert layout
// invariants directly against the SAME numbers PluginEditor.cpp actually
// lays components out with, instead of a second hand-copied set of
// constants that could silently drift out of sync.
//
// v0.3.4 (this revision): MASTER-05 BASELINE ARCHITECTURE, per Yves' final
// art direction - a full replacement of the three prior "component
// composition" attempts (v0.3.1's bare JUCE-drawn background, v0.3.2's
// single-master faceplate, v0.3.3's true component assembly). master-05.png
// (.scaffold/gui-assets/faceplate-silentium-v3/master-05.png, 1264x848) is
// now the SOLE baked faceplate: obsidian plate, brass bevel, 4 corner
// screws, rose flourish, both VU dial faces at rest (empty - no needle, no
// LED), all 9 knobs at 12 o'clock, the 2 toggles UP/on, and both tube-vent
// grilles at their normal (mid-intensity) glow are ALL part of this one
// image. Every constant below was re-measured DIRECTLY against master-05.png
// by .scaffold/gui-assets/faceplate-silentium-v3/analysis/measure_master_05.py
// (HoughCircles for the VU bezels/knobs/screws, HSV colour-threshold blob
// detection for the toggles/rose/vents - see that script's own docs for the
// exact technique per element family, and analysis/master_05_measurements.json
// for its raw output) - NOT copied from the master-04 table this file
// previously held (a different render generation; master-04 was a
// deliberately BARE plate for the now-abandoned "true component assembly"
// approach, so its measurements do not apply here even though the overall
// plate geometry is otherwise unchanged from master-03 onward, per Yves).
// Every constant is this master's own pixel geometry scaled by
// plateWidth1x / masterCanvasWidthPx down to this @1x table - re-derive all
// of them together (by re-running measure_master_05.py) if the master
// render is ever replaced.
namespace slnt::layout
{
    // juce::Rectangle/Point's constructors are not constexpr (JUCE 8.0.14),
    // so the rects below are plain namespace-scope consts rather than true
    // constexpr - still zero-initialisation-order risk since they only
    // depend on integer literals.

    // Master render's own canvas size, kept purely for documentation/
    // re-derivation purposes (the scale factor below is plateWidth1x /
    // masterCanvasWidthPx = 900 / 1264).
    constexpr int masterCanvasWidthPx = 1264;
    constexpr int masterCanvasHeightPx = 848;

    constexpr int plateWidth1x = 900;
    constexpr int plateHeight1x = 604; // masterCanvasHeightPx scaled by the same factor as plateWidth1x

    // Each AnalogMeter's component bounds are sized/positioned so the
    // needle/glow/LED overlay lands correctly on master-05's own BAKED dial
    // face - AnalogMeter no longer draws a face image of its own (see
    // AnalogMeter.h's v0.3.4 docs), so this box is purely a coordinate frame
    // for the overlay elements, not "the box a face asset is drawn into" as
    // in prior revisions. Measured: outer brass-bezel bounding box, master
    // px centre (321.0, 292.5)/(942.0, 292.0), diameter ~358px (mean of the
    // two meters' independently-measured diameters, which agree to within
    // 0.5px). meterPivotXFraction/meterPivotYFraction locate the needle-
    // pivot hub (the hex hub on the anchor bar) as a fraction of this box -
    // measured directly (HoughCircles on the right meter's own face bbox,
    // cross-checked by eye against the source render) rather than assumed;
    // both meters share this single fraction pair (mirrored-duplicate dial
    // design).
    constexpr int meterComponentSize1x = 255;
    const juce::Point<int> meterLTopLeft1x { 101, 81 };
    const juce::Point<int> meterRTopLeft1x { 543, 81 };
    constexpr float meterPivotXFraction = 0.478f;
    constexpr float meterPivotYFraction = 0.666f;

    // Control-bay knobs: a STAGGERED/brick layout baked into the master
    // render (row 2 sits offset right of row 1, not a straight 5-col/2-row
    // grid) - explicit per-knob centres rather than derived grid cells.
    // Row order/count matches PluginEditor.cpp's knobLayout table (row 1:
    // Threshold, Attack, Hold, Release, Range; row 2: Lookahead, SC HPF,
    // SC LPF, Knee). All 9 knobs are BAKED into master-05 at their 12
    // o'clock rest pose - these centres now position a transparent,
    // undecorated juce::Slider overlay (mouse + APVTS only, see
    // PluginEditor.cpp's Knob struct) rather than a rotating image.
    constexpr int knobRow1Y1x = 381;
    constexpr int knobRow2Y1x = 456;
    constexpr int knobDiameter1x = 48;

    constexpr std::array<int, 5> knobRow1X1x { 270, 360, 449, 539, 628 };
    constexpr std::array<int, 4> knobRow2X1x { 316, 405, 494, 584 };

    // Two footer toggles (Duck, Listen), same Y, explicit X centres - both
    // baked into master-05 in the UP/on position. toggleZoneSize1x is the
    // (deliberately generous) square crop PluginEditor.cpp blits from
    // master-06.png over master-05.png when a toggle is OFF (see that
    // file's paint() docs) - sized well beyond toggleSize1x (the toggle
    // body's own measured diameter) so the full lever-pivot arc is covered
    // with no visible seam; harmless to oversize since master-05/master-06
    // are pixel-identical everywhere outside the toggle mechanism itself.
    constexpr int toggleY1x = 514;
    constexpr int toggleSize1x = 32;
    constexpr int toggleZoneSize1x = 56;
    constexpr std::array<int, 2> toggleX1x { 405, 494 };

    // Rose flourish ornament - BAKED into master-05, no draw call (unlike
    // the master-04 generation's separate rose-emblem-v4.png overlay). Kept
    // here (same names as before) purely so
    // tests/gui/EditorLayoutTests.cpp's containment assertions keep
    // compiling against a real measurement; master-05's ornament is a thin
    // horizontal flourish line rather than the older circular/squircle
    // medallion, so roseDiameter1x is its bounding box's larger (width)
    // extent, not a true circle diameter.
    const juce::Point<int> roseCentre1x { 451, 323 };
    constexpr int roseDiameter1x = 150;

    // Four corner screws - BAKED into master-05, no draw call. Kept for the
    // same reason as roseCentre1x/roseDiameter1x above.
    const std::array<juce::Point<int>, 4> screwCentres1x {
        juce::Point<int> { 76, 77 },  // top-left
        juce::Point<int> { 815, 83 }, // top-right
        juce::Point<int> { 77, 525 }, // bottom-left
        juce::Point<int> { 820, 527 } // bottom-right
    };
    constexpr int screwDiameter1x = 21;

    // Tube-vent glow banks: unlike the master-04 generation (4 independently
    // flickering discrete tube-glow instances composited via a separate
    // tube-glow-v4.png asset), master-05 bakes the vent grille AND its
    // normal-intensity glow directly into the plate. The only remaining
    // dynamic behaviour is a SUBTLE, signal-driven cross-blend of this
    // WHOLE region between master-glow-dim.png (low signal) and master-05
    // itself (the approved baseline "normal" glow, t=1 - see
    // PluginEditor.cpp's paint() docs for the hard ceiling this cross-blend
    // must never exceed). Bounds measured directly (brightness-threshold
    // blob detection unioning the 6 individual slat bounding boxes) on the
    // RIGHT bank; the LEFT bank's bounds are the RIGHT bank's own box
    // mirrored across the plate's horizontal centre, because a direct
    // threshold on the left half is defeated by the diagonal softbox-
    // reflection sheen baked into that side of the render (see
    // measure_master_05.py's docs) - the two banks are a mirrored asset
    // pair by construction, so this is a measurement of the same geometry,
    // not an approximation of a different one.
    const juce::Rectangle<int> ventLBankBounds1x { 106, 355, 214 - 106, 487 - 355 };
    const juce::Rectangle<int> ventRBankBounds1x { 686, 355, 794 - 686, 487 - 355 };

    // Peak LEDs: a SMALL red indicator lamp sitting ON THE PLATE, OUTSIDE
    // each VU dial's brass bezel, at its upper-left - NOT inside the dial
    // face (a prior revision incorrectly drew a large LED inside the dial,
    // over the tick scale; rejected by Yves against master-03's own
    // reference look). This is why these live here as their own top-level
    // overlay geometry rather than inside AnalogMeter's own bounds (which
    // only cover the dial face itself, see PluginEditor.cpp's paint() for
    // the draw call).
    //
    // Measured DIRECTLY from master-03-raw.png (.scaffold/gui-assets/
    // faceplate-silentium-v3/master-03-raw.png, 1264x848 - the one master
    // render generation that happens to have BOTH peak LEDs lit; master-05,
    // this file's own baseline, has neither) by
    // analysis/led_diff/{register,extract}.py: register master-03 onto
    // master-05 per LED (small-window median-SSD sub-pixel search, mirroring
    // analysis/needle_diff/register.py's technique), abs-diff the two
    // (identical plate everywhere except the lit LED + its soft halo), then
    // a diff-magnitude-weighted centroid (3 passes, each pass re-centring a
    // circular ROI) for the centre and an azimuthal-mean radial profile for
    // the core/halo diameters (see extraction_results.json for the raw
    // numbers). Centres below are master-03's own measured centres scaled by
    // plateWidth1x / masterCanvasWidthPx (900/1264, this file's own top-of-
    // file scale factor) down to this @1x table; ledCoreDiameter1x is the
    // bright bulb DISC only (radius where the azimuthal-mean diff magnitude
    // first drops below half its peak value), NOT the much larger soft halo
    // - the halo is left to overflow past this nominal draw diameter
    // naturally via the led-master-diff.png asset's own alpha (see
    // PluginEditor.cpp's ledContentDiameterFraction docs), matching the old
    // AnalogMeter-owned LED's same convention.
    //
    // Both LEDs were independently measured (left core diameter 18.0 master
    // px, right 19.0 master px - agree to within rounding); the LEFT LED is
    // the one actually extracted into led-master-diff.png (analysis/
    // led_diff/finalize.py), the right is used only as an appearance cross-
    // check (see that revision's handoff notes) - both meters draw the SAME
    // asset at these two independently-measured centres, per the suite's
    // mirrored-duplicate dial design.
    const juce::Point<float> ledLCentre1x { 121.93f, 109.87f };
    const juce::Point<float> ledRCentre1x { 561.79f, 110.81f };
    constexpr float ledCoreDiameter1x = 12.82f;

    constexpr int topStripHeight1x = 32;
    constexpr int topStripGap1x = 6;
    constexpr int scaleButtonWidth1x = 64;

    constexpr int baseEditorWidth = plateWidth1x;
    constexpr int baseEditorHeight = topStripHeight1x + topStripGap1x + plateHeight1x;

    constexpr std::array<float, 3> scaleSteps { 1.0f, 1.5f, 2.0f };
}
