#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <SNMPClient.h>
#include <SNMPTable.h>

const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";
WiFiUDP udp;
SNMPClient client(udp);
SNMPDevice networkSwitch(client, "192.168.1.10", "public");
// Fixed capacity: reduce this if the rest of the sketch needs more RAM.
SNMPInterfaceRead<48> interfaces(networkSwitch);

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
