#include <SNMPClient.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

// Bridge the test transport's complete datagrams to a real loopback UDP socket.
// No BER response is manufactured here; Net-SNMP independently handles the wire.
struct Bridge
{
    UDP udp;
    int socketFD = -1, sent = 0;
    sockaddr_in address{};
    explicit Bridge(unsigned port)
    {
        socketFD = socket(AF_INET, SOCK_DGRAM, 0);
        if (socketFD < 0)
            throw std::runtime_error("socket");
        fcntl(socketFD, F_SETFL, O_NONBLOCK);
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        udp.peer = IPAddress(127, 0, 0, 1);
        udp.peerPort = port;
    }
    ~Bridge()
    {
        close(socketFD);
    }
    void service()
    {
        if (sent != udp.packets)
        {
            sent = udp.packets;
            if (sendto(socketFD, udp.outgoing.data(), udp.outgoing.size(), 0,
                       reinterpret_cast<sockaddr *>(&address),
                       sizeof(address)) != static_cast<ssize_t>(udp.outgoing.size()))
                throw std::runtime_error("sendto");
        }
        if (!udp.incoming.empty())
            return;
        unsigned char data[65536];
        sockaddr_in peer{};
        socklen_t size = sizeof(peer);
        ssize_t n =
            recvfrom(socketFD, data, sizeof(data), 0, reinterpret_cast<sockaddr *>(&peer), &size);
        if (n > 0 && peer.sin_addr.s_addr == address.sin_addr.s_addr &&
            peer.sin_port == address.sin_port)
            udp.incoming.assign(data, data + n);
    }
};
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void complete(SNMPClient &client, Bridge &bridge, SNMPOperation &operation)
{
    require(operation.start().ok(), "start");
    const auto started = std::chrono::steady_clock::now();
    while (operation.pending())
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - started)
                      .count();
        require(ms < 10000, "deadline");
        client.loop(static_cast<uint32_t>(ms));
        bridge.service();
        usleep(1000);
    }
    require(operation.status().ok(), operation.status().message());
}
bool collect(const SNMPResult &result, void *context)
{
    require(result.ok(), result.status.message());
    auto &oids = *static_cast<std::vector<std::string> *>(context);
    oids.push_back(result.oid);
    return oids.size() <= 1024;
}
int main(int argc, char **argv)
{
    try
    {
        require(argc == 2, "supply isolated agent port");
        unsigned port = std::stoul(argv[1]);
        require(port > 0 && port <= 65535, "port");
        Bridge bridge(port);
        SNMPClient client(bridge.udp);
        require(client.begin().ok(), "begin");
        std::vector<std::string> next, bulk;
        for (auto version : {SNMPVersion::Version1, SNMPVersion::Version2c})
        {
            SNMPDevice device(client, bridge.udp.peer, "interop", version);
            device.port = port;
            SNMPQuery<2> query(device);
            require(query.addOID(".1.3.6.1.2.1.1.2.0", OID).ok(), "add object ID");
            require(query.addOID(".1.3.6.1.2.1.1.3.0", TIMESTAMP).ok(), "add uptime");
            complete(client, bridge, query);
            require(query[0].ok() && query[1].ok(), "typed GET");
            SNMPWalk<1> walk(device);
            auto &values = version == SNMPVersion::Version1 ? next : bulk;
            require(walk.configure(".1.3.6.1.2.1.1").ok(), "configure");
            require(walk.stream(collect, &values).ok(), "stream");
            complete(client, bridge, walk);
            SNMPSet<1> write(device);
            SNMPValue value;
            require(value.setBytes(reinterpret_cast<const unsigned char *>("interop lab"), 11).ok(),
                    "value");
            require(write.addValue(".1.3.6.1.2.1.1.6.0", value).ok(), "add SET");
            complete(client, bridge, write);
            SNMPQuery<1> readback(device);
            require(readback.addOID(".1.3.6.1.2.1.1.6.0", STRING).ok(), "readback setup");
            complete(client, bridge, readback);
            require(readback[0].ok() && std::string(readback[0].value.text()) == "interop lab",
                    "readback value");
        }
        require(!next.empty() && next == bulk, "GETNEXT/GETBULK instance sets differ");
        std::cout << "PASS Net-SNMP v1/v2c typed GET, GETNEXT/GETBULK equivalence, SET/read-back\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
