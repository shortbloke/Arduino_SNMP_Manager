#include "registry.h"
#include "isolation.h"
#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 1 && !(argc == 3 && std::string(argv[1]) == "--group"))
    {
        std::cerr << "Usage: " << argv[0] << " [--group NAME]\n";
        return 2;
    }
    int failed = 0, count = 0;
    const auto tests = allTests();
    for (const auto &test : tests)
    {
        if (argc == 3 && std::string(test.group) != argv[2])
            continue;
        ++count;
        const auto result = runIsolated(test.run);
        std::cout << (result.passed ? "PASS " : "FAIL ") << test.group << ": " << test.name << '\n';
        if (!result.passed)
        {
            ++failed;
            std::cerr << result.diagnostics;
        }
    }
    std::cout << count << " tests, " << failed << " failures\n";
    return failed || count == 0 ? 1 : 0;
}
