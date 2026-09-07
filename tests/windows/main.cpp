// Read-only live validation for configured Windows or Ubuntu SNMP agents.
#include <SNMPClient.h>
#include <SNMPTable.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct SocketBridge
{
    UDP udp;
    int fd = -1, sent = 0;
    sockaddr_in peer{};

    explicit SocketBridge(const char *host)
    {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0 || inet_pton(AF_INET, host, &peer.sin_addr) != 1)
            throw std::runtime_error("host must be an IPv4 address");
        fcntl(fd, F_SETFL, O_NONBLOCK);
        peer.sin_family = AF_INET;
        peer.sin_port = htons(161);
        const unsigned char *octets = reinterpret_cast<const unsigned char *>(&peer.sin_addr);
        udp.peer = IPAddress(octets[0], octets[1], octets[2], octets[3]);
        udp.peerPort = 161;
    }
    ~SocketBridge()
    {
        if (fd >= 0)
            close(fd);
    }
    void service()
    {
        if (sent != udp.packets)
        {
            sent = udp.packets;
            if (sendto(fd, udp.outgoing.data(), udp.outgoing.size(), 0,
                       reinterpret_cast<sockaddr *>(&peer), sizeof(peer)) !=
                static_cast<ssize_t>(udp.outgoing.size()))
                throw std::runtime_error("UDP send failed");
        }
        if (!udp.incoming.empty())
            return;
        unsigned char packet[65536];
        sockaddr_in source{};
        socklen_t length = sizeof(source);
        const ssize_t received = recvfrom(fd, packet, sizeof(packet), 0,
                                          reinterpret_cast<sockaddr *>(&source), &length);
        if (received > 0 && source.sin_addr.s_addr == peer.sin_addr.s_addr &&
            source.sin_port == peer.sin_port)
            udp.incoming.assign(packet, packet + received);
    }
};

static void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
static bool matches(const char *actual, const char *expected)
{
    return !std::strcmp(expected, "-") || !std::strcmp(actual, expected);
}
template <typename Operation>
static void waitForTerminal(SNMPClient &client, SocketBridge &bridge, Operation &operation)
{
    const auto started = std::chrono::steady_clock::now();
    while (operation.pending())
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        require(elapsed < 10000, "query timed out");
        client.loop(static_cast<uint32_t>(elapsed));
        bridge.service();
        usleep(1000);
    }
}
template <typename Operation>
static void waitForCompletion(SNMPClient &client, SocketBridge &bridge, Operation &operation)
{
    waitForTerminal(client, bridge, operation);
    require(operation.status().ok(), operation.status().message());
}
static void finish(SNMPClient &client, SocketBridge &bridge, SNMPOperation &operation)
{
    require(operation.start().ok(), "could not start operation");
    waitForCompletion(client, bridge, operation);
}
struct WalkValues
{
    std::vector<std::string> oids;
    bool stringsOnly = true;
    bool trailingNul = false;
};
static bool collectInterface(const SNMPResult &result, void *context)
{
    auto &values = *static_cast<WalkValues *>(context);
    values.stringsOnly = values.stringsOnly && result.ok() && result.value.type == STRING &&
                          result.value.length;
    values.trailingNul = values.trailingNul ||
                         (result.value.length && result.value.bytes[result.value.length - 1] == 0);
    values.oids.push_back(result.oid);
    return values.oids.size() <= 128;
}

int main(int argc, char **argv)
{
    try
    {
        require(argc == 6, "usage: live-snmp HOST COMMUNITY EXPECTED_NAME EXPECTED_LOCATION EXPECTED_SERVICES");
        SocketBridge bridge(argv[1]);
        SNMPClient client(bridge.udp);
        require(client.begin().ok(), "client setup failed");
        SNMPDevice server(client, bridge.udp.peer, argv[2], SNMPVersion::Version2c);
        server.timeoutMs = 2000;
        server.retries = 0;
        std::string addressOID = ".1.3.6.1.2.1.4.20.1.1.";
        addressOID += argv[1]; // ipAdEntAddr for the test target itself.
        SNMPQuery<11> query(server);
        require(query.addOID(".1.3.6.1.2.1.1.1.0", STRING).ok(), "sysDescr setup");
        require(query.addOID(".1.3.6.1.2.1.1.2.0", OID).ok(), "sysObjectID setup");
        require(query.addOID(".1.3.6.1.2.1.1.3.0", TIMESTAMP).ok(), "sysUpTime setup");
        require(query.addOID(".1.3.6.1.2.1.1.4.0", STRING).ok(), "sysContact setup");
        require(query.addOID(".1.3.6.1.2.1.1.5.0", STRING).ok(), "sysName setup");
        require(query.addOID(".1.3.6.1.2.1.1.6.0", STRING).ok(), "sysLocation setup");
        require(query.addOID(".1.3.6.1.2.1.1.7.0", INTEGER).ok(), "sysServices setup");
        require(query.addOID(".1.3.6.1.2.1.2.1.0", INTEGER).ok(), "ifNumber setup");
#ifdef SNMP_LIVE_UBUNTU
        require(query.addOID(".1.3.6.1.2.1.31.1.1.1.15.1", GAUGE32).ok(), "ifHighSpeed setup");
        require(query.addOID(".1.3.6.1.2.1.31.1.1.1.6.1", COUNTER64).ok(), "ifHCInOctets setup");
#else
        require(query.addOID(".1.3.6.1.2.1.31.1.1.1.15.7", GAUGE32).ok(), "ifHighSpeed setup");
        require(query.addOID(".1.3.6.1.2.1.2.2.1.10.7", COUNTER32).ok(), "ifInOctets setup");
#endif
        require(query.addOID(addressOID.c_str(), NETWORK_ADDRESS).ok(), "ipAdEntAddr setup");
        finish(client, bridge, query);
        for (size_t index = 0; index < query.size(); ++index)
            require(query[index].ok(), query[index].status.message());
        require(query[0].value.isText() && query[0].value.length, "sysDescr is empty");
#ifdef SNMP_LIVE_UBUNTU
        require(!std::strncmp(query[1].value.text(), ".1.3.6.1.4.1.8072.", 18),
                "sysObjectID is not in the Net-SNMP enterprise branch");
#else
        require(!std::strncmp(query[1].value.text(), ".1.3.6.1.4.1.311.", 17),
                "sysObjectID is not in the Microsoft enterprise branch");
#endif
        require(query[2].value.unsigned32() != 0, "sysUpTime is zero");
        require(query[4].value.isText() && query[4].value.length && matches(query[4].value.text(), argv[3]),
                "sysName differs from the expected value");
        require(query[5].value.isText() && matches(query[5].value.text(), argv[4]),
                "sysLocation differs from the expected value");
        if (std::strcmp(argv[5], "-") != 0)
            require(query[6].value.integer() == std::strtol(argv[5], nullptr, 10),
                    "sysServices differs from the expected value");
        require(query[7].value.integer() > 0, "ifNumber is not positive");
        require(query[8].value.unsigned32() > 0, "ifHighSpeed.7 is not positive");
#ifdef SNMP_LIVE_UBUNTU
        require(query[9].value.type == COUNTER64, "ifHCInOctets.1 is not Counter64");
#else
        require(query[9].value.type == COUNTER32, "ifInOctets.7 is not Counter32");
#endif
        require(query[10].value.length == 4, "ipAdEntAddr is not IPv4");

        // The agent must report a missing instance explicitly, rather than
        // decoding an exception as a successful empty value.
        SNMPQuery<1> missing(server);
        require(missing.addOID(".1.3.6.1.2.1.1.999.0", INTEGER).ok(),
                "missing-object setup");
        require(missing.start().ok(), "missing-object start");
        waitForTerminal(client, bridge, missing);
        const bool missingHandled = missing.status().code() == SNMPStatus::Missing ||
                                    (missing.status().code() == SNMPStatus::ProtocolError &&
                                     missing.agentError() == 2);
        require(missingHandled && !missing[0].ok(),
                "missing-object response was not reported as an error");

        // Windows supports v1 and v2c. These operations exercise GETNEXT and GETBULK
        // independently while streaming the interface-description column.
        SNMPDevice v1(client, bridge.udp.peer, argv[2], SNMPVersion::Version1);
        v1.timeoutMs = server.timeoutMs;
        v1.retries = server.retries;
        WalkValues nextValues, bulkValues;
        SNMPWalk<1> next(v1), bulk(server);
        require(next.configure(".1.3.6.1.2.1.2.2.1.2").ok(), "GETNEXT walk setup");
        require(next.stream(collectInterface, &nextValues).ok(), "GETNEXT callback setup");
        finish(client, bridge, next);
        require(bulk.configure(".1.3.6.1.2.1.2.2.1.2").ok(), "GETBULK walk setup");
        require(bulk.stream(collectInterface, &bulkValues).ok(), "GETBULK callback setup");
        finish(client, bridge, bulk);
        require(nextValues.stringsOnly && bulkValues.stringsOnly,
                "interface walk returned an invalid description");
#ifndef SNMP_LIVE_UBUNTU
        // This Windows service encodes ifDescr with a trailing NUL inside its
        // OCTET STRING. Preserve it as binary rather than treating it as C text.
        require(nextValues.trailingNul && bulkValues.trailingNul,
                "Windows ifDescr trailing NUL was not preserved");
#endif
        require(nextValues.oids.size() == static_cast<size_t>(query[7].value.integer()),
                "GETNEXT interface count differs from ifNumber");
        require(nextValues.oids == bulkValues.oids, "GETNEXT and GETBULK interface sets differ");

        // This table prefers 64-bit interface counters. Windows falls back to
        // Counter32; Net-SNMP exposes its native Counter64 values.
        SNMPInterfaceRead<48, 16> interfaces(server, true);
        require(interfaces.start().ok(), "interface table setup");
        waitForCompletion(client, bridge, interfaces);
        require(interfaces.size() == nextValues.oids.size(), "interface table count differs from walk");
        size_t counter32Fallbacks = 0;
        for (const auto &row : interfaces)
        {
            require(row[0].ok() && row[0].value.type == STRING && row[0].value.length,
                    "interface table description is invalid");
            require(row[1].ok() && row[2].ok(), "interface table counter is missing");
            counter32Fallbacks += row[1].value.type == COUNTER32;
            counter32Fallbacks += row[2].value.type == COUNTER32;
        }
#ifdef SNMP_LIVE_UBUNTU
        require(counter32Fallbacks == 0, "unexpected Counter32 fallback on Net-SNMP");
        std::cout << "PASS Ubuntu SNMP: typed GET, GETNEXT, GETBULK, and "
#else
        require(counter32Fallbacks > 0, "expected Windows Counter32 fallback was not used");
        std::cout << "PASS Windows SNMP: typed GET, GETNEXT, GETBULK, and "
#endif
                  << interfaces.size() << " interface-table rows\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
