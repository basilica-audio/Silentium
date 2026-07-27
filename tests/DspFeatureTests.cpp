// Coverage for the M1 DSP additions: soft-knee, ducking mode, detection
// listen mode, and the optional external sidechain input - see
// docs/architecture.md for how each sits on top of the v0.1 hysteresis/hold
// state machine. GateEngineTests.cpp's null/hysteresis/reset/zero-block
// tests are left untouched and still pass unmodified, since every one of
// these features defaults off (Knee 0 dB, Duck/Listen false, no sidechain
// block) and reproduces the original v0.1 behaviour exactly at its default.

#include "PluginProcessor.h"
#include "dsp/GateEngine.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double testSampleRate = 48000.0;
    constexpr double testFrequencyHz = 1000.0;

    juce::dsp::ProcessSpec makeTestSpec (int numChannels, int blockSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32> (blockSize);
        spec.numChannels = static_cast<juce::uint32> (numChannels);
        return spec;
    }

    // Processes numBlocks consecutive blocks of a steady sine into engine,
    // returning the peak absolute sample value of the last block (by which
    // point attack/release ramps have settled for the fast times used
    // below).
    float processSteadyStateAndMeasurePeak (GateEngine& engine, float amplitude, int blockSize, int numBlocks)
    {
        float lastPeak = 0.0f;

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, amplitude);

            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block);

            if (b == numBlocks - 1)
                lastPeak = TestHelpers::peakAbsolute (buffer);
        }

        return lastPeak;
    }
}

TEST_CASE ("Knee: 0 dB reproduces the hard-knee target exactly", "[dsp][knee]")
{
    GateEngine hardKnee;
    hardKnee.setThresholdDb (-20.0f);
    hardKnee.setRangeDb (-40.0f);
    hardKnee.setAttackMs (0.1f);
    hardKnee.setHoldMs (0.0f);
    hardKnee.setReleaseMs (5.0f);
    hardKnee.setLookaheadMs (0.0f);
    hardKnee.setScHighpassHz (20.0f);
    hardKnee.setKneeDb (0.0f);
    hardKnee.prepare (makeTestSpec (2, 512));

    // Steady input exactly at Threshold: with a 0 dB knee the gate opens
    // (envelope >= Threshold) and settles at unity, matching the original
    // v0.1 hard-knee behaviour exactly.
    const auto atThreshold = juce::Decibels::decibelsToGain (-20.0f);
    const auto peakHardKnee = processSteadyStateAndMeasurePeak (hardKnee, atThreshold, 512, 40);

    CHECK (peakHardKnee == Catch::Approx (atThreshold).margin (atThreshold * 0.1f));
}

TEST_CASE ("Knee: a wide knee softens the target below unity for an envelope centred on Threshold", "[dsp][knee]")
{
    GateEngine softKnee;
    softKnee.setThresholdDb (-20.0f);
    softKnee.setRangeDb (-40.0f);
    softKnee.setAttackMs (0.1f);
    softKnee.setHoldMs (0.0f);
    softKnee.setReleaseMs (5.0f);
    softKnee.setLookaheadMs (0.0f);
    softKnee.setScHighpassHz (20.0f);
    softKnee.setKneeDb (12.0f); // band: [-26 dB, -14 dB], centred on Threshold
    softKnee.prepare (makeTestSpec (2, 512));

    const auto atThreshold = juce::Decibels::decibelsToGain (-20.0f);
    const auto peakSoftKnee = processSteadyStateAndMeasurePeak (softKnee, atThreshold, 512, 40);

    // Smoothstep at the exact centre of the knee band maps to openness 0.5,
    // i.e. a gain-computer target roughly midway (in dB) between Range and
    // 0 dB, clearly less attenuated than a fully closed gate would leave
    // this input (atThreshold * Range) and clearly more attenuated than a
    // fully open one (atThreshold * unity).
    const auto rangeGainMultiplier = juce::Decibels::decibelsToGain (-40.0f);
    const auto fullyClosedOutput = atThreshold * rangeGainMultiplier;
    const auto fullyOpenOutput = atThreshold;

    CHECK (peakSoftKnee < fullyOpenOutput * 0.9f);    // measurably below fully open
    CHECK (peakSoftKnee > fullyClosedOutput * 2.0f);  // measurably above fully closed
}

TEST_CASE ("Knee: Hold still forces a fully open target regardless of the knee curve", "[dsp][knee][hold]")
{
    GateEngine engine;
    engine.setThresholdDb (-20.0f);
    engine.setRangeDb (-40.0f);
    engine.setAttackMs (0.1f);
    engine.setHoldMs (200.0f);
    engine.setReleaseMs (5.0f);
    engine.setLookaheadMs (0.0f);
    engine.setScHighpassHz (20.0f);
    engine.setKneeDb (12.0f);
    engine.prepare (makeTestSpec (2, 512));

    // Open the gate with a loud transient, then drop straight into the
    // knee's lower half (well below Threshold, so the continuous curve
    // alone would attenuate) - Hold must keep the target at 0 dB regardless.
    const auto loud = juce::Decibels::decibelsToGain (-5.0f);
    const auto low = juce::Decibels::decibelsToGain (-25.0f); // inside the knee band, below Threshold

    juce::AudioBuffer<float> openBuffer (2, 512);
    TestHelpers::fillWithSine (openBuffer, testSampleRate, testFrequencyHz, loud);
    juce::dsp::AudioBlock<float> openBlock (openBuffer);
    engine.process (openBlock);
    REQUIRE (engine.isGateOpen());

    juce::AudioBuffer<float> holdBuffer (2, 512);
    TestHelpers::fillWithSine (holdBuffer, testSampleRate, testFrequencyHz, low);
    juce::dsp::AudioBlock<float> holdBlock (holdBuffer);
    engine.process (holdBlock);

    CHECK (engine.isGateOpen());
    CHECK (TestHelpers::peakAbsolute (holdBuffer) == Catch::Approx (low).margin (low * 0.1f));
}

TEST_CASE ("Duck: inverts the gain computer's target", "[dsp][duck]")
{
    GateEngine engine;
    engine.setThresholdDb (-10.0f);
    engine.setRangeDb (-60.0f);
    engine.setAttackMs (1.0f);
    engine.setHoldMs (0.0f);
    engine.setReleaseMs (10.0f);
    engine.setLookaheadMs (0.0f);
    engine.setScHighpassHz (20.0f);
    engine.setDuckingMode (true);
    engine.prepare (makeTestSpec (2, 2048));

    // Loud signal, well above Threshold: a ducker attenuates towards Range.
    const auto loudAmplitude = juce::Decibels::decibelsToGain (0.0f);
    juce::AudioBuffer<float> loudBuffer (2, 2048);
    TestHelpers::fillWithSine (loudBuffer, testSampleRate, testFrequencyHz, loudAmplitude);
    juce::dsp::AudioBlock<float> loudBlock (loudBuffer);
    engine.process (loudBlock);

    const auto rangeGain = juce::Decibels::decibelsToGain (-60.0f);
    const auto measureStart = 1024; // past the release-then-attack settling window
    float duckedPeak = 0.0f;

    for (int i = measureStart; i < loudBuffer.getNumSamples(); ++i)
        duckedPeak = std::max (duckedPeak, std::abs (loudBuffer.getReadPointer (0)[i]));

    CHECK (duckedPeak == Catch::Approx (loudAmplitude * rangeGain).margin (loudAmplitude * rangeGain * 0.2f + 1.0e-6f));

    // Quiet signal, well below Threshold: a ducker passes it at unity.
    const auto quietAmplitude = juce::Decibels::decibelsToGain (-40.0f);
    juce::AudioBuffer<float> quietBuffer (2, 2048);
    TestHelpers::fillWithSine (quietBuffer, testSampleRate, testFrequencyHz, quietAmplitude);
    juce::dsp::AudioBlock<float> quietBlock (quietBuffer);
    engine.process (quietBlock);

    float passedPeak = 0.0f;

    for (int i = measureStart; i < quietBuffer.getNumSamples(); ++i)
        passedPeak = std::max (passedPeak, std::abs (quietBuffer.getReadPointer (0)[i]));

    CHECK (passedPeak == Catch::Approx (quietAmplitude).margin (quietAmplitude * 0.1f));
}

TEST_CASE ("Duck defaults to off: unmodified gate behaviour", "[dsp][duck]")
{
    GateEngine engine;
    CHECK_FALSE (engine.isGateOpen()); // sanity: default-constructed engine starts closed

    engine.setThresholdDb (-10.0f);
    engine.setRangeDb (-60.0f);
    engine.setLookaheadMs (0.0f);
    engine.prepare (makeTestSpec (2, 512));

    const auto loudAmplitude = juce::Decibels::decibelsToGain (0.0f);
    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, loudAmplitude);
    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);

    CHECK (engine.isGateOpen()); // gate, not duck: loud signal opens it
}

TEST_CASE ("Listen: routes the SC-filtered detection signal to the output", "[dsp][listen]")
{
    GateEngine engine;
    // Threshold pinned high so the gain computer would normally attenuate
    // everything toward Range - Listen must bypass that entirely.
    engine.setThresholdDb (0.0f);
    engine.setRangeDb (-80.0f);
    engine.setLookaheadMs (0.0f);
    engine.setScHighpassHz (80.0f);
    engine.setListenMode (true);
    engine.prepare (makeTestSpec (2, 4096));

    SECTION ("an in-band signal passes through near full amplitude, not attenuated to Range")
    {
        const auto amplitude = 0.5f;
        juce::AudioBuffer<float> buffer (2, 4096);
        TestHelpers::fillWithSine (buffer, testSampleRate, 1000.0, amplitude);

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        CHECK_FALSE (engine.isGateOpen()); // the gate itself never opened...
        CHECK (TestHelpers::peakAbsolute (buffer) > amplitude * 0.8f); // ...but Listen bypasses that
    }

    SECTION ("a sub-cutoff signal is heavily attenuated by the SC HPF")
    {
        const auto amplitude = 0.5f;
        juce::AudioBuffer<float> buffer (2, 4096);
        TestHelpers::fillWithSine (buffer, testSampleRate, 20.0, amplitude); // well below the 80 Hz SC HPF

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        // Measure only the back half, past the HPF's settling transient.
        float peak = 0.0f;

        for (int i = 2048; i < buffer.getNumSamples(); ++i)
            peak = std::max (peak, std::abs (buffer.getReadPointer (0)[i]));

        CHECK (peak < amplitude * 0.5f);
    }
}

TEST_CASE ("Listen defaults to off: output is the normal gated signal", "[dsp][listen]")
{
    GateEngine engine;
    engine.setThresholdDb (-80.0f);
    engine.setRangeDb (0.0f); // always-open reference, see GateEngineTests.cpp's null test
    engine.setLookaheadMs (0.0f);
    engine.prepare (makeTestSpec (2, 512));

    juce::AudioBuffer<float> reference (2, 512);
    TestHelpers::fillWithSine (reference, testSampleRate, testFrequencyHz, 0.5f);
    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (reference);

    juce::dsp::AudioBlock<float> block (processed);
    engine.process (block);

    for (int i = 0; i < reference.getNumSamples(); ++i)
        CHECK (processed.getReadPointer (0)[i] == Catch::Approx (reference.getReadPointer (0)[i]).margin (1.0e-5f));
}

TEST_CASE ("External sidechain: a loud sidechain opens the gate even when the main input is quiet", "[dsp][sidechain]")
{
    GateEngine engine;
    engine.setThresholdDb (-20.0f);
    engine.setRangeDb (-60.0f);
    engine.setAttackMs (0.1f);
    engine.setHoldMs (0.0f);
    engine.setReleaseMs (10.0f);
    engine.setLookaheadMs (0.0f);
    engine.setScHighpassHz (20.0f);
    engine.prepare (makeTestSpec (2, 2048));

    juce::AudioBuffer<float> mainBuffer (2, 2048);
    TestHelpers::fillWithSine (mainBuffer, testSampleRate, testFrequencyHz, juce::Decibels::decibelsToGain (-50.0f)); // well below Threshold on its own

    juce::AudioBuffer<float> sidechainBuffer (2, 2048);
    TestHelpers::fillWithSine (sidechainBuffer, testSampleRate, testFrequencyHz, juce::Decibels::decibelsToGain (0.0f)); // well above Threshold

    juce::dsp::AudioBlock<float> mainBlock (mainBuffer);
    juce::dsp::AudioBlock<float> sidechainBlock (sidechainBuffer);
    engine.process (mainBlock, &sidechainBlock);

    CHECK (engine.isGateOpen());

    // The (quiet) main signal should have passed close to unity, not been
    // attenuated toward Range, since the gate opened via the sidechain.
    const auto mainAmplitude = juce::Decibels::decibelsToGain (-50.0f);
    float peak = 0.0f;

    for (int i = 1024; i < mainBuffer.getNumSamples(); ++i)
        peak = std::max (peak, std::abs (mainBuffer.getReadPointer (0)[i]));

    CHECK (peak == Catch::Approx (mainAmplitude).margin (mainAmplitude * 0.2f));
}

TEST_CASE ("External sidechain: a quiet main input alone (no sidechain) stays closed", "[dsp][sidechain]")
{
    GateEngine engine;
    engine.setThresholdDb (-20.0f);
    engine.setRangeDb (-60.0f);
    engine.setLookaheadMs (0.0f);
    engine.setScHighpassHz (20.0f);
    engine.prepare (makeTestSpec (2, 2048));

    juce::AudioBuffer<float> mainBuffer (2, 2048);
    TestHelpers::fillWithSine (mainBuffer, testSampleRate, testFrequencyHz, juce::Decibels::decibelsToGain (-50.0f));

    juce::dsp::AudioBlock<float> mainBlock (mainBuffer);
    engine.process (mainBlock, nullptr); // explicit "no sidechain" - falls back to self-detection

    CHECK_FALSE (engine.isGateOpen());
}

TEST_CASE ("External sidechain: an empty (zero-channel) sidechain block falls back safely to self-detection", "[dsp][sidechain]")
{
    GateEngine engine;
    engine.setThresholdDb (-20.0f);
    engine.setRangeDb (-60.0f);
    engine.setLookaheadMs (0.0f);
    engine.prepare (makeTestSpec (2, 512));

    juce::AudioBuffer<float> mainBuffer (2, 512);
    TestHelpers::fillWithSine (mainBuffer, testSampleRate, testFrequencyHz, juce::Decibels::decibelsToGain (0.0f)); // loud

    juce::AudioBuffer<float> emptySidechain (0, 512);
    juce::dsp::AudioBlock<float> mainBlock (mainBuffer);
    juce::dsp::AudioBlock<float> emptyBlock (emptySidechain);

    CHECK_NOTHROW (engine.process (mainBlock, &emptyBlock));
    CHECK (TestHelpers::allSamplesFinite (mainBuffer));
    CHECK (engine.isGateOpen()); // fell back to the (loud) main input
}

TEST_CASE ("External sidechain: a mono sidechain is splatted across a stereo detection path", "[dsp][sidechain]")
{
    GateEngine engine;
    engine.setThresholdDb (-20.0f);
    engine.setRangeDb (-60.0f);
    engine.setLookaheadMs (0.0f);
    engine.setScHighpassHz (20.0f);
    engine.prepare (makeTestSpec (2, 2048));

    juce::AudioBuffer<float> mainBuffer (2, 2048);
    mainBuffer.clear(); // silent main input

    juce::AudioBuffer<float> monoSidechain (1, 2048);
    TestHelpers::fillWithSine (monoSidechain, testSampleRate, testFrequencyHz, juce::Decibels::decibelsToGain (0.0f));

    juce::dsp::AudioBlock<float> mainBlock (mainBuffer);
    juce::dsp::AudioBlock<float> sidechainBlock (monoSidechain);

    CHECK_NOTHROW (engine.process (mainBlock, &sidechainBlock));
    CHECK (engine.isGateOpen());
    CHECK (TestHelpers::allSamplesFinite (mainBuffer));
}

TEST_CASE ("External sidechain bus: processor falls back to self-detection when the sidechain bus is disabled (the default)", "[processor][sidechain]")
{
    SilentiumAudioProcessor processor;

    // The sidechain bus (input bus 1) must start disabled so existing
    // sessions/hosts see no behaviour change.
    const auto* sidechainBus = processor.getBus (true, 1);
    REQUIRE (sidechainBus != nullptr);
    CHECK_FALSE (sidechainBus->isEnabled());

    processor.prepareToPlay (testSampleRate, 512);

    auto* thresholdParam = processor.apvts.getParameter (ParamIDs::threshold);
    REQUIRE (thresholdParam != nullptr);
    thresholdParam->setValueNotifyingHost (thresholdParam->convertTo0to1 (-20.0f));

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, testSampleRate, testFrequencyHz, juce::Decibels::decibelsToGain (0.0f));
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("External sidechain bus: an enabled sidechain drives the gate independently of a quiet main input", "[processor][sidechain]")
{
    SilentiumAudioProcessor processor;

    auto layout = processor.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    REQUIRE (processor.setBusesLayout (layout));
    REQUIRE (processor.getBus (true, 1)->isEnabled());

    processor.prepareToPlay (testSampleRate, 2048);

    auto* thresholdParam = processor.apvts.getParameter (ParamIDs::threshold);
    auto* holdParam = processor.apvts.getParameter (ParamIDs::hold);
    REQUIRE (thresholdParam != nullptr);
    REQUIRE (holdParam != nullptr);
    thresholdParam->setValueNotifyingHost (thresholdParam->convertTo0to1 (-20.0f));
    holdParam->setValueNotifyingHost (holdParam->convertTo0to1 (0.0f));

    // 4 channels: main (0,1) quiet, sidechain (2,3) loud.
    juce::AudioBuffer<float> buffer (4, 2048);
    buffer.clear();

    const auto mainAmplitudeSource = juce::Decibels::decibelsToGain (-50.0f);
    const auto sidechainAmplitudeSource = juce::Decibels::decibelsToGain (0.0f);

    for (int channel = 0; channel < 4; ++channel)
    {
        auto* data = buffer.getWritePointer (channel);
        const auto amplitude = channel < 2 ? mainAmplitudeSource : sidechainAmplitudeSource;

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto phase = juce::MathConstants<double>::twoPi * testFrequencyHz * static_cast<double> (sample) / testSampleRate;
            data[sample] = amplitude * static_cast<float> (std::sin (phase));
        }
    }

    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));

    // The (quiet) main output should be close to unpassed-through-Range,
    // i.e. clearly louder than it would be if the gate had stayed closed.
    const auto rangeGain = juce::Decibels::decibelsToGain (-60.0f); // default Range
    const auto mainAmplitude = juce::Decibels::decibelsToGain (-50.0f);
    float mainPeak = 0.0f;

    for (int i = 1024; i < buffer.getNumSamples(); ++i)
        mainPeak = std::max (mainPeak, std::abs (buffer.getReadPointer (0)[i]));

    CHECK (mainPeak > mainAmplitude * rangeGain * 2.0f); // clearly above what a closed gate would leave
}

//==============================================================================
// v0.4.0 measurements.
//
// Shared instrumentation first: the engine's applied gain is what every claim
// below is really about, so these helpers read it directly rather than trying
// to infer it from a sine's envelope.
//==============================================================================

namespace
{
    // Runs the engine one sample at a time and records the gain it applied to
    // each of them. Slower than block processing, obviously, but it is the
    // only way to see the *shape* of an opening or closing trajectory rather
    // than one arbitrarily-phased sample of it per block - and the shape is
    // exactly what Smooth Open and Release Shape change.
    std::vector<float> captureGainTrajectory (GateEngine& engine, const std::vector<float>& monoInput)
    {
        std::vector<float> trajectory;
        trajectory.reserve (monoInput.size());

        juce::AudioBuffer<float> buffer (1, 1);

        for (const auto value : monoInput)
        {
            buffer.setSample (0, 0, value);
            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block);
            trajectory.push_back (engine.getCurrentGainDb());
        }

        return trajectory;
    }

    // Settles the engine on a steady tone and returns the midpoint of the
    // gain range it applied across the final block. The midpoint rather than
    // the block-boundary value because a peak follower on a periodic signal
    // ripples slightly, and the boundary sample's phase within that ripple is
    // an accident of the block size.
    float measureSteadyGainDb (GateEngine& engine, float amplitude, double frequencyHz, double seconds)
    {
        constexpr int blockSize = 480;
        const auto numBlocks = static_cast<int> (seconds * testSampleRate / blockSize);

        juce::AudioBuffer<float> buffer (1, blockSize);
        juce::int64 sampleIndex = 0;

        for (int b = 0; b < numBlocks; ++b)
        {
            TestHelpers::fillWithSine (buffer, testSampleRate, frequencyHz, amplitude, sampleIndex);
            sampleIndex += blockSize;

            juce::dsp::AudioBlock<float> block (buffer);
            engine.process (block);
        }

        return 0.5f * (engine.getBlockMinGainDb() + engine.getBlockMaxGainDb());
    }

    // A mono engine configured for static gain-curve measurement: no hold (so
    // nothing pins the target open), fast ballistics (so it settles inside the
    // measurement window), and a wide-open sidechain band.
    void prepareCurveEngine (GateEngine& engine, float thresholdDb, float rangeDb, float ratio, float kneeDb)
    {
        engine.setThresholdDb (thresholdDb);
        engine.setRangeDb (rangeDb);
        engine.setRatio (ratio);
        engine.setKneeDb (kneeDb);
        engine.setHoldMs (0.0f);
        engine.setAttackMs (1.0f);
        engine.setReleaseMs (5.0f);
        engine.setLookaheadMs (0.0f);
        engine.setScHighpassHz (20.0f);
        engine.setScLowpassHz (16000.0f);
        engine.setHysteresisDb (0.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = 480;
        spec.numChannels = 1;
        engine.prepare (spec);
    }

    float dbToAmplitude (float db) { return juce::Decibels::decibelsToGain (db); }
}

//==============================================================================
// T3 - the expander law itself.
//
// Measured as DIFFERENCES between levels rather than as absolute gains: a peak
// follower sits a fraction of a dB below the true peak of a periodic signal,
// and that offset is identical at every level, so it cancels in a difference
// and does not have to be modelled or fudged. What remains is exactly the
// quantity under test - the slope of the gain law.
//==============================================================================

TEST_CASE ("Ratio: the expander law has the slope it claims", "[dsp][ratio][expander]")
{
    constexpr float thresholdDb = -30.0f;

    // Deep enough that even 8:1 at the lower measurement level (which asks
    // for 70 dB of attenuation) is nowhere near the floor - this test is
    // about the slope of the law, not about where it stops.
    constexpr float rangeDb = -80.0f;
    constexpr double measurementFrequencyHz = 4000.0;

    constexpr float upperLevelDb = thresholdDb - 4.0f;
    constexpr float lowerLevelDb = thresholdDb - 10.0f;
    constexpr float levelStepDb = lowerLevelDb - upperLevelDb; // -6 dB

    for (const auto ratio : { 2.0f, 4.0f, 8.0f })
    {
        GateEngine upper;
        GateEngine lower;
        prepareCurveEngine (upper, thresholdDb, rangeDb, ratio, 0.0f);
        prepareCurveEngine (lower, thresholdDb, rangeDb, ratio, 0.0f);

        const auto gainAtUpper = measureSteadyGainDb (upper, dbToAmplitude (upperLevelDb), measurementFrequencyHz, 0.5);
        const auto gainAtLower = measureSteadyGainDb (lower, dbToAmplitude (lowerLevelDb), measurementFrequencyHz, 0.5);

        const auto measuredSlope = (gainAtLower - gainAtUpper) / levelStepDb;

        INFO ("ratio " << ratio << ": gain " << gainAtUpper << " dB at " << upperLevelDb
                << " dBFS, " << gainAtLower << " dB at " << lowerLevelDb << " dBFS");

        // A ratio of R:1 attenuates by (R-1) dB for every dB below Threshold.
        CHECK (measuredSlope == Catch::Approx (ratio - 1.0f).margin (0.25));
    }
}

TEST_CASE ("Ratio: above Threshold the expander passes signal untouched", "[dsp][ratio][expander]")
{
    constexpr float thresholdDb = -30.0f;

    for (const auto ratio : { 1.5f, 4.0f, 12.0f })
    {
        GateEngine engine;
        prepareCurveEngine (engine, thresholdDb, -60.0f, ratio, 0.0f);

        const auto gainDb = measureSteadyGainDb (engine, dbToAmplitude (thresholdDb + 12.0f), 4000.0, 0.5);

        INFO ("ratio " << ratio);
        CHECK (gainDb == Catch::Approx (0.0f).margin (0.25));
    }
}

TEST_CASE ("Ratio: Range floors the expander, and nothing goes below it", "[dsp][ratio][expander]")
{
    constexpr float thresholdDb = -20.0f;
    constexpr float rangeDb = -30.0f;

    // 8:1 at 20 dB below Threshold would ask for 140 dB of attenuation; Range
    // is the floor, so the answer must be exactly Range and not a hair below.
    GateEngine engine;
    prepareCurveEngine (engine, thresholdDb, rangeDb, 8.0f, 0.0f);

    const auto gainDb = measureSteadyGainDb (engine, dbToAmplitude (thresholdDb - 20.0f), 4000.0, 0.5);

    INFO ("measured " << gainDb << " dB against a floor of " << rangeDb << " dB");
    CHECK (gainDb <= rangeDb + 0.25f);
    CHECK (gainDb >= rangeDb - 0.5f);
}

// A peak follower settles a fraction of a dB below the true peak of a
// periodic signal. That offset is a property of the detector, not of the gain
// law, and it is identical at every level - so rather than widening
// tolerances to hide it, this measures it once and the knee test evaluates
// its analytic expectation at the level the detector actually reports.
namespace
{
    float measureDetectorBiasDb()
    {
        constexpr float thresholdDb = -30.0f;
        constexpr float ratio = 4.0f;
        constexpr float offsetBelowThresholdDb = -8.0f;

        GateEngine engine;
        prepareCurveEngine (engine, thresholdDb, -80.0f, ratio, 0.0f);

        const auto gainDb = measureSteadyGainDb (engine,
                                                  dbToAmplitude (thresholdDb + offsetBelowThresholdDb),
                                                  4000.0,
                                                  0.5);

        // In the linear region G = (R - 1) * (L_actual - T), so the level the
        // detector actually reported follows directly from the gain applied.
        const auto reportedOffsetDb = gainDb / (ratio - 1.0f);
        return reportedOffsetDb - offsetBelowThresholdDb;
    }

    // The gain law from the implementation brief, in closed form, for use as
    // an independent expectation.
    float expectedExpanderGainDb (float levelDb, float thresholdDb, float ratio, float kneeDb, float rangeDb)
    {
        const auto overshoot = levelDb - thresholdDb;
        float gainDb;

        if (kneeDb > 0.0f && std::abs (2.0f * overshoot) <= kneeDb)
        {
            const auto distanceIntoKnee = thresholdDb + kneeDb * 0.5f - levelDb;
            gainDb = -(ratio - 1.0f) * distanceIntoKnee * distanceIntoKnee / (2.0f * kneeDb);
        }
        else if (overshoot >= 0.0f)
        {
            gainDb = 0.0f;
        }
        else
        {
            gainDb = (ratio - 1.0f) * overshoot;
        }

        return std::max (gainDb, rangeDb);
    }
}

TEST_CASE ("Ratio: a soft knee joins the two branches without a corner", "[dsp][ratio][expander][knee]")
{
    constexpr float thresholdDb = -30.0f;
    constexpr float rangeDb = -80.0f;
    constexpr float kneeDb = 12.0f;
    constexpr float ratio = 4.0f;

    const auto detectorBiasDb = measureDetectorBiasDb();
    INFO ("calibrated detector bias: " << detectorBiasDb << " dB");
    REQUIRE (std::abs (detectorBiasDb) < 2.0f); // sanity: a calibration, not a fudge factor

    std::vector<float> nominalLevels;
    std::vector<float> measuredGains;
    std::vector<float> expectedGains;

    for (auto offset = kneeDb * 0.5f; offset >= -kneeDb * 0.5f - 0.01f; offset -= 1.0f)
    {
        GateEngine engine;
        prepareCurveEngine (engine, thresholdDb, rangeDb, ratio, kneeDb);

        const auto nominalLevelDb = thresholdDb + offset;
        nominalLevels.push_back (nominalLevelDb);
        measuredGains.push_back (measureSteadyGainDb (engine, dbToAmplitude (nominalLevelDb), 4000.0, 0.5));
        expectedGains.push_back (expectedExpanderGainDb (nominalLevelDb + detectorBiasDb,
                                                          thresholdDb, ratio, kneeDb, rangeDb));
    }

    REQUIRE (measuredGains.size() >= 12);

    SECTION ("the measured curve matches the specified law across the whole knee band")
    {
        for (size_t i = 0; i < measuredGains.size(); ++i)
        {
            INFO ("level " << nominalLevels[i] << " dBFS (detector sees "
                    << nominalLevels[i] + detectorBiasDb << " dBFS)");
            CHECK (measuredGains[i] == Catch::Approx (expectedGains[i]).margin (0.35));
        }
    }

    SECTION ("attenuation increases monotonically through the knee, with no step")
    {
        auto largestStep = 0.0f;

        for (size_t i = 1; i < measuredGains.size(); ++i)
        {
            CHECK (measuredGains[i] <= measuredGains[i - 1] + 0.3f);
            largestStep = std::max (largestStep, std::abs (measuredGains[i] - measuredGains[i - 1]));
        }

        // Across a 1 dB level step the steepest part of the knee - its lower
        // edge, where it meets the (R-1) line - reaches (R-1) dB. A corner
        // would exceed it.
        INFO ("largest gain step across a 1 dB level step: " << largestStep);
        CHECK (largestStep <= (ratio - 1.0f) + 0.5f);
    }
}

//==============================================================================
// T5/T6 - Smooth Open.
//
// The claim being tested is specific: with a 0 ms attack the gate reaches full
// opening exactly as the delayed transient leaves the lookahead delay line,
// and it gets there along a continuous ramp rather than a step. Both halves
// matter - a step that arrives on time is still a click, and a smooth ramp
// that arrives late has eaten the transient.
//==============================================================================

namespace
{
    struct SmoothOpenFixture
    {
        static constexpr float thresholdDb = -40.0f;
        static constexpr float rangeDb = -60.0f;
        static constexpr float lookaheadMs = 5.0f;
        static constexpr int lookaheadSamples = 240; // 5 ms at 48 kHz
        static constexpr int silenceSamples = 4800;
        static constexpr int burstSamples = 4800;

        // Silence (a -80 dBFS floor, i.e. genuinely below Threshold) followed
        // by an abrupt -6 dBFS burst.
        static std::vector<float> makeStimulus()
        {
            std::vector<float> input (static_cast<size_t> (silenceSamples + burstSamples), 0.0f);

            for (size_t i = 0; i < input.size(); ++i)
            {
                const auto phase = juce::MathConstants<double>::twoPi * 1000.0
                                    * static_cast<double> (i) / testSampleRate;
                const auto amplitude = i < static_cast<size_t> (silenceSamples)
                                            ? juce::Decibels::decibelsToGain (-80.0f)
                                            : juce::Decibels::decibelsToGain (-6.0f);
                input[i] = amplitude * static_cast<float> (std::sin (phase));
            }

            return input;
        }

        static std::vector<float> trajectoryWith (bool smoothOpen)
        {
            GateEngine engine;
            engine.setThresholdDb (thresholdDb);
            engine.setRangeDb (rangeDb);
            engine.setAttackMs (0.0f); // the marquee case: instant attack
            engine.setHoldMs (20.0f);
            engine.setReleaseMs (80.0f);
            engine.setLookaheadMs (lookaheadMs);
            engine.setScHighpassHz (20.0f);
            engine.setScLowpassHz (16000.0f);
            engine.setSmoothOpen (smoothOpen);

            juce::dsp::ProcessSpec spec;
            spec.sampleRate = testSampleRate;
            spec.maximumBlockSize = 1;
            spec.numChannels = 1;
            engine.prepare (spec);

            return captureGainTrajectory (engine, makeStimulus());
        }
    };
}

TEST_CASE ("Smooth Open: the gate is fully open exactly when the delayed transient arrives", "[dsp][smoothopen]")
{
    const auto trajectory = SmoothOpenFixture::trajectoryWith (true);

    // The first sample of the burst does not reach the output until it has
    // travelled the whole lookahead delay - that is the sample the opening
    // ramp is timed to.
    const auto arrivalIndex = static_cast<size_t> (SmoothOpenFixture::silenceSamples
                                                    + SmoothOpenFixture::lookaheadSamples);
    REQUIRE (trajectory.size() > arrivalIndex);

    INFO ("gain at transient arrival: " << trajectory[arrivalIndex] << " dB");
    CHECK (trajectory[arrivalIndex] >= -1.0f);

    // ...and not before: opening early would defeat the point of having a
    // lookahead window at all (the gate would be wide open while the last of
    // the pre-transient noise is still coming out of the delay line).
    const auto halfwayIndex = static_cast<size_t> (SmoothOpenFixture::silenceSamples
                                                    + SmoothOpenFixture::lookaheadSamples / 2);
    INFO ("gain halfway through the window: " << trajectory[halfwayIndex] << " dB");
    CHECK (trajectory[halfwayIndex] < -1.0f);
}

TEST_CASE ("Smooth Open: the opening ramp is continuous, where the legacy path steps", "[dsp][smoothopen][click]")
{
    const auto smoothed = SmoothOpenFixture::trajectoryWith (true);
    const auto legacy = SmoothOpenFixture::trajectoryWith (false);

    const auto openingStart = static_cast<size_t> (SmoothOpenFixture::silenceSamples - 8);
    const auto openingEnd = static_cast<size_t> (SmoothOpenFixture::silenceSamples
                                                  + SmoothOpenFixture::lookaheadSamples + 8);

    REQUIRE (smoothed.size() > openingEnd);
    REQUIRE (legacy.size() > openingEnd);

    auto largestSmoothedStep = 0.0f;
    auto largestLegacyStep = 0.0f;
    auto smallestSmoothedStep = 0.0f;

    for (auto i = openingStart + 1; i <= openingEnd; ++i)
    {
        const auto smoothedStep = smoothed[i] - smoothed[i - 1];
        largestSmoothedStep = std::max (largestSmoothedStep, std::abs (smoothedStep));
        smallestSmoothedStep = std::min (smallestSmoothedStep, smoothedStep);
        largestLegacyStep = std::max (largestLegacyStep, std::abs (legacy[i] - legacy[i - 1]));
    }

    // The cascaded-box kernel is triangular, so a step of M dB spread over N
    // samples peaks at 2M/N dB per sample. 25 % of headroom on top of that
    // covers the ballistics' own contribution at the ramp's start.
    constexpr auto rangeSpanDb = -SmoothOpenFixture::rangeDb;
    constexpr auto theoreticalPeakSlope = 2.0f * rangeSpanDb / SmoothOpenFixture::lookaheadSamples;
    constexpr auto allowedSlope = 1.25f * theoreticalPeakSlope;

    INFO ("largest smoothed step " << largestSmoothedStep << " dB/sample, allowed " << allowedSlope);
    CHECK (largestSmoothedStep <= allowedSlope);

    // Monotone opening: the ramp never backs up on itself.
    INFO ("most negative smoothed step " << smallestSmoothedStep << " dB/sample");
    CHECK (smallestSmoothedStep >= -0.01f);

    // And the contrast that makes the number mean something: without Smooth
    // Open the same stimulus produces a single-sample jump of nearly the whole
    // Range.
    INFO ("largest legacy step " << largestLegacyStep << " dB/sample");
    CHECK (largestLegacyStep > 40.0f);
    CHECK (largestSmoothedStep < largestLegacyStep / 40.0f);
}

//==============================================================================
// T8 - dB-linear release.
//==============================================================================

namespace
{
    struct LineFit
    {
        double slopePerSample = 0.0;
        double rSquared = 0.0;
    };

    // Ordinary least squares of value against sample index, plus the
    // coefficient of determination - which is what actually distinguishes a
    // straight line from an exponential decay that happens to span the same
    // two endpoints.
    LineFit fitLine (const std::vector<float>& values, size_t first, size_t last)
    {
        const auto n = static_cast<double> (last - first + 1);
        auto sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumXX = 0.0;

        for (auto i = first; i <= last; ++i)
        {
            const auto x = static_cast<double> (i - first);
            const auto y = static_cast<double> (values[i]);
            sumX += x; sumY += y; sumXY += x * y; sumXX += x * x;
        }

        LineFit fit;
        const auto denominator = n * sumXX - sumX * sumX;
        fit.slopePerSample = (n * sumXY - sumX * sumY) / denominator;

        const auto intercept = (sumY - fit.slopePerSample * sumX) / n;
        const auto mean = sumY / n;
        auto residual = 0.0, total = 0.0;

        for (auto i = first; i <= last; ++i)
        {
            const auto x = static_cast<double> (i - first);
            const auto y = static_cast<double> (values[i]);
            const auto predicted = fit.slopePerSample * x + intercept;
            residual += (y - predicted) * (y - predicted);
            total += (y - mean) * (y - mean);
        }

        fit.rSquared = total > 0.0 ? 1.0 - residual / total : 1.0;
        return fit;
    }

    std::vector<float> releaseTrajectory (bool linearShape, float releaseMs, float rangeDb)
    {
        GateEngine engine;
        engine.setThresholdDb (-40.0f);
        engine.setRangeDb (rangeDb);
        engine.setAttackMs (1.0f);
        engine.setHoldMs (0.0f);
        engine.setReleaseMs (releaseMs);
        engine.setLookaheadMs (0.0f);
        engine.setScHighpassHz (20.0f);
        engine.setScLowpassHz (16000.0f);
        engine.setHysteresisDb (3.0f);
        engine.setReleaseShapeLinear (linearShape);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = testSampleRate;
        spec.maximumBlockSize = 1;
        spec.numChannels = 1;
        engine.prepare (spec);

        // A loud tone long enough to open fully, then true silence.
        std::vector<float> input (static_cast<size_t> (testSampleRate * 0.5), 0.0f);
        const auto toneSamples = input.size() / 2;

        for (size_t i = 0; i < toneSamples; ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * 1000.0
                                * static_cast<double> (i) / testSampleRate;
            input[i] = 0.5f * static_cast<float> (std::sin (phase));
        }

        return captureGainTrajectory (engine, input);
    }

    // The stretch of the trajectory that is unambiguously "closing": between
    // the two given gains, walking downwards.
    std::pair<size_t, size_t> findClosingSegment (const std::vector<float>& trajectory,
                                                   float fromDb,
                                                   float toDb)
    {
        size_t first = 0, last = 0;

        for (size_t i = 1; i < trajectory.size(); ++i)
        {
            if (first == 0 && trajectory[i] <= fromDb && trajectory[i] < trajectory[i - 1])
                first = i;

            if (first != 0 && trajectory[i] <= toDb)
            {
                last = i;
                break;
            }
        }

        return { first, last };
    }
}

TEST_CASE ("Release Shape: Linear closes at a constant dB/s, taking exactly the Release time", "[dsp][releaseshape]")
{
    constexpr float rangeDb = -60.0f;
    constexpr float releaseMs = 100.0f;

    const auto trajectory = releaseTrajectory (true, releaseMs, rangeDb);
    const auto [first, last] = findClosingSegment (trajectory, -5.0f, -55.0f);

    REQUIRE (first > 0);
    REQUIRE (last > first);

    const auto fit = fitLine (trajectory, first, last);
    const auto measuredDbPerSecond = fit.slopePerSample * testSampleRate;
    const auto expectedDbPerSecond = -static_cast<double> (-rangeDb) / (releaseMs * 0.001);

    INFO ("measured " << measuredDbPerSecond << " dB/s, expected " << expectedDbPerSecond
            << ", R^2 = " << fit.rSquared);

    CHECK (measuredDbPerSecond == Catch::Approx (expectedDbPerSecond).epsilon (0.10));

    // A straight line is what separates this from the exponential shape; an
    // exponential spanning the same endpoints fits a line far worse.
    CHECK (fit.rSquared > 0.99);
}

TEST_CASE ("Release Shape: Exponential is still a curve, not a line", "[dsp][releaseshape]")
{
    // The contrast that gives the previous test's R^2 threshold meaning.
    const auto trajectory = releaseTrajectory (false, 100.0f, -60.0f);
    const auto [first, last] = findClosingSegment (trajectory, -5.0f, -55.0f);

    REQUIRE (first > 0);
    REQUIRE (last > first);

    const auto fit = fitLine (trajectory, first, last);
    INFO ("exponential shape line-fit R^2 = " << fit.rSquared);
    CHECK (fit.rSquared < 0.99);
}

//==============================================================================
// T12 - sidechain filter slope.
//==============================================================================

TEST_CASE ("SC Slope: 24 dB/oct really is twice the rejection of 12 dB/oct", "[dsp][scslope]")
{
    constexpr double cutoffHz = 200.0;
    constexpr double probeHz = 100.0; // exactly one octave below the cutoff

    // Listen mode outputs the detection signal itself, which is the only way
    // to measure the sidechain filters' response directly - they never touch
    // the main path.
    auto measureRejectionDb = [] (bool steepSlope)
    {
        auto measure = [steepSlope] (double highpassHz)
        {
            GateEngine engine;
            engine.setListenMode (true);
            engine.setScHighpassHz (static_cast<float> (highpassHz));
            engine.setScLowpassHz (16000.0f);
            engine.setScSlope24 (steepSlope);
            engine.setThresholdDb (-80.0f);
            engine.setRangeDb (0.0f);
            engine.setLookaheadMs (0.0f);

            juce::dsp::ProcessSpec spec;
            spec.sampleRate = testSampleRate;
            spec.maximumBlockSize = 4096;
            spec.numChannels = 1;
            engine.prepare (spec);

            juce::AudioBuffer<float> buffer (1, 4096);
            juce::int64 sampleIndex = 0;
            float peak = 0.0f;

            // Several blocks so the filters and the slope crossfade settle;
            // measured on the last one.
            for (int b = 0; b < 12; ++b)
            {
                TestHelpers::fillWithSine (buffer, testSampleRate, probeHz, 0.5f, sampleIndex);
                sampleIndex += buffer.getNumSamples();

                juce::dsp::AudioBlock<float> block (buffer);
                engine.process (block);

                if (b == 11)
                    peak = TestHelpers::peakAbsolute (buffer);
            }

            return peak;
        };

        // Referenced against the same probe tone with the filter moved far
        // below it, so the measurement is a pure rejection figure rather than
        // an absolute level that would also carry the detection path's own
        // gain.
        const auto passband = measure (20.0);
        const auto rejected = measure (cutoffHz);

        return juce::Decibels::gainToDecibels (rejected / passband);
    };

    const auto rejection12 = measureRejectionDb (false);
    const auto rejection24 = measureRejectionDb (true);

    INFO ("12 dB/oct: " << rejection12 << " dB, 24 dB/oct: " << rejection24 << " dB, one octave below cutoff");

    // A 2nd-order Butterworth high-pass sits 12.3 dB down one octave below its
    // cutoff; a 4th-order one, 24.1 dB.
    CHECK (rejection12 == Catch::Approx (-12.3).margin (1.5));
    CHECK (rejection24 == Catch::Approx (-24.1).margin (1.5));
}

//==============================================================================
// T13 - automation safety.
//
// Every new control is automatable, so every new control has to be moveable
// under signal without producing a step in the applied gain.
//==============================================================================

TEST_CASE ("Automating the v0.4.0 controls under signal never steps the gain", "[dsp][automation][click]")
{
    GateEngine engine;
    engine.setThresholdDb (-30.0f);
    engine.setRangeDb (-40.0f);
    engine.setAttackMs (5.0f);
    engine.setHoldMs (20.0f);
    engine.setReleaseMs (80.0f);
    engine.setLookaheadMs (5.0f);
    engine.setScHighpassHz (80.0f);
    engine.setScLowpassHz (16000.0f);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = testSampleRate;
    spec.maximumBlockSize = 1;
    spec.numChannels = 1;
    engine.prepare (spec);

    const auto totalSamples = static_cast<int> (testSampleRate); // 1 s
    juce::AudioBuffer<float> buffer (1, 1);

    auto largestStep = 0.0f;
    auto previousGainDb = engine.getCurrentGainDb();

    for (int i = 0; i < totalSamples; ++i)
    {
        const auto position = static_cast<float> (i) / static_cast<float> (totalSamples);

        // Full-range sweeps of both continuous additions, plus a toggle of
        // each of the three switched ones at points spread across the run.
        engine.setRatio (juce::jmap (position, 1.0f, ParamConstants::maxRatio));
        engine.setHysteresisDb (juce::jmap (position, 0.0f, 12.0f));
        engine.setDetectorMode (position > 0.25f);
        engine.setScSlope24 (position > 0.5f);
        engine.setSmoothOpen (position > 0.75f);

        const auto phase = juce::MathConstants<double>::twoPi * 1000.0 * static_cast<double> (i) / testSampleRate;
        buffer.setSample (0, 0, juce::Decibels::decibelsToGain (-6.0f) * static_cast<float> (std::sin (phase)));

        juce::dsp::AudioBlock<float> block (buffer);
        engine.process (block);

        const auto gainDb = engine.getCurrentGainDb();

        // Skip the very first sample: the engine starts parked at Range and
        // its first approach towards the open target is the attack ramp
        // doing its job, not an artefact.
        if (i > 1)
            largestStep = std::max (largestStep, std::abs (gainDb - previousGainDb));

        previousGainDb = gainDb;
    }

    INFO ("largest single-sample gain change under full automation: " << largestStep << " dB");
    CHECK (largestStep < 6.0f);
}
