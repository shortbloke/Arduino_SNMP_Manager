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
SNMPDevice networkSwitch(client, "192.168.1.10", "public");
// Capacity counts logical interfaces, not physical ports. A 24-port switch can
// expose more than 48 rows. Reduce this if the rest of the sketch needs more working memory.
constexpr size_t MaxInterfaces = 64;       // Maximum rows retained; edit, rebuild, upload.
constexpr size_t InterfaceIndexBytes = 16; // Index text bytes, including its terminating zero.
SNMPInterfaceRead<MaxInterfaces, InterfaceIndexBytes> interfaces(networkSwitch);

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

void printCounter(const SNMPCell &cell)
{
    if (!cell.ok())
    {
        Serial.print(cell.status.message());
        return;
    }
    char text[24];
    snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(cell.value.unsigned64()));
    Serial.print(text);
}
void loop()
{
    if (!ready)
        return;
    client.loop();
    if (interfaces.takeCompleted())
    {
        if (!interfaces.status().ok())
            Serial.println(interfaces.status().message());
        if (interfaces.status().code() == SNMPStatus::CapacityExceeded)
        {
            Serial.print("Incomplete table: retained rows ");
            Serial.print(interfaces.size());
            Serial.print(" / ");
            Serial.println(MaxInterfaces);
            Serial.println(
                "If full, increase MaxInterfaces only if working memory allows; rebuild and upload.");
            Serial.println(
                "Otherwise check index/value/packet limits. See docs/TROUBLESHOOTING.md.");
            Serial.println("Use Walk_Values to stream without retaining a complete table.");
        }
        for (const auto &row : interfaces)
        {
            Serial.print(row.index);
            Serial.print(" ");
            if (row[0].ok() && row[0].value.isText())
                Serial.print(row[0].value.text());
            Serial.print(" received bytes: ");
            printCounter(row[1]);
            Serial.print(" sent bytes: ");
            printCounter(row[2]);
            Serial.println();
        }
    }
    const uint32_t now = millis();
    if (!interfaces.pending() && uint32_t(now - lastPoll) >= 5000)
    {
        lastPoll = now;
        const auto status = interfaces.start();
        if (!status.ok())
            Serial.println(status.message());
    }
}
