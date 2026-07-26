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
    // 0.5px).
    constexpr int meterComponentSize1x = 255;
    const juce::Point<int> meterLTopLeft1x { 101, 81 };
    const juce::Point<int> meterRTopLeft1x { 543, 81 };

    // 2026-07-23 pivot correction: meterPivotXFraction/meterPivotYFraction
    // below (0.478/0.666, HoughCircles-on-the-right-meter-only estimate,
    // shared by both dials) predate the accurate TRUE hub-pivot measurement
    // and put the live needle's rotation centre a few px off each dial's
    // real hub, causing a visible base wobble even though the filmstrip
    // frames themselves are correctly centred on their own pivot. Superseded
    // by the per-meter fractions below, independently fit per dial from
    // .scaffold/gui-assets/faceplate-silentium-v3/analysis/needle_diff/
    // (register.py registers master-03-raw.png, WITH baked needles, onto
    // master-05.png, WITHOUT needles, per dial; hub_fit.py fits the hub-cap
    // shadow-disc RING centre via ray-cast edge detection + Kasa circle fit
    // on master-05's own clean/unoccluded hub; a per-dial axis line is fit
    // from 500+ high-confidence needle-blade diff pixels and the pivot is
    // taken as that axis line projected onto the SAME dial's own hub-ring
    // centre - see finalize_pivot.py's docstring for why the alternative
    // cross-dial two-line intersection was rejected as ill-conditioned).
    // Both dials' axis fits agree with their own independently-fit hub-ring
    // centre to within ~1.5px (left) / ~3.3px (right) - see pivot_final.json.
    // Pivots were measured in each dial's own 540x540 local crop (register.py
    // CROP_HALF=270 around the master-px bezel centres above); converted to
    // master-05 full-canvas px by adding each crop's origin (left (51,22),
    // right (672,22)), then to this table's @1x fractions via
    // fraction = (pivotMasterPx * plateWidth1x/masterCanvasWidthPx -
    // meter{L,R}TopLeft1x) / meterComponentSize1x:
    //   left  true pivot: master px (319.42, 348.21) -> fraction (0.495833, 0.654640)
    //   right true pivot: master px (943.33, 355.96) -> fraction (0.504598, 0.676279)
    // Both meters previously shared one fraction pair even though the two
    // dials' hubs are NOT mirror-symmetric within this box (each dial's own
    // hub-ring fit differs enough - left vs right local hub centres in
    // needle_diff/hub_fit_results.json - that a single shared fraction can
    // only ever be exactly correct for one of them); hence per-meter
    // constants rather than one shared pair going forward.
    constexpr float meterLPivotXFraction = 0.495833f;
    constexpr float meterLPivotYFraction = 0.654640f;
    constexpr float meterRPivotXFraction = 0.504598f;
    constexpr float meterRPivotYFraction = 0.676279f;

    // Legacy shared fraction pair - NO LONGER used by PluginEditor.cpp's
    // AnalogMeter construction (see meterLPivot*/meterRPivot* above), kept
    // solely because tests/gui/EditorLayoutTests.cpp still references it for
    // a generic "pivot fraction lies strictly inside (0,1)" sanity bound.
    // TODO: fold EditorLayoutTests.cpp's check onto the per-meter constants
    // and drop this pair once that test file is next touched.
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

    // Org emblem (Basilica Audio rose-window medallion) - v0.3.8 addition,
    // drawn as its own overlay by PluginEditor.cpp's paint() (NOT baked into
    // master-05, unlike roseCentre1x's thin flourish line above). Sits
    // centred in the empty plate gap between the two VU dial bezels.
    //
    // X: exactly midway between the two meter component centres -
    // meterL centre x = meterLTopLeft1x.x + meterComponentSize1x/2 = 228.5,
    // meterR centre x = meterRTopLeft1x.x + meterComponentSize1x/2 = 670.5,
    // midpoint = 449.5, rounded to 450.
    // Y: placed, then verified, against a fresh render of the dial region
    // (see build/org-emblem-zoom.png) so the medallion sits balanced in the
    // dark plate gap between the two bezels without touching either or
    // fouling the plate's diagonal glossy reflection sheen.
    // orgEmblemDiameter1x (105) is its own independently-chosen circular
    // draw size (the size class of the master-04 generation's now-unused
    // rose-emblem-v4.png overlay) - unrelated to roseDiameter1x (150) above,
    // which is a bounding-box WIDTH for a thin flourish line, not a
    // like-for-like circular medallion measurement.
    //
    // Source asset provenance: resources/gui/org-emblem.png is extracted
    // (never hand-authored) from brand/v2-plastic/raw/org.png (repo-relative
    // to the suite root, one level above this repo) - the Basilica Audio
    // org-level rose-window emblem shared across the whole plugin suite.
    // That source is a 1024x1024 RGB render: a gold/bronze rose-window
    // medallion mounted on an opaque black rounded-square (squircle) plate,
    // NOT already circularly cut out (brand/v2-plastic/cut/org.png, the
    // suite's usual pre-cut app-icon export, was rejected for this purpose
    // for exactly that reason - it carries the same squircle backing, see
    // this revision's handoff notes). Extraction: (1) circle-fit the
    // medallion's own OUTER gold-ring edge - Kasa least-squares fit on
    // ray-cast edge points (outward scan per angle for the last strong
    // gold->background transition), centre (510.0, 517.0), radius 337.0 px
    // in the source's own 1024x1024 canvas, verified against multiple
    // independent zoomed crops; (2) cut a circular disc at that radius
    // (~1px feather); (3) colour-threshold INSIDE the disc (gold/warm pixels
    // opaque, the dark tracery openwork holes AND the surrounding squircle
    // mounting plate transparent) so the Silentium faceplate itself shows
    // through the medallion's own pierced tracery once mounted - exactly
    // mirroring how the master-04 generation's rose-emblem-v4.png was itself
    // built (colour-thresholded gold vs that source's dark squircle
    // mounting plate; see that asset's own near-binary alpha histogram). A
    // fully-opaque-disc-interior variant was also produced and compared
    // side by side; the colour-thresholded variant was chosen both for this
    // precedent-consistency and because it reads as more physically mounted
    // (the openwork actually shows plate through it) at the ~105px final
    // draw size. Ray-cast circularity check on the final cutout: outer
    // radius std/mean ~=0.3% across 180 sampled angles (334-338px range,
    // mean 335.7px) - the same control-check convention used to confirm
    // rose-emblem-v4.png's own circularity (std/mean ~=1.5%).
    // orgEmblemContentDiameterFraction is the visible medallion's own
    // diameter as a fraction of org-emblem.png's full 1024px canvas
    // (672/1024) - PluginEditor.cpp's ledContentDiameterFraction/
    // ledImageDrawSize1x pair for the peak-LED sprite uses the identical
    // back-derivation convention (canvas content fraction -> draw size that
    // lands the VISIBLE content, not the full image, at the nominal on-plate
    // diameter); this constant lives here (rather than alongside the LED's
    // own pair in PluginEditor.cpp's anonymous namespace) purely because the
    // org emblem's other two geometry constants belong here.
    const juce::Point<float> orgEmblemCentre1x { 450.0f, 227.5f };
    constexpr float orgEmblemDiameter1x = 105.0f;
    constexpr float orgEmblemContentDiameterFraction = 672.0f / 1024.0f;

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
