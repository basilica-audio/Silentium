#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

#include <BinaryData.h>

namespace
{
    // The small, Silentium-specific config surface PresetManager needs (see
    // src/presets/PresetManager.h's class docs) - everything else about the
    // preset system is fully generic and portable across the suite (see
    // basilica-audio/nave's docs/preset-system-notes.md, the M2 pilot's
    // replication recipe).
    basilica::presets::PresetManagerConfig makePresetManagerConfig()
    {
        // JucePlugin_CFBundleIdentifier expands to a raw (unquoted) token
        // sequence, not a string literal - JUCE_STRINGIFY() is the
        // documented way to turn it into one. This is always
        // "com.yvesvogl.silentium" here (BUNDLE_ID in CMakeLists.txt),
        // matching the "plugin" field baked into every
        // presets/factory/*.json file.
        basilica::presets::PresetManagerConfig config;
        config.pluginId = JUCE_STRINGIFY (JucePlugin_CFBundleIdentifier);
        config.pluginName = JucePlugin_Name;
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = JucePlugin_VersionString;
        // userPresetsDirectoryOverrideForTests intentionally left
        // default-constructed (empty) - production instances always use the
        // real platform-standard preset location (see PresetManager.h).
        return config;
    }

    // BinaryData symbol names are derived from the presets/factory/*.json
    // file names passed to juce_add_binary_data() in CMakeLists.txt (dots
    // become underscores) - this list must stay in sync with that SOURCES
    // list. Order here only affects factory-preset iteration order before
    // getAllPresets() re-sorts alphabetically, so it isn't otherwise
    // significant.
    std::vector<basilica::presets::FactoryPresetAsset> makeFactoryPresetAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::surgicalMute_json, BinaryData::surgicalMute_jsonSize },
            { BinaryData::naturalDecay_json, BinaryData::naturalDecay_jsonSize },
            { BinaryData::pickAttackFocus_json, BinaryData::pickAttackFocus_jsonSize },
            { BinaryData::diKeyedWorkflow_json, BinaryData::diKeyedWorkflow_jsonSize },
            { BinaryData::ambientSustain_json, BinaryData::ambientSustain_jsonSize },
            { BinaryData::chugLock_json, BinaryData::chugLock_jsonSize },
            { BinaryData::duckUnderLead_json, BinaryData::duckUnderLead_jsonSize },
            { BinaryData::listenCheck_json, BinaryData::listenCheck_jsonSize },
            { BinaryData::expanderGlue_json, BinaryData::expanderGlue_jsonSize },
        };
    }
}

//==============================================================================
SilentiumAudioProcessor::SilentiumAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          // Optional external sidechain input, disabled by default so
                          // existing sessions/hosts see no behaviour change until a user
                          // explicitly enables it in their host's routing matrix. See
                          // isBusesLayoutSupported() and processBlock() for how a
                          // disabled/unconnected sidechain falls back to self-detection.
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetManager (apvts, makePresetManagerConfig(), makeFactoryPresetAssets())
{
    thresholdDb = apvts.getRawParameterValue (ParamIDs::threshold);
    attackMs = apvts.getRawParameterValue (ParamIDs::attack);
    holdMs = apvts.getRawParameterValue (ParamIDs::hold);
    releaseMs = apvts.getRawParameterValue (ParamIDs::release);
    rangeDb = apvts.getRawParameterValue (ParamIDs::range);
    lookaheadMs = apvts.getRawParameterValue (ParamIDs::lookahead);
    scHighpassHz = apvts.getRawParameterValue (ParamIDs::scHighpass);
    scLowpassHz = apvts.getRawParameterValue (ParamIDs::scLowpass);
    kneeDb = apvts.getRawParameterValue (ParamIDs::knee);
    duckMode = apvts.getRawParameterValue (ParamIDs::duck);
    listenMode = apvts.getRawParameterValue (ParamIDs::listen);
    ratio = apvts.getRawParameterValue (ParamIDs::ratio);
    hysteresisDb = apvts.getRawParameterValue (ParamIDs::hysteresis);
    detectorChoice = apvts.getRawParameterValue (ParamIDs::detector);
    scSlopeChoice = apvts.getRawParameterValue (ParamIDs::scSlope);
    smoothOpen = apvts.getRawParameterValue (ParamIDs::smoothOpen);
    releaseShapeChoice = apvts.getRawParameterValue (ParamIDs::releaseShape);

    jassert (thresholdDb != nullptr);
    jassert (attackMs != nullptr);
    jassert (holdMs != nullptr);
    jassert (releaseMs != nullptr);
    jassert (rangeDb != nullptr);
    jassert (lookaheadMs != nullptr);
    jassert (scHighpassHz != nullptr);
    jassert (scLowpassHz != nullptr);
    jassert (kneeDb != nullptr);
    jassert (duckMode != nullptr);
    jassert (listenMode != nullptr);
    jassert (ratio != nullptr);
    jassert (hysteresisDb != nullptr);
    jassert (detectorChoice != nullptr);
    jassert (scSlopeChoice != nullptr);
    jassert (smoothOpen != nullptr);
    jassert (releaseShapeChoice != nullptr);

    // M2 default resolution: user "Default" preset > factory "Default"
    // preset > the ParameterLayout defaults apvts was just constructed
    // with above (see PresetManager::applyStartupDefault()'s docs).
    presetManager.applyStartupDefault();

    // v0.4.0 F6: publishes live Lookahead changes to the host from the
    // message thread (see timerCallback()).
    startTimer (latencyPollIntervalMs);
}

SilentiumAudioProcessor::~SilentiumAudioProcessor()
{
    stopTimer();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SilentiumAudioProcessor::createParameterLayout()
{
    return slnt::createParameterLayout();
}

//==============================================================================
const juce::String SilentiumAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SilentiumAudioProcessor::acceptsMidi() const
{
    return false;
}

bool SilentiumAudioProcessor::producesMidi() const
{
    return false;
}

bool SilentiumAudioProcessor::isMidiEffect() const
{
    return false;
}

double SilentiumAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SilentiumAudioProcessor::getNumPrograms()
{
    return 1;
}

int SilentiumAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SilentiumAudioProcessor::setCurrentProgram (int)
{
}

const juce::String SilentiumAudioProcessor::getProgramName (int)
{
    return {};
}

void SilentiumAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void SilentiumAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (getTotalNumOutputChannels());

    // Seed the engine's parameters from the current APVTS state before
    // prepare() derives the lookahead delay/latency and primes the sidechain
    // filter coefficients, so the very first block after prepareToPlay()
    // already reflects the host/session's actual parameter values rather
    // than the engine's built-in defaults.
    applyParametersToEngine();

    engine.prepare (spec);

    // Lookahead is the only source of the plugin's reported latency; the
    // main signal path is delayed internally by GateEngine (see
    // docs/architecture.md). Changing Lookahead live only takes effect on
    // the next prepareToPlay() (see GateEngine::getLatencySamples()).
    setLatencySamples (engine.getLatencySamples());
}

void SilentiumAudioProcessor::applyParametersToEngine()
{
    engine.setThresholdDb (thresholdDb->load (std::memory_order_relaxed));
    engine.setAttackMs (attackMs->load (std::memory_order_relaxed));
    engine.setHoldMs (holdMs->load (std::memory_order_relaxed));
    engine.setReleaseMs (releaseMs->load (std::memory_order_relaxed));
    engine.setRangeDb (rangeDb->load (std::memory_order_relaxed));
    engine.setLookaheadMs (lookaheadMs->load (std::memory_order_relaxed));
    engine.setScHighpassHz (scHighpassHz->load (std::memory_order_relaxed));
    engine.setScLowpassHz (scLowpassHz->load (std::memory_order_relaxed));
    engine.setKneeDb (kneeDb->load (std::memory_order_relaxed));
    engine.setDuckingMode (duckMode->load (std::memory_order_relaxed) >= 0.5f);
    engine.setListenMode (listenMode->load (std::memory_order_relaxed) >= 0.5f);

    // v0.4.0. The choice parameters' raw APVTS value is the selected index as
    // a float, so each is compared against the index constant that names it
    // (ParamIDs.h's ParamConstants) rather than against a bare number.
    engine.setRatio (ratio->load (std::memory_order_relaxed));
    engine.setHysteresisDb (hysteresisDb->load (std::memory_order_relaxed));
    engine.setDetectorMode (juce::roundToInt (detectorChoice->load (std::memory_order_relaxed)) == ParamConstants::detectorRms);
    engine.setScSlope24 (juce::roundToInt (scSlopeChoice->load (std::memory_order_relaxed)) == ParamConstants::scSlope24);
    engine.setSmoothOpen (smoothOpen->load (std::memory_order_relaxed) >= 0.5f);
    engine.setReleaseShapeLinear (juce::roundToInt (releaseShapeChoice->load (std::memory_order_relaxed)) == ParamConstants::releaseShapeLinear);
}

void SilentiumAudioProcessor::releaseResources()
{
}

void SilentiumAudioProcessor::reset()
{
    engine.reset();
}

bool SilentiumAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto disabled = juce::AudioChannelSet::disabled();

    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != mono && mainOut != stereo)
        return false;

    if (mainOut != mainIn)
        return false;

    // The optional sidechain input (bus index 1) may be disabled entirely -
    // the common case, and the one every host defaults to - or mono/stereo,
    // independent of the main bus's own channel count: a mono kick-drum
    // sidechain triggering a stereo guitar gate is a normal use case.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechainSet = layouts.inputBuses.getReference (1);

        if (sidechainSet != disabled && sidechainSet != mono && sidechainSet != stereo)
            return false;
    }

    return true;
}

void SilentiumAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The main bus is constrained to in == out (mono or stereo, see
    // isBusesLayoutSupported()), so getBusBuffer() for bus 0 returns the
    // exact same channel range for both directions - this is the correct,
    // allocation-free way to get a view restricted to just the main bus's
    // channels even when the optional sidechain bus (bus 1, input-only)
    // widens the combined `buffer` passed in by the host beyond the main
    // bus's own channel count.
    auto mainBuffer = getBusBuffer (buffer, false, 0);

    applyParametersToEngine();

    juce::dsp::AudioBlock<float> mainBlock (mainBuffer);

    // Sidechain (bus 1, input-only): getBusBuffer() safely returns a
    // zero-channel view whenever the bus doesn't exist, is disabled (the
    // default - see the constructor), or the host simply hasn't connected
    // anything to it, so GateEngine's fallback to self-detection (see
    // GateEngine::process()) covers all of those "no sidechain" cases with
    // no extra branching needed here.
    auto sidechainBuffer = getBusBuffer (buffer, true, 1);
    juce::dsp::AudioBlock<float> sidechainBlock (sidechainBuffer);
    const auto* sidechainBlockPtr = sidechainBlock.getNumChannels() > 0 ? &sidechainBlock : nullptr;

    // M3 GUI metering, input side: the block's peak level BEFORE the gate
    // is applied (so the meter shows what is driving the detector, not the
    // already-gated output). getMagnitude() is a simple allocation-free
    // scan; skipped for zero-sample blocks so the last real level holds
    // rather than collapsing to the floor.
    const auto numSamples = mainBuffer.getNumSamples();

    if (numSamples > 0 && mainBuffer.getNumChannels() > 0)
        meterInputLevelDb.store (juce::Decibels::gainToDecibels (mainBuffer.getMagnitude (0, numSamples), -100.0f),
                                 std::memory_order_relaxed);

    engine.process (mainBlock, sidechainBlockPtr);

    // Gain-reduction side, read after process() so it reflects this block's
    // final ramp position.
    meterGainReductionDb.store (engine.getCurrentGainDb(), std::memory_order_relaxed);

    // v0.4.0 F7: plus the extrema actually reached inside the block, which
    // the block-boundary value above cannot show.
    meterGainReductionMinDb.store (engine.getBlockMinGainDb(), std::memory_order_relaxed);
    meterGainReductionMaxDb.store (engine.getBlockMaxGainDb(), std::memory_order_relaxed);
}

void SilentiumAudioProcessor::timerCallback()
{
    // v0.4.0 F6. The engine moves its applied lookahead delay immediately
    // (crossfaded, on the audio thread) and publishes the new value; the host
    // must be told about it, but setLatencySamples()/updateHostDisplay() are
    // message-thread-only. This is that hand-off, and it is the ONLY place
    // the reported latency changes outside prepareToPlay().
    const auto engineLatency = engine.getLatencySamples();

    if (engineLatency != getLatencySamples())
        setLatencySamples (engineLatency);
}

//==============================================================================
bool SilentiumAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SilentiumAudioProcessor::createEditor()
{
    return new SilentiumAudioProcessorEditor (*this);
}

//==============================================================================
void SilentiumAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());

    // v0.4.0: stamp the schema version onto the root element. It is written
    // as an XML attribute rather than as a ValueTree property so it never
    // appears as a phantom entry in the APVTS state itself, and so an older
    // build - which knows nothing about it - simply ignores it and loads the
    // parameters as usual.
    xml->setAttribute (stateVersionAttribute, currentStateVersion);

    copyXmlToBinary (*xml, destData);
}

void SilentiumAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState == nullptr || ! xmlState->hasTagName (apvts.state.getType()))
        return;

    // A state written before v0.4.0 carries no version attribute; treat it as
    // schema 1.
    const auto stateVersion = xmlState->getIntAttribute (stateVersionAttribute, 1);

    switch (stateVersion)
    {
        case 1:
            // v0.3.x and earlier. No transform: every parameter v0.4.0 added
            // defaults to its exact-neutral value, and APVTS leaves absent
            // parameters at their defaults - so simply replacing the state
            // reproduces the old session's rendering exactly. This is
            // asserted against a render captured from an actual v0.3.x build
            // in tests/StateTests.cpp, not merely assumed.
            break;

        case currentStateVersion:
            break;

        default:
            // A state from a FUTURE build. APVTS already ignores parameter
            // IDs it does not know and keeps its own defaults for the ones
            // the file omits, so loading it is the best available behaviour -
            // strictly better than refusing and leaving the user with
            // whatever happened to be dialled in.
            break;
    }

    apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SilentiumAudioProcessor();
}
