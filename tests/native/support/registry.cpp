#include "registry.h"

std::vector<Test> allTests()
{
    std::vector<Test> tests;
    registerClientTests(tests);
    registerBerTests(tests);
    registerRequestsTests(tests);
    registerResponsesTests(tests);
    registerManagerTests(tests);
    registerTrackingTests(tests);
    registerOwnershipTests(tests);
    registerConfigurationTests(tests);
    return tests;
}
