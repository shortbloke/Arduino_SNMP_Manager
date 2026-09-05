# Embedded compatibility builds

Run from the repository root:

```sh
pio run -d tests/embedded
pio run -d tests/embedded -e esp8266
pio run -d tests/embedded -e esp32
pio run -d tests/embedded -e esp32c3
pio run -d tests/embedded -e nano_esp32
```

The pinned platforms compile and link a sketch against the real Arduino and Wi-Fi UDP APIs. Targets are NodeMCU ESP8266, classic ESP32, ESP32-C3, and Arduino Nano ESP32 (ESP32-S3). The sketch uses separate translation units, debug logging, fixed-width values, bounded BER operations, and callback registration. Both debug macros are enabled to catch logging-only compilation failures.

These are compile/link checks, not hardware or network interoperability tests. Do not upload the smoke sketch as an application: it has no Wi-Fi credentials or meaningful polling configuration. CI runs these builds together with the native suite, debug suite, strict C++11 multi-file test, sanitizer checks, and formatting check.

Supported metadata currently names ESP8266 and ESP32. Other modern 32-bit Arduino platforms are candidates for additional build profiles, not implicitly certified targets. AVR-era boards are outside the supported scope.

The project installs the checkout as a local library dependency, compiling its implementation files as well as the smoke sketch. The `esp32_custom` environment repeats the ESP32 build with nondefault packet, octet-string, OID, and pending-request limits to check configuration across the application/library boundary. It is included in the default build and CI matrix.

The smoke sketch also compiles the new client, typed reads, selected-column tables,
SET setup/cancellation, and notification registration against the real UDP API.
