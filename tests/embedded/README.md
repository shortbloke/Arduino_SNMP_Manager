# ESP core compatibility

Run `pio run -d tests/embedded` to compile the header-only 1.x API against the
pinned ESP8266, ESP32, ESP32-C3, and Nano ESP32 cores. Multiple translation units,
DEBUG logging, legacy calls, and additive bounded methods are included.
No credentials are configured and these checks do not upload or certify hardware.
