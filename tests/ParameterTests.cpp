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
            ParamIDs::range, ParamIDs::lookahead, ParamIDs::scHighpass, ParamIDs::scLowpass,
            ParamIDs::knee, ParamIDs::duck, ParamIDs::listen,
            // v0.4.0
            ParamIDs::ratio, ParamIDs::hysteresis, ParamIDs::detector,
            ParamIDs::scSlope, ParamIDs::smoothOpen, ParamIDs::releaseShape,
        };

        for (const auto* id : allIds)
            CHECK (apvts.getParameter (id) != nullptr);
    }

    SECTION ("total parameter count matches the v0.4.0 layout (v0.2.0's 11 plus six)")
    {
        CHECK (apvts.processor.getParameters().size() == 17);
    }

    SECTION ("Threshold: open threshold defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::threshold, -40.0f);
        checkFloatRange (apvts, ParamIDs::threshold, -80.0f, 0.0f);
    }

    SECTION ("Attack: open ramp time defaults and range (v0.2.0 lowers the floor to 0 ms)")
    {
        checkFloatDefault (apvts, ParamIDs::attack, 1.0f);
        checkFloatRange (apvts, ParamIDs::attack, 0.0f, 50.0f);
    }

    SECTION ("Hold: minimum open time defaults and range (v0.2.0 lowers the ceiling to 250 ms)")
    {
        checkFloatDefault (apvts, ParamIDs::hold, 20.0f);
        checkFloatRange (apvts, ParamIDs::hold, 0.0f, 250.0f);
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

    SECTION ("SC LPF (v0.2.0): sidechain low-pass defaults fully open, at the top of its range")
    {
        checkFloatDefault (apvts, ParamIDs::scLowpass, 16000.0f);
        checkFloatRange (apvts, ParamIDs::scLowpass, 1000.0f, 16000.0f);
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

    //==========================================================================
    // v0.4.0. Every assertion below is really the same assertion: the default
    // is the value that reproduces v0.3.x behaviour. If any of these drifts,
    // an existing session silently changes how it sounds.
    //==========================================================================

    SECTION ("Ratio (v0.4.0): defaults to the top of its range, i.e. a gate")
    {
        checkFloatDefault (apvts, ParamIDs::ratio, ParamConstants::maxRatio);
        checkFloatRange (apvts, ParamIDs::ratio, 1.0f, ParamConstants::maxRatio);
    }

    SECTION ("Ratio (v0.4.0): the top of the range is labelled as a gate, other values as a ratio")
    {
        auto* param = requireParam (apvts, ParamIDs::ratio);

        const auto atMaximum = param->getText (param->convertTo0to1 (ParamConstants::maxRatio), 64);
        CHECK (atMaximum.containsIgnoreCase ("Gate"));

        const auto atFour = param->getText (param->convertTo0to1 (4.0f), 64);
        CHECK (atFour.contains ("4.00"));
        CHECK (atFour.contains (": 1"));

        // Round-trip: what the parameter prints must parse back to the same
        // value, or a host's text entry silently lands somewhere else.
        CHECK (param->getValueForText (atFour)
                == Catch::Approx (param->convertTo0to1 (4.0f)).margin (1e-4));
        CHECK (param->getValueForText (atMaximum)
                == Catch::Approx (param->convertTo0to1 (ParamConstants::maxRatio)).margin (1e-4));
    }

    SECTION ("Hysteresis (v0.4.0): defaults to the 3 dB that used to be a fixed internal constant")
    {
        checkFloatDefault (apvts, ParamIDs::hysteresis, 3.0f);
        checkFloatRange (apvts, ParamIDs::hysteresis, 0.0f, 12.0f);
    }

    SECTION ("Detector (v0.4.0): Peak and RMS, defaulting to Peak")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::detector));
        REQUIRE (param != nullptr);
        CHECK (param->choices == juce::StringArray { "Peak", "RMS" });
        CHECK (requireParam (apvts, ParamIDs::detector)->getDefaultValue() == Catch::Approx (0.0f));
        CHECK (ParamConstants::detectorPeak == 0);
        CHECK (ParamConstants::detectorRms == 1);
    }

    SECTION ("SC Slope (v0.4.0): 12 and 24 dB/oct, defaulting to 12")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::scSlope));
        REQUIRE (param != nullptr);
        CHECK (param->choices == juce::StringArray { "12 dB/oct", "24 dB/oct" });
        CHECK (requireParam (apvts, ParamIDs::scSlope)->getDefaultValue() == Catch::Approx (0.0f));
    }

    SECTION ("Smooth Open (v0.4.0): off by default")
    {
        auto* param = requireParam (apvts, ParamIDs::smoothOpen);
        CHECK (param->getDefaultValue() == Catch::Approx (0.0f));
    }

    SECTION ("Release Shape (v0.4.0): Exponential and Linear, defaulting to Exponential")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::releaseShape));
        REQUIRE (param != nullptr);
        CHECK (param->choices == juce::StringArray { "Exponential", "Linear" });
        CHECK (requireParam (apvts, ParamIDs::releaseShape)->getDefaultValue() == Catch::Approx (0.0f));
    }

    SECTION ("every v0.4.0 parameter carries version hint 1, so its ID is stable in VST3 hosts")
    {
        static constexpr const char* newIds[] = {
            ParamIDs::ratio, ParamIDs::hysteresis, ParamIDs::detector,
            ParamIDs::scSlope, ParamIDs::smoothOpen, ParamIDs::releaseShape,
        };

        for (const auto* id : newIds)
        {
            auto* param = dynamic_cast<juce::AudioProcessorParameterWithID*> (apvts.getParameter (id));
            REQUIRE (param != nullptr);
            CHECK (param->getVersionHint() == 1);
        }
    }
}
