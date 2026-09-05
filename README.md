# SNMP Manager For ESP8266/ESP32/Arduino (and more)

Version 1.2.1 is a header-only SNMP manager for ESP8266, ESP32, and compatible
network-capable Arduino platforms. It sends GetRequest queries for exact OIDs
and processes their responses. GetNext, GetBulk, walks, Set, notifications, and
SNMPv3 are not implemented by this version.

The library supports:

- SNMP Versions:
  - v1 (protocol version 0)
  - v2c (protocol version 1)
- SNMP PDUs
  - GetRequest (sending query to a SNMP Agent for a specified OID)
  - GetResponse (Decoding the response to the SNMP GetRequest)

Value handlers use the following SNMP types. The API method names are retained
for 1.x compatibility; for example, `addTimestampHandler` handles `TimeTicks`.

| SNMP type | Handler method | C++ destination |
| --- | --- | --- |
| INTEGER | `addIntegerHandler` | `int32_t` or a compatible signed integer type; signed 32-bit values |
| INTEGER | `addFloatHandler` | `float`, using the legacy divide-by-ten conversion below |
| OCTET STRING | `addStringHandler` | Caller-owned `char` buffer passed through `char**`, for text |
| OCTET STRING | `addOctetHandler` | Byte buffer and `size_t` length, for binary data |
| OBJECT IDENTIFIER | `addOIDHandler` | Caller-owned `char` buffer containing dotted OID text |
| Counter32 | `addCounter32Handler` | `uint32_t` |
| Gauge32 | `addGaugeHandler` | `uint32_t` |
| TimeTicks | `addTimestampHandler` | `uint32_t`, measured in hundredths of a second |
| Opaque | `addOpaqueHandler` | Byte buffer and `size_t` length |
| Counter64 | `addCounter64Handler` | `uint64_t` (SNMPv2c only) |

`addFloatHandler` retains the legacy convention of converting an INTEGER value to a
`float` divided by ten. Use it only when that scale matches the queried object;
it is not a general SNMP floating-point decoder.

If you find this useful, consider providing some support:

[!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/martinrowan)

**Changelog**: [CHANGELOG.md](CHANGELOG.md)

## Pin your project's library version

Use an exact release for reproducible builds, or a major-version range if you want
compatible updates. Avoid an unversioned Git URL or a moving branch such as `master`
when your project must remain on the 1.x API. The 2.x rework is a separate development
branch, not a published release at the time of 1.2.1.

### Arduino IDE and Arduino CLI

Install **SNMP Manager** from Arduino's community Library Manager. In Arduino IDE,
open **Tools > Manage Libraries**, search for **SNMP Manager**, and select the
required version. For existing 1.x projects, select **1.2.1** (or another compatible
1.x release). There is no need to add a GitHub URL or a custom library index.

Review upgrade prompts instead of accepting a future 2.x upgrade for a 1.x project.
Library Manager installation is shared by sketches using that library directory;
it is not a per-project major-version lock. Record the exact version in your
project's setup instructions.

For scripted Arduino CLI setup, request an exact indexed version:

```sh
arduino-cli lib update-index
arduino-cli lib install "SNMP Manager@1.2.1"
```

New releases can take time to appear in the community index. Refresh the index
and retry if the requested version is not yet available. To use a future 2.x
release, install its exact version intentionally after adapting the sketch;
the CLI command installs a version, not a persistent major-version constraint.

### PlatformIO

Use the **shortbloke/SNMP Manager** registry package in your environment's
`platformio.ini`, alongside its existing board/framework settings:

```ini
lib_deps =
    shortbloke/SNMP Manager@^1.2.1
```

Choose the constraint that suits your project:

| Requirement | `lib_deps` entry | Allowed versions |
| --- | --- | --- |
| Remain on 1.x | `shortbloke/SNMP Manager@^1.1.13` | At least 1.1.13, below 2.0.0 |
| Require this patch, remain on 1.x | `shortbloke/SNMP Manager@^1.2.1` | At least 1.2.1, below 2.0.0 |
| Exact patch | `shortbloke/SNMP Manager@1.2.1` | Only 1.2.1 |
| Future 2.x opt-in | `shortbloke/SNMP Manager@^2.0.0` | At least 2.0.0, below 3.0.0 |

Constraints only work for versions published in the PlatformIO Registry, which is
separate from Arduino's community index. The 2.x constraint is illustrative until
2.0.0 is published; migrate your code before changing to it. See
[PlatformIO dependency configuration](https://docs.platformio.org/en/latest/projectconf/sections/env/options/library/lib_deps.html).

### Optional source installation

If you need a release before it appears in your package index, the
[GitHub releases](https://github.com/shortbloke/Arduino_SNMP_Manager/releases)
provide source archives. This is an optional fallback, not required for normal
Library Manager installation. For a PlatformIO Git dependency, append the exact
tag (for example, `#v1.2.1`) to the repository URL; Git tags do not accept
Registry-style version ranges. Avoid duplicate manual copies in your library
search paths.

## Configure the examples

Before uploading, edit the configuration near the top of your sketch:

- Replace `SSID` and `PASSWORD` with your Wi-Fi credentials, or configure the
  Ethernet MAC and module wiring for the Ethernet example (which uses DHCP).
- Set the SNMP agent address, read community, and supported version. The board's
  own IP address is different from the agent address being queried.
- For multi-device polling, set the last-octet range and the network prefix in
  `device.address` inside `setup()`. These devices share the configured community
  and version.
- Replace `.4` in each interface OID with the target interface's `ifIndex`.
  Keep `.0` on system scalars such as `sysName` and `sysUpTime`.
- Match changed OIDs to the handler and destination type. Remove unsupported
  optional registrations and their output; every requested value must update
  before the examples report a complete sample.
- Adjust polling/timeout values (milliseconds) and name buffer capacities for
  your devices. Keep `Polling.h` beside the sketch when copying it.

## 1.x compatibility and tests

Run `pio test -e native` or `make -C tests/native check` for the normal regression
suite. No Arduino board is required. `make -C tests/native sanitize` runs
AddressSanitizer and UndefinedBehaviorSanitizer checks. See the
[native test documentation](tests/native/README.md) for requirements and coverage.
The suite also checks the 1.x API, maintained examples, and sketch-local configuration.

When a callback is included in a successful `SNMPGet::sendTo`, its responses must match a pending request ID, peer, and UDP transport. Each callback supports `SNMP_MAX_PENDING_REQUESTS` outstanding requests (default 4). By default a full window replaces an older pending slot so lost replies do not stop existing polling loops. Set `callback->strictTracking = true` to opt into refusing sends when the window is full. Retransmission with the same ID reuses its slot; matching replies consume it. Call `callback->clearPendingRequests()` to abandon timed-out requests. Use distinct IDs while earlier replies may still arrive. Callbacks never included in a successful send retain legacy direct-response handling.

The 1.x API remains header-only: numeric version arguments, `setIP()`, short request IDs/ports, and sketch-local configuration defines remain supported. New checked `tryAddOIDPointer`, `tryAddHandler`, and `tryAddValueToList` methods report allocation failures; the original void methods remain available.

`callback->updateCount()` increments only when a response successfully writes that destination, including when the value is unchanged. Save the count before sending and compare afterwards to distinguish a fresh value from stale storage. Errors, exceptions, and rejected values do not count as updates. Duplicate replies are rejected for tracked requests; legacy untracked handlers do not provide duplicate detection. Register handlers once and reuse them; repeated registration still creates additional manager-owned handlers, but replies are matched to the registration used by the request.

## Buffer safety and ownership

Use `serialise(buffer, capacity)` and `fromBuffer(buffer, length)` when calling BER objects directly. For built-in BER values, `serialise(nullptr)` measures the encoded size (custom subclasses must support measurement to use it); insufficient capacity returns a negative result. Decoding returns false for malformed or incomplete input. The legacy forms without sizes remain available and require sufficient storage or complete input. The original pointer-only BER virtual signatures remain supported. Custom subclasses can additionally override the bounded overloads; without that override bounded calls fail safely. Built-in BER classes support both forms.

String and OID handlers accept a destination capacity including the terminator:

```cpp
char text[64] = {};
char *textPointer = text;
snmp.addStringHandler(deviceIP, oid, &textPointer, sizeof(text));
char oidValue[128] = {};
snmp.addOIDHandler(deviceIP, oid, oidValue, sizeof(oidValue));
```

For binary OCTET STRING or Opaque values, use `addOctetHandler` or `addOpaqueHandler` with a byte buffer, its capacity, and a `size_t*` receiving the actual length. These handlers preserve embedded NULs. Insufficient capacity leaves the destination and length unchanged; C-string handlers also reject embedded NULs. Legacy string/OID calls without capacities remain caller-sized APIs.

Managers own registrations returned by the `add*Handler` factories; requests retain references to registrations they use. The original `addHandler` borrows a caller-created callback (including its OID text); the caller must keep it alive. `tryAddHandler` instead transfers callback ownership on success. Clearing or destroying a request releases those references. Registrations and their OID strings are freed when the last owner releases them. Do not delete a registered callback directly. Destination buffers, community strings, and UDP objects remain caller-owned and must outlive operations that use them. Manager and request copies own independent lists while retaining shared registrations and caller-owned destinations. Built-in BER trees and parsed responses are deep-copied. Custom BER types can implement `clone()` to support copying within an owning tree.

## Usage

### Transport and requests

Create a manager and request using the agent's community. Pass the same initialized
`WiFiUDP` or `EthernetUDP` object to both with `setUDP(&udp)` after bringing up the
network. `SNMPManager::setUDP()` attempts to bind the socket; `begin()` can be called
explicitly to check binding success, as in the examples.

The manager binds local UDP port **162**. `SNMPGet` sends to the agent's UDP port
**161** by default; `setPort(short)` changes that destination port. Replies return
to the shared socket. This library does not implement a trap receiver despite
using local port 162.

```cpp
SNMPManager snmpManager("public"); // Replace with your agent's read community.
SNMPGet snmpRequest("public", 1);  // 0 = SNMPv1, 1 = SNMPv2c.
```

`sendTo(target)` selects the destination and returns whether the packet was sent
successfully, not whether a response has arrived. `setIP()` remains available for
source compatibility; it does not configure the board's network address and is
not needed when using `sendTo(target)`.

### Register once and reuse the request

Handlers associate an agent address and OID with caller-owned destination storage.
They do not invoke a user function when a response arrives. Keep destinations
alive for as long as their handlers are used, usually by declaring them globally.

For example, with `snmpManager` and `snmpRequest` already configured with UDP:

```cpp
// Global storage:
char sysName[64] = {};
char *sysNamePointer = sysName;
ValueCallback *callbackSysName = nullptr;

// Call once from setup() after configuring the network and UDP transport.
bool registerSysName(IPAddress target)
{
    callbackSysName = snmpManager.addStringHandler(
        target, ".1.3.6.1.2.1.1.5.0", &sysNamePointer, sizeof(sysName));
    return callbackSysName && snmpRequest.tryAddOIDPointer(callbackSysName);
}
```

Stop polling if registration fails. Add other handlers to the same request with
`tryAddOIDPointer()` and check its result. Keep the OID list for subsequent polls;
there is no need to clear and rebuild it every time.

### Wait for fresh responses

Call `snmpManager.loop()` frequently to process incoming packets. Before sending,
save each requested callback's `updateCount()` and choose a request ID distinct
from any still outstanding. Check `sendTo()` for failure. Process a complete sample
only after every requested callback's count has changed.

The count advances even when the value is unchanged. Missing values, error
responses, rejected types, and strings that do not fit do not advance it. Apply a
timeout, clear pending requests when abandoning a sample, and avoid printing or
calculating with stale destinations.

The examples' local [Polling.h](examples/ESP32_ESP8266_SNMP_Manager/Polling.h)
implements these checks, including wrap-safe `millis()` comparisons and request-ID
cycling. See the complete [Wi-Fi sketch](examples/ESP32_ESP8266_SNMP_Manager/ESP32_ESP8266_SNMP_Manager.ino)
for network setup, checked registration, polling, and timeout handling.

Multiple OIDs can share one request, but the agent may return errors or per-binding
exceptions. Both the agent's response limit and the manager's receive buffer must
accommodate the packet. Split large queries into smaller groups when needed.

## Working with SNMP data

### Bandwidth and counter deltas

Use two fresh samples and the agent's TimeTicks interval rather than assuming the
response interval equals your polling timer. TimeTicks are hundredths of a second.
The examples use the tested `Counter32Rate` helper in their local `Polling.h`:

```cpp
// Global state, retained between samples:
Counter32Rate rate;

// After a complete fresh sample:
double utilisation = 0;
if (rate.sample(inOctetsResponse, uptime, ifSpeedResponse, utilisation))
    Serial.println(utilisation, 1);
// Call rate.reset() after a timeout or failed send.
```

The helper establishes an initial baseline, preserves fractional seconds, checks
for zero speed and non-advancing uptime, and converts to `double` before multiplying.
For `uint32_t` counters, unsigned subtraction handles a single rollover correctly.
Repeated equal counter values produce zero utilisation when time has advanced.

Poll frequently enough to avoid multiple Counter32 wraps between samples. For fast
links, consider Counter64 objects such as `ifHCInOctets` when the agent supports
SNMPv2c; the example displays this value but still calculates its rate from Counter32.
Use the same interface index for the counter and interface speed. `ifSpeed` reports
interface capacity, which may differ from your Internet service's bandwidth.

Backwards uptime, including TimeTicks wrap, starts a new baseline. An interface
counter can also reset without a device reboot; the helper cannot distinguish that
from a rollover. For such devices, monitor their counter-discontinuity information
and reset the baseline when it changes.

### Strings and packet capacity

Use bounded text handlers and include space for the terminating NUL. A text value
that is too long or contains an embedded NUL is rejected without updating the
buffer. Use binary handlers when embedded NULs are valid data.

`SNMP_PACKET_LENGTH` defaults to **1500 bytes on ESP32** and **512 bytes otherwise**,
including ESP8266. It limits the complete received SNMP packet, not each string.
Oversized packets are discarded. If needed, define a different value before the
library include, consistently across translation units:

```cpp
#define SNMP_PACKET_LENGTH 1024
#include <Arduino_SNMP_Manager.h>
```

`MAX_OID_LENGTH` separately defaults to 128 bytes of dotted OID text, including
the terminator. Supply numeric OIDs with a leading dot, as in the examples. This
text capacity is not the protocol limit of 128 subidentifiers; some valid SNMP
OIDs will exceed the default text capacity and be rejected.

Increasing these limits consumes more memory: the manager holds a receive buffer,
request serialization uses a stack buffer three times this size, and decoded BER
objects also require heap storage. Account for the target board's UDP limits and
available memory; smaller queries may be preferable to a larger buffer.

## Troubleshooting

### Additional Logging

- Debug logging: add `#define DEBUG` before the library include `#include <Arduino_SNMP_Manager.h>`
- Additional ASN.1 debug logging: add `#define DEBUG_BER` before the library include `#include <Arduino_SNMP_Manager.h>`

### Suppress Errors

`SUPPRESS_ERROR_SHORT_PACKET` is no longer needed: 1.2.0 validates packet structure
instead of rejecting all responses of 30 bytes or fewer. Existing sketches may
leave the define in place; it has no effect.

- Suppress SNMP payload parsing error: add `#define SUPPRESS_ERROR_FAILED_PARSE` before `#include <Arduino_SNMP_Manager.h>`

## Examples

The examples demonstrate common numeric and string GetRequest handlers, including
a Counter64 binding in the Wi-Fi sketch; they do not cover every handler type.
Adapt the OIDs to your device. External SNMP tools can help inspect its MIB and
discover interface indices:

- [iReasoning MIB Browser](https://www.ireasoning.com/mibbrowser.shtml)
- Using [net-snmp](http://www.net-snmp.org/) snmpwalk. A command line tool available for various OS. Basic introductory usage information can be found [in this article](https://www.comparitech.com/net-admin/snmpwalk-examples-windows-linux/)

### Examples folder contents

- [ESP32_ESP8266_SNMP_Manager.ino](examples/ESP32_ESP8266_SNMP_Manager/ESP32_ESP8266_SNMP_Manager.ino) - ESP32/ESP8266 boards
- [ESP_Multiple_SNMP_Device_Polling.ino](examples/ESP_Multiple_SNMP_Device_Polling/ESP_Multiple_SNMP_Device_Polling.ino) - ESP32/ESP8266 boards querying multiple devices and storing results in a device record array
- [Arduino_Ethernet_SNMP_Manager.ino](examples/Arduino_Ethernet_SNMP_Manager/Arduino_Ethernet_SNMP_Manager.ino) - Ethernet-library sketch; CI compiles it on ESP8266 with Ethernet 2.0.2. Adapt module wiring to your board

## Tested Devices

The following boards have previously been used with this library (these are
affiliate links that help support my work). This is historical hardware coverage,
not a claim that every 1.2.0 example was run on each board. Current automated checks
compile for ESP8266, ESP32, ESP32-C3, and Nano ESP32; see the
[embedded test notes](tests/embedded/README.md) for limits and upstream warnings:

- WeMos D1 Mini - ESP8266 - [Amazon UK](https://amzn.to/3z6rQBt) [Amazon US](https://amzn.to/3AY4aBE)
- ESP32S Dev Module - [Amazon UK](https://amzn.to/2TAqWZJ) [Amazon US](https://amzn.to/3PgUZAx)

## Projects using this library

I'd love to hear about projects that find this library useful.

- [Broadband Utilisation Display](https://github.com/shortbloke/Broadband_Usage_Display) - An LED display showing broadband upstream and downstream utilisation.
- [Dekatron-speed](https://github.com/elegantalchemist/dekatron-speed) - Uses a Dekatron (1950s era neon counting tube) spinning based on broadband utilisation rate.
- [Wio Terminal - Router Graph LCD](https://github.com/dbzoo/wio_terminal/tree/master/router_graph_lcd) - Uses the SeeedStudio [Wio Terminal](https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/) to plot traffic received and transmitted rates on the integrated display.
- [Wio Terminal - Router Stats LCD](https://github.com/dbzoo/wio_terminal/tree/master/router_stats_lcd) - Uses the SeeedStudio [Wio Terminal](https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/) to show the current receive and transmit rates on the integrated display.

## Acknowledgements

This project is derived from an [SNMP Agent project](https://github.com/fusionps/Arduino_SNMP). With Manager functionality adapted from work by [Niich's fork](https://github.com/Niich/Arduino_SNMP).

## Maintenance and releases

See [release automation](docs/RELEASING.md) for the 1.x maintenance and 2.x
development branches, release PRs, and validated publication.
