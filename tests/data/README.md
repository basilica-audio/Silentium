# `tests/data/` — cross-version golden renders

`GoldenRenders.h` is a **generated, checked-in artifact**. It is the only thing
in this repository that can prove Silentium's neutrality claim:

> a session or preset saved under v0.3.x renders identically under v0.4.0.

## Why a checked-in artifact is required

The obvious test — render the same settings twice through the current binary
and compare — proves nothing across versions. Both sides of that comparison are
produced by the *same* build, so a regression in the legacy signal path shifts
both sides identically and the test stays green. Only a render captured from
the **previous version's own code** and committed can catch it.

`GoldenRenders.h` is exactly that capture. Tests
`tests/StateTests.cpp` (T1), `tests/GateEngineTests.cpp` (T4) and
`tests/PresetManagerTests.cpp` (T20) compare the current build's output
against it.

## Provenance of the checked-in file

| Field | Value |
|---|---|
| Generating commit | `e9bceb9` (`origin/main`, Silentium v0.3.0 engine sources) |
| Generated on | 2026-07-26, at the start of the `feat/v0.4.0-sota-dsp` branch, **before any engine change** |
| Harness | `tests/tools/GoldenRenderHarness.cpp` |
| Fixture | `tests/GoldenFixture.h` (stimulus, render loop, fingerprint definition) |
| Compiler | Apple clang 21.0.0 (`clang-2100.1.1.101`) |
| Host | macOS 26.5.1, arm64 |
| CMake / generator | CMake 4.4.0, Ninja |
| Build type | `Debug` (`-DCMAKE_BUILD_TYPE=Debug`), JUCE 8.0.14 recommended flags |
| Toolchain tag | `macos-arm64-clang21-debug` (recorded in the generated header) |

The `src/` tree at generation time was verified byte-identical to `origin/main`
(`git diff --stat origin/main -- src/ presets/` was empty).

## What is compared, and why it is a two-tier check

Bit-identical floating-point output across *different* toolchains is not
achievable: libm's `sin`/`cos`/`exp` differ by ULPs between platforms, and FMA
contraction differs between optimisation levels. Silentium's CI builds Release
on macOS (universal `arm64;x86_64`) and Release on Windows (MSVC), so a single
strict hash check would be guaranteed to fail somewhere for reasons that have
nothing to do with the DSP. The artifact therefore carries two tiers:

- **Tier A — windowed level fingerprint (always asserted).** Per-1024-sample
  RMS and peak, in dB, for the whole render. Compared with a tolerance of
  `2e-3 dB` (`GoldenFixture::fingerprintToleranceDb`). Cross-toolchain float
  divergence through the sidechain IIRs and the envelope follower lands around
  `1e-5 dB`; any real change to the gain law, knee, hysteresis or ballistics
  moves a window by more than `1e-2 dB`. The tolerance sits between the two
  with roughly two orders of magnitude of margin on each side, so this is a
  genuine guarantee, not a formality.
- **Tier B — exact SHA-256 (asserted on the generating configuration only).**
  The strict bit-identity check. It runs when the test binary's own
  `GoldenFixture::toolchainTag()` matches the tag recorded in the generated
  header, and is reported-and-skipped elsewhere.

The stimulus is built with sharp (cosine-phase) note onsets specifically so
that the gate's threshold crossings are steep. A crossing that is approached
slowly could be flipped by one sample by a last-ULP detector difference, which
would make Tier A flaky across platforms; a discontinuous onset crosses the
threshold within a single sample and cannot.

## Regeneration policy

**A failing golden test is a finding, not a chore.** It means the legacy signal
path changed. Investigate first; regenerating the artifact to make the test
pass destroys the guarantee it exists to provide.

Legitimate reasons to regenerate are narrow:

1. A **deliberate, changelogged** change to the legacy signal path — the
   CHANGELOG must say so explicitly, and the new file must be generated from a
   build of the release that introduced the change.
2. The CI toolchain moved far enough that Tier A no longer holds. In that case
   regenerate from a **tagged build of the previous release**, never from the
   working branch, and update the provenance table above.

To regenerate (at a checkout of the previous release's engine sources):

```sh
export CPM_SOURCE_CACHE="$HOME/.cache/CPM"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target GoldenRenderHarness
./build/GoldenRenderHarness > tests/data/GoldenRenders.h
```

`tests/GoldenFixture.h` (stimulus, render length, window size, parameter sets)
must not change once goldens exist — changing it silently invalidates every
cross-version guarantee the artifact provides.
