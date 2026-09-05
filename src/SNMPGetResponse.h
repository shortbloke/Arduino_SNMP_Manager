#ifndef SNMPGetResponse_h
#define SNMPGetResponse_h

#include "BER.h"
#include "VarBinds.h"
#include "SNMPProtocol.h"

class SNMPGetResponse
{

public:
    SNMPGetResponse() {};
    SNMPGetResponse(const SNMPGetResponse &) = delete;
    SNMPGetResponse &operator=(const SNMPGetResponse &) = delete;
    ~SNMPGetResponse()
    {
        delete varBinds;
        delete SNMPPacket;
    };
    char *communityString = nullptr;
    size_t communityLength = 0;
    int version = 0;
    ASN_TYPE requestType = STRUCTURE;
    unsigned long requestID = 0;
    int errorStatus = 0;
    int errorIndex = 0;
    VarBindList *varBinds = 0;
    VarBindList *varBindsCursor = 0;

    ComplexType *SNMPPacket = 0;
    bool parseFrom(unsigned char *buf, size_t available = static_cast<size_t>(-1));
    enum SNMPExpect EXPECTING = SNMPVERSION;
    bool isCorrupt = false;
};

#endif
