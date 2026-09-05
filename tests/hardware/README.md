# Physical-board interoperability and memory testing

This project is a runnable test sketch, separate from compile-only smoke tests.
**No physical-board run has been recorded yet.** A USB serial adapter is not proof
of its board model. Confirm the target and that replacing its firmware is acceptable
before uploading. A run requires an accessible agent supporting both SNMPv1 and v2c
with the same system-group view.

1. Copy `src/hardware_config.h.example` to `src/hardware_config.h` and fill in the
   Wi-Fi credentials and agent address/community/port. The local file is ignored.
2. Select the matching board environment in `platformio.ini`; the supplied profiles
   are NodeMCU ESP8266 and ESP32 DevKit. Do not guess from the USB serial adapter.
3. Compile with `pio run -d tests/hardware -e esp8266` (or `esp32`). With configuration
   omitted, the sketch prints a configuration message and does not connect.
4. Once firmware replacement is authorized, upload using
   `pio run -d tests/hardware -e esp8266 -t upload --upload-port PORT`.
5. Capture the full serial log at 115200 baud, including startup and `DONE`, using
   `pio device monitor --port PORT --baud 115200`. Some boards reset when monitoring
   opens. Record the exact commit, board/chip, Arduino core, agent software/firmware,
   SNMP view, signal strength, and any packet-capture reference with the result.

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
