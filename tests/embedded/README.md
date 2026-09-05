# ESP core compatibility

Run `pio run -d tests/embedded` to compile the header-only 1.x API against the
pinned ESP8266, ESP32, ESP32-C3, and Nano ESP32 cores. Multiple translation units,
DEBUG logging, legacy calls, and additive bounded methods are included.
No credentials are configured and these checks do not upload or certify hardware.

## Upstream build warnings

The pinned ESP8266 core's `tools/elf2bin.py` uses non-raw Python regular-expression
strings, which Python 3.12 reports as invalid escape sequences. This comes from
the board package rather than this library; firmware generation still succeeds.

The Nano ESP32 core reports that PlatformIO uses GPIO numbering instead of the
board's Arduino pin mapping. This test does not access pins. Sketches that do must
use the numbering selected by their build configuration. Do not enable
`BOARD_HAS_PIN_REMAP` globally to silence the warning: the core requires separate
flags for core and sketch compilation and rejects unsupported configurations.

These upstream warnings remain visible so changes in board support can be reviewed.
