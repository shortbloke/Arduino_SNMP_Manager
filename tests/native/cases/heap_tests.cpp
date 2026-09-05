#include "mock_agent.h"
#include "registry.h"
#include <SNMPTable.h>

namespace
{
const char *root = ".1.3.6.1.2.1.2.2.1.2";
const char *instance = ".1.3.6.1.2.1.2.2.1.2.7";
template <class Operation>
void complete(SNMPClient &client, UDP &udp, MockAgent &agent, Operation &operation)
{
    for (uint32_t now = 100; operation.pending() && now < 10000; now += 10)
    {
        client.loop(now);
        agent.service(udp);
    }
    CHECK(operation.status().ok());
}
}
void registerHeapTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"Heap", name, run}); };
    add("every response allocation boundary fails safely and query recovers",
        []
        {
            bool exhausted = false, retentionFailed = false;
            for (int allowance = 0; allowance < 256; ++allowance)
            {
                UDP udp;
                MockAgent agent;
                agent.put(instance, tlv(4, Bytes(120, 'x')));
                SNMPClient client(udp);
                SNMPDevice device(client, udp.peer, "public");
                SNMPQuery<1> query(device);
                CHECK(query.addOID(instance, STRING).ok());
                CHECK(client.begin().ok() && query.start().ok());
                client.loop(0);
                CHECK(agent.service(udp));
                unsigned failed;
                {
                    FailAllocations fail(allowance);
                    client.loop(1);
                    failed = fail.failures();
                }
                retentionFailed |= query.status().code() == SNMPStatus::AllocationFailure;
                if (query[0].ok())
                    CHECK(query[0].value.length == 120 && query[0].value.bytes[119] == 'x');
                else
                    CHECK(query[0].value.length == 0);
                if (failed)
                    CHECK(!query.status().ok());
                query.cancel();
                CHECK(query.start().ok());
                complete(client, udp, agent, query);
                CHECK(query[0].ok() && query[0].value.length == 120);
                if (!failed)
                {
                    exhausted = true;
                    break;
                }
            }
            CHECK(exhausted && retentionFailed);
        });
    add("walk and table allocation sweeps preserve valid cells and allow restart",
        []
        {
            for (bool useTable : {false, true})
            {
                bool exhausted = false, cellFailure = false;
                for (int allowance = 0; allowance < 256; ++allowance)
                {
                    UDP udp;
                    MockAgent agent;
                    agent.put(instance, tlv(4, Bytes(120, 'x')));
                    SNMPClient client(udp);
                    SNMPDevice device(client, udp.peer, "public");
                    SNMPWalk<2> walk(device);
                    SNMPTableRead<2, 1> table(device);
                    CHECK(walk.configure(root).ok());
                    CHECK(table.addColumn(root, STRING).ok());
                    CHECK(client.begin().ok());
                    CHECK((useTable ? table.start() : walk.start()).ok());
                    client.loop(0);
                    CHECK(agent.service(udp));
                    unsigned failed;
                    {
                        FailAllocations fail(allowance);
                        client.loop(1);
                        failed = fail.failures();
                    }
                    const size_t count = useTable ? table.size() : walk.size();
                    CHECK(count <= 1);
                    if (count)
                    {
                        auto status = useTable ? table[0][0].status : walk[0].status;
                        const auto &value = useTable ? table[0][0].value : walk[0].value;
                        if (status.ok())
                            CHECK(value.length == 120 && value.bytes[119] == 'x');
                        else
                        {
                            CHECK(status.code() == SNMPStatus::AllocationFailure);
                            CHECK(value.length == 0);
                            cellFailure = true;
                        }
                    }
                    if (useTable)
                    {
                        table.cancel();
                        CHECK(table.start().ok());
                        complete(client, udp, agent, table);
                        CHECK(table.size() == 1 && table[0][0].ok());
                    }
                    else
                    {
                        walk.cancel();
                        CHECK(walk.start().ok());
                        complete(client, udp, agent, walk);
                        CHECK(walk.size() == 1 && walk[0].ok());
                    }
                    if (!failed)
                    {
                        exhausted = true;
                        break;
                    }
                }
                CHECK(exhausted && cellFailure);
            }
        });
    add("INFORM allocation failures never acknowledge unread values and retry recovers",
        []
        {
            OIDType uptime(const_cast<char *>(".1.3.6.1.2.1.1.3.0"));
            OIDType trap(const_cast<char *>(".1.3.6.1.6.3.1.1.4.1.0"));
            Bytes values =
                join({tlv(0x30, join({encode(uptime), {0x43, 1, 3}})),
                      tlv(0x30, join({encode(trap), oidWire})), binding(tlv(4, Bytes(80, 'x')))});
            Bytes packet = message(values, 1, "public", InformRequestPDU, 55);
            bool exhausted = false, readFailed = false;
            for (int allowance = 0; allowance < 512; ++allowance)
            {
                UDP udp;
                SNMPClient client(udp);
                struct State
                {
                    unsigned accepted = 0, rejected = 0;
                } state;
                CHECK(client.begin(162).ok());
                CHECK(client
                          .notifications(
                              "public",
                              [](const SNMPNotification &n, void *context)
                              {
                                  auto &state = *static_cast<State *>(context);
                                  SNMPResult value;
                                  if (!n.read(2, value).ok())
                                  {
                                      ++state.rejected;
                                      return false;
                                  }
                                  CHECK(value.value.length == 80);
                                  ++state.accepted;
                                  return true;
                              },
                              &state)
                          .ok());
                udp.incoming = packet;
                unsigned failed;
                {
                    FailAllocations fail(allowance);
                    client.loop(0);
                    failed = fail.failures();
                }
                readFailed |= state.rejected != 0;
                if (udp.packets)
                    CHECK(state.accepted == 1 && state.rejected == 0);
                const int before = udp.packets;
                udp.incoming = packet; // Sender retries, including after ACK-encoding failure.
                client.loop(1);
                CHECK(udp.packets == before + 1);
                SNMPGetResponse ack;
                CHECK(ack.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
                CHECK(ack.requestType == GetResponsePDU && ack.requestID == 55);
                if (!failed)
                {
                    exhausted = true;
                    break;
                }
            }
            CHECK(exhausted && readFailed);
        });
    add("limited contiguous allocation preserves snapshots and SET never sends partial data",
        []
        {
            SNMPValue value;
            const unsigned char small[] = {'o', 'l', 'd'};
            CHECK(value.setBytes(small, sizeof(small)).ok());
            SNMPValue snapshot = value;
            Bytes large(120, 'x');
            {
                // Model fragmentation: small allocations succeed but large ones fail.
                FailAllocations fail(-1, 64);
                CHECK(value.setBytes(large.data(), large.size()).code() ==
                      SNMPStatus::AllocationFailure);
                CHECK(value.bytes == snapshot.bytes && value.length == 3);
                CHECK(value.setBytes(small, sizeof(small)).ok());
                CHECK(fail.failures() == 1);
            }
            CHECK(snapshot.length == 3 && snapshot.bytes[0] == 'o');
            UDP udp;
            SNMPClient client(udp);
            SNMPDevice device(client, udp.peer, "public");
            device.timeoutMs = 10;
            SNMPSet<1> write(device);
            CHECK(write.addValue(instance, value).ok());
            CHECK(client.begin().ok() && write.start().ok());
            {
                FailAllocations fail(0);
                client.loop(0);
                // Encoding an already prepared SET needs no library allocation.
                CHECK(fail.failures() == 0);
            }
            CHECK(udp.packets == 1 && write.pending());
            SNMPGetResponse sent;
            CHECK(sent.parseFrom(udp.outgoing.data(), udp.outgoing.size()));
            CHECK(sent.requestType == SetRequestPDU);
            CHECK(sent.varBinds && sent.varBinds->value->type == STRING);
            CHECK(std::string(static_cast<OctetType *>(sent.varBinds->value->value)->_value) ==
                  "old");
            Bytes bindings =
                tlv(0x30,
                    join({MockAgent::wireOID(MockAgent::oid(instance)), tlv(4, {'o', 'l', 'd'})}));
            udp.incoming = message(bindings, 1, "public", GetResponsePDU, sent.requestID);
            {
                FailAllocations fail(0);
                client.loop(1); // Cannot decode the reply under sustained exhaustion.
                CHECK(write.pending() && fail.failures() > 0);
                client.loop(20);
            }
            CHECK(write.status().code() == SNMPStatus::Timeout);
            CHECK(udp.packets == 1); // Outcome is unknown: never automatically repeat a SET.
            CHECK(write.start().ok());
            client.loop(21);
            CHECK(udp.packets == 2 && write.pending());
            write.cancel();
        });
}
