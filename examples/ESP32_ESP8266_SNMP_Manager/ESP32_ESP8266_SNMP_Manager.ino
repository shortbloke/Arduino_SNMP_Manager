// New to SNMP (Simple Network Management Protocol)? Read docs/GETTING_STARTED.md.
// Wi-Fi name/password connect the board; the community string grants device access.
// For terms such as OID (a reading's numeric address), see docs/TERMS.md.
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>
#include <Arduino_SNMP_Manager.h>
#include "Polling.h"

// USER CONFIGURATION: replace these placeholders before uploading.
const char *ssid = "SSID";         // Your Wi-Fi network name (case-sensitive).
const char *password = "PASSWORD"; // Your Wi-Fi password.
// Set the address of the SNMP device to query, not this board's address.
IPAddress router(192, 168, 200, 1);
// Match the read community configured on the agent; "public" is only an example.
const char *community = "public";
// Choose a version enabled on your agent: SNMPVersion::Version1 or SNMPVersion::Version2c.
const SNMPVersion snmpVersion = SNMPVersion::Version2c;
// Replace the final .4 in ALL interface OIDs with your device's ifIndex.
// Discover the index from its interface table; it need not equal the port number.
// ifSpeed is interface capacity, which may differ from your Internet service speed.
const char *oidIfSpeedGauge = ".1.3.6.1.2.1.2.2.1.5.4";
const char *oidInOctetsCount32 = ".1.3.6.1.2.1.2.2.1.10.4";
// sysUpTime is a scalar: retain its final .0 (it is not an interface index).
const char *oidUptime = ".1.3.6.1.2.1.1.3.0";
// Timing values are milliseconds; allow enough time for your agent to reply.
const uint32_t pollInterval = 10000;
const uint32_t responseTimeout = 2000;

uint32_t ifSpeedResponse = 0, inOctetsResponse = 0, uptime = 0;
// System scalars retain .0. When changing an OID, match its ASN.1 type
// to the add*Handler used in setup() and the destination variable type.
const char *oidServiceCountInt = ".1.3.6.1.2.1.1.7.0";
// sysName is a scalar string; retain its final .0.
const char *oidSysName = ".1.3.6.1.2.1.1.5.0";
// Use the same ifIndex as the Counter32 OID above; this requires SNMPv2c.
const char *oid64Counter = ".1.3.6.1.2.1.31.1.1.1.6.4";
int32_t servicesResponse = 0;
// Includes the terminating NUL; increase if your agent returns longer names.
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
    if (snmpVersion == SNMPVersion::Version2c)
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
    // Counter64 is a v2c type. If unsupported, remove this registration and
    // its printSample() output. Every registered OID must reply successfully.
    // Apply the same rule to any other optional OID your agent does not support.
    if (snmpVersion == SNMPVersion::Version2c)
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
