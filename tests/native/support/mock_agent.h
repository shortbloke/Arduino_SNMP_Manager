#pragma once
#include "fixtures.h"
#include <map>

// Test-only agent. OID ordering, BER decoding, and traversal are independent of
// the library under test. Values are supplied as complete primitive TLVs.
class MockAgent
{
public:
    using OID = std::vector<uint32_t>;
    struct Exchange
    {
        unsigned version;
        unsigned pdu;
        int32_t id;
        unsigned nonRepeaters, maxRepetitions;
        std::vector<OID> requested, returned;
        unsigned error = 0, errorIndex = 0;
        Bytes response;
    };
    std::vector<Exchange> exchanges;
    size_t responseLimit = 65535;
    bool dropNext = false, repeatNext = false, truncateNext = false;
    void put(const char *oid, Bytes value);
    Bytes answer(const Bytes &request);
    // Call after client.loop(). Exactly one transmitted datagram may be pending.
    bool service(UDP &udp);
    static OID oid(const char *text);
    static Bytes wireOID(const OID &oid);
    static Bytes integer(int32_t value);

private:
    std::map<OID, Bytes> values_;
    int processedPackets_ = 0;
};
