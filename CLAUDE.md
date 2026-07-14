# Silentium — tight lookahead noise gate (guitar)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Metal up your ass** symphonic-metal plugin suite (`github.com/metal-up-your-ass`).

## What this is
Silentium is the "tight lookahead noise gate (guitar)" member of the suite. AU / VST3 / Standalone, JUCE 8.

## Status (v0.1.0 — M1 DSP completion done)
Core DSP + M1 additions (soft knee, duck mode, listen mode, external sidechain input bus) working, **~42 Catch2 tests green** (null/reference, hysteresis, latency, state round-trip, sample-rate sweep 44.1-192kHz, mono/stereo bus configs, long-run NaN/Inf stability, bus-layout negotiation). CI (macOS + Windows, pluginval strictness 10 + auval) green as of the last verified push; the M1 additions widen the bus layout surface (external sidechain), which is the highest-risk area for pluginval/auval - see docs/architecture.md's "External sidechain input" section for the exact `isBusesLayoutSupported`/`getBusBuffer` contract. GUI is a functional v0.1 slider/toggle editor (custom LookAndFeel is roadmap M3). No signing yet (roadmap M4). Open work is tracked in this repo's GitHub **milestones/issues**.

## DSP
GateEngine (src/dsp/GateEngine.{h,cpp}) implements a lookahead hysteresis noise gate: a scratch copy of the input (or an external sidechain input, if the host has one enabled/connected - see PluginProcessor::processBlock's getBusBuffer slicing) is high-passed by a sidechain-only IIR HPF (juce::dsp::IIR via ProcessorDuplicator, 20-500 Hz, Butterworth Q), stereo-linked via per-sample max(|channel|), then peak-tracked by juce::dsp::BallisticsFilter (fixed 0.3ms/15ms internal ballistics, distinct from the user-facing Attack/Release). A comparator uses two thresholds - Threshold (open) and Threshold-3dB (close, fixed internal hysteresis) - plus a per-sample-retriggered Hold countdown, feeding an `openness` value in [0,1]: at Knee=0dB this snaps 0/1 exactly like v0.1; Knee>0dB blends it via smoothstep across a band centred on Threshold (Hold always forces openness=1 regardless of Knee, so it still bridges gaps as before). Duck, if enabled, inverts openness after that stage (attenuate above Threshold instead of opening above it). `targetGainDb = jmap(openness, Range, 0dB)` drives a dB-domain attack/release gain ramp. The main signal is delayed by an exact-integer juce::dsp::DelayLine<float, None> sized from Lookahead (a structural parameter re-derived only on prepare()) and multiplied by that gain, unless Listen is enabled, in which case the SC-filtered detection signal is output directly instead; Lookahead is reported as the plugin's total latency via setLatencySamples(). Range=0dB with Knee=0/Duck=off collapses the whole engine to a pure delay, which is exploited as the null-test reference.

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
GitHub milestones (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0) + issues. Read with `gh issue list --repo metal-up-your-ass/silentium`.

## Suite context
Style references: sibling `metal-up-your-ass/overture` and `metal-up-your-ass/twist-your-guts`. The suite: overture, tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis, twist-your-guts.
