<p align="center"><img src="docs/assets/icon.png" alt="Silentium icon" width="160"/></p>

# Silentium

*Silence between the storms — a tight lookahead noise gate for palm-muted rhythm.*

[![CI](https://github.com/basilica-audio/silentium/actions/workflows/ci.yml/badge.svg)](https://github.com/basilica-audio/silentium/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

> **Work in progress.** Silentium is pre-1.0 and under active development. There are no built binaries or releases yet — building from source is currently the only way to run it. Expect breaking changes until v1.0.0 ships (see [Roadmap](#roadmap)).

<!-- ==BEGIN BODY== (plugin engineer: replace this block with What it is / Features / Signal flow / Roadmap) -->
## What it is

Silentium is a tight, lookahead noise gate built on JUCE 8, aimed at killing amp hiss/hum in the silence between palm-muted chugs: it detects transients on a sidechain-filtered copy of the input (so hum/rumble can't falsely hold it open), opens fast with a lookahead head start so it never clips the leading edge of a pick attack, and uses two separate open/close thresholds so a signal hovering near the threshold can't chatter the gate open and closed.

## Features (v0.1.0 scope)

- **Threshold** - open threshold on the (sidechain-filtered) envelope, -80 dB to 0 dB (default -40 dB)
- **Hysteresis** - the gate's close threshold sits a fixed 3 dB below Threshold, so it can never chatter on a signal hovering near one value
- **Attack** - ramp time from the Range floor up to unity once the envelope opens the gate, 0.1 - 50 ms (default 1 ms)
- **Hold** - minimum time the gate stays open once opened, retriggered continuously while the envelope stays above the close threshold, 0 - 500 ms (default 20 ms)
- **Release** - ramp time back down to the Range floor once Hold has elapsed, 5 - 500 ms (default 80 ms)
- **Range** - floor attenuation applied while closed, -80 dB to 0 dB (default -60 dB); 0 dB means the gate never attenuates at all
- **Lookahead** - delays the main signal 0 - 20 ms (default 5 ms) so the gate can start opening just before a transient arrives; reported to the host as this plugin's total latency
- **SC HPF** - sidechain-only high-pass, 20 - 500 Hz (default 80 Hz), keeps hum/rumble from falsely holding the gate open; never applied to the main signal
- **Knee** - soft-knee width around Threshold, 0 - 24 dB (default 0 dB); 0 dB is the classic hard-knee snap, wider values blend the gain smoothly across the band
- **Duck** - inverts the gain computer into a ducker (attenuate above Threshold instead of opening above it), off by default
- **Listen** - routes the sidechain-filtered detection signal to the output for auditioning what the gate hears, off by default
- **External sidechain input** - an optional second input bus (disabled by default) lets the detection path be keyed from another track instead of the main input
- Full state save/recall via `AudioProcessorValueTreeState`

## Signal flow

```
                    +-- SC HPF (20-500 Hz) --> stereo-linked max|.| --> peak envelope follower --+
                    |                                                                             |
Input --> Lookahead |                    hysteresis comparator + hold timer + knee blend + duck <-+
 (or Sidechain      |                                                    |
  bus, if enabled)  |                                        attack/release gain ramp (dB domain)
    |               |                                                    |
    +---------------+------------------------------------------------> x (gain), or Listen output --> Output
```

See [`docs/architecture.md`](docs/architecture.md) for the full breakdown, including the hysteresis/hold state machine, the knee/duck/listen additions, the external sidechain input, and the lookahead latency-reporting strategy - and [`docs/manual.md`](docs/manual.md) for the user-facing parameter reference and mixing tips.

## Roadmap

| Milestone | Description | Status |
|---|---|---|
| M0 | Bootstrap - project skeleton, CI, docs | Done |
| M1 | DSP completion - knee, duck mode, listen mode, external sidechain input, broadened test suite | Done |
| M2 | Presets & state recall | Planned |
| M3 | Custom GUI & accessibility | Planned |
| M4 | Release engineering - signing, notarization, installers, v1.0.0 | Planned |
<!-- ==END BODY== -->

## Installation

No pre-built binaries are published yet (see the work-in-progress notice above). Once releases begin, installation will follow the standard plugin locations:

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
