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
