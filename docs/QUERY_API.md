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

All three compile on ESP8266 and ESP32. Configure Wi-Fi credentials, device
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
use four bytes. Text/binary result capacity is currently `MAX_OID_LENGTH - 1`;
larger values report `CapacityExceeded` rather than truncating.

An operation can finish successfully, partially, or with a failure such as
`Missing`, `TypeMismatch`, `Timeout`, `TransportError`, `ProtocolError`, or
`CapacityExceeded`. Use `status().message()` for a short description and
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
response or exhausted GETBULK retries falls back to GETNEXT. They stop at the subtree boundary/end-of-view,
reject nonadvancing OIDs, and have a 60-second deadline per walk. A table walks
its columns sequentially; the deadline applies separately to each column.

```cpp
SNMPTableRead<48, 2> traffic(router);
traffic.addColumn(".1.3.6.1.2.1.2.2.1.10", COUNTER32);
traffic.addColumn(".1.3.6.1.2.1.2.2.1.16", COUNTER32);
```

Check each `addColumn()` result before starting. Table rows are joined by the full
index suffix, including composite indices; they retain discovery order. A missing
cell stays `Missing`. Row capacity applies to the union of discovered indices;
excess rows produce `CapacityExceeded`, retaining collected rows. Tables progress
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

Result storage is fixed, and outgoing encoding does not allocate a BER tree.
Incoming packets still use the existing bounded BER decoder and temporary heap
allocations. The library is not allocation-free. Allocation failures cannot update
results; the operation eventually retries or times out. Large collections should
be global or deliberately allocated, not placed on a small task stack. PSRAM is
not automatically selected by this API.

All manager-side SNMPv1/v2c operations are available on both targets. Extra RAM can
increase capacities; it does not remove agent packet limits. Packet size, table
size, and pending capacity can be reduced independently. No board-exclusive
protocol features or compile-time feature switches are introduced in this version.

The existing bounded handler and `SNMPGet` APIs remain available and covered by
regression tests. Existing 1.x migration requirements still apply. New examples
use the query API; direct manipulation of legacy internals is not required.
