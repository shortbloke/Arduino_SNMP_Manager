# Example compilation

Run `pio run -d tests/examples`. Wrappers include the maintained 1.x sketches:
the Wi-Fi examples on ESP8266/ESP32 and the Ethernet example on ESP8266 with the
Ethernet library. Compile success does not certify network or application behavior.

Configure credentials, agent addresses, and interface OIDs before uploading.
Keep each sketch's `Polling.h` alongside its `.ino` when copying an example.
Handlers are registered once and reused. A sample is printed only after every
requested value has updated; missing or rejected values time out instead of
reusing stale data. Remove optional OIDs that your agent does not support.

Bandwidth examples use Counter32 and agent TimeTicks, with a new baseline after
a timeout or backwards uptime. Poll often enough to avoid multiple counter wraps;
fast links may require Counter64. Native tests exercise the actual local helpers
for freshness, incomplete responses, timeouts, timer wrap, and rate boundaries.
