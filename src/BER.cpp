#include "BER.h"

int NetworkAddress::serialise(unsigned char *buf, size_t capacity)
{
#ifdef DEBUG_BER
    Serial.println("[DEBUG_BER] NetworkAddress:serialise");
#endif
    if (capacity < 6)
        return -1;
    if (!buf)
        return 6;
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

bool NetworkAddress::fromBuffer(unsigned char *buf, size_t available)
{
#ifdef DEBUG_BER
    Serial.println("[DEBUG_BER] NetworkAddress:fromBuffer");
#endif
    size_t header, length;
    if (!readBERHeader(buf, available, header, length) || buf[0] != NETWORK_ADDRESS || length != 4)
        return false;
    _length = length;
    buf += header;
    byte tempAddress[4];
    tempAddress[0] = *buf++;
    tempAddress[1] = *buf++;
    tempAddress[2] = *buf++;
    tempAddress[3] = *buf++;
    _value = IPAddress(tempAddress);
    return true;
}

int IntegerType::serialise(unsigned char *buf, size_t capacity)
{
#ifdef DEBUG_BER
    Serial.println("[DEBUG_BER] IntegerType:serialise");
#endif
    // INTEGER carries signed Integer32 bits; application types are unsigned.
    // Work on a copy so repeated serialization preserves the stored value.
    const uint32_t value = static_cast<uint32_t>(_value);
    unsigned char contents[5] = {
        0, static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value)};
    size_t start = _type == INTEGER ? 1 : 0;
    while (start < 4)
    {
        const bool redundantZero = contents[start] == 0 && !(contents[start + 1] & 0x80);
        const bool redundantSign =
            _type == INTEGER && contents[start] == 0xff && (contents[start + 1] & 0x80);
        if (!redundantZero && !redundantSign)
            break;
        ++start;
    }
    _length = sizeof(contents) - start;
    if (capacity < static_cast<size_t>(_length) + 2)
        return -1;
    if (!buf)
        return _length + 2;
    buf[0] = _type;
    buf[1] = _length;
    memcpy(buf + 2, contents + start, _length);
    return _length + 2;
}

bool IntegerType::fromBuffer(unsigned char *buf, size_t available)
{
#ifdef DEBUG_BER
    Serial.println("[DEBUG_BER] Integer:fromBuffer");
#endif
    size_t header, length;
    if (!readBERHeader(buf, available, header, length) || buf[0] != _type)
        return false;
    buf += header;
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

OctetType::OctetType(char *value) : BER_CONTAINER(true, STRING)
{
    const size_t length = strlen(value);
    valid = length < sizeof(_value);
    _length = valid ? length : 0;
    if (valid)
        memcpy(_value, value, length + 1);
}

int OctetType::serialise(unsigned char *buf, size_t capacity)
{
    if (!valid)
        return -1;
    // Decoded strings retain their binary length. Directly populated legacy
    // C-string values use their terminator to determine the length.
    size_t length = _length;
    if (!decoded)
    {
        length = 0;
        while (length < sizeof(_value) && _value[length])
            ++length;
        if (length == sizeof(_value))
            return -1;
    }
    size_t header = length < 128 ? 2 : (length < 256 ? 3 : 4);
    if (header + length > capacity)
        return -1;
    if (!buf)
        return header + length;
    buf[0] = _type;
    if (length < 128)
        buf[1] = length;
    else if (length < 256)
    {
        buf[1] = 0x81;
        buf[2] = length;
        header = 3;
    }
    else
    {
        buf[1] = 0x82;
        buf[2] = length >> 8;
        buf[3] = length;
        header = 4;
    }
    memcpy(buf + header, _value, length);
    _length = length;
    return header + length;
}

bool OctetType::fromBuffer(unsigned char *buf, size_t available)
{
    size_t header, length;
    if (!readBERHeader(buf, available, header, length) || buf[0] != STRING ||
        length >= sizeof(_value))
        return false;
    memcpy(_value, buf + header, length);
    _value[length] = 0;
    _length = length;
    decoded = true;
    valid = true;
    return true;
}

bool RawType::fromBuffer(unsigned char *buf, size_t available)
{
    size_t header, length;
    if (!readBERHeader(buf, available, header, length) || buf[0] != _type ||
        length > sizeof(_value))
        return false;
    if (_type != OPAQUE && length != 0)
        return false;
    memcpy(_value, buf + header, length);
    _length = length;
    return true;
}

int RawType::serialise(unsigned char *buf, size_t capacity)
{
    size_t header = _length < 128 ? 2 : (_length < 256 ? 3 : 4);
    if (header + _length > capacity)
        return -1;
    if (!buf)
        return header + _length;
    buf[0] = _type;
    if (header == 2)
        buf[1] = _length;
    else if (header == 3)
    {
        buf[1] = 0x81;
        buf[2] = _length;
    }
    else
    {
        buf[1] = 0x82;
        buf[2] = _length >> 8;
        buf[3] = _length;
    }
    memcpy(buf + header, _value, _length);
    return header + _length;
}

OIDType::OIDType(char *value) : BER_CONTAINER(true, OID)
{
    _length = 0;
    if (strlen(value) < sizeof(_value))
        strcpy(_value, value);
}

int OIDType::serialise(unsigned char *buf, size_t capacity)
{
    size_t textLength = 0;
    while (textLength < sizeof(_value) && _value[textLength])
        ++textLength;
    if (textLength == sizeof(_value))
        return -1;
    const char *cursor = _value;
    const char *end = _value + textLength;
    unsigned char contents[MAX_OID_LENGTH];
    size_t length = 0, arcs = 0;
    uint64_t first = 0;
    while (cursor < end)
    {
        if (*cursor++ != '.' || cursor == end)
            return -1;
        uint64_t arc = 0;
        const char *digits = cursor;
        while (cursor < end && *cursor != '.')
        {
            if (*cursor < '0' || *cursor > '9')
                return -1;
            arc = arc * 10 + (*cursor++ - '0');
            if (arc > UINT32_MAX)
                return -1;
        }
        if (cursor == digits)
            return -1;
        if (arcs++ == 0)
        {
            if (arc > 2)
                return -1;
            first = arc;
            continue;
        }
        if (arcs == 2)
        {
            if (first < 2 && arc > 39)
                return -1;
            arc += first * 40;
        }
        unsigned char encoded[5];
        size_t start = sizeof(encoded);
        do
        {
            encoded[--start] = arc & 0x7f;
            arc >>= 7;
        } while (arc);
        if (length + sizeof(encoded) - start > sizeof(contents))
            return -1;
        while (start < sizeof(encoded))
        {
            contents[length++] = encoded[start] | (start + 1 < sizeof(encoded) ? 0x80 : 0);
            ++start;
        }
    }
    if (arcs < 2)
        return -1;
    size_t header = length < 128 ? 2 : (length < 256 ? 3 : 4);
    if (header + length > capacity)
        return -1;
    if (!buf)
        return header + length;
    buf[0] = OID;
    if (length < 128)
        buf[1] = length;
    else if (length < 256)
    {
        buf[1] = 0x81;
        buf[2] = length;
        header = 3;
    }
    else
    {
        buf[1] = 0x82;
        buf[2] = length >> 8;
        buf[3] = length;
        header = 4;
    }
    memcpy(buf + header, contents, length);
    _length = length;
    return header + length;
}

bool OIDType::fromBuffer(unsigned char *buf, size_t available)
{
    size_t header, length;
    if (!readBERHeader(buf, available, header, length) || buf[0] != OID || length == 0)
        return false;
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
            if (offset == length || ++octets > 5)
                return false;
            byte = buf[header + offset++];
            if (octets == 1 && byte == 0x80)
                return false;
            arc = (arc << 7) | (byte & 0x7f);
        } while (byte & 0x80);
        if (arc > static_cast<uint64_t>(UINT32_MAX) + (first ? 80 : 0))
            return false;
        int written;
        if (first)
        {
            unsigned long root = arc < 40 ? 0 : (arc < 80 ? 1 : 2);
            written = snprintf(text + used, sizeof(text) - used, ".%lu.%lu", root,
                               static_cast<unsigned long>(arc - root * 40));
            first = false;
        }
        else
            written =
                snprintf(text + used, sizeof(text) - used, ".%lu", static_cast<unsigned long>(arc));
        if (written < 0 || static_cast<size_t>(written) >= sizeof(text) - used)
            return false;
        used += written;
    }
    memcpy(_value, text, used + 1);
    _length = length;
    return true;
}

int NullType::serialise(unsigned char *buf, size_t capacity)
{
#ifdef DEBUG_BER
    Serial.println("[DEBUG_BER] NullType:serialise");
#endif
    if (capacity < 2)
        return -1;
    if (!buf)
        return 2;
    // NULL has a zero-length value, so its TLV contains only the tag and length.
    char *ptr = (char *)buf;
    *ptr = _type;
    ptr++;
    *ptr = 0;
    return 2;
}

bool NullType::fromBuffer(unsigned char *buf, size_t available)
{
#ifdef DEBUG_BER
    Serial.println("[DEBUG_BER] NullType:fromBuffer");
#endif
    size_t header, length;
    if (!readBERHeader(buf, available, header, length) || buf[0] != NULLTYPE || length != 0)
        return false;
    _length = 0;
    return true;
}

int Counter64::serialise(unsigned char *buf, size_t capacity)
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
    if (capacity < static_cast<size_t>(_length) + 2)
        return -1;
    if (!buf)
        return _length + 2;
    buf[0] = _type;
    buf[1] = _length;
    memcpy(buf + 2, contents + start, _length);
    return _length + 2;
}

bool Counter64::fromBuffer(unsigned char *buf, size_t available)
{
#ifdef DEBUG_BER
    Serial.println("[DEBUG_BER] Counter64:fromBuffer");
#endif
    size_t header, length;
    if (!readBERHeader(buf, available, header, length) || buf[0] != _type)
        return false;
    buf += header;
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

BER_LINKED_LIST::~BER_LINKED_LIST()
{
    while (next)
    {
        auto *node = next;
        next = node->next;
        node->next = nullptr;
        delete node;
    }
    delete value;
    value = 0;
}

bool ComplexType::decode(unsigned char *buf, size_t available, unsigned int depth)
{
    delete _values;
    _values = nullptr;
    size_t header, length;
    if (depth >= 32 || !readBERHeader(buf, available, header, length))
        return false;
    _length = length;
    size_t offset = header;
    const size_t end = header + length;
    while (offset < end)
    {
        size_t childHeader, valueLength;
        if (!readBERHeader(buf + offset, end - offset, childHeader, valueLength))
            return false;
        ASN_TYPE valueType = static_cast<ASN_TYPE>(buf[offset]);
        // Enforce the payload lengths of NULL, exceptions, and IPv4 addresses.
        if ((valueType == NULLTYPE || valueType == NOSUCHOBJECT || valueType == NOSUCHINSTANCE ||
             valueType == ENDOFMIBVIEW) &&
            valueLength != 0)
            return false;
        if (valueType == NETWORK_ADDRESS && valueLength != 4)
            return false;
        BER_CONTAINER *newObj;
        switch (valueType)
        {
        case STRUCTURE:
        case GetRequestPDU:
        case GetNextRequestPDU:
        case GetResponsePDU:
        case SetRequestPDU:
        case GetBulkRequestPDU:
        case TrapPDU: // Recognized here; the response parser rejects trap PDUs.
        case InformRequestPDU:
        case Trapv2PDU:
            newObj = new (std::nothrow) ComplexType(valueType);
            break;
            // primitive
        case INTEGER:
            newObj = new (std::nothrow) IntegerType();
            break;
        case STRING:
            newObj = new (std::nothrow) OctetType();
            break;
        case OID:
            newObj = new (std::nothrow) OIDType();
            break;
        case NULLTYPE:
            newObj = new (std::nothrow) NullType();
            break;
            // derived
        case NETWORK_ADDRESS:
            newObj = new (std::nothrow) NetworkAddress();
            break;
        case TIMESTAMP:
            newObj = new (std::nothrow) TimestampType();
            break;
        case COUNTER32:
            newObj = new (std::nothrow) Counter32();
            break;
        case GAUGE32:
            newObj = new (std::nothrow) Gauge();
            break;
        case COUNTER64:
            newObj = new (std::nothrow) Counter64();
            break;
        case OPAQUE:
        case NOSUCHOBJECT:
        case NOSUCHINSTANCE:
        case ENDOFMIBVIEW:
            newObj = new (std::nothrow) RawType(valueType);
            break;
        default:
            return false;
        }
        if (!newObj)
            return false;
        bool valid;
        if (!newObj->_isPrimitive)
            valid = static_cast<ComplexType *>(newObj)->decode(
                buf + offset, childHeader + valueLength, depth + 1);
        else
            valid = newObj->fromBuffer(buf + offset, childHeader + valueLength);
        if (!valid)
        {
            delete newObj;
            return false;
        }
        if (!addValueToList(newObj))
            return false;
        offset += childHeader + valueLength;
    }
    return true;
}

int ComplexType::serialise(unsigned char *buf, size_t capacity)
{
    // Measure before writing, so insufficient capacity leaves the buffer unchanged.
    size_t length = 0;
    for (ValuesList *entry = _values; entry; entry = entry->next)
    {
        int child = entry->value->serialise(nullptr);
        if (child < 0 || static_cast<size_t>(child) > 65535u - length)
            return -1;
        length += child;
    }
    size_t header = length < 128 ? 2 : (length < 256 ? 3 : 4);
    if (header + length > capacity)
        return -1;
    if (!buf)
        return header + length;
    buf[0] = _type;
    if (header == 2)
        buf[1] = length;
    else if (header == 3)
    {
        buf[1] = 0x81;
        buf[2] = length;
    }
    else
    {
        buf[1] = 0x82;
        buf[2] = length >> 8;
        buf[3] = length;
    }
    size_t offset = header;
    for (ValuesList *entry = _values; entry; entry = entry->next)
    {
        int child = entry->value->serialise(buf + offset, capacity - offset);
        if (child < 0)
            return -1;
        offset += child;
    }
    _length = length;
    return offset;
}

bool ComplexType::addValueToList(BER_CONTAINER *child)
{
    if (!child)
        return false;
    ValuesList *node = new (std::nothrow) ValuesList();
    if (!node)
    {
        delete child;
        return false;
    }
    node->value = child;
    ValuesList **tail = &_values;
    while (*tail)
        tail = &(*tail)->next;
    *tail = node;
    return true;
}

bool readBERHeader(const unsigned char *buf, size_t available, size_t &header, size_t &length)
{
    if (!buf || available < 2)
        return false;
    header = 2;
    length = buf[1];
    if (length & 0x80)
    {
        size_t octets = length & 0x7f;
        if (octets == 0 || octets == 127 || octets > available - header)
            return false;
        length = 0;
        while (octets--)
        {
            if (length > 65535u / 256u)
                return false;
            length = length * 256 + buf[header++];
        }
    }
    return length <= 65535u && length <= available - header;
}
