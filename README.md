# SNMP Manager For ESP8266/ESP32/Arduino (and more)

Version 2.0 contains breaking API and build changes. Existing users should read the [migration guide](MIGRATION.md).

The new [device and query API](docs/QUERY_API.md) provides checked IPv4 strings, owned results, batched reads, table discovery, writes, and notification reception. Start with the [simple read](examples/Simple_Read/Simple_Read.ino) or [interface traffic](examples/Interface_Traffic/Interface_Traffic.ino) example. See the [example guide](examples/README.md) for every supported operation, value types, and server/NAS and printer examples. The existing handler API remains available.

See the [standards coverage and limits](tests/native/RFC_NOTES.md#current-standards-and-scope-of-this-audit). This is a bounded v1/v2c manager, not a full STD 62/SNMPv3 implementation.

An SNMP Manager for network-capable ESP8266 and ESP32 Arduino platforms, providing SNMPv1 and SNMPv2c manager operations.

Validated build targets include NodeMCU ESP8266, ESP32, ESP32-C3, and Arduino Nano ESP32. Other modern 32-bit Arduino platforms can be added as tested targets; older AVR platforms are not supported. See [embedded compatibility builds](tests/embedded/README.md).

The library supports:

- SNMP Versions:
  - v1 (protocol version 0)
  - v2c (protocol version 1)
- SNMP PDUs
  - GetRequest (sending query to a SNMP Agent for a specified OID)
  - GetResponse (decoding responses and acknowledging v2c INFORMs)
  - GetNextRequest and GetBulkRequest (walking and table discovery; GETBULK is v2c only)
  - SetRequest (single-packet writes)
  - SNMPv1 and SNMPv2c trap reception, plus v2c INFORM reception
- SNMP Data Types:
  - Integer32 (`int32_t`; float callbacks scale integer tenths)
  - String (Arduino data type: char*)
  - Counter32 (`uint32_t`)
  - Counter64 (`uint64_t`)
  - Gauge32 (`uint32_t`)
  - Timestamp (`uint32_t`)

If you find this useful, consider providing some support:

[!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/martinrowan)

**Changelog**: [CHANGELOG.md](CHANGELOG.md)

## Native tests

Run `pio test -e native` to execute all native tests through PlatformIO, or `pio test -e native -a "--gtest_filter=Ber.*"` for BER tests. The suite includes checks for previously reported defects. No Arduino board is required. The standalone Make runner is also retained. See [native test documentation](tests/native/README.md) for coverage, sanitizer checks, and behavior groups.

When a callback is included in a successful `SNMPGet::sendTo`, its responses must match a pending request ID, peer, and UDP transport. Each callback supports `SNMP_MAX_PENDING_REQUESTS` outstanding requests (default 4). A send that needs another slot fails before transmission when the slots are full. Retransmission with the same ID reuses its slot; matching replies consume it. Call `request.cancelPendingRequests()` while its callbacks are attached to abandon timed-out requests, or clear one callback directly. Use distinct IDs while earlier replies may still arrive. Callbacks never included in a successful send retain direct-response handling.

## Buffer safety and ownership

Use `serialise(buffer, capacity)` and `fromBuffer(buffer, length)` when calling BER objects directly. `serialise(nullptr)` measures the encoded size; insufficient capacity returns a negative result. Decoding returns false for malformed or incomplete input. The legacy forms without sizes remain available and require sufficient storage or complete input. Custom BER subclasses must implement the capacity-aware virtual signatures.

String and OID handlers accept a destination capacity including the terminator:

```cpp
char text[64] = {};
char *textPointer = text;
snmp.addStringHandler(deviceIP, oid, &textPointer, sizeof(text));
char oidValue[128] = {};
snmp.addOIDHandler(deviceIP, oid, oidValue, sizeof(oidValue));
```

For binary OCTET STRING or Opaque values, use `addOctetHandler` or `addOpaqueHandler` with a byte buffer, its capacity, and a `size_t*` receiving the actual length. These handlers preserve embedded NULs. Insufficient capacity leaves the destination and length unchanged; C-string handlers also reject embedded NULs. Legacy string/OID calls without capacities remain caller-sized APIs.

Managers own their registrations; requests retain references to registrations they use. Clearing or destroying a request releases those references. Registrations and their OID strings are freed when the last owner releases them. Do not delete a registered callback directly. Destination buffers, community strings, and UDP objects remain caller-owned and must outlive operations that use them. Manager and request objects support move construction, but owning objects cannot be shallow-copied.

## Usage

### SNMPManager

An SNMPManager object is created for listening (on UDP port 162) to and parsing the SNMP GetResponses. This is initialised with the SNMP community string.

```cpp
SNMPManager snmpManager = SNMPManager("public");
```

### SNMPGet

An SNMPGet object is created to make SNMP GetRequest calls (to UDP port 161 by default). It is initialized with the SNMP community string and `SNMPVersion::Version1` or `SNMPVersion::Version2c`. The destination port can be changed using `setPort(<port number>)`.

```cpp
SNMPGet snmpRequest = SNMPGet("public", SNMPVersion::Version2c);
int32_t nextRequestId = 1;
```

### Handlers and Callbacks

The handlers and callbacks for receiving the incoming SNMP GetResponse are configured in `setup()`

```cpp
ValueCallback *callbackSysName;  // Blank Callback for each OID
void setup()
{
    IPAddress target(192, 168, 200, 187);
    callbackSysName = snmpManager.addStringHandler(target, ".1.3.6.1.2.1.1.5.0", &sysNameResponse, sizeof(sysName));  // Callback for SysName for target host
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
    snmpRequest.setUDP(&udp);
    snmpRequest.cancelPendingRequests(); // Previous poll has timed out.
    snmpRequest.setRequestID(nextRequestId++);
    if (!snmpRequest.sendTo(router))
      Serial.println("SNMP request could not be sent");
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

## Troubleshooting

### Additional Logging

- Debug logging: add `-DDEBUG` to the build flags for the sketch and library.
- Additional ASN.1 debug logging: add `-DDEBUG_BER` to the same build flags.

### Suppress Errors

- Suppress SNMP payload parsing errors: add `-DSUPPRESS_ERROR_FAILED_PARSE` to the build flags for the sketch and library.

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

## Embedded development

Use `make -C tests/native check` for normal/debug regressions and strict C++11 multi-file compatibility. The compatibility executable builds with exceptions and RTTI disabled. `pio run -d tests/embedded` builds the real board toolchains; CI runs the same checks.

Source formatting uses clang-format 19.1.7 and the checked-in `.clang-format`. Version 2.0 API changes are documented in the migration guide. Internal pending-request state is private; request summary fields are informational.

Packet buffers use `SNMP_PACKET_LENGTH` directly (512 bytes by default, 1500 on ESP32). Configure this limit as described below if the application requires another value. Registration APIs return null on allocation failure; `addOIDPointer`, `addHandler`, and request building return false on failure. `addHandler` adopts a supplied callback only on success; `addValueToList` consumes a supplied BER child even on failure. Pending sends are not registered until transmission succeeds. Constructors can remain empty after allocation failure, and subsequent operations report failure or retry allocation safely.

### Library compilation and configuration

Public headers contain declarations and small inline operations; the corresponding `src/*.cpp` files implement encoding, decoding, request handling, and callback tracking. Arduino and PlatformIO compile these sources automatically when the library is installed. Custom build systems must compile and link all `src/*.cpp` files. `Arduino_SNMP_Manager.h` remains the umbrella header; individual headers can also be included independently.

Defaults live in `src/SNMPConfig.h`. Apply overrides to **both the application and library sources**, for example in PlatformIO:

```ini
build_flags =
    -DSNMP_PACKET_LENGTH=1024
    -DSNMP_MAX_PENDING_REQUESTS=8
    -DDEBUG
```

The other capacity settings are `SNMP_OCTETSTRING_MAX_LENGTH`, `MAX_OID_LENGTH` (256 bytes by default, including termination), and `SNMP_VALUE_MAX_LENGTH` (1024 bytes per owned query payload by default). See [MIB value helpers](docs/QUERY_API.md#common-mib-values) for checked conversions of common readings. For a shared configuration file, define `SNMP_CONFIG_HEADER` as a quoted header filename in compiler flags and make its include directory available to all sources. Arduino CLI users can pass these `-D` options through `compiler.cpp.extra_flags`.

A `#define` placed only before the include in a sketch no longer configures the separately compiled library. Inconsistent capacity or logging settings produce a linker error mentioning `snmp_detail::BuildConfiguration`; rebuild all sources with the same settings to resolve it. The check uses no heap allocation and performs no I/O.
