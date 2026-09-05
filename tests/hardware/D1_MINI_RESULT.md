# D1 Mini read/walk interoperability run

> **PUBLIC** — Tracked in Git and shared in the repository.

Contributor reference: this document covers library validation, not application
setup. To read data from your device, start with [getting started](../../docs/GETTING_STARTED.md).

This is a historical result, not a hardware test of the current checkout.

Run on 2026-09-05 with the library at `987e6e9` and the hardware harness metadata
logging included in this change. The attached display's fixed-speed firmware was
backed up before the test. Restoration verification is recorded below.

| Configuration | Observed value |
| --- | --- |
| Board / chip | User-identified D1 Mini; detected ESP8266EX |
| Flash / CPU | 4 MB / 80 MHz |
| Build profile | `tests/hardware`, `d1_mini`, espressif8266 4.2.1 |
| Arduino core | 3.1.2 |
| SDK | 2.2.2-dev(38a443e) |
| Wi-Fi RSSI at start | -58 dBm |
| Agent | Router configured in the user's Broadband_Usage_Display project |
| Agent sysObjectID | `.1.3.6.1.4.1.12325.1.1.2.1.1` |
| Agent software version | Not independently identified |
| Transport | Board Wi-Fi and real WiFiUDP, SNMPv1/v2c |

All 50 cycles completed, with 200 successful operation stages and zero reported
failures: typed GET in each version, a v1 GETNEXT walk, and a v2c GETBULK walk.
The serial-log validator also compared the complete OID sequences in each cycle,
independently of the firmware's hashes, and confirmed numeric ordering and matching
instance sets. The host independently confirmed that the same agent answered both
versions before flashing. No SET was sent to the router.

| Memory measurement | Bytes |
| --- | ---: |
| Minimum sampled free heap, including response callbacks | 36,800 |
| Minimum sampled largest allocatable block | 36,784 |
| First end-of-cycle free heap | 44,864 |
| Last end-of-cycle free heap | 44,360 |
| End-of-cycle heap range | 44,248–44,864 |
| End-of-cycle largest-block range | 41,672–42,352 |

Free heap dropped by 504 bytes at cycle 6, then remained at 44,360 bytes except for
a 112-byte transient at cycle 34. There was no continuing downward trend over the
remaining cycles. This observation does not attribute allocations to the library
or Wi-Fi stack, prove absence of leaks, or establish an exact peak. A longer soak,
network loss/recovery, larger retained tables, and deliberately exhausted physical
heap remain separate tests. Native allocation-failure sweeps cover selected library
failure paths; this hardware run did not exhaust heap.

The local raw serial log is `tests/hardware/d1_mini.log` (ignored by Git). SHA-256:
`dcde76144fccbd4f7e745b85b25935c81092d409918eb60efdfa55a118e19448`.
Run `python3 tests/hardware/check_log.py tests/hardware/d1_mini.log` to verify it.
Credentials remain in the ignored local hardware configuration and are not included
in this report. No packet capture was taken.

## Original firmware restoration

The entire 4 MB flash was read and verified against the board before testing.
The initial 460800-baud read failed; the successful backup used 115200 baud.
Backup SHA-256:
`3b8e381274764b2b9964487b90fc8001615c0c4122c241fc9ec1dd4bd302318f`.
The backup is retained outside Git at
`~/Library/Caches/snmp-d1-mini-backup/original.bin` in a private directory.

Restoration completed successfully: the full image was written, `verify-flash`
reported a matching digest for all 4 MB, and the board was reset to the original
display firmware. No display-project source files were modified.

This result establishes successful read/walk interoperability for this board and
agent configuration. It does not certify other ESP boards, SET, traps/INFORMs,
all value types, display operation with the new API, or full RFC conformance.
