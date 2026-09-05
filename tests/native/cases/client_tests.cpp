#include "fixtures.h"
#include "registry.h"
#include <SNMPClient.h>
#include <SNMPTable.h>

namespace
{
Bytes reply(UDP &udp, Bytes values, int error = 0)
{
    SNMPGetResponse request;
    CHECK(request.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
    return message(values, request.version - 1, request.communityString, GetResponsePDU,
                   request.requestID, error);
}
}
void registerClientTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"Client", name, run}); };
    add("empty successful GETBULK falls back without inventing end of view",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPWalk<2> walk(device);
            CHECK(walk.configure(".1.3.6.1.2.1.1").ok());
            CHECK(client.begin().ok() && walk.start().ok());
            client.loop(0);
            udp.incoming = reply(udp, {});
            client.loop(1);
            CHECK(walk.pending() && walk.size() == 0);
            SNMPGetResponse request;
            CHECK(request.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
            CHECK(request.requestType == GetNextRequestPDU);
            udp.incoming = reply(udp, binding({2, 1, 7}));
            client.loop(2);
            CHECK(walk.pending() && walk.size() == 1 && walk[0].value.integer() == 7);
            udp.incoming = reply(udp, binding({0x82, 0}));
            client.loop(3);
            CHECK(walk.status().ok() && walk.size() == 1);
        });
    add("addresses and setup fail explicitly without sending",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            for (const char *text :
                 {"", "1.2.3", "256.1.1.1", "1.2.3.4.5", "host", "1.2.3.4x", "::1"})
            {
                SNMPDevice bad(client, text, "public");
                CHECK(bad.status().code() == SNMPStatus::InvalidAddress);
            }
            SNMPDevice device(client, "192.168.1.10", "public");
            SNMPQuery<1> query(device);
            CHECK(query.addOID(oid, INTEGER).ok());
            CHECK(query.start().code() == SNMPStatus::NotStarted);
            CHECK(client.begin().ok());
            CHECK(query.start().ok());
            CHECK(udp.packets == 0);
            CHECK(query.start().code() == SNMPStatus::Busy);
            query.cancel();
            CHECK(query.takeCompleted());
            CHECK(!query.takeCompleted());
            CHECK(query.status().code() == SNMPStatus::Cancelled);
        });
    add("queries correlate peer port community version and request ID",
        []
        {
            for (SNMPVersion version : {SNMPVersion::Version1, SNMPVersion::Version2c})
            {
                UDP udp;
                SNMPClient client(udp);
                SNMPDevice device(client, udp.peer, "secret", version);
                SNMPQuery<1> query(device);
                CHECK(query.addOID(oid, INTEGER).ok());
                CHECK(client.begin().ok());
                CHECK(query.start().ok());
                client.loop(0);
                Bytes good = reply(udp, binding({2, 1, 42}));
                udp.peerPort = 162;
                udp.incoming = good;
                client.loop(1);
                CHECK(query.pending());
                udp.peerPort = 161;
                udp.incoming = good;
                client.loop(2);
                CHECK(query.status().ok());
                CHECK(query[0].ok());
                CHECK(query[0].value.integer() == 42);
                CHECK(query.start().ok());
                client.loop(3);
                udp.incoming = good;
                client.loop(4);
                CHECK(query.pending());
                udp.incoming = reply(udp, binding({2, 1, 7}));
                client.loop(5);
                CHECK(query[0].value.integer() == 7);
            }
        });
    add("timeout retries are bounded across clock rollover",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            device.timeoutMs = 10;
            device.retries = 1;
            SNMPQuery<1> query(device);
            CHECK(query.addOID(oid).ok());
            CHECK(client.begin().ok());
            CHECK(query.start().ok());
            client.loop(UINT32_MAX - 5);
            Bytes first = udp.outgoing;
            client.loop(4);
            CHECK(udp.packets == 2);
            CHECK(udp.outgoing == first);
            client.loop(14);
            CHECK(query.status().code() == SNMPStatus::Timeout);
            CHECK(query[0].status.code() == SNMPStatus::Timeout);
            CHECK(query.takeCompleted());
        });
    add("range registration is atomic and missing values stay explicit",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPQuery<2> query(device);
            CHECK(query.addRange(".1.3.6.1", UINT32_MAX, 2).code() == SNMPStatus::CapacityExceeded);
            CHECK(query.size() == 0);
            CHECK(query.addOID(oid, COUNTER32).ok());
            CHECK(client.begin().ok());
            CHECK(query.start().ok());
            client.loop(0);
            udp.incoming = reply(udp, binding({0x81, 0}));
            client.loop(1);
            CHECK(query.status().code() == SNMPStatus::Missing);
            CHECK(query.start().ok());
            client.loop(2);
            udp.incoming = reply(udp, binding({2, 1, 0}));
            client.loop(3);
            CHECK(query.status().code() == SNMPStatus::TypeMismatch);
        });
    add("large query batches without losing ordering",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPQuery<96> query(device);
            CHECK(query.addRange(".1.3.6.1.2.1.2.2.1.10", 1, 96, COUNTER32).ok());
            CHECK(client.begin().ok());
            CHECK(query.start().ok());
            client.loop(0);
            for (unsigned n = 1; query.pending() && n < 100; ++n)
            {
                SNMPGetResponse request;
                CHECK(request.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
                Bytes values;
                for (VarBindList *b = request.varBinds; b && b->value; b = b->next)
                {
                    Bytes value = tlv(0x30, join({encode(*b->value->oid), {0x41, 1, 9}}));
                    values.insert(values.end(), value.begin(), value.end());
                }
                udp.incoming = reply(udp, values);
                client.loop(n);
            }
            CHECK(query.status().ok());
            CHECK(udp.packets > 1);
            for (size_t i = 0; i < query.size(); ++i)
                CHECK(query[i].value.unsigned32() == 9);
        });
    add("SET preserves signed and binary values and is not retried",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPSet<2> write(device);
            CHECK(write.addValue(oid, SNMPValue::integer32(-7)).ok());
            CHECK(client.begin().ok());
            CHECK(write.start().ok());
            client.loop(0);
            SNMPGetResponse request;
            CHECK(request.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
            CHECK(request.requestType == SetRequestPDU);
            CHECK(static_cast<int32_t>(
                      static_cast<IntegerType *>(request.varBinds->value->value)->_value) == -7);
            client.loop(1000);
            CHECK(write.status().code() == SNMPStatus::Timeout);
            CHECK(udp.packets == 1);
        });
    add("walks use version appropriate requests and numeric OID progression",
        []
        {
            for (SNMPVersion version : {SNMPVersion::Version1, SNMPVersion::Version2c})
            {
                UDP udp;
                SNMPClient client(udp);
                SNMPDevice device(client, udp.peer, "public", version);
                SNMPWalk<4> walk(device);
                CHECK(walk.configure(".1.3.6.1.2.1.1").ok());
                CHECK(client.begin().ok());
                CHECK(walk.start().ok());
                client.loop(0);
                SNMPGetResponse request;
                CHECK(request.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
                CHECK(request.requestType ==
                      (version == SNMPVersion::Version1 ? GetNextRequestPDU : GetBulkRequestPDU));
                udp.incoming = reply(udp, binding({2, 1, 4}));
                client.loop(1);
                CHECK(walk.pending());
                CHECK(walk.size() == 1);
                udp.incoming = reply(udp, binding({2, 1, 4}));
                client.loop(2);
                CHECK(walk.status().code() == SNMPStatus::ProtocolError);
                CHECK(walk.start().ok());
                client.loop(3);
                udp.incoming = version == SNMPVersion::Version1 ? reply(udp, binding({5, 0}), 2)
                                                                : reply(udp, binding({0x82, 0}));
                client.loop(4);
                CHECK(walk.status().ok());
                CHECK(walk.size() == 0);
            }
        });
    add("walk capacity and table sparse indices remain explicit",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPTableRead<3, 2> table(device);
            CHECK(table.addColumn(".1.3.6.1.2.1.2.2.1.10").ok());
            CHECK(table.addColumn(".1.3.6.1.2.1.2.2.1.16").ok());
            CHECK(client.begin().ok());
            CHECK(table.start().ok());
            client.loop(0);
            OIDType first(const_cast<char *>(".1.3.6.1.2.1.2.2.1.10.7"));
            udp.incoming = reply(udp, tlv(0x30, join({encode(first), {0x41, 1, 8}})));
            client.loop(1);
            udp.incoming = reply(udp, binding({0x82, 0}));
            client.loop(2);
            OIDType second(const_cast<char *>(".1.3.6.1.2.1.2.2.1.16.42"));
            udp.incoming = reply(udp, tlv(0x30, join({encode(second), {0x41, 1, 9}})));
            client.loop(3);
            udp.incoming = reply(udp, binding({0x82, 0}));
            client.loop(4);
            CHECK(table.takeCompleted());
            CHECK(table.status().code() == SNMPStatus::Partial);
            CHECK(table.size() == 2);
            CHECK(std::string(table[0].index) == "7");
            CHECK(table[0][0].ok());
            CHECK(!table[0][1].ok());
            CHECK(std::string(table[1].index) == "42");
            CHECK(table[1][1].ok());
        });
    add("INFORM is acknowledged after acceptance and traps are not acknowledged",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            unsigned calls = 0;
            CHECK(client.begin(162).ok());
            CHECK(client
                      .notifications(
                          "public",
                          [](const SNMPNotification &n, void *p)
                          {
                              ++*static_cast<unsigned *>(p);
                              CHECK(n.size() == 2);
                              CHECK(n.uptime == 3);
                              return true;
                          },
                          &calls)
                      .ok());
            OIDType uptime(const_cast<char *>(".1.3.6.1.2.1.1.3.0"));
            OIDType trap(const_cast<char *>(".1.3.6.1.6.3.1.1.4.1.0"));
            Bytes values = join({tlv(0x30, join({encode(uptime), {0x43, 1, 3}})),
                                 tlv(0x30, join({encode(trap), oidWire}))});
            udp.incoming = message(values, 1, "public", InformRequestPDU, 55);
            client.loop(0);
            CHECK(calls == 1);
            CHECK(udp.packets == 1);
            SNMPGetResponse ack;
            CHECK(ack.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
            CHECK(ack.requestType == GetResponsePDU);
            CHECK(ack.requestID == 55);
            udp.incoming = message(values, 1, "wrong", Trapv2PDU);
            client.loop(1);
            CHECK(calls == 1);
            udp.incoming = message(values, 1, "public", Trapv2PDU);
            client.loop(2);
            CHECK(calls == 2);
            CHECK(udp.packets == 1);
        });

    add("table uses Counter32 fallback when high capacity column is absent",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPTableRead<2, 1> table(device);
            CHECK(table.addColumn(".1.3.6.1.4.1.99.1", COUNTER64, ".1.3.6.1.4.1.99.2", COUNTER32)
                      .ok());
            CHECK(client.begin().ok());
            CHECK(table.start().ok());
            client.loop(0);
            // No high-capacity column, then a sparse Counter32 fallback.
            udp.incoming = reply(udp, binding({0x82, 0}));
            client.loop(1);
            OIDType name(const_cast<char *>(".1.3.6.1.4.1.99.2.42"));
            udp.incoming = reply(udp, tlv(0x30, join({encode(name), {0x41, 1, 9}})));
            client.loop(2);
            udp.incoming = reply(udp, binding({0x82, 0}));
            client.loop(3);
            CHECK(table.status().ok());
            CHECK(table.size() == 1);
            CHECK(table[0][0].value.type == COUNTER32);
            CHECK(table[0][0].value.unsigned32() == 9);
        });
    add("tooBig reduces reads but never splits SET",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPQuery<2> query(device);
            CHECK(query.addRange(".1.3.6.1.4.1.99", 1, 2).ok());
            CHECK(client.begin().ok());
            CHECK(query.start().ok());
            client.loop(0);
            udp.incoming = reply(udp, {}, 1);
            client.loop(1);
            SNMPGetResponse smaller;
            CHECK(smaller.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
            CHECK(smaller.varBinds->value);
            CHECK(!smaller.varBinds->next->value);
            query.cancel();
            SNMPSet<1> write(device);
            CHECK(write.addValue(oid, SNMPValue::integer32(3)).ok());
            CHECK(write.start().ok());
            client.loop(2);
            int packets = udp.packets;
            udp.incoming = reply(udp, {}, 1);
            client.loop(3);
            CHECK(write.status().code() == SNMPStatus::CapacityExceeded);
            CHECK(udp.packets == packets);
        });
    add("SNMPv1 traps preserve enterprise and agent metadata",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            unsigned calls = 0;
            CHECK(client.begin(162).ok());
            client.notifications(
                "public",
                [](const SNMPNotification &n, void *context)
                {
                    ++*static_cast<unsigned *>(context);
                    CHECK(n.version == SNMPVersion::Version1);
                    CHECK(!n.inform);
                    CHECK(n.genericTrap == 6);
                    CHECK(n.specificTrap == 5);
                    CHECK(n.uptime == 7);
                    CHECK(n.agentAddress == IPAddress(1, 2, 3, 4));
                    CHECK(n.size() == 1);
                    return true;
                },
                &calls);
            Bytes packet = tlv(0x30, join({{2, 1, 0},
                                           tlv(4, {'p', 'u', 'b', 'l', 'i', 'c'}),
                                           tlv(0xa4, join({oidWire,
                                                           {0x40, 4, 1, 2, 3, 4},
                                                           {2, 1, 6},
                                                           {2, 1, 5},
                                                           {0x43, 1, 7},
                                                           tlv(0x30, binding({2, 1, 9}))}))}));
            udp.incoming = packet;
            client.loop(0);
            CHECK(calls == 1);
            CHECK(udp.packets == 0);
            for (Bytes invalid :
                 {Bytes{0x46, 1, 1}, Bytes{0x80, 0}, Bytes{0x81, 0}, Bytes{0x82, 0}})
            {
                udp.incoming = tlv(0x30, join({{2, 1, 0},
                                               tlv(4, {'p', 'u', 'b', 'l', 'i', 'c'}),
                                               tlv(0xa4, join({oidWire,
                                                               {0x40, 4, 1, 2, 3, 4},
                                                               {2, 1, 6},
                                                               {2, 1, 5},
                                                               {0x43, 1, 7},
                                                               tlv(0x30, binding(invalid))}))}));
                client.loop(1);
                CHECK(calls == 1 && udp.packets == 0);
            }
        });
    add("stream cancellation and queue exhaustion release pending slots",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            CHECK(client.begin().ok());
            SNMPWalk<1> walk(device);
            CHECK(walk.configure(".1.3.6.1.2.1.1").ok());
            walk.stream(
                [](const SNMPResult &, void *context)
                {
                    static_cast<SNMPWalk<1> *>(context)->cancel();
                    return true;
                },
                &walk);
            CHECK(walk.start().ok());
            client.loop(0);
            udp.incoming = reply(udp, binding({2, 1, 1}));
            client.loop(1);
            CHECK(walk.status().code() == SNMPStatus::Cancelled);
            CHECK(walk.start().ok());
            walk.cancel();
            std::vector<SNMPQuery<1> *> reads;
            for (unsigned i = 0; i < SNMP_MAX_PENDING_REQUESTS; ++i)
            {
                auto *read = new SNMPQuery<1>(device);
                reads.push_back(read);
                CHECK(read->addOID(oid).ok());
                CHECK(read->start().ok());
            }
            SNMPQuery<1> extra(device);
            CHECK(extra.addOID(oid).ok());
            CHECK(extra.start().code() == SNMPStatus::CapacityExceeded);
            for (auto *read : reads)
                delete read;
            CHECK(extra.start().ok());
            extra.cancel();
        });
    add("independent devices cannot consume each other's responses",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice first(client, "192.168.1.10", "first");
            SNMPDevice second(client, "192.168.1.11", "second", SNMPVersion::Version1);
            SNMPQuery<1> a(first), b(second);
            CHECK(a.addOID(oid, INTEGER).ok());
            CHECK(b.addOID(oid, INTEGER).ok());
            CHECK(client.begin().ok());
            CHECK(a.start().ok());
            CHECK(b.start().ok());
            client.loop(0);
            Bytes secondReply = reply(udp, binding({2, 1, 8}));
            udp.incoming = secondReply;
            client.loop(1);
            CHECK(a.pending());
            CHECK(b.pending());
            udp.peer = IPAddress(192, 168, 1, 11);
            udp.incoming = secondReply;
            client.loop(2);
            CHECK(b.status().ok());
            CHECK(a.pending());
            udp.peer = IPAddress(192, 168, 1, 10);
            udp.incoming = message(binding({2, 1, 9}), 1, "wrong", GetResponsePDU, 1);
            client.loop(3);
            CHECK(a.pending());
            udp.incoming = message(binding({2, 1, 9}), 0, "first", GetResponsePDU, 1);
            client.loop(4);
            CHECK(a.pending());
            udp.incoming = message(binding({2, 1, 9}), 1, "first", GetResponsePDU, 1);
            client.loop(5);
            CHECK(a.status().ok());
            CHECK(a[0].value.integer() == 9);
            CHECK(b[0].value.integer() == 8);
        });
    add("allocation failure cannot publish an incomplete response",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            SNMPQuery<1> query(device);
            CHECK(query.addOID(oid).ok());
            CHECK(client.begin().ok());
            CHECK(query.start().ok());
            client.loop(0);
            Bytes good = reply(udp, binding({2, 1, 42}));
            udp.incoming = good;
            {
                FailAllocations fail(0);
                client.loop(1);
            }
            CHECK(query.pending());
            CHECK(!query[0].ok());
            udp.incoming = good;
            client.loop(2);
            CHECK(query.status().ok());
            CHECK(query[0].value.integer() == 42);
        });
    add("SET binary values preserve embedded zero and oversized writes never send",
        []
        {
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            CHECK(client.begin().ok());
            SNMPSet<1> write(device);
            SNMPValue value;
            unsigned char bytes[] = {'a', 0, 'b'};
            CHECK(value.setBytes(bytes, sizeof(bytes)).ok());
            CHECK(write.addValue(oid, value).ok());
            CHECK(write.start().ok());
            client.loop(0);
            SNMPGetResponse request;
            CHECK(request.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
            auto *octets = static_cast<OctetType *>(request.varBinds->value->value);
            CHECK(octets->getLength() == 3);
            CHECK(!memcmp(octets->_value, bytes, 3));
            write.cancel();
            SNMPSet<96> large(device);
            for (unsigned i = 0; i < 96; ++i)
            {
                std::string name = ".1.3.6.1.4.1.99." + std::to_string(i);
                CHECK(large.addValue(name.c_str(), value).ok());
            }
            CHECK(large.start().ok());
            int packets = udp.packets;
            client.loop(1);
            CHECK(large.status().code() == SNMPStatus::CapacityExceeded);
            CHECK(udp.packets == packets);
        });
}
