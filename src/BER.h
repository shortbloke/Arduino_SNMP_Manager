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
bool readBERHeader(const unsigned char *buf, size_t available, size_t &header, size_t &length);

class BER_CONTAINER
{
public:
    BER_CONTAINER(bool isPrimitive, ASN_TYPE type) : _isPrimitive(isPrimitive), _type(type) {};
    virtual ~BER_CONTAINER() {};
    bool _isPrimitive;
    ASN_TYPE _type;
    unsigned short _length = 0;
    virtual int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) = 0;
    virtual bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) = 0;
    virtual int getLength() = 0;
};

class NetworkAddress : public BER_CONTAINER
{
public:
    NetworkAddress() : BER_CONTAINER(true, NETWORK_ADDRESS) {};
    NetworkAddress(IPAddress ip) : BER_CONTAINER(true, NETWORK_ADDRESS), _value(ip) {};
    ~NetworkAddress() {};
    IPAddress _value;
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;
    int getLength() override
    {
        return _length;
    }
};

class IntegerType : public BER_CONTAINER
{
public:
    IntegerType() : BER_CONTAINER(true, INTEGER) {};
    IntegerType(unsigned long value) : BER_CONTAINER(true, INTEGER), _value(value) {};
    ~IntegerType() {};
    unsigned long _value = 0;
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;
    int getLength() override
    {
        return _length;
    }
};

class TimestampType : public IntegerType
{
public:
    TimestampType() : IntegerType()
    {
        _type = TIMESTAMP;
    };
    TimestampType(unsigned long value) : IntegerType(value)
    {
        _type = TIMESTAMP;
    };
    ~TimestampType() {};
};

class OctetType : public BER_CONTAINER
{
public:
    OctetType() : BER_CONTAINER(true, STRING)
    {
        _length = 0;
    }
    OctetType(char *value);
    char _value[SNMP_OCTETSTRING_MAX_LENGTH] = {};
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;
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
    explicit RawType(ASN_TYPE type = OPAQUE) : BER_CONTAINER(true, type) {}
    unsigned char _value[SNMP_OCTETSTRING_MAX_LENGTH] = {};
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    int getLength() override
    {
        return _length;
    }
};

class OIDType : public BER_CONTAINER
{
public:
    OIDType() : BER_CONTAINER(true, OID)
    {
        _length = 0;
    }
    OIDType(char *value);
    char _value[MAX_OID_LENGTH] = {};
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;
    int getLength() override
    {
        return _length;
    }
};

class NullType : public BER_CONTAINER
{
public:
    NullType() : BER_CONTAINER(true, NULLTYPE) {};
    ~NullType() {};
    char _value = 0;
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;

    int getLength() override
    {
        return 0;
    }
};

class Counter64 : public BER_CONTAINER
{
public:
    Counter64() : BER_CONTAINER(true, COUNTER64) {};
    Counter64(uint64_t value) : BER_CONTAINER(true, COUNTER64), _value(value) {};
    ~Counter64() {};
    uint64_t _value = 0;
    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;

    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override;

    int getLength() override
    {
        return _length;
    }
};

class Counter32 : public IntegerType
{
public:
    Counter32() : IntegerType()
    {
        _type = COUNTER32;
    };
    Counter32(uint32_t value) : IntegerType(value)
    {
        _type = COUNTER32;
    };
    ~Counter32() {};
};

class Gauge : public IntegerType
{ // Gauge32 uses unsigned 32-bit values.
public:
    Gauge() : IntegerType()
    {
        _type = GAUGE32;
    };
    Gauge(uint32_t value) : IntegerType(value)
    {
        _type = GAUGE32;
    };
    ~Gauge() {};
};

typedef struct BER_LINKED_LIST
{
    ~BER_LINKED_LIST();
    BER_CONTAINER *value = 0;
    struct BER_LINKED_LIST *next = 0;
} ValuesList;

class ComplexType : public BER_CONTAINER
{
public:
    ComplexType(ASN_TYPE type) : BER_CONTAINER(false, type) {};
    ComplexType(const ComplexType &) = delete;
    ComplexType &operator=(const ComplexType &) = delete;
    ~ComplexType()
    {
        delete _values;
    }
    ValuesList *_values = 0;
    bool fromBuffer(unsigned char *buf, size_t available = static_cast<size_t>(-1)) override
    {
        return decode(buf, available, 0);
    }
    bool decode(unsigned char *buf, size_t available, unsigned int depth);

    int serialise(unsigned char *buf, size_t capacity = static_cast<size_t>(-1)) override;

    int getLength() override
    {
        return _length;
    }

    // Takes ownership of the child on both success and failure.
    bool addValueToList(BER_CONTAINER *child);
};

#endif