#include "fixtures.h"

// Failure injection affects only the library's nothrow allocations.
static int allocationsBeforeFailure = -1;
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    if (allocationsBeforeFailure == 0)
        return nullptr;
    if (allocationsBeforeFailure > 0)
        --allocationsBeforeFailure;
    return ::operator new(size);
}
FailAllocations::FailAllocations(int count)
{
    allocationsBeforeFailure = count;
}
FailAllocations::~FailAllocations()
{
    allocationsBeforeFailure = -1;
}
