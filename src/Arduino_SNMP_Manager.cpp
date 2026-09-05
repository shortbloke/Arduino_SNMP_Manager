#include "Arduino_SNMP_Manager.h"
#include "SNMPUDP.h"

#include <utility>

SNMPManager::~SNMPManager()
{
    for (ValueCallbacks *entry = callbacks; entry; entry = entry->next)
        if (entry->value)
            entry->value->release();
    delete callbacks;
}

SNMPManager::SNMPManager(SNMPManager &&other) : SNMPManager()
{
    std::swap(_community, other._community);
    std::swap(_udp, other._udp);
    std::swap(callbacks, other.callbacks);
    callbacksCursor = callbacks;
    other.callbacksCursor = other.callbacks;
}

void SNMPManager::setUDP(UDP *udp)
{
    if (_udp)
    {
        _udp->stop();
    }
    _udp = udp;
    this->begin();
}

bool SNMPManager::begin()
{
    if (!_udp)
        return false;
    return _udp->begin(162) != 0;
}

bool SNMPManager::loop()
{
    if (!_udp)
    {
        return false;
    }
    receivePacket(_udp->parsePacket());
    return true;
}

void SNMPManager::printPacket(int len)
{
    Serial.print("[DEBUG] packet: ");
    for (int i = 0; i < len; i++)
    {
        if (_packetBuffer[i] < 16)
            Serial.print('0');
        Serial.print(_packetBuffer[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
}

bool SNMPManager::testParsePacket(String testPacket)
{
    // Parse directly from the String storage; no input-sized stack allocation.
    const char *cursor = testPacket.c_str();
    size_t length = 0;
    auto hexDigit = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    while (*cursor)
    {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
            ++cursor;
        if (!*cursor)
            break;
        int high = hexDigit(*cursor++);
        if (high < 0 || !*cursor)
            return false;
        int low = hexDigit(*cursor++);
        if (low < 0 || length == sizeof(_packetBuffer))
            return false;
        if (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n')
            return false;
        _packetBuffer[length++] = static_cast<unsigned char>((high << 4) | low);
    }
#ifdef DEBUG
    printPacket(static_cast<int>(length));
#endif
    return parsePacket(length);
}

bool SNMPManager::receivePacket(int packetLength)
{
    if (packetLength == 0)
        return false;
    if (packetLength < 0 || packetLength > SNMP_PACKET_LENGTH)
    {
        snmp_detail::discardDatagram(*_udp, packetLength, _packetBuffer, sizeof(_packetBuffer));
        return false;
    }
#ifdef DEBUG
    Serial.print(F("[DEBUG] Packet Length: "));
    Serial.print(packetLength);
    Serial.print(F(" From Address: "));
    Serial.println(_udp->remoteIP());
#endif

    memset(_packetBuffer, 0, SNMP_PACKET_LENGTH);
    int len = packetLength;
    const int received = _udp->read(_packetBuffer, len);
    if (received != len)
    {
        snmp_detail::discardDatagram(*_udp, len - (received > 0 ? received : 0), _packetBuffer,
                                     sizeof(_packetBuffer));
        return false;
    }

#ifdef DEBUG
    printPacket(len);
#endif

    return parsePacket(len);
}

bool SNMPManager::parsePacket(size_t length)
{
    SNMPGetResponse *snmpgetresponse = new (std::nothrow) SNMPGetResponse();
    if (!snmpgetresponse)
        return false;
    if (snmpgetresponse->parseFrom(_packetBuffer, length))
    {
        if (snmpgetresponse->requestType == GetResponsePDU)
        {
            // parseFrom exposes v1/v2c as 1/2, rather than their wire values 0/1.
            if ((snmpgetresponse->version != 1 && snmpgetresponse->version != 2) ||
                (strlen(_community) != snmpgetresponse->communityLength ||
                 memcmp(_community, snmpgetresponse->communityString,
                        snmpgetresponse->communityLength) != 0))
            {
                Serial.print(F("Invalid community or version - Community: "));
                Serial.print(snmpgetresponse->communityString);
                Serial.print(F(" - Version: "));
                Serial.println(snmpgetresponse->version);
                delete snmpgetresponse;
                return false;
            }
#ifdef DEBUG
            Serial.print(F("[DEBUG] Community: "));
            Serial.println(snmpgetresponse->communityString);
            Serial.print(F("[DEBUG] SNMP Version: "));
            Serial.println(snmpgetresponse->version);
#endif
            // A PDU-level error prevents all updates; per-binding exceptions are handled below.
            if (snmpgetresponse->errorStatus != 0)
            {
                for (ValueCallbacks *entry = callbacks; entry && entry->value; entry = entry->next)
                {
                    ValueCallback *callback = entry->value;
                    callback->consume(snmpgetresponse->requestID, _udp, _udp->remoteIP());
                }
                delete snmpgetresponse;
                return false;
            }
            snmpgetresponse->varBindsCursor = snmpgetresponse->varBinds;
            while (snmpgetresponse->varBindsCursor && snmpgetresponse->varBindsCursor->value)
            {
                char *responseOID = snmpgetresponse->varBindsCursor->value->oid->_value;
                IPAddress responseIP = _udp->remoteIP();
                ASN_TYPE responseType = snmpgetresponse->varBindsCursor->value->type;
                BER_CONTAINER *responseContainer = snmpgetresponse->varBindsCursor->value->value;
#ifdef DEBUG
                Serial.print(F("[DEBUG] Response from: "));
                Serial.print(responseIP);
                Serial.print(F(" - OID: "));
                Serial.println(responseOID);
#endif
                ValueCallback *callback = findCallback(responseIP, responseOID);
                if (!callback)
                {
                    Serial.print(F(
                        "Matching callback not found for received SNMP response. Response OID: "));
                    Serial.print(responseOID);
                    Serial.print(F(" - From IP Address: "));
                    Serial.println(responseIP);
                    delete snmpgetresponse;
                    return false;
                }
                if (callback->hasTrackedRequests() &&
                    !callback->consume(snmpgetresponse->requestID, _udp, responseIP))
                {
                    snmpgetresponse->varBindsCursor = snmpgetresponse->varBindsCursor->next;
                    continue;
                }
                // An exception belongs to this binding, not the whole response.
                if (responseType == NOSUCHOBJECT || responseType == NOSUCHINSTANCE ||
                    responseType == ENDOFMIBVIEW)
                {
                    snmpgetresponse->varBindsCursor = snmpgetresponse->varBindsCursor->next;
                    continue;
                }
                ASN_TYPE callbackType = callback->type;
                if (callbackType != responseType)
                {
                    Serial.print(F("Incorrect Callback type. Expected: "));
                    Serial.print(callbackType);
                    Serial.print(F(" Received: "));
                    Serial.print(responseType);
                    Serial.print(F(" - When calling: "));
                    Serial.println(responseOID);
                    delete snmpgetresponse;
                    snmpgetresponse = 0;
                    return false;
                }
                switch (callbackType)
                {
                case STRING:
                case OPAQUE:
                {
                    StringCallback *destination = static_cast<StringCallback *>(callback);
                    const unsigned char *source =
                        responseType == STRING
                            ? reinterpret_cast<unsigned char *>(
                                  static_cast<OctetType *>(responseContainer)->_value)
                            : static_cast<RawType *>(responseContainer)->_value;
                    size_t length = responseContainer->getLength();
                    if (destination->bytes)
                    {
                        if (length > destination->capacity)
                            break;
                        memcpy(destination->bytes, source, length);
                        *destination->length = length;
                    }
                    else
                    {
                        if (!destination->value || !*destination->value ||
                            length >= destination->capacity || memchr(source, 0, length))
                            break;
                        memcpy(*destination->value, source, length);
                        (*destination->value)[length] = 0;
                    }
                }
                break;
                case OID:
                {
                    OIDCallback *destination = static_cast<OIDCallback *>(callback);
                    const char *source = static_cast<OIDType *>(responseContainer)->_value;
                    size_t length = strlen(source);
                    if (!destination->value || length >= destination->capacity)
                        break;
                    memcpy(destination->value, source, length + 1);
                }
                break;
                case INTEGER:
                {
#ifdef DEBUG
                    Serial.println("[DEBUG] Type: Integer");
#endif
                    IntegerCallback *callbackValue = static_cast<IntegerCallback *>(callback);
                    const unsigned long raw = static_cast<IntegerType *>(responseContainer)->_value;
                    if (!callbackValue->isFloat)
                    {
                        *callbackValue->value = raw;
                    }
                    else
                    {
                        // Convert signed Integer32 tenths through the registered float destination.
                        *callbackValue->floatValue =
                            static_cast<float>(static_cast<int32_t>(raw)) / 10.0f;
                    }
                }
                break;
                case COUNTER32:
                {
#ifdef DEBUG
                    Serial.println("[DEBUG] Type: Counter32");
#endif
                    *(((Counter32Callback *)callback)->value) =
                        ((Counter32 *)responseContainer)->_value;
                }
                break;
                case COUNTER64:
                {
#ifdef DEBUG
                    Serial.println("[DEBUG] Type: Counter64");
#endif
                    *(((Counter64Callback *)callback)->value) =
                        ((Counter64 *)responseContainer)->_value;
                }
                break;
                case GAUGE32:
                {
#ifdef DEBUG
                    Serial.println("[DEBUG] Type: Gauge32");
#endif
                    *(((Gauge32Callback *)callback)->value) = ((Gauge *)responseContainer)->_value;
                }
                break;
                case TIMESTAMP:
                {
#ifdef DEBUG
                    Serial.println("[DEBUG] Type: TimeStamp");
#endif
                    *(((TimestampCallback *)callback)->value) =
                        ((TimestampType *)responseContainer)->_value;
                }
                break;
                default:
                {
#ifdef DEBUG
                    Serial.print(F("[DEBUG] Unsupported Type: "));
                    Serial.print(callbackType);
#endif
                }
                break;
                }
                snmpgetresponse->varBindsCursor = snmpgetresponse->varBindsCursor->next;
                if (!snmpgetresponse->varBindsCursor->value)
                {
                    break;
                }
            } // End while
        } // End if GetResponsePDU
    }
    else
    {
#ifndef SUPPRESS_ERROR_FAILED_PARSE
        Serial.println(F("SNMPGETRESPONSE: FAILED TO PARSE"));
#endif
        delete snmpgetresponse;
        return false;
    }
#ifdef DEBUG
    Serial.println(F("[DEBUG] SNMPGETRESPONSE: SUCCESS"));
#endif
    delete snmpgetresponse;
    return true;
}

ValueCallback *SNMPManager::findCallback(IPAddress ip, const char *oid)
{
    callbacksCursor = callbacks;

    if (callbacksCursor && callbacksCursor->value)
    {
        while (true)
        {
            if ((strcmp(callbacksCursor->value->OID, oid) == 0) &&
                (callbacksCursor->value->ip == ip))
            {
#ifdef DEBUG
                Serial.println(F("[DEBUG] Found callback with matching IP"));
#endif
                return callbacksCursor->value;
            }
            if (callbacksCursor->next)
            {
                callbacksCursor = callbacksCursor->next;
            }
            else
            {
#ifdef DEBUG
                Serial.println(F("[DEBUG] No matching callback found."));
#endif
                break;
            }
        }
    }
    return 0;
}

ValueCallback *SNMPManager::addStringHandler(IPAddress ip, const char *oid, char **value,
                                             size_t capacity)
{
    if (!oid || !value)
        return nullptr;
    ValueCallback *callback = new (std::nothrow) StringCallback();
    if (!callback)
        return nullptr;
    callback->OID = static_cast<char *>(malloc(strlen(oid) + 1));
    if (!callback->OID)
    {
        delete callback;
        return nullptr;
    }
    strcpy(callback->OID, oid);
    ((StringCallback *)callback)->value = value;
    ((StringCallback *)callback)->capacity = capacity;
    callback->ip = ip;
    if (!addHandler(callback))
    {
        callback->release();
        return nullptr;
    }
    return callback;
}

ValueCallback *SNMPManager::addIntegerHandler(IPAddress ip, const char *oid, int32_t *value)
{
    if (!oid || !value)
        return nullptr;
    ValueCallback *callback = new (std::nothrow) IntegerCallback();
    if (!callback)
        return nullptr;
    callback->OID = static_cast<char *>(malloc(strlen(oid) + 1));
    if (!callback->OID)
    {
        delete callback;
        return nullptr;
    }
    strcpy(callback->OID, oid);
    ((IntegerCallback *)callback)->value = value;
    ((IntegerCallback *)callback)->isFloat = false;
    callback->ip = ip;
    if (!addHandler(callback))
    {
        callback->release();
        return nullptr;
    }
    return callback;
}

ValueCallback *SNMPManager::addFloatHandler(IPAddress ip, const char *oid, float *value)
{
    if (!oid || !value)
        return nullptr;
    ValueCallback *callback = new (std::nothrow) IntegerCallback();
    if (!callback)
        return nullptr;
    callback->OID = static_cast<char *>(malloc(strlen(oid) + 1));
    if (!callback->OID)
    {
        delete callback;
        return nullptr;
    }
    strcpy(callback->OID, oid);
    ((IntegerCallback *)callback)->floatValue = value;
    ((IntegerCallback *)callback)->isFloat = true;
    callback->ip = ip;
    if (!addHandler(callback))
    {
        callback->release();
        return nullptr;
    }
    return callback;
}

ValueCallback *SNMPManager::addTimestampHandler(IPAddress ip, const char *oid, uint32_t *value)
{
    if (!oid || !value)
        return nullptr;
    ValueCallback *callback = new (std::nothrow) TimestampCallback();
    if (!callback)
        return nullptr;
    callback->OID = static_cast<char *>(malloc(strlen(oid) + 1));
    if (!callback->OID)
    {
        delete callback;
        return nullptr;
    }
    strcpy(callback->OID, oid);
    ((TimestampCallback *)callback)->value = value;
    callback->ip = ip;
    if (!addHandler(callback))
    {
        callback->release();
        return nullptr;
    }
    return callback;
}

ValueCallback *SNMPManager::addOIDHandler(IPAddress ip, const char *oid, char *value,
                                          size_t capacity)
{
    if (!oid || !value)
        return nullptr;
    ValueCallback *callback = new (std::nothrow) OIDCallback();
    if (!callback)
        return nullptr;
    callback->OID = static_cast<char *>(malloc(strlen(oid) + 1));
    if (!callback->OID)
    {
        delete callback;
        return nullptr;
    }
    strcpy(callback->OID, oid);
    ((OIDCallback *)callback)->capacity = capacity;
    ((OIDCallback *)callback)->value = value;
    callback->ip = ip;
    if (!addHandler(callback))
    {
        callback->release();
        return nullptr;
    }
    return callback;
}

ValueCallback *SNMPManager::addCounter64Handler(IPAddress ip, const char *oid, uint64_t *value)
{
    if (!oid || !value)
        return nullptr;
    ValueCallback *callback = new (std::nothrow) Counter64Callback();
    if (!callback)
        return nullptr;
    callback->OID = static_cast<char *>(malloc(strlen(oid) + 1));
    if (!callback->OID)
    {
        delete callback;
        return nullptr;
    }
    strcpy(callback->OID, oid);
    ((Counter64Callback *)callback)->value = value;
    callback->ip = ip;
    if (!addHandler(callback))
    {
        callback->release();
        return nullptr;
    }
    return callback;
}

ValueCallback *SNMPManager::addCounter32Handler(IPAddress ip, const char *oid, uint32_t *value)
{
    if (!oid || !value)
        return nullptr;
    ValueCallback *callback = new (std::nothrow) Counter32Callback();
    if (!callback)
        return nullptr;
    callback->OID = static_cast<char *>(malloc(strlen(oid) + 1));
    if (!callback->OID)
    {
        delete callback;
        return nullptr;
    }
    strcpy(callback->OID, oid);
    ((Counter32Callback *)callback)->value = value;
    callback->ip = ip;
    if (!addHandler(callback))
    {
        callback->release();
        return nullptr;
    }
    return callback;
}

ValueCallback *SNMPManager::addGaugeHandler(IPAddress ip, const char *oid, uint32_t *value)
{
    if (!oid || !value)
        return nullptr;
    ValueCallback *callback = new (std::nothrow) Gauge32Callback();
    if (!callback)
        return nullptr;
    callback->OID = static_cast<char *>(malloc(strlen(oid) + 1));
    if (!callback->OID)
    {
        delete callback;
        return nullptr;
    }
    strcpy(callback->OID, oid);
    ((Gauge32Callback *)callback)->value = value;
    callback->ip = ip;
    if (!addHandler(callback))
    {
        callback->release();
        return nullptr;
    }
    return callback;
}

ValueCallback *SNMPManager::addBinaryHandler(ASN_TYPE type, IPAddress ip, const char *oid,
                                             unsigned char *value, size_t capacity, size_t *length)
{
    if ((type != STRING && type != OPAQUE) || !value || !length || !oid)
        return nullptr;
    StringCallback *callback = new (std::nothrow) StringCallback(type);
    if (!callback)
        return nullptr;
    callback->OID = static_cast<char *>(malloc(strlen(oid) + 1));
    if (!callback->OID)
    {
        delete callback;
        return nullptr;
    }
    strcpy(callback->OID, oid);
    callback->bytes = value;
    callback->capacity = capacity;
    callback->length = length;
    callback->ip = ip;
    if (!addHandler(callback))
    {
        callback->release();
        return nullptr;
    }
    return callback;
}

ValueCallback *SNMPManager::addOctetHandler(IPAddress ip, const char *oid, unsigned char *value,
                                            size_t capacity, size_t *length)
{
    return addBinaryHandler(STRING, ip, oid, value, capacity, length);
}

ValueCallback *SNMPManager::addOpaqueHandler(IPAddress ip, const char *oid, unsigned char *value,
                                             size_t capacity, size_t *length)
{
    return addBinaryHandler(OPAQUE, ip, oid, value, capacity, length);
}

bool SNMPManager::addHandler(ValueCallback *callback)
{
    if (!callback)
        return false;
    ValueCallbacks **tail = &callbacks;
    while (*tail && (*tail)->value)
    {
        if ((*tail)->value == callback)
            return true;
        tail = &(*tail)->next;
    }
    if (!*tail)
        *tail = new (std::nothrow) ValueCallbacks();
    if (!*tail)
        return false;
    (*tail)->value = callback;
    callbacksCursor = *tail;
    return true;
}
