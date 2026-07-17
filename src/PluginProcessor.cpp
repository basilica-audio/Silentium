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

    // M2 default resolution: user "Default" preset > factory "Default"
    // preset > the ParameterLayout defaults apvts was just constructed
    // with above (see PresetManager::applyStartupDefault()'s docs).
    presetManager.applyStartupDefault();
}

SilentiumAudioProcessor::~SilentiumAudioProcessor() = default;

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

    engine.prepare (spec);

    // Lookahead is the only source of the plugin's reported latency; the
    // main signal path is delayed internally by GateEngine (see
    // docs/architecture.md). Changing Lookahead live only takes effect on
    // the next prepareToPlay() (see GateEngine::getLatencySamples()).
    setLatencySamples (engine.getLatencySamples());
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
    copyXmlToBinary (*xml, destData);
}

void SilentiumAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SilentiumAudioProcessor();
}
