#ifndef SNMP_VALUE_CALLBACKS_H
#define SNMP_VALUE_CALLBACKS_H

#include "BER.h"
#include <Udp.h>

class ValueCallback
{
public:
    ValueCallback(ASN_TYPE atype) : type(atype) {};
    // Registrations own their OID; destinations and transports remain caller-owned.
    virtual ~ValueCallback()
    {
        free(OID);
    }
    uint32_t updateCount() const
    {
        return updateSerial;
    }
    bool matches(int32_t id, UDP *udp, IPAddress peer) const;
    void retain()
    {
        ++references;
    }
    void release()
    {
        if (--references == 0)
            delete this;
    }
    ValueCallback(const ValueCallback &) = delete;
    ValueCallback &operator=(const ValueCallback &) = delete;

private:
    friend class SNMPManager;
    uint32_t updateSerial = 0;
    size_t references = 1;

public:
    IPAddress ip;
    char *OID = nullptr;
    ASN_TYPE type;
    bool overwritePrefix = false;
    // Legacy summary fields describe the most recent send and whether any reply is pending.
    bool requestTracked = false;
    bool requestPending = false;
    int32_t expectedRequestID = 0;
    UDP *requestUDP = nullptr;
    IPAddress requestPeer;

private:
    bool trackingEnabled = false;
    struct PendingRequest
    {
        bool active = false;
        int32_t id = 0;
        UDP *udp = nullptr;
        IPAddress peer;
    };
    PendingRequest pending[SNMP_MAX_PENDING_REQUESTS];

public:
    bool hasTrackedRequests() const
    {
        return trackingEnabled;
    }
    bool canTrack(int32_t id, UDP *udp, IPAddress peer) const;
    void track(int32_t id, UDP *udp, IPAddress peer);
    bool consume(int32_t id, UDP *udp, IPAddress peer);
    // Explicitly abandon lost/timed-out requests; tracked callbacks still reject unsolicited
    // replies.
    void clearPendingRequests();
};

class IntegerCallback : public ValueCallback
{
public:
    IntegerCallback() : ValueCallback(INTEGER) {};
    int32_t *value = nullptr;
    float *floatValue = nullptr;
    bool isFloat = false;
};

class TimestampCallback : public ValueCallback
{
public:
    TimestampCallback() : ValueCallback(TIMESTAMP) {};
    uint32_t *value;
};

class StringCallback : public ValueCallback
{
public:
    StringCallback(ASN_TYPE type = STRING) : ValueCallback(type) {};
    char **value = nullptr;
    unsigned char *bytes = nullptr;
    size_t *length = nullptr;
    size_t capacity = static_cast<size_t>(-1);
};

class OIDCallback : public ValueCallback
{
public:
    OIDCallback() : ValueCallback(ASN_TYPE::OID) {};
    char *value;
    size_t capacity = static_cast<size_t>(-1);
};

class Counter32Callback : public ValueCallback
{
public:
    Counter32Callback() : ValueCallback(ASN_TYPE::COUNTER32) {};
    uint32_t *value;
};

class Gauge32Callback : public ValueCallback
{
public:
    Gauge32Callback() : ValueCallback(ASN_TYPE::GAUGE32) {};
    uint32_t *value;
};

class Counter64Callback : public ValueCallback
{
public:
    Counter64Callback() : ValueCallback(ASN_TYPE::COUNTER64) {};
    uint64_t *value;
};

typedef struct ValueCallbackList
{
    ~ValueCallbackList();
    ValueCallback *value = nullptr;
    struct ValueCallbackList *next = 0;
} ValueCallbacks;

#endif
