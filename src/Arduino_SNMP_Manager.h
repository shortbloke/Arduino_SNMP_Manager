// #define DEBUG_BER

#ifndef SNMPManager_h
#define SNMPManager_h

#ifndef UDP_TX_PACKET_MAX_SIZE
#define UDP_TX_PACKET_MAX_SIZE 484
#endif

#ifndef SNMP_PACKET_LENGTH
#if defined(ESP32)
#define SNMP_PACKET_LENGTH 1500 // This will limit the size of packets which can be handled.
#else
#define SNMP_PACKET_LENGTH 512 // This value may need to be made smaller for lower memory devices.
#endif
#endif

#define MIN(X, Y) ((X < Y) ? X : Y)

#include <Udp.h>
#include <utility>

#include "BER.h"
#include "VarBinds.h"

class ValueCallback
{
public:
    ValueCallback(ASN_TYPE atype) : type(atype){};
    // Registrations own their OID; destinations and transports remain caller-owned.
    virtual ~ValueCallback() { free(OID); }
    void retain() { ++references; }
    void release() { if (--references == 0) delete this; }
    ValueCallback(const ValueCallback&) = delete;
    ValueCallback& operator=(const ValueCallback&) = delete;
private:
    size_t references = 1;
public:
    IPAddress ip;
    char *OID = nullptr;
    ASN_TYPE type;
    bool overwritePrefix = false;
    // One outstanding request per callback; only successful sends replace it.
    bool requestTracked = false;
    bool requestPending = false;
    unsigned long expectedRequestID = 0;
    UDP *requestUDP = nullptr;
    IPAddress requestPeer;

};

class IntegerCallback : public ValueCallback
{
public:
    IntegerCallback() : ValueCallback(INTEGER){};
    int *value;
    bool isFloat = false;
};

class TimestampCallback : public ValueCallback
{
public:
    TimestampCallback() : ValueCallback(TIMESTAMP){};
    uint32_t *value;
};

class StringCallback : public ValueCallback
{
public:
    StringCallback(ASN_TYPE type = STRING) : ValueCallback(type){};
    char **value = nullptr;
    unsigned char *bytes = nullptr;
    size_t *length = nullptr;
    size_t capacity = static_cast<size_t>(-1);
};

class OIDCallback : public ValueCallback
{
public:
    OIDCallback() : ValueCallback(ASN_TYPE::OID){};
    char *value;
    size_t capacity = static_cast<size_t>(-1);
};

class Counter32Callback : public ValueCallback
{
public:
    Counter32Callback() : ValueCallback(ASN_TYPE::COUNTER32){};
    uint32_t *value;
};

class Gauge32Callback : public ValueCallback
{
public:
    Gauge32Callback() : ValueCallback(ASN_TYPE::GAUGE32){};
    uint32_t *value;
};

class Counter64Callback : public ValueCallback
{
public:
    Counter64Callback() : ValueCallback(ASN_TYPE::COUNTER64){};
    uint64_t *value;
};

typedef struct ValueCallbackList
{
    ~ValueCallbackList()
    {
        delete next;
    }
    ValueCallback *value = nullptr;
    struct ValueCallbackList *next = 0;
} ValueCallbacks;

#include "SNMPGet.h"
#include "SNMPGetResponse.h"

class SNMPManager
{
public:
    SNMPManager(){};
    SNMPManager(const char *community) : _community(community ? community : "public"){};
    ~SNMPManager()
    {
        for (ValueCallbacks *entry=callbacks; entry; entry=entry->next)
            if (entry->value) entry->value->release();
        delete callbacks;
    }
    SNMPManager(const SNMPManager&) = delete;
    SNMPManager& operator=(const SNMPManager&) = delete;
    SNMPManager(SNMPManager&& other) : SNMPManager()
    {
        std::swap(_community,other._community);
        std::swap(_udp,other._udp);
        std::swap(callbacks,other.callbacks);
        callbacksCursor=callbacks;
        other.callbacksCursor=other.callbacks;
    }
    const char *_community = "public";

    ValueCallbacks *callbacks = new ValueCallbacks();
    ValueCallbacks *callbacksCursor = callbacks;
    ValueCallback *findCallback(IPAddress ip, const char *oid); // Find based on responding host IP address and OID
    ValueCallback *addFloatHandler(IPAddress ip, const char *oid, float *value);
    // Capacity includes the C terminator. Legacy calls without capacity require caller-sized storage.
    ValueCallback *addStringHandler(IPAddress ip, const char *, char **, size_t capacity = static_cast<size_t>(-1));
    ValueCallback *addOctetHandler(IPAddress ip, const char *oid, unsigned char *value, size_t capacity, size_t *length);
    ValueCallback *addOpaqueHandler(IPAddress ip, const char *oid, unsigned char *value, size_t capacity, size_t *length);
    ValueCallback *addBinaryHandler(ASN_TYPE type, IPAddress ip, const char *oid, unsigned char *value, size_t capacity, size_t *length);
    ValueCallback *addIntegerHandler(IPAddress ip, const char *oid, int *value);
    ValueCallback *addTimestampHandler(IPAddress ip, const char *oid, uint32_t *value);
    ValueCallback *addOIDHandler(IPAddress ip, const char *oid, char *value, size_t capacity = static_cast<size_t>(-1));
    ValueCallback *addCounter64Handler(IPAddress ip, const char *oid, uint64_t *value);
    ValueCallback *addCounter32Handler(IPAddress ip, const char *oid, uint32_t *value);
    ValueCallback *addGaugeHandler(IPAddress ip, const char *oid, uint32_t *value);

    void setUDP(UDP *udp);
    bool begin();
    bool loop();
    bool testParsePacket(String testPacket);
    char OIDBuf[MAX_OID_LENGTH];
    UDP *_udp = nullptr;
    void addHandler(ValueCallback *callback);

private:
    unsigned char _packetBuffer[SNMP_PACKET_LENGTH * 3];
    bool inline receivePacket(int length);
    bool parsePacket(size_t length);
    void printPacket(int len);
};

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
        Serial.printf("%02x ", _packetBuffer[i]);
    }
    Serial.println();
}

bool SNMPManager::testParsePacket(String testPacket)
{
    // Parse a sample packet written as hexadecimal bytes separated by spaces.
    int len = testPacket.length() + 1;
    memset(_packetBuffer, 0, SNMP_PACKET_LENGTH * 3);
    char charArrayPacket[len];
    testPacket.toCharArray(charArrayPacket, len);
    // Split on spaces and convert each token from hexadecimal.
    char *p = strtok(charArrayPacket, " ");
    int i = 0;
    while (p != NULL)
    {
        if (i >= sizeof(_packetBuffer)) return false;
        _packetBuffer[i++] = strtoul(p, NULL, 16);
        p = strtok(NULL, " ");
    }

#ifdef DEBUG
    printPacket(len);
#endif

    return parsePacket(i);
}

bool inline SNMPManager::receivePacket(int packetLength)
{
    if (packetLength == 0) return false;
    if (packetLength < 0 || packetLength > SNMP_PACKET_LENGTH)
    {
        _udp->flush();
        return false;
    }
#ifdef DEBUG
    Serial.print(F("[DEBUG] Packet Length: "));
    Serial.print(packetLength);
    Serial.print(F(" From Address: "));
    Serial.println(_udp->remoteIP());
#endif

    memset(_packetBuffer, 0, SNMP_PACKET_LENGTH * 3);
    int len = packetLength;
    const int received = _udp->read(_packetBuffer, len);
    _udp->flush();
    if (received != len) return false;

#ifdef DEBUG
    printPacket(len);
#endif

    return parsePacket(len);
}

bool SNMPManager::parsePacket(size_t length)
{
    SNMPGetResponse *snmpgetresponse = new SNMPGetResponse();
    if (snmpgetresponse->parseFrom(_packetBuffer, length))
    {
        if (snmpgetresponse->requestType == GetResponsePDU)
        {
            // parseFrom exposes v1/v2c as 1/2, rather than their wire values 0/1.
            if ((snmpgetresponse->version != 1 && snmpgetresponse->version != 2) || (strlen(_community) != snmpgetresponse->communityLength ||
                memcmp(_community, snmpgetresponse->communityString, snmpgetresponse->communityLength) != 0))
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
                    if (callback->requestPending && callback->requestUDP == _udp &&
                        callback->requestPeer == _udp->remoteIP() &&
                        callback->expectedRequestID == snmpgetresponse->requestID)
                        callback->requestPending = false;
                }
                delete snmpgetresponse;
                return false;
            }
            int varBindIndex = 1;
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
                    Serial.print(F("Matching callback not found for received SNMP response. Response OID: "));
                    Serial.print(responseOID);
                    Serial.print(F(" - From IP Address: "));
                    Serial.println(responseIP);
                    delete snmpgetresponse;
                    return false;
                }
                if (callback->requestTracked)
                {
                    if (!callback->requestPending || callback->requestUDP != _udp ||
                        !(callback->requestPeer == responseIP) ||
                        callback->expectedRequestID != snmpgetresponse->requestID)
                    {
                        snmpgetresponse->varBindsCursor = snmpgetresponse->varBindsCursor->next;
                        ++varBindIndex;
                        continue;
                    }
                    callback->requestPending = false;
                }
                // An exception belongs to this binding, not the whole response.
                if (responseType == NOSUCHOBJECT || responseType == NOSUCHINSTANCE || responseType == ENDOFMIBVIEW)
                {
                    snmpgetresponse->varBindsCursor = snmpgetresponse->varBindsCursor->next;
                    ++varBindIndex;
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
                    const unsigned char *source = responseType == STRING
                        ? reinterpret_cast<unsigned char *>(static_cast<OctetType *>(responseContainer)->_value)
                        : static_cast<RawType *>(responseContainer)->_value;
                    size_t length = responseContainer->getLength();
                    if (destination->bytes)
                    {
                        if (length > destination->capacity) break;
                        memcpy(destination->bytes,source,length);
                        *destination->length=length;
                    }
                    else
                    {
                        if (!destination->value || !*destination->value || length >= destination->capacity ||
                            memchr(source,0,length)) break;
                        memcpy(*destination->value,source,length);
                        (*destination->value)[length]=0;
                    }
                }
                break;
                case OID:
                {
                    OIDCallback *destination=static_cast<OIDCallback *>(callback);
                    const char *source=static_cast<OIDType *>(responseContainer)->_value;
                    size_t length=strlen(source);
                    if (!destination->value || length >= destination->capacity) break;
                    memcpy(destination->value,source,length+1);
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
                        // Restore the registered float pointer and convert integer tenths to units.
                        *reinterpret_cast<float *>(callbackValue->value) = static_cast<float>(static_cast<int32_t>(raw)) / 10.0f;
                    }
                }
                break;
                case COUNTER32:
                {
#ifdef DEBUG
                    Serial.println("[DEBUG] Type: Counter32");
#endif
                    Counter32 *value = new Counter32();
                    *(((Counter32Callback *)callback)->value) = ((Counter32 *)responseContainer)->_value;
                    value->_value = *(((Counter32Callback *)callback)->value);
                    delete value;
                }
                break;
                case COUNTER64:
                {
#ifdef DEBUG
                    Serial.println("[DEBUG] Type: Counter64");
#endif
                    Counter64 *value = new Counter64();
                    *(((Counter64Callback *)callback)->value) = ((Counter64 *)responseContainer)->_value;
                    value->_value = *(((Counter64Callback *)callback)->value);
                    delete value;
                }
                break;
                case GAUGE32:
                {
#ifdef DEBUG
                    Serial.println("[DEBUG] Type: Gauge32");
#endif
                    Gauge *value = new Gauge();
                    *(((Gauge32Callback *)callback)->value) = ((Gauge *)responseContainer)->_value;
                    value->_value = *(((Gauge32Callback *)callback)->value);
                    delete value;
                }
                break;
                case TIMESTAMP:
                {
#ifdef DEBUG
                    Serial.println("[DEBUG] Type: TimeStamp");
#endif
                    TimestampType *value = new TimestampType();
                    *(((TimestampCallback *)callback)->value) = ((TimestampType *)responseContainer)->_value;
                    value->_value = *(((TimestampCallback *)callback)->value);
                    delete value;
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
                varBindIndex++;
            } // End while
        }     // End if GetResponsePDU
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

    if (callbacksCursor->value)
    {
        while (true)
        {
            if ((strcmp(callbacksCursor->value->OID, oid) == 0) && (callbacksCursor->value->ip == ip))
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

ValueCallback *SNMPManager::addStringHandler(IPAddress ip, const char *oid, char **value, size_t capacity)
{
    ValueCallback *callback = new StringCallback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((StringCallback *)callback)->value = value;
    ((StringCallback *)callback)->capacity = capacity;
    callback->ip = ip;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addIntegerHandler(IPAddress ip, const char *oid, int *value)
{
    ValueCallback *callback = new IntegerCallback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((IntegerCallback *)callback)->value = value;
    ((IntegerCallback *)callback)->isFloat = false;
    callback->ip = ip;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addFloatHandler(IPAddress ip, const char *oid, float *value)
{
    ValueCallback *callback = new IntegerCallback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((IntegerCallback *)callback)->value = (int *)value;
    ((IntegerCallback *)callback)->isFloat = true;
    callback->ip = ip;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addTimestampHandler(IPAddress ip, const char *oid, uint32_t *value)
{
    ValueCallback *callback = new TimestampCallback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((TimestampCallback *)callback)->value = value;
    callback->ip = ip;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addOIDHandler(IPAddress ip, const char *oid, char *value, size_t capacity)
{
    ValueCallback *callback = new OIDCallback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((OIDCallback *)callback)->capacity = capacity;
    ((OIDCallback *)callback)->value = value;
    callback->ip = ip;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addCounter64Handler(IPAddress ip, const char *oid, uint64_t *value)
{
    ValueCallback *callback = new Counter64Callback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((Counter64Callback *)callback)->value = value;
    callback->ip = ip;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addCounter32Handler(IPAddress ip, const char *oid, uint32_t *value)
{
    ValueCallback *callback = new Counter32Callback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((Counter32Callback *)callback)->value = value;
    callback->ip = ip;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addGaugeHandler(IPAddress ip, const char *oid, uint32_t *value)
{
    ValueCallback *callback = new Gauge32Callback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((Gauge32Callback *)callback)->value = value;
    callback->ip = ip;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addBinaryHandler(ASN_TYPE type, IPAddress ip, const char *oid,
                                             unsigned char *value, size_t capacity, size_t *length)
{
    if ((type != STRING && type != OPAQUE) || !value || !length || !oid) return nullptr;
    StringCallback *callback=new StringCallback(type);
    callback->OID=static_cast<char *>(malloc(strlen(oid)+1));
    strcpy(callback->OID,oid);
    callback->bytes=value;
    callback->capacity=capacity;
    callback->length=length;
    callback->ip=ip;
    addHandler(callback);
    return callback;
}
ValueCallback *SNMPManager::addOctetHandler(IPAddress ip, const char *oid, unsigned char *value, size_t capacity, size_t *length)
{ return addBinaryHandler(STRING,ip,oid,value,capacity,length); }
ValueCallback *SNMPManager::addOpaqueHandler(IPAddress ip, const char *oid, unsigned char *value, size_t capacity, size_t *length)
{ return addBinaryHandler(OPAQUE,ip,oid,value,capacity,length); }

void SNMPManager::addHandler(ValueCallback *callback)
{
    callbacksCursor = callbacks;
    if (callbacksCursor->value)
    {
        while (callbacksCursor->next != 0)
        {
            callbacksCursor = callbacksCursor->next;
        }
        callbacksCursor->next = new ValueCallbacks();
        callbacksCursor = callbacksCursor->next;
        callbacksCursor->value = callback;
        callbacksCursor->next = 0;
    }
    else
        callbacks->value = callback;
}

#endif
