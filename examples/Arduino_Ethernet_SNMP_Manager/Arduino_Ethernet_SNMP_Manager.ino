// New to SNMP (Simple Network Management Protocol)? Read docs/GETTING_STARTED.md.
// This example uses wired Ethernet; the community string grants device access.
// For terms such as OID (a reading's numeric address), see docs/TERMS.md.
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Arduino_SNMP_Manager.h>
#include "Polling.h"

// USER CONFIGURATION: use your Ethernet module's MAC, unique on this network.
// This sketch requires DHCP. Configure the module's SPI/CS wiring for your board.
byte mac[] = {0xA8, 0x61, 0x0A, 0xAE, 0x64, 0x29}; // Use a unique MAC.
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
EthernetUDP udp;
SNMPManager snmp(community);
SNMPGet snmpRequest(community, snmpVersion);
PollState<3> sample;
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
    Serial.println(F("----------------------"));
}

void setup()
{
    Serial.begin(115200);
    // Initialize DHCP once. A failed initialization must not start polling.
    if (Ethernet.begin(mac) == 0)
    {
        Serial.println(F("Ethernet DHCP failed; check the module and network."));
        return;
    }
    Serial.print(F("IP address: "));
    Serial.println(Ethernet.localIP());
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
    Ethernet.maintain(); // Renew the DHCP lease during long-running polls.
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
