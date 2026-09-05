#include "registry.h"
#include <iostream>
#include <stdexcept>
#include <unistd.h>

// Execute ownership and parser-reuse cases in this process. Returning from main
// allows local/global destruction and the host's exit-time leak checker to run.
int main()
{
    alarm(30);
    std::vector<Test> tests;
    registerOwnershipTests(tests);
    registerResponsesTests(tests);
    for (const auto &test : tests)
    {
        try
        {
            test.run();
            std::cout << "PASS " << test.group << ": " << test.name << '\n';
        }
        catch (const std::exception &error)
        {
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
            return 1;
        }
    }
    alarm(0);
    return tests.empty() ? 1 : 0;
}
