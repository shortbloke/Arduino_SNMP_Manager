#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <SNMPClient.h>
#include <SNMPTable.h>
#include <SNMPMIB.h>

const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";
WiFiUDP udp;
SNMPClient client(udp);
// Printer-MIB rows have a compound device/supply index; keep the full suffix.
SNMPDevice printer(client, "192.168.1.30", "public");
SNMPTableRead<16, 3, 24> supplies(printer);
bool ready = false;
void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
        delay(100);
    auto status = client.begin();
    if (status.ok())
        status = supplies.addColumn(".1.3.6.1.2.1.43.11.1.1.6", STRING); // description
    if (status.ok())
        status = supplies.addColumn(".1.3.6.1.2.1.43.11.1.1.8", INTEGER); // capacity
    if (status.ok())
        status = supplies.addColumn(".1.3.6.1.2.1.43.11.1.1.9", INTEGER); // level
    if (status.ok())
        status = supplies.start();
    ready = status.ok();
    if (!ready)
        Serial.println(status.message());
}
void loop()
{
    if (!ready)
        return;
    client.loop();
    if (!supplies.takeCompleted())
        return;
    Serial.println(supplies.status().message());
    for (const auto &row : supplies)
    {
        Serial.print(row.index);
        Serial.print(" ");
        if (row[0].ok() && row[0].value.isText())
            Serial.print(row[0].value.text());
        Serial.print(": ");
        if (!row[2].ok())
        {
            Serial.println(row[2].status.message());
            continue;
        }
        double percent;
        if (row[1].ok() && SNMPMIB::supplyPercent(row[2].value, row[1].value, percent))
        {
            // Same-row capacity and level use the same units. For waste receptacles,
            // level describes remaining space, not the amount of waste accumulated.
            Serial.print(percent, 1);
            Serial.println("% remaining");
            continue;
        }
        switch (SNMPMIB::supplyState(row[2].value))
        {
        case SNMPMIB::SupplyState::Other:
            Serial.println("other");
            break;
        case SNMPMIB::SupplyState::Unknown:
            Serial.println("unknown");
            break;
        case SNMPMIB::SupplyState::SomeRemaining:
            Serial.println("some remaining");
            break;
        case SNMPMIB::SupplyState::Known:
            Serial.println("known level; percentage unavailable");
            break;
        default:
            Serial.println("invalid level");
            break;
        }
    }
}
