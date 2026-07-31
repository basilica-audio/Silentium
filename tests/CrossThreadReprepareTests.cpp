#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_events/juce_events.h>

#include <atomic>
#include <thread>

// Regression coverage for the cross-thread bug class fixed in sibling plugins
// Nave (basilica-audio/nave#28) and Triptych: message-thread-oriented host-
// notification state reachable from two unsynchronised entry points - the
// host's own prepareToPlay()-calling thread (not guaranteed to be JUCE's
// message thread) and JUCE's real message thread.
//
// SILENTIUM'S SHAPE IS DIFFERENT FROM NAVE'S AND TRIPTYCH'S, ON PURPOSE.
// Both of those use juce::AsyncUpdater to report a live Lookahead-driven
// latency change to the host. Silentium's own class-level comment on
// timerCallback() (PluginProcessor.h) explains why it does NOT: v0.4.0's live
// Lookahead reconfiguration (F6) needs to publish a new applied delay from
// processBlock() every block it changes, and AsyncUpdater::
// triggerAsyncUpdate() posts to the MessageManager's queue - which takes a
// CriticalSection and can grow (allocate) its backing array. Neither is
// permitted on the audio thread (tests/RobustnessTests.cpp's allocation
// guard asserts exactly that). So Silentium instead has GateEngine publish
// its applied delay as a plain atomic (GateEngine::latencySamples, already
// atomic - see GateEngine.h), and SilentiumAudioProcessor polls it from a
// juce::Timer (timerCallback(), every latencyPollIntervalMs = 25 ms) instead
// of an AsyncUpdater.
//
// THAT CHOICE CLOSES THE AUDIO-THREAD-ALLOCATION HAZARD. IT DOES NOT CLOSE
// THE CROSS-THREAD HAZARD THE NAVE/TRIPTYCH BUG CLASS IS ACTUALLY ABOUT.
// juce::Timer::timerCallback() carries the exact same guarantee
// AsyncUpdater::handleAsyncUpdate() does - always the real JUCE message
// thread - so swapping the notification mechanism does not change the
// threading shape of the bug at all:
//
//   - timerCallback() (PluginProcessor.cpp) - always the real JUCE message
//     thread (juce::Timer's own contract) - reads engine.getLatencySamples()
//     (GateEngine's own atomic - fine on its own) and, if it differs from
//     AudioProcessor::getLatencySamples(), calls AudioProcessor::
//     setLatencySamples().
//   - prepareToPlay() (PluginProcessor.cpp) - called by the host on WHATEVER
//     thread it chooses; the VST3/AU contract guarantees only that this is
//     not the audio thread, NOT that it is JUCE's own message thread - ALSO
//     calls AudioProcessor::getLatencySamples()/setLatencySamples() directly,
//     unconditionally, every time.
//
// Both AudioProcessor::getLatencySamples() and ::setLatencySamples() touch a
// plain, non-atomic int inside juce::AudioProcessor itself (JUCE 8.0.14
// source: ~/.cache/CPM/juce/*/modules/juce_audio_processors_headless/
// processors/juce_AudioProcessor.h declares "int blockSize = 0,
// latencySamples = 0;" as a plain member; juce_AudioProcessor.cpp's
// setLatencySamples() does a bare "if (latencySamples != newLatency) {
// latencySamples = newLatency; ... }" with no lock/atomic anywhere in the
// class). Calling it concurrently from two unsynchronised threads is a
// genuine data race on JUCE's OWN internal state, structurally identical to
// the Nave/Triptych bug even though the notification mechanism (Timer, not
// AsyncUpdater) and the failure mode (a silently corrupted reported-latency
// int, not a crash) both differ.
//
// AUDITED AND RULED OUT AS A SECOND ("RACE B"-SHAPED) HAZARD: unlike
// Triptych, SilentiumAudioProcessor itself has no plain (non-atomic) member
// analogous to Triptych's appliedLookaheadSamples/preparedSampleRate that
// prepareToPlay() writes and processBlock() also reads/writes - every piece
// of processor-level state processBlock() touches (the meter atomics, the
// APVTS raw-value atomics) is already std::atomic. GateEngine's own internal
// prepare()-vs-process() state (sampleRate, numChannels, the delay-crossfade
// bookkeeping, etc.) IS plain, non-atomic memory written by prepare() (called
// from prepareToPlay(), a host-chosen thread) and read by process() (the
// audio thread) - but that is the same prepare()/process() relationship every
// engine in this suite uses (TriptychEngine, CabConvolutionEngine, etc.) and
// is out of this task's chartered scope (the Nave/Triptych bug class is
// specifically about message-thread-oriented host-notification state with
// TWO entry points, not the ordinary single-entry prepare-then-process
// lifecycle every plugin already relies on hosts serialising). Not touched
// here.
//
// THE FIX (src/PluginProcessor.h/.cpp): a std::mutex (latencyReportMutex) now
// guards the AudioProcessor::getLatencySamples()/setLatencySamples() call in
// BOTH prepareToPlay() and timerCallback(), serialising the two entry points
// structurally - exactly as Nave's messageThreadMutex and Triptych's
// asyncHandshakeMutex do for their own equivalents. The mutex is never taken
// by processBlock() (or anything it calls), so no lock/allocation is added
// to the audio thread; the real-time guarantee timerCallback()'s own class
// comment describes is unchanged.
//
// RED-VERIFICATION. Reproduced with a from-scratch ThreadSanitizer build (no
// prior TSan setup existed in this repo's CMake configuration;
// -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"
// -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" at configure time needed no
// CMakeLists edit): against the pre-fix code, TSan reported the identical
// data-race pair on AudioProcessor::setLatencySamples()/latencySamples
// between prepareToPlay() and timerCallback() that Triptych's own equivalent
// test hit against its prepareToPlay()/handleAsyncUpdate() pair - confirmed
// live and reproducible, not merely theoretical. Against the fixed code, the
// same TSan build ran clean. A plain (non-instrumented) Debug build's stress
// run of this test did not itself crash or produce non-finite output either
// before or after the fix (expected - a corrupted reported-latency int has no
// necessarily-visible symptom outside an instrumented build, same as
// Triptych), so this test's REQUIRE_FALSE(sawNonFiniteOutput) assertion is a
// trip-wire against a worse failure mode, and the mutex fix plus TSan
// evidence are the actual defect-and-fix record - matching this suite's own
// precedent (Nave#28) that the goal is "structurally impossible via
// mutex/ordering," not "empirically rare."
//
// THIS TEST reproduces the concurrent-entry scenario directly: a host thread
// repeatedly reprepares at 44.1k/96k/192k across a small and a large block
// size and processes audio (simulating the host's own prepareToPlay()-
// calling thread, which need not be the message thread), while an automation
// thread drives ParamIDs::lookahead (the live-reconfiguration-relevant
// parameter this bug class targets) via setValueNotifyingHost() from a third
// thread (simulating audio-thread-delivered host automation, which
// processBlock() picks up every block through applyParametersToEngine() and
// GateEngine's own live-lookahead crossfade path), while this test's own
// calling thread - which becomes "the real JUCE message thread" the instant
// it touches MessageManager - pumps runDispatchLoopUntil(1) in a loop so
// timerCallback() actually fires concurrently with the host thread's work,
// exactly as JUCE's own Timer implementation is serviced by the message loop.
// Failures on the two worker threads are recorded into std::atomic<bool>
// flags (REQUIRE() is not thread-safe off the test's own thread) and
// asserted after join().
TEST_CASE ("Concurrent prepareToPlay and automation-driven live Lookahead reprepare survive 44.1k/96k/192k", "[processor][threading]")
{
    SilentiumAudioProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto* lookaheadParam = processor.apvts.getParameter (ParamIDs::lookahead);
    auto* scSlopeParam = processor.apvts.getParameter (ParamIDs::scSlope);

    REQUIRE (lookaheadParam != nullptr);
    REQUIRE (scSlopeParam != nullptr);

    std::atomic<bool> stop { false };
    std::atomic<bool> sawNonFiniteOutput { false };

    // Simulates host automation delivered from a non-message thread (real
    // DAWs typically deliver automation from the audio thread). Each
    // setValueNotifyingHost() call is picked up by the next processBlock()'s
    // applyParametersToEngine(), which drives GateEngine's live-lookahead
    // crossfade path (GateEngine::setLookaheadMs()/beginDelayCrossfadeTo()) -
    // the audio-thread side of exactly the hand-off timerCallback() then has
    // to publish to the host from the message thread.
    std::thread automationThread ([&]
    {
        int i = 0;

        while (! stop.load (std::memory_order_relaxed))
        {
            // Sweeps the full Lookahead range (0-20 ms per ParameterLayout.cpp)
            // so both up- and down-moves exercise the delay crossfade.
            const auto lookaheadMsValue = static_cast<float> (i % 21);
            const auto scSlopeChoice = static_cast<float> (i % 2);

            lookaheadParam->setValueNotifyingHost (lookaheadParam->convertTo0to1 (lookaheadMsValue));
            scSlopeParam->setValueNotifyingHost (scSlopeParam->convertTo0to1 (scSlopeChoice));

            ++i;
            std::this_thread::yield();
        }
    });

    // Simulates the host's own prepareToPlay()-calling thread, which per the
    // VST3/AU specs is not guaranteed to be JUCE's message thread: reprepares
    // across 44.1k/96k/192k with both a small and a large block size and
    // processes blocks. Catch2's assertion machinery is not meant to be
    // driven from a non-test thread, so failures are recorded into a plain
    // atomic instead of calling REQUIRE() directly.
    std::thread hostThread ([&]
    {
        for (int iteration = 0; iteration < 25; ++iteration)
        {
            for (double sampleRate : { 44100.0, 96000.0, 192000.0 })
            {
                for (int blockSize : { 64, 2048 })
                {
                    processor.prepareToPlay (sampleRate, blockSize);

                    juce::AudioBuffer<float> buffer (2, blockSize);
                    juce::MidiBuffer midi;

                    for (int block = 0; block < 2; ++block)
                    {
                        TestHelpers::fillWithSine (buffer, sampleRate, 220.0);
                        processor.processBlock (buffer, midi);

                        if (! TestHelpers::allSamplesFinite (buffer))
                            sawNonFiniteOutput.store (true, std::memory_order_relaxed);
                    }
                }
            }
        }

        stop.store (true, std::memory_order_relaxed);
    });

    // This test's own calling thread IS "the message thread" (whichever
    // thread first touches MessageManager::getInstance() becomes it, and
    // JUCE asserts if runDispatchLoopUntil() is called from any other
    // thread). Pumping it here is what lets
    // SilentiumAudioProcessor::timerCallback() actually fire, concurrently
    // with the host thread above - exactly as it would in a real host where
    // the JUCE message thread runs independently of whichever thread the
    // host calls prepareToPlay() from.
    while (! stop.load (std::memory_order_relaxed))
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);

    automationThread.join();
    hostThread.join();

    REQUIRE_FALSE (sawNonFiniteOutput.load());
}
