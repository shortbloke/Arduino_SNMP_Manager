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

const char *ssid = "YOUR_SSID";         // Your Wi-Fi network name (SSID).
const char *password = "YOUR_PASSWORD"; // Your Wi-Fi password, not the device community.
WiFiUDP udp;
SNMPClient client(udp);
// Uses a write-enabled community. Send 'w' in Serial Monitor to write once.
// There is no automatic SET retry; a timeout can mean the write already happened.
SNMPDevice device(client, "192.168.1.1", "YOUR_WRITE_COMMUNITY");
SNMPSet<1> writeLocation(device);
SNMPQuery<1> readLocation(device);
bool ready = false;
void setup()
{
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
        delay(100);
    SNMPValue location;
    const char text[] = "Lab";
    auto status =
        location.setBytes(reinterpret_cast<const unsigned char *>(text), sizeof(text) - 1);
    if (status.ok())
        status = writeLocation.addValue(".1.3.6.1.2.1.1.6.0", location);
    if (status.ok())
        status = readLocation.addOID(".1.3.6.1.2.1.1.6.0", STRING);
    if (status.ok())
        status = client.begin();
    ready = status.ok();
    Serial.println(ready ? "Send w to set sysLocation to Lab" : status.message());
}
void loop()
{
    if (!ready)
        return;
    client.loop();
    if (writeLocation.takeCompleted())
    {
        Serial.println(writeLocation.status().message());
        // Read back after success, error, or timeout before considering another write.
        auto status = readLocation.start();
        if (!status.ok())
            Serial.println(status.message());
    }
    if (readLocation.takeCompleted())
    {
        if (readLocation[0].ok() && readLocation[0].value.isText())
            Serial.println(readLocation[0].value.text());
        else if (!readLocation[0].ok())
            Serial.println(readLocation[0].status.message());
        else
            Serial.println("Location contains binary data");
    }
    if (Serial.available() && Serial.read() == 'w' && !writeLocation.pending() &&
        !readLocation.pending())
    {
        auto status = writeLocation.start();
        if (!status.ok())
            Serial.println(status.message());
    }
}
