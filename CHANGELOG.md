# SNMP Manager 2.x changelog

This file records the 2.x release line. The maintained
[1.x changelog](https://github.com/shortbloke/Arduino_SNMP_Manager/blob/release/1.x/CHANGELOG.md)
contains the earlier releases and ongoing backward-compatible fixes.

## 2.0.0-alpha.1

First preview of the 2.x API (unpublished draft). Existing 1.x projects should read the
[migration guide](MIGRATION.md) before upgrading.

### Friendly query API

- Introduce `SNMPClient` and `SNMPDevice` with checked IPv4 strings, owned results,
  automatic request IDs, bounded scheduling, batching, retries, and completion status.
- Add range queries, selected-column tables, and interface traffic reads without
  per-OID callbacks, addressing [#28](https://github.com/shortbloke/Arduino_SNMP_Manager/issues/28).
- Support SNMPv1/SNMPv2c GET, GETNEXT, bounded walks and SET, plus SNMPv2c GETBULK.
- Discover non-contiguous table indices and prefer Counter64 interface counters
  with Counter32 fallback where high-capacity counters are unavailable.
- Receive SNMPv1/SNMPv2c traps and acknowledge SNMPv2c INFORMs.
- Add helpers for common MIB values and examples for interface traffic, host
  storage, printer supplies, multiple devices, writes, and notifications.

### Breaking changes from 1.x

- Compile and link `src/*.cpp`; the library is no longer header-only.
- Apply configuration macros consistently to the application and library through
  build flags, rather than sketch-local defines.
- Use named SNMP versions, fixed-width numeric destinations, signed 32-bit
  request IDs, and mandatory capacities for text and OID buffers.
- Make owning library objects noncopyable and require explicit handling of
  bounded request capacity and cancellation.
- Remove `SNMPGet::setIP()` and the library's definition of the transport-owned
  `UDP_TX_PACKET_MAX_SIZE` macro.
- Target modern ESP8266/ESP32 platforms; older AVR platforms are not supported.

### Robustness and memory

- Recover from lost or oversized GETBULK replies with GETNEXT after bounded retries;
  reduce timed-out GET batches while leaving SET unsplit and unretried.
- Allow more logical interfaces in the interface example with a bounded 64-row table.

- Validate SNMP version/PDU/value combinations across the expanded manager
  operations and enforce the protocol's OID arc-count limit.
- Bound query, walk, table, packet, and payload storage; release invalidated
  payloads and report allocation failures through checked results.
- Support compact table-index storage and recover from empty successful GETBULK
  responses using GETNEXT.

### Validation and release tooling

- Add modular native regressions, independent mock agents, sparse-table fixtures,
  low-heap failure tests, sanitizer and lifecycle/leak checks.
- Build examples and library configurations for the supported ESP targets, and
  validate wire interoperability against Net-SNMP.
- Retain historical D1 Mini read/walk and burst-test evidence; those runs do not
  certify this preview on every board or agent.
- Automate release preparation PRs and checked publication for both major lines.
- Gate CI and releases with workflow/shell, Python, and Arduino packaging lint checks.

### Current limitations

- SNMPv3, DNS resolution, runtime MIB parsing, and sending traps/INFORMs are not
  implemented. This is a bounded SNMPv1/SNMPv2c manager, not full SNMP conformance
  certification.
