#pragma once
#include <functional>
#include <string>
struct TestResult
{
    bool passed;
    std::string diagnostics;
};
TestResult runIsolated(const std::function<void()> &run);
