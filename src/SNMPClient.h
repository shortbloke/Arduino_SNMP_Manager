#ifndef SNMP_CLIENT_H
#define SNMP_CLIENT_H

#include "SNMPGetResponse.h"
#include <Udp.h>
#include <cstdio>
#include <cstring>

// Fixed-size result storage; larger text/binary values report CapacityExceeded.
class SNMPStatus
{
public:
    enum Code
    {
        Success,
        Pending,
        Busy,
        InvalidAddress,
        InvalidConfiguration,
        InvalidOID,
        CapacityExceeded,
        NotStarted,
        TransportError,
        Timeout,
        Cancelled,
        Missing,
        TypeMismatch,
        ProtocolError,
        Partial,
        Unsupported
    };
    SNMPStatus(Code code = NotStarted) : code_(code) {}
    bool ok() const
    {
        return code_ == Success;
    }
    Code code() const
    {
        return code_;
    }
    const char *message() const;

private:
    Code code_;
};

struct SNMPValue
{
    ASN_TYPE type = NULLTYPE;
    uint64_t number = 0;
    unsigned char bytes[MAX_OID_LENGTH] = {};
    size_t length = 0;
    int32_t integer() const
    {
        return static_cast<int32_t>(number);
    }
    uint32_t unsigned32() const
    {
        return static_cast<uint32_t>(number);
    }
    uint64_t unsigned64() const
    {
        return number;
    }
    bool isText() const
    {
        return type == STRING && !memchr(bytes, 0, length);
    }
    const char *text() const
    {
        return reinterpret_cast<const char *>(bytes);
    }
    static SNMPValue integer32(int32_t value);
    static SNMPValue counter32(uint32_t value);
    SNMPStatus setBytes(const unsigned char *data, size_t size, ASN_TYPE tag = STRING);
};

struct SNMPResult
{
    char oid[MAX_OID_LENGTH] = {};
    SNMPValue value;
    SNMPStatus status;
    ASN_TYPE expected = NULLTYPE; // NULLTYPE means accept any supported type.
    bool ok() const
    {
        return status.ok();
    }
};

class SNMPClient;
class SNMPOperation;
class SNMPDevice
{
public:
    SNMPDevice(SNMPClient &client, IPAddress address, const char *community,
               SNMPVersion version = SNMPVersion::Version2c);
    SNMPDevice(SNMPClient &client, const char *address, const char *community,
               SNMPVersion version = SNMPVersion::Version2c);
    SNMPDevice(const SNMPDevice &) = delete;
    SNMPDevice &operator=(const SNMPDevice &) = delete;
    SNMPStatus status() const
    {
        return status_;
    }
    // Configuration is immutable while operations are pending.
    uint16_t port = 161;
    uint32_t timeoutMs = 1000;
    uint8_t retries = 1;

private:
    friend class SNMPClient;
    friend class SNMPOperation;
    SNMPClient &client_;
    IPAddress address_;
    char community_[64] = {};
    SNMPVersion version_;
    SNMPStatus status_;
};

// Device and client must outlive their operations. Destruction cancels silently.
class SNMPOperation
{
public:
    virtual ~SNMPOperation();
    SNMPOperation(const SNMPOperation &) = delete;
    SNMPOperation &operator=(const SNMPOperation &) = delete;
    SNMPStatus start();
    // Called from loop()/cancel(), after removal from the scheduler. A handler may
    // start another operation but must not destroy this operation or reenter loop().
    void onComplete(void (*handler)(SNMPOperation &, void *), void *context = nullptr)
    {
        completionHandler_ = handler;
        completionContext_ = context;
    }
    void cancel();
    bool pending() const
    {
        return status_.code() == SNMPStatus::Pending;
    }
    bool takeCompleted()
    {
        bool done = completed_;
        completed_ = false;
        return done;
    }
    SNMPStatus status() const
    {
        return status_;
    }
    size_t size() const
    {
        return count_;
    }
    int agentError() const
    {
        return agentError_;
    }
    int agentErrorIndex() const
    {
        return agentErrorIndex_;
    }
    const SNMPResult &operator[](size_t index) const
    {
        return results_[index];
    }

protected:
    SNMPOperation(SNMPDevice &device, SNMPResult *results, size_t capacity);
    SNMPStatus add(const char *oid, ASN_TYPE expected, const SNMPValue *value = nullptr);
    SNMPDevice &device_;
    SNMPResult *results_;
    size_t capacity_, count_ = 0;
    ASN_TYPE mode_ = GetRequestPDU;
    bool walking_ = false;
    char root_[MAX_OID_LENGTH] = {}, cursor_[MAX_OID_LENGTH] = {};
    // Streaming callbacks borrow the value only for the duration of the call.
    bool (*onValue_)(const SNMPResult &, void *) = nullptr;
    void *context_ = nullptr;
    SNMPStatus setRoot(const char *root);

private:
    friend class SNMPClient;
    SNMPStatus status_;
    bool completed_ = false;
    void (*completionHandler_)(SNMPOperation &, void *) = nullptr;
    void *completionContext_ = nullptr;
    size_t offset_ = 0, batch_ = 0, batchLimit_ = 0;
    int32_t id_ = 0;
    int agentError_ = 0, agentErrorIndex_ = 0;
    uint32_t sentAt_ = 0;
    uint8_t attempts_ = 0;
    bool sent_ = false;
    uint32_t startedAt_ = 0;
    void finish(SNMPStatus status);
};

template <size_t Capacity> class SNMPQuery : public SNMPOperation
{
public:
    explicit SNMPQuery(SNMPDevice &device) : SNMPOperation(device, storage_, Capacity) {}
    SNMPStatus addOID(const char *oid, ASN_TYPE expected = NULLTYPE)
    {
        return add(oid, expected);
    }
    SNMPStatus addRange(const char *column, uint32_t first, size_t count,
                        ASN_TYPE expected = NULLTYPE)
    {
        if (pending())
            return SNMPStatus::Busy;
        if (count > Capacity - count_ || (count && count - 1 > UINT32_MAX - first))
            return SNMPStatus::CapacityExceeded;
        size_t original = count_;
        for (size_t i = 0; i < count; ++i)
        {
            char name[MAX_OID_LENGTH];
            int n = snprintf(name, sizeof(name), "%s.%lu", column ? column : "",
                             static_cast<unsigned long>(first + i));
            SNMPStatus status = n < 0 || static_cast<size_t>(n) >= sizeof(name)
                                    ? SNMPStatus(SNMPStatus::InvalidOID)
                                    : add(name, expected);
            if (!status.ok())
            {
                count_ = original;
                return status;
            }
        }
        return SNMPStatus::Success;
    }

private:
    static_assert(Capacity > 0, "Query requires result capacity");
    SNMPResult storage_[Capacity];
};

template <size_t Capacity> class SNMPSet : public SNMPOperation
{
public:
    explicit SNMPSet(SNMPDevice &device) : SNMPOperation(device, storage_, Capacity)
    {
        mode_ = SetRequestPDU;
    }
    SNMPStatus addValue(const char *oid, const SNMPValue &value)
    {
        return add(oid, value.type, &value);
    }

private:
    static_assert(Capacity > 0, "SET requires capacity");
    SNMPResult storage_[Capacity];
};

template <size_t Capacity> class SNMPWalk : public SNMPOperation
{
public:
    explicit SNMPWalk(SNMPDevice &device) : SNMPOperation(device, storage_, Capacity)
    {
        walking_ = true;
    }
    SNMPStatus configure(const char *root)
    {
        return setRoot(root);
    }
    SNMPStatus stream(bool (*callback)(const SNMPResult &, void *), void *context)
    {
        if (pending())
            return SNMPStatus::Busy;
        onValue_ = callback;
        context_ = context;
        return SNMPStatus::Success;
    }

private:
    static_assert(Capacity > 0, "Walk requires capacity");
    SNMPResult storage_[Capacity];
};

struct SystemUptime
{
    static const char *oid()
    {
        return ".1.3.6.1.2.1.1.3.0";
    }
    static ASN_TYPE type()
    {
        return TIMESTAMP;
    }
};
template <class Descriptor> class SNMPRead : public SNMPQuery<1>
{
public:
    explicit SNMPRead(SNMPDevice &device) : SNMPQuery<1>(device)
    {
        this->addOID(Descriptor::oid(), Descriptor::type());
    }
    const SNMPResult &result() const
    {
        return (*this)[0];
    }
};

// Borrowed notification view, valid only during the handler call.
struct SNMPNotification
{
    IPAddress peer;
    uint16_t port = 0;
    SNMPVersion version = SNMPVersion::Version2c;
    bool inform = false;
    int32_t requestID = 0;
    const char *enterprise = nullptr; // v1 only
    IPAddress agentAddress;           // v1 only
    int32_t genericTrap = 0, specificTrap = 0;
    uint32_t uptime = 0;
    const ValuesList *bindings = nullptr;
    size_t size() const;
    SNMPStatus read(size_t index, SNMPResult &result) const;
};

class SNMPClient
{
public:
    explicit SNMPClient(UDP &udp) : udp_(udp) {}
    ~SNMPClient();
    SNMPClient(const SNMPClient &) = delete;
    SNMPClient &operator=(const SNMPClient &) = delete;
    SNMPStatus begin(uint16_t localPort = 0);
    // Return true once the application accepts the notification. INFORMs are
    // acknowledged only then. The handler must not destroy or reenter the client.
    SNMPStatus notifications(const char *community,
                             bool (*handler)(const SNMPNotification &, void *),
                             void *context = nullptr);
    void loop();
    void loop(uint32_t now); // Explicit clock for deterministic testing.
private:
    friend class SNMPOperation;
    UDP &udp_;
    bool begun_ = false;
    char notificationCommunity_[64] = {};
    bool (*notificationHandler_)(const SNMPNotification &, void *) = nullptr;
    void *notificationContext_ = nullptr;
    void notify(size_t size, IPAddress peer, uint16_t port);
    uint32_t nextID_ = 0;
    unsigned char buffer_[SNMP_PACKET_LENGTH];
    SNMPOperation *pending_[SNMP_MAX_PENDING_REQUESTS] = {};
    SNMPStatus schedule(SNMPOperation &operation);
    void remove(SNMPOperation &operation);
    bool send(SNMPOperation &operation, uint32_t now);
    void receive();
};
#endif
