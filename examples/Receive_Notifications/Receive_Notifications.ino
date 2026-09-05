#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <SNMPClient.h>

const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";
WiFiUDP udp;
SNMPClient client(udp);
// Configure your agent's trap/INFORM destination to this board's IP, UDP port 162.
bool ready = false;
bool received(const SNMPNotification &notification, void *)
{
    Serial.print(notification.inform ? "INFORM from " : "TRAP from ");
    Serial.println(notification.peer);
    if (notification.version == SNMPVersion::Version1)
    {
        Serial.print("Enterprise: ");
        Serial.println(notification.enterprise);
        Serial.print("Generic/specific: ");
        Serial.print(static_cast<long>(notification.genericTrap));
        Serial.print('/');
        Serial.println(static_cast<long>(notification.specificTrap));
    }
    // Inspect all bindings before accepting. A failed read leaves an INFORM
    // unacknowledged, allowing its sender to retry. Duplicate delivery is possible.
    for (size_t i = 0; i < notification.size(); ++i)
    {
        SNMPResult result;
        auto status = notification.read(i, result);
        if (!status.ok())
        {
            Serial.println(status.message());
            return false;
        }
        Serial.print(result.oid);
        Serial.print(" type=0x");
        Serial.println(static_cast<unsigned>(result.value.type), HEX);
        // v2c starts with sysUpTime.0 and snmpTrapOID.0, identifying the event.
        if (result.value.type == OID)
            Serial.println(result.value.text());
        else if (result.value.type == TIMESTAMP)
            Serial.println(static_cast<unsigned long>(result.value.unsigned32()));
    }
    return true; // Client sends RESPONSE for accepted INFORMs; traps get no reply.
}
void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
        delay(100);
    auto status = client.notifications("public", received);
    if (status.ok())
        status = client.begin(162);
    ready = status.ok();
    if (ready)
        Serial.println(WiFi.localIP());
    else
        Serial.println(status.message());
}
void loop()
{
    if (ready)
        client.loop(); // Handler runs here; keep it short in production.
}
