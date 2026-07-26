#include "ParameterLayout.h"
#include "ParameterIds.h"

namespace
{
    // True logarithmic (base-10) mapping for frequency parameters, so slider/
    // knob travel spends equal space per octave rather than per Hz. Uses
    // juce::mapToLog10/mapFromLog10 rather than NormalisableRange's built-in
    // power-law skew, which only approximates a log curve.
    juce::NormalisableRange<float> makeLogFrequencyRange (float minHz, float maxHz)
    {
        return juce::NormalisableRange<float> (
            minHz,
            maxHz,
            [] (float rangeStart, float rangeEnd, float normalised)
            { return juce::mapToLog10 (normalised, rangeStart, rangeEnd); },
            [] (float rangeStart, float rangeEnd, float value)
            { return juce::mapFromLog10 (value, rangeStart, rangeEnd); });
    }

    // Same idea for time parameters (Attack/Release) that span a wide,
    // perceptually-logarithmic range: equal slider travel per decade, not per
    // millisecond. Not used for Hold/Lookahead, whose range must include 0.
    juce::NormalisableRange<float> makeLogTimeRange (float minMs, float maxMs)
    {
        return juce::NormalisableRange<float> (
            minMs,
            maxMs,
            [] (float rangeStart, float rangeEnd, float normalised)
            { return juce::mapToLog10 (normalised, rangeStart, rangeEnd); },
            [] (float rangeStart, float rangeEnd, float value)
            { return juce::mapFromLog10 (value, rangeStart, rangeEnd); });
    }
}

namespace slnt
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        //======================================================================
        // Threshold: open threshold on the (sidechain-filtered) envelope.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::threshold, 1 },
            "Threshold",
            juce::NormalisableRange<float> (-80.0f, 0.0f, 0.01f),
            -40.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Attack: ramp time from closed (Range) to open (0 dB). v0.2.0 lowers
        // the floor from 0.1 ms to 0 ms (docs/design-brief.md's Attack
        // section - Nail The Mix: "0.1ms to 1ms, sometimes even 0ms if your
        // gate allows lookahead", and Silentium has lookahead). A true
        // log10 mapping (makeLogTimeRange) cannot include 0 (log(0) is
        // undefined), so this uses a skewed NormalisableRange instead: skew
        // 0.25 biases slider/automation resolution toward the low end,
        // approximating the old log curve's usable feel while still
        // including the new 0 ms floor exactly.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::attack, 1 },
            "Attack",
            juce::NormalisableRange<float> (0.0f, 50.0f, 0.01f, 0.25f),
            1.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        //======================================================================
        // Hold: minimum time the gate stays open, retriggered while the
        // envelope stays above the close threshold. Range must include 0, so
        // this uses a plain linear range rather than makeLogTimeRange.
        // v0.2.0 lowers the ceiling from 500 ms to 250 ms (docs/design-
        // brief.md's Hold section: Nail The Mix cites 10-50 ms as the
        // practical band and FabFilter Pro-G's own ceiling is 250 ms; v1's
        // 500 ms had no source). A v0.1.0 state with Hold > 250 ms (only
        // possible via a hand-edited/future state, since v0.1.0's own
        // ceiling was 500 ms) clamps to 250 ms on load via JUCE's own
        // NormalisableRange::convertTo0to1 clamping - see
        // tests/DesignBriefTests.cpp's Hold range test.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::hold, 1 },
            "Hold",
            juce::NormalisableRange<float> (0.0f, 250.0f, 0.1f),
            20.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        //======================================================================
        // Release: ramp time from open (0 dB) back down to Range.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::release, 1 },
            "Release",
            makeLogTimeRange (5.0f, 500.0f),
            80.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        //======================================================================
        // Range: floor attenuation applied when closed. 0 dB == always open
        // (a useful reference/bypass-like setting, see tests/EngineTests.cpp).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::range, 1 },
            "Range",
            juce::NormalisableRange<float> (-80.0f, 0.0f, 0.01f),
            -60.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Lookahead: delays the main signal so the gate can start opening
        // just before a transient arrives. Reported as the plugin's total
        // latency. Range must include 0 (lookahead disabled), so this uses a
        // plain linear range rather than makeLogTimeRange.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::lookahead, 1 },
            "Lookahead",
            juce::NormalisableRange<float> (0.0f, 20.0f, 0.01f),
            5.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        //======================================================================
        // SC HPF: sidechain-only high-pass, keeps hum/rumble from falsely
        // holding the gate open. Never applied to the main signal.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::scHighpass, 1 },
            "SC HPF",
            makeLogFrequencyRange (20.0f, 500.0f),
            80.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        //======================================================================
        // SC LPF (v0.2.0): sidechain-only low-pass, in series after SC HPF,
        // so the detection path can be narrowed toward the documented
        // 2-5 kHz pick-attack transient band instead of only having its
        // bottom end rejected. Defaults fully open (16 kHz) so a v0.1.0
        // session that never touches it reproduces v0.1.0 behaviour exactly
        // - see tests/DesignBriefTests.cpp's SC LPF null test.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::scLowpass, 1 },
            "SC LPF",
            makeLogFrequencyRange (1000.0f, 16000.0f),
            16000.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        //======================================================================
        // Knee: soft-knee width around Threshold. 0 dB = the original hard
        // gate/duck transition; wider values blend the gain computer's
        // target smoothly across the band instead of snapping at Threshold.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::knee, 1 },
            "Knee",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Duck: inverts the gain computer into a ducker (attenuate above
        // Threshold instead of opening above it). Off by default so v0.1
        // sessions/presets keep gating behaviour unchanged.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::duck, 1 },
            "Duck",
            false));

        //======================================================================
        // Listen: auditions the sidechain-filtered detection signal in place
        // of the gated output. Off by default.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::listen, 1 },
            "Listen",
            false));

        //======================================================================
        // v0.4.0 parameters. Every default below is the exact-neutral value,
        // i.e. the one that reproduces v0.3.x behaviour: a session or preset
        // saved before v0.4.0 carries none of these keys, APVTS therefore
        // leaves them at their defaults, and the render is unchanged (see
        // tests/StateTests.cpp's cross-version golden test).
        //======================================================================

        //======================================================================
        // Ratio: downward-expander law between Threshold and Range. The top
        // of the range is the default and is displayed as an infinite ratio,
        // because that is what a gate is: below Threshold the signal is
        // pushed all the way to the Range floor. Reducing Ratio turns the
        // gate into an expander, which is much gentler on decaying tails.
        //
        // Skew 0.4 spends most of the knob's travel on 1:1-5:1, where the
        // musically interesting settings are; the sparse top end is a single
        // detent-like destination (the gate).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::ratio, 1 },
            "Ratio",
            juce::NormalisableRange<float> (1.0f, ParamConstants::maxRatio, 0.01f, 0.4f),
            ParamConstants::maxRatio,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int)
                {
                    if (value >= ParamConstants::maxRatio - ParamConstants::ratioGateEpsilon)
                        return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e : 1 (Gate)"));

                    return juce::String (value, 2) + " : 1";
                })
                .withValueFromStringFunction ([] (const juce::String& text)
                {
                    // Accepts "4.00 : 1", "4:1", plain "4", and the infinity
                    // glyph the display function produces.
                    if (text.containsIgnoreCase ("gate")
                        || text.contains (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")))
                        || text.containsIgnoreCase ("inf"))
                        return ParamConstants::maxRatio;

                    return text.upToFirstOccurrenceOf (":", false, false).trim().getFloatValue();
                })));

        //======================================================================
        // Hysteresis: the open/close threshold gap, fixed at 3 dB internally
        // before v0.4.0 - hence the default. 0 dB deliberately allows the two
        // thresholds to coincide (the textbook chatter case) for users who
        // want the tightest possible close on very clean material.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::hysteresis, 1 },
            "Hysteresis",
            juce::NormalisableRange<float> (0.0f, 12.0f, 0.1f),
            3.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Detector: Peak is the v0.3.x ballistics follower (index 0, the
        // default); RMS is a fixed 5 ms mean-square window.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::detector, 1 },
            "Detector",
            juce::StringArray { "Peak", "RMS" },
            0));

        //======================================================================
        // SC Slope: order of both sidechain filters. 12 dB/oct (index 0, the
        // default) is the v0.3.x single-biquad pair.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::scSlope, 1 },
            "SC Slope",
            juce::StringArray { "12 dB/oct", "24 dB/oct" },
            0));

        //======================================================================
        // Smooth Open: continuous opening ramp inside the lookahead window.
        // Off by default (v0.3.x behaviour); adds no latency when on.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::smoothOpen, 1 },
            "Smooth Open",
            false));

        //======================================================================
        // Release Shape: Exponential (index 0, the default) is the v0.3.x
        // program-dependent approach; Linear is a constant-dB/s fade.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::releaseShape, 1 },
            "Release Shape",
            juce::StringArray { "Exponential", "Linear" },
            0));

        return layout;
    }
}
