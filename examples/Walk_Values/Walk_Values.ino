#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <SNMPClient.h>
#include <cstdio>

const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";
WiFiUDP udp;
SNMPClient client(udp);
// Version1 uses GETNEXT; Version2c uses GETBULK, with GETNEXT fallback.
// Run once with each version supported by your agent.
SNMPDevice device(client, "192.168.1.1", "public", SNMPVersion::Version2c);
SNMPWalk<1> walk(device); // Streaming avoids retaining a whole subtree.
bool ready = false;

bool printValue(const SNMPResult &result, void *)
{
    Serial.print(result.oid);
    Serial.print(" = ");
    if (!result.ok())
    {
        Serial.println(result.status.message());
        return true; // Keep walking; this particular value could not be retained.
    }
    const SNMPValue &value = result.value;
    char number[32];
    switch (value.type)
    {
    case INTEGER:
        Serial.print("Integer32: ");
        Serial.println(static_cast<long>(value.integer()));
        break;
    case COUNTER32:
    case GAUGE32: // Unsigned32 uses the same wire tag.
    case TIMESTAMP:
        Serial.print(value.type == COUNTER32 ? "Counter32: "
                     : value.type == GAUGE32 ? "Gauge32/Unsigned32: "
                                             : "TimeTicks (1/100 s): ");
        Serial.println(static_cast<unsigned long>(value.unsigned32()));
        break;
    case COUNTER64: // Available with SNMPv2c, not SNMPv1.
        snprintf(number, sizeof(number), "%llu",
                 static_cast<unsigned long long>(value.unsigned64()));
        Serial.print("Counter64: ");
        Serial.println(number);
        break;
    case OID:
        Serial.print("OID: ");
        Serial.println(value.text());
        break;
    case NETWORK_ADDRESS:
        Serial.print("IpAddress: ");
        if (value.length == 4)
            Serial.println(IPAddress(value.bytes));
        else
            Serial.println("unexpected length");
        break;
    case STRING:
    case OPAQUE:
        // Hex preserves binary strings, BITS, and vendor Opaque payloads.
        // isText() checks for embedded NULs, not printable characters or encoding.
        Serial.print(value.type == STRING ? "OCTET STRING (hex): " : "Opaque (hex): ");
        for (size_t i = 0; i < value.length; ++i)
        {
            if (value.bytes[i] < 16)
                Serial.print('0');
            Serial.print(value.bytes[i], HEX);
            Serial.print(' ');
        }
        Serial.println();
        break;
    case NULLTYPE:
        Serial.println("NULL");
        break;
    default:
        Serial.println("unsupported type");
    }
    return true; // Result is borrowed; copy it if it must outlive this call.
}
void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
        delay(100);
    auto status = client.begin();
    // Start with the system group; change the root to inspect another MIB subtree.
    if (status.ok())
        status = walk.configure(".1.3.6.1.2.1.1");
    if (status.ok())
        status = walk.stream(printValue, nullptr);
    if (status.ok())
        status = walk.start();
    ready = status.ok();
    if (!ready)
        Serial.println(status.message());
}
void loop()
{
    if (!ready)
        return;
    client.loop();
    if (walk.takeCompleted())
        Serial.println(walk.status().message());
}
