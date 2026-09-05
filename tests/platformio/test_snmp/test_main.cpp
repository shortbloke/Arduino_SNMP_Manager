#include <gtest/gtest.h>
#include "registry.h"
#include "isolation.h"
#include <cctype>

class IsolatedCase : public testing::Test
{
    std::function<void()> run;

public:
    explicit IsolatedCase(std::function<void()> body) : run(body) {}
    void TestBody() override
    {
        const auto result = runIsolated(run);
        ASSERT_TRUE(result.passed) << result.diagnostics;
    }
};

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    for (const auto &test : allTests())
    {
        std::string name = test.name;
        for (char &c : name)
            if (!std::isalnum(static_cast<unsigned char>(c)))
                c = '_';
        testing::RegisterTest(test.group, name.c_str(), nullptr, nullptr, __FILE__, __LINE__,
                              [test]() -> IsolatedCase * { return new IsolatedCase(test.run); });
    }
    // PlatformIO obtains test failure status from GoogleTest output.
    const int result = RUN_ALL_TESTS();
    (void)result;
    std::cout.flush();
    std::cerr.flush();
    return 0;
}
