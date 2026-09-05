// This deliberately uses the public 1.1.13 signatures and sketch-local macros.
#define SNMP_PACKET_LENGTH 768
#define DEBUG
#include <Arduino_SNMP_Manager.h>
#include <type_traits>
int compatibilityValue();
class LegacyPrimitive : public BER_CONTAINER
{
public:
    LegacyPrimitive() : BER_CONTAINER(true, NULLTYPE) {}
    int serialise(unsigned char *out) override
    {
        if (out)
        {
            out[0] = 5;
            out[1] = 0;
        }
        return 2;
    }
    bool fromBuffer(unsigned char *in) override
    {
        return in && in[0] == 5 && in[1] == 0;
    }
    int getLength() override
    {
        return 0;
    }
};
static_assert(std::is_same<decltype(SNMPGet::requestID), short>::value, "Legacy request ID type");
static_assert(std::is_same<decltype(SNMPGet::port), short>::value, "Legacy port type");
static_assert(std::is_copy_constructible<SNMPGet>::value, "Requests remain copyable");
int main()
{
    void (SNMPGet::*setID)(short) = &SNMPGet::setRequestID;
    void (SNMPGet::*setPort)(short) = &SNMPGet::setPort;
    void (SNMPGet::*addOID)(ValueCallback *) = &SNMPGet::addOIDPointer;
    void (SNMPManager::*add)(ValueCallback *) = &SNMPManager::addHandler;
    void (ComplexType::*addValue)(BER_CONTAINER *) = &ComplexType::addValueToList;
    ValueCallback *(SNMPManager::*stringHandler)(IPAddress, const char *, char **) =
        &SNMPManager::addStringHandler;
    ValueCallback *(SNMPManager::*oidHandler)(IPAddress, const char *, char *) =
        &SNMPManager::addOIDHandler;
    bool (SNMPGetResponse::*parse)(unsigned char *) = &SNMPGetResponse::parseFrom;
    (void)parse;
    (void)add;
    (void)addValue;
    (void)stringHandler;
    (void)oidHandler;
    SNMPManager manager;
    UDP udp;
    manager.setUDP(&udp);
    SNMPGet request("public", 1);
    request.setUDP(&udp);
    IPAddress peer(192, 0, 2, 1);
    request.setIP(peer);
    int number = 0;
    float fraction = 0;
    uint32_t counter = 0;
    uint64_t wide = 0;
    char text[64] = {};
    char *textPointer = text;
    const char *oid = ".1.3.6.1.2.1.1.1.0";
    (request.*addOID)(manager.addIntegerHandler(peer, oid, &number));
    manager.addFloatHandler(peer, oid, &fraction);
    manager.addStringHandler(peer, oid, &textPointer);
    manager.addOIDHandler(peer, oid, text);
    manager.addTimestampHandler(peer, oid, &counter);
    manager.addGaugeHandler(peer, oid, &counter);
    manager.addCounter32Handler(peer, oid, &counter);
    manager.addCounter64Handler(peer, oid, &wide);
    (request.*setID)(32767);
    (request.*setPort)(161);
    LegacyPrimitive primitive;
    unsigned char bytes[2];
    BER_CONTAINER &base = primitive;
    return !request.build() || base.serialise(bytes) != 2 || !base.fromBuffer(bytes) ||
           compatibilityValue() != 128;
}
