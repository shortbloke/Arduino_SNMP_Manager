#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <SNMPClient.h>
#include <cstdio>
#include <SNMPTable.h>
#include <SNMPMIB.h>

const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";
WiFiUDP udp;
SNMPClient client(udp);
// A NAS/server with HOST-RESOURCES-MIB enabled. Indices are discovered, not assumed.
SNMPDevice host(client, "192.168.1.20", "public");
// Counts all exposed storage entries, including memory, mounts, and datasets.
// Large NAS agents can exceed this capacity. Increase only within your RAM budget;
// stream the storage subtree (see Walk_Values) if you cannot retain the full table.
SNMPTableRead<16, 4, 16> storage(host);
bool ready = false;
void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
        delay(100);
    auto status = client.begin();
    if (status.ok())
        status = storage.addColumn(".1.3.6.1.2.1.25.2.3.1.3", STRING); // description
    if (status.ok())
        status = storage.addColumn(".1.3.6.1.2.1.25.2.3.1.4", INTEGER); // bytes per unit
    if (status.ok())
        status = storage.addColumn(".1.3.6.1.2.1.25.2.3.1.5", INTEGER); // total units
    if (status.ok())
        status = storage.addColumn(".1.3.6.1.2.1.25.2.3.1.6", INTEGER); // used units
    if (status.ok())
        status = storage.start();
    ready = status.ok();
    if (!ready)
        Serial.println(status.message());
}
void loop()
{
    if (!ready)
        return;
    client.loop();
    if (!storage.takeCompleted())
        return;
    Serial.println(storage.status().message()); // Partial rows remain inspectable.
    for (const auto &row : storage)
    {
        Serial.print(row.index);
        Serial.print(" ");
        if (row[0].ok() && row[0].value.isText())
            Serial.print(row[0].value.text());
        uint64_t total, used;
        if (row[1].ok() && row[2].ok() && row[3].ok() &&
            SNMPMIB::storageBytes(row[1].value, row[2].value, total) &&
            SNMPMIB::storageBytes(row[1].value, row[3].value, used))
        {
            char text[80];
            snprintf(text, sizeof(text), " used %llu / %llu bytes",
                     static_cast<unsigned long long>(used), static_cast<unsigned long long>(total));
            Serial.println(text);
        }
        else
            Serial.println(" size unavailable");
    }
}
