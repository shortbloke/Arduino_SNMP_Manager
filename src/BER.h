#ifndef BER_h
#define BER_h

#include "SNMPConfig.h"

#include <Arduino.h>
#include <IPAddress.h>
#include <new>

typedef enum ASN_TYPE_WITH_VALUE
{
    // Primitives
    INTEGER = 0x02,
    STRING = 0x04,
    NULLTYPE = 0x05,
    OID = 0x06,

    // Constructed sequence
    STRUCTURE = 0x30,

    // Application types
    NETWORK_ADDRESS = 0x40,
    COUNTER32 = 0x41,
    GAUGE32 = 0x42, // UNSIGNED32
    TIMESTAMP = 0x43,
    OPAQUE = 0x44,
    COUNTER64 = 0x46,

    // Per-binding exception tags
    NOSUCHOBJECT = 0x80,
    NOSUCHINSTANCE = 0x81,
    ENDOFMIBVIEW = 0x82,

    // Constructed PDU tags
    GetRequestPDU = 0xA0,
    GetNextRequestPDU = 0xA1,
    GetResponsePDU = 0xA2,
    SetRequestPDU = 0xA3,
    TrapPDU = 0xA4,
    GetBulkRequestPDU = 0xA5,
    InformRequestPDU = 0xA6,
    Trapv2PDU = 0xA7
} ASN_TYPE;

// Primitive types derive from BER_CONTAINER and serialise as type, length, and value (TLV).
// ComplexType owns a linked list of child BER_CONTAINER objects. Its fromBuffer method
// selects each child's decoder by tag and passes the complete child TLV to it.

// Read a definite-length TLV header without reading beyond the supplied buffer.
// The legacy pointer-only entry points cannot validate the allocation size.
/**
 * @brief Decode a definite-length type/length/value (TLV) header within supplied bounds.
 * @param buf Non-null encoded input, borrowed only for the call.
 * @param available Accessible bytes including the header and value.
 * @param header Output header-byte count; unspecified on failure.
 * @param length Output value-byte count; unspecified on failure.
 * @return True for supported framing with the full value present; false for truncation,
 *  indefinite lengths, overflow, or lengths exceeding the decoder's 65535-byte limit.
 */
bool readBERHeader(const unsigned char *buf, size_t available, size_t &header, size_t &length);

class BER_CONTAINER
{
public:
    /**
     * @brief Initialise an abstract encoded-value container.
     * @param isPrimitive True for a simple value, false for a constructed list.
     * @param type ASN.1 wire tag used when serializing.
     */
    BER_CONTAINER(bool isPrimitive, ASN_TYPE type) : _isPrimitive(isPrimitive), _type(type) {};
    /**
     * @brief Destroy a value through the base interface; derived owners release their storage.
     */
    virtual ~BER_CONTAINER() {};
    bool _isPrimitive;
    ASN_TYPE _type;
    unsigned short _length = 0;
    /**
     * @brief Encode a complete TLV, or measure its encoded size.
     * @param buf Destination buffer; null requests sizing without writes.
     * @param capacity Accessible destination bytes. Pass the actual size; the legacy default
     *  assumes unlimited caller storage and cannot protect an undersized allocation.
     * @return Complete encoded byte count, or a negative value for invalid content or insufficient
     *  capacity. Do not transmit/use output when negative; concrete types may update cached
     * lengths.
     */
    virtual int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) = 0;
    /**
     * @brief Decode one complete TLV from bounded input.
     * @param buf Borrowed input bytes, including tag and length, not just the value.
     * @param available Accessible bytes; always supply the real size instead of the unsafe legacy
     * default.
     * @return True on a valid supported decode; false on malformed/truncated/oversized data or
     *  allocation failure. Do not assume previous state is preserved after failure.
     */
    virtual bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) = 0;
    /**
     * @return Stored content length, excluding the TLV header. This can be zero before
     *  encoding/decoding; use serialise(nullptr) to measure the complete encoding.
     */
    virtual int getLength() = 0;
};

class NetworkAddress : public BER_CONTAINER
{
public:
    /**
     * @brief Create an IPv4 SNMP value with the default IPAddress value.
     */
    NetworkAddress() : BER_CONTAINER(true, NETWORK_ADDRESS) {};
    /**
     * @param ip Copied four-byte IPv4 value; encoded as the SNMP IpAddress type.
     */
    NetworkAddress(IPAddress ip) : BER_CONTAINER(true, NETWORK_ADDRESS), _value(ip) {};
    /// @brief Destroy this inline value; no caller-owned storage is released.
    ~NetworkAddress() {};
    IPAddress _value;
    /// @copydoc BER_CONTAINER::serialise
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::fromBuffer
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::getLength
    int getLength() override
    {
        return _length;
    }
};

class IntegerType : public BER_CONTAINER
{
public:
    /**
     * @brief Create an INTEGER value initialised to zero.
     */
    IntegerType() : BER_CONTAINER(true, INTEGER) {};
    /**
     * @param value Stored integer bits. INTEGER serialization uses signed 32-bit interpretation;
     *  derived application types use unsigned 32-bit interpretation.
     */
    IntegerType(unsigned long value) : BER_CONTAINER(true, INTEGER), _value(value) {};
    /// @brief Destroy this inline value; no caller-owned storage is released.
    ~IntegerType() {};
    unsigned long _value = 0;
    /// @copydoc BER_CONTAINER::serialise
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::fromBuffer
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::getLength
    int getLength() override
    {
        return _length;
    }
};

class TimestampType : public IntegerType
{
public:
    /**
     * @brief Create a zero TimeTicks value.
     */
    TimestampType() : IntegerType()
    {
        _type = TIMESTAMP;
    };
    /**
     * @param value TimeTicks in hundredths of a second, within the unsigned 32-bit range.
     */
    TimestampType(unsigned long value) : IntegerType(value)
    {
        _type = TIMESTAMP;
    };
    /// @brief Destroy this inline value; no caller-owned storage is released.
    ~TimestampType() {};
};

class OctetType : public BER_CONTAINER
{
public:
    /**
     * @brief Create an empty OCTET STRING with fixed-capacity inline storage.
     */
    OctetType() : BER_CONTAINER(true, STRING)
    {
        _length = 0;
    }
    /**
     * @brief Copy zero-terminated text into an OCTET STRING.
     * @param value Non-null C string, copied, not retained. Embedded zero bytes end the text.
     * @note Oversized text makes serialization fail. Use bounded decoding for binary content.
     */
    OctetType(char *value);
    char _value[SNMP_OCTETSTRING_MAX_LENGTH] = {};
    /// @copydoc BER_CONTAINER::serialise
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::fromBuffer
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::getLength
    int getLength() override
    {
        return _length;
    }

private:
    bool decoded = false;
    bool valid = true;
};

// Opaque and exception values are primitive payloads, never nested TLVs.
class RawType : public BER_CONTAINER
{
public:
    /**
     * @brief Create an empty primitive payload container.
     * @param type Wire tag, normally OPAQUE or a per-binding exception. Set payload and length
     *  consistently before encoding; protocol layers further restrict supported tags.
     */
    explicit RawType(ASN_TYPE type = OPAQUE) : BER_CONTAINER(true, type) {}
    unsigned char _value[SNMP_OCTETSTRING_MAX_LENGTH] = {};
    /// @copydoc BER_CONTAINER::fromBuffer
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::serialise
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::getLength
    int getLength() override
    {
        return _length;
    }
};

class OIDType : public BER_CONTAINER
{
public:
    /**
     * @brief Create an empty OID container; set valid content before serialization.
     */
    OIDType() : BER_CONTAINER(true, OID)
    {
        _length = 0;
    }
    /**
     * @brief Copy numeric dotted OID text into fixed-capacity storage.
     * @param value Non-null zero-terminated text. No pointer is retained.
     * @note Oversized text leaves the OID empty; serialization validates syntax and reports
     * failure.
     */
    OIDType(char *value);
    char _value[MAX_OID_LENGTH] = {};
    /// @copydoc BER_CONTAINER::serialise
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::fromBuffer
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::getLength
    int getLength() override
    {
        return _length;
    }
};

class NullType : public BER_CONTAINER
{
public:
    /**
     * @brief Create a NULL placeholder with no content bytes.
     */
    NullType() : BER_CONTAINER(true, NULLTYPE) {};
    /// @brief Destroy this inline value; no caller-owned storage is released.
    ~NullType() {};
    char _value = 0;
    /// @copydoc BER_CONTAINER::serialise
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    /// @copydoc BER_CONTAINER::fromBuffer
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;

    /// @copydoc BER_CONTAINER::getLength
    int getLength() override
    {
        return 0;
    }
};

class Counter64 : public BER_CONTAINER
{
public:
    /**
     * @brief Create a zero Counter64 value, supported by SNMPv2c.
     */
    Counter64() : BER_CONTAINER(true, COUNTER64) {};
    /**
     * @param value Unsigned 64-bit counter total copied into this object.
     */
    Counter64(uint64_t value) : BER_CONTAINER(true, COUNTER64), _value(value) {};
    /// @brief Destroy this inline value; no caller-owned storage is released.
    ~Counter64() {};
    uint64_t _value = 0;
    /// @copydoc BER_CONTAINER::serialise
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;

    /// @copydoc BER_CONTAINER::fromBuffer
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;

    /// @copydoc BER_CONTAINER::getLength
    int getLength() override
    {
        return _length;
    }
};

class Counter32 : public IntegerType
{
public:
    /**
     * @brief Create a zero Counter32 value.
     */
    Counter32() : IntegerType()
    {
        _type = COUNTER32;
    };
    /**
     * @param value Unsigned 32-bit counter total copied into this object.
     */
    Counter32(uint32_t value) : IntegerType(value)
    {
        _type = COUNTER32;
    };
    /// @brief Destroy this inline value; no caller-owned storage is released.
    ~Counter32() {};
};

class Gauge : public IntegerType
{ // Gauge32 uses unsigned 32-bit values.
public:
    /**
     * @brief Create a zero Gauge32 value.
     */
    Gauge() : IntegerType()
    {
        _type = GAUGE32;
    };
    /**
     * @param value Unsigned 32-bit gauge reading copied into this object.
     */
    Gauge(uint32_t value) : IntegerType(value)
    {
        _type = GAUGE32;
    };
    /// @brief Destroy this inline value; no caller-owned storage is released.
    ~Gauge() {};
};

typedef struct BER_LINKED_LIST
{
    /**
     * @brief Iteratively delete owned child containers and successor list nodes.
     */
    ~BER_LINKED_LIST();
    BER_CONTAINER *value = 0;
    struct BER_LINKED_LIST *next = 0;
} ValuesList;

/**
 * @brief Own a constructed BER value and its child tree; copying/moving is disabled.
 */
class ComplexType : public BER_CONTAINER
{
public:
    /**
     * @brief Create an empty owning list of encoded child values.
     * @param type Constructed wire tag, such as STRUCTURE or a request/response PDU.
     */
    ComplexType(ASN_TYPE type) : BER_CONTAINER(false, type) {};
    ComplexType(const ComplexType &) = delete;
    ComplexType &operator=(const ComplexType &) = delete;
    /**
     * @brief Delete all owned children/list nodes; borrowed pointers to them become invalid.
     */
    ~ComplexType()
    {
        delete _values;
    }
    ValuesList *_values = 0;
    /// @copydoc BER_CONTAINER::fromBuffer
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override
    {
        return decode(buf, available, 0);
    }
    /**
     * @brief Decode a constructed value while limiting nesting depth.
     * @param buf Borrowed bytes of a complete constructed TLV.
     * @param available Accessible input bytes including all children.
     * @param depth Current nesting depth; use zero for a top-level call.
     * @return True on complete supported decoding; false on malformed input, depth >= 32,
     *  capacity, or allocation failure. Previous children are released before decoding.
     */
    bool decode(unsigned char *buf, size_t available, unsigned int depth);

    /// @copydoc BER_CONTAINER::serialise
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;

    /// @copydoc BER_CONTAINER::getLength
    int getLength() override
    {
        return _length;
    }

    // Takes ownership of the child on both success and failure.
    /**
     * @brief Append a child with unconditional ownership transfer.
     * @param child Heap-created value; consumed even if the append fails. Null is rejected.
     * @return True if appended; false for null or list-node allocation failure.
     * @note Never delete or use child after a failed call; a successful child belongs to this list.
     */
    bool addValueToList(BER_CONTAINER *child);
};

#endif