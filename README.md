# SNMP Manager For ESP8266/ESP32/Arduino (and more)

An SNMP Manager for ESP, Arduino and similar MCU. Providing simple SNMP Get-Request support for specified OIDs.

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
  - Gauge32 (Arduino data type: unsigned int)
  - Timestamp (Arduino data type: unsigned int)

If you find this useful, consider providing some support:

<a href="https://www.buymeacoffee.com/martinrowan" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/default-orange.png" alt="Buy Me A Coffee" height="41" width="174"></a>

**Changelog**: [CHANGELOG.md](CHANGELOG.md)

## Usage

### SNMPManager

An SNMPManager object is created for listening (on UDP port 162) to and parsing the SNMP GetResponses. This is initialised with the SNMP community string.

```cpp
SNMPManager snmpManager = SNMPManager("public");
```

### SNMPGet

An SNMPGet object is created to make SNMP GetRequest calls (from UDP port 161 (by default)). This is initialised with the SNMP community string and an SNMP version. Note SNMPv1 = 0, SNMPv2 = 1. The port scan be changed if required using `setPort(<port number>)`

```cpp
SNMPGet snmpRequest = SNMPGet("public", 1);
```

### Handlers and Callbacks

The handlers and callbacks for receiving the incoming SNMP GetResponse are configured in `setup()`

```cpp
ValueCallback *callbackSysName;  // Blank Callback for each OID
void setup()
{
    IPAddress target(192, 168, 200, 187);
    callbackSysName = snmpManager.addStringHandler(target, ".1.3.6.1.2.1.1.5.0", &sysNameResponse);  // Callback for SysName for target host
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

You can add multiple OID to be queried in a single request by calling `snmpRequest.addOIDPointer(another_callback);` This approach ensures all the requested OID are returned in the same response. Though I expect there are limits on the maximum packet sizes, so some experimentation may be required with large numbers of OID.

## Working With SNMP Data

### Time Based Measurements

It's important to note that even if you make GetRequests every _n_ seconds, that the response may not arrive in the allotted time period. SNMP responses are often deprioritised by devices when under load. As such your poll interval shouldn't be used for any calculations of time, instead using the devices uptime counter will show the time elapsed between data collections. For example if I want to calculate the bandwidth utilisation of my ADSL connection, then we need to look calculate: `Utilisation = Amount of Data in time period / Max Possible data in time period`

Which can be performed with the following:

```cpp
// Note: Calculation will be incorrect if inOctets counter has wrapped.
bandwidthUtilisationPercent = ((float)((inOctets - lastInOctets) * 8) / (float)(downSpeed * ((uptime - lastUptime) / 100)) * 100);
```

What does this mean? Well lets explain the variables:

- inOctets: (Counter32) ifInOctets (.1.3.6.1.2.1.2.2.1.10.4) - Amount of bytes received on the specified interface, 4 in this example.
- lastInOctets: Stores the inOctets from the previous poll.
- downSpeed: (Gauge) - The maximum possible download speed in bps (bits per second). This can be measured value from your own speed test, or you might query the interface speed, or in the of (A/V)DSL you might query the sync speed adslAtucChanCurrTxRate (.1.3.6.1.2.1.10.94.1.1.4.1.2.4) again for interface 4.
- uptime: (TimeTicks) - SysUpTime (.1.3.6.1.2.1.1.3.0) - The time in hundredths of seconds since the device was last reinitialised.
- lastUptime: Stores the upTime from the previous poll.

This can be broken down as:

- `((inOctets - lastInOctets) * 8)` calculates the delta in data received and converts Bytes to Bits by multiplying by 8.
- `((uptime - lastUptime) / 100)` calculates the time between two samples and converts it to seconds.
- `(downSpeed * ((uptime - lastUptime) / 100))` We calculate how much data could have been theoretically received in the elapsed time for a given maximum download speed.

### Counters

When working with SNMP Counters (COUNTER32 or COUNTER64) they can only be used for measuring a change between two values. A device reboot may reset the counter such that the calculating a delta would give an incorrect reading. Or depending on the sample period and the rate of change, the counter can wrap.

#### Counters Wrapping

To compensate for wrapping, you can:

- Poll more frequently, giving less time for the counter to have wrapped. But doing so increased the load on the SNMP agent device and the manager.
- If the device supports them, then High Capacity (HC) 64bit counters can be used. Note SNMPv1 doesn't support COUNTER64, this is only available in SNMPv2 and later.
- If the counter has wrapped, you could assert it has only wrapped once in the sample period. For the example for Bandwidth utilisation above we'd need to adjust the formula to correct compensation if we detect it has wrapped. To calculate the delta in traffic being measured with a COUNTER32 which has is an unsigned integer (maximum value: 4294967295) gives us: `(((4294967295 - lastInOctets) + inOctets) * 8)`

```cpp
if (inOctets > lastInOctets)
{
  // Note: Calculation will be incorrect if inOctets counter has wrapped.
  bandwidthUtilisationPercent = ((float)((inOctets - lastInOctets) * 8) / (float)(downSpeed * ((uptime - lastUptime) / 100)) * 100);
}
else if (lastInOctets > inOctets)
{
  // This handles 32bit counters wrapping a maximum of one time.
  bandwidthUtilisationPercent = (((float)((4294967295 - lastInOctets) + inOctets) * 8) / (float)(downSpeed * ((uptime - lastUptime) / 100)) * 100);
}
```

#### Device Reset

To compensate for device reset:

- Monitor SysUptime and if is lower than the previous value, then assume the device has restarted, don't process the data, just store the new counter values and await the next poll to be able to calculate the difference.

### Strings

SNMP can be used to query strings, however long strings lead to larger packet sizes needing larger buffers and increased memory usage. The ESP8266 appears to have a bug in the WiFi or UDP protocol support, leading to a maximum UDP packet size that can be received being 1024 bytes. As there are can be multiple OID responses in a single packet along with headers etc, this will reduce the maximum string size that can be received. Reading strings in to a character arrays can use a significant amount of memory, which may not be available on some MCUs. As such query strings should will likely need to be limited.

## Examples

The examples folder contains an SNMP GetRequest example for each of the data types. Note that the OID will need to be adapted the device you are querying. To understand what OID your device supports and the data type of each one, I'd recommend walking to the device with standard SNMP tools:

- [iReasoning MIB Browser](https://www.ireasoning.com/mibbrowser.shtml)
- Using [net-snmp](http://www.net-snmp.org/) snmpwalk. A command line tool available for various OS. Basic introductory usage information can be found [in this article](https://www.comparitech.com/net-admin/snmpwalk-examples-windows-linux/)

### Examples folder contents

- [ESP32_ESP8266_SNMP_Manager.ino](examples/ESP32_ESP8266_SNMP_Manager/ESP32_ESP8266_SNMP_Manager.ino) - ESP32/ESP2866 boards
- [ESP_Multiple_SNMP_Device_Polling.ino](examples/ESP_Multiple_SNMP_Device_Polling/ESP_Multiple_SNMP_Device_Polling.ino) - ESP32/ESP8266 boards querying multiple devices and storing results in a device record array
- [Arduino_Ethernet_SNMP_Manager.ino](examples/Arduino_Ethernet_SNMP_Manager/Arduino_Ethernet_SNMP_Manager.ino) - Arduino Mega with Ethernet Shield

## Tested Devices

The following devices have been confirmed to work with this library (these are affiliate links that help support my work):

- WeMos D1 Mini - ESP8266 - [Amazon UK](https://amzn.to/3z6rQBt) [Amazon US](https://amzn.to/3AY4aBE)
- ESP32S Dev Module - [Amazon UK](https://amzn.to/2TAqWZJ) [Amazon US](https://amzn.to/3PgUZAx)

## Projects using this library

I'd love to hear about projects that find this library useful.

- [Broadband Utilisation Display](https://github.com/shortbloke/Broadband_Usage_Display) - An LED display showing broadband upstream and downstream utilisation.
- [Dekatron-speed](https://github.com/elegantalchemist/dekatron-speed) - Uses a Dekatron (1950s era neon counting tube) spinning based on broadband utilisation rate.
- [Wio Terminal - Router Graph LCD](https://github.com/dbzoo/wio_terminal/tree/master/router_graph_lcd) - Uses the SeeedStudio [Wio Terminal](https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/) to plot traffic received and transmitted rates on the integrated display.
- [Wio Terminal - Router Stats LCD](https://github.com/dbzoo/wio_terminal/tree/master/router_stats_lcd) - Uses the SeeedStudio [Wio Terminal](https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/) to show the current receive and transmit rates on the integrated display.

## Acknowledgements

This project a derived from an [SNMP Agent project](https://github.com/fusionps/Arduino_SNMP). With Manager functionality adapted from work by [Niich's fork](https://github.com/Niich/Arduino_SNMP).
