# Silentium — tight lookahead noise gate (guitar)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Basilica Audio** plugin suite — sacred-architecture DSP for heavy music (`github.com/basilica-audio`).

## What this is
Silentium is the "tight lookahead noise gate (guitar)" member of the suite. AU / VST3 / Standalone, JUCE 8.

## Status (v0.4.1 — v0.4.0 DSP + restored approved photoreal GUI)
Core DSP + v0.2.0's voicing pass + v0.4.0's additions (downward-expander Ratio, Hysteresis, RMS Detector mode, 24dB/oct SC slope, click-free lookahead Smooth Open, linear Release Shape, live lookahead reconfiguration, gain-reduction min/max telemetry) working, **122 Catch2 tests green** (DSP null/reference, hysteresis, latency incl. live reconfiguration, state round-trip incl. schema versioning, sample-rate sweep 44.1-192kHz, mono/stereo/sidechain bus configs, long-run NaN/Inf stability, bus-layout negotiation, design-brief guarantees, M2 preset system, i18n frame, plus the M3 GUI's own regression suite: snapshot, layout, accessibility, ballistics, contrast, filmstrip-frame-math). GUI is the approved photoreal skeuomorphic editor (master-05/06/glow-dim baseline faceplate, master-diff-extracted needle/LED/org-emblem, rotating knob-discs on FilmstripKnob/RotatingImageKnob) - **exactly 9 knobs + 2 toggles are wired**, matching the baked faceplate render. v0.4.0's six new parameters (Ratio, Hysteresis, Detector, SC Slope, Smooth Open, Release Shape) have **no dedicated knob/control yet** - same precedent as v0.2.0's SC LPF - and are automation/preset-only until a faceplate re-render adds them (see the tracking issue). CI (macOS + Windows, pluginval strictness 10 + auval) green as of the last verified push. No signing yet (roadmap M4). Open work is tracked in this repo's GitHub **milestones/issues**.

v0.4.1 fixed a regression: PR #28 (v0.4.0's DSP work) was branched before the approved-GUI merge (#22) and its squash-merge silently reverted `main`'s GUI files to the pre-#22 v1 pilot editor for one release cycle (v0.4.0's shipped binaries carried the old GUI). Restored from `feat/v0.3.2-vu-nano` (Yves' visual sign-off + post-sign-off refinements) on top of v0.4.0's DSP, unchanged.

## DSP
GateEngine (src/dsp/GateEngine.{h,cpp}) implements a lookahead hysteresis noise gate: a scratch copy of the input (or an external sidechain input, if the host has one enabled/connected - see PluginProcessor::processBlock's getBusBuffer slicing) is high-passed by a sidechain-only IIR HPF (juce::dsp::IIR via ProcessorDuplicator, 20-500 Hz, Butterworth Q), then low-passed by a second sidechain-only IIR LPF (v0.2.0, 1-16kHz, default 16kHz/off), stereo-linked via per-sample max(|channel|), then peak-tracked by juce::dsp::BallisticsFilter (fixed 0.3ms/15ms internal ballistics, distinct from the user-facing Attack/Release). A comparator uses two thresholds - Threshold (open) and Threshold-3dB (close, fixed internal hysteresis) - plus a per-sample-retriggered Hold countdown, feeding an `openness` value in [0,1]: at Knee=0dB this snaps 0/1 exactly like v0.1; Knee>0dB blends it via smoothstep across a band centred on Threshold (Hold always forces openness=1 regardless of Knee, so it still bridges gaps as before). Duck, if enabled, inverts openness after that stage (attenuate above Threshold instead of opening above it). `targetGainDb = jmap(openness, Range, 0dB)` drives a program-dependent exponential attack/release gain ramp (v0.2.0 - replaces v0.1's fixed dB/sample slope; see docs/design-brief.md and docs/architecture.md's "Program-dependent attack/release ramp" section). The main signal is delayed by an exact-integer juce::dsp::DelayLine<float, None> sized from Lookahead (a structural parameter re-derived only on prepare()) and multiplied by that gain, unless Listen is enabled, in which case the SC-filtered detection signal is output directly instead; Lookahead is reported as the plugin's total latency via setLatencySamples(). Range=0dB with Knee=0/Duck=off collapses the whole engine to a pure delay, which is exploited as the null-test reference.

## Presets & i18n (v0.2.0)
src/presets/{PresetManager,PresetBar,Localisation} implement the suite-wide M2 preset system (.scaffold/specs/preset-system-m2.md), copied verbatim from basilica-audio/nave's pilot implementation. Nine factory presets (presets/factory/*.json, embedded via BinaryData) documented in docs/presets.md. resources/i18n/de.txt provides the German preset-bar frame translation (parameter/DSP terms never translated).

## Build & test
```sh
export CPM_SOURCE_CACHE="$HOME/.cache/CPM"      # shared JUCE 8.0.14 + Catch2 cache
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target Tests Silentium_Standalone --parallel 4
ctest --test-dir build --output-on-failure
```
Release/universal + pluginval + auval run in CI, not locally.

## Conventions & guardrails
- JUCE 8.0.14 via CPM · C++20 · AGPLv3 · Pamplejuce `SharedCode` pattern · manufacturer `Yvsv`, plugin code `Slnt`, `com.yvesvogl.silentium`.
- **Real-time safety:** no alloc/lock/file-IO/logging on the audio thread; allocate in `prepareToPlay`; `reset()` clears all state; `ScopedNoDenormals`; smoothed params; report latency via `setLatencySamples` where the chain adds any.
- **DryWetMixer gotcha (JUCE 8.0.14):** prime `setWetMixProportion(mix)` before `reset()` in `prepare()` (else it ramps from 100% wet). See sibling `overture`.
- **`main` is protected** — no direct commits; feature branch + PR, green CI required (Conventional Commits). New DSP needs tests (null/reference, NaN/Inf sweep, state round-trip, latency).

## Roadmap
GitHub milestones (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0) + issues. Read with `gh issue list --repo basilica-audio/silentium`.

## Suite context
Style references: sibling `basilica-audio/overture` and `basilica-audio/crypta`. The suite: overture, tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis, crypta.
