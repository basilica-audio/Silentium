#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/GateEngine.h"
#include "presets/PresetManager.h"

// Silentium: a tight lookahead noise gate with hysteresis, for silencing amp
// hiss/hum between palm-muted chugs. Signal flow lives in GateEngine
// (src/dsp) so it stays unit-testable independent of this AudioProcessor;
// this class is just APVTS + host plumbing around it.
class SilentiumAudioProcessor final : public juce::AudioProcessor
{
public:
    SilentiumAudioProcessor();
    ~SilentiumAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // M2 preset system (.scaffold/specs/preset-system-m2.md,
    // src/presets/PresetManager.h). Constructed after apvts (its
    // constructor registers APVTS parameter listeners) and public so
    // SilentiumAudioProcessorEditor's PresetBar can talk to it directly -
    // the same "processor owns it, editor references it" pattern apvts
    // itself already uses.
    basilica::presets::PresetManager presetManager;

    // M3 GUI metering (src/gui/AnalogMeter.h): instantaneous values written
    // once per processBlock() with plain relaxed atomic stores (no locks/
    // allocation on the audio thread) and polled from the editor's timer.
    // Ballistic smoothing happens entirely on the GUI side (AnalogMeter),
    // never here.
    //
    // Gain reduction: the gate's currently applied gain in dB (0 = fully
    // open, negative = attenuating towards Range).
    float getGainReductionDb() const noexcept { return meterGainReductionDb.load (std::memory_order_relaxed); }

    // Input level: the current block's peak level in dBFS, measured on the
    // main input before the gate is applied (floored at -100 dB for silent/
    // empty blocks).
    float getInputLevelDb() const noexcept { return meterInputLevelDb.load (std::memory_order_relaxed); }

private:
    GateEngine engine;

    // Idle-rest fix: BOTH meter atomics must default (and, via reset()
    // below, re-converge) to a value at/below -20 dB, the AnalogMeter dial's
    // own tick-table floor (AnalogMeter.cpp's `ticks.front() == {-20dB,
    // -26.80deg}`, clamped for anything <= -20 - see AnalogMeter::
    // tickAngleDegreesForDb()) - so a freshly opened editor's needles rest
    // exactly on the -20 tick before the engine has ever run, rather than
    // wherever the previous default happened to map to.
    //
    // meterGainReductionDb previously defaulted to 0.0f (unity/fully-open),
    // which maps to the dial's 0dB tick (+18.02deg) - visibly NOT at rest -
    // for every instance between construction and this processor's own
    // first processBlock() call (the gap a real host's editor spends before
    // its first ~33ms GUI timer tick even asks). -100.0f matches
    // meterInputLevelDb's own long-standing floor convention and GateEngine's
    // closed-gate output range (Range defaults to -60dB, see
    // ParameterLayout.cpp - always <= -20 for any in-range Range value).
    std::atomic<float> meterGainReductionDb { -100.0f };
    std::atomic<float> meterInputLevelDb { -100.0f };

    // Raw atomic pointers into the APVTS-managed parameter values, resolved
    // once at construction time so processBlock() never has to search for
    // them (no allocation/locks on the audio thread).
    std::atomic<float>* thresholdDb = nullptr;
    std::atomic<float>* attackMs = nullptr;
    std::atomic<float>* holdMs = nullptr;
    std::atomic<float>* releaseMs = nullptr;
    std::atomic<float>* rangeDb = nullptr;
    std::atomic<float>* lookaheadMs = nullptr;
    std::atomic<float>* scHighpassHz = nullptr;
    std::atomic<float>* scLowpassHz = nullptr;
    std::atomic<float>* kneeDb = nullptr;
    std::atomic<float>* duckMode = nullptr;
    std::atomic<float>* listenMode = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilentiumAudioProcessor)
};
