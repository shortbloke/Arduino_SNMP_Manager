#ifndef SNMPManager_h
#define SNMPManager_h

#ifndef UDP_TX_PACKET_MAX_SIZE
#define UDP_TX_PACKET_MAX_SIZE 484
#endif

#ifndef SNMP_PACKET_LENGTH
#define SNMP_PACKET_LENGTH 484
#endif

#define MIN(X, Y) ((X < Y) ? X : Y)

#include <UDP.h>

#include "BER.h"
#include "VarBinds.h"

class ValueCallback
{
public:
    ValueCallback(ASN_TYPE atype) : type(atype){};
    char *OID;
    ASN_TYPE type;
    bool overwritePrefix = false;
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
    int *value;
};

class StringCallback : public ValueCallback
{
public:
    StringCallback() : ValueCallback(STRING){};
    char **value;
};

class OIDCallback : public ValueCallback
{
public:
    OIDCallback() : ValueCallback(ASN_TYPE::OID){};
    char *value;
};

class Counter32Callback : public ValueCallback
{
public:
    Counter32Callback() : ValueCallback(ASN_TYPE::COUNTER32){};
    uint32_t *value;
};

class Guage32Callback : public ValueCallback
{
public:
    Guage32Callback() : ValueCallback(ASN_TYPE::GUAGE32){};
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
    ValueCallback *value;
    struct ValueCallbackList *next = 0;
} ValueCallbacks;

#include "SNMPGet.h"
#include "SNMPGetResponse.h"

class SNMPManager
{
public:
    SNMPManager(){};
    SNMPManager(const char *community) : _community(community){};
    const char *_community;

    ValueCallbacks *callbacks = new ValueCallbacks();
    ValueCallbacks *callbacksCursor = callbacks;
    ValueCallback *findCallback(char *oid);
    ValueCallback *addFloatHandler(char *oid, float *value); // this obv just adds integer but with the *0.1 set
    ValueCallback *addStringHandler(char *, char **);        // passing in a pointer to a char*
    ValueCallback *addIntegerHandler(char *oid, int *value);
    ValueCallback *addTimestampHandler(char *oid, int *value);
    ValueCallback *addOIDHandler(char *oid, char *value);           // Not implemented
    ValueCallback *addCounter64Handler(char *oid, uint64_t *value); // Not implemented
    ValueCallback *addCounter32Handler(char *oid, uint32_t *value);
    ValueCallback *addGuageHandler(char *oid, uint32_t *value);

    bool setUDP(UDP *udp);
    bool begin();
    bool loop();
    char OIDBuf[50];
    UDP *_udp;
    void addHandler(ValueCallback *callback);

private:
    unsigned char _packetBuffer[SNMP_PACKET_LENGTH * 3];
    bool inline receivePacket(int length);
};

bool SNMPManager::setUDP(UDP *udp)
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
    _udp->begin(162);
}

bool SNMPManager::loop()
{
    if (!_udp)
    {
        return false;
    }
    receivePacket(_udp->parsePacket());
}

bool inline SNMPManager::receivePacket(int packetLength)
{
    if (!packetLength)
        return false;
    // Serial.print("Packet Length: ");Serial.print(packetLength);Serial.print(" - From: ");Serial.println(_udp->remoteIP());
    if (packetLength < 0 || packetLength > SNMP_PACKET_LENGTH)
    {
        Serial.println(F("Incorrect Packet Length - Dropping packet"));
        return false;
    }
    memset(_packetBuffer, 0, SNMP_PACKET_LENGTH * 3);
    int len = packetLength;
    _udp->read(_packetBuffer, MIN(len, SNMP_PACKET_LENGTH));
    // for(int i = 0; i < len; i++){
    //     _packetBuffer[i] = _udp->read();
    //     Serial.print(_packetBuffer[i], HEX);
    //     Serial.print(" ");
    // }
    _udp->flush();
    _packetBuffer[len] = 0;

    SNMPGetRespose *snmpgetresponse = new SNMPGetRespose();
    if (snmpgetresponse->parseFrom(_packetBuffer))
    {
        // Serial.printf("Current heap size: %u\n", ESP.getFreeHeap());
        if (snmpgetresponse->requestType == GetResponsePDU)
        {
            if (!(snmpgetresponse->version != 1 || snmpgetresponse->version != 2) || strcmp(_community, snmpgetresponse->communityString) != 0)
            {
                Serial.println(F("Invalid community or version"));
                Serial.print("Community: ");
                Serial.print(snmpgetresponse->communityString);
                Serial.print(" Version: ");
                Serial.print(snmpgetresponse->version);
                delete snmpgetresponse;
                return false;
            }
            int varBindIndex = 1;
            snmpgetresponse->varBindsCursor = snmpgetresponse->varBinds;
            while (true)
            {
                //Serial.print("OID: ");Serial.println(snmpgetresponse->varBindsCursor->value->oid->_value);
                ValueCallback *callback = findCallback(snmpgetresponse->varBindsCursor->value->oid->_value);
                if (callback->type != snmpgetresponse->varBindsCursor->value->type)
                {
                    switch (snmpgetresponse->varBindsCursor->value->type)
                    {
                    case NOSUCHOBJECT:
                    {
                        Serial.print("No such object: ");
                        Serial.println(snmpgetresponse->varBindsCursor->value->oid->_value);
                    }
                    break;
                    case NOSUCHINSTANCE:
                    {
                        Serial.print("No such instance: ");
                        Serial.println(snmpgetresponse->varBindsCursor->value->oid->_value);
                    }
                    break;
                    case ENDOFMIBVIEW:
                    {
                        Serial.print("End of MIB view when calling: ");
                        Serial.println(snmpgetresponse->varBindsCursor->value->oid->_value);
                    }
                    break;
                    default:
                    {
                        Serial.print("Callback expected type: ");
                        Serial.print(callback->type);
                        Serial.print(" Is not of received type: ");
                        Serial.println(snmpgetresponse->varBindsCursor->value->type);
                    }
                    }
                    delete snmpgetresponse;
                    return false;
                }
                switch (callback->type)
                {
                case STRING:
                {
                    memcpy(*((StringCallback *)callback)->value, String(((OctetType *)snmpgetresponse->varBindsCursor->value->value)->_value).c_str(), 25); // FIXME: this is VERY dangerous, i'm assuming the length of the source char*, this needs to change. for some reason strncpy didnd't work, need to look into this. the '25' also needs to be defined somewhere so this won't break;
                    *(*((StringCallback *)callback)->value + 24) = 0x0;                                                                                     // close off the dest string, temporary
                    OctetType *value = new OctetType(*((StringCallback *)callback)->value);
                    delete value;
                }
                break;
                case INTEGER:
                {
                    IntegerType *value = new IntegerType();
                    if (!((IntegerCallback *)callback)->isFloat)
                    {
                        *(((IntegerCallback *)callback)->value) = ((IntegerType *)snmpgetresponse->varBindsCursor->value->value)->_value;
                        value->_value = *(((IntegerCallback *)callback)->value);
                    }
                    else
                    {
                        *(((IntegerCallback *)callback)->value) = (float)(((IntegerType *)snmpgetresponse->varBindsCursor->value->value)->_value / 10);
                        value->_value = *(float *)(((IntegerCallback *)callback)->value) * 10;
                    }
                    delete value;
                }
                break;
                case COUNTER32:
                {
                    Counter32 *value = new Counter32();
                    *(((Counter32Callback *)callback)->value) = ((Counter32 *)snmpgetresponse->varBindsCursor->value->value)->_value;
                    value->_value = *(((Counter32Callback *)callback)->value);
                    delete value;
                }
                break;
                case COUNTER64:
                {
                    Counter64 *value = new Counter64();
                    *(((Counter64Callback *)callback)->value) = ((Counter64 *)snmpgetresponse->varBindsCursor->value->value)->_value;
                    value->_value = *(((Counter64Callback *)callback)->value);
                    delete value;
                }
                break;
                case GUAGE32:
                {
                    Guage *value = new Guage();
                    *(((Guage32Callback *)callback)->value) = ((Guage *)snmpgetresponse->varBindsCursor->value->value)->_value;
                    value->_value = *(((Guage32Callback *)callback)->value);
                    delete value;
                }
                break;
                case TIMESTAMP:
                {
                    TimestampType *value = new TimestampType();
                    *(((TimestampCallback *)callback)->value) = ((TimestampType *)snmpgetresponse->varBindsCursor->value->value)->_value;
                    value->_value = *(((TimestampCallback *)callback)->value);
                    delete value;
                }
                break;
                default:
                {
                    Serial.print("Unsupported Type: ");
                    Serial.print(callback->type);
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
        Serial.println("SNMPGETRESPONSE: FAILED TO PARSE");
        delete snmpgetresponse;
        return false;
    }
    delete snmpgetresponse;
    return true;
}

ValueCallback *SNMPManager::findCallback(char *oid)
{
    callbacksCursor = callbacks;

    if (callbacksCursor->value)
    {
        while (true)
        {
            memset(OIDBuf, 0, 50);
            strcat(OIDBuf, callbacksCursor->value->OID);
            if (strcmp(OIDBuf, oid) == 0)
            {
                //  found
                return callbacksCursor->value;
            }
            if (callbacksCursor->next)
            {
                callbacksCursor = callbacksCursor->next;
            }
            else
            {
                break;
            }
        }
    }
    return 0;
}

ValueCallback *SNMPManager::addStringHandler(char *oid, char **value)
{
    ValueCallback *callback = new StringCallback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((StringCallback *)callback)->value = value;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addIntegerHandler(char *oid, int *value)
{
    ValueCallback *callback = new IntegerCallback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((IntegerCallback *)callback)->value = value;
    ((IntegerCallback *)callback)->isFloat = false;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addFloatHandler(char *oid, float *value)
{
    ValueCallback *callback = new IntegerCallback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((IntegerCallback *)callback)->value = (int *)value;
    ((IntegerCallback *)callback)->isFloat = true;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addTimestampHandler(char *oid, int *value)
{
    ValueCallback *callback = new TimestampCallback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((TimestampCallback *)callback)->value = value;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addOIDHandler(char *oid, char *value)
{
    ValueCallback *callback = new OIDCallback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((OIDCallback *)callback)->value = value;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addCounter64Handler(char *oid, uint64_t *value)
{
    ValueCallback *callback = new Counter64Callback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((Counter64Callback *)callback)->value = value;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addCounter32Handler(char *oid, uint32_t *value)
{
    ValueCallback *callback = new Counter32Callback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((Counter32Callback *)callback)->value = value;
    addHandler(callback);
    return callback;
}

ValueCallback *SNMPManager::addGuageHandler(char *oid, uint32_t *value)
{
    ValueCallback *callback = new Guage32Callback();
    callback->OID = (char *)malloc((sizeof(char) * strlen(oid)) + 1);
    strcpy(callback->OID, oid);
    ((Guage32Callback *)callback)->value = value;
    addHandler(callback);
    return callback;
}

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
