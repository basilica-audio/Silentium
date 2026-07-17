#pragma once

#include <juce_dsp/juce_dsp.h>

// The complete Silentium signal path, independent of juce::AudioProcessor so
// it can be exercised directly by unit tests without instantiating a full
// plugin (see tests/EngineTests.cpp). Owns all DSP state; every buffer/
// filter/delay-line is allocated in prepare() and never reallocated on the
// audio thread.
//
// Signal flow (see docs/architecture.md and docs/design-brief.md for the
// full v0.2.0 diagram):
//
//   Detection path (never reaches the output):
//     input -> SC HPF (sidechain-only) -> SC LPF (sidechain-only, v0.2.0,
//     off/fully-open by default) -> stereo-linked max|.| -> peak envelope
//     follower -> dBFS -> hysteresis comparator + hold timer -> knee blend
//     -> program-dependent attack/release gain ramp (dB domain, floor =
//     Range)
//
//   Main path:
//     input -> lookahead delay -> * gain (from the detection path, applied
//     identically to every channel: a stereo-linked gate never shifts the
//     stereo image) -> output
//
// The gate uses separate open/close thresholds (Threshold, and Threshold
// minus a fixed internal hysteresis amount) so a signal hovering right at
// Threshold cannot chatter the gate open and closed on every sample - this is
// what tests/EngineTests.cpp's hysteresis test verifies. Range == 0 dB means
// the floor is 0 dB below unity, i.e. the gate never attenuates at all; this
// is used as an "always open" reference passthrough in the null test.
//
// SC LPF (v0.2.0) is a second, independent sidechain-only filter stage in
// series after SC HPF, so the detection path can be narrowed toward the
// documented guitar pick-attack transient band (roughly 2-5 kHz) instead of
// only having its bottom end rejected - see docs/design-brief.md's "SC LPF"
// section. It defaults to 16 kHz (effectively fully open at typical sample
// rates), so a v0.1.0 session that never touches it reproduces v0.1.0
// behaviour exactly (tests/DesignBriefTests.cpp's SC LPF null test).
//
// Attack/Release (v0.2.0) drive a program-dependent exponential ramp rather
// than v0.1's fixed dB/sample linear slope - see setAttackMs()/setReleaseMs()
// and process()'s gain-computer comment for the exact mechanism and the
// honesty note on why this specific curve shape was chosen.
//
// Three optional, off-by-default refinements sit on top of that same
// hysteresis/hold state machine (see process()):
//
//   - Knee softens the gain computer's target into a smooth blend across a
//     band centred on Threshold, instead of an instant snap between Range
//     and 0 dB; Hold still overrides the blend to guarantee a fully open
//     target for its whole duration, exactly as it does at Knee == 0.
//   - Duck inverts that same target (attenuate above Threshold instead of
//     opening above it), turning the gate into a ducker without touching
//     the detection path.
//   - Listen substitutes the sidechain-filtered detection signal itself for
//     the gain computer's output, so the detector's trigger signal can be
//     auditioned directly.
//
// process() also optionally accepts an external sidechain block: when
// provided (non-null, non-empty), the detection path is fed from it instead
// of a copy of the main block, e.g. for keying off a kick drum or a second
// guitar track. Never written to; passed as non-const only because
// juce::dsp::AudioBlock does not offer a deep-const view.
class GateEngine
{
public:
    GateEngine();

    // Allocates all DSP state. Must be called (and completed) before the
    // first process() call, and again whenever sample rate/block size/
    // channel count change.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears all filter/envelope-follower/delay-line/gate state without
    // deallocating. Safe to call from the audio thread (e.g. on playback
    // stop/loop).
    void reset();

    // Processes `block` in place. `block`'s channel count must be at most
    // the count declared to prepare(), but its *sample* count is not
    // required to be at most spec.maximumBlockSize, even though every host
    // is supposed to honour that promise: JUCE's own AudioProcessor::
    // processBlock docs warn verbatim that block sizes are "NOT guaranteed
    // to be the same for every callback, and may be more or less than" the
    // prepared value. An oversized block is handled safely and correctly by
    // processing it in internal chunks of at most the prepared capacity
    // (see the private processChunk() and issue #12) rather than writing
    // past detectionBuffer/monoEnvelopeBuffer's fixed-size allocations. A
    // zero-sample block is a safe no-op. No allocation occurs here.
    //
    // `sidechainBlock`, if non-null and has both channels and the same
    // *total* sample count as `block`, is used as the detection path's
    // source instead of a copy of `block` (external sidechain) - that
    // check is made once against the full, pre-chunking sample count, and
    // the sidechain is then sliced identically to `block` for each internal
    // chunk. A sidechain with fewer channels than the detection path's
    // channel count (e.g. mono sidechain feeding a stereo instance) is
    // splatted: the last available sidechain channel is reused for any
    // remaining detection channels. Any other case (null, zero channels,
    // mismatched sample count) falls back to the normal self-detection
    // behaviour.
    void process (juce::dsp::AudioBlock<float>& block, const juce::dsp::AudioBlock<float>* sidechainBlock = nullptr);

    // Parameter setters, in real units (dB, ms, Hz). Safe to call every block
    // from the audio thread - no allocation/locks. Range and SC HPF are
    // smoothed internally (see rangeSmoothed/scHighpassSmoothed); Threshold/
    // Attack/Hold/Release only affect the discrete gate state machine's
    // decision boundaries/ramp rates, so they are applied directly without
    // additional smoothing (a block-rate step in these does not itself
    // multiply the audio signal, unlike Range).
    void setThresholdDb (float newThresholdDb);
    void setAttackMs (float newAttackMs);
    void setHoldMs (float newHoldMs);
    void setReleaseMs (float newReleaseMs);
    void setRangeDb (float newRangeDb);
    void setLookaheadMs (float newLookaheadMs);
    void setScHighpassHz (float newFrequencyHz);

    // v0.2.0: sidechain-only low-pass, in series after the SC HPF - see the
    // class-level docs. Smoothed the same way as setScHighpassHz().
    void setScLowpassHz (float newFrequencyHz);

    // Soft-knee width in dB, centred on Threshold; 0 dB (the default)
    // reproduces the original hard-knee target exactly. Not smoothed, for
    // the same reason Threshold/Attack/Hold/Release are not: it only
    // reshapes the gain computer's target curve, which the Attack/Release
    // ramp already approaches gradually - see process().
    void setKneeDb (float newKneeDb);

    // Inverts the gain computer's target (attenuate above Threshold instead
    // of opening above it), turning the gate into a ducker. Off by default.
    void setDuckingMode (bool shouldDuck);

    // Substitutes the sidechain-filtered detection signal for the gain
    // computer's output, auditioning the detector's trigger signal
    // directly. Off by default.
    void setListenMode (bool shouldListen);

    // Lookahead latency in samples, valid after prepare() has run. Lookahead
    // is treated as a structural parameter (like an oversampling factor):
    // its value at the time prepare() runs determines both the applied delay
    // and the reported latency for the life of that prepared session. Live
    // changes via setLookaheadMs() take effect on the next prepare() call
    // (typically triggered by the host re-calling prepareToPlay), not
    // immediately - this keeps the reported host latency always consistent
    // with the actual delay applied, without calling into
    // AudioProcessor::setLatencySamples()/updateHostDisplay() (which are not
    // safe to call from the audio thread) from inside process().
    int getLatencySamples() const noexcept { return latencySamples; }

    // True while the gate is open (including its attack/hold phase), false
    // while closed/releasing towards Range. Cheap, side-effect-free query;
    // exposed for diagnostics/tests (see tests/EngineTests.cpp's hysteresis
    // test) and a future GUI gate-open indicator.
    bool isGateOpen() const noexcept { return gateOpen; }

    // The gain currently applied to the main path, in dB: 0 dB fully open,
    // ramping down towards Range while closed/closing. Cheap,
    // side-effect-free query for the M3 GUI's gain-reduction meter - read
    // on the audio thread right after process() and published to the GUI
    // via an atomic (see SilentiumAudioProcessor::getGainReductionDb()).
    float getCurrentGainDb() const noexcept { return currentGainDb; }

private:
    // Butterworth (maximally-flat) Q for the sidechain HPF.
    static constexpr float filterQ = juce::MathConstants<float>::sqrt2 / 2.0f;

    // Fixed hysteresis: the close threshold sits this many dB below the
    // open (Threshold) value, so the two thresholds never coincide - the
    // single most common cause of gate chatter on a signal hovering near one
    // threshold.
    static constexpr float hysteresisDb = 3.0f;

    // Envelope-follower ballistics (fixed, not user-exposed): fast attack so
    // transients are caught almost immediately, moderate release so the
    // envelope doesn't itself chatter on a bumpy, sustained signal. Distinct
    // from the user-facing Attack/Release, which shape the gain ramp, not
    // the envelope.
    static constexpr float detectorAttackMs = 0.3f;
    static constexpr float detectorReleaseMs = 15.0f;

    // Floor used for both Decibels::gainToDecibels/decibelsToGain calls -
    // anything quieter is treated as silence for gate-logic purposes.
    static constexpr float minusInfinityDb = -100.0f;

    // Generous upper bound on Lookahead so the delay line's fixed capacity
    // (set once in prepare()) comfortably covers the whole parameter range
    // (0-20 ms, see ParameterLayout.cpp) at any realistic sample rate.
    static constexpr float maxLookaheadMs = 25.0f;

    // Upper bound on Knee (see ParameterLayout.cpp); used only to clamp
    // defensively, the same way clampBelowNyquist clamps SC HPF.
    static constexpr float maxKneeDb = 24.0f;

    // v0.2.0 program-dependent ramp (see process()'s gain-computer comment
    // for the full mechanism): the exponential approach is calibrated so a
    // full Range-span transition reaches within this many dB of its target
    // in the user-facing Attack/Release time - i.e. "practically at target".
    // A smaller, e.g. near-Threshold, transition then reaches the same
    // absolute tolerance in proportionally less wall-clock time purely as a
    // consequence of the exponential shape, which is what
    // tests/DesignBriefTests.cpp's ramp-proof test measures.
    static constexpr float rampCloseEnoughDb = 0.5f;

    static float clampBelowNyquist (float frequencyHz, double sampleRate) noexcept;
    int computeLookaheadSamples() const noexcept;

    // Runs the actual detection/gain-computer/lookahead DSP on a single
    // chunk. `block.getNumSamples()` must be <= preparedBlockSize -
    // process()'s chunking loop guarantees that precondition on every call,
    // so this is the only place that indexes into detectionBuffer/
    // monoEnvelopeBuffer (see issue #12). Body unchanged from process()
    // itself prior to that fix.
    void processChunk (juce::dsp::AudioBlock<float>& block, const juce::dsp::AudioBlock<float>* sidechainBlock);

    double sampleRate = 44100.0;
    juce::uint32 numChannels = 2;

    // Sidechain-only high-pass; never applied to the main signal path.
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> scHighPass;

    // v0.2.0: sidechain-only low-pass, in series after scHighPass above -
    // never applied to the main signal path. See setScLowpassHz()/the
    // class-level docs.
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> scLowPass;

    // Mono (1-channel) peak envelope follower fed by the stereo-linked
    // max|.| of the sidechain-filtered detection signal.
    juce::dsp::BallisticsFilter<float> envelopeFollower;

    // Lookahead delay on the main signal, one channel-independent tap per
    // channel but driven by the same (stereo-linked) gain envelope.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> lookaheadDelay { 0 };

    // Scratch buffers for the detection path, sized in prepare() to the
    // maximum block size declared there; never resized on the audio thread.
    juce::AudioBuffer<float> detectionBuffer;
    juce::AudioBuffer<float> monoEnvelopeBuffer;

    // Sample-count capacity detectionBuffer/monoEnvelopeBuffer were sized
    // for in prepare() (== spec.maximumBlockSize there, captured here
    // because ProcessSpec itself isn't retained). process() chunks any
    // oversized host block into pieces of at most this many samples before
    // calling processChunk() - see issue #12.
    size_t preparedBlockSize = 0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rangeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> scHighpassSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> scLowpassSmoothed;
    static constexpr double smoothingTimeSeconds = 0.05;

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied on every prepare() so re-prepare (sample-rate
    // change, etc.) never resets a live parameter back to a default.
    float lastThresholdDb = -40.0f;
    float lastAttackMs = 1.0f;
    float lastHoldMs = 20.0f;
    float lastReleaseMs = 80.0f;
    float lastRangeDb = -60.0f;
    float lastLookaheadMs = 5.0f;
    float lastScHighpassHz = 80.0f;
    float lastScLowpassHz = 16000.0f;
    float lastKneeDb = 0.0f;
    bool duckingMode = false;
    bool listenMode = false;

    // Gate state machine, advanced one sample at a time in process().
    bool gateOpen = false;
    int holdCounterSamples = 0;
    float currentGainDb = -60.0f;

    int latencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateEngine)
};
