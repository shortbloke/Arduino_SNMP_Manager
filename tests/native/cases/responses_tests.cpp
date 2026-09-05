#include "fixtures.h"
#include "registry.h"

void registerResponsesTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"Responses", name, run}); };
    add("response metadata and multiple bindings",
        []
        {
            for (int version : {0, 1})
            {
                auto b = message(join({binding({2, 1, 42}), binding({0x43, 2, 1, 0})}), version);
                SNMPGetResponse r;
                CHECK(r.parseFrom(b.data()));
                CHECK(r.version == version + 1);
                CHECK(r.requestID == 7);
                CHECK(r.errorStatus == 0 && r.errorIndex == 0);
                CHECK(std::string(r.communityString) == "public");
                CHECK(r.requestType == GetResponsePDU);
                CHECK(r.varBinds->value->type == INTEGER);
                CHECK(r.varBinds->next->value->type == TIMESTAMP);
                CHECK(r.varBinds->next->next->value == nullptr);
            }
        });
    add("response rejects wrong top-level tag",
        []
        {
            Bytes b{4, 0};
            SNMPGetResponse r;
            CHECK(!r.parseFrom(b.data()));
            CHECK(r.isCorrupt);
        });
    add("response rejects wrong version field type",
        []
        {
            auto b = message(binding({2, 1, 42}));
            b[2] = 4;
            SNMPGetResponse r;
            CHECK(!r.parseFrom(b.data()));
            CHECK(r.isCorrupt);
        });
    add("v2c Get exceptions parse as individual bindings",
        []
        {
            for (int tag : {0x80, 0x81})
            {
                auto b = message(binding(tlv(tag, {})));
                SNMPGetResponse r;
                CHECK(r.parseFrom(b.data()));
                CHECK(r.errorStatus == 0);
                CHECK(r.varBinds->value->type == tag);
            }
        });
    add("traversal endOfMibView parses as an exception",
        []
        {
            auto b = message(binding({0x82, 0}));
            SNMPGetResponse r;
            CHECK(r.parseFrom(b.data()));
            CHECK(r.varBinds->value->type == ENDOFMIBVIEW);
        });
    add("response nonminimal outer length accepted",
        []
        {
            auto b = message(binding({2, 1, 42}));
            b.insert(b.begin() + 1, {0x82, 0});
            SNMPGetResponse r;
            CHECK(r.parseFrom(b.data()));
            CHECK(r.requestID == 7);
            CHECK(r.varBinds->value->type == INTEGER);
        });
    add("v1 noSuchName metadata retains one-based error index",
        []
        {
            auto b = message(binding({5, 0}), 0, "public", 0xa2, 7, 2, 1);
            SNMPGetResponse r;
            CHECK(r.parseFrom(b.data()));
            CHECK(r.version == 1);
            CHECK(r.errorStatus == 2 && r.errorIndex == 1);
        });
    add("bounded response parser rejects every truncated prefix",
        []
        {
            const auto packet = message(binding({2, 1, 42}));
            for (size_t length = 0; length < packet.size(); ++length)
            {
                Bytes prefix(packet.begin(), packet.begin() + length);
                SNMPGetResponse response;
                CHECK(!response.parseFrom(prefix.data(), prefix.size()));
            }
            Manager manager;
            UDP udp;
            manager.setUDP(&udp);
            int value = 99;
            manager.addIntegerHandler(udp.peer, oid, &value);
            udp.incoming = Bytes(SNMP_PACKET_LENGTH * 3 + 1, 0);
            manager.loop();
            CHECK(value == 99 && udp.reads == 0);
        });
    add("response parser safely handles empty and missing fields on reuse",
        []
        {
            SNMPGetResponse response;
            auto valid = message(binding({2, 1, 42}));
            CHECK(response.parseFrom(valid.data(), valid.size()));
            auto empty = message({});
            CHECK(response.parseFrom(empty.data(), empty.size()));
            CHECK(!response.varBinds->value);
            for (Bytes malformed :
                 {tlv(0x30, {}), message(tlv(0x30, {})), message(tlv(0x30, oidWire))})
            {
                CHECK(!response.parseFrom(malformed.data(), malformed.size()));
                CHECK(response.isCorrupt);
            }
            CHECK(response.parseFrom(valid.data(), valid.size()));
            CHECK(!response.isCorrupt && response.varBinds->value->type == INTEGER);
        });
    add("short tooBig response with empty bindings accepted",
        []
        {
            auto b = message({}, 1, "public", 0xa2, 7, 1, 0);
            CHECK(b.size() < 30);
            SNMPGetResponse r;
            CHECK(r.parseFrom(b.data()));
            CHECK(r.errorStatus == 1 && r.errorIndex == 0);
            CHECK(!r.varBinds || !r.varBinds->value);
        });
    add("empty bindings accepted with long community",
        []
        {
            auto b = message({}, 1, "long-community-name", 0xa2, 7, 1, 0);
            CHECK(b.size() > 30);
            SNMPGetResponse r;
            CHECK(r.parseFrom(b.data()));
            CHECK(r.errorStatus == 1);
            CHECK(!r.varBinds || !r.varBinds->value);
        });
}
