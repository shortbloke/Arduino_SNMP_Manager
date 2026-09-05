#include "registry.h"

std::vector<Test> allTests()
{
    std::vector<Test> tests;
    registerBerTests(tests);
    registerRequestsTests(tests);
    registerResponsesTests(tests);
    registerManagerTests(tests);
    registerTrackingTests(tests);
    registerOwnershipTests(tests);
    return tests;
}
