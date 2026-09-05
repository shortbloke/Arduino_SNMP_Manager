# Setup, memory limits, and error recovery

Unfamiliar terminology? See [terms explained](TERMS.md).

## Before uploading an example

1. Install the 2.x library source and select your ESP8266 or ESP32 board. A sketch
   written for 2.x will not compile against the stable 1.x API.
2. Replace `YOUR_SSID` and `YOUR_PASSWORD` with your Wi-Fi details in your local
   sketch. Keep credentials out of shared repositories.
3. Replace the example's device address with the address of the router, switch,
   NAS, or printer you want to query. Use four decimal numbers, such as
   `192.168.1.10`; hostnames are not supported.
4. Enable SNMP on that device. Match its community and version in `SNMPDevice`.
   `public` is an example, not a universal password. Permit the board's IP to read
   the required objects in the device's SNMP settings.
5. Open Serial Monitor at 115200 baud. A sketch waiting for Wi-Fi cannot query
   SNMP yet. Confirm the board joins the network before diagnosing an SNMP timeout.
6. Begin with `Simple_Read`. Once uptime works, try the device-specific example.
   Set its OIDs to objects the device actually exposes. A scalar OID normally ends
   in `.0`; table instances have device-assigned indices.

The sketches call `client.loop()` to process replies and timeouts. Keep calling it
frequently; long delays or blocking application work delay every pending query.

RAM (Random Access Memory) is the board's working memory. The heap is the part
requested while the program runs; it needs room for Wi-Fi and returned values.

## What does capacity mean?

A **capacity is a limit chosen by your sketch or build**, usually to keep memory
use predictable. It is not the size of a disk or the number of physical ports.
Different limits control different things; increasing one does not increase the others.

For example, the interface sketch declares:

```cpp
constexpr size_t MaxInterfaces = 64;
constexpr size_t InterfaceIndexBytes = 16;
SNMPInterfaceRead<MaxInterfaces, InterfaceIndexBytes> interfaces(networkSwitch);
```

- `64` reserves space for up to 64 interface rows. A row holds the description and
  incoming/outgoing counters for one interface. Virtual interfaces count too: a
  24-port switch can expose more than 48 rows.
- `16` allows up to 15 characters in the row's index plus the terminating zero
  byte required for C++ text. This is not a limit of 16 interfaces or index 16.
  For example, index `1001` needs five bytes including termination.
- These are compile-time choices. Edit the constant, rebuild, and upload again.
  A running operation does not expand its capacity automatically.

A storage table uses `SNMPTableRead<MaxStorageRows, 4, StorageIndexBytes>`.
The middle `4` is the number of selected columns: description, allocation units,
total blocks, and used blocks. Change the row limit to store more entries; changing
`4` does not provide more rows. Memory, mounts, and datasets can each appear as rows.

### If a table reaches its row limit

1. Read the example's error and retained-row count in Serial Monitor.
2. If the count equals `MaxInterfaces`, `MaxStorageRows`, or `MaxSupplyRows`, the
   table may need more rows. For example, change `MaxInterfaces` from 64 to 96
   when the device exposes more than 64 interfaces. Leave the other constants alone.
3. Rebuild and check the compiler's RAM report. Upload and check free heap under
   normal Wi-Fi/polling load, not just at startup. More reserved rows leave less
   memory for networking and returned strings. There is no universally safe maximum.
4. If the board cannot afford the complete table, use `Walk_Values` to stream a
   subtree and process each binding immediately. For storage, set its walk root
   to `.1.3.6.1.2.1.25.2.3.1`. Streaming does not automatically join bindings into rows.

When the retained count is below the row limit, do not assume that more rows will
help: an index, OID, value, or packet might be too large instead. The shared
`CapacityExceeded` code does not identify the exact exhausted limit.

A table that stopped early is incomplete. Later columns might not have been read.
Check each cell's `ok()` before using it; missing counters are not zero counters.

### Other limits and what to change

| Where the limit is reached | What it controls | What to do |
| --- | --- | --- |
| `query.addOID()` / `addRange()` | Number of requested values in `SNMPQuery<N>` | Increase `N` or put fewer OIDs in the query. Check setup results before `start()`. |
| `table.addColumn()` | Number of selected columns | Match the second template argument to the columns you add. |
| Collecting `SNMPWalk<N>` | Number of stored results | Increase `N` or call `stream()` before `start()` to process values without collecting them. |
| `start()` with other operations pending | Simultaneous-operation slots | Wait for an operation to finish, or schedule fewer operations together. Increase `SNMP_MAX_PENDING_REQUESTS` only if needed. |
| A long composite table index | Text bytes in the third table template argument | Increase the named index-byte constant, for example from 16 to 32. Include one byte for termination. |
| A long numeric OID | `MAX_OID_LENGTH`, including termination | Increase this build setting only if the required dotted OID exceeds the configured limit. Invalid or oversized OIDs can also return `InvalidOID`. |
| A long string or binary value | `SNMP_VALUE_MAX_LENGTH` and decoder limit `SNMP_OCTETSTRING_MAX_LENGTH` | Increase the relevant build limits for required values, or query smaller objects. Decoder rejection can appear as a timeout. |
| A request/reply packet | `SNMP_PACKET_LENGTH` | Reads already reduce batches and walks fall back to GETNEXT after failed bulk retries. If even one required value does not fit, use a larger packet limit or a smaller object. |
| `AllocationFailure` | Available heap, including a large enough contiguous block | Reduce rows, concurrent operations, or retained copies of values. Increasing capacities makes this worse. |

A streamed callback returning `false` also ends its walk with `CapacityExceeded`:
return `true` to continue. A SET must fit one packet; the library will not split it.

### Changing a build-wide limit

Template constants above belong in your sketch. The `SNMP_*` and `MAX_OID_LENGTH`
settings in the table must instead reach **both the sketch and library compiler**.
For example, to use a 1500-byte packet buffer in PlatformIO:

```ini
build_flags =
    -DSNMP_PACKET_LENGTH=1500
```

Add this to the project's environment in `platformio.ini`, preserving any existing
flags, then rebuild and upload. Do not put a sketch-only `#define` before an include.
The defaults are 512 packet bytes on ESP8266 and 1500 on ESP32; this example adds
988 buffer bytes on ESP8266. Temporary decoding and retained results need heap too.
See [build configuration](../MIGRATION.md#compile-and-link-the-implementation-files)
for shared headers and Arduino CLI settings.

## What should I do with an error?

Use `status().code()` in application logic and `status().message()` for display.
Check `start()` immediately, then check completion separately. A successful start
means the work was scheduled, not that a response has arrived.

| Status | Next step |
| --- | --- |
| `Pending` | Keep calling `client.loop()`; wait before using the result. |
| `Busy` | That operation is already running. Wait for completion instead of calling `start()` again. |
| `NotStarted` | Connect the network, call `client.begin()`, and check its status. |
| `InvalidAddress` | Correct the device's dotted IPv4 address; do not pass a hostname. |
| `InvalidConfiguration` | Check community length (at most 63 bytes), named version, nonzero port/timeout, and that the operation has OIDs or columns configured. |
| `InvalidOID` | Check numeric dotted syntax, scalar `.0` or table index, duplicates, and OID length. Check the failing setup call before continuing. |
| `TransportError` | Check Wi-Fi/network connection and the UDP socket. Retry a read after connectivity returns. |
| `Timeout` | Verify address, community, version, SNMP access rules, and frequent `loop()` calls. A received but rejected/oversized reply can also cause timeout; it does not prove the device is offline. |
| `Missing` | Check that the device exposes the OID and instance to this community. Skip unavailable table cells. |
| `TypeMismatch` | Match the expected type to the device's MIB; for example, use Counter64 rather than INTEGER for a high-capacity counter. |
| `Partial` | Inspect individual results. Use successful cells and report failed ones; do not treat the entire poll as complete. |
| `ProtocolError` | Inspect `agentError()` and `agentErrorIndex()` on a query or walk. Check OIDs and access permissions; retain a minimal reproducer if valid reads consistently fail. |
| `Unsupported` | Check the SNMP version and requested operation; GETBULK is v2c-only. SNMPv3 is not implemented. |
| `Cancelled` | The operation was stopped. Restart only if the application still needs it. |
| `CapacityExceeded` / `AllocationFailure` | Follow the limit-specific steps above; configured limits and actual free heap are different problems. |

After a SET timeout, the device might already have applied the write. Read back
before deciding whether to repeat it; do not blindly retry writes.

If `Host_Storage` prints `size unavailable`, check cell statuses and allocation
units. A row with zero allocation units has no valid byte-size conversion. Increasing
capacity cannot fix that value; skip that row and continue with valid ones.

## Protocol background

For the rules behind message size limits and read retries, see
[RFC 3416, sections 2.2–2.3](https://www.rfc-editor.org/rfc/rfc3416.html#section-2.2).
The capacities and recovery steps above describe this library's implementation.
The [RFC reference guide](TERMS.md#protocol-references-optional) also links
value definitions and device-specific groups of readings.
