#include "fixtures.h"
#include "registry.h"

namespace
{
// Construct a valid response of an exact size without using the library encoder.
// Small repeated bindings keep this independent of the octet-string capacity.
Bytes responseOfSize(size_t size)
{
    Bytes prefix;
    for (size_t count = 0; count < size; ++count)
    {
        for (size_t length = 1; length <= 32; ++length)
        {
            Bytes wire = message(join({prefix, binding(tlv(4, Bytes(length, 'x')))}));
            if (wire.size() == size)
                return wire;
        }
        prefix = join({prefix, binding({4, 0})});
        if (message(prefix).size() > size)
            break;
    }
    throw std::runtime_error("Could not construct exact-size response fixture");
}
}

void registerConfigurationTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"Configuration", name, run}); };
    add("receive accepts packet limit and rejects the next byte before reading",
        []
        {
            UDP udp;
            Manager manager;
            manager.setUDP(&udp);
            char text[33] = "unchanged";
            char *destination = text;
            CHECK(manager.addStringHandler(udp.peer, oid, &destination, sizeof(text)));
            udp.incoming = responseOfSize(SNMP_PACKET_LENGTH);
            CHECK(manager.loop());
            CHECK(udp.reads == 1 && text[0] == 'x');
            strcpy(text, "unchanged");
            udp.incoming = responseOfSize(SNMP_PACKET_LENGTH + 1);
            CHECK(manager.loop());
            CHECK(udp.reads == 1 && udp.flushes == 2);
            CHECK(std::string(text) == "unchanged");
        });
    add("send rejects an oversized request before starting a UDP packet",
        []
        {
            UDP udp;
            Manager manager;
            int32_t value = 0;
            auto *callback = manager.addIntegerHandler(udp.peer, oid, &value);
            CHECK(callback);
            Request request;
            request.setUDP(&udp);
            Bytes bindings;
            bool rejected = false;
            for (size_t count = 0; count < SNMP_PACKET_LENGTH; ++count)
            {
                CHECK(request.addOIDPointer(callback));
                bindings = join({bindings, binding({5, 0})});
                const Bytes expected = message(bindings, 1, "public", 0xa0);
                const int packets = udp.packets;
                if (expected.size() <= SNMP_PACKET_LENGTH)
                {
                    CHECK(request.sendTo(udp.peer));
                    CHECK(udp.outgoing == expected);
                }
                else
                {
                    CHECK(!request.sendTo(udp.peer));
                    CHECK(udp.packets == packets);
                    CHECK(request.packet == nullptr);
                    rejected = true;
                    break;
                }
            }
            CHECK(rejected);
        });
    add("octet and opaque limits preserve their distinct terminator requirements",
        []
        {
            Bytes payload(SNMP_OCTETSTRING_MAX_LENGTH - 1, 'a');
            Bytes wire = tlv(4, payload);
            OctetType text;
            CHECK(text.fromBuffer(wire.data(), wire.size()));
            CHECK(encode(text) == wire);
            payload.push_back('a');
            wire = tlv(4, payload);
            CHECK(!text.fromBuffer(wire.data(), wire.size()));
            std::string oversized(payload.begin(), payload.end());
            OctetType encoded(const_cast<char *>(oversized.c_str()));
            CHECK(encoded.serialise(nullptr) < 0);
            RawType opaque;
            wire = tlv(OPAQUE, payload);
            CHECK(opaque.fromBuffer(wire.data(), wire.size()));
            CHECK(encode(opaque) == wire);
            payload.push_back('a');
            wire = tlv(OPAQUE, payload);
            CHECK(!opaque.fromBuffer(wire.data(), wire.size()));
        });
    add("OID text accepts capacity minus terminator and rejects the next character",
        []
        {
            std::string name = ".1.3";
            Bytes arcs{43};
            while (name.size() + 2 < MAX_OID_LENGTH)
            {
                name += ".1";
                arcs.push_back(1);
            }
            if (name.size() == MAX_OID_LENGTH - 2)
            {
                name += '0';
                arcs.back() = 10;
            }
            CHECK(name.size() == MAX_OID_LENGTH - 1);
            OIDType encoded(const_cast<char *>(name.c_str()));
            Bytes wire = tlv(6, arcs);
            CHECK(encode(encoded) == wire);
            OIDType decoded;
            CHECK(decoded.fromBuffer(wire.data(), wire.size()));
            CHECK(std::string(decoded._value) == name);
            name += '0';
            arcs.back() *= 10;
            wire = tlv(6, arcs);
            CHECK(!decoded.fromBuffer(wire.data(), wire.size()));
            OIDType oversized(const_cast<char *>(name.c_str()));
            CHECK(oversized.serialise(nullptr) < 0);
        });
    add("pending request capacity rejects excess sends and can be cleared",
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
            for (int i = 0; i < SNMP_MAX_PENDING_REQUESTS; ++i)
            {
                request.setRequestID(i + 1);
                CHECK(request.sendTo(udp.peer));
            }
            int packets = udp.packets;
            request.setRequestID(100);
            CHECK(!request.sendTo(udp.peer) && udp.packets == packets);
            udp.incoming = message(binding({2, 1, 42}), 1, "public", 0xa2, 1);
            manager.loop();
            CHECK(value == 42);
            CHECK(request.sendTo(udp.peer));
            callback->clearPendingRequests();
            CHECK(!callback->requestPending);
            udp.incoming = message(binding({2, 1, 7}), 1, "public", 0xa2, 100);
            manager.loop();
            CHECK(value == 42);
            CHECK(request.sendTo(udp.peer));
        });
}
