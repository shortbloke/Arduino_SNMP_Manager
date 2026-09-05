# Device and query API design

The proposal is implemented in the 2.x API. This document records its design
rationale; [QUERY_API.md](QUERY_API.md) is the usage reference and the
[examples](../examples/README.md) contain compiling sketches. The original proposal
is preserved in Git history at `archive/friendly-query-api-2026-09-05`.

## Device-oriented queries

`SNMPClient` services an Arduino UDP transport through `loop()`. `SNMPDevice` owns
its checked IPv4 address and community. Reusable read, write, walk, and table
operations own their results and expose completion and status without requiring
per-OID callback registration or user-assigned request IDs.

This addresses [issue #28](https://github.com/shortbloke/Arduino_SNMP_Manager/issues/28):
reading traffic counters across switch interfaces. `SNMPInterfaceRead` discovers
actual indices, joins descriptions and counters, and prefers Counter64 with
Counter32 fallback. Results retain counter widths; rates and discontinuity handling
belong to the application. Generic tables retain complete composite index suffixes.

## Embedded resource model

The implementation targets ESP8266 and ESP32 with C++11 and no exception-dependent
control flow. Operation, row, OID, packet, and payload capacities are bounded.
Numeric values require no payload allocation; variable-length values use bounded
shared heap storage. Allocation and capacity failures are explicit where operations
can report them; decoder allocation failures may cause retries or timeouts.

Network progress is cooperative. Reads have bounded retries; walks have deadlines.
SET uses one packet and is never automatically retried. Notifications are processed
synchronously, and accepted v2c INFORMs receive acknowledgements. Both targets offer
the same SNMPv1/v2c operations; larger memory budgets permit larger capacities.
SNMPv3, DNS, runtime MIB parsing, and notification origination are not implemented.

## Compatibility and ownership

The low-level `SNMPManager`/`SNMPGet` API remains available with the 2.x changes in
[MIGRATION.md](../MIGRATION.md). It shares protocol code with the friendly API but
uses its own callback routing and request tracking. It must not consume the same
UDP instance as an `SNMPClient`.

The transport outlives the client, the client its devices, and devices their
operations. Query owners are noncopyable and nonmovable; stable instances keep
borrowed references valid. Accepted starts invalidate old operation results;
rejected starts preserve them. Applications can copy an `SNMPValue` to retain its
shared payload across subsequent polls.

## Validation

Shared native regressions cover scheduling, correlation, ownership, cancellation,
low-heap recovery, sparse traversal, and composite tables. An independent mock agent
checks GET/GETNEXT/GETBULK exchanges; Net-SNMP tests exercise host wire interoperability.
Real ESP toolchains compile the library and examples. Historical physical-board
results identify the tested board and revision; they do not certify every checkout.

See [native tests](../tests/native/README.md), [embedded builds](../tests/embedded/README.md),
and [hardware testing](../tests/hardware/README.md) for their respective scope.
