# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **On-screen controls for the six v0.4.0 parameters** ([#33](https://github.com/basilica-audio/Silentium/issues/33)): an expansion bay below the faceplate — framed like the editor's header strip, populated from the suite's pre-existing brass component renders (no new Blender output; the approved master-05 faceplate art is untouched). Ratio and Hysteresis are 128-frame filmstrip knobs; Detector, SC Slope, Smooth Open and Release Shape are two-position filmstrip switches with gold position legends (the active option lit) and contrast-tested caption labels. Full mapping and rationale: `docs/gui-mapping.md`.
- **Keyboard accessibility for the new controls from day one**: the aux knobs reuse `KnobSlider`'s WAI-ARIA stepping (#36) and gain a Shift-drag fine mouse mode (which the plate knobs inherit too); the switches expose a checkable state plus a description announcing the current option by name, kept fresh across notification-free state changes. New/extended tests: aux switch a11y + APVTS wiring, Ratio keyboard stepping to the ∞:1 detent, aux-bay layout invariants, a rendered state-change pixel proof, legend/panel WCAG contrast, and filmstrip frame maths (test suite: 124 → 131 cases).

## [0.4.2] - 2026-07-31

Race-fix patch release.

### Fixed

- **Data race on latency reporting between the host thread and the live-Lookahead timer** (PR #34, ThreadSanitizer-confirmed). `prepareToPlay()` (host-chosen thread) and the live-Lookahead `timerCallback()` (JUCE message thread) both called `AudioProcessor::setLatencySamples()`/`getLatencySamples()` unsynchronized — the same defect class fixed across the suite in Nave, Requiem and Triptych. Fixed by serializing the two entry points behind a mutex the audio thread never takes. Red/green-verified under TSan. New regression guard: `tests/CrossThreadReprepareTests.cpp`.

## [0.4.0] - 2026-07-27

Silentium becomes a gate *and* a downward expander, opens without a click even
at 0 ms attack, and can be reconfigured live. Every existing session, preset
and rendered mix is unaffected: all six new controls default to the value that
reproduces v0.3.x exactly, and that is verified against renders captured from
an actual v0.3.0 build rather than asserted.

### Added

- **Ratio** (`ratio`, 1:1 to 20:1, default 20:1 displayed "∞ : 1 (Gate)") - a continuous downward-expander law between Threshold and Range, with hard and soft knee (the soft branch is continuous in value *and* slope at both knee edges, so widening Knee never introduces a corner). At the top of the range the gain computer takes the literal pre-v0.4.0 binary path, which is what makes the default neutral by construction rather than by numerical coincidence. Measured: the applied gain follows (Ratio − 1) dB per dB below Threshold to within 0.25 dB at 2:1, 4:1 and 8:1, and matches the closed-form law across the whole knee band to within 0.35 dB.
- **Hysteresis** (`hysteresis`, 0-12 dB, default 3 dB) - the open/close threshold gap, previously a fixed internal 3 dB constant. Measured: the gap between the opening and closing levels tracks the setting to within 1 dB at 0, 3, 6 and 12 dB.
- **Detector** (`detector`, Peak/RMS, default Peak) - a 5 ms mean-square window as an alternative to the peak follower, weighing brief excursions by the energy they actually carry. Measured: on material carrying isolated single-sample spikes over a quiet bed, the peak detector opens the gate 99 times in a second where RMS opens it not at all. Switching crossfades over 5 ms.
- **SC Slope** (`scSlope`, 12/24 dB/oct, default 12) - a Butterworth-Q-paired second section for both sidechain filters. Measured: 12.30 dB and 24.11 dB of rejection one octave below the high-pass cutoff (theory: 12.3 and 24.1). Switching crossfades over 10 ms.
- **Smooth Open** (`smoothOpen`, default off) - a moving-max plus cascaded-box smoother that shapes the opening gain trajectory inside the existing lookahead window, so a 0 ms attack opens along a continuous ramp instead of a single-sample step. Measured: the steepest step falls from 59.5 dB/sample to 0.50 dB/sample - a 119x reduction, and within 0.01 % of the triangular kernel's theoretical 2·Range/N peak - while the gate still reaches full opening exactly as the delayed transient leaves the delay line. Adds no latency at any Lookahead setting.
- **Release Shape** (`releaseShape`, Exponential/Linear, default Exponential) - a constant-dB/s close as an alternative to the program-dependent exponential approach. Measured: 600.03 dB/s against a specified 600 dB/s, with a straight-line fit of R² = 1.000 (the exponential shape fits a line at R² = 0.916).
- **Gain-reduction telemetry** - the deepest and shallowest gain applied *inside* each block (the previous block-boundary value alone hides a complete open-and-close cycle at large block sizes), plus a preallocated lock-free single-producer/single-consumer history ring that refuses to overwrite unread entries rather than silently corrupting a stalled consumer's view. No display consumes it yet; it ships tested so that work is a pure addition.
- **`presets/factory/expanderGlue.json`** ("Expander Glue") - a gentle finite-ratio expander: 2.5:1, RMS detection, linear release and a shallow -18 dB floor, for cleaning up amp noise without anything sounding gated.
- **`tests/data/`** - cross-version golden renders captured from the v0.3.0 engine, with `tests/data/README.md` documenting their provenance, the two-tier comparison, and the regeneration policy. These are what make this release's neutrality claim evidence rather than an assertion.

### Changed

- **Lookahead now applies live.** Moving it takes effect immediately - the delay crossfades equal-power to its new length over 10 ms and the new latency is reported to the host shortly after - instead of waiting for the host to re-prepare the plugin. `setLatencySamples()` is still only ever called from the message thread; the audio thread's entire contribution is one relaxed atomic store, so `processBlock()` remains allocation-free even on the block where the lookahead moves.
- **Saved state carries a schema version** (`stateVersion="2"` on the root element). A state without the attribute is treated as schema 1 and needs no transform, because every new parameter defaults to its neutral value. The version switch exists so a future schema change has a tested seam.
- **Chug Lock** gains Smooth Open. It is a 0 ms-attack preset, which is exactly the case Smooth Open exists for. This is the only deliberate voicing change in this release.
- CMake project version bumped to 0.4.0.
- Test suite grew from 87 to 114 Catch2 cases (9156 assertions).

### Not included

- **Program-adaptive auto-release** was specified for this release and is **not** implemented. The mechanism as designed sits close enough to an in-force patent (US 11,641,183) that shipping it needs a decision the project has not taken: a claim-by-claim clearance by qualified patent counsel, a written acceptance of the risk, or a replacement built on documented pre-existing dual-time-constant art. No parameter ID was reserved for it, because shipping a dead control into a frozen ID contract is worse than adding one later. `presets/factory/adaptiveTail.json` and the auto-release half of the planned Chug Lock re-voicing are deferred with it.
- **Surgical Mute was not re-voiced.** Smooth Open shapes the closing edge as well as the opening one - the moving-max window holds the open target for up to half the Lookahead time after the signal falls away, and the box cascade then spreads the close - which measurably cost that preset the "quieter between notes than Natural Decay" guarantee it exists to provide. It keeps its v0.3.x voicing; the trade-off is documented in the manual.
- The six new parameters are **fully wired and host-automatable but have no controls in the custom editor yet.** They are visible and editable in any host's generic parameter view. Adding them to the photoreal editor is a follow-up, kept separate so this release touches no GUI code at all.

## [0.3.0] - 2026-07-17

### Added

- **Photoreal skeuomorphic GUI (M3 pilot)** - the suite's first full custom editor, replacing the v0.1 functional slider/toggle layout. Built from pre-rendered Blender assets (the suite's gui-pipeline renders, copied into `resources/gui/` and embedded via BinaryData so the repo stays self-contained): a stone/gunmetal faceplate with engraved section bays, brass filmstrip knobs (128 frames, -135°..+135°), brass lever toggles with hover states, and two glass-covered analog needle VU meters (Gain Reduction + Input Level) with spring ballistics (~300 ms integration) driven by new lock-free metering atomics on the processor. See `docs/gui-preview.png` for the rendered result and `docs/gui-components.md` for the component architecture.
- **Suite-reusable GUI component family** (`src/gui/`): `FilmstripKnob` (filmstrip-backed `juce::Slider`, Shift = fine drag, double-click resets to the parameter default, mouse-wheel support), `FilmstripToggle` (4-frame `juce::Button`), `AnalogMeter` (face + vectorially rotated needle + glass overlay, unit-testable ballistics/tick-angle math), `BasilicaLookAndFeel` (gold serif labels with an engraved dual-shadow look - the interim JUCE-drawn label solution until per-control text is baked into the faceplate art), and `ImageDensity.h` (@1x/@2x asset tier selection). All Silentium-agnostic; the plugin-specific layout lives in a single coordinate table in `PluginEditor.cpp`.
- **Stepped window scaling** (100/150/200%, via a control next to the preset bar) - no free resize, because the artwork is pre-rendered at fixed density tiers. The chosen step persists in the plugin state (a plain `uiScaleStep` property on the APVTS tree) and round-trips through host session save/reload.
- **Accessibility**: all controls derive from stock `juce::Slider`/`juce::Button`, so JUCE's accessibility handlers, keyboard operation, and host parameter attachments work unchanged; accessible titles are set from parameter names, meters expose a label role, and creation order matches the visual reading order for focus traversal.
- Six new GUI test cases (76 total, up from 70): filmstrip frame-math edges, toggle frame-table mapping, meter ballistics step response and monotonic approach, tick-angle interpolation, editor construct/destroy, and an offscreen editor snapshot (written to `build/gui-preview.png`, committed as `docs/gui-preview.png`) verified non-blank.

### Changed

- `GateEngine` exposes `getCurrentGainDb()` (the gain currently applied to the main path) for the gain-reduction meter; `processBlock()` publishes it plus the pre-gate input peak level via relaxed atomics. No DSP behaviour change - all 70 pre-existing tests are unchanged and green.
- CMake project version bumped to 0.3.0.

## [0.2.0] - 2026-07-16

### Added

- **Research-derived voicing pass** (`docs/design-brief.md`, `docs/research-notes.md`): a deep-dive gap analysis against the reference class (ISP Decimator, dbx 166 series, FabFilter Pro-G, plus Nail The Mix/Fortin Amps workflow lore) identified Silentium's biggest gap versus the category as a lack of program-dependence in the gain computer, and a sidechain path that rejected hum but never emphasized the guitar pick-attack transient band. Both are addressed below. Every numeric default/range change is individually sourced in `docs/research-notes.md`; see `docs/design-brief.md`'s honesty section for what is and isn't independently verified against real hardware (nothing was - this is manual/practitioner-article-derived, not measured).
- **Program-dependent attack/release ramp**: replaces the v0.1.0 fixed dB/sample linear slope with an exponential approach whose per-sample convergence rate is calibrated so a full Range-span transition still completes in the stated Attack/Release time, but a partial transition (e.g. a brief dip that reopens before fully closing) now converges to the same absolute tolerance in proportionally less wall-clock time - the defining behaviour both ISP's "Time Vector Integration" and dbx's "AutoDynamic" cite as their headline differentiator. This is this brief's own plausible, testable mechanism, not a reproduction of either vendor's undisclosed proprietary algorithm - flagged as the riskiest change in the brief and recommended for validation against real playing.
- **SC LPF** parameter (`scLowpass`, 1000-16000 Hz, default 16000 Hz/effectively off): a second sidechain-only low-pass filter in series after SC HPF, letting the detection path be narrowed toward the documented 2-5 kHz guitar pick-attack transient band. Inert at its default, so a v0.1.0 session reproduces v0.1.0 behaviour exactly unless this new parameter is touched.
- **M2 preset system** (`.scaffold/specs/preset-system-m2.md`, copied from `basilica-audio/nave`'s pilot implementation): `src/presets/{PresetManager,PresetBar}` - factory presets (embedded via BinaryData), user presets (`~/Library/Audio/Presets/Yves Vogl/Silentium/` on macOS), save/save-as/rename/delete, single-file and zip-bank import/export, dirty-state tracking, prev/next navigation, and default-preset resolution (user Default > factory Default > built-in parameter defaults). Nine factory presets ship (`docs/presets.md`): Default, Surgical Mute, Natural Decay, Pick Attack Focus, DI-Keyed Workflow, Ambient Sustain, Chug Lock, Duck Under Lead, and Listen Check.
- **DE localisation frame** (`src/presets/Localisation.{h,cpp}`, `resources/i18n/de.txt`): every user-facing preset-bar frame string (buttons, menus, dialogs, error messages) is wrapped in `TRANS()`/`juce::translate()` and automatically switches to German when the system language is German, falling back to English otherwise. Core/DSP terminology (parameter names, units) is never translated.
- `docs/design-brief.md`, `docs/research-notes.md`, `docs/presets.md`: the full sourcing/reasoning behind this release's voicing changes and factory preset content.
- Test suite grew from 43 to 70 Catch2 cases: the seven design-brief test guarantees (SC LPF null test, a measured SC HPF/LPF band-pass curve, the program-dependent ramp proof, the Attack-floor-reaches-instantaneously proof, Hold-clamps-at-250ms, tolerant v0.1.0 state import, and a spectral proof that the Surgical Mute/Natural Decay preset pair are audibly distinct), sixteen M2 preset-system tests, and three i18n frame tests.

### Changed

- **Attack** floor lowered from 0.1 ms to 0 ms, so lookahead-assisted instantaneous (within-one-sample) gate opening is now reachable, matching documented practitioner guidance ("0.1ms to 1ms, sometimes even 0ms if your gate allows lookahead"). Default (1 ms) unchanged.
- **Hold** ceiling lowered from 500 ms to 250 ms, matching the best-documented software reference's own ceiling (v0.1.0's 500 ms had no found justification). Default (20 ms) unchanged. A hand-edited/future state with Hold above 250 ms clamps to the new ceiling on load rather than asserting or wrapping.
- Parameter count grew from 10 to 11 (`scLowpass` added); every pre-existing v0.1.0 parameter ID, range, and default not called out above is unchanged, and a v0.1.0 state tree loads with the new ID populated at its v0.2.0 default (tolerant import - no value remapping needed, since no existing ID was renamed or rescaled).
- CMake project version bumped to 0.2.0; `ICON_BIG` app icon wiring (already present from v0.1.1) verified unchanged.

### Fixed

- `docs/architecture.md`/`docs/manual.md`/`README.md` updated to describe the full v0.2.0 signal path (SC LPF, program-dependent ramp), parameter ranges, the M2 preset system, and the i18n frame.

## [0.1.1] - 2026-07-16

### Changed

- Housekeeping: new icon motif with canonical squircle cutout embedded into the plugin binary (`ICON_BIG`) and README/manual, org link sweep, heavy-music copy reframe, README pointed at GitHub Releases, and the signed tag-triggered release CI workflow added.

### Fixed

- **GateEngine: clamp/chunk `numSamples` against prepared capacity to prevent heap overflow on oversized host blocks** ([#12](https://github.com/basilica-audio/Silentium/issues/12)). `GateEngine::process()` now chunks any host block larger than the size promised to `prepare()` into pieces of at most that prepared capacity before touching `detectionBuffer`/`monoEnvelopeBuffer`, instead of trusting the host-supplied sample count directly. A block within capacity is unaffected (still exactly one iteration of the new chunking loop).

## [0.1.0] - 2026-07-14

### Added

- Project bootstrap: README, license, contributing guide, architecture and build docs, ADRs, and CI workflow.
- DSP core: initial working Silentium signal path (sidechain-filtered detection, stereo-linked peak envelope, hysteresis comparator + hold timer, dB-domain attack/release gain ramp, exact-integer lookahead delay with latency reporting) with unit tests.
- Knee parameter (0-24 dB, default 0 dB): soft-knee blend of the gain computer's target across a band centred on Threshold, in place of the original instant on/off snap; `Knee = 0 dB` reproduces v0.1 behaviour exactly, and the Hold timer still guarantees a fully open target for its whole duration regardless of Knee.
- Duck parameter (off by default): inverts the gain computer into a ducker - attenuate above Threshold instead of opening above it - reusing the same detection/hysteresis/hold/knee machinery.
- Listen parameter (off by default): routes the sidechain-filtered detection signal (post SC HPF, pre envelope-follower) directly to the output, bypassing the gain computer, for auditioning what the gate's detector hears.
- Optional external sidechain input bus (`"Sidechain"`, stereo, disabled by default): lets the detection path be keyed from another track (e.g. a kick drum or a reference DI) instead of the main input; `isBusesLayoutSupported` accepts the bus disabled, mono, or stereo independent of the main bus's own channel count, and a disabled/unconnected sidechain falls back to self-detection automatically.
- `docs/manual.md`: a full user manual (what the plugin is, where it sits in a symphonic-metal chain, signal-flow description, complete parameter reference, mixing tips).
- Broadened Catch2 suite: dedicated coverage for Knee/Duck/Listen/external sidechain (engine and processor level), bus-layout negotiation (mono/stereo main bus, sidechain enabled/disabled/mono/stereo/rejected), a 44.1-192 kHz sample-rate sweep, and a long-run (~21 s of audio) NaN/Inf stability test with continuously varying parameters/content; existing null/reference, hysteresis, latency, and state round-trip tests extended to cover the three new parameters and still pass unmodified at their defaults.

### Changed

- Parameter count grew from 7 to 10 (Knee, Duck, Listen added); all existing v0.1 parameter IDs, ranges, and defaults are unchanged.
- `GateEngine::process()` gained an optional second `sidechainBlock` parameter (default `nullptr`, fully backward compatible with the v0.1 single-block call).
- `docs/architecture.md` and `README.md` updated to describe the full v0.1.0 signal path (knee/duck/listen/sidechain), and the v0.1.0 GUI now also exposes Knee, Duck, and Listen.
