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
//
// Both are volatile, and that is load-bearing rather than cargo cult. Clang
// and GCC model a call to operator new as touching no memory the caller can
// see, so in an optimised build they hoist the counter into a register across
// the call and the guard reads back the zero its constructor stored - however
// many times the replacement below actually ran. That is not a hypothetical:
// it is what made this guard report zero allocations for everything, including
// its own deliberate-allocation self-test, in the Release build CI runs while
// the Debug build developers run locally passed. A volatile access has to go
// to memory, which is exactly the guarantee the counter needs.
namespace AllocationGuardDetail
{
    inline thread_local std::size_t volatile allocationCount = 0;
    inline thread_local bool volatile counting = false;
}

// Replacing the global operators is deliberate and affects the whole test
// binary; with `counting` false (its default everywhere outside a guard) the
// added cost is one thread-local branch per allocation.
//
// The definitions live in AllocationGuard.cpp, NOT here. A replacement
// operator new/delete may not be declared inline ([basic.stc.dynamic]/4 -
// ill-formed, no diagnostic required), and defining them in a header included
// by several translation units is exactly that. It happens to work in an
// unoptimised build; in a Release build the compiler is free to treat a call
// it can see through as the implicit library operator instead, which silently
// turns every "processBlock allocates nothing" assertion into a check on a
// counter nothing increments. One out-of-line definition in one TU is what
// the standard actually asks for, and it is what makes the guard's self-test
// below meaningful.
void* operator new (std::size_t size);
void* operator new[] (std::size_t size);
void operator delete (void* pointer) noexcept;
void operator delete[] (void* pointer) noexcept;
void operator delete (void* pointer, std::size_t) noexcept;
void operator delete[] (void* pointer, std::size_t) noexcept;

// Sink for the guard's own self-test. Storing the allocation through a
// volatile pointer at namespace scope stops the optimiser from eliding the
// new/delete pair outright, which C++14 [expr.new]/10 explicitly permits it
// to do and which Release builds actually do - the self-test then "passes"
// zero allocations and proves nothing.
namespace AllocationGuardDetail
{
    inline void* volatile allocationSink = nullptr;
}

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
