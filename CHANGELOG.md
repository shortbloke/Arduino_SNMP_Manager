# CHANGELOG for SNMP Manager For ESP8266/ESP32/Arduino

## 2.0.0

- Separate owned query payload capacity from OID capacity, retain shared payloads across polls, and increase default dotted OID storage to accommodate longer compound indices.
- Add checked common MIB value conversions and regression fixtures for long interface descriptions, IPv6 TCP indices, payload ownership, and allocation failures.

- Add a device/query API with checked IPv4 strings, owned results, automatic request IDs, bounded scheduling, batching, retries, and completion status.
- Add SNMPv1/v2c walks, selected-column tables, and interface traffic reads with Counter64-to-Counter32 fallback.
- Add single-packet SET requests, v1/v2c trap reception, and v2c INFORM acknowledgement while retaining the existing handler API.
- Add compiling single-device, multiple-device, and interface-table examples and query regression coverage.

- Split declarations and implementations into independently includable headers and compiled source files.
- Added bounded BER parsing/serialization and bounded text, binary OCTET STRING, Opaque, and OID callbacks.
- Use fixed-width callback types for SNMP Integer32, Counter32, Gauge32, TimeTicks, and Counter64 values.
- Select request versions with `SNMPVersion::Version1` or `SNMPVersion::Version2c` and use signed 32-bit request IDs consistently.
- Correlate responses with outstanding request IDs, peers, and transports, with explicit cancellation for timed-out requests.
- Correct BER integer, Counter64, OID, sequence-length, response validation, exception, and ownership behavior.
- Remove the ineffective `SNMPGet::setIP()` API and require capacities for string and OID callback destinations.
- Stop defining the transport-owned `UDP_TX_PACKET_MAX_SIZE` macro.
- Add native, sanitizer, leak, configuration, example, and ESP-family compile checks.

See [MIGRATION.md](MIGRATION.md) for required application changes.

## 1.1.13
- Fix crash when using OIDs with 10 digits. Contributor: [AlphaArslan](https://github.com/AlphaArslan)

## 1.1.12

- Added flag to suppress short packet errors. Add `#define SUPPRESS_ERROR_SHORT_PACKET` before `#include <Arduino_SNMP_Manager.h>`
- Added flag to suppress failed to parse errors. Add `#define SUPPRESS_ERROR_FAILED_PARSE` before `#include <Arduino_SNMP_Manager.h>`
- Corrected some spelling mistakes.

## 1.1.11

- Fixed implementation of encoding integers to use the minimum number of bytes necessary. Previously was always used 4 bytes. This Fixes #25.

## 1.1.10

- Fixed spelling error `Guage` now corrected all references to `Gauge`. This maybe a breaking change if for example you are were using `addGuageHandler` or referencing the type `GUAGE32`, which now should be updated to `addGaugeHandler` and `GAUGE32`.

## 1.1.9

- Added a new example file for ESP MCU to show polling of multiple devices and storing results in a device record array. #20
  
## 1.1.8

- Fixed #19 timeticks should be of type unsigned integer. This change impacts `SNMPManager::addTimestampHandler`.

## 1.1.7

- Fixes #18 support OID that use large integers, up to 4 bytes.

## 1.1.6

- Allow non standard port to be used when making SNMP requests. Default UDP port 161 can be overridden using `setPort(<port number>)`.

## 1.1.5

- Support longer OIDs. Change in v1.1.1 was incomplete

## 1.1.4

- Fixes #12 where additional check for packet length was incorrect and unnecessary

## 1.1.3

Focus: Increase robustness

- Better handling devices sending invalid packets in response to requests
- Better handling for receiving responses with OID that weren't requested
- Added DEBUG log messages to aid future troubleshooting. Just add extra defines `#define DEBUG` and/or `#define DEBUG_BER`
- Added support for using test data in `SNMPManager::receivePacket` to better support users experiencing issues

## 1.1.2

- Reduce max size of SNMP message on ESP8266 to address [reported issue](https://github.com/shortbloke/Broadband_Usage_Display/issues/4_) which triggered exception: `Exception 9: LoadStoreAlignmentCause: Load or store to an unaligned address`

## 1.1.1

- Improve OctetString handling for long strings
- Handle OID > 50 characters

## 1.1.0

- Initial library release
