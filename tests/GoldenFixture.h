#pragma once

#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/PresetManager.h"

#include <BinaryData.h>

#include <juce_cryptography/juce_cryptography.h>

#include <cmath>
#include <cstdint>
#include <vector>

// Deterministic offline render fixture shared by tests/tools/
// GoldenRenderHarness.cpp (which GENERATES the checked-in golden artifacts in
// tests/data/) and the tests that COMPARE against them (StateTests.cpp,
// GateEngineTests.cpp, PresetManagerTests.cpp - brief §4/§6-T1/T4/T20).
//
// Everything here is fixed by construction so that the same source, built at
// two different commits, renders the same audio: fixed sample rate, fixed
// block size, fixed total length, a stimulus built from a seeded LCG plus a
// closed-form note model (no RNG library, no time/threading dependence), and
// a fixed parameter set. Nothing in this header may change once the goldens
// in tests/data/ have been generated - changing it silently invalidates every
// cross-version guarantee those artifacts exist to provide.
//
// FLOAT-DETERMINISM POLICY (see tests/data/README.md for the full rationale):
// bit-identical float output across *different* toolchains/architectures/
// optimisation levels is not achievable - libm's sin/cos/exp differ by ulps
// between platforms and FMA contraction differs between -O0 and -O2. The
// golden artifacts therefore carry two tiers:
//
//   Tier A - a windowed level fingerprint (per-1024-sample RMS and peak, in
//            dB). Portable across every platform the plugin builds on, and
//            asserted unconditionally by the tests. Its tolerance
//            (fingerprintToleranceDb) is ~3 orders of magnitude tighter than
//            any behavioural change a regression in the legacy path could
//            produce, so it is a real cross-version guarantee rather than a
//            formality.
//   Tier B - the exact SHA-256 of the rendered samples. Asserted only when
//            the running build's toolchain tag matches the tag recorded
//            alongside the golden (i.e. on the generating configuration);
//            reported and skipped elsewhere. This is the strict bit-identity
//            check the brief specifies, kept meaningful rather than made
//            flaky.
namespace GoldenFixture
{
    inline constexpr double sampleRate = 48000.0;
    inline constexpr int blockSize = 512;

    // 186 blocks == 93 windows == 1.984 s. Chosen to be an exact multiple of
    // both blockSize and windowSamples so neither the render loop nor the
    // fingerprint has a partial tail to special-case.
    inline constexpr int numSamples = 95232;
    inline constexpr int windowSamples = 1024;
    inline constexpr int numWindows = numSamples / windowSamples;

    // Tier-A comparison tolerance, in dB, on every windowed RMS/peak value.
    // Accumulated cross-toolchain float divergence through the sidechain
    // IIRs and the envelope follower stays around 1e-5 dB; any real change
    // to the gain law, knee, hysteresis or ballistics moves a window by more
    // than 1e-2 dB. 2e-3 dB sits between the two with wide margin on both
    // sides.
    inline constexpr double fingerprintToleranceDb = 2.0e-3;

    // Compile-time identification of the build configuration, recorded next
    // to each golden so the Tier-B exact hash check knows whether it is
    // looking at the configuration that generated it.
    inline juce::String toolchainTag()
    {
        juce::String tag;

       #if JUCE_MAC
        tag << "macos";
       #elif JUCE_WINDOWS
        tag << "windows";
       #elif JUCE_LINUX
        tag << "linux";
       #else
        tag << "other";
       #endif

       #if defined (__aarch64__) || defined (_M_ARM64)
        tag << "-arm64";
       #elif defined (__x86_64__) || defined (_M_X64)
        tag << "-x86_64";
       #else
        tag << "-unknownarch";
       #endif

       #if defined (__clang__)
        tag << "-clang" << __clang_major__;
       #elif defined (_MSC_VER)
        tag << "-msvc" << (int) (_MSC_VER);
       #elif defined (__GNUC__)
        tag << "-gcc" << __GNUC__;
       #endif

       #if defined (NDEBUG)
        tag << "-release";
       #else
        tag << "-debug";
       #endif

        return tag;
    }

    //==========================================================================
    // Deterministic 32-bit LCG (Numerical Recipes constants). Used only for
    // the stimulus' noise floor, so the "hiss" the gate is asked to close on
    // is identical on every platform.
    struct Lcg
    {
        std::uint32_t state = 0u;

        float next() noexcept
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<float> (static_cast<double> (state >> 8) / 8388608.0 - 1.0);
        }
    };

    // The fixed stimulus: seven sharp note onsets (cosine-phase attacks, so
    // each onset is a genuine discontinuity that crosses Threshold within a
    // single sample - this is what makes the gate's open decision robust
    // against last-ulp detector differences across platforms) with an
    // exponential decay, over a constant -80 dBFS noise floor, and a ~0.48 s
    // decayed tail at the end so the release/close path is exercised too.
    inline void fillStimulus (juce::AudioBuffer<float>& buffer)
    {
        static constexpr double onsetSeconds[] = { 0.00, 0.25, 0.50, 0.75, 1.00, 1.25, 1.50 };
        static constexpr double amplitudes[] = { 0.50, 0.35, 0.60, 0.25, 0.45, 0.55, 0.30 };
        static constexpr double partialsHz[] = { 110.0, 220.0, 330.0 };
        static constexpr double partialGains[] = { 1.0, 0.5, 0.25 };
        static constexpr double partialGainSum = 1.75;
        static constexpr double decayTauSeconds = 0.060;
        static constexpr double noiseFloorLinear = 1.0e-4; // -80 dBFS

        const auto totalSamples = buffer.getNumSamples();
        std::vector<float> mono (static_cast<size_t> (totalSamples), 0.0f);

        Lcg rng { 0x5ee0u };

        for (int i = 0; i < totalSamples; ++i)
        {
            const auto t = static_cast<double> (i) / sampleRate;
            auto value = noiseFloorLinear * static_cast<double> (rng.next());

            for (size_t note = 0; note < std::size (onsetSeconds); ++note)
            {
                const auto dt = t - onsetSeconds[note];

                if (dt < 0.0)
                    continue;

                auto partialSum = 0.0;

                for (size_t partial = 0; partial < std::size (partialsHz); ++partial)
                    partialSum += partialGains[partial]
                                   * std::cos (juce::MathConstants<double>::twoPi * partialsHz[partial] * dt);

                value += amplitudes[note] * std::exp (-dt / decayTauSeconds) * partialSum / partialGainSum;
            }

            mono[static_cast<size_t> (i)] = static_cast<float> (value);
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.copyFrom (channel, 0, mono.data(), totalSamples);
    }

    // Renders the fixed stimulus through `processor` and returns the output.
    // prepareToPlay() runs here (after the caller has applied whatever state/
    // preset it wants), because Lookahead is a structural parameter.
    inline juce::AudioBuffer<float> render (SilentiumAudioProcessor& processor)
    {
        juce::AudioBuffer<float> stimulus (2, numSamples);
        fillStimulus (stimulus);

        processor.prepareToPlay (sampleRate, blockSize);
        processor.reset();

        juce::AudioBuffer<float> output (2, numSamples);
        output.clear();

        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;

        for (int start = 0; start < numSamples; start += blockSize)
        {
            for (int channel = 0; channel < 2; ++channel)
                block.copyFrom (channel, 0, stimulus, channel, start, blockSize);

            processor.processBlock (block, midi);

            for (int channel = 0; channel < 2; ++channel)
                output.copyFrom (channel, start, block, channel, 0, blockSize);
        }

        return output;
    }

    //==========================================================================
    struct Fingerprint
    {
        juce::String sha256;
        std::vector<double> windowRmsDb;
        std::vector<double> windowPeakDb;
    };

    inline double toDb (double linear) noexcept
    {
        return linear > 1.0e-9 ? 20.0 * std::log10 (linear) : -180.0;
    }

    // Both channels of every render carry identical audio by construction
    // (identical input, and the gate applies one stereo-linked gain to every
    // channel), so channel 0 alone is the canonical fingerprint subject.
    inline Fingerprint fingerprintOf (const juce::AudioBuffer<float>& buffer)
    {
        Fingerprint fingerprint;

        const auto* data = buffer.getReadPointer (0);
        const auto totalSamples = buffer.getNumSamples();

        juce::SHA256 digest (data, sizeof (float) * static_cast<size_t> (totalSamples));
        fingerprint.sha256 = digest.toHexString();

        const auto windows = totalSamples / windowSamples;
        fingerprint.windowRmsDb.reserve (static_cast<size_t> (windows));
        fingerprint.windowPeakDb.reserve (static_cast<size_t> (windows));

        for (int window = 0; window < windows; ++window)
        {
            auto sumOfSquares = 0.0;
            auto peak = 0.0;

            for (int i = 0; i < windowSamples; ++i)
            {
                const auto value = static_cast<double> (data[window * windowSamples + i]);
                sumOfSquares += value * value;
                peak = std::max (peak, std::abs (value));
            }

            fingerprint.windowRmsDb.push_back (toDb (std::sqrt (sumOfSquares / static_cast<double> (windowSamples))));
            fingerprint.windowPeakDb.push_back (toDb (peak));
        }

        return fingerprint;
    }

    //==========================================================================
    // The legacy (v0.3.x) parameter set the state-blob golden was captured
    // with. Deliberately non-default in every legacy parameter that affects
    // the render, so a regression anywhere in the legacy signal path shows
    // up: a soft knee is engaged, the sidechain band is narrowed at both
    // ends, and the ramp times are away from their defaults.
    struct LegacySettings
    {
        float threshold = -35.0f;
        float attack = 0.5f;
        float hold = 30.0f;
        float release = 120.0f;
        float range = -70.0f;
        float lookahead = 5.0f;
        float scHighpass = 120.0f;
        float scLowpass = 8000.0f;
        float knee = 6.0f;
        bool duck = false;
        bool listen = false;
    };

    inline void setParameter (SilentiumAudioProcessor& processor, const char* id, float realValue)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (realValue));
    }

    inline void applyLegacySettings (SilentiumAudioProcessor& processor, const LegacySettings& settings = {})
    {
        setParameter (processor, ParamIDs::threshold, settings.threshold);
        setParameter (processor, ParamIDs::attack, settings.attack);
        setParameter (processor, ParamIDs::hold, settings.hold);
        setParameter (processor, ParamIDs::release, settings.release);
        setParameter (processor, ParamIDs::range, settings.range);
        setParameter (processor, ParamIDs::lookahead, settings.lookahead);
        setParameter (processor, ParamIDs::scHighpass, settings.scHighpass);
        setParameter (processor, ParamIDs::scLowpass, settings.scLowpass);
        setParameter (processor, ParamIDs::knee, settings.knee);

        if (auto* duck = processor.apvts.getParameter (ParamIDs::duck))
            duck->setValueNotifyingHost (settings.duck ? 1.0f : 0.0f);

        if (auto* listen = processor.apvts.getParameter (ParamIDs::listen))
            listen->setValueNotifyingHost (settings.listen ? 1.0f : 0.0f);
    }

    // Loads factory presets through a PresetManager that is pinned to an
    // empty, throwaway user-presets directory, so a factory preset render is
    // never influenced by whatever user presets happen to exist on the
    // machine running the render (PresetManager::loadPreset() resolves user
    // presets before factory ones - a user preset literally named "Default"
    // would otherwise silently change the golden). Mirrors
    // tests/PresetManagerTests.cpp's own isolation pattern.
    struct IsolatedFactoryPresetLoader
    {
        explicit IsolatedFactoryPresetLoader (SilentiumAudioProcessor& processorToUse)
            : directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("SilentiumGoldenRenders")
                             .getChildFile (juce::String (juce::Time::getHighResolutionTicks())
                                             + "_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)))),
              manager (processorToUse.apvts, makeConfig (directory), makeAssets())
        {
            directory.createDirectory();
        }

        ~IsolatedFactoryPresetLoader()
        {
            directory.deleteRecursively();
        }

        bool load (const juce::String& name) { return manager.loadPreset (name); }

        std::vector<juce::String> getAllPresetNames() const
        {
            std::vector<juce::String> names;

            for (const auto& entry : manager.getAllPresets())
                names.push_back (entry.name);

            return names;
        }

        JUCE_DECLARE_NON_COPYABLE (IsolatedFactoryPresetLoader)

    private:
        static basilica::presets::PresetManagerConfig makeConfig (const juce::File& userDirectory)
        {
            basilica::presets::PresetManagerConfig config;
            config.pluginId = "com.yvesvogl.silentium";
            config.pluginName = "Silentium";
            config.manufacturerName = "Yves Vogl";
            config.pluginVersion = "golden";
            config.userPresetsDirectoryOverrideForTests = userDirectory;
            return config;
        }

        static std::vector<basilica::presets::FactoryPresetAsset> makeAssets()
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

        juce::File directory;
        basilica::presets::PresetManager manager;
    };

    // The names of the factory presets whose voicing v0.4.0 deliberately does
    // NOT change (brief §4: chugLock/surgicalMute are intentionally re-voiced
    // and are therefore excluded from the untouched-preset golden set).
    inline const std::vector<juce::String>& untouchedFactoryPresetNames()
    {
        static const std::vector<juce::String> names {
            "Default",
            "Natural Decay",
            "Pick Attack Focus",
            "DI-Keyed Workflow",
            "Ambient Sustain",
            "Duck Under Lead",
            "Listen Check",
        };

        return names;
    }
}
