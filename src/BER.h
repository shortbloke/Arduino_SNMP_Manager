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
    OctetType() : BER_CONTAINER(true, STRING){};
    OctetType(char *value) : BER_CONTAINER(true, STRING)
    {
        strncpy(_value, value, sizeof(_value));
        _value[sizeof(_value)] = 0;
    };
    ~OctetType(){};
    char _value[SNMP_OCTETSTRING_MAX_LENGTH];
    int serialise(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] OctetType:serialise");
#endif
        // here we print out the BER encoded ASN.1 bytes, which includes type, length and value.
        char *ptr = (char *)buf;
        int numExtraBytes = 0;
        char temp[SNMP_OCTETSTRING_MAX_LENGTH];
        int valueLength = sprintf(temp, "%s", _value);

        *ptr++ = _type; // Set the type identifier
        // Long-form lengths start with 0x80 plus the number of length octets.
        if (valueLength > 127)
        {
            numExtraBytes++;       // Need an extra byte
            if (valueLength >= 256) // Lengths 256 and above need two octets.
            {
                numExtraBytes++; // Need another extra byte to store the length
            }
            *ptr++ = (numExtraBytes | 0x80); // 0x8x where x is the number of bytes which provide the total string length
            if (valueLength >= 256)
            {
                *ptr++ = valueLength / 256;
                valueLength = valueLength % 256;
            }
            *ptr++ = valueLength;
        }
        else
        {
            *ptr++ = valueLength;
        }
        _length = sprintf(ptr, "%s", _value);
        return _length + numExtraBytes + 2;
    }
    bool fromBuffer(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] OctetType:fromBuffer");
#endif
        buf++; // skip Type
        _length = *buf;
        // length should be treated as: if first byte is 0x8x, the x is how many bytes follow
        if (_length > 127)
        {
            int numBytes = _length &= 0x7F;
            unsigned int special_length = 0;
            for (int k = 0; k < numBytes; k++)
            {
                buf++;
                special_length <<= 8;
                special_length |= *buf;
            }
            _length = special_length;
        }
        buf++;
        memset(_value, 0, sizeof(_value)); // Null out _value
        if (_length < sizeof(_value))
        {
            strncpy(_value, (char *)buf, _length); // Copy buffer to Value, using length from ASN structure.
        }
        else
        {
            Serial.println(F("OctetString too large, adjust SNMP_OCTETSTRING_MAX_LENGTH. String Truncated."));
            strncpy(_value, (char *)buf, 253); // Copy truncated buffer to Value
        }
        return true;
    }
    int getLength()
    {
        return _length;
    }
};

class OIDType : public BER_CONTAINER
{
public:
    OIDType() : BER_CONTAINER(true, OID){};
    OIDType(char *value) : BER_CONTAINER(true, OID)
    {
        strncpy(_value, value, MAX_OID_LENGTH);
    };
    ~OIDType(){};
    char _value[MAX_OID_LENGTH];
    int serialise(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] OIDType:serialise");
#endif
        // here we print out the BER encoded ASN.1 bytes, which includes type, length and value.
        char *ptr = (char *)buf;
        *ptr = _type;
        ptr++;
        char *lengthPtr = ptr;
        ptr++;
        *ptr = 0x2b;
        char *internalPtr = ++ptr;
        char *valuePtr = &_value[5];
        _length = 3;
        bool toBreak = false;
        while (true)
        {
            char *start = valuePtr;
            char *end = strchr(start, '.');

            if (!end)
            {
                end = strchr(start, 0);
                toBreak = true;
            }
            char tempBuf[12];
            memset(tempBuf, 0, 12);
            //            char* tempBuf = (char*) malloc(sizeof(char) * (end-start));
            strncpy(tempBuf, start, end - start + 1);
            long tempVal;
            char *pEnd;
            tempVal = (uint32_t)strtoul(tempBuf, &pEnd, 10);
            if (tempVal < 128)
            {
                _length += 1;
                *ptr++ = (char)tempVal;
            }
            else
            {
                // Serial.print("large num: ");Serial.println(tempVal);
                // FIXME: This will only encode integers upto 4 bytes. Ideally this should be a loop.
                if (tempVal / 128 / 128 > 128)
                {
                    *ptr++ = ((tempVal / 128 / 128 / 128 ) | 0x80) & 0xFF;
                    _length += 1;
                }
                if (tempVal / 128 > 128)
                {
                    *ptr++ = ((tempVal / 128 / 128) | 0x80) & 0xFF;
                    _length += 1;
                }
                *ptr++ = ((tempVal / 128) | 0x80) & 0xFF;

                *ptr++ = tempVal % 128 & 0xFF;
                _length += 2;
            }

            valuePtr = end + 1;

            //            free(tempBuf);
            if (toBreak)
                break;
            // delay(1);
        }
        *lengthPtr = _length - 2;

        return _length;
    }
    bool fromBuffer(unsigned char *buf)
    {
#ifdef DEBUG_BER
        Serial.println("[DEBUG_BER] OIDType:fromBuffer");
#endif
        buf++; // skip Type
        _length = *buf;
        buf++;
        buf++;
        memset(_value, 0, 128);
        _value[0] = '.';
        _value[1] = '1';
        _value[2] = '.';
        _value[3] = '3'; // we fill in the first two bytes already
        char *ptr = &_value[4];
        char i = _length - 1;
        while (i > 0)
        {
            if (*buf < 128)
            { // we can keep raw
                ptr += sprintf(ptr, ".%d", *buf);
                i--;
                buf++;
            }
            else
            {                                   // we have to do the special >128 thing
                long value = 0;                 // keep track of the actual thing
                unsigned char n = 0;            // count how many large bits have been set
                unsigned char tempBuf[6] = {0}; // no bigger than 4 bytes
                while (*buf > 127)
                {
                    i--;
                    *buf &= 0x7F;
                    tempBuf[n] = *buf;
                    n++;
                    buf++;
                }
                value = *buf;
                buf++;
                i--;
                for (unsigned char k = 0; k < n; k++)
                {
                    value += (pow(128, (n - k))) * tempBuf[k];
                }
                ptr += sprintf(ptr, ".%d", value);
            }
        }
        // Serial.print("OID: " );Serial.println(_value);
        //        memcpy(_value, buf, _length);
        return true;
    }

    int getLength()
    {
        return _length;
    }
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