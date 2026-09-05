#include "fixtures.h"
#include "registry.h"

void registerRequestsTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"Requests", name, run}); };
    add("request missing transport",
        []
        {
            Request r;
            CHECK(!r.sendTo(IPAddress()));
        });
    add("request golden wire and ports",
        []
        {
            for (int version : {0, 1})
            {
                Manager m;
                int value = 0;
                UDP udp;
                Request r(version);
                r.setUDP(&udp);
                r.addOIDPointer(m.addIntegerHandler(udp.peer, oid, &value));
                CHECK(r.sendTo(udp.peer));
                CHECK(udp.outgoing == message(binding({5, 0}), version, "public", 0xa0));
                CHECK(udp.destination == udp.peer);
                CHECK(udp.destinationPort == 161);
                r.setPort(1161);
                udp.endResult = 0;
                CHECK(!r.sendTo(udp.peer));
                CHECK(udp.destinationPort == 1161);
            }
        });
    add("request list ordering and clearing",
        []
        {
            Manager m;
            int v = 0;
            auto *a = m.addIntegerHandler(IPAddress(), oid, &v);
            auto *b = m.addIntegerHandler(IPAddress(), ".1.3.6.1.2.1.1.3.0", &v);
            Request r;
            r.addOIDPointer(a);
            r.addOIDPointer(b);
            CHECK(r.callbacks->value == a && r.callbacks->next->value == b);
            r.clearOIDList();
            CHECK(r.callbacks->value == nullptr);
            CHECK(std::string(a->OID) == oid);
            r.addOIDPointer(b);
            CHECK(r.callbacks->value == b);
        });
    add("successful Get preserves OID order on wire",
        []
        {
            Manager m;
            UDP u;
            int n = 0;
            Request r;
            r.setUDP(&u);
            r.addOIDPointer(m.addIntegerHandler(u.peer, oid, &n));
            r.addOIDPointer(m.addIntegerHandler(u.peer, ".1.3.6.1.2.1.1.3.0", &n));
            Bytes second = oidWire;
            second[8] = 3;
            CHECK(r.sendTo(u.peer));
            CHECK(u.outgoing == message(join({binding({5, 0}), tlv(0x30, join({second, {5, 0}}))}),
                                        1, "public", 0xa0));
        });
    add("full-width request ID and high UDP port are preserved",
        []
        {
            Manager manager;
            UDP udp;
            int32_t value = 0;
            Request request;
            request.setUDP(&udp);
            request.addOIDPointer(manager.addIntegerHandler(udp.peer, oid, &value));
            request.setRequestID(INT32_MAX);
            request.setPort(65535);
            CHECK(request.sendTo(udp.peer));
            CHECK(udp.destinationPort == 65535);
            SNMPGetResponse decoded;
            CHECK(decoded.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
            CHECK(decoded.requestID == static_cast<unsigned long>(INT32_MAX));
            CHECK(sizeof(manager) < SNMP_PACKET_LENGTH + 512);
        });
}
