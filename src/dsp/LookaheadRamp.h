#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdint>
#include <vector>

// Smooth Open (v0.4.0 F1): turns the gate's per-sample gain trajectory into a
// continuous, C1 opening ramp that fits exactly inside the existing lookahead
// window - so a 0 ms attack genuinely opens without a step, and the marquee
// "instant attack" claim stops being a click generator.
//
// TOPOLOGY (research-gate-expander.md §2.6 - Signalsmith's moving-max plus
// cascaded box filters)
//
//     w[n] = box2( box1( movmax_{N_m}( g[n] ) ) )
//
// with, for a lookahead of N samples, N_m = N/2 and two cascaded boxes of
// N_b = N/2 each. Everything is in dB, and the result REPLACES the gain
// rather than being combined with it (see "why not a parallel max" below).
//
// WHY IT ADDS NO LATENCY
// The smoother runs on the *control* signal, in parallel in time with the
// N-sample audio delay the lookahead already applies. A backward moving-max
// delays falls, not rises: a rising edge at t0 passes movmax instantly, and
// the two-box cascade of total length K*N_b = N spreads that edge linearly
// over [t0, t0+N] - so the gate is exactly fully open at the moment the
// delayed transient leaves the delay line. Reported latency is unchanged.
//
// The maximum slope of the resulting triangular-kernel ramp is 2M/N dB per
// sample for a step of M dB, which is what the click-freedom test asserts
// against.
//
// The N_m window acts on the CLOSING side only: it holds the open target for
// up to N/2 samples after the signal falls away, i.e. an implicit pre-hold of
// at most half the lookahead time. That is intentional and documented in the
// manual.
//
// WHY NOT A PARALLEL MAX
// An earlier design applied max(g[n], w[n]). That reintroduces exactly the
// click this exists to remove: with attack = 0 the ballistic gain steps by
// the whole Range in a single sample, so at the opening edge the raw step
// wins the max while w[] has barely started ramping. In series the step is
// absorbed: attack = 0 yields the pure triangular ramp, and a slow attack
// (already smoother than the kernel) passes through essentially unchanged.
//
// Real-time safety: every buffer is sized once in prepare() for the maximum
// lookahead; nothing here allocates, locks, or calls a transcendental on the
// audio thread. process() is O(1) amortised (the moving max uses a monotonic
// deque, the boxes are running sums).
class LookaheadRamp
{
public:
    // `maxLookaheadSamples` is the largest window that will ever be
    // requested. Allocates once; setWindowSamples() below is then free to
    // move within that bound on the audio thread.
    void prepare (int maxLookaheadSamples)
    {
        capacity = std::max (1, maxLookaheadSamples);
        ringCapacity = capacity + 1;

        dequeValues.assign (static_cast<size_t> (ringCapacity), 0.0f);
        dequeIndices.assign (static_cast<size_t> (ringCapacity), 0);
        boxAHistory.assign (static_cast<size_t> (capacity), 0.0f);
        boxBHistory.assign (static_cast<size_t> (capacity), 0.0f);

        movingMaxLength = 0;
        boxLength = 0;

        reset (0.0f);
    }

    // Sets the window from the current lookahead, in samples. Cheap and
    // audio-thread-safe; re-priming only happens when the length actually
    // changes, and re-primes to the last value seen so the change itself
    // cannot produce a step.
    void setWindowSamples (int lookaheadSamples)
    {
        const auto clamped = juce::jlimit (0, capacity, lookaheadSamples);
        const auto newMovingMaxLength = clamped / 2;
        const auto newBoxLength = clamped / 2;

        // The fully-open-by-exit invariant: the two cascaded boxes must
        // finish inside the lookahead window, or the gate would still be
        // opening when the delayed transient has already left the delay line.
        jassert (2 * newBoxLength <= clamped);

        if (newMovingMaxLength == movingMaxLength && newBoxLength == boxLength)
            return;

        movingMaxLength = newMovingMaxLength;
        boxLength = newBoxLength;

        reset (lastValue);
    }

    // Restores the smoother to a settled state holding `value`, so the next
    // process() call returns `value` exactly.
    void reset (float value)
    {
        lastValue = value;

        dequeHead = 0;
        dequeTail = 0;
        dequeCount = 0;
        sampleIndex = 0;

        boxAIndex = 0;
        boxBIndex = 0;

        std::fill (boxAHistory.begin(), boxAHistory.end(), value);
        std::fill (boxBHistory.begin(), boxBHistory.end(), value);

        boxASum = static_cast<double> (value) * static_cast<double> (std::max (1, boxLength));
        boxBSum = boxASum;

        if (movingMaxLength > 1)
        {
            dequeValues[0] = value;
            dequeIndices[0] = 0;
            dequeTail = 1;
            dequeCount = 1;
            sampleIndex = 1;
        }
    }

    // One control-rate (per-sample) step. `value` is the ballistic gain in
    // dB; the return value is the smoothed gain in dB.
    float process (float value) noexcept
    {
        lastValue = value;

        if (movingMaxLength <= 1 && boxLength <= 0)
            return value;

        const auto maxed = movingMaxLength > 1 ? pushMovingMax (value) : value;

        if (boxLength <= 0)
            return maxed;

        const auto stage1 = pushBox (boxAHistory, boxAIndex, boxASum, maxed);
        return pushBox (boxBHistory, boxBIndex, boxBSum, stage1);
    }

    int getWindowSamples() const noexcept { return movingMaxLength + 2 * boxLength; }

private:
    // Monotonic-deque moving maximum over the last `movingMaxLength` samples.
    // Amortised O(1): every sample is pushed once and popped at most once.
    float pushMovingMax (float value) noexcept
    {
        while (dequeCount > 0)
        {
            const auto back = (dequeTail - 1 + ringCapacity) % ringCapacity;

            if (dequeValues[static_cast<size_t> (back)] > value)
                break;

            dequeTail = back;
            --dequeCount;
        }

        dequeValues[static_cast<size_t> (dequeTail)] = value;
        dequeIndices[static_cast<size_t> (dequeTail)] = sampleIndex;
        dequeTail = (dequeTail + 1) % ringCapacity;
        ++dequeCount;

        while (dequeCount > 0
               && dequeIndices[static_cast<size_t> (dequeHead)] <= sampleIndex - static_cast<std::int64_t> (movingMaxLength))
        {
            dequeHead = (dequeHead + 1) % ringCapacity;
            --dequeCount;
        }

        ++sampleIndex;

        return dequeValues[static_cast<size_t> (dequeHead)];
    }

    float pushBox (std::vector<float>& history, int& index, double& sum, float value) noexcept
    {
        sum -= static_cast<double> (history[static_cast<size_t> (index)]);
        history[static_cast<size_t> (index)] = value;
        sum += static_cast<double> (value);

        index = index + 1 >= boxLength ? 0 : index + 1;

        return static_cast<float> (sum / static_cast<double> (boxLength));
    }

    int capacity = 1;
    int ringCapacity = 2;

    int movingMaxLength = 0;
    int boxLength = 0;

    std::vector<float> dequeValues;
    std::vector<std::int64_t> dequeIndices;
    int dequeHead = 0;
    int dequeTail = 0;
    int dequeCount = 0;
    std::int64_t sampleIndex = 0;

    std::vector<float> boxAHistory;
    std::vector<float> boxBHistory;
    int boxAIndex = 0;
    int boxBIndex = 0;
    double boxASum = 0.0;
    double boxBSum = 0.0;

    float lastValue = 0.0f;
};
