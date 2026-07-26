#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "GoldenFixture.h"
#include "data/GoldenRenders.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
    // Compares a fresh render's windowed level fingerprint against a
    // checked-in golden (see tests/data/README.md for why the comparison is
    // windowed rather than a bare hash). Reports the worst window rather than
    // spraying 186 individual failures, because the useful diagnostic is
    // "how far off, and where".
    void checkFingerprintMatchesGolden (const GoldenFixture::Fingerprint& measured,
                                         const double* goldenRms,
                                         const double* goldenPeak,
                                         const juce::String& what)
    {
        INFO ("golden comparison: " << what);

        REQUIRE (static_cast<int> (measured.windowRmsDb.size()) == GoldenRenders::numWindows);
        REQUIRE (static_cast<int> (measured.windowPeakDb.size()) == GoldenRenders::numWindows);

        auto worstRmsDelta = 0.0;
        auto worstPeakDelta = 0.0;
        auto worstRmsWindow = 0;
        auto worstPeakWindow = 0;

        for (int window = 0; window < GoldenRenders::numWindows; ++window)
        {
            const auto rmsDelta = std::abs (measured.windowRmsDb[static_cast<size_t> (window)] - goldenRms[window]);
            const auto peakDelta = std::abs (measured.windowPeakDb[static_cast<size_t> (window)] - goldenPeak[window]);

            if (rmsDelta > worstRmsDelta)
            {
                worstRmsDelta = rmsDelta;
                worstRmsWindow = window;
            }

            if (peakDelta > worstPeakDelta)
            {
                worstPeakDelta = peakDelta;
                worstPeakWindow = window;
            }
        }

        INFO ("worst RMS deviation " << worstRmsDelta << " dB at window " << worstRmsWindow);
        INFO ("worst peak deviation " << worstPeakDelta << " dB at window " << worstPeakWindow);

        CHECK (worstRmsDelta <= GoldenFixture::fingerprintToleranceDb);
        CHECK (worstPeakDelta <= GoldenFixture::fingerprintToleranceDb);
    }

    // The strict bit-identity tier. Only meaningful on the configuration the
    // golden was generated with - see tests/data/README.md - so elsewhere it
    // is recorded and skipped rather than failed.
    void checkExactHashWhereMeaningful (const GoldenFixture::Fingerprint& measured,
                                         const char* goldenSha256,
                                         const juce::String& what)
    {
        const auto tag = GoldenFixture::toolchainTag();

        if (tag != juce::String (GoldenRenders::toolchainTag))
        {
            WARN ("exact-hash check skipped for " << what << ": this build is " << tag
                    << ", the golden was generated on " << GoldenRenders::toolchainTag
                    << " (the windowed fingerprint check above still applies)");
            return;
        }

        INFO ("exact SHA-256 comparison: " << what);
        CHECK (measured.sha256 == juce::String (goldenSha256));
    }

    juce::MemoryBlock decodeLegacyStateBlob()
    {
        juce::MemoryOutputStream decoded;
        const auto ok = juce::Base64::convertFromBase64 (decoded, GoldenRenders::legacyStateBlobBase64);
        REQUIRE (ok);

        juce::MemoryBlock block;
        block.append (decoded.getData(), decoded.getDataSize());
        return block;
    }
}

TEST_CASE ("State round-trip preserves non-default values of every parameter", "[state]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* thresholdParam = processor.apvts.getParameter (ParamIDs::threshold);
    auto* attackParam = processor.apvts.getParameter (ParamIDs::attack);
    auto* holdParam = processor.apvts.getParameter (ParamIDs::hold);
    auto* releaseParam = processor.apvts.getParameter (ParamIDs::release);
    auto* rangeParam = processor.apvts.getParameter (ParamIDs::range);
    auto* lookaheadParam = processor.apvts.getParameter (ParamIDs::lookahead);
    auto* scHighpassParam = processor.apvts.getParameter (ParamIDs::scHighpass);
    auto* scLowpassParam = processor.apvts.getParameter (ParamIDs::scLowpass);
    auto* kneeParam = processor.apvts.getParameter (ParamIDs::knee);
    auto* duckParam = processor.apvts.getParameter (ParamIDs::duck);
    auto* listenParam = processor.apvts.getParameter (ParamIDs::listen);

    REQUIRE (thresholdParam != nullptr);
    REQUIRE (attackParam != nullptr);
    REQUIRE (holdParam != nullptr);
    REQUIRE (releaseParam != nullptr);
    REQUIRE (rangeParam != nullptr);
    REQUIRE (lookaheadParam != nullptr);
    REQUIRE (scHighpassParam != nullptr);
    REQUIRE (scLowpassParam != nullptr);
    REQUIRE (kneeParam != nullptr);
    REQUIRE (duckParam != nullptr);
    REQUIRE (listenParam != nullptr);

    thresholdParam->setValueNotifyingHost (thresholdParam->convertTo0to1 (-25.0f));
    attackParam->setValueNotifyingHost (attackParam->convertTo0to1 (5.0f));
    holdParam->setValueNotifyingHost (holdParam->convertTo0to1 (120.0f));
    releaseParam->setValueNotifyingHost (releaseParam->convertTo0to1 (200.0f));
    rangeParam->setValueNotifyingHost (rangeParam->convertTo0to1 (-45.0f));
    lookaheadParam->setValueNotifyingHost (lookaheadParam->convertTo0to1 (12.0f));
    scHighpassParam->setValueNotifyingHost (scHighpassParam->convertTo0to1 (200.0f));
    scLowpassParam->setValueNotifyingHost (scLowpassParam->convertTo0to1 (6000.0f));
    kneeParam->setValueNotifyingHost (kneeParam->convertTo0to1 (8.0f));
    duckParam->setValueNotifyingHost (1.0f);
    listenParam->setValueNotifyingHost (1.0f);

    const auto savedThreshold = thresholdParam->getValue();
    const auto savedAttack = attackParam->getValue();
    const auto savedHold = holdParam->getValue();
    const auto savedRelease = releaseParam->getValue();
    const auto savedRange = rangeParam->getValue();
    const auto savedLookahead = lookaheadParam->getValue();
    const auto savedScHighpass = scHighpassParam->getValue();
    const auto savedScLowpass = scLowpassParam->getValue();
    const auto savedKnee = kneeParam->getValue();
    const auto savedDuck = duckParam->getValue();
    const auto savedListen = listenParam->getValue();

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    // Reset every parameter back to its default before restoring, so the
    // round-trip assertion below can't pass by accident.
    thresholdParam->setValueNotifyingHost (thresholdParam->getDefaultValue());
    attackParam->setValueNotifyingHost (attackParam->getDefaultValue());
    holdParam->setValueNotifyingHost (holdParam->getDefaultValue());
    releaseParam->setValueNotifyingHost (releaseParam->getDefaultValue());
    rangeParam->setValueNotifyingHost (rangeParam->getDefaultValue());
    lookaheadParam->setValueNotifyingHost (lookaheadParam->getDefaultValue());
    scHighpassParam->setValueNotifyingHost (scHighpassParam->getDefaultValue());
    scLowpassParam->setValueNotifyingHost (scLowpassParam->getDefaultValue());
    kneeParam->setValueNotifyingHost (kneeParam->getDefaultValue());
    duckParam->setValueNotifyingHost (duckParam->getDefaultValue());
    listenParam->setValueNotifyingHost (listenParam->getDefaultValue());

    REQUIRE (thresholdParam->getValue() != Catch::Approx (savedThreshold));
    REQUIRE (attackParam->getValue() != Catch::Approx (savedAttack));
    REQUIRE (holdParam->getValue() != Catch::Approx (savedHold));
    REQUIRE (releaseParam->getValue() != Catch::Approx (savedRelease));
    REQUIRE (rangeParam->getValue() != Catch::Approx (savedRange));
    REQUIRE (lookaheadParam->getValue() != Catch::Approx (savedLookahead));
    REQUIRE (scHighpassParam->getValue() != Catch::Approx (savedScHighpass));
    REQUIRE (scLowpassParam->getValue() != Catch::Approx (savedScLowpass));
    REQUIRE (kneeParam->getValue() != Catch::Approx (savedKnee));
    REQUIRE (duckParam->getValue() != Catch::Approx (savedDuck));
    REQUIRE (listenParam->getValue() != Catch::Approx (savedListen));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    CHECK (thresholdParam->getValue() == Catch::Approx (savedThreshold).margin (1e-6));
    CHECK (attackParam->getValue() == Catch::Approx (savedAttack).margin (1e-6));
    CHECK (holdParam->getValue() == Catch::Approx (savedHold).margin (1e-6));
    CHECK (releaseParam->getValue() == Catch::Approx (savedRelease).margin (1e-6));
    CHECK (rangeParam->getValue() == Catch::Approx (savedRange).margin (1e-6));
    CHECK (lookaheadParam->getValue() == Catch::Approx (savedLookahead).margin (1e-6));
    CHECK (scHighpassParam->getValue() == Catch::Approx (savedScHighpass).margin (1e-6));
    CHECK (scLowpassParam->getValue() == Catch::Approx (savedScLowpass).margin (1e-6));
    CHECK (kneeParam->getValue() == Catch::Approx (savedKnee).margin (1e-6));
    CHECK (duckParam->getValue() == Catch::Approx (savedDuck).margin (1e-6));
    CHECK (listenParam->getValue() == Catch::Approx (savedListen).margin (1e-6));
}

//==============================================================================
// T1 - the cross-version neutrality guarantee.
//
// This is the test the whole tests/data/ artifact exists for. Everything else
// about v0.4.0 can be argued about; this one says, with evidence, that a
// session saved before v0.4.0 still sounds exactly the way it did.
//==============================================================================

TEST_CASE ("A v0.3.x state renders identically under v0.4.0 (cross-version golden)", "[state][golden][neutrality]")
{
    SilentiumAudioProcessor processor;

    const auto legacyState = decodeLegacyStateBlob();
    processor.setStateInformation (legacyState.getData(), static_cast<int> (legacyState.getSize()));

    SECTION ("every parameter v0.4.0 added sits at its exact-neutral default")
    {
        // The legacy blob carries none of these IDs. APVTS leaving them at
        // their defaults IS the migration - so this section is really the
        // proof that no default drifted away from neutral.
        auto* ratio = processor.apvts.getParameter (ParamIDs::ratio);
        auto* hysteresis = processor.apvts.getParameter (ParamIDs::hysteresis);
        auto* detector = processor.apvts.getParameter (ParamIDs::detector);
        auto* scSlope = processor.apvts.getParameter (ParamIDs::scSlope);
        auto* smoothOpen = processor.apvts.getParameter (ParamIDs::smoothOpen);
        auto* releaseShape = processor.apvts.getParameter (ParamIDs::releaseShape);

        REQUIRE (ratio != nullptr);
        REQUIRE (hysteresis != nullptr);
        REQUIRE (detector != nullptr);
        REQUIRE (scSlope != nullptr);
        REQUIRE (smoothOpen != nullptr);
        REQUIRE (releaseShape != nullptr);

        CHECK (ratio->convertFrom0to1 (ratio->getValue()) == Catch::Approx (ParamConstants::maxRatio).margin (1e-3));
        CHECK (hysteresis->convertFrom0to1 (hysteresis->getValue()) == Catch::Approx (3.0f).margin (1e-3));
        CHECK (detector->getValue() == Catch::Approx (0.0f));
        CHECK (scSlope->getValue() == Catch::Approx (0.0f));
        CHECK (smoothOpen->getValue() == Catch::Approx (0.0f));
        CHECK (releaseShape->getValue() == Catch::Approx (0.0f));
    }

    SECTION ("the render matches the golden captured from an actual v0.3.x build")
    {
        const auto fingerprint = GoldenFixture::fingerprintOf (GoldenFixture::render (processor));

        checkFingerprintMatchesGolden (fingerprint,
                                        GoldenRenders::legacyStateRms,
                                        GoldenRenders::legacyStatePeak,
                                        "v0.3.x state blob");

        checkExactHashWhereMeaningful (fingerprint, GoldenRenders::legacyStateSha256, "v0.3.x state blob");
    }

    SECTION ("the loaded state renders identically to the same settings dialled in by hand")
    {
        // Intra-version consistency ONLY. This says nothing about v0.3.x -
        // both sides are computed by this binary, so a regression in the
        // legacy path would move both of them together and this section
        // would stay green. It is here to catch a different bug: state
        // restoration itself dropping or mangling a parameter. The
        // cross-version guarantee is the section above.
        const auto restored = GoldenFixture::render (processor);

        SilentiumAudioProcessor dialledIn;
        GoldenFixture::applyLegacySettings (dialledIn);
        const auto manual = GoldenFixture::render (dialledIn);

        REQUIRE (restored.getNumSamples() == manual.getNumSamples());

        auto worstDifference = 0.0f;

        for (int sample = 0; sample < restored.getNumSamples(); ++sample)
            worstDifference = std::max (worstDifference,
                                         std::abs (restored.getReadPointer (0)[sample]
                                                    - manual.getReadPointer (0)[sample]));

        INFO ("worst sample difference " << worstDifference);
        CHECK (worstDifference == 0.0f);
    }
}

//==============================================================================
// T2 - the schema marker and its migration seam.
//==============================================================================

TEST_CASE ("State carries a schema version, and a state without one is treated as v1", "[state][version]")
{
    SECTION ("a state written by this build is stamped with the current schema version")
    {
        SilentiumAudioProcessor processor;

        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        REQUIRE (saved.getSize() > 0);

        const std::unique_ptr<juce::XmlElement> xml (
            juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize())));

        REQUIRE (xml != nullptr);
        CHECK (xml->hasAttribute (SilentiumAudioProcessor::stateVersionAttribute));
        CHECK (xml->getIntAttribute (SilentiumAudioProcessor::stateVersionAttribute)
                == SilentiumAudioProcessor::currentStateVersion);
    }

    SECTION ("the captured v0.3.x blob genuinely carries no version marker")
    {
        // If this ever fails, the golden blob was regenerated by a build that
        // already writes the marker - which would silently turn the migration
        // test above into a same-schema test.
        const auto legacyState = decodeLegacyStateBlob();

        const std::unique_ptr<juce::XmlElement> xml (
            juce::AudioProcessor::getXmlFromBinary (legacyState.getData(), static_cast<int> (legacyState.getSize())));

        REQUIRE (xml != nullptr);
        CHECK (! xml->hasAttribute (SilentiumAudioProcessor::stateVersionAttribute));
        CHECK (xml->getIntAttribute (SilentiumAudioProcessor::stateVersionAttribute, 1) == 1);
    }

    SECTION ("a v2 state round-trips every parameter, including the six v0.4.0 added")
    {
        SilentiumAudioProcessor processor;

        struct Setting { const char* id; float value; };

        const Setting settings[] = {
            { ParamIDs::threshold, -25.0f }, { ParamIDs::attack, 5.0f },
            { ParamIDs::hold, 120.0f },      { ParamIDs::release, 200.0f },
            { ParamIDs::range, -45.0f },     { ParamIDs::lookahead, 12.0f },
            { ParamIDs::scHighpass, 200.0f },{ ParamIDs::scLowpass, 6000.0f },
            { ParamIDs::knee, 8.0f },        { ParamIDs::ratio, 3.5f },
            { ParamIDs::hysteresis, 7.5f },
        };

        for (const auto& setting : settings)
        {
            auto* param = processor.apvts.getParameter (setting.id);
            REQUIRE (param != nullptr);
            param->setValueNotifyingHost (param->convertTo0to1 (setting.value));
        }

        const char* switchIds[] = {
            ParamIDs::duck, ParamIDs::listen, ParamIDs::smoothOpen,
            ParamIDs::detector, ParamIDs::scSlope, ParamIDs::releaseShape,
        };

        for (const auto* id : switchIds)
        {
            auto* param = processor.apvts.getParameter (id);
            REQUIRE (param != nullptr);
            param->setValueNotifyingHost (1.0f);
        }

        std::vector<float> expected;

        for (auto* param : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                expected.push_back (ranged->getValue());

        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        // Reset everything, so the restore below cannot pass by accident.
        for (auto* param : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

        processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        size_t index = 0;

        for (auto* param : processor.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                INFO ("parameter " << ranged->paramID);
                CHECK (ranged->getValue() == Catch::Approx (expected[index]).margin (1e-6));
                ++index;
            }
        }

        CHECK (index == expected.size());
    }

    SECTION ("a state claiming a future schema version still loads its known parameters")
    {
        SilentiumAudioProcessor source;
        auto* threshold = source.apvts.getParameter (ParamIDs::threshold);
        REQUIRE (threshold != nullptr);
        threshold->setValueNotifyingHost (threshold->convertTo0to1 (-17.0f));

        juce::MemoryBlock saved;
        source.getStateInformation (saved);

        const std::unique_ptr<juce::XmlElement> xml (
            juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize())));
        REQUIRE (xml != nullptr);
        xml->setAttribute (SilentiumAudioProcessor::stateVersionAttribute, 99);

        juce::MemoryBlock forwardState;
        juce::AudioProcessor::copyXmlToBinary (*xml, forwardState);

        SilentiumAudioProcessor destination;
        destination.setStateInformation (forwardState.getData(), static_cast<int> (forwardState.getSize()));

        auto* restored = destination.apvts.getParameter (ParamIDs::threshold);
        REQUIRE (restored != nullptr);
        CHECK (restored->convertFrom0to1 (restored->getValue()) == Catch::Approx (-17.0f).margin (0.05f));
    }
}
