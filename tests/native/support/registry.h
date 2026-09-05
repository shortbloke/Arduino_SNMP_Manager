#pragma once
#include <functional>
#include <vector>
struct Test
{
    const char *group;
    const char *name;
    std::function<void()> run;
};
std::vector<Test> allTests();
void registerBerTests(std::vector<Test> &tests);
void registerRequestsTests(std::vector<Test> &tests);
void registerResponsesTests(std::vector<Test> &tests);
void registerManagerTests(std::vector<Test> &tests);
void registerTrackingTests(std::vector<Test> &tests);
void registerOwnershipTests(std::vector<Test> &tests);

void registerConfigurationTests(std::vector<Test> &tests);

void registerClientTests(std::vector<Test> &tests);

void registerAgentTests(std::vector<Test> &tests);
