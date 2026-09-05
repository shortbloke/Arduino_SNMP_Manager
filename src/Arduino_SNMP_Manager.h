#ifndef SNMPManager_h
#define SNMPManager_h

#include "SNMPConfig.h"

#include <Udp.h>

#include "BER.h"
#include "VarBinds.h"

#include "ValueCallbacks.h"

#include "SNMPGet.h"
#include "SNMPGetResponse.h"

class SNMPManager
{
public:
    SNMPManager() {};
    SNMPManager(const char *community) : _community(community ? community : "public") {};
    ~SNMPManager();
    SNMPManager(const SNMPManager &) = delete;
    SNMPManager &operator=(const SNMPManager &) = delete;
    SNMPManager(SNMPManager &&other);
    const char *_community = "public";

    ValueCallbacks *callbacks = new (std::nothrow) ValueCallbacks();
    ValueCallbacks *callbacksCursor = callbacks;
    ValueCallback *
    findCallback(IPAddress ip, const char *oid); // Find based on responding host IP address and OID
    ValueCallback *addFloatHandler(IPAddress ip, const char *oid, float *value);
    // Capacity includes the C terminator.
    ValueCallback *addStringHandler(IPAddress ip, const char *, char **, size_t capacity);
    ValueCallback *addOctetHandler(IPAddress ip, const char *oid, unsigned char *value,
                                   size_t capacity, size_t *length);
    ValueCallback *addOpaqueHandler(IPAddress ip, const char *oid, unsigned char *value,
                                    size_t capacity, size_t *length);
    ValueCallback *addBinaryHandler(ASN_TYPE type, IPAddress ip, const char *oid,
                                    unsigned char *value, size_t capacity, size_t *length);
    ValueCallback *addIntegerHandler(IPAddress ip, const char *oid, int32_t *value);
    ValueCallback *addTimestampHandler(IPAddress ip, const char *oid, uint32_t *value);
    ValueCallback *addOIDHandler(IPAddress ip, const char *oid, char *value, size_t capacity);
    ValueCallback *addCounter64Handler(IPAddress ip, const char *oid, uint64_t *value);
    ValueCallback *addCounter32Handler(IPAddress ip, const char *oid, uint32_t *value);
    ValueCallback *addGaugeHandler(IPAddress ip, const char *oid, uint32_t *value);

    void setUDP(UDP *udp);
    bool begin();
    bool loop();
    bool testParsePacket(String testPacket);
    char OIDBuf[MAX_OID_LENGTH];
    UDP *_udp = nullptr;
    bool addHandler(ValueCallback *callback);

private:
    unsigned char _packetBuffer[SNMP_PACKET_LENGTH];
    bool receivePacket(int length);
    bool parsePacket(size_t length);
    void printPacket(int len);
};

#endif
