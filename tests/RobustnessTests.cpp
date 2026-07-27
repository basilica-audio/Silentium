#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <random>

namespace
{
    void setParam (SilentiumAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Silence produces silence (and no NaN/Inf)", "[robustness]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::threshold, -40.0f);
    setParam (processor, ParamIDs::range, -60.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Full-scale input at extreme gate settings produces no NaN/Inf", "[robustness]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::threshold, -80.0f);
    setParam (processor, ParamIDs::attack, 0.0f);
    setParam (processor, ParamIDs::hold, 250.0f);
    setParam (processor, ParamIDs::release, 5.0f);
    setParam (processor, ParamIDs::range, -80.0f);
    setParam (processor, ParamIDs::lookahead, 20.0f);
    setParam (processor, ParamIDs::scHighpass, 500.0f);
    setParam (processor, ParamIDs::scLowpass, 1000.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) < 100.0f); // sane bound, not just "finite"
}

TEST_CASE ("Denormal-range input produces no NaN/Inf output", "[robustness]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::threshold, -80.0f);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("NaN/Inf input samples do not crash processBlock, and reset() lets clean audio recover", "[robustness][nan]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::threshold, -40.0f);
    setParam (processor, ParamIDs::lookahead, 5.0f);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            if (sample % 3 == 0)
                data[sample] = nan;
            else if (sample % 3 == 1)
                data[sample] = inf;
            else
                data[sample] = 0.1f;
        }
    }

    juce::MidiBuffer midi;

    // Feeding pathological input is not something the gate is expected to
    // sanitise on its own (that is the host's job) - the contract under
    // test is that it must not crash, hang, or throw.
    CHECK_NOTHROW (processor.processBlock (buffer, midi));

    // reset() clears every filter/envelope/delay-line's internal state
    // (including the lookahead delay line's buffered samples), so any
    // NaN/Inf poisoned into the lookahead buffer above must not leak into
    // subsequent clean audio.
    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f);

    // Process enough blocks to fully flush the lookahead delay line's
    // capacity with clean samples.
    for (int i = 0; i < 4; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Zero-sample buffer does not crash processBlock", "[robustness]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (buffer.getNumSamples() == 0);
}

TEST_CASE ("Extreme parameter values at both range edges produce no NaN/Inf", "[robustness]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (bool useMinimum : { true, false })
    {
        setParam (processor, ParamIDs::threshold, useMinimum ? -80.0f : 0.0f);
        setParam (processor, ParamIDs::attack, useMinimum ? 0.0f : 50.0f);
        setParam (processor, ParamIDs::hold, useMinimum ? 0.0f : 250.0f);
        setParam (processor, ParamIDs::release, useMinimum ? 5.0f : 500.0f);
        setParam (processor, ParamIDs::range, useMinimum ? -80.0f : 0.0f);
        setParam (processor, ParamIDs::lookahead, useMinimum ? 0.0f : 20.0f);
        setParam (processor, ParamIDs::scHighpass, useMinimum ? 20.0f : 500.0f);
        setParam (processor, ParamIDs::scLowpass, useMinimum ? 1000.0f : 16000.0f);

        TestHelpers::fillWithSine (buffer, 44100.0, 440.0, 0.8f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Rapid parameter automation across many blocks produces no NaN/Inf", "[robustness]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 100; ++block)
    {
        setParam (processor, ParamIDs::threshold, -80.0f + unit (rng) * 80.0f);
        setParam (processor, ParamIDs::attack, unit (rng) * 50.0f);
        setParam (processor, ParamIDs::hold, unit (rng) * 250.0f);
        setParam (processor, ParamIDs::release, 5.0f + unit (rng) * 495.0f);
        setParam (processor, ParamIDs::range, -80.0f + unit (rng) * 80.0f);
        setParam (processor, ParamIDs::lookahead, unit (rng) * 20.0f);
        setParam (processor, ParamIDs::scHighpass, 20.0f + unit (rng) * 480.0f);
        setParam (processor, ParamIDs::scLowpass, 1000.0f + unit (rng) * 15000.0f);
        setParam (processor, ParamIDs::knee, unit (rng) * 24.0f);
        setParam (processor, ParamIDs::duck, unit (rng) > 0.5f ? 1.0f : 0.0f);
        setParam (processor, ParamIDs::listen, unit (rng) > 0.5f ? 1.0f : 0.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + unit (rng) * 4000.0, 0.7f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Sample-rate sweep 44.1-192 kHz produces no NaN/Inf and correctly scaled latency", "[robustness][samplerate]")
{
    static constexpr double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    for (const auto sr : sampleRates)
    {
        SilentiumAudioProcessor processor;
        processor.prepareToPlay (sr, 512);

        setParam (processor, ParamIDs::threshold, -30.0f);
        setParam (processor, ParamIDs::lookahead, 5.0f);
        processor.prepareToPlay (sr, 512); // re-prepare so the new Lookahead value is applied (structural parameter)

        // 5 ms of lookahead must round to the same number of samples the
        // engine itself would compute, at every rate in the sweep.
        const auto expectedLatency = juce::roundToInt (5.0 * 0.001 * sr);
        CHECK (processor.getLatencySamples() == expectedLatency);

        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, sr, 1000.0, 0.6f);
        juce::MidiBuffer midi;

        for (int i = 0; i < 8; ++i)
            CHECK_NOTHROW (processor.processBlock (buffer, midi));

        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Long-run stability: thousands of blocks with continuously varying parameters/content stay finite", "[robustness][longrun]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    std::mt19937 rng (9001);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 512);

    // 2000 blocks * 512 samples ~= 21.3 s of audio at 48 kHz - enough to
    // exercise long-run accumulation issues (denormal creep, filter state
    // drift) while staying comfortably under a minute even in a Debug CI
    // build on Windows.
    constexpr int numBlocks = 2000;

    for (int block = 0; block < numBlocks; ++block)
    {
        // Re-randomise parameters only every 50 blocks so ramps/filters get
        // time to settle between changes, rather than automating every
        // single block (already covered by the dedicated automation test).
        if (block % 50 == 0)
        {
            setParam (processor, ParamIDs::threshold, -80.0f + unit (rng) * 80.0f);
            setParam (processor, ParamIDs::attack, unit (rng) * 50.0f);
            setParam (processor, ParamIDs::hold, unit (rng) * 250.0f);
            setParam (processor, ParamIDs::release, 5.0f + unit (rng) * 495.0f);
            setParam (processor, ParamIDs::range, -80.0f + unit (rng) * 80.0f);
            setParam (processor, ParamIDs::scHighpass, 20.0f + unit (rng) * 480.0f);
            setParam (processor, ParamIDs::scLowpass, 1000.0f + unit (rng) * 15000.0f);
            setParam (processor, ParamIDs::knee, unit (rng) * 24.0f);
            setParam (processor, ParamIDs::duck, unit (rng) > 0.7f ? 1.0f : 0.0f);
        }

        TestHelpers::fillWithSine (buffer, 48000.0, 100.0 + unit (rng) * 8000.0, 0.4f + unit (rng) * 0.5f);

        processor.processBlock (buffer, midi);

        if (! TestHelpers::allSamplesFinite (buffer))
        {
            FAIL ("Non-finite sample at block " << block);
            break;
        }
    }

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("reset() followed by processBlock does not crash", "[robustness]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::threshold, -30.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    juce::MidiBuffer midi;

    processor.processBlock (buffer, midi);

    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

//==============================================================================
// v0.4.0.
//==============================================================================

#include "AllocationGuard.h"

#include <cmath>
#include <vector>

namespace
{
    // Turns on every v0.4.0 addition at once - the configuration with the most
    // moving parts, and therefore the one worth guarding.
    void engageEveryNewFeature (SilentiumAudioProcessor& processor)
    {
        setParam (processor, ParamIDs::threshold, -35.0f);
        setParam (processor, ParamIDs::range, -60.0f);
        setParam (processor, ParamIDs::attack, 0.0f);
        setParam (processor, ParamIDs::hold, 20.0f);
        setParam (processor, ParamIDs::release, 80.0f);
        setParam (processor, ParamIDs::lookahead, 5.0f);
        setParam (processor, ParamIDs::knee, 6.0f);
        setParam (processor, ParamIDs::ratio, 3.0f);
        setParam (processor, ParamIDs::hysteresis, 6.0f);

        auto setChoiceOrToggle = [&processor] (const char* id, float normalised)
        {
            auto* param = processor.apvts.getParameter (id);
            REQUIRE (param != nullptr);
            param->setValueNotifyingHost (normalised);
        };

        setChoiceOrToggle (ParamIDs::detector, 1.0f);      // RMS
        setChoiceOrToggle (ParamIDs::scSlope, 1.0f);       // 24 dB/oct
        setChoiceOrToggle (ParamIDs::smoothOpen, 1.0f);    // on
        setChoiceOrToggle (ParamIDs::releaseShape, 1.0f);  // linear
    }
}

//==============================================================================
// T16 - the real-time contract.
//==============================================================================

TEST_CASE ("processBlock allocates nothing, with every v0.4.0 feature engaged", "[robustness][allocation]")
{
    SilentiumAudioProcessor processor;
    engageEveryNewFeature (processor);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    // Warm up outside the guard: the first blocks legitimately touch
    // lazily-initialised JUCE internals that a steady-state audio callback
    // never touches again.
    for (int b = 0; b < 8; ++b)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f, b * 512);
        processor.processBlock (buffer, midi);
    }

    SECTION ("steady state")
    {
        std::size_t allocations = 0;

        for (int b = 0; b < 64; ++b)
        {
            const auto loud = (b / 4) % 2 == 0;
            TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, loud ? 0.5f : 0.000001f, b * 512);

            ScopedAllocationGuard guard;
            processor.processBlock (buffer, midi);
            allocations += guard.count();
        }

        INFO ("allocations across 64 guarded blocks: " << allocations);
        CHECK (allocations == 0);
    }

    SECTION ("across a live Lookahead change, which moves the delay and republishes the latency")
    {
        // The block on which the lookahead actually moves is the interesting
        // one: it starts a tap crossfade and stores a new reported latency.
        // Neither may allocate - which is precisely why that hand-off is an
        // atomic polled by a timer rather than a message posted from here.
        std::size_t allocations = 0;

        for (int b = 0; b < 32; ++b)
        {
            if (b == 4)
                setParam (processor, ParamIDs::lookahead, 12.0f);
            else if (b == 16)
                setParam (processor, ParamIDs::lookahead, 2.0f);

            TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f, b * 512);

            ScopedAllocationGuard guard;
            processor.processBlock (buffer, midi);
            allocations += guard.count();
        }

        INFO ("allocations across a run containing two live Lookahead changes: " << allocations);
        CHECK (allocations == 0);
    }

    SECTION ("the guard itself detects an allocation, so a zero above means something")
    {
        // A guard that could never fire would make every assertion above
        // vacuous.
        //
        // The allocation goes through the volatile sink in AllocationGuard.h
        // on purpose: a plain `new float[64]` immediately followed by
        // `delete[]` is elidable under C++14 [expr.new]/10, and a Release
        // build does elide it - which made this self-test fail while the
        // vacuous assertions above still "passed".
        std::size_t counted = 0;

        {
            ScopedAllocationGuard guard;
            AllocationGuardDetail::allocationSink = new float[64];
            counted = guard.count();
        }

        delete[] static_cast<float*> (AllocationGuardDetail::allocationSink);
        AllocationGuardDetail::allocationSink = nullptr;

        CHECK (counted >= 1);
    }
}

TEST_CASE ("No denormals survive in the new state after a long silent tail", "[robustness][denormal]")
{
    SilentiumAudioProcessor processor;
    engageEveryNewFeature (processor);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    // A loud burst to charge every envelope, filter and smoother...
    for (int b = 0; b < 20; ++b)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f, b * 512);
        processor.processBlock (buffer, midi);
    }

    // ...then ten seconds of true digital silence, which is where a one-pole
    // that is not flushed decays into the denormal range and starts costing
    // hundreds of cycles per sample.
    constexpr int silentBlocks = 10 * 48000 / 512;

    for (int b = 0; b < silentBlocks; ++b)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
    }

    // The output must be exactly zero - not merely small. A residue in the
    // denormal range would still count as "silent" to the ear while quietly
    // wrecking the CPU budget.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            REQUIRE (buffer.getSample (channel, i) == 0.0f);

    // And the engine must still respond normally afterwards.
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f);
    processor.processBlock (buffer, midi);
    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) > 0.0f);
}

//==============================================================================
// T17 - block-size invariance.
//
// A host is free to hand over any block size it likes, and to change it
// between callbacks. Anything computed once per block rather than once per
// sample is a place where that freedom can leak into the sound.
//==============================================================================

TEST_CASE ("Output is independent of the host's block size, with every v0.4.0 feature engaged", "[robustness][blocksize]")
{
    constexpr int totalSamples = 48000; // 1 s

    auto renderAtBlockSize = [] (int blockSize)
    {
        SilentiumAudioProcessor processor;
        engageEveryNewFeature (processor);
        processor.prepareToPlay (48000.0, blockSize);

        juce::AudioBuffer<float> source (2, totalSamples);

        // Bursts and gaps, so the gate opens and closes several times during
        // the comparison rather than sitting still.
        for (int i = 0; i < totalSamples; ++i)
        {
            const auto t = static_cast<double> (i) / 48000.0;
            const auto burst = std::fmod (t, 0.25) < 0.12;
            const auto amplitude = burst ? 0.5 : 0.0005;
            const auto value = amplitude * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t);

            source.setSample (0, i, static_cast<float> (value));
            source.setSample (1, i, static_cast<float> (value));
        }

        juce::AudioBuffer<float> output (2, totalSamples);
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;

        for (int start = 0; start + blockSize <= totalSamples; start += blockSize)
        {
            for (int channel = 0; channel < 2; ++channel)
                block.copyFrom (channel, 0, source, channel, start, blockSize);

            processor.processBlock (block, midi);

            for (int channel = 0; channel < 2; ++channel)
                output.copyFrom (channel, start, block, channel, 0, blockSize);
        }

        return output;
    };

    const auto reference = renderAtBlockSize (64);

    for (const auto blockSize : { 32, 333, 1024 })
    {
        const auto candidate = renderAtBlockSize (blockSize);

        // Only compare the region every block size covers completely.
        const auto comparableSamples = (totalSamples / 1024) * 1024;

        auto sumOfSquares = 0.0;

        for (int i = 0; i < comparableSamples; ++i)
        {
            const auto difference = static_cast<double> (candidate.getSample (0, i))
                                     - static_cast<double> (reference.getSample (0, i));
            sumOfSquares += difference * difference;
        }

        const auto rmsDifference = std::sqrt (sumOfSquares / comparableSamples);

        INFO ("block size " << blockSize << " vs 64: RMS difference " << rmsDifference);
        CHECK (rmsDifference <= 1.0e-6);
    }
}
