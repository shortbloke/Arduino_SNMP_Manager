#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <SNMPClient.h>
#include <initializer_list>
#if __has_include("hardware_config.h")
#include "hardware_config.h"
#else
#define TEST_SSID ""
#define TEST_PASSWORD ""
#define TEST_AGENT "192.0.2.1"
#define TEST_COMMUNITY "public"
#define TEST_PORT 161
#endif

WiFiUDP udp;
SNMPClient client(udp);
SNMPDevice v1(client, TEST_AGENT, TEST_COMMUNITY, SNMPVersion::Version1);
SNMPDevice v2(client, TEST_AGENT, TEST_COMMUNITY, SNMPVersion::Version2c);
SNMPQuery<2> get1(v1), get2(v2);
SNMPWalk<1> nextWalk(v1), bulkWalk(v2);
SNMPOperation *operations[] = {&get1, &get2, &nextWalk, &bulkWalk};
unsigned stage = 0, cycles = 0, failures = 0;
uint32_t rows[2] = {}, hashes[2] = {}, lowestHeap = UINT32_MAX, lowestBlock = UINT32_MAX;
bool running = false;

void memorySample()
{
    const uint32_t heap = ESP.getFreeHeap();
#if defined(ESP8266)
    const uint32_t block = ESP.getMaxFreeBlockSize();
#else
    const uint32_t block = ESP.getMaxAllocHeap();
#endif
    if (heap < lowestHeap)
        lowestHeap = heap;
    if (block < lowestBlock)
        lowestBlock = block;
    Serial.printf(
        "MEM cycle=%u stage=%u free=%lu largest=%lu minSampledFree=%lu minSampledBlock=%lu\n",
        cycles, stage, (unsigned long)heap, (unsigned long)block, (unsigned long)lowestHeap,
        (unsigned long)lowestBlock);
}
bool collect(const SNMPResult &result, void *context)
{
    unsigned index = *static_cast<unsigned *>(context);
    if (!result.ok() || ++rows[index] > 1024)
        return false;
    // Compare instance sets, not changing uptime/counters. Hash collisions are possible;
    // log OIDs too so the serial record supports an exact independent comparison.
    for (const char *p = result.oid; *p; ++p)
        hashes[index] = (hashes[index] ^ static_cast<unsigned char>(*p)) * 16777619u;
    hashes[index] = (hashes[index] ^ 0xffu) * 16777619u;
    Serial.printf("OID version=%u %s\n", index + 1, result.oid);
    memorySample(); // Includes the live decoded response and retained binding.
    return true;
}
unsigned nextIndex = 0, bulkIndex = 1;
void setup()
{
    Serial.begin(115200);
    if (!TEST_SSID[0])
    {
        Serial.println("Configure hardware_config.h before running this test");
        return;
    }
    v1.port = TEST_PORT;
    v2.port = TEST_PORT;
    WiFi.begin(TEST_SSID, TEST_PASSWORD);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - started) < 30000)
        delay(100);
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("FAIL Wi-Fi connection");
        return;
    }
    auto status = client.begin();
    for (auto *query : {&get1, &get2})
    {
        if (status.ok())
            status = query->addOID(".1.3.6.1.2.1.1.2.0", OID);
        if (status.ok())
            status = query->addOID(".1.3.6.1.2.1.1.3.0", TIMESTAMP);
    }
    if (status.ok())
        status = nextWalk.configure(".1.3.6.1.2.1.1");
    if (status.ok())
        status = bulkWalk.configure(".1.3.6.1.2.1.1");
    if (status.ok())
        status = nextWalk.stream(collect, &nextIndex);
    if (status.ok())
        status = bulkWalk.stream(collect, &bulkIndex);
    if (status.ok())
        status = get1.start();
    running = status.ok();
    if (!running)
        Serial.printf("FAIL setup: %s\n", status.message());
    hashes[0] = hashes[1] = 2166136261u;
    memorySample();
}
void loop()
{
    if (!running)
    {
        delay(10);
        return;
    }
    client.loop();
    auto &operation = *operations[stage];
    if (!operation.takeCompleted())
    {
        delay(1);
        return;
    }
    if (!operation.status().ok())
        ++failures;
    Serial.printf("RESULT cycle=%u stage=%u status=%s\n", cycles, stage,
                  operation.status().message());
    memorySample();
    if (++stage == 4)
    {
        if (!rows[0] || rows[0] != rows[1] || hashes[0] != hashes[1])
            ++failures;
        if (!get1[0].ok() || !get2[0].ok() || strcmp(get1[0].value.text(), get2[0].value.text()))
            ++failures;
        stage = 0;
        rows[0] = rows[1] = 0;
        hashes[0] = hashes[1] = 2166136261u;
        if (++cycles == 50)
        {
            Serial.printf("DONE cycles=%u failures=%u; inspect MEM trend and OID sets\n", cycles,
                          failures);
            running = false;
            return;
        }
    }
    auto status = operations[stage]->start();
    if (!status.ok())
    {
        Serial.printf("FAIL start: %s\n", status.message());
        running = false;
    }
}
