#pragma once

#include "../params/ParameterIds.h"
#include "LookaheadRamp.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cstdint>

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

    //==========================================================================
    // v0.4.0 additions. Every one of these defaults to the value that
    // reproduces v0.3.x behaviour bit-for-bit, and each is implemented as a
    // literal branch around the legacy code rather than as a re-derivation of
    // it - see the neutrality note on each setter.

    // Downward-expander ratio (1:1 to ParamConstants::maxRatio). At the top
    // of the range the gain computer takes the *literal* v0.3.x binary path
    // (target snaps between Range and 0 dB); anything below it follows the
    // continuous expander curve documented in process(). Default is the top
    // of the range, so this is neutral until moved.
    void setRatio (float newRatio);

    // Open/close threshold gap in dB, replacing what was a fixed internal
    // 3 dB constant before v0.4.0. The default (3 dB) reproduces that
    // constant exactly.
    void setHysteresisDb (float newHysteresisDb);

    // Detector characteristic: false = the v0.3.x peak ballistics follower,
    // true = a 5 ms mean-square (RMS) window. Switching crosses the two
    // envelopes over in detectorCrossfadeSeconds so the change cannot click;
    // at rest in peak mode the RMS contribution is exactly zero-weighted, so
    // peak mode is unchanged.
    void setDetectorMode (bool shouldUseRms);

    // Sidechain filter order: false = the v0.3.x 12 dB/oct pair, true = a
    // 24 dB/oct pair (a second biquad per filter, Butterworth Q-paired).
    // Crossfaded over slopeCrossfadeSeconds on change; at rest in 12 dB/oct
    // mode the 24 dB/oct chain is exactly zero-weighted.
    void setScSlope24 (bool shouldUse24dBPerOctave);

    // Smooth Open: shapes the opening gain trajectory with a moving-max plus
    // cascaded-box smoother that fits inside the existing lookahead window
    // (see LookaheadRamp.h), so even a 0 ms attack opens along a continuous
    // ramp. Adds no latency, and is bypassed entirely when Lookahead is 0.
    // Off by default; toggling crosses the two trajectories over in
    // smoothOpenCrossfadeSeconds.
    void setSmoothOpen (bool shouldSmoothOpening);

    // Release trajectory shape: false = the v0.3.x program-dependent
    // exponential approach, true = a constant-dB/s (dB-linear) fade, so a
    // full-Range close takes exactly the Release time and the tail's decay
    // rate never changes. Only the falling branch is affected; the attack
    // branch is the exponential approach in both shapes.
    void setReleaseShapeLinear (bool shouldUseLinearRelease);

    // The delay currently applied to the main path, in samples - i.e. the
    // latency the host must be told about.
    //
    // v0.4.0 makes Lookahead a LIVE parameter. Before v0.4.0 it was
    // structural: a change only took effect on the next prepare(), because
    // there was no safe way to keep the reported latency consistent with the
    // applied delay. Now setLookaheadMs() moves the delay immediately, via an
    // equal-power crossfade between the old and new taps (both always inside
    // the delay line's fixed capacity, so no allocation), and publishes the
    // new value here. The owning processor polls this from the message thread
    // and calls setLatencySamples() there - never from the audio thread. See
    // SilentiumAudioProcessor::timerCallback().
    //
    // Atomic because it is written on the audio thread and read on the
    // message thread; relaxed ordering is sufficient (it is a single int with
    // no other state hanging off it).
    int getLatencySamples() const noexcept { return latencySamples.load (std::memory_order_relaxed); }

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
    float getCurrentGainDb() const noexcept { return appliedGainDb; }

    //==========================================================================
    // v0.4.0 F7 - gain-reduction telemetry.
    //
    // getCurrentGainDb() above reports the value at the END of the last
    // processed block, which is all a needle-style meter needs but hides
    // everything that happened inside the block: at a 1024-sample block size
    // a complete open-and-close cycle can be invisible. These two report the
    // extrema actually reached during the last process() call, so a display
    // can show the real gating action rather than a stroboscopic sample of
    // it. Both are plain reads of values written by the audio thread right
    // before process() returned; the owning processor republishes them as
    // relaxed atomics.
    float getBlockMinGainDb() const noexcept { return blockMinGainDb; }
    float getBlockMaxGainDb() const noexcept { return blockMaxGainDb; }

    // One telemetry point: the running sample position it was taken at, and
    // the lowest applied gain over the preceding sub-block.
    struct GainReductionPoint
    {
        std::int64_t sampleTime = 0;
        float gainReductionDb = 0.0f;
    };

    // Fixed-capacity single-producer/single-consumer ring for those points.
    // The audio thread pushes; a display thread pops. Deliberately REFUSES to
    // write when full rather than overwriting the oldest unread entry: a
    // consumer that stalls briefly then resumes gets a contiguous, correctly
    // ordered history with a gap at the end, instead of a silently corrupted
    // one. Storage is a fixed member array, so pushing never allocates.
    class GainReductionHistory
    {
    public:
        static constexpr int capacity = 2048; // power of two - the mask below depends on it

        bool push (const GainReductionPoint& point) noexcept
        {
            const auto write = writeIndex.load (std::memory_order_relaxed);
            const auto next = (write + 1) & mask;

            if (next == readIndex.load (std::memory_order_acquire))
                return false; // full: never clobber an unread entry

            points[static_cast<size_t> (write)] = point;
            writeIndex.store (next, std::memory_order_release);
            return true;
        }

        bool pop (GainReductionPoint& result) noexcept
        {
            const auto read = readIndex.load (std::memory_order_relaxed);

            if (read == writeIndex.load (std::memory_order_acquire))
                return false; // empty

            result = points[static_cast<size_t> (read)];
            readIndex.store ((read + 1) & mask, std::memory_order_release);
            return true;
        }

        int getNumReady() const noexcept
        {
            const auto write = writeIndex.load (std::memory_order_acquire);
            const auto read = readIndex.load (std::memory_order_acquire);
            return (write - read + capacity) & mask;
        }

        void clear() noexcept
        {
            readIndex.store (0, std::memory_order_relaxed);
            writeIndex.store (0, std::memory_order_relaxed);
        }

    private:
        static constexpr int mask = capacity - 1;

        std::array<GainReductionPoint, static_cast<size_t> (capacity)> points {};
        std::atomic<int> readIndex { 0 };
        std::atomic<int> writeIndex { 0 };
    };

    // The consumer side of the telemetry ring. Nothing in v0.4.0 reads it in
    // production - the display work that will is a later, GUI-side change -
    // but the producer and the ring itself ship and are tested now, so that
    // work is a pure addition rather than an engine change.
    GainReductionHistory& getGainReductionHistory() noexcept { return gainReductionHistory; }

    // How many samples one telemetry point summarises.
    static constexpr int telemetryIntervalSamples = 64;

private:
    // Butterworth (maximally-flat) Q for the sidechain HPF.
    static constexpr float filterQ = juce::MathConstants<float>::sqrt2 / 2.0f;

    // Default hysteresis: the close threshold sits this many dB below the
    // open (Threshold) value, so the two thresholds never coincide - the
    // single most common cause of gate chatter on a signal hovering near one
    // threshold. Fixed internally before v0.4.0; now the default of the
    // user-facing Hysteresis parameter (see setHysteresisDb()), which is why
    // this value must never change: it is what makes a pre-v0.4.0 session
    // render identically.
    static constexpr float defaultHysteresisDb = 3.0f;

    // v0.4.0 RMS detector (research-gate-expander.md §2.1): a one-pole on
    // the squared detection signal. 5 ms is short enough to catch a pick
    // attack and long enough to stop a low-frequency fundamental's own
    // period from rippling the envelope into chatter.
    static constexpr float rmsTimeConstantMs = 5.0f;

    // Bias added to the mean-square before the square root, so a fully
    // silent detection path cannot produce a denormal or a log of zero.
    static constexpr double rmsEpsilon = 1.0e-30;

    // Q values of the two sections of a 4th-order Butterworth response, used
    // by the 24 dB/oct sidechain mode (research-gate-expander.md §2.7). The
    // 12 dB/oct mode keeps its own single-section chain at filterQ, untouched.
    static constexpr float butterworth4thOrderQ1 = 0.5411961f;
    static constexpr float butterworth4thOrderQ2 = 1.3065630f;

    // Crossfade times for the two mode switches that would otherwise step the
    // detection envelope discontinuously.
    static constexpr double detectorCrossfadeSeconds = 0.005;
    static constexpr double slopeCrossfadeSeconds = 0.010;
    static constexpr double smoothOpenCrossfadeSeconds = 0.010;

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

    // Upper bound on Hysteresis (see ParameterLayout.cpp), clamped for the
    // same defensive reason.
    static constexpr float maxHysteresisDb = 12.0f;

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

    // v0.4.0 24 dB/oct sidechain chain: a complete, independent second copy
    // of the HPF->LPF pair, each stage Butterworth Q-paired so the cascade is
    // a true 4th-order response. Deliberately a PARALLEL chain rather than an
    // extra section spliced into the 12 dB/oct one: that keeps the 12 dB/oct
    // path literally untouched (its filters keep filterQ and see exactly the
    // same input as before), and it makes the mode switch a plain crossfade
    // between two continuously-running, always-warm chains instead of a
    // discontinuity plus a filter settling transient. Both chains run
    // unconditionally for the same reason - a chain that is only stepped
    // while engaged restarts from stale state on every switch. The cost is
    // four biquads per channel on the (scratch) detection path only, which
    // the v0.4.0 CPU budget accounts for.
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> scHighPass24a;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> scHighPass24b;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> scLowPass24a;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> scLowPass24b;

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

    // v0.4.0: the 24 dB/oct chain's own copy of the detection signal (it
    // filters the same input independently of the 12 dB/oct chain), and the
    // stereo-linked max|.| of it, kept so the linked signal that feeds the
    // envelope followers can itself be crossfaded between the two slopes.
    juce::AudioBuffer<float> detection24Buffer;
    juce::AudioBuffer<float> mono24Buffer;

    // v0.4.0: the RMS detector's envelope, computed alongside the peak
    // follower's so switching between them is a crossfade of two live
    // signals rather than a jump to a cold one.
    juce::AudioBuffer<float> monoRmsBuffer;

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

    // v0.4.0 commanded values, all at their exact-neutral defaults here.
    float lastRatio = ParamConstants::maxRatio;
    float lastHysteresisDb = defaultHysteresisDb;
    bool useRmsDetector = false;
    bool use24dBPerOctaveSlope = false;
    bool useLinearReleaseShape = false;
    bool useSmoothOpen = false;

    // Smooth Open's control-rate smoother (see LookaheadRamp.h). Runs on
    // every block regardless of whether Smooth Open is engaged, for the same
    // reason the RMS detector does: so the trajectory it holds is warm when
    // the crossfade towards it begins.
    LookaheadRamp openingRamp;

    // Crossfade weights: 0 == the legacy (peak / 12 dB per octave) path
    // contributes alone, 1 == the v0.4.0 path does. Linear smoothing, so a
    // weight parked at 0 multiplies the new path out entirely and the legacy
    // path's samples pass through byte-for-byte.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> detectorMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> slopeMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothOpenMix;

    // One-pole mean-square accumulator behind the RMS detector, plus its
    // coefficient (recomputed only in prepare(), never per sample).
    double rmsAccumulator = 0.0;
    double rmsCoefficient = 0.0;

    // Gate state machine, advanced one sample at a time in process().
    bool gateOpen = false;
    int holdCounterSamples = 0;

    // The ballistics' own output: the state variable the attack/release ramp
    // integrates. Smooth Open shapes this into appliedGainDb below without
    // feeding back into it, so the ballistics behave identically whether or
    // not Smooth Open is engaged.
    float currentGainDb = -60.0f;

    // The gain actually multiplied into the main path, and what
    // getCurrentGainDb() reports. Equal to currentGainDb whenever Smooth Open
    // is off (its default), so the metering contract is unchanged.
    float appliedGainDb = -60.0f;

    // F7 telemetry state: per-block extrema of appliedGainDb, plus the
    // running sub-block accumulator behind the history ring.
    float blockMinGainDb = 0.0f;
    float blockMaxGainDb = 0.0f;
    float telemetryRunningMinDb = 0.0f;
    int telemetryCounter = 0;
    std::int64_t telemetrySampleTime = 0;
    GainReductionHistory gainReductionHistory;

    // F6 live lookahead: the tap currently being faded out, the tap being
    // faded in (also the value reported as latency), and the crossfade
    // position. delayCrossfade parked at 1 means "no transition in flight" -
    // the single-tap read path below, i.e. exactly v0.3.x.
    int previousDelaySamples = 0;
    int targetDelaySamples = 0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delayCrossfade;
    static constexpr double delayCrossfadeSeconds = 0.010;

    void beginDelayCrossfadeTo (int newDelaySamples);

    std::atomic<int> latencySamples { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateEngine)
};
