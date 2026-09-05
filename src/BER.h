#ifndef BER_h
#define BER_h

#ifndef SNMP_OCTETSTRING_MAX_LENGTH
#define SNMP_OCTETSTRING_MAX_LENGTH 1024
#endif

#ifndef MAX_OID_LENGTH
#define MAX_OID_LENGTH 128
#endif

#include <Arduino.h>
#include <math.h>

typedef enum ASN_TYPE_WITH_VALUE
{
    // Primitives
    INTEGER = 0x02,
    STRING = 0x04,
    NULLTYPE = 0x05,
    OID = 0x06,

    // Complex
    STRUCTURE = 0x30,
    NETWORK_ADDRESS = 0x40,
    COUNTER32 = 0x41,
    GAUGE32 = 0x42, // UNSIGNED32
    TIMESTAMP = 0x43,
    OPAQUE = 0x44,
    COUNTER64 = 0x46,

    NOSUCHOBJECT = 0x80,
    NOSUCHINSTANCE = 0x81,
    ENDOFMIBVIEW = 0x82,

    GetRequestPDU = 0xA0,
    GetNextRequestPDU = 0xA1,
    GetResponsePDU = 0xA2,
    SetRequestPDU = 0xA3,
    TrapPDU = 0xA4,
    GetBulkRequestPDU = 0xA5,
    Trapv2PDU = 0xA7
} ASN_TYPE;

// Primitive types inherits straight off the container, complex come off complexType.
// All primitives have to serialise themselves (type, length, data), to be put straight into the packet.
// For deserialising from the parent container we check the type, then create an object of that type and call deSerialise,
// passing in the data, which pulls it out and saves it.
// If complexType, first split up its children into separate BERs, then passes the child with it's data using the same process.
// Complex types have a linked list of BER_CONTAINERS to hold its' children.

// Read a definite-length TLV header without reading beyond the supplied buffer.
// The legacy pointer-only entry points cannot validate the allocation size.
inline bool readBERHeader(const unsigned char *buf, size_t available, size_t &header, size_t &length)
{
    if (available < 2) return false;
    header = 2;
    length = buf[1];
    if (length & 0x80)
    {
        size_t octets = length & 0x7f;
        if (octets == 0 || octets == 127 || octets > available - header) return false;
        length = 0;
        while (octets--)
        {
            if (length > 65535u / 256u) return false;
            length = length * 256 + buf[header++];
        }
    }
    return length <= 65535u && length <= available - header;
}

class BER_CONTAINER
{
public:
    BER_CONTAINER(bool isPrimitive, ASN_TYPE type) : _isPrimitive(isPrimitive), _type(type){};
    virtual ~BER_CONTAINER(){};
    bool _isPrimitive;
    ASN_TYPE _type;
    unsigned short _length;
    virtual int serialise(unsigned char *buf) = 0;
    virtual bool fromBuffer(unsigned char *buf) = 0;
    virtual int getLength() = 0;
};

class NetworkAddress : public BER_CONTAINER
{
public:
    NetworkAddress() : BER_CONTAINER(true, NETWORK_ADDRESS){};
    NetworkAddress(IPAddress ip) : _value(ip), BER_CONTAINER(true, NETWORK_ADDRESS){};
    ~NetworkAddress(){};
    IPAddress _value;
    int serialise(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] NetworkAddress:serialise");
#endif
        unsigned char *ptr = buf;
        *ptr++ = _type;

        _length = 4;

        *ptr++ = _length;
        *ptr++ = _value[0];
        *ptr++ = _value[1];
        *ptr++ = _value[2];
        *ptr++ = _value[3];
        return _length + 2;
    }
    bool fromBuffer(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] NetworkAddress:fromBuffer");
#endif
        buf++; // skip Type
        _length = *buf;
        buf++;
        byte tempAddress[4];
        tempAddress[0] = *buf++;
        tempAddress[1] = *buf++;
        tempAddress[2] = *buf++;
        tempAddress[3] = *buf++;
        _value = IPAddress(tempAddress);
        return true;
    }
    int getLength()
    {
        return _length;
    }
};

class IntegerType : public BER_CONTAINER
{
public:
    IntegerType() : BER_CONTAINER(true, INTEGER){};
    IntegerType(unsigned long value) : _value(value), BER_CONTAINER(true, INTEGER){};
    ~IntegerType(){};
    unsigned long _value;
    int serialise(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] IntegerType:serialise");
#endif
        // INTEGER carries signed Integer32 bits; application types are unsigned.
        // Work on a copy so repeated serialization preserves the stored value.
        const uint32_t value = static_cast<uint32_t>(_value);
        unsigned char contents[5] = {0,
            static_cast<unsigned char>(value >> 24),
            static_cast<unsigned char>(value >> 16),
            static_cast<unsigned char>(value >> 8),
            static_cast<unsigned char>(value)};
        size_t start = _type == INTEGER ? 1 : 0;
        while (start < 4)
        {
            const bool redundantZero = contents[start] == 0 && !(contents[start + 1] & 0x80);
            const bool redundantSign = _type == INTEGER && contents[start] == 0xff && (contents[start + 1] & 0x80);
            if (!redundantZero && !redundantSign)
                break;
            ++start;
        }
        _length = sizeof(contents) - start;
        buf[0] = _type;
        buf[1] = _length;
        memcpy(buf + 2, contents + start, _length);
        return _length + 2;
    }
    bool fromBuffer(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] Integer:fromBuffer");
#endif
        // The caller must provide the complete TLV to this pointer-only API.
        if (*buf++ != _type)
            return false;
        unsigned int length = *buf++;
        if (length & 0x80)
        {
            const unsigned int octets = length & 0x7f;
            if (octets == 0 || octets == 127)
                return false;
            length = 0;
            for (unsigned int i = 0; i < octets; ++i)
            {
                length = (length << 8) | *buf++;
                if (length > 5)
                    return false;
            }
        }
        const bool isSigned = _type == INTEGER;
        if (length == 0 || length > (isSigned ? 4u : 5u))
            return false;
        if (!isSigned && ((buf[0] & 0x80) || (length == 5 && buf[0] != 0)))
            return false;
        // Extend the sign through unsigned long, including on 64-bit hosts.
        unsigned long value = isSigned && (buf[0] & 0x80) ? ~0UL : 0;
        for (unsigned int i = 0; i < length; ++i)
        {
            value = (value << 8) | *buf++;
        }
        _value = value;
        _length = length;
        return true;
    }
    int getLength()
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
    ~TimestampType(){};
};

class OctetType : public BER_CONTAINER
{
public:
    OctetType() : BER_CONTAINER(true, STRING) { _length = 0; }
    OctetType(char *value) : BER_CONTAINER(true, STRING)
    {
        const size_t length = strlen(value);
        valid = length < sizeof(_value);
        _length = valid ? length : 0;
        if (valid) memcpy(_value, value, length + 1);
    }
    char _value[SNMP_OCTETSTRING_MAX_LENGTH] = {};
    int serialise(unsigned char *buf)
    {
        if (!valid) return -1;
        // Decoded strings retain their binary length. Directly populated legacy
        // C-string values use their terminator to determine the length.
        size_t length = _length;
        if (!decoded)
        {
            length = 0;
            while (length < sizeof(_value) && _value[length]) ++length;
            if (length == sizeof(_value)) return -1;
        }
        size_t header = 2;
        buf[0] = _type;
        if (length < 128) buf[1] = length;
        else if (length < 256) { buf[1] = 0x81; buf[2] = length; header = 3; }
        else { buf[1] = 0x82; buf[2] = length >> 8; buf[3] = length; header = 4; }
        memcpy(buf + header, _value, length);
        _length = length;
        return header + length;
    }
    bool fromBuffer(unsigned char *buf)
    {
        size_t header, length;
        if (buf[0] != STRING || !readBERHeader(buf, static_cast<size_t>(-1), header, length) ||
            length >= sizeof(_value)) return false;
        memcpy(_value, buf + header, length);
        _value[length] = 0;
        _length = length;
        decoded = true;
        valid = true;
        return true;
    }
    int getLength() { return _length; }
private:
    bool decoded = false;
    bool valid = true;
};

class OIDType : public BER_CONTAINER
{
public:
    OIDType() : BER_CONTAINER(true, OID) { _length = 0; }
    OIDType(char *value) : BER_CONTAINER(true, OID)
    {
        _length = 0;
        if (strlen(value) < sizeof(_value)) strcpy(_value, value);
    }
    char _value[MAX_OID_LENGTH] = {};
    int serialise(unsigned char *buf)
    {
        size_t textLength = 0;
        while (textLength < sizeof(_value) && _value[textLength]) ++textLength;
        if (textLength == sizeof(_value)) return -1;
        const char *cursor = _value;
        const char *end = _value + textLength;
        unsigned char contents[MAX_OID_LENGTH];
        size_t length = 0, arcs = 0;
        uint64_t first = 0;
        while (cursor < end)
        {
            if (*cursor++ != '.' || cursor == end) return -1;
            uint64_t arc = 0;
            const char *digits = cursor;
            while (cursor < end && *cursor != '.')
            {
                if (*cursor < '0' || *cursor > '9') return -1;
                arc = arc * 10 + (*cursor++ - '0');
                if (arc > UINT32_MAX) return -1;
            }
            if (cursor == digits) return -1;
            if (arcs++ == 0)
            {
                if (arc > 2) return -1;
                first = arc;
                continue;
            }
            if (arcs == 2)
            {
                if (first < 2 && arc > 39) return -1;
                arc += first * 40;
            }
            unsigned char encoded[5];
            size_t start = sizeof(encoded);
            do { encoded[--start] = arc & 0x7f; arc >>= 7; } while (arc);
            if (length + sizeof(encoded) - start > sizeof(contents)) return -1;
            while (start < sizeof(encoded))
            {
                contents[length++] = encoded[start] | (start + 1 < sizeof(encoded) ? 0x80 : 0);
                ++start;
            }
        }
        if (arcs < 2) return -1;
        size_t header = 2;
        buf[0] = OID;
        if (length < 128) buf[1] = length;
        else if (length < 256) { buf[1] = 0x81; buf[2] = length; header = 3; }
        else { buf[1] = 0x82; buf[2] = length >> 8; buf[3] = length; header = 4; }
        memcpy(buf + header, contents, length);
        _length = length;
        return header + length;
    }
    bool fromBuffer(unsigned char *buf)
    {
        size_t header, length;
        if (buf[0] != OID || !readBERHeader(buf, static_cast<size_t>(-1), header, length) || length == 0) return false;
        char text[MAX_OID_LENGTH] = {};
        size_t used = 0, offset = 0;
        bool first = true;
        while (offset < length)
        {
            uint64_t arc = 0;
            unsigned char byte;
            unsigned int octets = 0;
            do
            {
                if (offset == length || ++octets > 5) return false;
                byte = buf[header + offset++];
                if (octets == 1 && byte == 0x80) return false;
                arc = (arc << 7) | (byte & 0x7f);
            } while (byte & 0x80);
            if (arc > static_cast<uint64_t>(UINT32_MAX) + (first ? 80 : 0)) return false;
            int written;
            if (first)
            {
                unsigned long root = arc < 40 ? 0 : (arc < 80 ? 1 : 2);
                written = snprintf(text + used, sizeof(text) - used, ".%lu.%lu", root,
                                   static_cast<unsigned long>(arc - root * 40));
                first = false;
            }
            else
                written = snprintf(text + used, sizeof(text) - used, ".%lu", static_cast<unsigned long>(arc));
            if (written < 0 || static_cast<size_t>(written) >= sizeof(text) - used) return false;
            used += written;
        }
        memcpy(_value, text, used + 1);
        _length = length;
        return true;
    }
    int getLength() { return _length; }
};

class NullType : public BER_CONTAINER
{
public:
    NullType() : BER_CONTAINER(true, NULLTYPE){};
    ~NullType(){};
    char _value = 0;
    int serialise(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] NullType:serialise");
#endif
        // here we print out the BER encoded ASN.1 bytes, which includes type, length and value.
        char *ptr = (char *)buf;
        *ptr = _type;
        ptr++;
        *ptr = 0;
        return 2;
    }
    bool fromBuffer(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] NullType:fromBuffer");
#endif
        _length = 0;
        return true;
    }

    int getLength()
    {
        return 0;
    }
};

class Counter64 : public BER_CONTAINER
{
public:
    Counter64() : BER_CONTAINER(true, COUNTER64){};
    Counter64(uint64_t value) : _value(value), BER_CONTAINER(true, COUNTER64){};
    ~Counter64(){};
    uint64_t _value;
    int serialise(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] Counter64:serialise");
#endif
        // Up to eight value octets plus a leading zero to preserve the positive sign.
        unsigned char contents[9];
        size_t start = sizeof(contents);
        uint64_t remaining = _value;
        do
        {
            contents[--start] = static_cast<unsigned char>(remaining & 0xff);
            remaining >>= 8;
        } while (remaining != 0);
        if (contents[start] & 0x80)
        {
            contents[--start] = 0;
        }
        _length = sizeof(contents) - start;
        buf[0] = _type;
        buf[1] = _length;
        memcpy(buf + 2, contents + start, _length);
        return _length + 2;
    }

    bool fromBuffer(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] Counter64:fromBuffer");
#endif
        // This pointer-only API requires the caller to supply the complete TLV.
        if (*buf++ != COUNTER64)
            return false;
        unsigned int length = *buf++;
        if (length & 0x80)
        {
            const unsigned int lengthOctets = length & 0x7f;
            if (lengthOctets == 0 || lengthOctets == 127)
                return false;
            length = 0;
            for (unsigned int i = 0; i < lengthOctets; ++i)
            {
                length = (length << 8) | *buf++;
                // Counter64 needs at most eight value octets and a sign octet.
                if (length > 9)
                    return false;
            }
        }
        if (length == 0 || length > 9 || (buf[0] & 0x80))
            return false;
        if (length == 9 && buf[0] != 0)
            return false;
        uint64_t value = 0;
        for (unsigned int i = 0; i < length; ++i)
        {
            value = (value << 8) | *buf++;
        }
        _length = length;
        _value = value;
        return true;
    }

    int getLength()
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
    Counter32(unsigned int value) : IntegerType(value)
    {
        _type = COUNTER32;
    };
    ~Counter32(){};
};

class Gauge : public IntegerType
{ // Unsigned int
public:
    Gauge() : IntegerType()
    {
        _type = GAUGE32;
    };
    Gauge(unsigned int value) : IntegerType(value)
    {
        _type = GAUGE32;
    };
    ~Gauge(){};
};

typedef struct BER_LINKED_LIST
{
    ~BER_LINKED_LIST()
    {
        delete next;
        next = 0;
        delete value;
        value = 0;
    }
    BER_CONTAINER *value = 0;
    struct BER_LINKED_LIST *next = 0;
} ValuesList;

class ComplexType : public BER_CONTAINER
{
public:
    ComplexType(ASN_TYPE type) : BER_CONTAINER(false, type){};
    ~ComplexType()
    {
        delete _values;
    }
    ValuesList *_values = 0;
    bool fromBuffer(unsigned char *buf)
    {
        return fromBuffer(buf, static_cast<size_t>(-1));
    }
    bool fromBuffer(unsigned char *buf, size_t available, unsigned int depth = 0)
    {
        delete _values;
        _values = nullptr;
        size_t header, length;
        if (depth >= 32 || !readBERHeader(buf, available, header, length)) return false;
        _length = length;
        size_t offset = header;
        const size_t end = header + length;
        while (offset < end)
        {
            size_t childHeader, valueLength;
            if (!readBERHeader(buf + offset, end - offset, childHeader, valueLength)) return false;
            ASN_TYPE valueType = static_cast<ASN_TYPE>(buf[offset]);
            // Primitive fixed-width types must be checked before their legacy decoders.
            if ((valueType == NULLTYPE || valueType == NOSUCHOBJECT ||
                 valueType == NOSUCHINSTANCE || valueType == ENDOFMIBVIEW) && valueLength != 0) return false;
            if (valueType == NETWORK_ADDRESS && (valueLength != 4 || childHeader != 2)) return false;
            BER_CONTAINER *newObj;
            switch (valueType)
            {
            case STRUCTURE:
            case GetRequestPDU:
            case GetNextRequestPDU:
            case GetResponsePDU:
            case SetRequestPDU:
            case GetBulkRequestPDU:
            case TrapPDU: // should never get trap, but put it in anyway
            case Trapv2PDU:
                newObj = new ComplexType(valueType);
                break;
                // primitive
            case INTEGER:
                newObj = new IntegerType();
                break;
            case STRING:
                newObj = new OctetType();
                break;
            case OID:
                newObj = new OIDType();
                break;
            case NULLTYPE:
                newObj = new NullType();
                break;
                // derived
            case NETWORK_ADDRESS:
                newObj = new NetworkAddress();
                break;
            case TIMESTAMP:
                newObj = new TimestampType();
                break;
            case COUNTER32:
                newObj = new Counter32();
                break;
            case GAUGE32:
                newObj = new Gauge();
                break;
            case COUNTER64:
                newObj = new Counter64();
                break;
                /* OPAQUE = 0x44 */

            default:
#ifdef DEBUG
                Serial.println("[DEBUG_BER] default new ComplexType");
#endif
                newObj = new ComplexType(valueType);
                break;
            }
            bool valid;
            if (!newObj->_isPrimitive)
                valid = static_cast<ComplexType *>(newObj)->fromBuffer(buf + offset, childHeader + valueLength, depth + 1);
            else
                valid = newObj->fromBuffer(buf + offset);
            if (!valid)
            {
                delete newObj;
                return false;
            }
            addValueToList(newObj);
            offset += childHeader + valueLength;
        }
        return true;
    }

    int serialise(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] ComplexType:serialise");
#endif
        int actualLength = 0;
        unsigned char *ptr = buf;
        *ptr = _type;
        ptr++;
        unsigned char *lengthPtr = ptr++;
        *lengthPtr = 0;
        ValuesList *conductor = _values;
        int tempLength = 0;
        while (conductor)
        {
            // Serial.print("about to serialise something of type: ");Serial.println(conductor->value->_type, HEX);
            delay(0);

            int length = conductor->value->serialise(ptr);
            if (length < 0) return -1;
            ptr += length;
            actualLength += length;
            conductor = conductor->next;
        }
        // printf("Length to return: %d\n", actualLength);
        if (actualLength > 127)
        {
#ifdef DEBUG_BER
            Serial.println("TOO BIG - Adding extra byte");
#endif
            // Use 0x81 for one length octet or 0x82 for two length octets.
            // Shift the serialized contents to make room for the expanded header.
            int tempVal = 1;
            if (actualLength >= 256)
            {
                *lengthPtr++ = (2 | 0x80) & 0xFF; // Two length octets, most significant first.

                tempLength += 1;
                unsigned char *endPtrPos = ++ptr;
                for (unsigned char *i = endPtrPos; i > buf + 1; i--)
                {
                    // i is the char we are moving INTO
                    *i = *(i - 1);
                }
                tempVal = 2;
                *lengthPtr++ = actualLength / 256;
            }
            else
            {
                *lengthPtr++ = (1 | 0x80) & 0xFF;
            }

            // Make room for the final length octet, copying from the end.
            unsigned char *endPtrPos = ptr + 1;
            for (unsigned char *i = endPtrPos; i > buf + tempVal; i--)
            {
                // i is the char we are moving INTO
                *i = *(i - 1);
            }
            *lengthPtr++ = actualLength % 256;

            tempLength += 1; // account for extra byte in Length param
        }
        else
        {
            *lengthPtr = actualLength;
        }
        return actualLength + 2 + tempLength;
    }

    int getLength()
    {
        return _length;
    }

    void addValueToList(BER_CONTAINER *newObj)
    {
        ValuesList *conductor = _values;
        if (_values != 0)
        {
            while (conductor->next != 0)
            {
                conductor = conductor->next;
                delay(0);
            }
            conductor->next = new ValuesList;
            conductor = conductor->next;
            conductor->value = newObj;
            conductor->next = 0;
        }
        else
        {
            _values = new ValuesList;
            _values->value = newObj;
            _values->next = 0;
        }
    }
};

#endif