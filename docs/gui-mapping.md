# M3 photoreal GUI — parameter mapping (master-05 design)

Follows the suite convention of `docs/gui-mapping.md` in aureate/tenebrae/
apotheosis: one authoritative table of which APVTS parameter is controlled
by which physical control, and where every control's geometry comes from.

## Design source

The approved **master-05 reference chain**
(`.scaffold/gui-assets/faceplate-silentium-v3/`, repo-relative to the suite
root): `master-05.png` is the sole baked faceplate (obsidian plate, brass
bevel, corner screws, rose flourish, both VU dial faces, all 9 knobs at
rest, both toggles UP, tube-vent grilles), with `master-06.png` (toggles
DOWN) and `master-glow-dim.png` (dim vent glow) contributing small targeted
crops. Geometry: `src/PluginEditorLayout.h`, re-measured against master-05
by `analysis/measure_master_05.py` — see that header's docs.

## Faceplate controls (baked art + invisible hit surfaces)

| Control | Parameter | Kind |
|---|---|---|
| Knob row 1 | `threshold`, `attack`, `hold`, `release`, `range` | `KnobSlider` (transparent) over baked art + rotating inner disc overlay |
| Knob row 2 | `lookahead`, `scHighpass`, `scLowpass`, `knee` | same |
| Footer toggles | `duck`, `listen` | invisible `juce::ToggleButton` + master-06 crop swap |
| Left dial | Gain Reduction meter | `AnalogMeter` (display only) |
| Right dial | Input Level meter | `AnalogMeter` (display only) |

## Aux control bay (issue #33 — the v0.4.0 parameters)

The v0.4.0 DSP wave added six parameters after master-05 was approved; the
baked knob bay has no positions for them. Instead of a plate re-render
(issue #33's originally sketched path), they live in a **JUCE-drawn
expansion bay below the plate**, framed exactly like the editor's existing
header strip (same `BasilicaLookAndFeel` panel gradient and thin gold rule,
same 6 px gap rhythm) and populated from the suite's **pre-existing brass
component render family** — `knob_brass_v2_strip_160px_128f.png` and
`toggle_brass_v2_strip_{40px,80px}_4f.png`, the filmstrips the v0.3.x
component-assembly generation shipped. **No new Blender renders of any
kind.** The approved master-05 art is untouched.

Column order (left → right, also the keyboard focus order):

| # | Control | Parameter | Kind | Positions (off / on) |
|---|---|---|---|---|
| 1 | Ratio | `ratio` | `KnobSlider`, filmstrip mode | — |
| 2 | Hysteresis | `hysteresis` | `KnobSlider`, filmstrip mode | — |
| 3 | Detector | `detector` | `FilmstripSwitch` | Peak / RMS |
| 4 | SC Slope | `scSlope` | `FilmstripSwitch` | 12 dB/oct / 24 dB/oct |
| 5 | Smooth Open | `smoothOpen` | `FilmstripSwitch` | Off / On |
| 6 | Release Shape | `releaseShape` | `FilmstripSwitch` | Exponential / Linear |

**Rationale for the split**: Ratio and Hysteresis are continuous voicing
controls (knobs); the other four are two-state mode selections, which a
two-position lever represents honestly — a two-entry
`AudioParameterChoice` maps exactly onto toggle state 0/1 through the
stock `ButtonAttachment` (JUCE 8.0.14, `juce_ParameterAttachments.cpp`).

Each switch carries editor-drawn **position legends** (the ON option's name
above the lever — lever up = on, matching the plate's own baked toggle
convention — the OFF option's below; the active option at full legend
gold, the inactive one dimmed). Each column has a caption label using
`BasilicaLookAndFeel::drawLabel()`'s contrast-guaranteed backing-chip
styling.

### Accessibility (day-one contract, tested)

- Every aux control is keyboard-focusable and in the visual-reading-order
  focus chain (creation order: plate top-to-bottom, then the bay
  left-to-right).
- Knobs: WAI-ARIA stepping via `KeyboardSteps.h` (Arrow 1 %, Shift+Arrow
  0.1 %, PageUp/Down 10 %, Home/End extremes), unit-suffixed accessible
  value strings, Shift-drag fine mouse mode.
- Switches: parameter-named accessible title, checkable/checked state, and
  a description announcing the **current option by name** (kept fresh on
  every state change, including notification-free programmatic ones — see
  `src/gui/FilmstripSwitch.h`).
- Caption labels are excluded from the accessibility tree (redundant with
  the controls' own titles).
- Contrast: caption chips reuse the tested label pair; the active legend
  gold clears WCAG 1.4.3 (4.5:1) against the panel band's brightest tone —
  asserted in `tests/gui/BasilicaLookAndFeelContrastTests.cpp` against the
  same colour accessors the editor renders with.

### Asset economics

The knob strip ships **@1x only**: the largest draw size any scale step
produces is 96 px (48 px knob at the 200 % window step), well under the
strip's 160 px native frame — the @2x strip (~15 MB) stays on disk,
unembedded. The toggle strips ship both tiers (the 34 px switch draws at
68 px at 200 %, past the @1x strip's 40 px frame). See `CMakeLists.txt`'s
asset docs.

## Non-goals

- No re-render of the master-05 chain; the bay is deliberately a framed
  JUCE-drawn section, the same visual world as the header strip.
- No third density tier (see `docs/gui-components.md`'s known limitation on
  200 %+ displays).
