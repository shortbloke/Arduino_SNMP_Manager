# CHANGELOG for SNMP Manager For ESP8266/ESP32/Arduino

## 1.2.0

A robustness and test-coverage release that retains the header-only 1.x API and existing sketch interfaces. It improves packet validation, BER encoding/decoding, callback memory handling, and request tracking, with optional bounded APIs for safer buffer use.

- Fix BER length boundaries, Integer32 and Counter64 encoding/decoding, binary strings, and full-range OID subidentifiers.
- Validate packet boundaries, response versions/communities/structure, and per-binding exceptions; reject incomplete UDP reads and avoid receive-side flush transmissions.
- Correct string termination and fractional float callbacks, manage registration ownership, and handle allocation failures without leaking partial trees.
- Correlate replies with successful sends while retaining rolling request tracking for existing polling loops; strict capacity enforcement is opt-in.
- Retain the header-only 1.x API, numeric versions, short request fields, setIP(), capacity-free text calls, original BER virtual methods, and safe copying; add bounded/checked alternatives and portable numeric destination overloads.
- Fix OID callback registration and returned OID handling (#32).
- Add native regression/sanitizer tests (#13), source-compatibility checks, and ESP8266, ESP32, ESP32-C3, and Nano ESP32 compilation checks, including the unchanged examples.

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
