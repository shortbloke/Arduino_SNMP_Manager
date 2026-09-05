#include "fixtures.h"

// Failure injection affects only the library's nothrow allocations.
static int allocationsBeforeFailure = -1;
static size_t largestAvailableBlock = static_cast<size_t>(-1);
static unsigned failedAllocations = 0;
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    if (allocationsBeforeFailure == 0 || size > largestAvailableBlock)
    {
        ++failedAllocations;
        return nullptr;
    }
    if (allocationsBeforeFailure > 0)
        --allocationsBeforeFailure;
    return ::operator new(size);
}
FailAllocations::FailAllocations(int count, size_t largestBlock)
{
    allocationsBeforeFailure = count;
    largestAvailableBlock = largestBlock;
    failedAllocations = 0;
}
FailAllocations::~FailAllocations()
{
    allocationsBeforeFailure = -1;
    largestAvailableBlock = static_cast<size_t>(-1);
}
unsigned FailAllocations::failures() const
{
    return failedAllocations;
}
