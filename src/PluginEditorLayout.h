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
// v0.3.3 (this revision): TRUE COMPONENT ASSEMBLY, per Yves' final art
// direction - the bare baseline plate is
// .scaffold/gui-assets/faceplate-silentium-v3/master-04-empty.png
// ("master-04" generation, 1264x848), with every control/decoration
// composited as its own standalone master-reference asset on top (see
// PluginEditor.cpp's docs) rather than one single baked master render.
// Every constant below was measured DIRECTLY against master-04-empty.png
// (NOT copied from the older faceplate-metadata.json, which was measured
// against the PRIOR "master-03" render generation and does not match this
// one - master-04 uses noticeably different dial/knob proportions and
// spacing) - see this revision's PR/handoff notes for the measurement
// methodology (a mix of colour-threshold blob centroids, an algebraic
// circle fit, and direct visual pixel-grid readings, cross-checked against
// each other via a full Python/PIL composite preview before being ported to
// this file). Every constant is this master's own pixel geometry scaled by
// plateWidth1x / masterCanvasWidthPx down to this @1x table - re-derive all
// of them together if the master render is ever replaced.
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

    // Each AnalogMeter's component bounds are the exact box the dial FACE
    // asset (vu-face-v4.png) is drawn into, scaled/positioned so the face's
    // own outer bezel lands on the plate's dial void (measured: bezel
    // centre (410,252)/(854,252) master px, diameter 248 master px; the
    // face asset's own bezel occupies ~88.4% of its 1024x1024 canvas,
    // canvas-centred - see PluginEditor.cpp's makeMeterAssets() docs for the
    // exact fit maths). The needle/glow/LED pivot is NOT the box centre
    // (unlike v0.3.2's pivot-centred convention) - see meterPivotXFraction/
    // meterPivotYFraction below, matched to the face asset's own measured
    // hub position.
    constexpr int meterComponentSize1x = 200;
    const juce::Point<int> meterLTopLeft1x { 191, 78 };
    const juce::Point<int> meterRTopLeft1x { 507, 78 };
    constexpr float meterPivotXFraction = 0.5f;
    constexpr float meterPivotYFraction = 0.630f;

    // Control-bay knobs: a STAGGERED/brick layout baked into the master
    // render (row 2 sits offset right of row 1, not a straight 5-col/2-row
    // grid) - explicit per-knob centres rather than derived grid cells.
    // Row order/count matches PluginEditor.cpp's knobLayout table (row 1:
    // Threshold, Attack, Hold, Release, Range; row 2: Lookahead, SC HPF,
    // SC LPF, Knee).
    constexpr int knobRow1Y1x = 338;
    constexpr int knobRow2Y1x = 427;
    constexpr int knobDiameter1x = 71;

    constexpr std::array<int, 5> knobRow1X1x { 267, 356, 445, 534, 623 };
    constexpr std::array<int, 4> knobRow2X1x { 310, 399, 488, 577 };

    // Two footer toggles (Duck, Listen), same Y, explicit X centres. Drawn
    // by the older FilmstripToggle component (toggle_brass_v2_strip_*.png)
    // - see PluginEditor.cpp's docs for why this element, alone, was NOT
    // ported to a fresh master-ref asset in this revision (no master-ref
    // toggle render exists yet).
    constexpr int toggleY1x = 502;
    constexpr int toggleSize1x = 39;
    constexpr std::array<int, 2> toggleX1x { 404, 475 };

    // Rose emblem (rose-emblem-v4.png), centred in the gap between the two
    // dials.
    const juce::Point<int> roseCentre1x { 450, 171 };
    constexpr int roseDiameter1x = 107;
    constexpr float roseContentDiameterFraction = 684.0f / 1024.0f;

    // Four corner screws (screw-v4.png).
    const std::array<juce::Point<int>, 4> screwCentres1x {
        juce::Point<int> { 84, 83 },  // top-left
        juce::Point<int> { 834, 83 }, // top-right
        juce::Point<int> { 84, 520 }, // bottom-left
        juce::Point<int> { 835, 513 } // bottom-right
    };
    constexpr int screwDiameter1x = 31;
    constexpr float screwContentDiameterFraction = 132.0f / 1024.0f;

    // Tube-vent glow banks: two independently-flickering tube-glow
    // instances per side (Yves' brief: "2 independent noise sources per
    // meter"), composited BEHIND the vent grille art that's already baked
    // into master-04-empty.png (see PluginEditor.cpp's docs for why the
    // separate vent-empty-master-ref.png overlay was deliberately NOT
    // re-composited on top - redundant/misalignment-risk against the
    // already-present baked grille). Positions are the two tube slots in
    // the baked grille that already show glass/glow detail.
    const juce::Point<int> ventLBankCentre1x { 142, 385 };
    const juce::Point<int> ventRBankCentre1x { 758, 385 };
    constexpr int tubeGlowInstanceXOffset1x = 11; // +/- from the bank centre, the two tube slots
    constexpr int tubeGlowDrawWidth1x = 32;
    constexpr int tubeGlowDrawHeight1x = 150;

    constexpr int topStripHeight1x = 32;
    constexpr int topStripGap1x = 6;
    constexpr int scaleButtonWidth1x = 64;

    constexpr int baseEditorWidth = plateWidth1x;
    constexpr int baseEditorHeight = topStripHeight1x + topStripGap1x + plateHeight1x;

    constexpr std::array<float, 3> scaleSteps { 1.0f, 1.5f, 2.0f };
}
