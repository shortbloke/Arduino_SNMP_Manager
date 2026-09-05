# Example compile checks

Contributor reference: this document covers library validation, not application
setup. To read data from your device, start with [getting started](../../docs/GETTING_STARTED.md).

These environments compile the distributed examples directly, so API changes or platform-dependent destination types fail CI. No sketches are uploaded and no network connections are attempted.

From the repository root, run:

```sh
pio run -d tests/examples
```

The Wi-Fi examples compile for ESP8266 and ESP32. The Ethernet example compiles on the ESP8266 core with the pinned Arduino Ethernet 2.0.2 library; it remains a compile check rather than a claim of hardware compatibility.

The default builds compile every distributed Wi-Fi sketch on ESP8266 and ESP32,
including reads, walks, device tables, SET/read-back, and notification reception.
CI divides these environments into `legacy`, `query_esp8266`, and `query_esp32`
groups through `scripts/build_examples.py`.
