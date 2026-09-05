#pragma once
#include "Arduino.h"
#include <vector>
#include <algorithm>
// Only the UDP surface used by this library; no sockets or board dependencies.
class UDP {
public:
    std::vector<uint8_t> incoming, outgoing;
    IPAddress peer{192,168,1,10}, destination;
    uint16_t listenPort=0, destinationPort=0;
    int stops=0, flushes=0, reads=0, packets=0;
    int endResult=1;
    int beginPacketResult=1;
    size_t writeLimit=static_cast<size_t>(-1);
    uint8_t beginResult=1;
    uint8_t begin(uint16_t p) { listenPort=p; return beginResult; }
    void stop() { ++stops; }
    int parsePacket() { return static_cast<int>(incoming.size()); }
    int read(unsigned char* p, size_t n) { ++reads; n=std::min(n,incoming.size()); std::copy_n(incoming.begin(),n,p); return static_cast<int>(n); }
    void flush() { ++flushes; incoming.clear(); }
    IPAddress remoteIP() { return peer; }
    int beginPacket(IPAddress ip, uint16_t p) { ++packets; destination=ip; destinationPort=p; outgoing.clear(); return beginPacketResult; }
    size_t write(const unsigned char* p, size_t n) { n=std::min(n,writeLimit); outgoing.insert(outgoing.end(),p,p+n); return n; }
    int endPacket() { return endResult; }
};
