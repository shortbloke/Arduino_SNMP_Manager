// Controlled notification load test; selected only by the d1_mini_burst environment.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <SNMPClient.h>
#if __has_include("hardware_config.h")
#include "hardware_config.h"
#else
#define TEST_SSID ""
#define TEST_PASSWORD ""
#endif

WiFiUDP burstUdp;
SNMPClient burstClient(burstUdp);
uint8_t seen[128] = {};
unsigned received = 0, unique = 0, invalid = 0, pauseMs = 0;
uint32_t minHeap = UINT32_MAX, minBlock = UINT32_MAX;
bool ready = false;

void sample()
{
    minHeap = min(minHeap, ESP.getFreeHeap());
    minBlock = min(minBlock, ESP.getMaxFreeBlockSize());
}
bool notification(const SNMPNotification &event, void *)
{
    sample(); // Includes the live BER tree.
    const int id = event.requestID;
    bool valid = event.size() == 3 && id > 0 && id <= 1024;
    for (size_t i = 0; i < event.size(); ++i)
    {
        SNMPResult result;
        if (!event.read(i, result).ok())
            valid = false;
        if (i == 2)
        {
            if (result.value.type != STRING ||
                (result.value.length != 32 && result.value.length != 256))
                valid = false;
            for (size_t j = 0; j < result.value.length; ++j)
                if (result.value.bytes[j] != 'x')
                    valid = false;
        }
        sample(); // Also includes the owned decoded value.
    }
    if (!valid)
    {
        ++invalid;
        return false;
    }
    ++received;
    const unsigned bit = static_cast<unsigned>(id - 1);
    if (!(seen[bit / 8] & (1u << (bit % 8))))
    {
        seen[bit / 8] |= 1u << (bit % 8);
        ++unique;
    }
    return true;
}
void setup()
{
    Serial.begin(115200);
    WiFi.begin(TEST_SSID, TEST_PASSWORD);
    const auto start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 30000)
        delay(100);
    ready = WiFi.status() == WL_CONNECTED &&
            burstClient.notifications("burst-test", notification).ok() &&
            burstClient.begin(1162).ok();
    Serial.printf("BURST_READY ok=%u ip=%s core=%s rssi=%d\n", ready,
                  WiFi.localIP().toString().c_str(), ESP.getCoreVersion().c_str(), WiFi.RSSI());
    Serial.setTimeout(100);
}
void loop()
{
    if (Serial.available())
    {
        const String command = Serial.readStringUntil('\n');
        if (command.startsWith("R "))
        {
            pauseMs = constrain(command.substring(2).toInt(), 0, 100);
            received = unique = invalid = 0;
            memset(seen, 0, sizeof(seen));
            minHeap = minBlock = UINT32_MAX;
            Serial.printf("RESET delay=%u\n", pauseMs);
        }
        if (command == "S")
            Serial.printf(
                "STATS received=%u unique=%u invalid=%u free=%u largest=%u minHeap=%u minBlock=%u\n",
                received, unique, invalid, ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(), minHeap,
                minBlock);
        if (command == "I")
            Serial.printf("BURST_READY ok=%u ip=%s core=%s rssi=%d\n", ready,
                          WiFi.localIP().toString().c_str(), ESP.getCoreVersion().c_str(),
                          WiFi.RSSI());
    }
    if (ready)
        burstClient.loop();
    sample();
    delay(pauseMs); // Yield to Wi-Fi even when the application delay is zero.
}
