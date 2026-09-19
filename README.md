<p align="center"><img src="docs/assets/icon.png" alt="Silentium icon" width="160"/></p>

# Silentium

*Silence between the storms — a tight lookahead noise gate for palm-muted rhythm.*

[![CI](https://github.com/basilica-audio/silentium/actions/workflows/ci.yml/badge.svg)](https://github.com/basilica-audio/silentium/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

> **Work in progress.** Silentium is pre-1.0 and under active development. Binaries for macOS and Windows are available from the [Releases](../../releases) page (macOS builds are signed with a Developer ID certificate, notarized and stapled); building from source works too. Expect breaking changes until v1.0.0 ships (see [Roadmap](#roadmap)).

<!-- ==BEGIN BODY== (plugin engineer: replace this block with What it is / Features / Signal flow / Roadmap) -->
## What it is

Silentium is a tight, lookahead noise gate built on JUCE 8, aimed at killing amp hiss/hum in the silence between palm-muted chugs: it detects transients on a sidechain-filtered copy of the input (so hum/rumble can't falsely hold it open), opens fast with a lookahead head start so it never clips the leading edge of a pick attack, and uses two separate open/close thresholds so a signal hovering near the threshold can't chatter the gate open and closed.

## Features

- **Threshold** - open threshold on the (sidechain-filtered) envelope, -80 dB to 0 dB (default -40 dB)
- **Hysteresis** - the gap between the open and close thresholds, 0 - 12 dB (default 3 dB); 0 dB deliberately allows the two to coincide for the tightest possible close on very clean material, wider values prevent chatter on a signal hovering near Threshold
- **Detector** - Peak (default) or RMS envelope follower driving the gate; RMS is a fixed 5 ms mean-square window
- **Attack** - program-dependent ramp time from the Range floor up to unity once the envelope opens the gate, 0 - 50 ms (default 1 ms)
- **Hold** - minimum time the gate stays open once opened, retriggered continuously while the envelope stays above the close threshold, 0 - 250 ms (default 20 ms)
- **Release** - program-dependent ramp time back down to the Range floor once Hold has elapsed, 5 - 500 ms (default 80 ms)
- **Release Shape** - Exponential (default, program-dependent) or Linear (constant dB/s) release curve
- **Range** - floor attenuation applied while closed, -80 dB to 0 dB (default -60 dB); 0 dB means the gate never attenuates at all
- **Ratio** - downward-expander law between Threshold and Range, 1:1 through 20:1, defaulting to the top of the range, which displays as "∞ : 1 (Gate)" - the classic hard-gate behaviour; lower settings turn Silentium into a gentler expander
- **Lookahead** - delays the main signal 0 - 20 ms (default 5 ms) so the gate can start opening just before a transient arrives; reported to the host as this plugin's total latency
- **Smooth Open** - a continuous opening ramp inside the lookahead window instead of a hard trigger, off by default; adds no extra latency
- **SC HPF** - sidechain-only high-pass, 20 - 500 Hz (default 80 Hz), keeps hum/rumble from falsely holding the gate open; never applied to the main signal
- **SC LPF** - sidechain-only low-pass in series after SC HPF, 1000 - 16000 Hz (default 16000 Hz/off), narrows the detection band toward the guitar pick-attack transient region
- **SC Slope** - order of both sidechain filters (SC HPF and SC LPF), 12 dB/oct (default) or 24 dB/oct
- **Knee** - soft-knee width around Threshold, 0 - 24 dB (default 0 dB); 0 dB is the classic hard-knee snap, wider values blend the gain smoothly across the band
- **Duck** - inverts the gain computer into a ducker (attenuate above Threshold instead of opening above it), off by default
- **Listen** - routes the sidechain-filtered detection signal to the output for auditioning what the gate hears, off by default
- **External sidechain input** - an optional second input bus (disabled by default) lets the detection path be keyed from another track instead of the main input
- **Presets** - ten factory presets plus full user preset save/load/import/export, with a German-localised preset bar frame (falls back to English)
- Full state save/recall via `AudioProcessorValueTreeState`, tolerant of older (v0.1.0) sessions

## Signal flow

```
                    +-- SC HPF (20-500 Hz) --> SC LPF (1-16 kHz) --> stereo-linked max|.| --> peak envelope follower --+
                    |                                                                                                  |
Input --> Lookahead |                                hysteresis comparator + hold timer + knee blend + duck <---------+
 (or Sidechain      |                                                    |
  bus, if enabled)  |                     program-dependent attack/release gain ramp (dB domain)
    |               |                                                    |
    +---------------+------------------------------------------------> x (gain), or Listen output --> Output
```

See [`docs/architecture.md`](docs/architecture.md) for the full breakdown, including the hysteresis/hold state machine, the knee/duck/listen additions, the external sidechain input, the v0.2.0 SC LPF and program-dependent ramp, and the lookahead latency-reporting strategy - and [`docs/manual.md`](docs/manual.md) for the user-facing parameter reference and mixing tips. [`docs/design-brief.md`](docs/design-brief.md) documents the research-derived sourcing behind the v0.2.0 voicing pass.

## Roadmap

| Milestone | Description | Status |
|---|---|---|
| M0 | Bootstrap - project skeleton, CI, docs | Done |
| M1 | DSP completion - knee, duck mode, listen mode, external sidechain input, broadened test suite | Done |
| M2 | Presets & state recall, research-derived deep-dive voicing pass, DE localisation | Done |
| M3 | Custom GUI & accessibility | Planned |
| M4 | Release engineering - signing, notarization, installers, v1.0.0 | Planned |
<!-- ==END BODY== -->

## Documentation

- [`docs/manual.md`](docs/manual.md) — the user manual: what every control does, and how to use it
- [`docs/presets.md`](docs/presets.md) — what each factory preset is for
- [`CHANGELOG.md`](CHANGELOG.md) — what shipped in each release
- [Silentium on basilica-audio.github.io](https://basilica-audio.github.io/website/silentium/) — the product page (English and German)

## Installation

Download the archive for your platform from the [Releases](../../releases) page and copy the bundles into the standard plugin locations:

**macOS**

| Format | Path |
|---|---|
| AU (Component) | `~/Library/Audio/Plug-Ins/Components/` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |

If Logic Pro doesn't pick up the plugin after installing, force a rescan by resetting the AU cache:

```sh
killall -9 AudioComponentRegistrar
auval -a
```

**Windows**

| Format | Path |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\` |

## Building from source

Requires JUCE 8.0.14, C++20, and CMake ≥ 3.24. See [`docs/building.md`](docs/building.md) for full prerequisites and step-by-step build/test commands for macOS and Windows.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

Silentium is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPLv3).

This project uses [JUCE](https://juce.com) 8, whose open-source tier is licensed under AGPLv3 (as of JUCE 8; JUCE 7 and earlier used GPLv3), which is why this project is AGPLv3 rather than GPLv3. See [`docs/adr/0002-agplv3-licensing.md`](docs/adr/0002-agplv3-licensing.md) for the full reasoning.

VST is a registered trademark of Steinberg Media Technologies GmbH.

Silentium is an independent open-source project and is not affiliated with, endorsed by, or sponsored by any plugin manufacturer.
