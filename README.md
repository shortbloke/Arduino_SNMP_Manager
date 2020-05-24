# Arduino_SNMP_Manager

An SNMP Manager for Arduino and similar MCU. Providing simple SNMP Get-Request support for specified OIDs.

The library supports:

- SNMP Versions:
  - v1 (protocol version 0)
  - v2 (protocol version 1)
- SNMP PDUs
  - GetRequest (sending query to a SNMP Agent for a specified OID)
  - GetResponse (Decoding the response to the SNMP GetRequest)
- SNMP Data Types:
  - Integer (Arduino data type: int)
  - String (Arduino data type: char*)
  - Counter32 (Arduino data type: unsigned int)
  - Counter64 (Arduino data type: long long unsigned int)
  - Guage32 (Arduino data type: unsigned int)
  - Timestamp (Arduino data type: int)

## Usage

An SNMPManager object is created for listening (on UDP port 162) to and parsing the SNMP GetResponses. This is initialised with the SNMP community string.

```cpp
SNMPManager snmpManager = SNMPManager("public");
```

An SNMPGet object is created to make SNMP GetRequest calls (from UDP port 161). This is initialised with the SNMP community string and an SNMP version. Note SNMPv1 = 0, SNMPv2 = 1.

```cpp
SNMPGet snmpRequest = SNMPGet("public", 1);
```

The handlers and callbacks for receiving the incoming SNMP GetResponse are configured in `setup()`

```cpp
ValueCallback *callbackSysName;  // Blank Callback for each OID
void setup()
{
    snmpManager.addStringHandler(".1.3.6.1.2.1.1.5.0", &sysNameResponse);  // Handler for SysName
    callbackSysName = snmpManager.findCallback(".1.3.6.1.2.1.1.5.0");  // Callback for SysName
}
```

Within the main program `snmpManager.loop()` needs to be called frequently to capture and parse incoming GetResponses. GetRequests can be sent as needed, though typically a significantly lower rate than the main loop.

```cpp
void loop()
{
    snmpManager.loop();  // Call frequently
    getSNMP();
}
void getSNMP()
{
  // Check to see if it is time to send an SNMP request.
  if ((timeLast + pollInterval) <= millis())
  {
    // Send SNMP Get request
    snmpRequest.addOIDPointer(callbackSysName);
    snmpRequest.setIP(WiFi.localIP()); //IP of the arduino
    snmpRequest.setUDP(&udp);
    snmpRequest.setRequestID(rand() % 5555);
    snmpRequest.sendTo(router);
    snmpRequest.clearOIDList();
    // Display response (first call might be empty)
    Serial.print("sysNameResponse: ");
    Serial.println(sysNameResponse);
    Serial.println("----------------------");

    timeLast = millis();
  }
}
```

## Examples

The examples folder contains an SNMP GetRequest example for each of the data types. Note that the OID will need to be adapted the device you are querying. To understand what OID your device supports and the data type of each one, I'd recommend walking to the device with standard SNMP tools:

- [iReasoning MIB Browser](https://www.ireasoning.com/mibbrowser.shtml)
- Using [net-snmp](http://www.net-snmp.org/) snmpwalk. A command line tool available for various OS. Basic introductory usage information can be found [in this article](https://www.comparitech.com/net-admin/snmpwalk-examples-windows-linux/)

### Examples folder contents

- [ESP8266_snmpget.ino](examples/ESP8266_SNMP_Manager/ESP8266_snmpget.ino) - ESP8266
- [ESP32_snmpget.ino](examples/ESP32_SNMP_Manager/ESP32_snmpget.ino) - ESP32

## Tested Devices

The following devices have been confirmed to work with this library:

- WeMos D1 Mini (v3.1) - ESP8266 - [eBay UK](https://www.ebay.co.uk/itm/WeMos-D1-Mini-LATEST-V3-1-UK-Stock-Arduino-NodeMCU-MicroPython-WiFi-ESP8266/112325195239)
- ESP32S Dev Module - [Amazon UK](https://amzn.to/2TAqWZJ)

## Projects using this library

Contributions welcome

## Acknowledgements

This project a derived from an [SNMP Agent project](https://github.com/fusionps/Arduino_SNMP). With Manager functionality adapted from work by [Niich's fork](https://github.com/Niich/Arduino_SNMP).
