// Bus-layout coverage for M1's "Broaden test coverage" issue: mono/stereo
// main-bus configurations, and the optional sidechain input bus's
// isBusesLayoutSupported() contract (disabled/mono/stereo accepted,
// anything else rejected). See PluginProcessor.cpp for the implementation
// this exercises.

#include "PluginProcessor.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    juce::AudioProcessor::BusesLayout makeLayout (juce::AudioChannelSet mainSet,
                                                    juce::AudioChannelSet sidechainSet = juce::AudioChannelSet::disabled())
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (mainSet);
        layout.inputBuses.add (sidechainSet);
        layout.outputBuses.add (mainSet);
        return layout;
    }
}

TEST_CASE ("isBusesLayoutSupported: main bus mono/stereo, in == out", "[processor][buses]")
{
    SilentiumAudioProcessor processor;

    CHECK (processor.isBusesLayoutSupported (makeLayout (juce::AudioChannelSet::mono())));
    CHECK (processor.isBusesLayoutSupported (makeLayout (juce::AudioChannelSet::stereo())));
    CHECK_FALSE (processor.isBusesLayoutSupported (makeLayout (juce::AudioChannelSet::create5point1())));
}

TEST_CASE ("isBusesLayoutSupported: main bus input must match output", "[processor][buses]")
{
    SilentiumAudioProcessor processor;

    juce::AudioProcessor::BusesLayout mismatched;
    mismatched.inputBuses.add (juce::AudioChannelSet::mono());
    mismatched.inputBuses.add (juce::AudioChannelSet::disabled());
    mismatched.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK_FALSE (processor.isBusesLayoutSupported (mismatched));
}

TEST_CASE ("isBusesLayoutSupported: sidechain bus accepts disabled, mono, or stereo", "[processor][buses][sidechain]")
{
    SilentiumAudioProcessor processor;

    CHECK (processor.isBusesLayoutSupported (makeLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled())));
    CHECK (processor.isBusesLayoutSupported (makeLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono())));
    CHECK (processor.isBusesLayoutSupported (makeLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())));

    // Independent of the main bus's own channel count.
    CHECK (processor.isBusesLayoutSupported (makeLayout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())));
}

TEST_CASE ("isBusesLayoutSupported: sidechain bus rejects unsupported channel sets", "[processor][buses][sidechain]")
{
    SilentiumAudioProcessor processor;

    CHECK_FALSE (processor.isBusesLayoutSupported (makeLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::create5point1())));
    CHECK_FALSE (processor.isBusesLayoutSupported (makeLayout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::createLCR())));
}

TEST_CASE ("Sidechain bus starts disabled by default (no host behaviour change without opt-in)", "[processor][buses][sidechain]")
{
    SilentiumAudioProcessor processor;

    const auto* sidechainBus = processor.getBus (true, 1);
    REQUIRE (sidechainBus != nullptr);
    CHECK_FALSE (sidechainBus->isEnabled());
}

TEST_CASE ("Mono main bus: full processBlock round trip is finite and stable", "[processor][buses][mono]")
{
    SilentiumAudioProcessor processor;

    auto layout = makeLayout (juce::AudioChannelSet::mono());
    REQUIRE (processor.setBusesLayout (layout));

    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (1, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Stereo main bus: full processBlock round trip is finite and stable", "[processor][buses][stereo]")
{
    SilentiumAudioProcessor processor;

    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.7f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Switching between mono and stereo main-bus layouts across prepareToPlay calls stays stable", "[processor][buses]")
{
    SilentiumAudioProcessor processor;

    REQUIRE (processor.setBusesLayout (makeLayout (juce::AudioChannelSet::stereo())));
    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> stereoBuffer (2, 256);
    TestHelpers::fillWithSine (stereoBuffer, 48000.0, 1000.0, 0.5f);
    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (stereoBuffer, midi));
    CHECK (TestHelpers::allSamplesFinite (stereoBuffer));

    REQUIRE (processor.setBusesLayout (makeLayout (juce::AudioChannelSet::mono())));
    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> monoBuffer (1, 256);
    TestHelpers::fillWithSine (monoBuffer, 48000.0, 1000.0, 0.5f);
    CHECK_NOTHROW (processor.processBlock (monoBuffer, midi));
    CHECK (TestHelpers::allSamplesFinite (monoBuffer));
}
