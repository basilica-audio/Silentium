# GUI component notes (v0.3.1 visual overhaul)

Silentium is the GUI pilot for the Basilica Audio suite: the first photoreal
skeuomorphic editor, built from a small suite-reusable component family under
`src/gui/`. This document is the "why" behind the code comments, for whoever
ports this pattern to the next plugin. The v0.3.1 pass replaced every asset
family with mock-graded v2 renders after the v0.3.0 assets were rejected in
design review; the component ARCHITECTURE below is unchanged from the pilot.

## Components (`src/gui/`)

| Component | Base class | Backs onto |
|---|---|---|
| `FilmstripKnob` | `juce::Slider` (RotaryVerticalDrag) | `knob-brass-v2` 128-frame filmstrip |
| `FilmstripToggle` | `juce::Button` | `toggle-brass-v2` 4-frame filmstrip (housed switch) |
| `AnalogMeter` | `juce::Component` + `juce::Timer` | `vu-nano-v1` circular face/needle (v0.3.2; was `vu-dome-v1`) |
| `BasilicaLookAndFeel` | `juce::LookAndFeel_V4` | EB Garamond type system + `button-brass-v1` 3-slice preset-bar caps |
| `ImageDensity.h` | (free functions) | @1x/@2x tier selection shared by all |

All are Silentium-agnostic: they take asset `juce::Image`s and generic config
(frame counts, titles, tick tables) through their constructors, not
Silentium's parameter IDs. `PluginEditor.cpp` is the only file that knows
about Silentium's actual 9-knob/2-toggle/2-meter parameter set.

## Layout table & the engraved-label contract

`src/PluginEditorLayout.h` (`slnt::layout`) holds the single @1x geometry
table; `PluginEditor.cpp`'s anonymous namespace maps parameter IDs onto its
grid. Since v0.3.1 every static caption (title, knob labels, DUCK/LISTEN) is
ENGRAVED into the faceplate PNG itself - EB Garamond gold-inlay lettering
rendered by `.scaffold/gui-assets/render_faceplate_silentium_v2.py`, whose
label positions are derived from the SAME integer-division grid math
`resized()` uses. Three artifacts carry the same numbers and must change
together in one commit: `PluginEditorLayout.h`, the render script, and
`faceplate-silentium-v2/layout-manifest.json`. There are no caption
`juce::Label`s in the editor any more; accessible names live on the controls
themselves (`setTitle`), unchanged.

## v0.3.2 meter asset notes

- **Meters are the nano-banana-approved Weston-style VU face** (`vu-nano-v1`,
  promoted from Silentium as the reusable Basilica Audio VU component):
  measured (not hand-tuned) 9-tick arc (-20 dB..+3 dB), amber-backlit dial,
  needle pivot at canvas fraction (0.499838, 0.706359), visible bezel =
  face_diameter_px/1024 (~79% of canvas, `contentFractionOfCanvas`) - see
  `.scaffold/gui-assets/vu-nano-v1/README.md`. Single 1024px tier per layer
  (no @1x/@2x pair, no separate glass decal - the face's own baked highlight
  carries that read), and the needle is authored at rest pointing straight
  up (0 deg), so `AnalogMeter` applies the measured tick angle directly as
  the rotation with no rest-angle subtraction (unlike the superseded
  `vu-dome-v1` family). The tick table in `AnalogMeter.cpp` is copied from
  `vu-metadata.json`'s `tick_angle_at_db` and matches ONLY this face art.

## v0.3.1 asset notes

- **`ImageDensity.h` never upscales any more** (tier margin 1.25 -> 1.0):
  any draw width beyond the @1x native size selects the @2x tier. With the
  stepped 100/150/200% scaling this means every tier choice is a downscale
  or exact hit.
- **Knob strips have no baked shadow** (the v1 shadow-catcher wash rendered
  as a gray square behind every knob on the plate); the near-black plate
  makes contact shadows visually irrelevant, so none are baked anywhere.
- **The preset bar is fully re-skinned** via `BasilicaLookAndFeel`: brass
  3-slice button caps (`button-brass-v1`, normal/hover baked + runtime
  pressed darken), the preset name in a recessed dark display window
  (selected by the `presetNameDisplay` componentID set in `PresetBar.cpp`),
  EB Garamond button text with a WCAG-checked contrast pair, and focus rings
  restored on the image-drawn path. `PresetBar` itself contains no drawing
  code, so the reskin propagates to siblings copying `src/presets/` +
  `src/gui/` verbatim.
- **Typography**: EB Garamond Regular + SemiBold embedded via BinaryData
  (`resources/fonts/`, OFL license file alongside). `BasilicaLookAndFeel::
  getSerifFont()` is the single access point; the platform serif is only a
  defensive fallback if the embedded face ever failed to parse.

## Known limitations / open ends

- **Metering choice: Gain Reduction + Input Level**, not "Gain Reduction +
  Output" - the M3 task brief asked for input level explicitly (so a user
  can see what's driving the detector, alongside the gate's action on it).
  Deliberate, not an oversight.
- **No engraved meter captions**: the dials carry only the "VU" wordmark -
  the plate has no "GAIN REDUCTION"/"INPUT" lettering (no vertical space
  between the header and control bays at the current meter diameter).
  Meters keep their accessible titles; a future pass could add small
  engraved captions if the bay geometry is revisited.
- **Stepped window scaling (100/150/200%) has no @3x/@4x tier**, so 200% on
  a very high-density display still only has @2x source resolution to work
  with. Acceptable for this pass; add a third density tier if that visibly
  matters.
- The v0.3.0-era notes about `vu-brass-v1`/`toggle-brass-v1` README errata
  are obsolete - those asset families are retired by this pass (the errata
  remain documented in the gui-pipeline repo's history).
