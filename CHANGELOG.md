# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
