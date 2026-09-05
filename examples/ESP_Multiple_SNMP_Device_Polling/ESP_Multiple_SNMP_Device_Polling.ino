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
// Match the read community configured on the agent; "public" is only an example.
const char *community = "public";
// Choose a version enabled on your agent: SNMPVersion::Version1 or SNMPVersion::Version2c.
const SNMPVersion snmpVersion = SNMPVersion::Version2c;
// sysName is a scalar string; retain its final .0.
const char *oidSysName = ".1.3.6.1.2.1.1.5.0";
// sysUpTime is a scalar: retain its final .0 (it is not an interface index).
const char *oidUptime = ".1.3.6.1.2.1.1.3.0";
// All timing values are milliseconds. Increase the timeout for slower agents.
const uint32_t devicePollInterval = 100;
const uint32_t lastDeviceWaitPeriod = 5000;
const uint32_t responseTimeout = 2000;
// Poll this inclusive last-octet range. Also change 192, 168, 200 in
// device.address inside setup() to your agents' network prefix.
// Every device in this example must use the community and version above.
#define LOWEROCTETLIMIT 1
#define UPPEROCTETLIMIT 6
static_assert(LOWEROCTETLIMIT >= 1 && UPPEROCTETLIMIT <= 254 && LOWEROCTETLIMIT <= UPPEROCTETLIMIT,
              "Configure a valid last-octet range");

WiFiUDP udp;
SNMPManager snmp(community);
struct Device
{
    IPAddress address;
    // Includes the terminating NUL; increase for longer device names.
    char name[50] = {};
    char *namePointer = name;
    uint32_t uptime = 0;
    SNMPGet request{community, snmpVersion};
    PollState<2> sample;
    bool fresh = false;
};
const size_t deviceCount = UPPEROCTETLIMIT - LOWEROCTETLIMIT + 1;
Device devices[deviceCount];
size_t nextDevice = 0;
uint32_t lastSend = 0;
short requestID = 0;
bool ready = false, firstSend = true;

void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - start) < 30000)
        delay(100);
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(F("Wi-Fi connection failed."));
        return;
    }
    snmp.setUDP(&udp);
    if (!snmp.begin())
    {
        Serial.println(F("SNMP bind failed."));
        return;
    }
    for (size_t i = 0; i < deviceCount; ++i)
    {
        Device &device = devices[i];
        device.address = IPAddress(192, 168, 200, LOWEROCTETLIMIT + i); // Configure your subnet.
        device.request.setUDP(&udp);
        // These registrations and OID lists are reused on every polling cycle.
        if (!device.sample.add(snmp.addStringHandler(device.address, oidSysName,
                                                     &device.namePointer, sizeof(device.name)),
                               device.request) ||
            !device.sample.add(snmp.addTimestampHandler(device.address, oidUptime, &device.uptime),
                               device.request))
        {
            Serial.println(F("SNMP registration failed."));
            return;
        }
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
    bool pending = false;
    for (Device &device : devices)
    {
        if (device.sample.complete())
        {
            device.fresh = true;
            device.sample.finish();
        }
        else if (device.sample.expired(now, responseTimeout))
            device.sample.finish();
        pending = pending || device.sample.active();
    }
    if (nextDevice < deviceCount)
    {
        if (firstSend || uint32_t(now - lastSend) >= devicePollInterval)
        {
            firstSend = false;
            lastSend = now;
            Device &device = devices[nextDevice++];
            device.fresh = false;
            requestID = nextRequestID(requestID);
            device.request.setRequestID(requestID);
            if (!device.sample.begin(now) || !device.request.sendTo(device.address))
            {
                device.sample.finish();
                Serial.print(F("SNMP send failed: "));
                Serial.println(device.address);
            }
        }
    }
    else if (!pending && uint32_t(now - lastSend) >= lastDeviceWaitPeriod)
    {
        // Wait from the actual final send, rather than a timer anchored to startup.
        for (const Device &device : devices)
        {
            Serial.print(device.address);
            if (!device.fresh)
                Serial.println(F(" - no fresh complete response"));
            else
            {
                Serial.print(F(" - Name: "));
                Serial.print(device.name);
                Serial.print(F(" - Uptime: "));
                Serial.println(device.uptime);
            }
        }
        nextDevice = 0;
        firstSend = true;
    }
}
