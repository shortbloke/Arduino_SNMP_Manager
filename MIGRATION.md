# Migrating to SNMP Manager 2.0

> **PUBLIC** — Tracked in Git and shared in the repository.

This guide is for existing 1.x sketches. If you are starting a new project, use
[getting started](docs/GETTING_STARTED.md); you do not need to migrate anything.

Version 2.0 makes packet parsing, destination writes, ownership, and request matching explicit. Migration also requires named versions, compiled implementation files and review of ownership and request tracking; see the sections below. The 1.2.x fixes and successful-update counts are retained.

## Select the SNMP version by name

The request constructor now accepts a scoped version rather than a numeric `short`:

```cpp
SNMPGet request("public", SNMPVersion::Version2c);
```

Replace `0` with `SNMPVersion::Version1` and `1` with `SNMPVersion::Version2c`. The explicit names prevent unsupported values and distinguish community-based SNMPv2c from other SNMPv2 variants. Public request IDs and request-correlation fields now consistently use `int32_t`.

## Compile and link the implementation files

The library is no longer header-only. Arduino IDE, Arduino CLI, and PlatformIO compile `src/*.cpp` automatically when the library is installed normally. Custom build systems must compile and link every `.cpp` file in `src/`.

Configuration macros must be applied to the library and application together. Set them as build flags rather than defining them immediately before `#include <Arduino_SNMP_Manager.h>`:

```ini
build_flags =
    -DSNMP_PACKET_LENGTH=1024
    -DSNMP_MAX_PENDING_REQUESTS=8
    -DDEBUG
```

The configurable capacity macros are `SNMP_PACKET_LENGTH`, `SNMP_OCTETSTRING_MAX_LENGTH`, `MAX_OID_LENGTH`, `SNMP_MAX_PENDING_REQUESTS`, and `SNMP_VALUE_MAX_LENGTH`. Logging uses `DEBUG` and `DEBUG_BER`; parse-error logging uses `SUPPRESS_ERROR_FAILED_PARSE`. `SNMP_CONFIG_HEADER` can name a shared configuration header. A link error mentioning `snmp_detail::BuildConfiguration` means the application and library were compiled with different settings.

`SUPPRESS_ERROR_SHORT_PACKET` has been removed because short packets are now accepted or rejected through structural validation.

The library no longer defines `UDP_TX_PACKET_MAX_SIZE`. Configure a transport library's packet size through that transport's documented build settings if required.

## Use fixed-width callback destinations

SNMP integer types have fixed widths. Update callback storage accordingly:

```cpp
int32_t integerValue = 0;
uint32_t counterValue = 0;
uint32_t gaugeValue = 0;
uint32_t uptime = 0;
uint64_t counter64Value = 0;

manager.addIntegerHandler(peer, integerOid, &integerValue);
manager.addCounter32Handler(peer, counterOid, &counterValue);
manager.addGaugeHandler(peer, gaugeOid, &gaugeValue);
manager.addTimestampHandler(peer, uptimeOid, &uptime);
manager.addCounter64Handler(peer, counter64Oid, &counter64Value);
```

Do not substitute `int`, `unsigned int`, `unsigned long`, or `unsigned long long`: their correspondence to these types varies between Arduino cores.

## Supply text destination capacities

String and printable-OID handlers now require a capacity, including room for the terminating null:

```cpp
char name[64];
char *namePointer = name;
char returnedOid[128];

manager.addStringHandler(peer, nameOid, &namePointer, sizeof(name));
manager.addOIDHandler(peer, oidOid, returnedOid, sizeof(returnedOid));
```

Use `addOctetHandler` for binary OCTET STRING values and `addOpaqueHandler` for Opaque values. Both take a byte buffer, capacity, and returned-length pointer.

## Remove `SNMPGet::setIP()`

`setIP()` and the public `agentIP` field have been removed because they never affected the encoded packet or UDP transport. Continue passing the remote destination to `sendTo()`:

```cpp
request.setUDP(&udp);
request.sendTo(remoteAgent);
```

## Handle outstanding requests

After a successful send, a callback accepts only a response with the matching request ID, peer, and UDP transport. Each callback can track `SNMP_MAX_PENDING_REQUESTS` requests. Matching replies consume their slots; a send fails before transmission if a required callback has no available slot.

Use distinct request IDs while earlier responses can still arrive. When a polling interval establishes that requests have timed out, cancel the callbacks currently attached to the request before reusing them:

```cpp
request.cancelPendingRequests();
request.setRequestID(nextRequestId);
bool sent = request.sendTo(remoteAgent);
```

`clearOIDList()` only removes callbacks from the request builder. It does not cancel their pending response state. `ValueCallback::clearPendingRequests()` remains available when cancellation must apply to one callback.

## Check operations that can fail

Handler registration returns `nullptr` on failure; `addOIDPointer`, `sendTo`, request building, and `SNMPManager::addHandler` report failure. Embedded allocation failures and UDP failures are handled without partial ownership, but applications must check these results.

`SNMPManager`, `SNMPGet`, `SNMPGetResponse`, and `ComplexType` are owning types and cannot be copied. Keep stable instances instead of copying them. Only `SNMPManager` and `SNMPGet` provide move constructors; `SNMPGetResponse` and `ComplexType` do not support moving. The low-level manager and request borrow their community strings; keep those strings alive and unchanged while in use. `SNMPDevice` instead owns its community copy. `addHandler` adopts a callback only when it succeeds. `ComplexType::addValueToList` consumes its BER child on both success and failure.

## Custom BER types

Custom classes derived from `BER_CONTAINER` must implement the bounded virtual interface:

```cpp
int serialise(unsigned char *buffer, size_t capacity) override;
bool fromBuffer(unsigned char *buffer, size_t available) override;
```

Return a negative serialization length or `false` when the supplied bounds are insufficient. Built-in decoders require the complete TLV size so they can reject truncated or overlong input.

## Optional device and query API

After applying the migration changes above, low-level handler code remains supported. New sketches can use
`SNMPClient`, `SNMPDevice`, `SNMPQuery`, `SNMPWalk`, and `SNMPTableRead` without
callback registration or explicit request IDs. The client accepts a UDP reference;
devices accept `IPAddress` or checked dotted IPv4 strings and own their community.
Results belong to their operation and include individual status. See the
[query API guide](docs/QUERY_API.md) and new examples for exact usage.

`SNMPClient::begin()` returns a status object; the old `SNMPManager::begin()` still
returns bool. Do not run both engines on the same UDP instance. `SNMPSet` does not
split or automatically retry writes. Notification handling supports v1/v2c traps
and v2c INFORM acknowledgement; SNMPv3 remains unsupported.

## Query value storage

The new query API defaults to 256 bytes for dotted OIDs (`MAX_OID_LENGTH`, including
termination) and 1024 bytes per retained value (`SNMP_VALUE_MAX_LENGTH`, excluding
termination). Packet and decoder limits remain separate. Configure macros consistently
for all compilation units. Numeric values do not allocate payload storage; variable
values use bounded heap storage and report retention failures as `AllocationFailure`.
`SNMPValue::bytes` is read-only: use `setBytes()` and check its status instead of
writing into the payload. Copies retain shared ownership across subsequent polls.
See [query values and MIB helpers](docs/QUERY_API.md#common-mib-values).

## Maintained 1.x improvements

The 2.x branch retains the 1.2.x null-input response rejection, duplicate-registration
request matching, `updateCount()` freshness signal, bounded string examples, and
wrap-safe polling/rate helpers. Owning library objects remain noncopyable in 2.x;
1.2.x copy compatibility and rolling tracking were specific to the 1.x backport.
2.x tracking remains bounded/strict; abandon timed-out requests before reuse.
