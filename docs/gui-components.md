# M3 GUI pilot - component notes

Silentium is the M3 GUI pilot for the Basilica Audio suite: the first
photoreal skeuomorphic editor, built from a small suite-reusable component
family under `src/gui/`. This document is the "why" behind the code
comments, for whoever ports this pattern to the next plugin.

## Components (`src/gui/`)

| Component | Base class | Backs onto |
|---|---|---|
| `FilmstripKnob` | `juce::Slider` (RotaryVerticalDrag) | `knob-brass-v1` 128-frame filmstrip |
| `FilmstripToggle` | `juce::Button` | `toggle-brass-v1` 4-frame filmstrip |
| `AnalogMeter` | `juce::Component` + `juce::Timer` | `vu-brass-v1` face/needle/glass |
| `BasilicaLookAndFeel` | `juce::LookAndFeel_V4` | interim JUCE-drawn label styling |
| `ImageDensity.h` | (free functions) | @1x/@2x tier selection shared by all three |

All four are Silentium-agnostic: they take asset `juce::Image`s and generic
config (frame counts, titles, tick tables) through their constructors, not
Silentium's parameter IDs. `PluginEditor.cpp` is the only file that knows
about Silentium's actual 9-knob/2-toggle/2-meter parameter set.

## Layout table

`PluginEditor.cpp`'s anonymous namespace holds ONE table of `KnobLayoutEntry`
(parameter ID, label text, grid column/row) and one of `ToggleLayoutEntry`,
both expressed in the faceplate's base @1x (900x600) pixel coordinates. The
same table positions both the `FilmstripKnob`/`FilmstripToggle` control AND
its `juce::Label` caption - so when the faceplate gets a real per-control
label pass baked into the art (Blender text objects, not image-gen - see
`.scaffold/gui-assets/faceplate-silentium-v1/README.md`), the follow-up work
is "hide/remove these `juce::Label`s", not "recompute a layout".

The faceplate bay rectangles themselves (`headerBay1x`, `meterLBay1x`,
`meterRBay1x`, `controlBay1x`, `auxBay1x`) were derived from
`.scaffold/gui-assets/render_faceplate.py`'s actual Blender world
coordinates via that script's own orthographic camera math, then
cross-checked visually against `faceplate_silentium_preview.png`. This is
NOT pixel-measured/verified the way the asset READMEs' own numbers are (see
"Known limitations" below) - a future pass that needs pixel-perfect
alignment should measure the rendered PNG directly (PIL/numpy in a venv,
matching the asset pipeline's own verification method) rather than trusting
the projection math as-is.

## Known limitations / open ends for the next M3 pass

- **`vu-brass-v1/README.md`'s stated pivot pixel is wrong.** The README
  claims the needle pivot lands at pixel (0.5w, 0.86h); both the generator
  script's own world-to-pixel math (world (0, -0.40) on a canvas spanning
  world y [-0.9, 0.9] -> fraction 13/18 ≈ 0.722) and a direct pixel
  measurement of the shipped needle PNG (ImageMagick alpha bounding box
  `77x62+170+140` on the 480x270 layer -> hub-disc centre ≈ (239.5, 194.5)
  -> fraction (0.499, 0.720)) agree on ≈0.722 instead. `AnalogMeter` uses
  the measured/derived 13/18. The README should be corrected upstream in
  the gui-pipeline repo so the next plugin doesn't inherit the bad number.
- **`vu-brass-v1`'s dial content only occupies the central half of its
  canvas** (transparent margins all around; the face/glass planes are
  1.6x0.9 world units on a 3.2x1.8 canvas). `AnalogMeter` exposes this as
  `contentFractionOfCanvas` and the editor sizes each meter 2x its engraved
  bay, centred, so the visible dial fills the bay. A future asset
  re-render could instead crop the canvas to the dial for less overhang.
- **`toggle-brass-v1/README.md`'s prose frame formula is wrong.** It says
  "frame = state * 2 + hovered", but its own frame TABLE only matches
  "frame = hovered * 2 + state" (`FilmstripToggle::frameIndexFor`, unit
  tested). Not a blocking issue - the code uses the table, not the prose -
  but worth fixing the README text in a follow-up so nobody copies the wrong
  formula into a future plugin.
- **The preset bar (`src/presets/PresetBar.h`) is not re-skinned** with
  photoreal assets in this pass - it lives in a plain strip above the
  faceplate art, using the same stock `juce::TextButton`s M2 shipped
  (`BasilicaLookAndFeel` styles its `juce::Label`s suite-wide, which gives it
  *some* visual consistency for free, but its buttons are untouched). A
  themed preset-bar treatment is a reasonable follow-up once more of the
  suite has adopted this GUI pattern.
- **Metering choice: Gain Reduction + Input Level**, not the
  `faceplate-silentium-v1/README.md`'s own suggested "Gain Reduction +
  Output" pairing - the M3 task brief asked for input level explicitly (so
  a user can see what's driving the detector, alongside the gate's action on
  it). Either reading is meaningful for a noise gate; this is a deliberate
  choice, not an oversight, but flagging the deviation from the asset
  README's own wording for anyone reconciling the two documents later.
- **Stepped window scaling (100/150/200%) always redraws from the same
  source images** (`ImageDensity.h` just picks @1x vs @2x by target pixel
  size); there is no @3x/@4x tier, so 200% on a very high-density display
  still only has @2x source resolution to work with. Acceptable for this
  pilot; a future pass could add a third density tier if that visibly
  matters on 200%+ displays.
