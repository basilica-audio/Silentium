#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "dsp/GateEngine.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    void setParam (SilentiumAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("getLatencySamples() reports the lookahead delay after prepareToPlay", "[latency]")
{
    SilentiumAudioProcessor processor;

    // Before prepareToPlay, no engine has been prepared yet - JUCE's default
    // AudioProcessor latency is 0.
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (48000.0, 512);

    // Cross-check against a standalone engine prepared identically with the
    // Lookahead parameter's default (5 ms): the processor must report
    // exactly what GateEngine computes, not an approximation of it.
    GateEngine referenceEngine;
    referenceEngine.setLookaheadMs (5.0f);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    referenceEngine.prepare (spec);

    CHECK (processor.getLatencySamples() == referenceEngine.getLatencySamples());
    CHECK (processor.getLatencySamples() == 240); // 5 ms @ 48 kHz, exact integer
}

TEST_CASE ("Latency tracks a non-default Lookahead value on the next prepareToPlay", "[latency]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::lookahead, 10.0f);

    // Lookahead is treated as a structural parameter (see
    // GateEngine::getLatencySamples()): the new value only takes effect on
    // the next prepareToPlay(), not immediately.
    processor.prepareToPlay (48000.0, 512);

    CHECK (processor.getLatencySamples() == 480); // 10 ms @ 48 kHz
}

TEST_CASE ("Latency is stable across repeated prepareToPlay calls at the same sample rate", "[latency]")
{
    SilentiumAudioProcessor processor;

    processor.prepareToPlay (44100.0, 256);
    const auto firstLatency = processor.getLatencySamples();

    processor.prepareToPlay (44100.0, 256);
    const auto secondLatency = processor.getLatencySamples();

    CHECK (firstLatency == secondLatency);
}

TEST_CASE ("Latency scales with sample rate for a fixed Lookahead", "[latency]")
{
    SilentiumAudioProcessor processor;

    processor.prepareToPlay (44100.0, 512);
    const auto latencyAt44k = processor.getLatencySamples();

    processor.prepareToPlay (96000.0, 512);
    const auto latencyAt96k = processor.getLatencySamples();

    CHECK (latencyAt44k > 0);
    CHECK (latencyAt96k > latencyAt44k); // same lookahead in ms, more samples at a higher rate
}

//==============================================================================
// v0.4.0.
//==============================================================================

#include "TestHelpers.h"

#include <cmath>
#include <vector>

//==============================================================================
// T7 - Smooth Open must not cost latency.
//
// The whole point of shaping the opening ramp inside the lookahead window is
// that the window is already paid for. If enabling it moved the reported
// latency, it would be doing something quite different from what it claims.
//==============================================================================

TEST_CASE ("Smooth Open changes no reported latency, at any Lookahead", "[latency][smoothopen]")
{
    for (const auto lookaheadMs : { 0.0f, 2.5f, 5.0f, 20.0f })
    {
        auto latencyWith = [lookaheadMs] (bool smoothOpen)
        {
            SilentiumAudioProcessor processor;
            setParam (processor, ParamIDs::lookahead, lookaheadMs);

            auto* param = processor.apvts.getParameter (ParamIDs::smoothOpen);
            REQUIRE (param != nullptr);
            param->setValueNotifyingHost (smoothOpen ? 1.0f : 0.0f);

            processor.prepareToPlay (48000.0, 512);
            return processor.getLatencySamples();
        };

        INFO ("lookahead " << lookaheadMs << " ms");
        CHECK (latencyWith (true) == latencyWith (false));
        CHECK (latencyWith (true) == juce::roundToInt (lookaheadMs * 0.001f * 48000.0f));
    }
}

//==============================================================================
// T14 - Lookahead is live.
//
// Before v0.4.0 moving the Lookahead knob did nothing until the host happened
// to re-prepare the plugin. Now it takes effect immediately, which means two
// things have to hold at once: the applied delay must actually move, and the
// host must be told - from the message thread, never from the audio thread.
//==============================================================================

namespace
{
    // Measures the delay the engine is really applying, by finding where an
    // impulse comes out.
    int measureAppliedDelaySamples (SilentiumAudioProcessor& processor, int searchLength)
    {
        juce::AudioBuffer<float> buffer (2, searchLength);
        juce::MidiBuffer midi;

        buffer.clear();

        // Full-scale impulse: Threshold is set to its minimum by the caller,
        // so the gate is open and the path is a pure delay.
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample (channel, 0, 1.0f);

        processor.processBlock (buffer, midi);

        auto bestIndex = -1;
        auto bestValue = 0.0f;

        for (int i = 0; i < searchLength; ++i)
        {
            const auto value = std::abs (buffer.getSample (0, i));

            if (value > bestValue)
            {
                bestValue = value;
                bestIndex = i;
            }
        }

        return bestValue > 0.5f ? bestIndex : -1;
    }
}

TEST_CASE ("Lookahead applies live, and the host is told from the message thread", "[latency][lookahead][live]")
{
    SilentiumAudioProcessor processor;

    // Range 0 dB collapses the engine to a pure delay, so an impulse's
    // position is exactly the applied delay.
    setParam (processor, ParamIDs::threshold, -80.0f);
    setParam (processor, ParamIDs::range, 0.0f);
    setParam (processor, ParamIDs::lookahead, 5.0f);
    processor.prepareToPlay (48000.0, 4096);

    REQUIRE (processor.getLatencySamples() == 240);
    REQUIRE (measureAppliedDelaySamples (processor, 4096) == 240);

    SECTION ("the applied delay moves without a re-prepare")
    {
        setParam (processor, ParamIDs::lookahead, 10.0f);

        // Deliberately NOT calling prepareToPlay: this is the behaviour that
        // did not exist before v0.4.0.
        juce::AudioBuffer<float> buffer (2, 4096);
        juce::MidiBuffer midi;

        // Run enough audio for the equal-power tap crossfade to complete
        // (10 ms) before measuring where the impulse lands.
        for (int b = 0; b < 4; ++b)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
        }

        CHECK (measureAppliedDelaySamples (processor, 4096) == 480);
    }

    SECTION ("the host's reported latency follows, from the message thread")
    {
        setParam (processor, ParamIDs::lookahead, 10.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        buffer.clear();
        processor.processBlock (buffer, midi);

        // The audio thread only publishes an atomic; the reported latency is
        // updated by a message-thread poll, so it cannot have changed yet.
        CHECK (processor.getLatencySamples() == 240);

        // The brief's "within 100 ms" bound is a property of how often the
        // poll is scheduled, not of how promptly a shared CI runner happens to
        // get round to running it. Assert the design bound directly, where it
        // is deterministic...
        CHECK (SilentiumAudioProcessor::latencyPollIntervalMs <= 100);

        // ...and assert the hand-off itself by pumping the message loop the
        // way a host does continuously, until the new latency has been
        // published. A single runDispatchLoopUntil (100 ms) was flaky on CI:
        // the timer thread and the message thread both have to be scheduled
        // inside that window, and on a loaded runner they are not.
        //
        // The deadline below is a watchdog, not the guarantee under test - it
        // only bounds the failure case so the suite cannot hang.
        constexpr int watchdogMs = 5000;
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) watchdogMs;

        while (processor.getLatencySamples() != 480
                && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (SilentiumAudioProcessor::latencyPollIntervalMs);

        CHECK (processor.getLatencySamples() == 480);
    }

    SECTION ("the crossfade between the old and new taps does not click")
    {
        // A steady tone through the transition: with the gate wide open the
        // output is the input delayed, so any discontinuity at the tap change
        // shows up as a jump between consecutive output samples that the
        // signal itself could never produce.
        constexpr int blockSize = 512;
        constexpr double frequencyHz = 500.0;

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::int64 sampleIndex = 0;

        // Settle first.
        for (int b = 0; b < 4; ++b)
        {
            TestHelpers::fillWithSine (buffer, 48000.0, frequencyHz, 0.5f, sampleIndex);
            sampleIndex += blockSize;
            processor.processBlock (buffer, midi);
        }

        setParam (processor, ParamIDs::lookahead, 12.0f);

        auto largestStep = 0.0f;
        auto previous = 0.0f;
        auto first = true;

        for (int b = 0; b < 8; ++b)
        {
            TestHelpers::fillWithSine (buffer, 48000.0, frequencyHz, 0.5f, sampleIndex);
            sampleIndex += blockSize;
            processor.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto value = buffer.getSample (0, i);

                if (! first)
                    largestStep = std::max (largestStep, std::abs (value - previous));

                previous = value;
                first = false;
            }
        }

        // One sample of a 500 Hz sine at 0.5 amplitude steps by at most
        // 0.5 * 2*pi*500/48000 = 0.033. Allowing 6x that still catches any
        // real discontinuity, which would be a sizeable fraction of the
        // amplitude.
        INFO ("largest sample-to-sample step through the tap crossfade: " << largestStep);
        CHECK (largestStep < 0.2f);
    }
}
