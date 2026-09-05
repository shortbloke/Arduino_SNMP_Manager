#include "fixtures.h"
#include "registry.h"

void registerTrackingTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"Tracking", name, run}); };
    add("repeated registrations dispatch to the callback associated with each request",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            uint32_t value = 0;
            Request request;
            request.setUDP(&udp);
            for (int cycle = 1; cycle <= 12; ++cycle)
            {
                auto *callback = manager.addTimestampHandler(udp.peer, oid, &value);
                request.addOIDPointer(callback);
                request.setRequestID(cycle);
                CHECK(request.sendTo(udp.peer));
                request.clearOIDList();
                udp.incoming = message(binding({0x43, 1, static_cast<unsigned char>(cycle)}), 1,
                                       "public", 0xa2, cycle);
                manager.loop();
                CHECK(value == static_cast<uint32_t>(cycle));
                CHECK(callback->updateCount() == 1);
            }
        });
    add("duplicate OID registrations keep concurrent destinations and freshness separate",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int one = 0, two = 0;
            auto *a = manager.addIntegerHandler(udp.peer, oid, &one);
            auto *b = manager.addIntegerHandler(udp.peer, oid, &two);
            Request first, second;
            first.setUDP(&udp);
            second.setUDP(&udp);
            first.addOIDPointer(a);
            second.addOIDPointer(b);
            first.setRequestID(1);
            second.setRequestID(2);
            CHECK(first.sendTo(udp.peer) && second.sendTo(udp.peer));
            udp.incoming = message(binding({2, 1, 42}), 1, "public", 0xa2, 2);
            manager.loop();
            CHECK(one == 0 && two == 42 && a->updateCount() == 0 && b->updateCount() == 1);
            udp.incoming = message(binding({2, 1, 7}), 1, "public", 0xa2, 1);
            manager.loop();
            CHECK(one == 7 && a->updateCount() == 1);
            udp.incoming = message(binding({2, 1, 9}), 1, "public", 0xa2, 2);
            manager.loop();
            CHECK(two == 42 && b->updateCount() == 1);
            second.setRequestID(3);
            CHECK(second.sendTo(udp.peer));
            udp.incoming = message(binding({2, 1, 42}), 1, "public", 0xa2, 3);
            manager.loop();
            CHECK(two == 42 && b->updateCount() == 2); // Unchanged values are still fresh.
        });
    add("rejected and error bindings never mark a destination fresh",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            char text[4] = "old";
            char *ptr = text;
            auto *callback = manager.addStringHandler(udp.peer, oid, &ptr, sizeof(text));
            Request request;
            request.setUDP(&udp);
            request.addOIDPointer(callback);
            const std::vector<Bytes> values = {
                tlv(4, {'l', 'o', 'n', 'g'}), {0x81, 0}, {2, 1, 9}, tlv(4, {'o', 'l', 'd'})};
            for (size_t i = 0; i < values.size(); ++i)
            {
                request.setRequestID(i + 1);
                CHECK(request.sendTo(udp.peer));
                udp.incoming =
                    message(binding(values[i]), 1, "public", 0xa2, i + 1, i == 3 ? 2 : 0);
                manager.loop();
                CHECK(callback->updateCount() == 0 && std::string(text) == "old");
            }
        });
    add("request cancellation releases pending slots for its callbacks",
        []
        {
            Manager manager;
            UDP udp;
            int32_t value = 0;
            Request request;
            request.setUDP(&udp);
            auto *callback = manager.addIntegerHandler(udp.peer, oid, &value);
            CHECK(request.addOIDPointer(callback));
            for (int id = 1; id <= SNMP_MAX_PENDING_REQUESTS; ++id)
            {
                request.setRequestID(id);
                CHECK(request.sendTo(udp.peer));
            }
            request.setRequestID(100);
            CHECK(!request.sendTo(udp.peer));
            request.cancelPendingRequests();
            CHECK(!callback->requestPending);
            CHECK(request.sendTo(udp.peer));
        });
    add("legacy request summary cannot disable response matching",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int32_t value = 99;
            auto *callback = manager.addIntegerHandler(udp.peer, oid, &value);
            Request request;
            request.setUDP(&udp);
            CHECK(request.addOIDPointer(callback));
            CHECK(request.sendTo(udp.peer));
            callback->requestTracked = false;
            udp.incoming = message(binding({2, 1, 42}), 1, "public", 0xa2, 8);
            manager.loop();
            CHECK(value == 99);
        });
    add("response with matching outstanding request ID updates value",
        []
        {
            Manager m;
            UDP u;
            m.setUDP(&u);
            int n = 99;
            Request r;
            r.setUDP(&u);
            r.addOIDPointer(m.addIntegerHandler(u.peer, oid, &n));
            CHECK(r.sendTo(u.peer));
            u.incoming = message(binding({2, 1, 42}), 1, "public", 0xa2, 7);
            m.loop();
            CHECK(n == 42);
        });
    add("request tracking is independent per callback and requires a complete send",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int first = 99, second = 99;
            IPAddress otherPeer(192, 0, 2, 2);
            auto *a = manager.addIntegerHandler(udp.peer, oid, &first);
            auto *b = manager.addIntegerHandler(otherPeer, oid, &second);
            Request one, two;
            one.setUDP(&udp);
            two.setUDP(&udp);
            one.addOIDPointer(a);
            two.addOIDPointer(b);
            udp.beginPacketResult = 0;
            CHECK(!one.sendTo(udp.peer) && !a->requestTracked);
            udp.beginPacketResult = 1;
            udp.writeLimit = 1;
            CHECK(!one.sendTo(udp.peer) && !a->requestTracked);
            udp.writeLimit = static_cast<size_t>(-1);
            CHECK(one.sendTo(udp.peer));
            two.setRequestID(8);
            CHECK(two.sendTo(otherPeer));
            udp.incoming = message(binding({2, 1, 42}));
            manager.loop();
            CHECK(first == 42 && second == 99 && b->requestPending);
            udp.peer = otherPeer;
            udp.incoming = message(binding({2, 1, 7}), 1, "public", 0xa2, 8);
            manager.loop();
            CHECK(first == 42 && second == 7 && !b->requestPending);
        });
    add("request tracking handles concurrent failed and duplicate replies",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int value = 99;
            auto *callback = manager.addIntegerHandler(udp.peer, oid, &value);
            Request request;
            request.setUDP(&udp);
            request.addOIDPointer(callback);
            CHECK(request.sendTo(udp.peer)); // 7
            request.setRequestID(8);
            CHECK(request.sendTo(udp.peer));
            request.setRequestID(9);
            udp.endResult = 0;
            CHECK(!request.sendTo(udp.peer));
            auto reply = [&](int id, int contents)
            {
                udp.incoming = message(binding({2, 1, static_cast<unsigned char>(contents)}), 1,
                                       "public", 0xa2, id);
                manager.loop();
            };
            reply(7, 1);
            CHECK(value == 1);
            reply(9, 2);
            CHECK(value == 1);
            reply(8, 42);
            CHECK(value == 42);
            reply(8, 3);
            CHECK(value == 42);
            udp.endResult = 1;
            request.setRequestID(10);
            CHECK(request.sendTo(udp.peer));
            udp.incoming = message({}, 1, "public", 0xa2, 10, 1, 0);
            manager.loop();
            CHECK(!callback->requestPending);
            reply(10, 4);
            CHECK(value == 42);
        });
    add("response must match outstanding request ID",
        []
        {
            Manager m;
            UDP u;
            m.setUDP(&u);
            int n = 99;
            Request r;
            r.setUDP(&u);
            r.addOIDPointer(m.addIntegerHandler(u.peer, oid, &n));
            CHECK(r.sendTo(u.peer));
            u.incoming = message(binding({2, 1, 42}), 1, "public", 0xa2, 8);
            m.loop();
            CHECK(n == 99);
        });
}
