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

// Named test profiles use different agents while keeping Wi-Fi credentials in
// the ignored hardware_config.h file. The base d1_mini profile remains backward
// compatible with TEST_AGENT/TEST_COMMUNITY/TEST_PORT.
#if defined(TEST_TARGET_WINDOWS) && defined(TEST_WINDOWS_AGENT) && defined(TEST_WINDOWS_COMMUNITY)
#define ACTIVE_TEST_AGENT TEST_WINDOWS_AGENT
#define ACTIVE_TEST_COMMUNITY TEST_WINDOWS_COMMUNITY
#if defined(TEST_WINDOWS_PORT)
#define ACTIVE_TEST_PORT TEST_WINDOWS_PORT
#else
#define ACTIVE_TEST_PORT 161
#endif
#elif defined(TEST_TARGET_UBUNTU) && defined(TEST_UBUNTU_AGENT) && defined(TEST_UBUNTU_COMMUNITY)
#define ACTIVE_TEST_AGENT TEST_UBUNTU_AGENT
#define ACTIVE_TEST_COMMUNITY TEST_UBUNTU_COMMUNITY
#if defined(TEST_UBUNTU_PORT)
#define ACTIVE_TEST_PORT TEST_UBUNTU_PORT
#else
#define ACTIVE_TEST_PORT 161
#endif
#else
#define ACTIVE_TEST_AGENT TEST_AGENT
#define ACTIVE_TEST_COMMUNITY TEST_COMMUNITY
#define ACTIVE_TEST_PORT TEST_PORT
#endif

WiFiUDP udp;
SNMPClient client(udp);
SNMPDevice v1(client, ACTIVE_TEST_AGENT, ACTIVE_TEST_COMMUNITY, SNMPVersion::Version1);
SNMPDevice v2(client, ACTIVE_TEST_AGENT, ACTIVE_TEST_COMMUNITY, SNMPVersion::Version2c);
SNMPQuery<2> get1(v1), get2(v2);
SNMPQuery<1> missing1(v1), missing2(v2);
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
unsigned errorStage = 0;
void setup()
{
    Serial.begin(115200);
    Serial.println("START hardware SNMP test");
    if (!TEST_SSID[0])
    {
        Serial.println("Configure hardware_config.h before running this test");
        return;
    }
    v1.port = ACTIVE_TEST_PORT;
    v2.port = ACTIVE_TEST_PORT;
    WiFi.begin(TEST_SSID, TEST_PASSWORD);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - started) < 30000)
        delay(100);
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("FAIL Wi-Fi connection");
        return;
    }
    Serial.println("WIFI connected");
#if defined(ESP8266)
    Serial.printf("BOARD core=%s sdk=%s cpuMHz=%u flashBytes=%u rssi=%d\n",
                  ESP.getCoreVersion().c_str(), ESP.getSdkVersion(),
                  static_cast<unsigned>(ESP.getCpuFreqMHz()),
                  static_cast<unsigned>(ESP.getFlashChipRealSize()), static_cast<int>(WiFi.RSSI()));
#endif
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
    // Both protocol versions must turn an absent object into an explicit
    // Missing result; neither request writes to the agent.
    if (status.ok())
        status = missing1.addOID(".1.3.6.1.2.1.1.999.0", INTEGER);
    if (status.ok())
        status = missing2.addOID(".1.3.6.1.2.1.1.999.0", INTEGER);
    if (status.ok())
        status = missing1.start();
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
    if (errorStage == 0)
    {
        if (!missing1.takeCompleted())
        {
            delay(1);
            return;
        }
        if (missing1.status().code() != SNMPStatus::Missing || missing1[0].status.code() != SNMPStatus::Missing)
        {
            Serial.printf("FAIL v1 missing-object status=%s\n", missing1.status().message());
            running = false;
            return;
        }
        Serial.println("ERRORCHECK version=1 missing=Missing");
        const auto status = missing2.start();
        if (!status.ok())
        {
            Serial.printf("FAIL v2 missing-object start=%s\n", status.message());
            running = false;
        }
        errorStage = 1;
        return;
    }
    if (errorStage == 1)
    {
        if (!missing2.takeCompleted())
        {
            delay(1);
            return;
        }
        const bool missingHandled = missing2.status().code() == SNMPStatus::Missing ||
                                    (missing2.status().code() == SNMPStatus::ProtocolError &&
                                     missing2.agentError() == 2);
        if (!missingHandled || missing2[0].ok())
        {
            Serial.printf("FAIL v2 missing-object status=%s\n", missing2.status().message());
            running = false;
            return;
        }
        Serial.println("ERRORCHECK version=2 missing=Handled");
        const auto status = get1.start();
        if (!status.ok())
        {
            Serial.printf("FAIL first read start=%s\n", status.message());
            running = false;
        }
        errorStage = 2;
        return;
    }
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
