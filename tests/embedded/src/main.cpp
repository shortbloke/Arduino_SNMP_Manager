#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <Arduino_SNMP_Manager.h>
#include <SNMPTable.h>
#include <SNMPMIB.h>

WiFiUDP queryUDP;
SNMPClient client(queryUDP);
SNMPDevice device(client, "192.0.2.1", "public");
SNMPRead<SystemUptime> uptime(device);
SNMPTableRead<2, 2> table(device);
SNMPSet<1> writeRequest(device);

static_assert(sizeof(int) >= 4, "Supported targets require 32-bit int");
static_assert(sizeof(float) == 4, "Float callbacks require 32-bit float");

WiFiUDP udp;
SNMPManager manager("public");
SNMPGet request("public", SNMPVersion::Version2c);
int32_t integerValue;
float floatValue;
double sensorReading;
uint32_t counterValue;
uint64_t counter64Value;
char text[64];
char *textPointer = text;
unsigned char binary[64];
size_t binaryLength;

bool checkSecondTranslationUnit();
void setup()
{
    if (!checkSecondTranslationUnit())
        return;
    IPAddress peer(192, 0, 2, 1);
    const char *oid = ".1.3.6.1.2.1.1.1.0";
    manager.setUDP(&udp);
    request.setUDP(&udp);
    request.addOIDPointer(manager.addIntegerHandler(peer, oid, &integerValue));
    manager.addFloatHandler(peer, oid, &floatValue);
    manager.addCounter32Handler(peer, oid, &counterValue);
    manager.addCounter64Handler(peer, oid, &counter64Value);
    manager.addStringHandler(peer, oid, &textPointer, sizeof(text));
    manager.addOIDHandler(peer, oid, text, sizeof(text));
    manager.addOctetHandler(peer, oid, binary, sizeof(binary), &binaryLength);
    manager.addOpaqueHandler(peer, oid, binary, sizeof(binary), &binaryLength);
    request.setRequestID(INT32_MAX);
    request.setPort(65535);
    request.build();
    // Compile the real UDP send/receive path; no Wi-Fi credentials are configured.
    request.sendTo(peer);
    SNMPMIB::fixedPoint(SNMPValue::integer32(125), 0, 1, sensorReading);
    SNMPMIB::storageBytes(SNMPValue::integer32(4096), SNMPValue::integer32(1024), counter64Value);
    client.begin();
    uptime.start();
    table.addColumn(".1.3.6.1.2.1.2.2.1.10");
    table.addColumn(".1.3.6.1.2.1.2.2.1.16");
    table.start();
    writeRequest.addValue(".1.3.6.1.2.1.1.7.0", SNMPValue::integer32(0));
    // Exercise encoding without sending a write to a real device.
    writeRequest.start();
    writeRequest.cancel();
    client.notifications("public", [](const SNMPNotification &, void *) { return true; });
}
void loop()
{
    manager.loop();
    client.loop();
}
