#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    // Convenience wrapper: fetches a parameter by ID and requires it to
    // exist before returning, so every SECTION below fails loudly (not with
    // a null-deref) if an ID typo ever creeps in.
    juce::RangedAudioParameter* requireParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* param = apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param;
    }

    // Checks that a float parameter's underlying NormalisableRange covers
    // [expectedMin, expectedMax], independent of any skew/log mapping.
    void checkFloatRange (juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& id,
                           float expectedMin,
                           float expectedMax)
    {
        auto* param = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (id));
        REQUIRE (param != nullptr);

        const auto range = param->getNormalisableRange().getRange();
        CHECK (range.getStart() == Catch::Approx (expectedMin));
        CHECK (range.getEnd() == Catch::Approx (expectedMax));
    }

    // Checks a float parameter's default value in real (non-normalised)
    // units, going through convertTo0to1 so log-skewed ranges are handled
    // the same way as linear ones.
    void checkFloatDefault (juce::AudioProcessorValueTreeState& apvts,
                             const juce::String& id,
                             float expectedDefault)
    {
        auto* param = requireParam (apvts, id);
        CHECK (param->getDefaultValue() == Catch::Approx (param->convertTo0to1 (expectedDefault)).margin (1e-4));
    }
}

TEST_CASE ("Processor instantiates with the expected parameters", "[processor][parameters]")
{
    SilentiumAudioProcessor processor;
    auto& apvts = processor.apvts;

    SECTION ("plugin name")
    {
        CHECK (processor.getName() == juce::String ("Silentium"));
    }

    SECTION ("all documented parameter IDs resolve")
    {
        static constexpr const char* allIds[] = {
            ParamIDs::threshold, ParamIDs::attack, ParamIDs::hold, ParamIDs::release,
            ParamIDs::range, ParamIDs::lookahead, ParamIDs::scHighpass,
            ParamIDs::knee, ParamIDs::duck, ParamIDs::listen,
        };

        for (const auto* id : allIds)
            CHECK (apvts.getParameter (id) != nullptr);
    }

    SECTION ("total parameter count matches the v0.1.0 layout")
    {
        CHECK (apvts.processor.getParameters().size() == 10);
    }

    SECTION ("Threshold: open threshold defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::threshold, -40.0f);
        checkFloatRange (apvts, ParamIDs::threshold, -80.0f, 0.0f);
    }

    SECTION ("Attack: open ramp time defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::attack, 1.0f);
        checkFloatRange (apvts, ParamIDs::attack, 0.1f, 50.0f);
    }

    SECTION ("Hold: minimum open time defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::hold, 20.0f);
        checkFloatRange (apvts, ParamIDs::hold, 0.0f, 500.0f);
    }

    SECTION ("Release: close ramp time defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::release, 80.0f);
        checkFloatRange (apvts, ParamIDs::release, 5.0f, 500.0f);
    }

    SECTION ("Range: floor attenuation defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::range, -60.0f);
        checkFloatRange (apvts, ParamIDs::range, -80.0f, 0.0f);
    }

    SECTION ("Lookahead: main-path delay defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::lookahead, 5.0f);
        checkFloatRange (apvts, ParamIDs::lookahead, 0.0f, 20.0f);
    }

    SECTION ("SC HPF: sidechain high-pass defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::scHighpass, 80.0f);
        checkFloatRange (apvts, ParamIDs::scHighpass, 20.0f, 500.0f);
    }

    SECTION ("Knee: soft-knee width defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::knee, 0.0f);
        checkFloatRange (apvts, ParamIDs::knee, 0.0f, 24.0f);
    }

    SECTION ("Duck: off by default")
    {
        auto* param = requireParam (apvts, ParamIDs::duck);
        CHECK (param->getDefaultValue() == Catch::Approx (0.0f));
    }

    SECTION ("Listen: off by default")
    {
        auto* param = requireParam (apvts, ParamIDs::listen);
        CHECK (param->getDefaultValue() == Catch::Approx (0.0f));
    }
}
