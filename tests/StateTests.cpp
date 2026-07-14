#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
    CHECK (kneeParam->getValue() == Catch::Approx (savedKnee).margin (1e-6));
    CHECK (duckParam->getValue() == Catch::Approx (savedDuck).margin (1e-6));
    CHECK (listenParam->getValue() == Catch::Approx (savedListen).margin (1e-6));
}
