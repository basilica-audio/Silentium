#include "AllocationGuard.h"

// The single out-of-line definition of the replacement operators declared in
// AllocationGuard.h. See the rationale there: these must not be inline, and
// there must be exactly one definition of each in the program.
//
// Only the Tests target compiles this file. The shipped plugin never links it,
// and neither does tests/tools/GoldenRenderHarness.cpp (its own target, and
// excluded from the Tests sources by CMakeLists.txt).

void* operator new (std::size_t size)
{
    // Spelt out rather than `++`: a compound increment of a volatile object is
    // deprecated in C++20 (-Wdeprecated-volatile).
    if (AllocationGuardDetail::counting)
        AllocationGuardDetail::allocationCount = AllocationGuardDetail::allocationCount + 1;

    if (auto* pointer = std::malloc (size == 0 ? 1 : size))
        return pointer;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    return operator new (size);
}

void operator delete (void* pointer) noexcept { std::free (pointer); }
void operator delete[] (void* pointer) noexcept { std::free (pointer); }
void operator delete (void* pointer, std::size_t) noexcept { std::free (pointer); }
void operator delete[] (void* pointer, std::size_t) noexcept { std::free (pointer); }
