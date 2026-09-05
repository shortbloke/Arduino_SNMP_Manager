# Physical-board interoperability and memory testing

This project is a runnable test sketch, separate from compile-only smoke tests.
**A D1 Mini run is recorded in [D1_MINI_RESULT.md](D1_MINI_RESULT.md).** A USB serial adapter is not proof
of its board model. Confirm the target and that replacing its firmware is acceptable
before uploading. A run requires an accessible agent supporting both SNMPv1 and v2c
with the same system-group view.

1. Copy `src/hardware_config.h.example` to `src/hardware_config.h` and fill in the
   Wi-Fi credentials and agent address/community/port. The local file is ignored.
2. Select the matching board environment in `platformio.ini`; the supplied profiles
   are NodeMCU ESP8266, D1 Mini (`d1_mini`), and ESP32 DevKit. Do not guess from the USB serial adapter.
3. Compile with `pio run -d tests/hardware -e esp8266` (or `esp32`). With configuration
   omitted, the sketch prints a configuration message and does not connect.
4. Once firmware replacement is authorized, upload using
   `pio run -d tests/hardware -e esp8266 -t upload --upload-port PORT`.
5. Capture the full serial log at 115200 baud, including startup and `DONE`, using
   `pio device monitor --port PORT --baud 115200`. Some boards reset when monitoring
   opens. Record the exact commit, board/chip, Arduino core, agent software/firmware,
   SNMP view, signal strength, and any packet-capture reference with the result.

Validate the captured log with `python3 tests/hardware/check_log.py path/to/run.log`.
The validator requires every stage, compares exact OID sequences independently of
the firmware hashes, and rejects reported failures or incomplete runs. Its rejection
cases run in CI through `python3 tests/hardware/test_check_log.py`.

The sketch performs repeated typed GETs in both versions and compares streamed
GETNEXT/GETBULK system-subtree instance counts and hashes. It logs every OID so an
independent exact comparison can supplement the hash. Changing agent tables/views
can legitimately change the set during the test. It checks sysObjectID consistency;
changing uptime values are not compared for equality. A `DONE` record with zero
failures is required, and every stage must succeed. A watchdog reset, early `FAIL`,
or missing completion is a failed/incomplete run.

`MEM` records include free heap and largest allocatable block both after operations
and inside walk callbacks, while the decoder and binding are alive. They report
**sampled** minima, not exact allocator high-water marks. Compare warmed-up cycles
for downward trends in both values, repeat with realistic payload sizes, and run
longer soaks under Wi-Fi loss/recovery. Wi-Fi itself allocates memory; one lower
sample is not sufficient to diagnose a library leak. Free heap alone does not
establish that a sufficiently large contiguous block exists.

This read-only sketch does not certify SET, notification reception, all value types,
all MIBs, or agent brands. Use Set_Location against a designated writable test object
and Receive_Notifications with a real agent sender for those paths; record SET
read-back, v1/v2c traps, INFORM acknowledgement and sender retries explicitly. Never
infer hardware interoperability from the host test or a successful compilation.

## Memory budgets

Keep clients/operations with substantial fixed storage global, as in the examples.
The client shares one packet buffer. Numeric values have no payload allocation;
strings, OIDs, and binary values retain bounded, exact-length shared payloads.
Incoming BER decoding still uses temporary allocations. Accepted walk/table restarts
now release invalidated payloads, including rows a shorter subsequent poll omits.
Copies deliberately retained by an application keep their payloads alive.

Tables accept an optional index capacity including the terminator:
`SNMPTableRead<Rows, Columns, IndexCapacity>` and `SNMPInterfaceRead<Rows, IndexCapacity>`.
The default remains `MAX_OID_LENGTH`. Use 16 for simple interface/storage indices,
or 24 for the printer example's two-part index. Arbitrary MIB tables may need larger
capacities. Too-long indices return `CapacityExceeded`, never truncated rows.
Full request/response OIDs retain the independent library-wide limit.

Measured static RAM for the interface example's 48 rows, with pinned build profiles:

| Target | Before compact indices | With 16-byte indices | Reduction |
| --- | ---: | ---: | ---: |
| ESP8266 | 50,116 bytes | 38,596 bytes | 11,520 bytes |
| ESP32 | 69,264 bytes | 57,744 bytes | 11,520 bytes |

These are linker figures, excluding runtime Wi-Fi, decoder, and retained-payload heap
use. They do not establish peak RAM or certify a workload. Row count, payload limit,
packet size, pending operations, and application-held snapshots also affect memory.
No automatic PSRAM placement is used.

## Notification bursts

The `d1_mini_burst` environment selects `src/burst.cpp`, leaving the read/walk
sketch selected for existing environments. It exercises the unchanged client with
real WiFiUDP reception. After preserving the board's firmware, upload this profile
using the same upload procedure above. It uses the ignored Wi-Fi configuration,
but listens on UDP 1162 with a dedicated `burst-test` community; it does not query
or modify the configured agent.

Run the host sender on the same reachable network (Python with `pyserial`):

```sh
python3 tests/hardware/burst_test.py --serial /dev/cu.usbserial-10 --output tests/hardware/burst.log
python3 tests/hardware/burst_test.py --serial /dev/cu.usbserial-10 --small --output tests/hardware/burst-small.log
python3 tests/hardware/test_burst_test.py
```

The host independently encodes SNMPv2c traps and INFORMs with sequence IDs and
32/256-byte payloads. The matrix varies packet count, pacing, and simulated work
between client loop calls (0/10/50 ms). The optional `--small` run instead sends
short back-to-back bursts with 32-byte payloads and no simulated loop delay. The firmware counts unique notifications,
validates decoded payloads, and samples heap while decoding. The host compares
complete INFORM response BER values, including request IDs, against the sent
notifications. Every burst is followed by a paced recovery probe. Burst INFORMs are sent
once, without retries, so raw overload loss remains visible. Recovery probes allow
up to three paced retries for unacknowledged IDs, recording initial receipt/ACK
counts and retry traffic separately; an isolated UDP loss does not prove a stuck client. Actual send duration
is logged because host scheduling makes requested pacing approximate.

Missing traps/acknowledgements under overload are measurements, not automatic test
failures. Invalid decoded data, unexpected nonempty replies, missing serial replies,
or incomplete recovery reception/acknowledgement after retries stop the run. A final `done` record is required for a
complete matrix. Inspect acknowledgement counts as well as firmware receipt counts;
one does not imply the other. Unexpected empty datagrams are recorded separately,
not accepted as INFORM acknowledgements. The test discovered these on ESP8266 with
the current library's receive-side `flush()` calls.

This isolates notification reception; it does not run the display, simultaneous
queries, v1 traps, or an AsyncUDP comparison. Simulated delays yield to Wi-Fi and do
not model an application that disables interrupts or starves the network stack.
Packet loss can occur in the sender, network, or device; this test does not attribute
every missing packet to the SNMP library. Restore and verify the original firmware
when finished. See [D1_MINI_BURST_RESULT.md](D1_MINI_BURST_RESULT.md) for measured results.
