# Protocol references for the native suite

> **PUBLIC** — Tracked in Git and shared in the repository.

Contributor reference: this document covers library validation, not application
setup. To read data from your device, start with [getting started](../../docs/GETTING_STARTED.md).

These references explain the expectations behind the SNMPv1 and community-based SNMPv2c operation, notification, and BER tests in `cases/`. They distinguish protocol requirements from library conventions. The suite covers selected behavior; passing it is not a conformance certification.

## Message versions

SNMPv1 uses an ASN.1 sequence containing version INTEGER 0, community OCTET STRING, and a PDU. Failed parsing and unsupported versions must cause rejection. Missing objects produce a PDU-level `noSuchName` error with a one-based error index. [RFC 1157 §§4–4.1.2](https://www.rfc-editor.org/rfc/rfc1157.html#section-4)

SNMPv2c retains the community wrapper but uses version INTEGER 1. The project's term “v2” means v2c in this context. The parser's internal `version + 1` representation must not be confused with the wire value. [RFC 1901 §3](https://www.rfc-editor.org/rfc/rfc1901.html#section-3)

## Requests and responses

Get requests address exact object instances; NULL is an appropriate placeholder. Responses preserve request IDs for correlation. Nonzero error-status means binding values must be ignored. In v2c, `noSuchObject` and `noSuchInstance` are per-binding exceptions; `endOfMibView` belongs to GetNext/GetBulk traversal. A `tooBig` response has an empty binding list. [RFC 3416 §§4.1–4.2.4](https://www.rfc-editor.org/rfc/rfc3416.html#section-4.1)

Short messages and empty binding lists are not inherently malformed. Tests cover structural validation, mixed successful/exception bindings, PDU-level errors without updates, request correlation, and short `tooBig` responses.

## Transport and BER

UDP 161 is the suggested command-responder port; 162 is for notification receivers. The UDP mapping requires reception through 484 bytes and recommends 1472. SNMP requires definite lengths and primitive simple values, but explicitly permits extra length octets. [RFC 3417 §§3, 8](https://www.rfc-editor.org/rfc/rfc3417.html#section-3)

Responses return to the originating transport endpoint. Binding the shared request/response socket to 162 can work, but 162 is not a mandatory response destination. The low-level `SNMPManager::begin()` binds port 162; `SNMPClient::begin()` defaults to port 0 (a transport-selected ephemeral port). Pass 162 explicitly for notification reception. [RFC 3412 §4.1.2](https://www.rfc-editor.org/rfc/rfc3412.html#section-4.1.2)

Keep exact-byte assertions for the chosen encoder output, but do not require decoders to accept only that representation. Tests cover nonminimal definite lengths and reject indefinite lengths.

BER INTEGER contents use minimal two's-complement, most-significant octet first: 128 is `02 02 00 80`, -1 is `02 01 ff`. Unsigned application integers still require a leading zero when their high bit is set. Counter64 maximum therefore needs nine content octets; fixed eight-byte output is incorrect. OIDs combine the first two arcs as `40*first + second`, then encode subidentifiers in minimal base-128 groups. [ITU-T X.690 §§8.3, 8.19](https://www.itu.int/rec/T-REC-X.690-202102-I/en)

Exact-byte integer assertions describe minimal signed BER, not a requirement for SNMP to use CER/DER.

## Data types versus library conventions

INTEGER values (including SMIv2 Integer32) are signed 32-bit. OCTET STRING holds binary or textual bytes, with no implicit C terminator. OID limits are 128 subidentifiers, each at most 2^32−1—not 128 printable characters. Counter32/64 wrap; Gauge32 can increase or decrease; TimeTicks measures hundredths of a second modulo 2^32. [RFC 2578 §7.1](https://www.rfc-editor.org/rfc/rfc2578.html#section-7.1)

Tests cover embedded-zero strings, maximum subidentifiers, and the 128-arc limit. `make oid-limits` uses a separate 1408-byte printable-OID build to round-trip the maximum legal OID against independent expected wire bytes; production defaults remain smaller. The float handler's division by ten is a library convention whose suitability depends on the MIB; SNMP does not mandate that scale. C-string termination is also an API responsibility. Tests for these API conventions are not standalone protocol-conformance assertions.

## Current standards and scope of this audit

The current Internet Standard is [STD 62](https://www.rfc-editor.org/info/std62/),
a suite of RFCs covering the SNMP architecture, message processing, applications,
security, access control, operations, transports, and management instrumentation.
There is no single newer RFC that replaces the whole suite. This project remains a
bounded **SNMPv1/v2c manager**, not a full STD 62/SNMPv3 implementation. It must not
be advertised as fully RFC-compliant on the strength of these tests.

[RFC 3416](https://www.rfc-editor.org/info/rfc3416/) remains the operations reference.
The verified [erratum 2757](https://www.rfc-editor.org/errata/eid2757) corrects the
request-ID range to signed 32-bit. Dedicated response/BulkPDU tests cover both ends
of that range using independently specified wire bytes. The automatic client uses
a positive subset and never wraps IDs into old outstanding traffic.

[RFC 3417](https://www.rfc-editor.org/info/rfc3417/) is updated by
[RFC 4789](https://www.rfc-editor.org/info/rfc4789/) (direct IEEE 802 transport) and
[RFC 5590](https://www.rfc-editor.org/info/rfc5590/) (the transport subsystem).
This library uses UDP/IPv4; it does not implement those additional transport models.
[RFC 3584](https://www.rfc-editor.org/info/rfc3584/) supersedes RFC 2576 for version
coexistence. Selecting v1/v2c per device is not a claim to implement its proxy or
message-translation functions.

| Area | Implementation / regression evidence | Limits |
| --- | --- | --- |
| v1/v2c envelopes | Wire versions 0/1; reject unsupported versions and v2-only PDUs in v1 | Community lengths are bounded; no SNMPv3 message/security processing |
| VarBind syntax | Simple supported alternatives only; reject nested structures and v2-only values in v1 responses/traps | MIB-specific semantics remain application-owned |
| GET / SET | Correlated request IDs, peer, transport, community and version; whole-packet SET, per-result statuses | Native and Net-SNMP wire tests are selected coverage, not exhaustive conformance |
| Walks | Numeric successor ordering, subtree boundaries, sparse indices, v1 GETNEXT and v2c GETBULK | Bounded collection and walk deadline are application policy |
| Empty bulk response | RFC 3416 permits zero bindings; retry via GETNEXT instead of declaring malformed data or false end-of-view | Empty GETNEXT remains an error; fallback preserves the existing deadline |
| Notifications | v1 metadata, v2c leading uptime/trap-OID bindings, accepted INFORM replies | No notification originator, proxy, management counters/MIB instrumentation, or SNMPv3 security |
| BER / OIDs | Definite lengths, nonminimal length octets, primitive encodings, 32-bit arcs, 128-arc maximum, full-size native OID round-trip | Configured capacities can reject legal messages |
| Transport | Default receive buffers exceed 484 bytes; ESP32 default also exceeds the 1472-byte recommendation | Smaller custom profiles intentionally fall below the transport requirement; physical UDP behavior is not certified |

The successful empty-bulk response case follows
[RFC 3416 §4.2.3](https://www.rfc-editor.org/rfc/rfc3416.html#section-4.2.3).
Choosing GETNEXT as recovery is this client's policy, not a mandated algorithm.

## Remaining conformance limits

- Default `MAX_OID_LENGTH=256` cannot represent every legal OID. The largest legal
  dotted representation needs 1400 bytes including termination. The wide native
  test uses 1408; this increases fixed object storage and stack use, so it is not
  silently enabled on constrained targets. Retaining a maximum-size OID through
  the query API also requires `SNMP_VALUE_MAX_LENGTH >= 1399` and sufficient packet
  capacity. The wide test validates the codec, not a complete full-capacity client.
- Communities, octet strings, packets, retained rows and pending operations have
  separate bounds. A legal wire message may exceed them; configure for the agent's
  workload. Passing minimum packet-size checks alone does not prove that every
  legal message at that size can be decoded and retained.
- Full STD 62 includes functionality outside the agreed v1/v2c scope. No SNMPv3
  engine/security model, USM/VACM, discovery/timeliness, secure transport, agent MIB
  instrumentation, or proxy is claimed. Manager-only scope does not certify those
  components of a complete SNMP engine.
- This audit and suite are not an independent conformance certification or an
  exhaustive ASN.1/procedure proof. Additional malformed inputs, all error-index
  combinations, every agent behavior, and physical-network interoperability need
  broader validation. A D1 Mini read/walk run is recorded in [the hardware results](../hardware/D1_MINI_RESULT.md);
  other boards, operations, and failure conditions still require physical validation.

The accurate product claim is **bounded SNMPv1/v2c manager operations with explicit
resource limits and RFC-derived regression coverage**, not “implements the latest
SNMP standard in full.”
