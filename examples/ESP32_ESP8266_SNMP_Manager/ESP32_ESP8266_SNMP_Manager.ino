#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <Arduino_SNMP_Manager.h>
#include "Polling.h"

const char *ssid = "SSID";
const char *password = "PASSWORD";
// Configure the agent and interface index for your device.
IPAddress router(192, 168, 200, 1);
const char *community = "public";
const short snmpVersion = 1; // 0 = SNMPv1, 1 = SNMPv2c.
const char *oidIfSpeedGauge = ".1.3.6.1.2.1.2.2.1.5.4";
const char *oidInOctetsCount32 = ".1.3.6.1.2.1.2.2.1.10.4";
const char *oidUptime = ".1.3.6.1.2.1.1.3.0";
const uint32_t pollInterval = 10000;
const uint32_t responseTimeout = 2000;

uint32_t ifSpeedResponse = 0, inOctetsResponse = 0, uptime = 0;
const char *oidServiceCountInt = ".1.3.6.1.2.1.1.7.0";
const char *oidSysName = ".1.3.6.1.2.1.1.5.0";
const char *oid64Counter = ".1.3.6.1.2.1.31.1.1.1.6.4";
int servicesResponse = 0;
char sysName[50] = {};
char *sysNameResponse = sysName;
uint64_t hcCounter = 0;
WiFiUDP udp;
SNMPManager snmp(community);
SNMPGet snmpRequest(community, snmpVersion);
PollState<6> sample;
Counter32Rate rate;
uint32_t pollStart = 0;
short requestID = 0;
bool ready = false, firstPoll = true;

void printSample()
{
    double utilisation = 0;
    if (rate.sample(inOctetsResponse, uptime, ifSpeedResponse, utilisation))
    {
        Serial.print(F("Bandwidth In Utilisation %: "));
        Serial.println(utilisation, 1);
    }
    else
        Serial.println(
            F("Fresh sample received; bandwidth unavailable until a usable interval exists."));
    Serial.print(F("ifSpeed: "));
    Serial.println(ifSpeedResponse);
    Serial.print(F("ifInOctets: "));
    Serial.println(inOctetsResponse);
    Serial.print(F("Uptime: "));
    Serial.println(uptime);
    Serial.print(F("Services: "));
    Serial.println(servicesResponse);
    Serial.print(F("Name: "));
    Serial.println(sysNameResponse);
    if (snmpVersion == 1)
        Serial.printf("HC counter: %llu\n", static_cast<unsigned long long>(hcCounter));
    Serial.println(F("----------------------"));
}

void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - start) < 30000)
        delay(100);
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(F("Wi-Fi connection failed; check configuration."));
        return;
    }
    Serial.print(F("IP address: "));
    Serial.println(WiFi.localIP());
    snmp.setUDP(&udp);
    if (!snmp.begin())
    {
        Serial.println(F("SNMP bind failed."));
        return;
    }
    snmpRequest.setUDP(&udp);
    // Register and build the OID list once; later polls reuse both.
    bool registered =
        sample.add(snmp.addGaugeHandler(router, oidIfSpeedGauge, &ifSpeedResponse), snmpRequest) &&
        sample.add(snmp.addCounter32Handler(router, oidInOctetsCount32, &inOctetsResponse),
                   snmpRequest) &&
        sample.add(snmp.addTimestampHandler(router, oidUptime, &uptime), snmpRequest);
    registered =
        registered &&
        sample.add(snmp.addIntegerHandler(router, oidServiceCountInt, &servicesResponse),
                   snmpRequest) &&
        sample.add(snmp.addStringHandler(router, oidSysName, &sysNameResponse, sizeof(sysName)),
                   snmpRequest);
    // Counter64 is a v2c type. Remove this optional binding if your agent lacks it.
    if (snmpVersion == 1)
        registered =
            registered &&
            sample.add(snmp.addCounter64Handler(router, oid64Counter, &hcCounter), snmpRequest);
    if (!registered)
    {
        Serial.println(F("SNMP registration failed."));
        return;
    }
    ready = true;
}

void loop()
{
    if (!ready)
    {
        delay(10);
        return;
    }
    snmp.loop();
    const uint32_t now = millis();
    if (sample.complete())
    {
        sample.finish();
        printSample(); // Every requested destination was updated by this request.
    }
    else if (sample.expired(now, responseTimeout))
    {
        sample.finish();
        rate.reset();
        Serial.println(F("Missing or rejected SNMP values; sample skipped."));
    }
    if (!sample.active() && (firstPoll || uint32_t(now - pollStart) >= pollInterval))
    {
        firstPoll = false;
        pollStart = now; // No catch-up burst after a slow response.
        requestID = nextRequestID(requestID);
        snmpRequest.setRequestID(requestID);
        if (!sample.begin(now) || !snmpRequest.sendTo(router))
        {
            sample.finish();
            rate.reset();
            Serial.println(F("SNMP send failed; sample skipped."));
        }
    }
}
