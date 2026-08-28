#include "PluginProcessor.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

// The factory-preset headroom gate (issue #50).
//
// Nothing in this repo used to check what a factory preset does to the LEVEL of
// the signal it is handed. tests/PresetManagerTests.cpp asserts that every
// factory preset parses, loads, and lands its parameters in range - not that the
// result stays inside full scale. A preset that clips is a preset the user has
// to fix before they can even audition it, which is a defect and not
// gain-staging taste.
//
// This file is the gate for that. It renders every shipped factory preset
// through the real AudioProcessor at a documented reference input level and
// asserts the output peak stays below 0 dBFS. A preset added later that clips
// this reference fails here.
//
// WHAT THIS DELIBERATELY DOES NOT DO is level-match the presets to each other.
// The gate is a ceiling, not a target: a preset sitting well below the line is
// left exactly where its author put it. Relative loudness between presets is a
// voicing/taste question and is not decided here.
namespace
{
    constexpr double referenceSampleRate = 48000.0;
    constexpr int referenceBlockSize = 512;

    // The suite-wide reference input level. -12 dBFS peak is the level a track
    // is conventionally recorded at, leaving 12 dB of headroom, and therefore
    // the level a factory preset's author must be assumed to have voiced for.
    constexpr float referencePeakDbfs = -12.0f;

    // The line this gate asserts: a factory preset must not push the reference
    // programme past full scale.
    constexpr double clippingCeiling = 1.0; // 0 dBFS, linear

    // What a shipped output trim would AIM at, 0.3 dB below the asserted line.
    // The gap is deliberate: it is the difference between "an upstream voicing
    // tweak moved the peak a little" and "this gate goes red". A trim is derived
    // as (measured overshoot + 0.3 dB), rounded up to the output parameter's
    // step - never tasted, and never applied to a preset that was already under
    // the line. No preset in this repo currently needs one.
    constexpr double headroomTargetDbfs = -0.3;

    // Ten factory presets ship - CMakeLists.txt's juce_add_binary_data
    // list and PluginProcessor.cpp's makeFactoryPresetAssets() must agree with
    // this. Asserted below so that "the preset library stopped loading" is
    // distinguishable from "every preset passed".
    constexpr int shippedFactoryPresetCount = 10;

    // The two ways a user actually arrives at a factory preset, which are NOT
    // equivalent and are both gated below.
    enum class Arrival
    {
        // Session load: the host restores state and THEN calls prepareToPlay(),
        // so every smoothed parameter and every DSP stage is primed at the
        // preset's own values before the first sample. This measures the preset
        // itself.
        sessionLoad,

        // Mid-session recall: the plugin is already prepared and running at its
        // parameter defaults when the user clicks the preset in the browser, so
        // every parameter JUMPS from where it was to where the preset puts it
        // while the DSP is still primed for the old values. This measures the
        // preset PLUS whatever the transition into it costs - which is the thing
        // a user actually hears in the first ten seconds. In
        // basilica-audio/Aureate this exact distinction found a 17.6 dB blast on
        // a preset click that the session-load path could not see at all, which
        // is why both are gated here.
        midSessionRecall
    };

    // Rendering the reference programme with NO preset loaded: the state a
    // mid-session recall departs FROM, i.e. the plugin's own parameter defaults.
    // The recall gate needs it, because a transition can only fairly be blamed
    // for clipping it introduced - see the gate below.
    constexpr const char* departureStateName = "the parameter defaults";

    //==========================================================================
    // The fleet reference programme signal.
    //
    // Four plucked notes, each a harmonic series with 1/n partial amplitudes, a
    // 3 ms pick attack and a 900 ms decay, with the upper partials decaying
    // faster than the fundamental as a real string does. Deterministic and
    // repeatable, which a recording could not be inside a test suite.
    //
    // The four fundamentals span E1 (41.203 Hz) to A5 (880.000 Hz), and twelve
    // harmonics per note carry content up to 10.6 kHz - so the signal exercises
    // the full band this plugin shapes rather than only the middle of it.
    //
    // The same reference is used for every plugin in the suite, deliberately.
    // What this gate measures is a PEAK against full scale, which no plugin's
    // domain makes materially easier or harder at a fixed input peak, and using
    // one signal across all thirteen is what makes their headroom numbers
    // comparable to each other at all. A domain-specific reference would buy
    // realism this particular assertion does not need, and cost that comparison.
    //
    // Peak-normalised to referencePeakDbfs after synthesis, so the level the
    // gate talks about is exact regardless of how the partials happen to add.
    juce::AudioBuffer<float> makeReferenceProgramme (double sampleRate,
                                                    double seconds = 2.0,
                                                    float peakDbfs = referencePeakDbfs)
    {
        const auto numSamples = static_cast<int> (seconds * sampleRate);
        juce::AudioBuffer<float> buffer (2, numSamples);
        buffer.clear();

        const double notes[] = { 41.203, 110.000, 293.665, 880.000 }; // E1, A2, D4, A5
        const auto noteCount = static_cast<int> (std::size (notes));
        const auto noteSamples = numSamples / noteCount;

        for (int noteIndex = 0; noteIndex < noteCount; ++noteIndex)
        {
            const auto fundamental = notes[noteIndex];
            const auto start = noteIndex * noteSamples;

            for (int sample = 0; sample < noteSamples; ++sample)
            {
                const auto t = static_cast<double> (sample) / sampleRate;
                const auto envelope = (1.0 - std::exp (-t / 0.003)) * std::exp (-t / 0.9);

                double value = 0.0;

                for (int harmonic = 1; harmonic <= 12; ++harmonic)
                {
                    const auto frequency = fundamental * static_cast<double> (harmonic);

                    if (frequency >= 0.45 * sampleRate)
                        break;

                    const auto harmonicDecay = std::exp (-t * static_cast<double> (harmonic) / 2.5);
                    value += (1.0 / static_cast<double> (harmonic)) * harmonicDecay
                                 * std::sin (juce::MathConstants<double>::twoPi * frequency * t);
                }

                const auto out = static_cast<float> (envelope * value);

                for (int channel = 0; channel < 2; ++channel)
                    buffer.setSample (channel, start + sample, out);
            }
        }

        const auto peak = buffer.getMagnitude (0, numSamples);

        if (peak > 0.0f)
            buffer.applyGain (juce::Decibels::decibelsToGain (peakDbfs) / peak);

        return buffer;
    }

    void renderThrough (juce::AudioProcessor& processor, juce::AudioBuffer<float>& buffer, int blockSize)
    {
        juce::MidiBuffer midi;

        for (int position = 0; position < buffer.getNumSamples(); position += blockSize)
        {
            const auto thisBlock = juce::jmin (blockSize, buffer.getNumSamples() - position);
            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), position, thisBlock);
            processor.processBlock (block, midi);
        }
    }

    // One preset's measured output peak, in dBFS, on the reference programme.
    // An empty `presetName` renders the departure state - the parameter
    // defaults, no preset loaded at all.
    double renderFactoryPresetPeakDb (const juce::String& presetName,
                                      const juce::AudioBuffer<float>& input,
                                      Arrival arrival)
    {
        SilentiumAudioProcessor processor;

        if (presetName.isNotEmpty() && arrival == Arrival::sessionLoad)
            REQUIRE (processor.presetManager.loadPreset (presetName));

        processor.setPlayConfigDetails (2, 2, referenceSampleRate, referenceBlockSize);
        processor.prepareToPlay (referenceSampleRate, referenceBlockSize);
        processor.reset();

        if (presetName.isNotEmpty() && arrival == Arrival::midSessionRecall)
            REQUIRE (processor.presetManager.loadPreset (presetName));

        juce::AudioBuffer<float> rendered (2, input.getNumSamples());
        rendered.makeCopyOf (input);
        renderThrough (processor, rendered, referenceBlockSize);

        REQUIRE (TestHelpers::allSamplesFinite (rendered));

        return juce::Decibels::gainToDecibels (
            static_cast<double> (rendered.getMagnitude (0, rendered.getNumSamples())));
    }
} // namespace

TEST_CASE ("Factory presets: none of them push the reference programme past 0 dBFS", "[presets][headroom]")
{
    const auto input = makeReferenceProgramme (referenceSampleRate);

    // The input really is at the level this gate claims it is.
    const auto inputPeakDb = juce::Decibels::gainToDecibels (
        static_cast<double> (input.getMagnitude (0, input.getNumSamples())));
    REQUIRE (std::abs (inputPeakDb - static_cast<double> (referencePeakDbfs)) < 0.01);

    // The state a mid-session recall departs from, measured once. See the
    // recall assertion below for what it is used for.
    const auto departurePeakDb = renderFactoryPresetPeakDb ({}, input, Arrival::sessionLoad);
    INFO ("departure state (" << departureStateName << ") peaks at " << departurePeakDb << " dBFS");

    SilentiumAudioProcessor probe;
    const auto presets = probe.presetManager.getAllPresets();

    auto factoryCount = 0;
    auto overFullScale = 0;
    auto transitionsThatMadeItWorse = 0;

    for (const auto& entry : presets)
    {
        if (! entry.isFactory)
            continue;

        ++factoryCount;
        INFO ("preset: " << entry.name);

        // 1. THE PRESET ITSELF, measured on the session-load path so that what
        //    is measured is the preset's own voicing rather than the ramp into
        //    it: the host restores state and THEN calls prepareToPlay(), which
        //    primes every smoothed stage at the preset's own values, so the
        //    first sample is already at the preset's staging.
        const auto sessionLoadPeakDb = renderFactoryPresetPeakDb (entry.name, input, Arrival::sessionLoad);

        {
            INFO ("session load: output peak " << sessionLoadPeakDb << " dBFS at a "
                  << referencePeakDbfs << " dBFS input peak");

            if (sessionLoadPeakDb >= 0.0)
                ++overFullScale;

            CHECK (juce::Decibels::decibelsToGain (sessionLoadPeakDb) < clippingCeiling);
        }

        // 2. THE TRANSITION INTO IT, measured on the recall path - the plugin
        //    already running at its defaults when the preset is clicked.
        //
        //    The bound here is deliberately NOT a flat 0 dBFS. A recall is a
        //    move between two states, and it can only fairly be blamed for
        //    clipping it INTRODUCED: if the state the user was already in was
        //    itself over the line, a transient during the crossfade is that
        //    state's headroom problem, not this preset's. So the line is "below
        //    full scale, OR below where you already were" - which needs no
        //    tolerance constant, and which tightens automatically the moment the
        //    departure state's own headroom is fixed.
        //
        //    What it catches is the case where the transition is louder than
        //    BOTH of its endpoints, which is not a crossfade but a
        //    discontinuity - a DSP stage whose internal state is inconsistent
        //    with its new parameters. That is exactly the 17.6 dB defect this
        //    measurement found in basilica-audio/Aureate's Iron stage.
        {
            const auto recallPeakDb = renderFactoryPresetPeakDb (entry.name, input, Arrival::midSessionRecall);
            const auto recallCeilingDb = juce::jmax (0.0, departurePeakDb);

            INFO ("mid-session recall: output peak " << recallPeakDb << " dBFS, ceiling "
                  << recallCeilingDb << " dBFS (the greater of full scale and the departure state)");

            if (recallPeakDb >= recallCeilingDb)
                ++transitionsThatMadeItWorse;

            CHECK (recallPeakDb < recallCeilingDb);
        }
    }

    // If this collapses, the preset library stopped being loaded rather than
    // every preset passing - the failure mode this gate must not have.
    INFO ("factory presets exercised: " << factoryCount
          << ", of which at or over 0 dBFS on session load: " << overFullScale
          << ", of which worse than their own departure state on recall: "
          << transitionsThatMadeItWorse);
    CHECK (factoryCount == shippedFactoryPresetCount);
    CHECK (overFullScale == 0);
    CHECK (transitionsThatMadeItWorse == 0);
}

// Hidden reporting case: `./Tests "[.headroom-table]"` prints the measured peak
// of every factory preset on both arrival paths, which is how a trim is derived
// and how the next one would be.
TEST_CASE ("Factory preset headroom table", "[.headroom-table]")
{
    const auto input = makeReferenceProgramme (referenceSampleRate);

    SilentiumAudioProcessor probe;

    WARN ("departure state (" << departureStateName << ") | peak "
          << renderFactoryPresetPeakDb ({}, input, Arrival::sessionLoad) << " dBFS");

    for (const auto& entry : probe.presetManager.getAllPresets())
    {
        if (! entry.isFactory)
            continue;

        const auto loadDb = renderFactoryPresetPeakDb (entry.name, input, Arrival::sessionLoad);
        const auto recallDb = renderFactoryPresetPeakDb (entry.name, input, Arrival::midSessionRecall);

        WARN (entry.name << " | peak " << loadDb << " dBFS | recall " << recallDb
              << " dBFS | trim to hit " << headroomTargetDbfs << " dBFS: "
              << (headroomTargetDbfs - loadDb) << " dB");
    }
}
