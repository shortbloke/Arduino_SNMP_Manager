#include "registry.h"
#include "isolation.h"
#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 1)
    {
        std::cerr << "Usage: " << argv[0] << " (runs all test groups)\n";
        return 2;
    }
    int failed = 0;
    const auto tests = allTests();
    for (const auto &test : tests)
    {
        const auto result = runIsolated(test.run);
        std::cout << (result.passed ? "PASS " : "FAIL ") << test.group << ": " << test.name << '\n';
        if (!result.passed)
        {
            ++failed;
            std::cerr << result.diagnostics;
        }
    }
    std::cout << tests.size() << " tests, " << failed << " failures\n";
    return failed || tests.empty() ? 1 : 0;
}
