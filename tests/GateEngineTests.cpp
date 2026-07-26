#include "dsp/GateEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr int testBlockSize = 4096;
    constexpr double testFrequencyHz = 1000.0;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels, int blockSize = testBlockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }
}

TEST_CASE ("Gate-open passthrough: Threshold at minimum + Range 0 dB is unity apart from lookahead delay", "[dsp][gate][null]")
{
    GateEngine engine;

    // Range = 0 dB means the gain computer's target is 0 dB whether the
    // gate is open or closed - the gate can therefore never attenuate, and
    // the whole signal path reduces to a pure delay. This is the correct
    // "always open" reference passthrough case (Threshold is set to its
    // minimum too, so the gate is also genuinely open throughout, not just
    // coincidentally unattenuated).
    engine.setThresholdDb (-80.0f);
    engine.setRangeDb (0.0f);
    engine.setAttackMs (1.0f);
    engine.setHoldMs (20.0f);
    engine.setReleaseMs (80.0f);
    engine.setLookaheadMs (5.0f);
    engine.setScHighpassHz (20.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    const auto latency = engine.getLatencySamples();
    REQUIRE (latency > 0);
    REQUIRE (latency < testBlockSize / 2);

    juce::AudioBuffer<float> reference (2, testBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    const auto overlapLength = testBlockSize - latency;
    REQUIRE (overlapLength > testBlockSize / 2);

    constexpr float tolerance = 1.0e-5f;

    for (int channel = 0; channel < reference.getNumChannels(); ++channel)
    {
        const auto* refData = reference.getReadPointer (channel);
        const auto* outData = processed.getReadPointer (channel);

        float maxResidual = 0.0f;

        for (int i = 0; i < overlapLength; ++i)
            maxResidual = std::max (maxResidual, std::abs (outData[latency + i] - refData[i]));

        CHECK (maxResidual < tolerance);
    }

    CHECK (engine.isGateOpen());
}

TEST_CASE ("Below-threshold signal is attenuated toward the Range floor", "[dsp][gate]")
{
    GateEngine engine;

    engine.setThresholdDb (-10.0f);
    engine.setRangeDb (-60.0f);
    engine.setAttackMs (1.0f);
    engine.setHoldMs (0.0f);
    engine.setReleaseMs (30.0f);
    // Isolate the gain-computer amplitude check from the lookahead delay.
    engine.setLookaheadMs (0.0f);
    engine.setScHighpassHz (20.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    // -40 dBFS peak: comfortably below both the open threshold (-10 dB) and
    // the close threshold (-13 dB, Threshold - hysteresis), so the gate
    // never opens and the gain computer sits at the Range floor throughout.
    constexpr float inputAmplitude = 0.01f; // -40 dBFS
    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, inputAmplitude);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK_FALSE (engine.isGateOpen());

    const auto expectedFloorGain = juce::Decibels::decibelsToGain (-60.0f);
    const auto expectedPeak = inputAmplitude * expectedFloorGain;

    // Measure only the back half of the buffer, safely past the release
    // ramp's settling time (30 ms release is a tiny fraction of the buffer
    // length at 48 kHz).
    const auto measureStart = testBlockSize / 2;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        float peak = 0.0f;
        const auto* data = buffer.getReadPointer (channel);

        for (int i = measureStart; i < testBlockSize; ++i)
            peak = std::max (peak, std::abs (data[i]));

        CHECK (peak == Catch::Approx (expectedPeak).margin (expectedPeak * 0.05f + 1.0e-6f));
    }
}

TEST_CASE ("Hysteresis: the gate's dead band prevents chatter", "[dsp][gate][hysteresis]")
{
    constexpr float thresholdDb = -20.0f;
    constexpr float loudDb = -10.0f;    // well above Threshold: opens the gate
    constexpr float betweenDb = -21.5f; // below Threshold, above Threshold - hysteresis (-23 dB): the dead band

    const auto loudAmplitude = juce::Decibels::decibelsToGain (loudDb);
    const auto betweenAmplitude = juce::Decibels::decibelsToGain (betweenDb);

    auto configureEngine = [] (GateEngine& engine)
    {
        engine.setThresholdDb (thresholdDb);
        engine.setRangeDb (-60.0f);
        engine.setAttackMs (1.0f);
        engine.setHoldMs (0.0f);
        engine.setReleaseMs (30.0f);
        engine.setLookaheadMs (0.0f);
        engine.setScHighpassHz (20.0f);
        engine.prepare (makeTestSpec (2, 512));
    };

    auto processChunks = [] (GateEngine& engine, float amplitude, int numChunks, int& openTransitions, int& closeTransitions, bool& previousState)
    {
        for (int c = 0; c < numChunks; ++c)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, amplitude);

            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block);

            const auto currentState = engine.isGateOpen();
            if (currentState && ! previousState)
                ++openTransitions;
            if (! currentState && previousState)
                ++closeTransitions;
            previousState = currentState;
        }
    };

    SECTION ("gate opened while loud stays open once it drops into the dead band, with zero chatter")
    {
        GateEngine engine;
        configureEngine (engine);

        int openTransitions = 0;
        int closeTransitions = 0;
        bool previousState = engine.isGateOpen();

        // Phase 1: loud signal opens the gate (~213 ms, well past the 1 ms attack).
        processChunks (engine, loudAmplitude, 20, openTransitions, closeTransitions, previousState);
        REQUIRE (engine.isGateOpen());

        // Phase 2: drop into the dead band. A single-threshold comparator
        // would close here (betweenDb < thresholdDb); hysteresis must keep
        // the gate open with zero chatter because betweenDb is still above
        // the close threshold (thresholdDb - hysteresis).
        processChunks (engine, betweenAmplitude, 20, openTransitions, closeTransitions, previousState);
        CHECK (engine.isGateOpen());

        CHECK (openTransitions == 1);
        CHECK (closeTransitions == 0);
    }

    SECTION ("the dead-band level alone, from a closed start, never opens the gate")
    {
        GateEngine engine;
        configureEngine (engine);

        REQUIRE_FALSE (engine.isGateOpen());

        int openTransitions = 0;
        int closeTransitions = 0;
        bool previousState = engine.isGateOpen();

        processChunks (engine, betweenAmplitude, 20, openTransitions, closeTransitions, previousState);

        CHECK_FALSE (engine.isGateOpen());
        CHECK (openTransitions == 0);
    }
}

TEST_CASE ("Engine reset() clears filter/envelope/delay-line state without crashing", "[dsp][gate]")
{
    GateEngine engine;
    engine.setThresholdDb (-30.0f);
    engine.setRangeDb (-60.0f);
    engine.setLookaheadMs (5.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (2, testBlockSize);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK_NOTHROW (engine.reset());
    CHECK (TestHelpers::allSamplesFinite (buffer));

    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, 0.9f);
    CHECK_NOTHROW (engine.process (block));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Zero-sample block is a safe no-op", "[dsp][gate]")
{
    GateEngine engine;
    engine.setLookaheadMs (5.0f);

    const auto spec = makeTestSpec (2);
    engine.prepare (spec);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::dsp::AudioBlock<float> block (buffer);

    CHECK_NOTHROW (engine.process (block));
}

TEST_CASE ("Oversized host block (larger than the size promised to prepare()) is chunked safely and fully processed", "[dsp][gate][robustness]")
{
    // Issue #12: detectionBuffer/monoEnvelopeBuffer are allocated in
    // prepare() for exactly spec.maximumBlockSize samples. A host is not
    // guaranteed to honour that promise (JUCE's own AudioProcessor::
    // processBlock docs warn block sizes "may be more or less than" the
    // prepared value - e.g. an offline bounce or a "Multiprocessor
    // Rendering" host), so process() must not write past that capacity
    // when handed a larger block.
    GateEngine engine;

    // Range = 0 dB + Threshold at its minimum reduces the whole engine to
    // a pure delay (see the null-test reference at the top of this file),
    // so the expected output for *every* sample of an oversized block -
    // not just the first preparedBlockSize of them - is the delayed input.
    // This is what distinguishes a fix that fully processes an oversized
    // block (chunking) from one that merely truncates/drops its tail to
    // avoid the overflow.
    engine.setThresholdDb (-80.0f);
    engine.setRangeDb (0.0f);
    engine.setAttackMs (1.0f);
    engine.setHoldMs (20.0f);
    engine.setReleaseMs (80.0f);
    engine.setLookaheadMs (1.0f);
    engine.setScHighpassHz (20.0f);

    // Deliberately tiny prepared capacity so a still-small, entirely
    // realistic block is nonetheless many times larger than it.
    constexpr int preparedBlockSize = 32;
    const auto spec = makeTestSpec (2, preparedBlockSize);
    engine.prepare (spec);

    const auto latency = engine.getLatencySamples();
    REQUIRE (latency > 0);

    constexpr int oversizedBlockSize = preparedBlockSize * 64; // 2048 samples, 64x prepared capacity

    juce::AudioBuffer<float> reference (2, oversizedBlockSize);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.5f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    CHECK_NOTHROW (engine.process (block));

    CHECK (TestHelpers::allSamplesFinite (processed));

    const auto overlapLength = oversizedBlockSize - latency;
    REQUIRE (overlapLength > oversizedBlockSize / 2);

    constexpr float tolerance = 1.0e-5f;

    for (int channel = 0; channel < reference.getNumChannels(); ++channel)
    {
        const auto* refData = reference.getReadPointer (channel);
        const auto* outData = processed.getReadPointer (channel);

        float maxResidual = 0.0f;

        for (int i = 0; i < overlapLength; ++i)
            maxResidual = std::max (maxResidual, std::abs (outData[latency + i] - refData[i]));

        CHECK (maxResidual < tolerance);
    }
}

//==============================================================================
// v0.4.0.
//==============================================================================

#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "GoldenFixture.h"
#include "data/GoldenRenders.h"

#include <algorithm>
#include <vector>

//==============================================================================
// T4 - Ratio at its maximum is the old gate, not merely something that
// resembles it.
//
// The first section is the one with teeth: it compares against a render
// captured from an actual v0.3.x build, which had no Ratio parameter at all.
// The second is a same-binary check and is labelled as such - useful for
// localising a failure, worthless as a cross-version guarantee.
//==============================================================================

TEST_CASE ("Ratio at its maximum takes the legacy binary-gate path", "[dsp][ratio][neutrality][golden]")
{
    SECTION ("with Ratio at maximum the render matches the pre-v0.4.0 golden")
    {
        SilentiumAudioProcessor processor;
        GoldenFixture::applyLegacySettings (processor);

        auto* ratio = processor.apvts.getParameter (ParamIDs::ratio);
        REQUIRE (ratio != nullptr);
        ratio->setValueNotifyingHost (ratio->convertTo0to1 (ParamConstants::maxRatio));

        const auto fingerprint = GoldenFixture::fingerprintOf (GoldenFixture::render (processor));

        REQUIRE (static_cast<int> (fingerprint.windowRmsDb.size()) == GoldenRenders::numWindows);

        auto worstDeviation = 0.0;

        for (int window = 0; window < GoldenRenders::numWindows; ++window)
            worstDeviation = std::max ({ worstDeviation,
                                         std::abs (fingerprint.windowRmsDb[static_cast<size_t> (window)]
                                                    - GoldenRenders::legacyStateRms[window]),
                                         std::abs (fingerprint.windowPeakDb[static_cast<size_t> (window)]
                                                    - GoldenRenders::legacyStatePeak[window]) });

        INFO ("worst deviation from the v0.3.x golden: " << worstDeviation << " dB");
        CHECK (worstDeviation <= GoldenFixture::fingerprintToleranceDb);
    }

    SECTION ("one step below maximum is audibly a different device (so the check above is not vacuous)")
    {
        SilentiumAudioProcessor processor;
        GoldenFixture::applyLegacySettings (processor);

        auto* ratio = processor.apvts.getParameter (ParamIDs::ratio);
        REQUIRE (ratio != nullptr);
        ratio->setValueNotifyingHost (ratio->convertTo0to1 (4.0f));

        const auto fingerprint = GoldenFixture::fingerprintOf (GoldenFixture::render (processor));

        auto largestDeviation = 0.0;

        for (int window = 0; window < GoldenRenders::numWindows; ++window)
            largestDeviation = std::max (largestDeviation,
                                          std::abs (fingerprint.windowRmsDb[static_cast<size_t> (window)]
                                                     - GoldenRenders::legacyStateRms[window]));

        // An expander at 4:1 leaves the decaying tails far louder than a gate
        // does; if this were small, the parameter would not be doing anything.
        INFO ("largest deviation at 4:1: " << largestDeviation << " dB");
        CHECK (largestDeviation > 3.0);
    }
}

//==============================================================================
// T10 - Hysteresis is calibrated, not merely present.
//==============================================================================

TEST_CASE ("Hysteresis: the open/close gap equals the Hysteresis setting", "[dsp][hysteresis]")
{
    constexpr float thresholdDb = -40.0f;
    constexpr double rampDbPerSecond = 10.0;

    for (const auto hysteresisDb : { 0.0f, 3.0f, 6.0f, 12.0f })
    {
        GateEngine engine;
        engine.setThresholdDb (thresholdDb);
        engine.setRangeDb (-60.0f);
        engine.setAttackMs (1.0f);
        engine.setHoldMs (0.0f);
        engine.setReleaseMs (5.0f);
        engine.setLookaheadMs (0.0f);
        engine.setScHighpassHz (20.0f);
        engine.setScLowpassHz (16000.0f);
        engine.setHysteresisDb (hysteresisDb);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = 1;
        spec.numChannels = 1;
        engine.prepare (spec);

        // A slow triangular level ramp through Threshold and back: up first
        // (which finds the open point), then down (which finds the close
        // point). 10 dB/s is slow enough that the detector tracks it without
        // meaningful lag.
        constexpr auto startDb = -60.0f;
        constexpr auto peakDb = -20.0f;
        const auto rampSamples = static_cast<int> ((peakDb - startDb) / rampDbPerSecond * testSampleRate);

        auto openLevelDb = 0.0f;
        auto closeLevelDb = 0.0f;
        auto wasOpen = false;

        juce::AudioBuffer<float> buffer (1, 1);

        for (int i = 0; i < rampSamples * 2; ++i)
        {
            const auto rising = i < rampSamples;
            const auto progress = rising ? static_cast<float> (i) / static_cast<float> (rampSamples)
                                          : 1.0f - static_cast<float> (i - rampSamples) / static_cast<float> (rampSamples);
            const auto levelDb = juce::jmap (progress, startDb, peakDb);

            const auto phase = juce::MathConstants<double>::twoPi * 2000.0 * static_cast<double> (i) / testSampleRate;
            buffer.setSample (0, 0, juce::Decibels::decibelsToGain (levelDb) * static_cast<float> (std::sin (phase)));

            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block);

            const auto isOpen = engine.isGateOpen();

            if (isOpen && ! wasOpen)
                openLevelDb = levelDb;
            else if (! isOpen && wasOpen)
                closeLevelDb = levelDb;

            wasOpen = isOpen;
        }

        INFO ("hysteresis " << hysteresisDb << " dB: opened at " << openLevelDb
                << " dBFS, closed at " << closeLevelDb << " dBFS");

        CHECK (openLevelDb - closeLevelDb == Catch::Approx (hysteresisDb).margin (1.0));
    }
}

//==============================================================================
// T11 - chatter immunity, which is the entire reason hysteresis exists.
//==============================================================================

namespace
{
    int countGateTransitions (GateEngine& engine, const std::vector<float>& input)
    {
        juce::AudioBuffer<float> buffer (1, 1);
        auto transitions = 0;
        auto wasOpen = engine.isGateOpen();
        auto first = true;

        for (const auto value : input)
        {
            buffer.setSample (0, 0, value);
            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block);

            const auto isOpen = engine.isGateOpen();

            if (! first && isOpen != wasOpen)
                ++transitions;

            wasOpen = isOpen;
            first = false;
        }

        return transitions;
    }
}

TEST_CASE ("Hysteresis: a signal dithering around Threshold does not chatter", "[dsp][hysteresis][chatter]")
{
    constexpr float thresholdDb = -30.0f;

    GateEngine engine;
    engine.setThresholdDb (thresholdDb);
    engine.setRangeDb (-60.0f);
    engine.setAttackMs (1.0f);
    engine.setHoldMs (0.0f);
    engine.setReleaseMs (50.0f);
    engine.setLookaheadMs (0.0f);
    engine.setScHighpassHz (20.0f);
    engine.setScLowpassHz (16000.0f);
    engine.setHysteresisDb (4.0f);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = testSampleRate;
    spec.maximumBlockSize = 1;
    spec.numChannels = 1;
    engine.prepare (spec);

    // A 2 kHz tone whose level wanders +/-1.5 dB around Threshold at 7 Hz -
    // i.e. entirely inside the 4 dB hysteresis band, which is exactly the
    // case a single-threshold comparator turns into machine-gun chatter.
    const auto totalSamples = static_cast<size_t> (testSampleRate * 2.0);
    std::vector<float> input (totalSamples);

    for (size_t i = 0; i < totalSamples; ++i)
    {
        const auto t = static_cast<double> (i) / testSampleRate;
        const auto levelDb = thresholdDb + 1.5f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 7.0 * t));
        const auto phase = juce::MathConstants<double>::twoPi * 2000.0 * t;
        input[i] = juce::Decibels::decibelsToGain (levelDb) * static_cast<float> (std::sin (phase));
    }

    const auto transitions = countGateTransitions (engine, input);

    // One transition is expected and correct: the gate opens once, at the
    // start, and then stays open because nothing ever falls 4 dB below
    // Threshold again.
    INFO ("gate transitions over 2 s of dithered level: " << transitions);
    CHECK (transitions <= 1);
}

namespace
{
    // A mono engine set up purely to count gate transitions on a given
    // stimulus, with everything except the detector held constant.
    void prepareChatterEngine (GateEngine& engine, float thresholdDb, float hysteresisDb, bool useRms)
    {
        engine.setThresholdDb (thresholdDb);
        engine.setRangeDb (-60.0f);
        engine.setAttackMs (1.0f);
        engine.setHoldMs (0.0f);
        engine.setReleaseMs (20.0f);
        engine.setLookaheadMs (0.0f);
        engine.setScHighpassHz (20.0f);
        engine.setScLowpassHz (16000.0f);
        engine.setHysteresisDb (hysteresisDb);
        engine.setDetectorMode (useRms);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = 1;
        spec.numChannels = 1;
        engine.prepare (spec);
    }
}

TEST_CASE ("Detector: RMS mode holds steady on a drop-tuned fundamental sitting on Threshold", "[dsp][detector][chatter]")
{
    constexpr float thresholdDb = -30.0f;
    constexpr double lowFrequencyHz = 70.0;

    GateEngine engine;
    // With the working hysteresis the brief specifies for this case. The RMS
    // window is 5 ms against a 14 ms period, so the mean-square ripple is not
    // fully smoothed - it lands within a couple of dB, which is exactly what
    // the hysteresis band is sized to absorb.
    prepareChatterEngine (engine, thresholdDb, 4.0f, true);

    const auto totalSamples = static_cast<size_t> (testSampleRate);
    std::vector<float> input (totalSamples);

    // Level chosen so the RMS reading - 3 dB below peak for a sine - lands on
    // Threshold exactly: the worst case for this detector, not one that
    // flatters it.
    const auto amplitude = juce::Decibels::decibelsToGain (thresholdDb + 3.0f);

    for (size_t i = 0; i < totalSamples; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * lowFrequencyHz
                            * static_cast<double> (i) / testSampleRate;
        input[i] = amplitude * static_cast<float> (std::sin (phase));
    }

    const auto transitions = countGateTransitions (engine, input);

    INFO ("gate transitions in 1 s at 70 Hz sitting on Threshold, RMS + 4 dB hysteresis: " << transitions);
    CHECK (transitions <= 1);
}

TEST_CASE ("Detector: RMS ignores isolated spikes that make the peak detector chatter", "[dsp][detector][chatter]")
{
    // This is what the RMS mode is actually for. A peak follower opens the
    // gate on any excursion, however brief - so a sustained-but-quiet signal
    // carrying occasional spikes (fret noise, a noisy DI, a nearby snare
    // bleeding into a guitar mic) machine-guns the gate. A mean-square window
    // weighs those spikes by how much energy they actually carry, which is
    // almost none.
    constexpr float thresholdDb = -25.0f;

    auto countFor = [] (bool useRms)
    {
        GateEngine engine;
        prepareChatterEngine (engine, thresholdDb, 0.0f, useRms); // no hysteresis: isolate the detector

        const auto totalSamples = static_cast<size_t> (testSampleRate);
        std::vector<float> input (totalSamples, 0.0f);

        const auto bedAmplitude = juce::Decibels::decibelsToGain (-45.0f);   // well below Threshold
        const auto spikeAmplitude = juce::Decibels::decibelsToGain (-6.0f);  // well above it
        const auto spikeIntervalSamples = static_cast<size_t> (testSampleRate * 0.02); // every 20 ms

        for (size_t i = 0; i < totalSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 1000.0
                                * static_cast<double> (i) / testSampleRate;
            input[i] = bedAmplitude * static_cast<float> (std::sin (phase));

            if (i % spikeIntervalSamples == 0)
                input[i] = spikeAmplitude; // a single-sample excursion
        }

        return countGateTransitions (engine, input);
    };

    const auto peakTransitions = countFor (false);
    const auto rmsTransitions = countFor (true);

    INFO ("transitions in 1 s of spiky material - peak: " << peakTransitions
            << ", RMS: " << rmsTransitions);

    CHECK (peakTransitions > 20);   // the peak follower opens on every spike
    CHECK (rmsTransitions == 0);    // the RMS window sees almost no energy in them
}

//==============================================================================
// T19 - gain-reduction telemetry.
//==============================================================================

TEST_CASE ("Telemetry: the block extrema bracket the block-boundary value", "[dsp][telemetry]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    auto setParameter = [&processor] (const char* id, float value)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    setParameter (ParamIDs::threshold, -30.0f);
    setParameter (ParamIDs::range, -60.0f);
    setParameter (ParamIDs::attack, 1.0f);
    setParameter (ParamIDs::hold, 5.0f);
    setParameter (ParamIDs::release, 30.0f);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    // Bursts and gaps, so the gate really is moving inside blocks.
    for (int b = 0; b < 200; ++b)
    {
        const auto loud = (b / 4) % 2 == 0;
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, loud ? 0.5f : 0.00001f, b * 256);

        processor.processBlock (buffer, midi);

        const auto minDb = processor.getGainReductionMinDb();
        const auto maxDb = processor.getGainReductionMaxDb();
        const auto boundaryDb = processor.getGainReductionDb();

        INFO ("block " << b << ": min " << minDb << ", boundary " << boundaryDb << ", max " << maxDb);
        CHECK (minDb <= boundaryDb + 1.0e-4f);
        CHECK (boundaryDb <= maxDb + 1.0e-4f);
        CHECK (std::isfinite (minDb));
        CHECK (std::isfinite (maxDb));
    }
}

TEST_CASE ("Telemetry: the history ring is FIFO and refuses to clobber unread entries", "[dsp][telemetry]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto& history = processor.getGainReductionHistory();

    SECTION ("a producing run fills it in order, and it is drained in that order")
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (int b = 0; b < 8; ++b)
        {
            TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f, b * 512);
            processor.processBlock (buffer, midi);
        }

        // 8 blocks of 512 samples, one point per 64 samples.
        CHECK (history.getNumReady() == 8 * 512 / GateEngine::telemetryIntervalSamples);

        GateEngine::GainReductionPoint point;
        std::int64_t previousSampleTime = -1;
        auto popped = 0;

        while (history.pop (point))
        {
            CHECK (point.sampleTime > previousSampleTime);
            CHECK (point.gainReductionDb <= 0.0f);
            CHECK (std::isfinite (point.gainReductionDb));
            previousSampleTime = point.sampleTime;
            ++popped;
        }

        CHECK (popped == 8 * 512 / GateEngine::telemetryIntervalSamples);
        CHECK (history.getNumReady() == 0);
    }

    SECTION ("when full it refuses new entries rather than overwriting the oldest unread one")
    {
        history.clear();

        // The ring holds capacity - 1 entries: one slot is always kept empty
        // so full and empty stay distinguishable.
        const auto usableCapacity = GateEngine::GainReductionHistory::capacity - 1;

        for (int i = 0; i < usableCapacity; ++i)
            CHECK (history.push ({ static_cast<std::int64_t> (i), -static_cast<float> (i) }));

        CHECK (history.getNumReady() == usableCapacity);
        CHECK (! history.push ({ 999999, -1.0f }));

        // The oldest entry must still be the one that was written first.
        GateEngine::GainReductionPoint point;
        REQUIRE (history.pop (point));
        CHECK (point.sampleTime == 0);

        // ...and now that a slot is free, a push succeeds again.
        CHECK (history.push ({ 999999, -1.0f }));
    }
}
