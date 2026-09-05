#ifndef SNMPGet_h
#define SNMPGet_h

#include "BER.h"
#include "ValueCallbacks.h"
#include "SNMPProtocol.h"

class SNMPGet
{
public:
    SNMPGet(const char *community, SNMPVersion version);
    ~SNMPGet()
    {
        releaseCallbacks();
        delete packet;
    }
    SNMPGet(const SNMPGet &) = delete;
    SNMPGet &operator=(const SNMPGet &) = delete;
    SNMPGet(SNMPGet &&other);
    void releaseCallbacks();
    const char *_community;
    SNMPVersion _version;
    uint16_t port = 161;
    int32_t requestID = 0;

    // Configure the request ID, port, and transport.

    void setRequestID(int32_t request)
    {
        requestID = request;
    }

    void setPort(uint16_t portnumber)
    {
        port = portnumber;
    }

    void setUDP(UDP *udp)
    {
        _udp = udp;
    }

    bool addOIDPointer(ValueCallback *callback);
    ValueCallbacks *callbacks = new (std::nothrow) ValueCallbacks();
    ValueCallbacks *callbacksCursor = callbacks;

    UDP *_udp = 0;
    bool sendTo(IPAddress ip);

    ComplexType *packet = 0;
    bool build();

    void clearOIDList();
    // Abandon outstanding requests for the callbacks currently in this request.
    void cancelPendingRequests();
};

#endif
