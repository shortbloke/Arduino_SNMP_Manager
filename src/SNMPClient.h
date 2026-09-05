#ifndef SNMP_CLIENT_H
#define SNMP_CLIENT_H

#include "SNMPGetResponse.h"
#include <Udp.h>
#include <cstdio>
#include <cstring>

// Status for configuration, scheduling, and checked operation results.
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
        Unsupported,
        AllocationFailure
    };
    /**
     * @brief Wrap an operation/configuration outcome.
     * @param code Initial status; defaults to work not yet started.
     */
    SNMPStatus(Code code = NotStarted) : code_(code) {}
    /**
     * @return True only for Success; Pending and Partial are not success.
     */
    bool ok() const
    {
        return code_ == Success;
    }
    /**
     * @return Machine-readable status for branching; prefer this over comparing message text.
     */
    Code code() const
    {
        return code_;
    }
    /**
     * @return Static, read-only description. Do not free it; wording may change between versions.
     */
    const char *message() const;

private:
    Code code_;
};

/**
 * @brief Owned numeric or shared immutable variable-length value.
 * @note Check result status and type before conversion. Do not replace bytes/length
 *  directly; use setBytes(). Copying shares payload ownership and is not thread-safe.
 */
struct SNMPValue
{
    ASN_TYPE type = NULLTYPE; ///< Actual SNMP wire type; NULLTYPE is the empty default.
    uint64_t number = 0; ///< Numeric bits; INTEGER uses signed interpretation of the low 32 bits.
    // Immutable shared payload; copies retain ownership without allocating.
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>("");
    /**
     * @brief Construct an empty NULL value without allocating payload memory.
     */
    SNMPValue() = default;
    /**
     * @brief Release this value's payload reference; other copies retain their data.
     */
    ~SNMPValue();
    /**
     * @brief Share immutable payload storage without allocating.
     * @param other Source value; the copy can outlive it. Reference counts are not thread-safe.
     */
    SNMPValue(const SNMPValue &other);
    /**
     * @brief Replace this value with a shared copy, releasing its previous payload.
     * @param other Source value; self-assignment is allowed.
     * @return This value.
     */
    SNMPValue &operator=(const SNMPValue &other);
    size_t length = 0; ///< Payload bytes excluding the extra terminator; not a character count.
    /**
     * @return Signed 32-bit interpretation. Check the result status and INTEGER type first.
     */
    int32_t integer() const
    {
        return static_cast<int32_t>(number);
    }
    /**
     * @return Unsigned 32-bit interpretation. Check status and the expected 32-bit type first.
     */
    uint32_t unsigned32() const
    {
        return static_cast<uint32_t>(number);
    }
    /**
     * @return Stored unsigned number; check status and type before using it as Counter64.
     */
    uint64_t unsigned64() const
    {
        return number;
    }
    /**
     * @return True for an OCTET STRING with no embedded zero bytes and within the value limit.
     * @note This does not validate character encoding or remove control characters.
     */
    bool isText() const
    {
        return type == STRING && length <= SNMP_VALUE_MAX_LENGTH && !memchr(bytes, 0, length);
    }
    /**
     * @return Borrowed zero-terminated payload pointer. Check isText() for text, or type for OIDs.
     * @note Valid only while a value retains this payload; do not free or modify it.
     */
    const char *text() const
    {
        return reinterpret_cast<const char *>(bytes);
    }
    /**
     * @brief Construct a signed SNMP INTEGER without allocating.
     * @param value Signed 32-bit value.
     * @return Owned numeric value with INTEGER type.
     */
    static SNMPValue integer32(int32_t value);
    /**
     * @brief Construct an unsigned SNMP Counter32 without allocating.
     * @param value Counter total, not a rate.
     * @return Owned numeric value with COUNTER32 type.
     */
    static SNMPValue counter32(uint32_t value);
    /**
     * @brief Copy a bounded variable-length payload; preserve this value on failure.
     * @param data Bytes to copy; may alias the old payload, or be null when size is zero.
     * @param size Byte count, excluding any extra text terminator.
     * @param tag STRING, OPAQUE, OID (dotted text), or NETWORK_ADDRESS (four bytes).
     * @return Success, InvalidConfiguration, CapacityExceeded, or AllocationFailure.
     * @note Copies remain valid after data is released. OID syntax is checked when used in
     * requests.
     */
    SNMPStatus setBytes(const unsigned char *data, size_t size, ASN_TYPE tag = STRING);

private:
    struct Payload
    {
        size_t references;
    };
    Payload *payload_ = nullptr;
    void release();
};

/**
 * @brief One copied OID, value, expected type, and individual result status.
 */
struct SNMPResult
{
    char oid[MAX_OID_LENGTH] = {};
    SNMPValue value;
    SNMPStatus status;
    ASN_TYPE expected = NULLTYPE; // NULLTYPE means accept any supported type.
    /// @return True only when this binding has a usable successful value.
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
    /**
     * @brief Configure a peer without network I/O; this device is neither copyable nor movable.
     * @param client Borrowed client, which must outlive this device and its operations.
     * @param address IPv4 address copied into this device.
     * @param community Zero-terminated access string, copied; non-null and at most 63 bytes.
     * @param version Version1 or Version2c. Check status() for invalid configuration.
     */
    SNMPDevice(SNMPClient &client, IPAddress address, const char *community,
               SNMPVersion version = SNMPVersion::Version2c);
    /**
     * @brief Configure a peer using checked dotted IPv4 text; no name lookup is performed.
     * @param client Borrowed client that must outlive the device.
     * @param address Four decimal numbers in 0..255; copied, not retained as a pointer.
     * @param community Copied, non-null access string of at most 63 bytes.
     * @param version Version1 or Version2c.
     * @note Check status(); malformed addresses produce InvalidAddress before any request is sent.
     */
    SNMPDevice(SNMPClient &client, const char *address, const char *community,
               SNMPVersion version = SNMPVersion::Version2c);
    SNMPDevice(const SNMPDevice &) = delete;
    SNMPDevice &operator=(const SNMPDevice &) = delete;
    /// @return Device construction status; Success does not prove network reachability.
    SNMPStatus status() const
    {
        return status_;
    }
    // The application must not change these settings while operations are pending.
    uint16_t port = 161;       ///< Remote service port; nonzero.
    uint32_t timeoutMs = 1000; ///< Per-attempt timeout in ms, 1..INT32_MAX.
    uint8_t retries = 1;       ///< Extra read attempts per batch; ignored for SET.

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
    /**
     * @brief Unregister outstanding work and release owned results without invoking its handler.
     */
    virtual ~SNMPOperation();
    SNMPOperation(const SNMPOperation &) = delete;
    SNMPOperation &operator=(const SNMPOperation &) = delete;
    /**
     * @brief Schedule this reusable operation; no synchronous network send occurs.
     * @return Success when accepted, or Busy, NotStarted, InvalidConfiguration, the device's
     *  configuration error, or CapacityExceeded when no scheduler slot is available.
     * @note Rejected starts preserve previous results. Accepted starts invalidate them until
     *  completion. Keep calling client.loop() and check status() after takeCompleted().
     */
    SNMPStatus start();
    // Called from loop()/cancel(), after removal from the scheduler. A handler may
    // start another operation but must not destroy this operation or reenter loop().
    /**
     * @brief Set an optional completion callback instead of polling alone.
     * @param handler Called with this operation and context; null disables it.
     * @param context Borrowed application pointer forwarded unchanged; keep it valid until
     * disabled.
     * @note Runs during loop() or cancel(), not an interrupt. Do not destroy the active
     *  operation or reenter loop() from the callback. Returns no value.
     */
    void onComplete(void (*handler)(SNMPOperation &, void *), void *context = nullptr)
    {
        completionHandler_ = handler;
        completionContext_ = context;
    }
    /**
     * @brief End pending work with Cancelled and notify the completion handler, if set.
     * @note No effect when not pending; retained results remain inspectable. Returns no value.
     */
    void cancel();
    /**
     * @return True while this operation is scheduled and awaiting completion.
     */
    bool pending() const
    {
        return status_.code() == SNMPStatus::Pending;
    }
    /**
     * @return True once for each completed/cancelled run; consumes the flag, not the results.
     */
    bool takeCompleted()
    {
        bool done = completed_;
        completed_ = false;
        return done;
    }
    /// @return Current operation outcome; inspect individual cells on Partial.
    SNMPStatus status() const
    {
        return status_;
    }
    /**
     * @return Number of configured query/write results or collected walk results; streaming stores
     * none.
     */
    size_t size() const
    {
        return count_;
    }
    /**
     * @return SNMP error-status from the most recently matched reply, or zero after start.
     * @note Zero does not mean the operation succeeded; also check status().
     */
    int agentError() const
    {
        return agentError_;
    }
    /**
     * @return One-based binding index from the most recently matched reply, or zero.
     * @note This refers to that wire request batch, not the whole multi-batch query.
     */
    int agentErrorIndex() const
    {
        return agentErrorIndex_;
    }
    /**
     * @brief Access one retained result without copying it.
     * @param index Zero-based slot; must be less than size(). No bounds check is performed.
     * @return Borrowed result; check ok() before interpreting value. Reuse/destruction may
     * invalidate it.
     */
    const SNMPResult &operator[](size_t index) const
    {
        return results_[index];
    }

protected:
    /**
     * @brief Initialise the base of an operation; does not send anything.
     * @param device Borrowed device that must outlive this operation.
     * @param results Derived-class result array, kept alive for the operation's lifetime.
     * @param capacity Number of available slots, not bytes.
     */
    SNMPOperation(SNMPDevice &device, SNMPResult *results, size_t capacity);
    /**
     * @brief Validate and append an exact instance before scheduling.
     * @param oid Zero-terminated numeric OID; canonical text is copied.
     * @param expected Expected reply type; NULLTYPE accepts any supported type.
     * @param value Optional SET value to share-copy; null configures a read.
     * @return Success, Busy, CapacityExceeded, InvalidOID, InvalidConfiguration, or Unsupported.
     */
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
    /**
     * @brief Validate and copy a subtree root for a walk.
     * @param root Zero-terminated numeric OID.
     * @return Success, Busy, or the OID validation/capacity error.
     */
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

/**
 * @brief Reusable exact-instance reads with owned result slots.
 * @tparam Capacity Maximum requested instances, greater than zero; not a byte limit.
 */
template <size_t Capacity> class SNMPQuery : public SNMPOperation
{
public:
    /**
     * @brief Create a read with Capacity result slots; configure OIDs before start().
     * @param device Borrowed peer configuration that must outlive the query.
     */
    explicit SNMPQuery(SNMPDevice &device) : SNMPOperation(device, storage_, Capacity) {}
    /**
     * @brief Append one exact instance; repeated OIDs are rejected.
     * @param oid Zero-terminated dotted OID copied into the query.
     * @param expected Required reply type, or NULLTYPE to accept any supported type.
     * @return Success, Busy, CapacityExceeded, or InvalidOID. No network traffic is sent.
     */
    SNMPStatus addOID(const char *oid, ASN_TYPE expected = NULLTYPE)
    {
        return add(oid, expected);
    }
    /**
     * @brief Append count consecutive index suffixes atomically; does not discover sparse indices.
     * @param column Dotted column OID without a row index.
     * @param first First unsigned index appended to column.
     * @param count Number of instances, not the final index. Zero appends nothing.
     * @param expected Required type, or NULLTYPE to accept any supported type.
     * @return Success, Busy, CapacityExceeded (including index overflow), or InvalidOID.
     * @note Any failure rolls back the entire addition; existing configured OIDs remain.
     */
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

/**
 * @brief Bounded whole-message writes with no automatic retry.
 * @tparam Capacity Maximum instance/value pairs, greater than zero.
 */
template <size_t Capacity> class SNMPSet : public SNMPOperation
{
public:
    /**
     * @brief Create a bounded write; start() is the explicit permission to transmit it.
     * @param device Borrowed peer that must outlive the write. SET is not split or automatically
     * retried.
     */
    explicit SNMPSet(SNMPDevice &device) : SNMPOperation(device, storage_, Capacity)
    {
        mode_ = SetRequestPDU;
    }
    /**
     * @brief Append one instance/value pair without sending it.
     * @param oid Numeric instance OID copied into the request; duplicates are rejected.
     * @param value Value copied with shared payload ownership; the original may be released.
     * @return Success or a configuration, OID, capacity, Busy, or unsupported-type error.
     * @note On a later SET timeout, read back before repeating the write.
     */
    SNMPStatus addValue(const char *oid, const SNMPValue &value)
    {
        return add(oid, value.type, &value);
    }

private:
    static_assert(Capacity > 0, "SET requires capacity");
    SNMPResult storage_[Capacity];
};

/**
 * @brief Traverse a subtree using v1 GETNEXT or v2c GETBULK with fallback.
 * @tparam Capacity Collected result slots, greater than zero. In streaming mode slots
 *  are not populated; payload/packet limits and the 60-second walk deadline still apply.
 */
template <size_t Capacity> class SNMPWalk : public SNMPOperation
{
public:
    /**
     * @brief Create a walk with Capacity collected-result slots; call configure() before start().
     * @param device Borrowed peer that must outlive the walk.
     */
    explicit SNMPWalk(SNMPDevice &device) : SNMPOperation(device, storage_, Capacity)
    {
        walking_ = true;
    }
    /**
     * @brief Set the subtree to traverse, stopping when returned OIDs leave it.
     * @param root Numeric dotted subtree OID, copied into the walk.
     * @return Success, Busy, or an OID/capacity validation error.
     */
    SNMPStatus configure(const char *root)
    {
        return setRoot(root);
    }
    /**
     * @brief Select streamed delivery instead of collecting results in slots.
     * @param callback Receives a borrowed result and context. Return true to continue; false
     *  ends with CapacityExceeded. Null restores collection. May cancel but must not destroy the
     * walk.
     * @param context Borrowed user pointer, forwarded unchanged for each value.
     * @return Success or Busy if a walk is already pending.
     * @note Copy values that must outlive the callback; callbacks run synchronously from loop().
     */
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
    /**
     * @return Static instance OID for management-subsystem uptime; do not free or modify it.
     */
    static const char *oid()
    {
        return ".1.3.6.1.2.1.1.3.0";
    }
    /**
     * @return TIMESTAMP, the wire tag for TimeTicks (hundredths of a second).
     */
    static ASN_TYPE type()
    {
        return TIMESTAMP;
    }
};
/**
 * @brief A single typed reading built on SNMPQuery.
 * @tparam Descriptor Supplies static oid() returning instance text and type() returning ASN_TYPE.
 */
template <class Descriptor> class SNMPRead : public SNMPQuery<1>
{
public:
    /**
     * @brief Configure one reading using Descriptor::oid() and Descriptor::type().
     * @param device Borrowed peer that must outlive this read.
     * @note Descriptor must supply a valid OID/type; check start() and the completed result.
     */
    explicit SNMPRead(SNMPDevice &device) : SNMPQuery<1>(device)
    {
        this->addOID(Descriptor::oid(), Descriptor::type());
    }
    /**
     * @return Borrowed result of this single read; check ok() before accessing value.
     */
    const SNMPResult &result() const
    {
        return (*this)[0];
    }
};

// Borrowed notification view, valid only during the handler call.
struct SNMPNotification
{
    IPAddress peer;    ///< Notification sender address, not necessarily the v1 agentAddress.
    uint16_t port = 0; ///< Sender UDP port; INFORM responses return to this endpoint.
    SNMPVersion version = SNMPVersion::Version2c;
    bool inform = false;              ///< True only for an INFORM that expects an acknowledgement.
    int32_t requestID = 0;            ///< v2c correlation ID; not used by v1 traps.
    const char *enterprise = nullptr; // v1 only
    IPAddress agentAddress;           // v1 only
    int32_t genericTrap = 0, specificTrap = 0;
    uint32_t uptime = 0;                  ///< Sender uptime in hundredths of a second.
    const ValuesList *bindings = nullptr; ///< Borrowed tree; use read() to retain a binding.
    /**
     * @return Number of variable bindings in this borrowed notification.
     */
    size_t size() const;
    /**
     * @brief Copy one notification binding into owned result storage.
     * @param index Zero-based binding number.
     * @param result Output slot; left unchanged when index is absent, otherwise replaced.
     * @return Success, Missing for an absent index, or a conversion/storage failure.
     * @note The copied value can outlive the callback; the notification itself cannot.
     */
    SNMPStatus read(size_t index, SNMPResult &result) const;
};

class SNMPClient
{
public:
    /**
     * @brief Borrow a UDP transport without opening it; keep device/operation objects stable.
     * @param udp Transport that must outlive the client; no other consumer may service it.
     */
    explicit SNMPClient(UDP &udp) : udp_(udp) {}
    /**
     * @brief Stop a begun transport and cancel any remaining work.
     * @note Destroy operations and devices before their client, and the client before its UDP
     * object.
     */
    ~SNMPClient();
    SNMPClient(const SNMPClient &) = delete;
    SNMPClient &operator=(const SNMPClient &) = delete;
    /**
     * @brief Open the shared request/reply socket after the network is connected.
     * @param localPort Local listening port; zero selects a transport-assigned port, 162 receives
     * events.
     * @return Success or TransportError. Repeated begin() succeeds without changing the existing
     * port.
     */
    SNMPStatus begin(uint16_t localPort = 0);
    // Return true once the application accepts the notification. INFORMs are
    // acknowledged only then. The handler must not destroy or reenter the client.
    /**
     * @brief Register or disable notification handling for one community.
     * @param community Non-null access string, copied, at most 63 bytes.
     * @param handler Receives a borrowed notification and context; null disables delivery.
     *  Return true to accept (and acknowledge INFORM), false to leave it unacknowledged.
     * @param context Borrowed application pointer, valid during callbacks.
     * @return Success or InvalidConfiguration; network binding still requires begin().
     * @note Runs in loop(); do not destroy/reenter the client. Duplicate events may be delivered.
     */
    SNMPStatus notifications(const char *community,
                             bool (*handler)(const SNMPNotification &, void *),
                             void *context = nullptr);
    /**
     * @brief Process a received message and advance scheduled work using millis().
     * @note Call frequently from one task. Does nothing before begin(); returns no value.
     */
    void loop();
    /**
     * @brief Advance work using an explicit clock for deterministic tests.
     * @param now Millisecond clock, advancing modulo 2^32; use one consistent clock for the client.
     * @note Same callback/reentrancy rules as loop(). Returns no value.
     */
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
