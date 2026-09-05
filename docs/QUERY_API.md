# Device and query API (SNMPv1 / SNMPv2c)

Include `SNMPClient.h` for reads, writes, walks, and notifications, or `SNMPTable.h`
for selected-column tables and interface traffic. This API is implemented alongside
the existing `SNMPManager`/`SNMPGet` API. SNMPv3, DNS, runtime MIB parsing, and sending
traps/informs are outside this implementation.

## Start with an example

- [One reading](../examples/Simple_Read/Simple_Read.ino): device uptime.
- [Multiple devices](../examples/Multiple_Devices/Multiple_Devices.ino): independent
  reads over one client.
- [Interface traffic](../examples/Interface_Traffic/Interface_Traffic.ino): discover
  interface indices and read names plus incoming/outgoing counters.
- [Typed walk](../examples/Walk_Values/Walk_Values.ino): GETNEXT/GETBULK and value primitives.
- [Host storage](../examples/Host_Storage/Host_Storage.ino): server/NAS storage sizes.
- [Printer supplies](../examples/Printer_Supplies/Printer_Supplies.ino): compound indices and supply states.
- [Write and read back](../examples/Set_Location/Set_Location.ino): explicit SET with timeout handling.
- [Notifications](../examples/Receive_Notifications/Receive_Notifications.ino): v1/v2c traps and v2c INFORM acceptance.

See the [example guide](../examples/README.md) for operation and value-type coverage.
These query API examples compile on ESP8266 and ESP32. Configure Wi-Fi credentials, device
addresses, and communities before uploading. Network setup belongs to the sketch.

```cpp
WiFiUDP udp;
SNMPClient client(udp);
SNMPDevice router(client, "192.168.1.1", "public", SNMPVersion::Version2c);
SNMPRead<SystemUptime> uptime(router);
```

Call `client.begin()` after connecting the network. Check its returned status.
Call `uptime.start()` to schedule a read and service `client.loop()` regularly.
`start()` does not send synchronously. On `takeCompleted()`, check
`uptime.result().ok()` before reading `uptime.result().value.unsigned32()`.
TimeTicks are hundredths of a second. `takeCompleted()` consumes the event, not
its result. A rejected start preserves the previous results; an accepted start
invalidates them until new responses arrive.

`SNMPDevice` accepts either `IPAddress` or four decimal IPv4 octets in a string.
It copies the address and community; invalid configuration is reported by
`device.status()` and `start()`. Communities have room for 63 bytes plus the
terminator. Hostnames, IPv6, trailing text, and octets greater than 255 are rejected.
Set `device.port`, `timeoutMs`, and `retries` before starting work. Do not modify
these settings while that device has pending operations.

## Arbitrary OIDs and ranges

```cpp
SNMPQuery<48> counters(router);
auto status = counters.addRange(".1.3.6.1.2.1.2.2.1.10", 1, 48, COUNTER32);
if (status.ok())
    status = counters.start();
```

`addOID(oid, expectedType)` adds one instance; omit the type to accept any supported
value. `addRange(column, first, count, expectedType)` appends decimal indices.
Check every setup result. Duplicate or invalid OIDs are rejected and a failed
range addition rolls back the entire addition. Capacity is a template argument.

Reads are automatically batched. A `tooBig` response reduces a batch. Exhausted
read retries also reduce multi-value batches, allowing recovery when oversized
responses were dropped. Single-value timeouts remain explicit. SNMPv1
`noSuchName` on a batch causes individual reads so missing values do not hide
successful ones. Every result retains its OID, actual type, value, and status.
Access results by index only within `size()`.

## Values and errors

`SNMPValue` exposes signed `integer()`, `unsigned32()`, and `unsigned64()` accessors.
Check status and type before conversion. OCTET STRING and Opaque values use
`bytes` and `length`, preserving embedded zeros. For display, check `isText()`
before using `text()`. OID values use canonical dotted text; network addresses
use four bytes. `SNMP_VALUE_MAX_LENGTH` bounds each owned payload (default 1024
bytes, excluding its extra terminator). `MAX_OID_LENGTH` separately bounds dotted
OID storage (default 256 bytes, including the terminator). Larger values report
`CapacityExceeded` rather than truncating. Packet and decoder limits also apply.

Payload bytes are read-only. Use `setBytes()` to create or replace a value and check
its returned status. Copying an `SNMPValue` shares ownership without allocating;
a copy remains valid after the original result is replaced or destroyed. A raw
`bytes` or `text()` pointer is valid only while an owning value retains that payload.
Do not overwrite `bytes` or `length` directly. Ownership is not thread-safe; use
values and the client from one task or provide external synchronization.

An operation can finish successfully, partially, or with a failure such as
`Missing`, `TypeMismatch`, `Timeout`, `TransportError`, `ProtocolError`, or
`CapacityExceeded` or `AllocationFailure`. Use `status().message()` for a short description and
`status().code()` for program logic. `agentError()`/`agentErrorIndex()` on a query
or walk expose the most recently matched response's SNMP error fields.
Already successful cells remain available if a later batch fails.

`onComplete(handler, context)` is an optional operation-level notification. It runs
from `loop()` or explicit `cancel()`, after the operation leaves the pending pool.
A handler may schedule another operation but must not destroy the active operation
or reenter `loop()`. Destruction of an operation unregisters it silently.

## Walks and tables

`SNMPWalk<N>` collects up to N results beneath a configured root. Call
`configure(root)`, then `start()`. For larger trees, `stream(handler, context)`
delivers borrowed `SNMPResult` objects as they arrive, without collecting them.
Copy anything that must outlive the call. Returning false stops with
`CapacityExceeded`. A handler may cancel the walk; it must not destroy it.

Walks use GETNEXT for v1 and GETBULK with four repetitions for v2c. A v2c `tooBig`
response, an empty successful bulk response, or exhausted GETBULK retries falls back to GETNEXT. They stop at the subtree boundary/end-of-view,
reject nonadvancing OIDs, and have a 60-second deadline per walk. A table walks
its columns sequentially; the deadline applies separately to each column.

```cpp
SNMPTableRead<48, 2> traffic(router);
traffic.addColumn(".1.3.6.1.2.1.2.2.1.10", COUNTER32);
traffic.addColumn(".1.3.6.1.2.1.2.2.1.16", COUNTER32);
```

For compact known indices, use `SNMPTableRead<48, 2, 16>`: the third parameter
bounds the row's index text including its terminator. It defaults to `MAX_OID_LENGTH`.
`SNMPInterfaceRead<48, 16>` exposes the same option. Oversized indices return
`CapacityExceeded`, without truncation or accidental row merging. Full OID buffers
remain independently bounded by `MAX_OID_LENGTH`.

Check each `addColumn()` result before starting. Table rows are joined by the full
index suffix, including composite indices; they retain discovery order. A missing
cell stays `Missing`. Row capacity applies to the union of discovered indices;
excess rows produce `CapacityExceeded`, retaining collected rows. Accepted restarts
release previous row payloads, including slots absent from the new result set. Tables progress
through `client.loop()` without a second service method.

`SNMPInterfaceRead<48>` selects interface descriptions and traffic counters. It
prefers high-capacity counters and fills unavailable cells from Counter32 columns.
Pass `false` as the constructor's second argument to request Counter32 directly
(particularly for v1 agents). Columns 0, 1, and 2 are description, incoming octets,
and outgoing octets. The actual counter type identifies its width. Indices need
not be contiguous or correspond to physical port numbers. Results are cumulative
byte counts; rate calculation and discontinuity detection remain application work.

## Writes

```cpp
SNMPSet<1> write(router);
SNMPValue location;
auto status = location.setBytes(reinterpret_cast<const unsigned char *>("Lab"), 3);
if (status.ok())
    status = write.addValue(".1.3.6.1.2.1.1.6.0", location); // sysLocation.0
// Device configuration must permit writes to this object.
```

Only call `start()` when the application intends to perform the write. Other
values can be constructed with a type, numeric value, or checked `setBytes()`.
A SET must fit in one packet: the library never splits a write transaction.
SET requests are not automatically retried. A timeout means the outcome is unknown;
read back the value before deciding whether to repeat a write. Agent errors are
available through the operation's status and error fields.

## Notifications

Call `client.begin(162)` to listen on the usual notification port, then register
`client.notifications(community, handler, context)`. This client can also send
queries; do not have another consumer reading its UDP instance.

The handler receives a borrowed `SNMPNotification` view. `read(index, result)`
copies a binding into bounded storage. v1 traps also expose enterprise, agent
address, generic/specific trap numbers, and uptime. v2c notifications require the
standard uptime and trap-OID bindings. Unknown communities and malformed packets
are ignored.

Return true after accepting a notification. v2c INFORMs are acknowledged with a
RESPONSE only after acceptance; traps never receive an acknowledgement. Returning
false leaves an INFORM unacknowledged so the sender can retry. Delivery is not
exactly-once: applications needing deduplication must track sender/request identity.
Handlers run synchronously from `loop()` and should remain short. There is no
unbounded notification queue. An acknowledgement send failure is left for the
sender's retry mechanism to recover.

## Resource and compatibility contract

The UDP object must outlive the client, the client its devices, and devices their
operations. Owners are noncopyable. Use stable instances as in the examples.
Do not service the same UDP instance through both the old manager and new client.

`SNMP_PACKET_LENGTH` controls the shared packet buffer and
`SNMP_MAX_PENDING_REQUESTS` bounds the client's simultaneous operations. Existing
ESP8266/ESP32 defaults remain; query and table template capacities determine retained
storage. Firmware and library configuration must match as described in the
[migration guide](../MIGRATION.md).

Result slots and OID buffers have fixed capacities. Numeric values need no payload
allocation; text, binary, and OID values allocate their actual length plus ownership
metadata and a terminator. This keeps numeric tables compact without reserving the
maximum string capacity in every cell. Increasing OID capacity still increases
per-row storage. Outgoing encoding does not allocate a BER tree, but validating an
OID can allocate. Incoming packets use the existing bounded BER decoder and
temporary heap allocations. The library is not allocation-free. Failure to retain
a decoded value reports `AllocationFailure`; decoder allocation failures can instead
lead to a retry or timeout. Large collections should be global or deliberately
allocated, not placed on a small task stack. PSRAM is not automatically selected.

All manager-side SNMPv1/v2c operations are available on both targets. Extra RAM can
increase capacities; it does not remove agent packet limits. Packet size, table
size, and pending capacity can be reduced independently. No board-exclusive
protocol features or compile-time feature switches are introduced in this version.

The existing bounded handler and `SNMPGet` APIs remain available and covered by
regression tests. Existing 1.x migration requirements still apply. New examples
use the query API; direct manipulation of legacy internals is not required.

## Common MIB values

Include `<SNMPMIB.h>` for checked conversions. First check the result or table cell
status. These helpers validate wire types and ranges, allocate no storage, and
leave output arguments unchanged on failure:

- `storageBytes(units, blocks, bytes)` multiplies HOST-RESOURCES-MIB allocation
  units and block counts using 64-bit arithmetic.
- `truthValue(value, result)` accepts TruthValue's `true(1)` and `false(2)` only.
- `fixedPoint(value, decimalExponent, precision, result)` converts signed
  ENTITY-SENSOR fixed-point readings and rejects underflow/overflow sentinels.
  Pass the SI exponent (for example, -3 for milli), **not** the scale enumeration.
  Positive precision specifies fractional digits; negative precision describes
  accuracy. Check sensor type, units, and operational status separately.
- `supplyState(level)` preserves Printer-MIB's other, unknown, and some-remaining
  states. `supplyPercent(level, capacity, result)` requires known nonnegative
  levels and positive capacity in the same units.
- `formatMAC(value, buffer, capacity)` formats a six-byte OCTET STRING; allow 18
  bytes including the terminator.
- `formatAddress(addressType, value, buffer, capacity)` formats InetAddress
  IPv4(1) or IPv6(2) values; allow 40 bytes for IPv6. IPv6 output is uncompressed.
  Zone-indexed addresses and DNS names are not accepted; this adds no IPv6 transport.

```cpp
uint64_t bytes;
if (units.status.ok() && blocks.status.ok() &&
    SNMPMIB::storageBytes(units.value, blocks.value, bytes)) {
    // bytes now contains the storage size without 32-bit multiplication overflow.
}
```

These are explicit value helpers, not a MIB loader or automatic table schema.
DateAndTime, BITS, enumerations, and vendor-specific conventions still require
application interpretation. Tables retain the full numeric index suffix, including
sparse and compound indices; the application decides how columns and indices relate.
The default OID capacity accommodates the tested TCP-MIB IPv6 connection indices,
but does not cover every legal SNMP OID. Increase global limits when required.

Definitions: [HOST-RESOURCES-MIB](https://www.rfc-editor.org/rfc/rfc2790.html),
[ENTITY-SENSOR-MIB](https://www.rfc-editor.org/rfc/rfc3433.html),
[Printer-MIB](https://www.rfc-editor.org/rfc/rfc3805.html),
[textual conventions](https://www.rfc-editor.org/rfc/rfc2579.html), and
[INET-ADDRESS-MIB](https://www.rfc-editor.org/rfc/rfc4001.html).

## Low-level handler freshness

The retained `SNMPManager` handler API provides `ValueCallback::updateCount()`.
Compare it with a saved count to detect a successfully written value even when
the value is unchanged. Tracked duplicate replies, errors, and rejected values
do not increment it. The friendly query API uses its result/status methods instead.
