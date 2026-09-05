#include "registry.h"

std::vector<Test> allTests()
{
    std::vector<Test> tests;
    registerExampleTests(tests);
    registerClientTests(tests);
    registerAgentTests(tests);
    registerMIBTests(tests);
    registerHeapTests(tests);
    registerBerTests(tests);
    registerRequestsTests(tests);
    registerResponsesTests(tests);
    registerManagerTests(tests);
    registerTrackingTests(tests);
    registerOwnershipTests(tests);
    registerConfigurationTests(tests);
    return tests;
}
