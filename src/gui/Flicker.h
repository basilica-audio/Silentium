#pragma once

#include <array>
#include <cmath>

#include <juce_core/juce_core.h>

// Suite-reusable irregular flicker multiplier: a sum of a few low-frequency,
// deliberately non-harmonic sine layers (no common integer ratio between
// their frequencies), so the result reads as an irregular, organic pilot
// -lamp/tube flicker rather than a smooth metronomic pulse. Originally
// implemented inline in AnalogMeter.cpp for the VU dials' incandescent glow
// (see that file's git history); pulled out here, unchanged, so
// PluginEditor.cpp's tube-vent glow (a different visual element - the amber
// glass tubes behind the side vent grilles, not the meter dial glow) can
// reuse the exact same technique with its own independent phase seed rather
// than re-deriving/duplicating the maths.
namespace basilica::gui
{
    struct FlickerLayer
    {
        float frequencyHz;
        float weight; // all weights across a call should sum to 1.0
    };

    // Suite-standard 3-layer table (matches AnalogMeter's dial-glow flicker
    // and this revision's tube-vent glow) - exposed as a named constant
    // rather than re-typed at each call site.
    inline constexpr std::array<FlickerLayer, 3> standardFlickerLayers {
        FlickerLayer { 0.63f, 0.5f },
        FlickerLayer { 1.13f, 0.3f },
        FlickerLayer { 0.29f, 0.2f },
    };

    // Returns 1.0 +/- amplitudeFraction, i.e. a multiplier to scale a base
    // alpha/brightness value by. `phaseSeed` should differ per visual
    // instance (e.g. 0.0f / 1.0f / 2.0f / 3.0f for four independently
    // flickering tubes) so that multiple instances sharing this function
    // never flicker in lockstep - each layer's phase is offset by
    // `phaseSeed * 3.7f + layerIndex * 2.1f` radians, the same irrational-
    // feeling constants AnalogMeter has used since v0.3.2.
    inline float flickerMultiplier (double nowSeconds, double startSeconds, float phaseSeed, float amplitudeFraction,
                                    const std::array<FlickerLayer, 3>& layers = standardFlickerLayers) noexcept
    {
        const auto t = (float) (nowSeconds - startSeconds);

        float sum = 0.0f;

        for (size_t i = 0; i < layers.size(); ++i)
        {
            const auto phase = phaseSeed * 3.7f + (float) i * 2.1f;
            sum += layers[i].weight * std::sin (juce::MathConstants<float>::twoPi * layers[i].frequencyHz * t + phase);
        }

        return 1.0f + amplitudeFraction * sum;
    }
}
