#include "mock_agent.h"
#include "registry.h"
#include <SNMPTable.h>
#include <SNMPMIB.h>
#include <algorithm>

namespace
{
template <class Operation>
void complete(SNMPClient &client, MockAgent &agent, UDP &udp, Operation &operation,
              uint32_t start = 0)
{
    for (uint32_t now = start; operation.pending() && now < 10000; now += 10)
    {
        client.loop(now);
        agent.service(udp);
    }
    CHECK(!operation.pending());
}
const char *column = ".1.3.6.1.2.1.2.2.1.10";
std::vector<uint32_t> populate(MockAgent &agent)
{
    // Insertion order is intentionally different from numeric OID order.
    std::vector<uint32_t> indices{1001, 42, 10, 7, 1};
    for (uint32_t i = 1100; indices.size() < 48; i += 13)
        indices.push_back(i);
    for (uint32_t i : indices)
    {
        std::string name = std::string(column) + "." + std::to_string(i);
        agent.put(name.c_str(), {0x41, 1, 9});
    }
    std::sort(indices.begin(), indices.end());
    return indices;
}
Bytes rawRequest(unsigned type, unsigned nonRepeaters, unsigned repetitions,
                 const std::vector<const char *> &names)
{
    Bytes bindings;
    for (const char *name : names)
    {
        Bytes b = tlv(0x30, join({MockAgent::wireOID(MockAgent::oid(name)), {5, 0}}));
        bindings.insert(bindings.end(), b.begin(), b.end());
    }
    return tlv(0x30,
               join({{2, 1, 1},
                     tlv(4, {'p', 'u', 'b', 'l', 'i', 'c'}),
                     tlv(type, join({MockAgent::integer(123456), MockAgent::integer(nonRepeaters),
                                     MockAgent::integer(repetitions), tlv(0x30, bindings)}))}));
}
}
void registerAgentTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"Agent", name, run}); };
    add("oversized bulk replies recover through GETNEXT without losing rows",
        []
        {
            UDP udp;
            MockAgent agent;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            device.timeoutMs = 10;
            device.retries = 1;
            const char *root = ".1.3.6.1.4.1.999";
            for (unsigned i : {1u, 7u, 26u, 65u, 91u})
            {
                std::string name = std::string(root) + "." + std::to_string(i);
                agent.put(name.c_str(), tlv(4, Bytes(SNMP_PACKET_LENGTH / 3, 'x')));
            }
            SNMPWalk<8> walk(device);
            CHECK(client.begin().ok() && walk.configure(root).ok() && walk.start().ok());
            complete(client, agent, udp, walk);
            CHECK(walk.status().ok() && walk.size() == 5);
            CHECK(agent.exchanges.front().response.size() > SNMP_PACKET_LENGTH);
            bool next = false;
            for (const auto &exchange : agent.exchanges)
                next = next || exchange.pdu == 0xa1;
            CHECK(next);
            CHECK(std::string(walk[2].oid) == std::string(root) + ".26");
            CHECK(std::string(walk[3].oid) == std::string(root) + ".65");
            for (size_t i = 0; i < walk.size(); ++i)
                CHECK(walk[i].ok() && walk[i].value.length == SNMP_PACKET_LENGTH / 3);
        });
    add("logical interfaces beyond physical ports fit the expanded example capacity",
        []
        {
            UDP udp;
            MockAgent agent;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            for (unsigned n = 0; n < 53; ++n)
            {
                const std::string index = "." + std::to_string(n < 26 ? n + 1 : n + 39);
                agent.put((".1.3.6.1.2.1.2.2.1.2" + index).c_str(), tlv(4, {'p'}));
                agent.put((".1.3.6.1.2.1.31.1.1.1.6" + index).c_str(), {0x46, 1, 7});
                agent.put((".1.3.6.1.2.1.31.1.1.1.10" + index).c_str(), {0x46, 1, 9});
            }
            SNMPInterfaceRead<64, 16> table(device);
            CHECK(client.begin().ok() && table.start().ok());
            complete(client, agent, udp, table);
            CHECK(table.status().ok() && table.size() == 53);
            CHECK(std::string(table[25].index) == "26");
            CHECK(std::string(table[26].index) == "65");
            for (const auto &row : table)
                CHECK(row[0].ok() && row[1].ok() && row[2].ok() && row[1].value.unsigned64() == 7 &&
                      row[2].value.unsigned64() == 9);
        });
    add("large sparse storage tables retain wide sizes and isolate invalid units",
        []
        {
            UDP udp;
            MockAgent agent;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            const std::string root = ".1.3.6.1.2.1.25.2.3.1.";
            for (unsigned row = 0; row < 103; ++row)
            {
                const std::string index = "." + std::to_string(row * 3 + 1);
                agent.put((root + "3" + index).c_str(), tlv(4, {'d', 'i', 's', 'k'}));
                agent.put((root + "4" + index).c_str(), MockAgent::integer(row == 54 ? 0 : 4096));
                agent.put((root + "5" + index).c_str(), MockAgent::integer(2000000000));
                agent.put((root + "6" + index).c_str(), MockAgent::integer(1500000000));
            }
            SNMPTableRead<128, 4, 16> table(device);
            for (unsigned column = 3; column <= 6; ++column)
                CHECK(table
                          .addColumn((root + std::to_string(column)).c_str(),
                                     column == 3 ? STRING : INTEGER)
                          .ok());
            CHECK(client.begin().ok() && table.start().ok());
            complete(client, agent, udp, table);
            CHECK(table.status().ok() && table.size() == 103);
            for (size_t row = 0; row < table.size(); ++row)
            {
                CHECK(std::string(table[row].index) == std::to_string(row * 3 + 1));
                CHECK(table[row][0].ok() && table[row][1].ok() && table[row][2].ok() &&
                      table[row][3].ok());
                uint64_t bytes = 123;
                CHECK(SNMPMIB::storageBytes(table[row][1].value, table[row][2].value, bytes) ==
                      (row != 54));
                CHECK(bytes == (row == 54 ? 123ULL : 8192000000000ULL));
            }
            SNMPTableRead<16, 1, 16> small(device);
            CHECK(small.addColumn((root + "3").c_str(), STRING).ok() && small.start().ok());
            complete(client, agent, udp, small);
            CHECK(small.status().code() == SNMPStatus::CapacityExceeded && small.size() == 16);
            CHECK(small[0][0].ok() && small[15][0].ok());
            SNMPWalk<1> stream(device);
            size_t received = 0;
            CHECK(stream.configure((root + "3").c_str()).ok());
            CHECK(stream
                      .stream(
                          [](const SNMPResult &result, void *context)
                          {
                              CHECK(result.ok());
                              ++*static_cast<size_t *>(context);
                              return true;
                          },
                          &received)
                      .ok());
            CHECK(stream.start().ok());
            complete(client, agent, udp, stream);
            CHECK(stream.status().ok() && received == 103 && stream.size() == 0);
        });
    add("mock numeric ordering and bulk repetition semantics have independent expectations",
        []
        {
            MockAgent agent;
            CHECK(MockAgent::wireOID(MockAgent::oid(".1.3.6.1.2.1.1.1.0")) == oidWire);
            CHECK(MockAgent::integer(128) == Bytes({2, 2, 0, 128}));
            agent.put(".1.3.6.1.10", {2, 1, 10});
            agent.put(".1.3.6.1.7", {2, 1, 7});
            agent.put(".1.3.6.1.42", {2, 1, 42});
            agent.answer(rawRequest(0xa5, 1, 3, {".1.3.6.1.6", ".1.3.6.1.7", ".1.3.6.1.10"}));
            const auto &exchange = agent.exchanges.back();
            CHECK(exchange.id == 123456);
            CHECK(exchange.returned ==
                  std::vector<MockAgent::OID>(
                      {MockAgent::oid(".1.3.6.1.7"), MockAgent::oid(".1.3.6.1.10"),
                       MockAgent::oid(".1.3.6.1.42"), MockAgent::oid(".1.3.6.1.42"),
                       MockAgent::oid(".1.3.6.1.42"), MockAgent::oid(".1.3.6.1.42"),
                       MockAgent::oid(".1.3.6.1.42")}));
            SNMPGetResponse decoded;
            Bytes response = exchange.response;
            CHECK(decoded.parseFrom(response.data(), response.size()));
            unsigned position = 0;
            for (auto *b = decoded.varBinds; b && b->value; b = b->next, ++position)
                CHECK(b->value->type == (position >= 4 ? ENDOFMIBVIEW : INTEGER));
            agent.answer(rawRequest(0xa5, 0, 0, {".1.3.6.1.7"}));
            CHECK(agent.exchanges.back().returned.empty());
        });
    add("complete sparse walks match all 48 expected rows in both versions",
        []
        {
            for (auto version : {SNMPVersion::Version1, SNMPVersion::Version2c})
            {
                UDP udp;
                MockAgent agent;
                auto indices = populate(agent);
                // A following subtree must not leak into results.
                agent.put(".1.3.6.1.2.1.2.2.1.11.1", {2, 1, 1});
                SNMPClient client(udp);
                SNMPDevice device(client, udp.peer, "public", version);
                SNMPWalk<48> walk(device);
                CHECK(walk.configure(column).ok());
                CHECK(client.begin().ok());
                CHECK(walk.start().ok());
                complete(client, agent, udp, walk);
                CHECK(walk.status().ok());
                CHECK(walk.size() == indices.size());
                for (size_t i = 0; i < indices.size(); ++i)
                {
                    CHECK(std::string(walk[i].oid) ==
                          std::string(column) + "." + std::to_string(indices[i]));
                    CHECK(walk[i].ok());
                    CHECK(walk[i].value.unsigned32() == 9);
                }
                CHECK(agent.exchanges.size() == (version == SNMPVersion::Version1 ? 49 : 13));
                for (const auto &exchange : agent.exchanges)
                {
                    CHECK(exchange.pdu == (version == SNMPVersion::Version1 ? 0xa1u : 0xa5u));
                    CHECK(exchange.nonRepeaters == 0);
                    CHECK(exchange.maxRepetitions == (version == SNMPVersion::Version1 ? 0u : 4u));
                }
            }
        });
    add("end of MIB uses noSuchName for v1 and endOfMibView for v2c",
        []
        {
            for (auto version : {SNMPVersion::Version1, SNMPVersion::Version2c})
            {
                UDP udp;
                MockAgent agent;
                populate(agent);
                SNMPClient client(udp);
                SNMPDevice device(client, udp.peer, "public", version);
                SNMPWalk<48> walk(device);
                CHECK(client.begin().ok());
                CHECK(walk.configure(column).ok());
                CHECK(walk.start().ok());
                complete(client, agent, udp, walk);
                CHECK(walk.status().ok());
                CHECK(walk.size() == 48);
                CHECK(agent.exchanges.back().error == (version == SNMPVersion::Version1 ? 2u : 0u));
                if (version == SNMPVersion::Version1)
                    CHECK(agent.exchanges.back().errorIndex == 1);
            }
        });
    add("sparse uneven table columns join complete and composite indices",
        []
        {
            UDP udp;
            MockAgent agent;
            agent.put(".1.3.6.1.4.1.99.1.7.1", {2, 1, 1});
            agent.put(".1.3.6.1.4.1.99.1.42.2", {2, 1, 2});
            agent.put(".1.3.6.1.4.1.99.2.7.1", {2, 1, 3});
            agent.put(".1.3.6.1.4.1.99.2.1001.4", {2, 1, 4});
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPTableRead<3, 2> table(device);
            CHECK(table.addColumn(".1.3.6.1.4.1.99.1", INTEGER).ok());
            CHECK(table.addColumn(".1.3.6.1.4.1.99.2", INTEGER).ok());
            CHECK(client.begin().ok());
            CHECK(table.start().ok());
            complete(client, agent, udp, table);
            CHECK(table.status().code() == SNMPStatus::Partial);
            CHECK(table.size() == 3);
            CHECK(std::string(table[0].index) == "7.1");
            CHECK(table[0][0].ok() && table[0][1].ok());
            CHECK(table[0][1].value.integer() == 3);
            CHECK(std::string(table[1].index) == "42.2");
            CHECK(!table[1][1].ok());
            CHECK(std::string(table[2].index) == "1001.4");
            CHECK(!table[2][0].ok());
        });
    add("agent faults exercise fallback retries capacity and progression",
        []
        {
            for (unsigned fault = 0; fault < 5; ++fault)
            {
                UDP udp;
                MockAgent agent;
                populate(agent);
                if (fault == 0)
                    agent.responseLimit = 90;
                if (fault == 1)
                    agent.dropNext = true;
                if (fault == 2)
                    agent.truncateNext = true;
                if (fault == 3)
                    agent.repeatNext = true;
                SNMPClient client(udp);
                SNMPDevice device(client, udp.peer, "public");
                device.timeoutMs = 20;
                SNMPWalk<47> walk(device);
                CHECK(walk.configure(column).ok());
                CHECK(client.begin().ok());
                CHECK(walk.start().ok());
                complete(client, agent, udp, walk);
                CHECK(walk.status().code() ==
                      (fault == 3 ? SNMPStatus::ProtocolError : SNMPStatus::CapacityExceeded));
                if (fault == 0)
                    CHECK(agent.exchanges[1].pdu == 0xa1);
                if (fault == 1 || fault == 2)
                    CHECK(agent.exchanges[0].id == agent.exchanges[1].id);
                if (fault != 3)
                    CHECK(walk.size() == 47);
            }
        });
    add("GET distinguishes exact values missing instances and unknown objects",
        []
        {
            for (auto version : {SNMPVersion::Version1, SNMPVersion::Version2c})
            {
                UDP udp;
                MockAgent agent;
                agent.put(".1.3.6.1.4.1.99.1.7", {2, 1, 9});
                SNMPClient client(udp);
                SNMPDevice device(client, udp.peer, "public", version);
                SNMPQuery<3> query(device);
                CHECK(query.addOID(".1.3.6.1.4.1.99.1.7").ok());
                CHECK(query.addOID(".1.3.6.1.4.1.99.1.8").ok());
                CHECK(query.addOID(".1.3.6.1.4.1.99.2.7").ok());
                CHECK(client.begin().ok());
                CHECK(query.start().ok());
                complete(client, agent, udp, query);
                CHECK(query.status().code() == SNMPStatus::Partial);
                CHECK(query[0].ok());
                CHECK(query[1].status.code() == SNMPStatus::Missing);
                CHECK(query[2].status.code() == SNMPStatus::Missing);
            }
        });
    add("interface discovery verifies 48 sparse names and mixed counter widths",
        []
        {
            for (auto version : {SNMPVersion::Version1, SNMPVersion::Version2c})
            {
                UDP udp;
                MockAgent agent;
                std::vector<uint32_t> indices;
                for (uint32_t i = 0; i < 48; ++i)
                {
                    uint32_t index = i * 17 + 7;
                    indices.push_back(index);
                    std::string suffix = "." + std::to_string(index);
                    std::string label = "port-" + std::to_string(index);
                    agent.put((".1.3.6.1.2.1.2.2.1.2" + suffix).c_str(),
                              tlv(4, Bytes(label.begin(), label.end())));
                    agent.put((".1.3.6.1.2.1.2.2.1.10" + suffix).c_str(),
                              {0x41, 1, static_cast<unsigned char>(i + 1)});
                    agent.put((".1.3.6.1.2.1.2.2.1.16" + suffix).c_str(),
                              {0x41, 1, static_cast<unsigned char>(i + 2)});
                    if (version == SNMPVersion::Version2c && i % 2 == 0)
                    {
                        agent.put((".1.3.6.1.2.1.31.1.1.1.6" + suffix).c_str(),
                                  {0x46, 1, static_cast<unsigned char>(i + 70)});
                        agent.put((".1.3.6.1.2.1.31.1.1.1.10" + suffix).c_str(),
                                  {0x46, 1, static_cast<unsigned char>(i + 71)});
                    }
                }
                SNMPClient client(udp);
                SNMPDevice device(client, udp.peer, "public", version);
                SNMPInterfaceRead<48> table(device);
                CHECK(client.begin().ok());
                CHECK(table.start().ok());
                complete(client, agent, udp, table);
                CHECK(table.status().ok());
                CHECK(table.size() == 48);
                for (size_t i = 0; i < table.size(); ++i)
                {
                    CHECK(std::string(table[i].index) == std::to_string(indices[i]));
                    CHECK(table[i][0].ok());
                    CHECK(std::string(table[i][0].value.text()) ==
                          "port-" + std::to_string(indices[i]));
                    bool high = version == SNMPVersion::Version2c && i % 2 == 0;
                    CHECK(table[i][1].ok() && table[i][2].ok());
                    CHECK(table[i][1].value.type == (high ? COUNTER64 : COUNTER32));
                    CHECK(table[i][1].value.unsigned64() == (high ? i + 70 : i + 1));
                    CHECK(table[i][2].value.unsigned64() == (high ? i + 71 : i + 2));
                }
            }
        });
    add("duplicate bulk response cannot be collected twice",
        []
        {
            UDP udp;
            MockAgent agent;
            populate(agent);
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPWalk<48> walk(device);
            CHECK(walk.configure(column).ok());
            CHECK(client.begin().ok());
            CHECK(walk.start().ok());
            client.loop(0);
            agent.service(udp);
            Bytes duplicate = udp.incoming;
            client.loop(10);
            CHECK(walk.size() == 4);
            udp.incoming = duplicate;
            client.loop(20);
            CHECK(walk.size() == 4);
            CHECK(walk.pending());
            agent.service(udp);
            complete(client, agent, udp, walk, 30);
            CHECK(walk.status().ok());
            CHECK(walk.size() == 48);
        });
}
