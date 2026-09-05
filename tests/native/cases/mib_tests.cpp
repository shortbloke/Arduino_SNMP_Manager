#include "mock_agent.h"
#include "registry.h"
#include <SNMPMIB.h>
#include <SNMPTable.h>
#include <cmath>

void registerMIBTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"MIB", name, run}); };
    add("IF-MIB descriptions exceed old inline capacity and survive new polls",
        []
        {
            UDP udp;
            MockAgent agent;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            const char *name = ".1.3.6.1.2.1.2.2.1.2.7";
            size_t size = SNMP_PACKET_LENGTH >= 320 ? 255 : 160;
            agent.put(name, tlv(4, Bytes(size, 'x')));
            SNMPQuery<1> query(device);
            CHECK(query.addOID(name, STRING).ok());
            CHECK(client.begin().ok());
            CHECK(query.start().ok());
            client.loop(0);
            agent.service(udp);
            client.loop(1);
            CHECK(query.status().ok());
            CHECK(query[0].value.length == size);
            CHECK(query[0].value.isText());
            SNMPValue snapshot = query[0].value;
            agent.put(name, tlv(4, {'n', 'e', 'w'}));
            CHECK(query.start().ok());
            client.loop(2);
            agent.service(udp);
            client.loop(3);
            CHECK(query[0].value.length == 3);
            CHECK(snapshot.length == size);
            CHECK(snapshot.bytes[size - 1] == 'x');
        });
    add("TCP-MIB IPv6 composite instance is walked without textual truncation",
        []
        {
            std::string address = "2.16.32.1.13.184";
            for (unsigned i = 0; i < 12; ++i)
                address += ".255";
            std::string name = ".1.3.6.1.2.1.6.19.1.7." + address + ".65535." + address + ".65535";
            CHECK(name.size() > 127);
            UDP udp;
            MockAgent agent;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPQuery<1> query(device);
            if (name.size() >= MAX_OID_LENGTH)
            {
                CHECK(query.addOID(name.c_str()).code() == SNMPStatus::InvalidOID);
                return;
            }
            CHECK(query.addOID(name.c_str(), INTEGER).ok());
            agent.put(name.c_str(), {2, 1, 5});
            CHECK(client.begin().ok());
            CHECK(query.start().ok());
            client.loop(0);
            agent.service(udp);
            client.loop(1);
            CHECK(query.status().ok());
            SNMPWalk<1> walk(device);
            CHECK(walk.configure(".1.3.6.1.2.1.6.19.1.7").ok());
            CHECK(walk.start().ok());
            client.loop(2);
            agent.service(udp);
            client.loop(3);
            CHECK(walk.status().ok());
            CHECK(walk.size() == 1);
            CHECK(std::string(walk[0].oid) == name);
        });
    add("payload bounds copies and failed allocation preserve ownership",
        []
        {
            static_assert(sizeof(SNMPValue) <= 64,
                          "Numeric results must not contain a large inline payload");
            SNMPValue original;
            const unsigned char data[] = {'a', 0, 'b'};
            CHECK(original.setBytes(data, sizeof(data)).ok());
            CHECK(!original.isText());
            SNMPValue copy;
            {
                FailAllocations fail(0);
                copy = original;
                CHECK(copy.bytes == original.bytes);
                CHECK(original.setBytes(data, sizeof(data)).code() ==
                      SNMPStatus::AllocationFailure);
            }
            CHECK(original.length == 3);
            original = SNMPValue::integer32(4);
            CHECK(copy.length == 3 && copy.bytes[2] == 'b');
            copy = copy;
            Bytes maximum(SNMP_VALUE_MAX_LENGTH, 0xa5);
            CHECK(copy.setBytes(maximum.data(), maximum.size(), OPAQUE).ok());
            CHECK(copy.setBytes(maximum.data(), maximum.size() + 1, OPAQUE).code() ==
                  SNMPStatus::CapacityExceeded);
            CHECK(copy.length == maximum.size());
            CHECK(copy.setBytes(nullptr, 0).ok());
            CHECK(copy.isText());
        });
    add("accepted walk and table restarts release old payload slots",
        []
        {
            UDP udp;
            MockAgent agent;
            const char *root = ".1.3.6.1.2.1.2.2.1.2";
            agent.put(".1.3.6.1.2.1.2.2.1.2.7", tlv(4, Bytes(120, 'x')));
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            CHECK(client.begin().ok());
            auto finish = [&](SNMPOperation &operation)
            {
                for (uint32_t now = 0; operation.pending() && now < 1000; now += 10)
                {
                    client.loop(now);
                    agent.service(udp);
                }
                CHECK(operation.status().ok());
            };
            SNMPWalk<2> walk(device);
            CHECK(walk.configure(root).ok());
            CHECK(walk.start().ok());
            finish(walk);
            CHECK(walk.size() == 1);
            // References expose the existing slot, whose object lifetime continues.
            const auto &slot = walk[0].value;
            SNMPValue snapshot = slot;
            CHECK(walk.start().ok());
            CHECK(slot.length == 0);
            CHECK(snapshot.length == 120);
            walk.cancel();
            SNMPTableRead<2, 1> table(device);
            CHECK(table.addColumn(root, STRING).ok());
            CHECK(table.start().ok());
            for (uint32_t now = 0; table.pending() && now < 1000; now += 10)
            {
                client.loop(now);
                agent.service(udp);
            }
            CHECK(table.status().ok() && table.size() == 1);
            const auto &cell = table[0][0].value;
            CHECK(cell.length == 120);
            // Rejected starts retain the visible previous result.
            device.port = 0;
            CHECK(!table.start().ok());
            CHECK(cell.length == 120);
            device.port = 161;
            CHECK(table.start().ok());
            CHECK(cell.length == 0);
            CHECK(snapshot.length == 120);
            table.cancel();
        });
    add("compact table indices save RAM and reject overflow without truncation",
        []
        {
            static_assert(sizeof(SNMPTableRead<16, 1, 16>) + 16 * (MAX_OID_LENGTH - 16) <=
                              sizeof(SNMPTableRead<16, 1>),
                          "Compact indices must reduce row storage");
            UDP udp;
            MockAgent agent;
            agent.put(".1.3.6.1.2.1.2.2.1.2.123", tlv(4, {'a'}));
            agent.put(".1.3.6.1.2.1.2.2.1.2.1234", tlv(4, {'b'}));
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPTableRead<2, 1, 4> table(device);
            CHECK(table.addColumn(".1.3.6.1.2.1.2.2.1.2", STRING).ok());
            CHECK(client.begin().ok());
            CHECK(table.start().ok());
            for (uint32_t now = 0; table.pending() && now < 1000; now += 10)
            {
                client.loop(now);
                agent.service(udp);
            }
            CHECK(table.status().code() == SNMPStatus::CapacityExceeded);
            CHECK(table.size() == 1);
            CHECK(std::string(table[0].index) == "123");
            CHECK(table[0][0].ok() && table[0][0].value.bytes[0] == 'a');
        });
    add("MIB helper invalid inputs preserve outputs and all supply states are distinct",
        []
        {
            SNMPValue wrong = SNMPValue::counter32(1);
            uint64_t bytes = 99;
            bool truth = false;
            double number = 42;
            CHECK(!SNMPMIB::storageBytes(wrong, SNMPValue::integer32(1), bytes));
            CHECK(!SNMPMIB::truthValue(wrong, truth));
            CHECK(SNMPMIB::truthValue(SNMPValue::integer32(1), truth) && truth);
            for (int exponent : {-25, 25})
                CHECK(!SNMPMIB::fixedPoint(SNMPValue::integer32(1), exponent, 0, number));
            for (int precision : {-9, 10})
                CHECK(!SNMPMIB::fixedPoint(SNMPValue::integer32(1), 0, precision, number));
            CHECK(!SNMPMIB::fixedPoint(wrong, 0, 0, number));
            CHECK(!SNMPMIB::fixedPoint(SNMPValue::integer32(-1000000000), 0, 0, number));
            CHECK(SNMPMIB::supplyState(SNMPValue::integer32(-1)) == SNMPMIB::SupplyState::Other);
            CHECK(SNMPMIB::supplyState(SNMPValue::integer32(-4)) == SNMPMIB::SupplyState::Invalid);
            CHECK(SNMPMIB::supplyState(wrong) == SNMPMIB::SupplyState::Invalid);
            CHECK(SNMPMIB::supplyState(SNMPValue::integer32(0)) == SNMPMIB::SupplyState::Known);
            CHECK(
                !SNMPMIB::supplyPercent(SNMPValue::integer32(2), SNMPValue::integer32(1), number));
            CHECK(
                !SNMPMIB::supplyPercent(SNMPValue::integer32(0), SNMPValue::integer32(0), number));
            CHECK(!SNMPMIB::supplyPercent(SNMPValue::integer32(1), wrong, number));
            CHECK(bytes == 99 && number == 42);
        });
    add("address helpers and owned address OID payloads enforce lengths and preserve binary data",
        []
        {
            SNMPValue value;
            unsigned char ip[] = {192, 0, 2, 1};
            CHECK(value.setBytes(ip, sizeof(ip)).ok());
            char text[40] = "unchanged";
            CHECK(!SNMPMIB::formatAddress(SNMPValue::integer32(1), value, text, 9));
            CHECK(std::string(text) == "unchanged");
            CHECK(SNMPMIB::formatAddress(SNMPValue::integer32(1), value, text, sizeof(text)));
            CHECK(std::string(text) == "192.0.2.1");
            CHECK(!SNMPMIB::formatAddress(SNMPValue::integer32(3), value, text, sizeof(text)));
            CHECK(!SNMPMIB::formatAddress(SNMPValue::counter32(1), value, text, sizeof(text)));
            CHECK(!SNMPMIB::formatAddress(SNMPValue::integer32(2), value, text, sizeof(text)));
            CHECK(!SNMPMIB::formatAddress(SNMPValue::integer32(1), value, nullptr, sizeof(text)));
            CHECK(!SNMPMIB::formatMAC(value, text, sizeof(text)));
            CHECK(std::string(text) == "192.0.2.1");
            CHECK(value.setBytes(ip, sizeof(ip), NETWORK_ADDRESS).ok());
            CHECK(!value.setBytes(ip, 3, NETWORK_ADDRESS).ok());
            CHECK(value.type == NETWORK_ADDRESS && value.length == 4);
            CHECK(!value.setBytes(nullptr, 1).ok());
            CHECK(!value.setBytes(ip, 4, INTEGER).ok());
            const unsigned char oid[] = ".1.3.6.1";
            CHECK(value.setBytes(oid, sizeof(oid) - 1, OID).ok());
            CHECK(!value.setBytes(oid, sizeof(oid), OID).ok());
            CHECK(!value.setBytes(nullptr, 0, OID).ok());
            CHECK(value.type == OID && std::string(value.text()) == ".1.3.6.1");
            CHECK(value.setBytes(value.bytes, value.length, OID).ok()); // Aliased replacement.
            CHECK(std::string(value.text()) == ".1.3.6.1");
        });
    add("host storage conversion uses wide arithmetic and checked types",
        []
        {
            uint64_t bytes = 7;
            CHECK(SNMPMIB::storageBytes(SNMPValue::integer32(4096),
                                        SNMPValue::integer32(2147483647), bytes));
            CHECK(bytes == 8796093018112ULL);
            CHECK(!SNMPMIB::storageBytes(SNMPValue::integer32(0), SNMPValue::integer32(1), bytes));
            CHECK(bytes == 8796093018112ULL);
            CHECK(!SNMPMIB::storageBytes(SNMPValue::integer32(1), SNMPValue::integer32(-1), bytes));
        });
    add("sensor precision and printer sentinel values are not naive percentages",
        []
        {
            double reading = 123;
            CHECK(SNMPMIB::fixedPoint(SNMPValue::integer32(-125), 0, 1, reading));
            CHECK(reading == -12.5);
            CHECK(SNMPMIB::fixedPoint(SNMPValue::integer32(1500), -3, 0, reading));
            CHECK(std::abs(reading - 1.5) < 1e-10);
            CHECK(SNMPMIB::fixedPoint(SNMPValue::integer32(125), 0, -2, reading));
            CHECK(reading == 125);
            CHECK(!SNMPMIB::fixedPoint(SNMPValue::integer32(1000000000), 0, 0, reading));
            CHECK(reading == 125);
            CHECK(SNMPMIB::supplyState(SNMPValue::integer32(-2)) == SNMPMIB::SupplyState::Unknown);
            CHECK(SNMPMIB::supplyState(SNMPValue::integer32(-3)) ==
                  SNMPMIB::SupplyState::SomeRemaining);
            CHECK(!SNMPMIB::supplyPercent(SNMPValue::integer32(-2), SNMPValue::integer32(100),
                                          reading));
            CHECK(SNMPMIB::supplyPercent(SNMPValue::integer32(25), SNMPValue::integer32(200),
                                         reading));
            CHECK(reading == 12.5);
            bool truth = true;
            CHECK(SNMPMIB::truthValue(SNMPValue::integer32(2), truth));
            CHECK(!truth);
            CHECK(!SNMPMIB::truthValue(SNMPValue::integer32(0), truth));
            CHECK(!truth);
        });
    add("binary MAC and IPv6 addresses retain zeros and format safely",
        []
        {
            SNMPValue value;
            char result[40] = "unchanged";
            unsigned char mac[] = {0, 1, 2, 128, 254, 255};
            CHECK(value.setBytes(mac, sizeof(mac)).ok());
            CHECK(!SNMPMIB::formatMAC(value, result, 17));
            CHECK(std::string(result) == "unchanged");
            CHECK(SNMPMIB::formatMAC(value, result, sizeof(result)));
            CHECK(std::string(result) == "00:01:02:80:fe:ff");
            unsigned char ip[16] = {32, 1, 13, 184};
            CHECK(value.setBytes(ip, sizeof(ip)).ok());
            CHECK(SNMPMIB::formatAddress(SNMPValue::integer32(2), value, result, sizeof(result)));
            CHECK(std::string(result) == "2001:db8:0:0:0:0:0:0");
        });
}
