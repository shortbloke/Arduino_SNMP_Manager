#include "fixtures.h"
#include "registry.h"

void registerManagerTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"Manager", name, run}); };
    add("bounded string binary and OID callbacks preserve destinations",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            char text[4] = "old";
            char *ptr = text;
            manager.addStringHandler(udp.peer, oid, &ptr, sizeof(text));
            udp.incoming = message(binding(tlv(4, {'l', 'o', 'n', 'g'})));
            manager.loop();
            CHECK(std::string(text) == "old");
            udp.incoming = message(binding(tlv(4, {'x', 0, 'y'})));
            manager.loop();
            CHECK(std::string(text) == "old");
            udp.incoming = message(binding(tlv(4, {'n', 'e', 'w'})));
            manager.loop();
            CHECK(std::string(text) == "new");
            for (int tag : {4, 0x44})
            {
                Manager binary;
                binary.setUDP(&udp);
                unsigned char output[3] = {9, 9, 9};
                size_t length = 99;
                if (tag == 4)
                    binary.addOctetHandler(udp.peer, oid, output, sizeof(output), &length);
                else
                    binary.addOpaqueHandler(udp.peer, oid, output, sizeof(output), &length);
                udp.incoming = message(binding(tlv(tag, {'a', 0, 'b'})));
                binary.loop();
                CHECK(length == 3 && output[0] == 'a' && output[1] == 0 && output[2] == 'b');
                udp.incoming = message(binding(tlv(tag, {1, 2, 3, 4})));
                binary.loop();
                CHECK(length == 3 && output[0] == 'a');
            }
            Manager oidManager;
            oidManager.setUDP(&udp);
            char result[7] = "old";
            auto *callback = oidManager.addOIDHandler(udp.peer, oid, result, sizeof(result));
            CHECK(std::string(callback->OID) == oid);
            udp.incoming = message(binding({6, 2, 43, 6}));
            oidManager.loop();
            CHECK(std::string(result) == ".1.3.6");
            udp.incoming = message(binding(oidWire));
            oidManager.loop();
            CHECK(std::string(result) == ".1.3.6");
        });
    add("default manager starts without transport",
        []
        {
            SNMPManager manager;
            CHECK(manager._udp == nullptr);
            CHECK(std::string(manager._community) == "public");
            CHECK(!manager.begin() && !manager.loop());
            UDP udp;
            manager.setUDP(&udp);
            CHECK(udp.stops == 0 && manager.begin());
        });
    add("callback identity requires IP and OID",
        []
        {
            Manager m;
            int v = 0;
            IPAddress ip(1, 2, 3, 4);
            CHECK(!m.findCallback(ip, oid));
            auto *a = m.addIntegerHandler(ip, oid, &v);
            CHECK(m.findCallback(ip, oid) == a);
            CHECK(!m.findCallback(IPAddress(), oid));
            CHECK(!m.findCallback(ip, ".1.3.6.1.2.1.1.2.0"));
        });
    add("library convention: UDP lifecycle and integer dispatch",
        []
        {
            Manager m;
            UDP a, b;
            int v = 0;
            CHECK(!m.begin());
            CHECK(!m.loop());
            m.setUDP(&a);
            CHECK(a.listenPort == 162);
            m.setUDP(&b);
            CHECK(a.stops == 1);
            m.addIntegerHandler(b.peer, oid, &v);
            CHECK(m.loop());
            CHECK(b.reads == 0);
            b.incoming = message(binding({2, 1, 42}));
            CHECK(m.loop());
            CHECK(v == 42);
            CHECK(b.reads == 1 && b.flushes == 1);
        });
    add("manager unsigned typed dispatch",
        []
        {
            for (int tag : {0x41, 0x42, 0x43, 0x46})
            {
                Manager m;
                UDP u;
                m.setUDP(&u);
                uint32_t n = 0;
                uint64_t big = 0;
                if (tag == 0x41)
                    m.addCounter32Handler(u.peer, oid, &n);
                if (tag == 0x42)
                    m.addGaugeHandler(u.peer, oid, &n);
                if (tag == 0x43)
                    m.addTimestampHandler(u.peer, oid, &n);
                if (tag == 0x46)
                    m.addCounter64Handler(u.peer, oid, &big);
                u.incoming = message(binding(tlv(tag, {0, 255, 255, 255, 255})));
                m.loop();
                CHECK(tag == 0x46 ? big == UINT32_MAX : n == UINT32_MAX);
            }
        });
    add("community, peer and callback type rejection",
        []
        {
            for (int scenario = 0; scenario < 3; ++scenario)
            {
                Manager m;
                UDP u;
                m.setUDP(&u);
                int value = 99;
                m.addIntegerHandler(u.peer, oid, &value);
                Bytes val = scenario == 2 ? Bytes{4, 1, 'x'} : Bytes{2, 1, 42};
                u.incoming = message(binding(val), 1, scenario == 0 ? "private" : "public");
                if (scenario == 1)
                    u.peer = IPAddress();
                m.loop();
                CHECK(value == 99);
            }
        });
    // RFC 3416 section 4.2.1: exceptions are values, not malformed packets.
    add("manager rejects unsupported version",
        []
        {
            for (int version : {2, 3, 127})
            {
                Manager manager;
                UDP udp;
                manager.setUDP(&udp);
                int value = 99;
                manager.addIntegerHandler(udp.peer, oid, &value);
                udp.incoming = message(binding({2, 1, 42}), version);
                manager.loop();
                CHECK(value == 99);
                // Rejection must leave the manager able to process v1 and v2c.
                for (int supported : {0, 1})
                {
                    value = 99;
                    udp.incoming = message(binding({2, 1, 42}), supported);
                    manager.loop();
                    CHECK(value == 42);
                }
            }
        });
    add("library convention: float callback preserves fractional tenths",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            float value = 99;
            manager.addFloatHandler(udp.peer, oid, &value);
            for (unsigned char raw : {0, 1, 10, 123})
            {
                udp.incoming = message(binding({2, 1, raw}));
                manager.loop();
                CHECK(std::abs(value - static_cast<float>(raw) / 10.0f) < 0.001f);
            }
            udp.incoming = message(binding({2, 1, 0x85})); // -123 integer tenths
            manager.loop();
            CHECK(std::abs(value + 12.3f) < 0.001f);
        });
    add("manager receives 484-byte response",
        []
        {
            Manager m;
            UDP u;
            m.setUDP(&u);
            char storage[512]{};
            char *ptr = storage;
            m.addStringHandler(u.peer, oid, &ptr);
            u.incoming = message(binding(tlv(4, Bytes(434, 'x'))));
            CHECK(u.incoming.size() == 484);
            m.loop();
            CHECK(std::string(ptr) == std::string(434, 'x'));
        });
    // X.690 section 8.3: signed values require sign extension on decode.
    add("PDU-level errors must not update values",
        []
        {
            for (int version : {0, 1})
            {
                Manager manager;
                UDP udp;
                manager.setUDP(&udp);
                int first = 99, second = 99;
                manager.addIntegerHandler(udp.peer, oid, &first);
                manager.addIntegerHandler(udp.peer, ".1.3.6.1.2.1.1.3.0", &second);
                Bytes other = oidWire;
                other[8] = 3;
                auto bindings = join({binding({2, 1, 42}), tlv(0x30, join({other, {2, 1, 7}}))});
                for (int error : {1, 2, 3, 4, 5})
                {
                    udp.incoming =
                        message(bindings, version, "public", 0xa2, 7, error, error == 1 ? 0 : 2);
                    manager.loop();
                    CHECK(first == 99 && second == 99);
                }
                udp.incoming = message(bindings, version);
                manager.loop();
                CHECK(first == 42 && second == 7);
            }
        });
    add("exception binding does not discard following success",
        []
        {
            for (int tag : {0x80, 0x81, 0x82})
            {
                Manager m;
                UDP u;
                m.setUDP(&u);
                int missing = 99, success = 0;
                m.addIntegerHandler(u.peer, oid, &missing);
                m.addIntegerHandler(u.peer, ".1.3.6.1.2.1.1.3.0", &success);
                Bytes second = oidWire;
                second[8] = 3;
                u.incoming =
                    message(join({binding(tlv(tag, {})), tlv(0x30, join({second, {2, 1, 42}}))}));
                m.loop();
                CHECK(missing == 99);
                CHECK(success == 42);
            }
        });
    add("three-byte negative INTEGER reaches signed callback",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int value = 99;
            manager.addIntegerHandler(udp.peer, oid, &value);
            udp.incoming = message(binding({2, 3, 0xff, 0xff, 0x7f}));
            manager.loop();
            CHECK(value == -129);
        });
    add("UDP bind failure is reported",
        []
        {
            Manager manager;
            UDP udp;
            udp.beginResult = 0;
            manager.setUDP(&udp);
            CHECK(!manager.begin());
            udp.beginResult = 1;
            CHECK(manager.begin());
            CHECK(udp.listenPort == 162);
        });
    add("embedded NUL community cannot match public prefix",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int value = 99;
            manager.addIntegerHandler(udp.peer, oid, &value);
            auto valid = message(binding({2, 1, 42}));
            // Replace the independently generated community field, then rewrap.
            Bytes body(valid.begin() + 2, valid.end());
            body.erase(body.begin() + 3, body.begin() + 11);
            auto community = tlv(4, {'p', 'u', 'b', 'l', 'i', 'c', 0, 'x'});
            body.insert(body.begin() + 3, community.begin(), community.end());
            udp.incoming = tlv(0x30, body);
            manager.loop();
            CHECK(value == 99);
        });
    add("over-cap community must not match truncated prefix",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int value = 99;
            manager.addIntegerHandler(udp.peer, oid, &value);
            std::string configured(253, 'a');
            manager._community = configured.c_str();
            std::string incoming(SNMP_OCTETSTRING_MAX_LENGTH, 'a');
            incoming.back() = 'b';
            auto bytes = message(binding({2, 1, 42}), 1, incoming.c_str());
            CHECK(bytes.size() < SNMP_PACKET_LENGTH * 3);
            // Exercise the public parser helper to isolate community handling from
            // the separate 512-byte UDP read cap. Every byte of the TLV is present.
            std::string hex;
            for (auto byte : bytes)
            {
                char token[4];
                snprintf(token, sizeof(token), "%02x ", byte);
                hex += token;
            }
            manager.testParsePacket(String(hex.c_str()));
            CHECK(value == 99);
        });
    add("incomplete UDP response cannot update a callback",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int value = 99;
            manager.addIntegerHandler(udp.peer, oid, &value);
            udp.incoming = message(binding({2, 1, 42}));
            udp.incoming.pop_back(); // INTEGER content is missing, lengths unchanged.
            manager.loop();
            CHECK(value == 99);
        });

    // Registration release must invoke derived destructors through the base type.
    add("long OID request and response preserve callback identity",
        []
        {
            for (size_t arcs : {24, 60})
            {
                std::string name = ".1.3";
                Bytes contents{43};
                for (size_t i = 0; i < arcs; ++i)
                {
                    name += ".1";
                    contents.push_back(1);
                }
                CHECK(name.size() > 50 && name.size() < MAX_OID_LENGTH);
                Manager manager;
                UDP udp;
                int value = 99;
                manager.setUDP(&udp);
                auto *callback = manager.addIntegerHandler(udp.peer, name.c_str(), &value);
                CHECK(manager.findCallback(udp.peer, name.c_str()) == callback);
                Request request;
                request.setUDP(&udp);
                request.addOIDPointer(callback);
                CHECK(request.sendTo(udp.peer));
                auto wireOID = tlv(6, contents);
                CHECK(udp.outgoing ==
                      message(tlv(0x30, join({wireOID, {5, 0}})), 1, "public", 0xa0));
                udp.incoming = message(tlv(0x30, join({wireOID, {2, 1, 42}})));
                manager.loop();
                CHECK(value == 42);
            }
        });
    add("same OID responses update only the matching device",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            IPAddress first(192, 0, 2, 1), second(192, 0, 2, 2);
            int a = 99, b = 99;
            manager.addIntegerHandler(first, oid, &a);
            manager.addIntegerHandler(second, oid, &b);
            udp.peer = second;
            udp.incoming = message(binding({2, 1, 42}));
            manager.loop();
            CHECK(a == 99 && b == 42);
            udp.peer = first;
            udp.incoming = message(binding({2, 1, 7}));
            manager.loop();
            CHECK(a == 7 && b == 42);
        });
    add("unregistered OID response leaves callbacks intact",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int value = 99;
            manager.addIntegerHandler(udp.peer, oid, &value);
            Bytes other = oidWire;
            other[8] = 2;
            udp.incoming = message(tlv(0x30, join({other, {2, 1, 42}})));
            manager.loop();
            CHECK(value == 99);
            udp.incoming = message(binding({2, 1, 7}));
            manager.loop();
            CHECK(value == 7);
        });
    add("hex parser rejects bad input and debug prints decoded byte count",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            std::string input;
            for (int i = 0; i < SNMP_PACKET_LENGTH; ++i)
                input += "00 ";
            Serial.hexWrites = 0;
            CHECK(!manager.testParsePacket(String(input.c_str())));
#ifdef DEBUG
            CHECK(Serial.hexWrites == SNMP_PACKET_LENGTH);
#endif
            for (const char *invalid : {"0", "gg", "000", "00z", "-1"})
                CHECK(!manager.testParsePacket(String(invalid)));
            int value = 99;
            manager.addIntegerHandler(udp.peer, oid, &value);
            auto packet = message(binding({2, 1, 42}));
            input.clear();
            for (auto byte : packet)
            {
                char token[4];
                snprintf(token, sizeof(token), "%02X ", byte);
                input += token;
            }
            CHECK(manager.testParsePacket(String(input.c_str())) && value == 42);
        });
    add("library convention: shorter string response terminates old value",
        []
        {
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            char storage[32] = "previous value";
            char *value = storage;
            manager.addStringHandler(udp.peer, oid, &value);
            for (const char *text : {"new", "", "longer again", "x"})
            {
                udp.incoming = message(binding(tlv(4, Bytes(text, text + strlen(text)))));
                manager.loop();
                CHECK(std::string(value) == text);
            }
        });
    // RFC 3417 section 8 permits nonminimal definite-length fields.
}
