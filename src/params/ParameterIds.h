#pragma once

// Central definition of all AudioProcessorValueTreeState parameter IDs for
// Silentium. See docs/architecture.md for the corresponding signal-flow
// diagram.
//
// FROZEN AS OF THE v0.1 PARAMETER LAYOUT:
// Parameter IDs below must NEVER change once shipped - saved sessions and
// presets persist the APVTS state keyed by these string IDs, and renaming or
// removing one would silently break every user's saved state. Ranges,
// defaults, and skew MAY still be refined during voicing/tuning milestones;
// only the IDs themselves are frozen.
namespace ParamIDs
{
    // Open threshold, in dBFS, measured on the (sidechain-filtered) envelope.
    // The gate's close threshold sits a fixed amount below this (see
    // GateEngine::hysteresisDb) so a signal hovering right at Threshold does
    // not chatter the gate open/closed.
    inline constexpr auto threshold = "threshold";

    // Time for the gain computer to ramp from Range (closed) to 0 dB (open)
    // once the envelope crosses the open threshold.
    inline constexpr auto attack = "attack";

    // Minimum time the gate stays open once opened, regardless of the
    // envelope dropping below the close threshold - retriggered every sample
    // the envelope stays above the close threshold. Prevents the gate from
    // slamming shut between consecutive transients of a palm-muted phrase.
    inline constexpr auto hold = "hold";

    // Time for the gain computer to ramp from 0 dB back down to Range once
    // the hold time has elapsed with the envelope below the close threshold.
    inline constexpr auto release = "release";

    // Floor attenuation applied when the gate is closed, in dB. 0 dB means
    // the gate never attenuates at all (an always-open passthrough).
    inline constexpr auto range = "range";

    // Lookahead applied to the main (delayed) signal path so the gate can
    // start opening slightly before a transient actually arrives. Reported
    // to the host as this plugin's total latency (see
    // SilentiumAudioProcessor::prepareToPlay).
    inline constexpr auto lookahead = "lookahead";

    // Sidechain high-pass filter cutoff applied only to the detection path
    // (never to the main signal), so that low-frequency hum/rumble doesn't
    // falsely hold the gate open.
    inline constexpr auto scHighpass = "scHighpass";

    // v0.2.0: sidechain low-pass filter cutoff, applied only to the
    // detection path in series after the SC HPF, so the detection band can
    // be narrowed toward the guitar pick-attack transient region (roughly
    // 2-5 kHz - see docs/design-brief.md) instead of only having its bottom
    // end rejected. Defaults fully open (16 kHz) so a v0.1.0 session that
    // never touches it reproduces v0.1.0 behaviour exactly (tolerant
    // import - see docs/design-brief.md's Versioning section).
    inline constexpr auto scLowpass = "scLowpass";

    // Soft-knee width, in dB, centred on Threshold. 0 dB reproduces the
    // original v0.1 hard-knee behaviour exactly (the gain computer's target
    // snaps between Range and 0 dB at the open/close thresholds); a wider
    // knee blends the target gain smoothly across the band, still bounded by
    // the same hysteresis/hold state machine (see GateEngine::process()).
    inline constexpr auto knee = "knee";

    // Duck: inverts the gain computer so the detector crossing Threshold
    // attenuates the output toward Range instead of opening it - the same
    // detection path (SC HPF, hysteresis, hold, lookahead) driving a ducker
    // instead of a gate.
    inline constexpr auto duck = "duck";

    // Listen: routes the sidechain-filtered detection signal (post SC HPF,
    // pre envelope-follower) directly to the output, bypassing the gain
    // computer entirely, so the gate's actual trigger signal can be
    // auditioned while dialling in SC HPF/Threshold.
    inline constexpr auto listen = "listen";

    //==========================================================================
    // v0.4.0 additions. Same freeze rule as everything above: these IDs are
    // now permanent. Every one of them defaults to the value that reproduces
    // v0.3.x behaviour exactly, so a v0.3.x session or preset - which carries
    // none of these keys - loads at exact-neutral settings and renders
    // identically (tests/StateTests.cpp's golden-master test).

    // Ratio: the downward-expander law applied between Threshold and Range.
    // At the top of its range the gate takes the literal v0.3.x binary path
    // (open or floored, nothing in between), which is what makes the
    // neutrality guarantee a code-level branch rather than a numerical
    // coincidence. Below that, the gain computer follows a continuous
    // expander curve, so quiet material is attenuated proportionally instead
    // of being switched off.
    inline constexpr auto ratio = "ratio";

    // Hysteresis: the gap between the open threshold (Threshold) and the
    // close threshold (Threshold minus this value), previously a fixed
    // internal 3 dB constant. Wider settings buy chatter immunity on
    // material that hovers around Threshold; 0 dB makes the two thresholds
    // coincide, which is the classic chatter case and is allowed
    // deliberately.
    inline constexpr auto hysteresis = "hysteresis";

    // Detector: how the sidechain envelope is measured. Peak is the v0.3.x
    // ballistics follower. RMS is a fixed 5 ms mean-square window, which
    // tracks perceived loudness rather than instantaneous excursion and is
    // markedly steadier on low-frequency material.
    inline constexpr auto detector = "detector";

    // SC Slope: the order of BOTH sidechain filters. 12 dB/oct is the v0.3.x
    // single-biquad pair; 24 dB/oct cascades a second biquad per filter with
    // true 4th-order Butterworth Q pairing, for a detection band with much
    // harder edges.
    inline constexpr auto scSlope = "scSlope";

    // Smooth Open: shapes the opening gain trajectory inside the existing
    // lookahead window so that even a 0 ms attack opens along a continuous
    // ramp instead of a single-sample step. Adds no latency. No effect when
    // Lookahead is 0.
    inline constexpr auto smoothOpen = "smoothOpen";

    // Release Shape: Exponential is the v0.3.x program-dependent approach.
    // Linear closes at a constant dB/s rate, so a full-Range close takes
    // exactly the Release time and the tail's decay rate stays constant -
    // the behaviour a dB-linear VCA integrator produces.
    inline constexpr auto releaseShape = "releaseShape";
}

// Numeric constants that the parameter layout and the DSP engine must agree
// on exactly. They live here, next to the IDs, rather than being written out
// twice: the "Ratio is at its maximum" test is what selects the literal
// v0.3.x binary-gate code path in GateEngine, so a drift between the layout's
// range and the engine's comparison would silently break neutrality.
namespace ParamConstants
{
    // Top of the Ratio range, displayed as an infinite ratio.
    inline constexpr float maxRatio = 20.0f;

    // Tolerance for the "Ratio is at its maximum" comparison, wide enough to
    // absorb the normalise/denormalise round-trip a host performs on the
    // parameter value, far narrower than the 0.01 parameter step.
    inline constexpr float ratioGateEpsilon = 0.001f;

    // Choice indices, so the engine and the layout cannot disagree about
    // which entry of a StringArray means what.
    inline constexpr int detectorPeak = 0;
    inline constexpr int detectorRms = 1;

    inline constexpr int scSlope12 = 0;
    inline constexpr int scSlope24 = 1;

    inline constexpr int releaseShapeExponential = 0;
    inline constexpr int releaseShapeLinear = 1;
}
