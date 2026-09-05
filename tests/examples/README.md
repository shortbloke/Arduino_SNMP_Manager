# Example compile checks

These environments compile the distributed examples directly, so API changes or platform-dependent destination types fail CI. No sketches are uploaded and no network connections are attempted.

From the repository root, run:

```sh
pio run -d tests/examples
```

The Wi-Fi examples compile for ESP8266 and ESP32. The Ethernet example compiles with the ESP8266 Arduino core's bundled Ethernet library; it remains a compile check rather than a claim of hardware compatibility.
