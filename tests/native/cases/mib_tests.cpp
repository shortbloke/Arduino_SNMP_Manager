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
