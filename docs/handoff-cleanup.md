# Handoff: cleanup after the master-05 baseline GUI revision

The master-05 baseline architecture (v0.3.4, this revision) replaces the
three prior "component composition" GUI attempts. Per the revision's
constraints, this agent could not run `rm`/`mv`/`git rm` - the files below
are still on disk and still tracked by git, just no longer referenced by
`CMakeLists.txt`'s `juce_add_binary_data` SOURCES list or by any `src/`
source file. Yves (or a follow-up agent with `git rm` permission) should
delete them once the visual sign-off on this revision lands.

## Orphaned this revision (v0.3.11 - master-extracted needle/LED, real org emblem)

Per Yves' brief: the needle-filmstrip approach was rejected twice (it was
Blender-modelled, not recovered from the approved master), the peak LED was
mis-registered onto a footer toggle, and the org emblem was the wrong
medallion (no guitar silhouette - not the actual Basilica Audio mark). All
three were replaced this revision. The five files below are STILL ON DISK
and STILL TRACKED by git (this agent could not run `rm`/`mv`/`git rm`), just
no longer referenced by `CMakeLists.txt`'s `juce_add_binary_data` SOURCES
list or by any `src/` source file:

```
resources/gui/needle-filmstrip-v2.png
resources/gui/needle-filmstrip-v2.json
resources/gui/vu-hub-occluder-v1.png
resources/gui/vu-hub-occluder-v1.json
resources/gui/led-master-diff.png
resources/gui/org-emblem.png
```

`resources/gui/vu-hub-occluder-v1.png` in particular is not just superseded
but PROVABLY dead: the new needle-from-master.png sprite's own alpha is
already transparent within ~43 master px of the pivot, wide enough that
master-05's own baked hub-cap/anchor-bar/boss art shows through there
unmodified - see `src/gui/AnalogMeter.h`'s top-of-file docs.

**Still embedded, do NOT delete:** the new `resources/gui/needle-from-master.png`,
`resources/gui/led-from-master.png`, `resources/gui/org-emblem-basilica-v1.png`,
plus their `.json` provenance siblings (`needle-from-master.json`,
`led-from-master.json` - copied to disk for documentation, never embedded,
same convention the superseded filmstrip/occluder `.json` files used).

## Orphaned PNGs in `resources/gui/` (no longer embedded)

These 22 files are no longer listed in `CMakeLists.txt`'s
`juce_add_binary_data(SilentiumBinaryData SOURCES ...)` block - they were
superseded by earlier GUI revisions (v0.3.1's filmstrip assets, v0.3.2's
single master, v0.3.3's true-component-assembly `*-v4.png` family) and are
now fully dead weight:

```
resources/gui/needle-filmstrip-v1.png
resources/gui/needle-filmstrip-v1.json
resources/gui/faceplate-silentium-v4-base.png
resources/gui/reflection-v4.png
resources/gui/tube-glow-v4.png
resources/gui/rose-emblem-v4.png
resources/gui/vu-face-v4.png
resources/gui/knob-v4.png
resources/gui/screw-v4.png
resources/gui/faceplate-silentium-v3.png
resources/gui/faceplate_silentium_v2_1800x1200.png
resources/gui/faceplate_silentium_v2_900x600.png
resources/gui/knob_brass_v2_strip_160px_128f.png
resources/gui/knob_brass_v2_strip_320px_128f.png
resources/gui/toggle_brass_v2_strip_40px_4f.png
resources/gui/toggle_brass_v2_strip_80px_4f.png
resources/gui/vu_dome_face_200x200.png
resources/gui/vu_dome_face_400x400.png
resources/gui/vu_dome_glass_200x200.png
resources/gui/vu_dome_glass_400x400.png
resources/gui/vu_dome_needle_200x200.png
resources/gui/vu_dome_needle_400x400.png
resources/gui/vu_nano_face_1024x1024.png
resources/gui/vu_nano_needle_1024x1024.png
```

**Still embedded, do NOT delete:**
`resources/gui/master-05.png`, `master-06.png`, `master-glow-dim.png` (new
this revision), `vu-needle-master-v3.png`, `led-v4.png` (both carried over
unchanged), and `button_brass_v1_normal_40x28.png` /
`button_brass_v1_normal_80x56.png` / `button_brass_v1_hover_40x28.png` /
`button_brass_v1_hover_80x56.png` (unrelated to the faceplate rework -
`BasilicaLookAndFeel.cpp` still 3-slices these for every `juce::TextButton`
the suite draws, e.g. the preset bar and this editor's scale-cycle button;
`BasilicaLookAndFeel.{h,cpp}` was outside this revision's file whitelist).

## Orphaned source files in `src/gui/`

No longer instantiated by `PluginEditor.cpp` (replaced by a plain,
transparent-draw `juce::Slider` per knob and a plain `juce::ToggleButton`
per toggle - see `src/PluginEditor.h`'s docs):

```
src/gui/RotatingImageKnob.h
src/gui/RotatingImageKnob.cpp
src/gui/FilmstripKnob.h
src/gui/FilmstripKnob.cpp
src/gui/FilmstripToggle.h
src/gui/FilmstripToggle.cpp
```

These four still compile today (CMake globs `src/**/*.{h,cpp}` rather than
listing files explicitly, so nothing broke by leaving them in place), but
they have no remaining caller anywhere in `src/`.
`tests/gui/FilmstripFrameMathTests.cpp` still unit-tests
`FilmstripToggle::frameIndexFor()`/`FilmstripKnob::frameIndexForValue()` in
isolation (pure static functions, no editor wiring) - delete that test file
too if `FilmstripKnob.{h,cpp}`/`FilmstripToggle.{h,cpp}` are removed.

**Still live, do NOT delete:** `src/gui/ImageDensity.h` -
`BasilicaLookAndFeel.cpp`'s `pickImageForWidth()` calls (for the
`button_brass_v1_*` 3-slice picking above) keep this header load-bearing
independent of the Filmstrip*/RotatingImageKnob cleanup.

## One-liner for Yves

```sh
git rm resources/gui/needle-filmstrip-v1.png resources/gui/needle-filmstrip-v1.json \
  resources/gui/faceplate-silentium-v4-base.png resources/gui/reflection-v4.png \
  resources/gui/tube-glow-v4.png resources/gui/rose-emblem-v4.png resources/gui/vu-face-v4.png \
  resources/gui/knob-v4.png resources/gui/screw-v4.png resources/gui/faceplate-silentium-v3.png \
  resources/gui/faceplate_silentium_v2_1800x1200.png resources/gui/faceplate_silentium_v2_900x600.png \
  resources/gui/knob_brass_v2_strip_160px_128f.png resources/gui/knob_brass_v2_strip_320px_128f.png \
  resources/gui/toggle_brass_v2_strip_40px_4f.png resources/gui/toggle_brass_v2_strip_80px_4f.png \
  resources/gui/vu_dome_face_200x200.png resources/gui/vu_dome_face_400x400.png \
  resources/gui/vu_dome_glass_200x200.png resources/gui/vu_dome_glass_400x400.png \
  resources/gui/vu_dome_needle_200x200.png resources/gui/vu_dome_needle_400x400.png \
  resources/gui/vu_nano_face_1024x1024.png resources/gui/vu_nano_needle_1024x1024.png \
  src/gui/RotatingImageKnob.h src/gui/RotatingImageKnob.cpp \
  src/gui/FilmstripKnob.h src/gui/FilmstripKnob.cpp \
  src/gui/FilmstripToggle.h src/gui/FilmstripToggle.cpp \
  tests/gui/FilmstripFrameMathTests.cpp
```

Note: run this from the repo root, then re-run
`cmake --build build --target Tests Silentium_Standalone --parallel 4 && ctest --test-dir build --output-on-failure`
to confirm the glob-based `SharedCode`/`Tests` source lists still pick up
cleanly with those six files gone (they should - nothing else references
them, see above).
