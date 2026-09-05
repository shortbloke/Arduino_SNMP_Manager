#include "fixtures.h"
#include "registry.h"
#include <cmath>
namespace wifi_example
{
#include "../../../examples/ESP32_ESP8266_SNMP_Manager/Polling.h"
}
namespace ethernet_example
{
#include "../../../examples/Arduino_Ethernet_SNMP_Manager/Polling.h"
}
namespace multi_example
{
#include "../../../examples/ESP_Multiple_SNMP_Device_Polling/Polling.h"
}
template <class Rate, class Poll> void checkExampleHelpers()
{
    Rate rate;
    double percent = -1;
    CHECK(!rate.sample(100, 100, 8000, percent));
    CHECK(rate.sample(600, 150, 8000, percent));
    CHECK(std::abs(percent - 100.0) < 0.00001); // Fractional seconds.
    CHECK(!rate.sample(700, 150, 8000, percent));
    CHECK(rate.sample(1100, 200, 8000, percent));
    CHECK(std::abs(percent - 100.0) < 0.00001);
    CHECK(rate.sample(1100, 250, 8000, percent) && percent == 0);
    CHECK(!rate.sample(1200, 300, 0, percent));
    CHECK(!rate.sample(0, 1, 8000, percent)); // Reboot establishes a baseline.
    rate.reset();
    CHECK(!rate.sample(UINT32_MAX - 4, 100, 800, percent));
    CHECK(rate.sample(5, 200, 800, percent));
    CHECK(std::abs(percent - 10.0) < 0.00001);
    rate.reset();
    CHECK(!rate.sample(0, 100, 800000000, percent));
    CHECK(rate.sample(100000000, 200, 800000000, percent));
    CHECK(std::abs(percent - 100.0) < 0.00001); // No integer intermediate overflow.
    CHECK(!rate.sample(0, 0, 8000, percent));   // TimeTicks wrap resets.

    Manager manager;
    UDP udp;
    manager.setUDP(&udp);
    int a = 0, b = 0;
    Request request;
    request.setUDP(&udp);
    Poll poll;
    CHECK(!poll.begin(0));
    CHECK(!poll.add(nullptr, request));
    auto *first = manager.addIntegerHandler(udp.peer, oid, &a);
    CHECK(poll.add(first, request));
    CHECK(poll.add(manager.addIntegerHandler(udp.peer, ".1.3.6.1.2.1.1.2.0", &b), request));
    Bytes secondOid = oidWire;
    secondOid[8] = 2;
    const Bytes both = join({binding({2, 1, 42}), tlv(0x30, join({secondOid, {2, 1, 9}}))});
    for (int cycle = 1; cycle <= 12; ++cycle)
    {
        request.setRequestID(cycle);
        CHECK(poll.begin(UINT32_MAX - 99));
        CHECK(!poll.begin(0));
        CHECK(!poll.expired(99, 200) && poll.expired(100, 200));
        CHECK(request.sendTo(udp.peer));
        udp.incoming = message(cycle == 1 ? binding({2, 1, 42}) : both, 1, "public", 0xa2, cycle);
        manager.loop();
        CHECK(poll.complete() == (cycle != 1));
        poll.finish();
        CHECK(!poll.active() && !poll.complete());
        const uint32_t updates = first->updateCount();
        udp.incoming = message(both, 1, "public", 0xa2, cycle);
        manager.loop(); // Late/duplicate packets must not update the destination.
        CHECK(first->updateCount() == updates);
    }
    CHECK(a == 42 && b == 9);
}

void registerExampleTests(std::vector<Test> &tests)
{
    tests.push_back(
        {"Examples", "wifi_example polling and rates", []
         {
             checkExampleHelpers<wifi_example::Counter32Rate, wifi_example::PollState<2>>();
             CHECK(wifi_example::nextRequestID(32767) == 1);
         }});
    tests.push_back(
        {"Examples", "ethernet_example polling and rates", []
         {
             checkExampleHelpers<ethernet_example::Counter32Rate, ethernet_example::PollState<2>>();
             CHECK(ethernet_example::nextRequestID(32767) == 1);
         }});
    tests.push_back(
        {"Examples", "multi_example polling and rates", []
         {
             checkExampleHelpers<multi_example::Counter32Rate, multi_example::PollState<2>>();
             CHECK(multi_example::nextRequestID(32767) == 1);
         }});
}
