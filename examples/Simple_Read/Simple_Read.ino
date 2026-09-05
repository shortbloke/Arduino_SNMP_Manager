// New to SNMP (Simple Network Management Protocol)? Read docs/GETTING_STARTED.md.
// Wi-Fi name/password connect the board; the community string grants device access.
// For terms such as OID (a reading's numeric address), see docs/TERMS.md.
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <SNMPClient.h>
#include <SNMPTable.h>

const char *ssid = "YOUR_SSID";         // Your Wi-Fi network name (SSID).
const char *password = "YOUR_PASSWORD"; // Your Wi-Fi password, not the device community.
WiFiUDP udp;
SNMPClient client(udp);
SNMPDevice router(client, "192.168.1.1", "public");
SNMPRead<SystemUptime> uptime(router);

bool ready = false;
uint32_t lastPoll = 0;
void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
        delay(100);
    const auto status = client.begin();
    ready = status.ok();
    if (!ready)
        Serial.println(status.message());
}

void loop()
{
    if (!ready)
        return;
    client.loop();
    if (uptime.takeCompleted())
    {
        if (uptime.result().ok())
            Serial.println(static_cast<unsigned long>(uptime.result().value.unsigned32() / 100));
        else
            Serial.println(uptime.status().message());
    }
    const uint32_t now = millis();
    if (!uptime.pending() && uint32_t(now - lastPoll) >= 5000)
    {
        lastPoll = now;
        const auto status = uptime.start();
        if (!status.ok())
            Serial.println(status.message());
    }
}
