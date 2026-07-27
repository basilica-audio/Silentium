#include "GateEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

GateEngine::GateEngine() = default;

float GateEngine::clampBelowNyquist (float frequencyHz, double rate) noexcept
{
    const auto nyquist = static_cast<float> (rate) * 0.5f;
    return juce::jlimit (10.0f, nyquist * 0.9f, frequencyHz);
}

int GateEngine::computeLookaheadSamples() const noexcept
{
    const auto clampedMs = juce::jlimit (0.0f, maxLookaheadMs, lastLookaheadMs);
    return juce::roundToInt (clampedMs * 0.001 * sampleRate);
}

void GateEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    numChannels = spec.numChannels;

    scHighPass.prepare (spec);
    scLowPass.prepare (spec);

    scHighPass24a.prepare (spec);
    scHighPass24b.prepare (spec);
    scLowPass24a.prepare (spec);
    scLowPass24b.prepare (spec);

    juce::dsp::ProcessSpec monoSpec = spec;
    monoSpec.numChannels = 1;
    envelopeFollower.prepare (monoSpec);
    envelopeFollower.setLevelCalculationType (juce::dsp::BallisticsFilterLevelCalculationType::peak);
    envelopeFollower.setAttackTime (detectorAttackMs);
    envelopeFollower.setReleaseTime (detectorReleaseMs);

    // Lookahead is a structural parameter (see getLatencySamples()'s
    // comment): the delay line's maximum capacity comfortably covers the
    // whole parameter range regardless of what is currently requested, but
    // the *applied* delay and reported latency are only re-derived here, in
    // prepare(), from the current lastLookaheadMs.
    const auto maxLookaheadSamples = static_cast<int> (std::ceil (maxLookaheadMs * 0.001 * sampleRate)) + 1;
    lookaheadDelay.setMaximumDelayInSamples (maxLookaheadSamples);
    lookaheadDelay.prepare (spec);

    detectionBuffer.setSize (static_cast<int> (spec.numChannels), static_cast<int> (spec.maximumBlockSize), false, false, true);
    monoEnvelopeBuffer.setSize (1, static_cast<int> (spec.maximumBlockSize), false, false, true);
    detection24Buffer.setSize (static_cast<int> (spec.numChannels), static_cast<int> (spec.maximumBlockSize), false, false, true);
    mono24Buffer.setSize (1, static_cast<int> (spec.maximumBlockSize), false, false, true);
    monoRmsBuffer.setSize (1, static_cast<int> (spec.maximumBlockSize), false, false, true);
    preparedBlockSize = static_cast<size_t> (spec.maximumBlockSize);

    // One-pole mean-square coefficient for the v0.4.0 RMS detector. Computed
    // here (and only here), never per sample - the whole control path's
    // convention.
    rmsCoefficient = std::exp (-1.0 / (rmsTimeConstantMs * 0.001 * sampleRate));

    detectorMix.reset (sampleRate, detectorCrossfadeSeconds);
    detectorMix.setCurrentAndTargetValue (useRmsDetector ? 1.0f : 0.0f);
    slopeMix.reset (sampleRate, slopeCrossfadeSeconds);
    slopeMix.setCurrentAndTargetValue (use24dBPerOctaveSlope ? 1.0f : 0.0f);
    smoothOpenMix.reset (sampleRate, smoothOpenCrossfadeSeconds);
    smoothOpenMix.setCurrentAndTargetValue (useSmoothOpen ? 1.0f : 0.0f);

    // Smooth Open's window is derived from the lookahead, and its storage is
    // sized once here for the largest lookahead the parameter allows - so
    // moving the Lookahead knob later only moves indices, never allocates.
    openingRamp.prepare (maxLookaheadSamples);

    rangeSmoothed.reset (sampleRate, smoothingTimeSeconds);
    rangeSmoothed.setCurrentAndTargetValue (lastRangeDb);
    scHighpassSmoothed.reset (sampleRate, smoothingTimeSeconds);
    scHighpassSmoothed.setCurrentAndTargetValue (lastScHighpassHz);
    scLowpassSmoothed.reset (sampleRate, smoothingTimeSeconds);
    scLowpassSmoothed.setCurrentAndTargetValue (lastScLowpassHz);

    // prepare() is structural: it snaps the delay to the current Lookahead
    // rather than crossfading to it, and cancels any transition that was in
    // flight. (Live moves go through setLookaheadMs()/beginDelayCrossfadeTo()
    // instead - see getLatencySamples().)
    const auto preparedDelaySamples = computeLookaheadSamples();

    latencySamples.store (preparedDelaySamples, std::memory_order_relaxed);
    previousDelaySamples = preparedDelaySamples;
    targetDelaySamples = preparedDelaySamples;
    delayCrossfade.reset (sampleRate, delayCrossfadeSeconds);
    delayCrossfade.setCurrentAndTargetValue (1.0f);

    lookaheadDelay.setDelay (static_cast<float> (preparedDelaySamples));
    openingRamp.setWindowSamples (preparedDelaySamples);

    reset();

    // Prime the SC HPF coefficients immediately so the very first process()
    // call runs with correct, non-default coefficients rather than an
    // identity/uninitialised state. Assigning from the plain std::array
    // returned by ArrayCoefficients::makeHighPass (rather than
    // Coefficients::makeHighPass, which heap-allocates a new reference-
    // counted Coefficients object every call) also grows scHighPass.state's
    // internal juce::Array to its final capacity here, in prepare(), so the
    // identical assignment in process() below never allocates either - see
    // the comment there.
    *scHighPass.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (
        sampleRate, clampBelowNyquist (lastScHighpassHz, sampleRate), filterQ);

    // Same reasoning as the SC HPF priming above, for the v0.2.0 SC LPF
    // stage: prime real coefficients here (not an identity/uninitialised
    // state) and grow scLowPass.state's storage now so the identical
    // assignment in process() below never allocates either.
    *scLowPass.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (
        sampleRate, clampBelowNyquist (lastScLowpassHz, sampleRate), filterQ);

    // Same priming/storage-growing reasoning again for the v0.4.0 24 dB/oct
    // chain's four sections.
    const auto primedHighpassHz = clampBelowNyquist (lastScHighpassHz, sampleRate);
    const auto primedLowpassHz = clampBelowNyquist (lastScLowpassHz, sampleRate);

    *scHighPass24a.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, primedHighpassHz, butterworth4thOrderQ1);
    *scHighPass24b.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, primedHighpassHz, butterworth4thOrderQ2);
    *scLowPass24a.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sampleRate, primedLowpassHz, butterworth4thOrderQ1);
    *scLowPass24b.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sampleRate, primedLowpassHz, butterworth4thOrderQ2);
}

void GateEngine::reset()
{
    scHighPass.reset();
    scLowPass.reset();
    scHighPass24a.reset();
    scHighPass24b.reset();
    scLowPass24a.reset();
    scLowPass24b.reset();
    envelopeFollower.reset();
    lookaheadDelay.reset();

    rmsAccumulator = 0.0;
    detectorMix.setCurrentAndTargetValue (useRmsDetector ? 1.0f : 0.0f);
    slopeMix.setCurrentAndTargetValue (use24dBPerOctaveSlope ? 1.0f : 0.0f);
    smoothOpenMix.setCurrentAndTargetValue (useSmoothOpen ? 1.0f : 0.0f);

    gateOpen = false;
    holdCounterSamples = 0;
    // Start fully closed at the current Range floor, so a signal that never
    // crosses Threshold stays silent (apart from the attack ramp) rather
    // than momentarily passing through at 0 dB right after reset()/prepare().
    currentGainDb = lastRangeDb;
    appliedGainDb = lastRangeDb;

    delayCrossfade.setCurrentAndTargetValue (1.0f);
    previousDelaySamples = targetDelaySamples;

    blockMinGainDb = lastRangeDb;
    blockMaxGainDb = lastRangeDb;
    telemetryRunningMinDb = 0.0f;
    telemetryCounter = 0;
    telemetrySampleTime = 0;
    gainReductionHistory.clear();

    // Park the opening smoother settled at the same value, so it too starts
    // from "fully closed" rather than from whatever the previous session left
    // in its history.
    openingRamp.reset (lastRangeDb);
}

void GateEngine::setThresholdDb (float newThresholdDb)
{
    lastThresholdDb = newThresholdDb;
}

void GateEngine::setAttackMs (float newAttackMs)
{
    lastAttackMs = newAttackMs;
}

void GateEngine::setHoldMs (float newHoldMs)
{
    lastHoldMs = newHoldMs;
}

void GateEngine::setReleaseMs (float newReleaseMs)
{
    lastReleaseMs = newReleaseMs;
}

void GateEngine::setRangeDb (float newRangeDb)
{
    lastRangeDb = newRangeDb;
    rangeSmoothed.setTargetValue (newRangeDb);
}

void GateEngine::setLookaheadMs (float newLookaheadMs)
{
    if (juce::approximatelyEqual (newLookaheadMs, lastLookaheadMs))
        return;

    lastLookaheadMs = newLookaheadMs;

    // v0.4.0 F6: Lookahead is live. Before the first prepare() there is no
    // sample rate to convert with and no delay line to read from, so the new
    // value is simply remembered and prepare() will pick it up.
    if (preparedBlockSize > 0)
        beginDelayCrossfadeTo (computeLookaheadSamples());
}

void GateEngine::beginDelayCrossfadeTo (int newDelaySamples)
{
    if (newDelaySamples == targetDelaySamples)
        return;

    // A change arriving while a transition is still in flight snaps that
    // transition to its destination first, rather than trying to blend three
    // taps. Bounded worst case under fast automation, and the following
    // crossfade still covers the new move.
    previousDelaySamples = targetDelaySamples;
    targetDelaySamples = newDelaySamples;

    delayCrossfade.setCurrentAndTargetValue (0.0f);
    delayCrossfade.setTargetValue (1.0f);

    // Both taps live inside the delay line's fixed capacity (sized in
    // prepare() for the whole Lookahead range), so this only moves a read
    // offset - it never resizes anything.
    lookaheadDelay.setDelay (static_cast<float> (newDelaySamples));

    // The opening smoother's window follows the lookahead it is defined in
    // terms of.
    openingRamp.setWindowSamples (newDelaySamples);

    // Published for the message thread to pick up and report to the host.
    latencySamples.store (newDelaySamples, std::memory_order_relaxed);
}

void GateEngine::setScHighpassHz (float newFrequencyHz)
{
    lastScHighpassHz = newFrequencyHz;
    scHighpassSmoothed.setTargetValue (newFrequencyHz);
}

void GateEngine::setScLowpassHz (float newFrequencyHz)
{
    lastScLowpassHz = newFrequencyHz;
    scLowpassSmoothed.setTargetValue (newFrequencyHz);
}

void GateEngine::setKneeDb (float newKneeDb)
{
    lastKneeDb = newKneeDb;
}

void GateEngine::setDuckingMode (bool shouldDuck)
{
    duckingMode = shouldDuck;
}

void GateEngine::setListenMode (bool shouldListen)
{
    listenMode = shouldListen;
}

void GateEngine::setRatio (float newRatio)
{
    lastRatio = juce::jlimit (1.0f, ParamConstants::maxRatio, newRatio);
}

void GateEngine::setHysteresisDb (float newHysteresisDb)
{
    lastHysteresisDb = juce::jlimit (0.0f, maxHysteresisDb, newHysteresisDb);
}

void GateEngine::setDetectorMode (bool shouldUseRms)
{
    useRmsDetector = shouldUseRms;
    detectorMix.setTargetValue (shouldUseRms ? 1.0f : 0.0f);
}

void GateEngine::setScSlope24 (bool shouldUse24dBPerOctave)
{
    use24dBPerOctaveSlope = shouldUse24dBPerOctave;
    slopeMix.setTargetValue (shouldUse24dBPerOctave ? 1.0f : 0.0f);
}

void GateEngine::setSmoothOpen (bool shouldSmoothOpening)
{
    useSmoothOpen = shouldSmoothOpening;
    smoothOpenMix.setTargetValue (shouldSmoothOpening ? 1.0f : 0.0f);
}

void GateEngine::setReleaseShapeLinear (bool shouldUseLinearRelease)
{
    useLinearReleaseShape = shouldUseLinearRelease;
}

void GateEngine::process (juce::dsp::AudioBlock<float>& block, const juce::dsp::AudioBlock<float>* sidechainBlock)
{
    const auto requestedSamples = block.getNumSamples();

    if (requestedSamples == 0)
        return;

    // F7: the intra-block extrema describe this whole process() call, not
    // each internal chunk, so they are seeded here rather than in
    // processChunk() - a host block that gets subdivided still reports one
    // pair of extrema covering all of it.
    blockMinGainDb = std::numeric_limits<float>::max();
    blockMaxGainDb = std::numeric_limits<float>::lowest();

    // sidechainUsable is decided once, against the full, pre-chunking
    // sample count - a sidechain with a different *total* sample count
    // than the main block is unusable regardless of how the loop below
    // subdivides that block internally. See process()'s header doc.
    const auto sidechainUsable = sidechainBlock != nullptr
                                  && sidechainBlock->getNumChannels() > 0
                                  && sidechainBlock->getNumSamples() == requestedSamples;

    // Issue #12: detectionBuffer/monoEnvelopeBuffer (and every other piece
    // of per-block scratch state processChunk() indexes into) are
    // allocated in prepare() for at most preparedBlockSize samples. Hosts
    // are expected to never call process() with more samples than they
    // promised via prepare()'s spec.maximumBlockSize, but JUCE's own docs
    // explicitly warn that block sizes are "NOT guaranteed" to honour that
    // promise. Rather than write past those buffers' actual capacity (heap
    // overflow) or silently truncate/drop the tail of an oversized block,
    // process it in chunks of at most preparedBlockSize - mirroring the
    // suite's proven chunking pattern (see e.g. twist-your-guts's
    // PluginProcessor::processBlock) - so every sample is still processed
    // correctly. A normal, in-capacity block takes exactly one iteration of
    // this loop, so this is a no-op change for the common case.
    const auto chunkLimit = preparedBlockSize > 0
                                 ? preparedBlockSize
                                 : juce::jmax (static_cast<size_t> (1), requestedSamples);

    for (size_t offset = 0; offset < requestedSamples; offset += chunkLimit)
    {
        const auto chunkLength = juce::jmin (chunkLimit, requestedSamples - offset);
        auto chunkBlock = block.getSubBlock (offset, chunkLength);

        if (sidechainUsable)
        {
            auto sidechainChunk = sidechainBlock->getSubBlock (offset, chunkLength);
            processChunk (chunkBlock, &sidechainChunk);
        }
        else
        {
            processChunk (chunkBlock, nullptr);
        }
    }
}

void GateEngine::processChunk (juce::dsp::AudioBlock<float>& block, const juce::dsp::AudioBlock<float>* sidechainBlock)
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    // --- Detection path: SC HPF (sidechain-only, never touches the main
    // signal) applied to a scratch copy of either the main input or an
    // external sidechain input, if one was supplied and usable. ---
    juce::dsp::AudioBlock<float> detectionBlock (detectionBuffer);
    auto detectionSub = detectionBlock.getSubBlock (0, numSamples);

    const auto sidechainUsable = sidechainBlock != nullptr
                                  && sidechainBlock->getNumChannels() > 0
                                  && sidechainBlock->getNumSamples() == numSamples;

    if (sidechainUsable)
    {
        // Splat: if the sidechain has fewer channels than the detection path
        // (e.g. a mono sidechain feeding a stereo instance), reuse the last
        // available sidechain channel for the remaining detection channels
        // rather than leaving them holding stale data from a previous block.
        const auto sidechainChannels = sidechainBlock->getNumChannels();

        for (size_t channel = 0; channel < detectionSub.getNumChannels(); ++channel)
        {
            const auto sourceChannel = std::min (channel, sidechainChannels - 1);
            detectionSub.getSingleChannelBlock (channel).copyFrom (sidechainBlock->getSingleChannelBlock (sourceChannel));
        }
    }
    else
    {
        detectionSub.copyFrom (block);
    }

    // v0.4.0 SC Slope: the 24 dB/oct chain filters the same *unfiltered*
    // detection input, independently, so it must be copied off before the
    // 12 dB/oct chain runs in place below.
    juce::dsp::AudioBlock<float> detection24Block (detection24Buffer);
    auto detection24Sub = detection24Block.getSubBlock (0, numSamples);
    detection24Sub.copyFrom (detectionSub);

    const auto scHz = clampBelowNyquist (scHighpassSmoothed.skip (static_cast<int> (numSamples)), sampleRate);

    // Recomputed once per block from the smoothed cutoff. Deliberately uses
    // ArrayCoefficients::makeHighPass (a plain std::array<float, 6> returned
    // by value) assigned in place into the existing, ref-counted
    // scHighPass.state object, *not* Coefficients::makeHighPass, whose
    // implementation is `return *new Coefficients (...)`: a heap allocation
    // (plus the matching deallocation of the temporary Coefficients::Ptr)
    // on every single process() call, which is not real-time-safe.
    // Coefficients::operator=(const std::array&) reuses the juce::Array
    // storage already reserved by the identical assignment in prepare()
    // above (ensureStorageAllocated only grows, never shrinks/reallocates
    // when the requested size is already met), so this line does not
    // allocate either.
    *scHighPass.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, scHz, filterQ);

    juce::dsp::ProcessContextReplacing<float> detectionContext (detectionSub);
    scHighPass.process (detectionContext);

    // v0.2.0 SC LPF: same allocation-free coefficient-recompute pattern as
    // the SC HPF immediately above, applied in series right after it (see
    // the class-level topology comment in GateEngine.h). Sits entirely
    // inside the sidechain-only detection path - never touches the main
    // signal.
    const auto scLowHz = clampBelowNyquist (scLowpassSmoothed.skip (static_cast<int> (numSamples)), sampleRate);
    *scLowPass.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sampleRate, scLowHz, filterQ);
    scLowPass.process (detectionContext);

    // v0.4.0 24 dB/oct chain: the same two cutoffs, but each filter built as
    // a Butterworth-Q-paired cascade of two sections, giving a true 4th-order
    // response. Same allocation-free ArrayCoefficients pattern as above.
    *scHighPass24a.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, scHz, butterworth4thOrderQ1);
    *scHighPass24b.state = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass (sampleRate, scHz, butterworth4thOrderQ2);
    *scLowPass24a.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sampleRate, scLowHz, butterworth4thOrderQ1);
    *scLowPass24b.state = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (sampleRate, scLowHz, butterworth4thOrderQ2);

    juce::dsp::ProcessContextReplacing<float> detection24Context (detection24Sub);
    scHighPass24a.process (detection24Context);
    scHighPass24b.process (detection24Context);
    scLowPass24a.process (detection24Context);
    scLowPass24b.process (detection24Context);

    // Slope crossfade, applied to the detection signal itself (in place, into
    // detectionSub) rather than to the envelope, so that everything
    // downstream - the stereo-link, both detectors, and Listen mode - sees
    // one consistent detection signal. At a weight parked at 0 the arithmetic
    // is exactly `1.0f * legacy + 0.0f * new`, which is the legacy sample
    // unchanged: 12 dB/oct mode is bit-identical to v0.3.x.
    //
    // Stereo-linked combine: per-sample max(|channel|) across all channels,
    // so a signal panned to one side alone can still open the gate, and the
    // gate never shifts the stereo image (the same gain is applied to every
    // channel below).
    auto* monoData = monoEnvelopeBuffer.getWritePointer (0);
    const auto detectionChannels = detectionSub.getNumChannels();

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto slopeWeight = slopeMix.getNextValue();
        float maxAbs = 0.0f;

        for (size_t channel = 0; channel < detectionChannels; ++channel)
        {
            auto* wide = detectionSub.getChannelPointer (channel);
            const auto steep = detection24Sub.getChannelPointer (channel)[sample];
            const auto blended = (1.0f - slopeWeight) * wide[sample] + slopeWeight * steep;

            wide[sample] = blended;
            maxAbs = std::max (maxAbs, std::abs (blended));
        }

        monoData[sample] = maxAbs;
    }

    // v0.4.0 RMS detector: a one-pole on the squared stereo-linked signal,
    // computed BEFORE the peak follower overwrites monoData in place. It runs
    // on every block regardless of which detector is selected, so that
    // switching to it crossfades towards a warm, already-tracking envelope
    // rather than towards a cold one - a detector that only runs while
    // selected restarts from silence on every switch, which is audible.
    auto* rmsData = monoRmsBuffer.getWritePointer (0);

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        const auto input = static_cast<double> (monoData[sample]);
        rmsAccumulator = rmsCoefficient * rmsAccumulator + (1.0 - rmsCoefficient) * input * input;
        rmsData[sample] = static_cast<float> (std::sqrt (rmsAccumulator + rmsEpsilon));
    }

    juce::dsp::AudioBlock<float> monoBlock (monoEnvelopeBuffer);
    auto monoSub = monoBlock.getSubBlock (0, numSamples);
    juce::dsp::ProcessContextReplacing<float> monoContext (monoSub);
    envelopeFollower.process (monoContext);
    // monoEnvelopeBuffer now holds the per-sample linear peak envelope, and
    // monoRmsBuffer the per-sample linear RMS envelope.

    // --- Gain computer: hysteresis comparator + hold timer + attack/release
    // ramp, all in the dB domain, evaluated once per block for the
    // block-rate quantities (Range, thresholds, ramp rates) and once per
    // sample for the state machine and gain itself. ---
    const auto rangeDbNow = rangeSmoothed.skip (static_cast<int> (numSamples));

    // v0.4.0: the open/close gap is user-set. Its default is exactly the
    // constant this line used to read, so an untouched session is unchanged.
    const auto closeThresholdDb = lastThresholdDb - lastHysteresisDb;

    const auto attackTimeSamples = std::max (1.0f, static_cast<float> (lastAttackMs * 0.001 * sampleRate));
    const auto releaseTimeSamples = std::max (1.0f, static_cast<float> (lastReleaseMs * 0.001 * sampleRate));
    const auto holdTimeSamples = std::max (0, juce::roundToInt (lastHoldMs * 0.001 * sampleRate));

    // v0.2.0 program-dependent ramp (docs/design-brief.md's "Program-
    // dependent gain ramp" section): rather than v0.1's fixed dB/sample
    // linear slope (rate = full Range span / Attack(or Release)Time,
    // applied identically no matter how far the current gain actually sits
    // from its target), the gain computer now exponentially approaches its
    // per-sample target: the per-sample distance-to-target shrinks by a
    // fixed multiplier every sample, rather than by a fixed dB amount. That
    // shape has two properties together, verified by
    // tests/DesignBriefTests.cpp's ramp-proof test:
    //
    //   (a) the multiplier is calibrated (see rampCloseEnoughDb) so a
    //       *full* Range-span transition (Range floor <-> unity) reaches
    //       within rampCloseEnoughDb of its target in the user-facing
    //       Attack/Release time, same as v0.1's contract for a full-scale
    //       transition;
    //   (b) because the SAME multiplier applies regardless of how large the
    //       actual transition is, a *partial* transition (e.g. a few dB
    //       overshoot near Threshold) reaches that same absolute
    //       rampCloseEnoughDb tolerance in proportionally FEWER samples
    //       than a full-scale one does - the defining "program dependent"
    //       property both ISP's Time Vector Integration and dbx's
    //       AutoDynamic are documented (though not in reproducible
    //       algorithmic detail) to have. See docs/design-brief.md's honesty
    //       section: this specific curve shape is this brief's own
    //       plausible, testable proposal, not a reproduction of either
    //       vendor's proprietary algorithm.
    //
    // A sustained note (envelope staying open, target staying at 0 dB) sees
    // currentGainDb converge to and then sit flush at the target - no
    // periodic re-modulation - which is the conservative interpretation of
    // "eliminates modulation of sustained notes" this implementation
    // guarantees by construction, independent of how closely this curve
    // shape matches either hardware reference's unpublished algorithm.
    const auto fullScaleSpanDb = std::max (std::abs (rangeDbNow), rampCloseEnoughDb * 2.0f);
    const auto convergenceRatio = juce::jlimit (1.0e-6f, 0.999f, rampCloseEnoughDb / fullScaleSpanDb);
    const auto attackMultiplier = std::pow (convergenceRatio, 1.0f / attackTimeSamples);
    const auto releaseMultiplier = std::pow (convergenceRatio, 1.0f / releaseTimeSamples);

    const auto numChannelsToProcess = block.getNumChannels();

    // Knee: 0 dB reproduces the original hard-knee target exactly (openness
    // snaps between 0 and 1 with gateOpen); a wider knee blends openness
    // smoothly across a band centred on Threshold. Hold still overrides the
    // blend to guarantee a fully open target for its whole duration in both
    // cases - otherwise a dip into the knee band during Hold could sag the
    // gain, defeating Hold's purpose.
    const auto kneeDbNow = juce::jlimit (0.0f, maxKneeDb, lastKneeDb);
    const auto hardKnee = kneeDbNow < 0.0001f;
    const auto kneeLowerDb = lastThresholdDb - kneeDbNow * 0.5f;

    // v0.4.0 F4 (dB-linear release): a constant dB/sample decrement sized so
    // that a full Range-span close takes exactly the Release time. Shares
    // fullScaleSpanDb with the exponential shape above, so both shapes agree
    // on what "a full close" means.
    const auto linearReleaseDbPerSample = fullScaleSpanDb / releaseTimeSamples;

    // v0.4.0 F2 (downward expander): at the top of the Ratio range the loop
    // below takes the literal v0.3.x binary branch. That is a code-level
    // guarantee of neutrality, not a numerical coincidence of the expander
    // curve happening to become infinitely steep.
    const auto ratioNow = juce::jlimit (1.0f, ParamConstants::maxRatio, lastRatio);
    const auto isBinaryGate = ratioNow >= ParamConstants::maxRatio - ParamConstants::ratioGateEpsilon;
    const auto expanderSlope = ratioNow - 1.0f;

    for (size_t sample = 0; sample < numSamples; ++sample)
    {
        // v0.4.0 detector crossfade. Parked at 0 (Peak, the default) this is
        // exactly `1.0f * peak + 0.0f * rms`, i.e. the v0.3.x envelope
        // sample unchanged.
        const auto detectorWeight = detectorMix.getNextValue();
        const auto envelopeLinear = (1.0f - detectorWeight) * monoData[sample]
                                     + detectorWeight * rmsData[sample];
        const auto envelopeDb = juce::Decibels::gainToDecibels (envelopeLinear, minusInfinityDb);

        if (! gateOpen && envelopeDb >= lastThresholdDb)
        {
            gateOpen = true;
            holdCounterSamples = holdTimeSamples;
        }
        else if (gateOpen)
        {
            if (envelopeDb >= closeThresholdDb)
                holdCounterSamples = holdTimeSamples;
            else if (holdCounterSamples > 0)
                --holdCounterSamples;
            else
                gateOpen = false;
        }

        float targetGainDb;

        if (isBinaryGate)
        {
            // ---- v0.3.x binary path, reproduced literally ----
            float openness; // 0 == fully closed target, 1 == fully open target

            if (hardKnee)
            {
                openness = gateOpen ? 1.0f : 0.0f;
            }
            else
            {
                const auto normalised = juce::jlimit (0.0f, 1.0f, (envelopeDb - kneeLowerDb) / kneeDbNow);
                openness = normalised * normalised * (3.0f - 2.0f * normalised); // smoothstep
            }

            // Hold guarantees a fully open target for its whole duration,
            // regardless of the knee curve's instantaneous value.
            if (gateOpen && holdCounterSamples > 0)
                openness = 1.0f;

            // Duck inverts the target: attenuate above Threshold instead of
            // opening above it, reusing the exact same detection/hysteresis/
            // hold/knee machinery above.
            if (duckingMode)
                openness = 1.0f - openness;

            targetGainDb = juce::jmap (openness, rangeDbNow, 0.0f);
        }
        else
        {
            // ---- v0.4.0 downward-expander curve ----
            // (research-gate-expander.md §2.2; Giannoulis/Massberg/Reiss
            // placement: the whole gain computer stays in the dB domain.)
            //
            //   hard knee:  G = 0                            , L >= T
            //               G = (R - 1) * (L - T)            , L <  T
            //   soft knee:  G = -(R - 1) * (T + W/2 - L)^2 / (2W)
            //                                                , |2(L - T)| <= W
            //
            // The soft-knee branch is continuous with the hard-knee one in
            // both value and slope at both knee edges, so Knee widens the
            // transition without introducing a corner anywhere.
            //
            // Duck is handled by reflecting the detector level about
            // Threshold rather than by a second curve: the reflection maps
            // the "quiet" side of the law onto the "loud" side (and the knee
            // band onto itself), which is exactly what ducking means, and it
            // keeps a single expression to reason about and test.
            const auto level = duckingMode ? (2.0f * lastThresholdDb - envelopeDb) : envelopeDb;
            const auto overshootDb = level - lastThresholdDb;

            float expanderGainDb;

            if (! hardKnee && std::abs (2.0f * overshootDb) <= kneeDbNow)
            {
                const auto distanceIntoKnee = lastThresholdDb + kneeDbNow * 0.5f - level;
                expanderGainDb = -expanderSlope * distanceIntoKnee * distanceIntoKnee / (2.0f * kneeDbNow);
            }
            else if (overshootDb >= 0.0f)
            {
                expanderGainDb = 0.0f;
            }
            else
            {
                expanderGainDb = expanderSlope * overshootDb;
            }

            // Hold keeps its meaning: for its whole duration the target is
            // pinned to the fully-passed value (or, when ducking, the fully
            // attenuated one), exactly as in the binary path.
            if (gateOpen && holdCounterSamples > 0)
                expanderGainDb = duckingMode ? rangeDbNow : 0.0f;

            // Range is the floor of the expander, not a separate stage: no
            // matter how far below Threshold the signal sits, attenuation
            // stops there.
            targetGainDb = std::max (expanderGainDb, rangeDbNow);
        }

        const auto diffFromTargetDb = currentGainDb - targetGainDb;

        if (diffFromTargetDb < 0.0f) // attacking: current below target, ramping up towards it
        {
            currentGainDb = targetGainDb + diffFromTargetDb * attackMultiplier;
        }
        else if (diffFromTargetDb > 0.0f) // releasing: current above target, ramping down towards it
        {
            // v0.4.0 F4: Linear walks down at a constant dB/sample rate (the
            // dB-linear VCA-integrator fade), so the tail's decay rate never
            // changes and a full close takes exactly the Release time.
            // Exponential is the v0.3.x program-dependent approach.
            if (useLinearReleaseShape)
                currentGainDb = std::max (currentGainDb - linearReleaseDbPerSample, targetGainDb);
            else
                currentGainDb = targetGainDb + diffFromTargetDb * releaseMultiplier;
        }

        // v0.4.0 F1 (Smooth Open): the ballistic trajectory is shaped by the
        // lookahead-windowed smoother and the result REPLACES the applied
        // gain (in series - never max()'d against the raw trajectory, which
        // would let a 0 ms attack's single-sample step win at the opening
        // edge and reintroduce the very click this removes; see
        // LookaheadRamp.h). The smoother runs unconditionally so it stays
        // warm, and the crossfade weight parked at 0 makes this exactly
        // `1.0f * currentGainDb + 0.0f * shaped`, i.e. v0.3.x unchanged.
        const auto shapedGainDb = openingRamp.process (currentGainDb);
        const auto smoothOpenWeight = smoothOpenMix.getNextValue();

        appliedGainDb = (1.0f - smoothOpenWeight) * currentGainDb + smoothOpenWeight * shapedGainDb;

        // F7 telemetry: intra-block extrema, plus one history point per
        // telemetryIntervalSamples carrying that sub-block's deepest
        // reduction. Two compares and (once per 64 samples) one lock-free
        // store - nothing that can allocate or block.
        blockMinGainDb = std::min (blockMinGainDb, appliedGainDb);
        blockMaxGainDb = std::max (blockMaxGainDb, appliedGainDb);
        telemetryRunningMinDb = std::min (telemetryRunningMinDb, appliedGainDb);
        ++telemetrySampleTime;

        if (++telemetryCounter >= telemetryIntervalSamples)
        {
            gainReductionHistory.push ({ telemetrySampleTime, telemetryRunningMinDb });
            telemetryCounter = 0;
            telemetryRunningMinDb = 0.0f;
        }

        const auto gainLinear = juce::Decibels::decibelsToGain (appliedGainDb, minusInfinityDb);
        const auto detectionChannelCount = detectionSub.getNumChannels();

        // F6 live lookahead: while a delay change is in flight, read both the
        // outgoing and incoming taps and blend them equal-power (the two taps
        // are effectively decorrelated, so a linear fade would dip). Parked at
        // 1 - the overwhelmingly common case - this collapses to the single
        // v0.3.x tap read, with no extra work and no arithmetic applied.
        const auto delayWeight = delayCrossfade.getNextValue();
        const auto crossfadingDelay = delayWeight < 1.0f;
        const auto outgoingTapGain = crossfadingDelay ? std::sqrt (1.0f - delayWeight) : 0.0f;
        const auto incomingTapGain = crossfadingDelay ? std::sqrt (delayWeight) : 1.0f;

        for (size_t channel = 0; channel < numChannelsToProcess; ++channel)
        {
            auto* channelData = block.getChannelPointer (channel);
            lookaheadDelay.pushSample (static_cast<int> (channel), channelData[sample]);

            float delayed;

            if (crossfadingDelay)
            {
                const auto outgoing = lookaheadDelay.popSample (static_cast<int> (channel),
                                                                 static_cast<float> (previousDelaySamples),
                                                                 false);
                delayed = outgoingTapGain * outgoing
                           + incomingTapGain * lookaheadDelay.popSample (static_cast<int> (channel),
                                                                          static_cast<float> (targetDelaySamples),
                                                                          true);
            }
            else
            {
                delayed = lookaheadDelay.popSample (static_cast<int> (channel));
            }

            if (listenMode)
            {
                // Listen bypasses the gain computer entirely: audition
                // exactly what the detector hears (post SC HPF, pre
                // envelope-follower), not the gated/ducked main signal.
                const auto detectionChannel = detectionChannelCount > 0
                                                   ? std::min (channel, detectionChannelCount - 1)
                                                   : channel;
                channelData[sample] = detectionChannelCount > 0
                                           ? detectionSub.getChannelPointer (detectionChannel)[sample]
                                           : 0.0f;
            }
            else
            {
                channelData[sample] = delayed * gainLinear;
            }
        }
    }
}
