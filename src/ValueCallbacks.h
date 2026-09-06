#ifndef SNMP_VALUE_CALLBACKS_H
#define SNMP_VALUE_CALLBACKS_H

#include "BER.h"
#include <Udp.h>

class ValueCallback
{
public:
    /**
     * @brief Create a registration with one ownership reference.
     * @param atype Expected SNMP value type. Prefer manager factory methods to populate
     * destinations.
     */
    ValueCallback(ASN_TYPE atype) : type(atype) {};
    // Registrations own their OID; destinations and transports remain caller-owned.
    /**
     * @brief Free the owned OID text; caller-owned destinations and transports are not deleted.
     */
    virtual ~ValueCallback()
    {
        free(OID);
    }
    /**
     * @return Wrapping unsigned count of successful destination writes, including unchanged values.
     * @note Compare to a saved count for freshness; rejected values and tracked duplicate replies
     * do not advance it.
     */
    uint32_t updateCount() const
    {
        return updateSerial;
    }
    /**
     * @param id Request ID to find.
     * @param udp Borrowed transport identity to match.
     * @param peer Remote IPv4 address to match.
     * @return True if that exact request is active; does not consume it.
     */
    bool matches(int32_t id, UDP *udp, IPAddress peer) const;
    /**
     * @brief Add an ownership reference; pair it with release(). Returns no value.
     */
    void retain()
    {
        ++references;
    }
    /**
     * @brief Drop an ownership reference, deleting this callback when the count reaches zero.
     * @note Do not access this pointer after releasing its last reference. Returns no value.
     */
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
    /**
     * @return True once tracking has ever been enabled, even after cancellation/completion.
     * @note This is not a test for whether any active slot remains; it enforces late-reply
     * rejection.
     */
    bool hasTrackedRequests() const
    {
        return trackingEnabled;
    }
    /**
     * @param id Proposed request ID.
     * @param udp Borrowed transport identity.
     * @param peer Remote IPv4 address.
     * @return True if a free slot exists or the exact request is already tracked; makes no changes.
     */
    bool canTrack(int32_t id, UDP *udp, IPAddress peer) const;
    /**
     * @brief Record a successfully sent request, reusing an exact match when present.
     * @param id Sent request ID.
     * @param udp Borrowed transport identity, kept alive while requests use it.
     * @param peer Remote IPv4 address copied into tracking state.
     * @note Call canTrack() before sending. No free slot means no change; returns no value.
     */
    void track(int32_t id, UDP *udp, IPAddress peer);
    /**
     * @brief Remove matching active request entries and refresh the pending summary.
     * @param id Response request ID.
     * @param udp Borrowed transport identity to match.
     * @param peer Response source address.
     * @return True if a matching active entry was removed, false for unknown/duplicate replies.
     */
    bool consume(int32_t id, UDP *udp, IPAddress peer);
    // Explicitly abandon lost/timed-out requests; tracked callbacks still reject unsolicited
    // replies.
    /**
     * @brief Abandon all active slots without changing whether strict tracking has been enabled.
     * @note Previously tracked registrations still reject late replies. Shared users are affected;
     * returns no value.
     */
    void clearPendingRequests();
};

class IntegerCallback : public ValueCallback
{
public:
    /**
     * @brief Select this registration's wire type; configure the destination before use.
     * @note Prefer the matching SNMPManager factory so destination, OID, and limits are set
     * together.
     */
    IntegerCallback() : ValueCallback(INTEGER) {};
    int32_t *value = nullptr;
    float *floatValue = nullptr;
    bool isFloat = false;
};

class TimestampCallback : public ValueCallback
{
public:
    /**
     * @brief Select this registration's wire type; configure the destination before use.
     * @note Prefer the matching SNMPManager factory so destination, OID, and limits are set
     * together.
     */
    TimestampCallback() : ValueCallback(TIMESTAMP) {};
    uint32_t *value;
};

class StringCallback : public ValueCallback
{
public:
    /**
     * @brief Create a text/binary registration; initialise destination fields before use.
     * @param type STRING for text/bytes or OPAQUE for bytes. Prefer the manager's bounded
     * factories.
     */
    StringCallback(ASN_TYPE type = STRING) : ValueCallback(type) {};
    char **value = nullptr;
    unsigned char *bytes = nullptr;
    size_t *length = nullptr;
    size_t capacity = static_cast<size_t>(-1);
};

class OIDCallback : public ValueCallback
{
public:
    /**
     * @brief Select this registration's wire type; configure the destination before use.
     * @note Prefer the matching SNMPManager factory so destination, OID, and limits are set
     * together.
     */
    OIDCallback() : ValueCallback(ASN_TYPE::OID) {};
    char *value;
    size_t capacity = static_cast<size_t>(-1);
};

class Counter32Callback : public ValueCallback
{
public:
    /**
     * @brief Select this registration's wire type; configure the destination before use.
     * @note Prefer the matching SNMPManager factory so destination, OID, and limits are set
     * together.
     */
    Counter32Callback() : ValueCallback(ASN_TYPE::COUNTER32) {};
    uint32_t *value;
};

class Gauge32Callback : public ValueCallback
{
public:
    /**
     * @brief Select this registration's wire type; configure the destination before use.
     * @note Prefer the matching SNMPManager factory so destination, OID, and limits are set
     * together.
     */
    Gauge32Callback() : ValueCallback(ASN_TYPE::GAUGE32) {};
    uint32_t *value;
};

class Counter64Callback : public ValueCallback
{
public:
    /**
     * @brief Select this registration's wire type; configure the destination before use.
     * @note Prefer the matching SNMPManager factory so destination, OID, and limits are set
     * together.
     */
    Counter64Callback() : ValueCallback(ASN_TYPE::COUNTER64) {};
    uint64_t *value;
};

typedef struct ValueCallbackList
{
    /**
     * @brief Delete successor list nodes iteratively; registration references are released by
     * owners.
     */
    ~ValueCallbackList();
    ValueCallback *value = nullptr;
    struct ValueCallbackList *next = 0;
} ValueCallbacks;

#endif
