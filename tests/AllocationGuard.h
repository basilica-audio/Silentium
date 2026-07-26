#pragma once

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

// Counts heap allocations made by the calling thread while a guard is in
// scope, so a test can assert the hard real-time rule the whole engine is
// written to: processBlock() must never allocate.
//
// This is the only way to check that claim mechanically. Reading the code and
// concluding "there is no `new` here" misses everything that allocates
// indirectly - a juce::Array growing, a ref-counted Coefficients object being
// created by the wrong factory overload (which is a real bug this codebase has
// shipped and fixed before), a std::function, a message being posted to the
// MessageManager.
//
// The counter is thread_local, so allocations made by other threads (the
// message thread, JUCE's own timer thread) cannot make a guarded region look
// dirty - only the thread standing in for the audio thread is measured.
namespace AllocationGuardDetail
{
    inline thread_local std::size_t allocationCount = 0;
    inline thread_local bool counting = false;
}

// Replacing the global operators is deliberate and affects the whole test
// binary; with `counting` false (its default everywhere outside a guard) the
// added cost is one thread-local branch per allocation.
inline void* operator new (std::size_t size)
{
    if (AllocationGuardDetail::counting)
        ++AllocationGuardDetail::allocationCount;

    if (auto* pointer = std::malloc (size == 0 ? 1 : size))
        return pointer;

    throw std::bad_alloc();
}

inline void* operator new[] (std::size_t size)
{
    return operator new (size);
}

inline void operator delete (void* pointer) noexcept { std::free (pointer); }
inline void operator delete[] (void* pointer) noexcept { std::free (pointer); }
inline void operator delete (void* pointer, std::size_t) noexcept { std::free (pointer); }
inline void operator delete[] (void* pointer, std::size_t) noexcept { std::free (pointer); }

// RAII: counts allocations on this thread for as long as it is alive.
//
//   {
//       ScopedAllocationGuard guard;
//       processor.processBlock (buffer, midi);
//       CHECK (guard.count() == 0);
//   }
struct ScopedAllocationGuard
{
    ScopedAllocationGuard()
    {
        AllocationGuardDetail::allocationCount = 0;
        AllocationGuardDetail::counting = true;
    }

    ~ScopedAllocationGuard()
    {
        AllocationGuardDetail::counting = false;
    }

    std::size_t count() const noexcept { return AllocationGuardDetail::allocationCount; }

    ScopedAllocationGuard (const ScopedAllocationGuard&) = delete;
    ScopedAllocationGuard& operator= (const ScopedAllocationGuard&) = delete;
};
