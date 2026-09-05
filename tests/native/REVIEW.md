# Review findings

Reviewed the five library headers, package metadata, README, and example layout. Findings below distinguish executable failures from source inspection. The original review left production files unchanged; subsequent fixes are noted below.

## Specification cross-check

See [RFC review notes](RFC_NOTES.md) for official sources and corrections to the interpretation of this suite. In particular, empty varbind lists can be valid, v2c exceptions are per binding, binary strings need length-aware handling, and float scaling is a library convention. The current suite is not a protocol-conformance suite.

## Confirmed by regression tests

| Priority | Location | Finding / reproducible trigger |
| --- | --- | --- |
| High | `src/BER.h`, `IntegerType::serialise` | Encoding 128 produces incorrect BER bytes rather than `02 02 00 80`. Multi-byte serialization also shifts `_value` itself, destroying the stored value. This affects request IDs and derived integer types. Two regression cases. |
| High | `src/Arduino_SNMP_Manager.h`, `parsePacket` version condition | The version expression is always false, so an otherwise valid response with unsupported version updates registered values. |
| High | `src/Arduino_SNMP_Manager.h`, INTEGER float branch | Integer division discards tenths and the result is written through an `int*` pointing at float storage. An integer response of 123 does not produce 12.3. |
| Medium | `src/Arduino_SNMP_Manager.h`, STRING branch | Copying only the incoming string length omits the terminator. Updating `previous value` with `new` retains the old suffix. |
| Medium | `src/BER.h`, `OctetType::serialise` | **Fixed:** the length header now uses two octets at exactly 256 bytes. The regression check is promoted to baseline. |
| Medium | `src/BER.h`, `OIDType::serialise` | Strict greater-than comparisons lose a base-128 group at subidentifier 16384. |

## Additional RFC-based regressions

The RFC extension added 15 failing cases, bringing the regression group to 22 before the issue #66 comparison:

| Area | Executed finding |
| --- | --- |
| Signed INTEGER | Negative short encodings are not sign-extended; INT32_MIN serialization is incorrect. |
| Unsigned application integers | Counter32 maximum is encoded incorrectly; Counter64 always emits eight bytes for nonzero values, violating minimal encoding for 1 and omitting the positive sign octet for UINT64_MAX. |
| Binary OCTET STRING | Embedded-zero decoding loses subsequent bytes; re-encoding uses C-string length. Two separate cases. |
| OIDs | Decoding assumes .1.3 even for .2.999.3; encoding UINT32_MAX loses the required fifth base-128 octet. |
| BER lengths | An indefinite sequence is accepted even though SNMP forbids it. |
| Empty lists | A short tooBig response is rejected by the size heuristic; a valid long-community variant reaches a null dereference. Two separate cases. |
| PDU errors | genErr responses update destination values despite nonzero error-status (both v1 and v2c fixtures). |
| Per-binding exceptions | A missing object/instance prevents a subsequent successful binding from updating. |
| Request correlation | After sending request 7, response 8 updates its destination. The public API currently has no outstanding-request integration. |

These cases assert desired behavior and remain failures. In particular, a crash is not an expected-pass condition. Sanitizers identify the empty-list null dereference at `src/SNMPGetResponse.h:184`; process isolation lets later cases execute.

## Additional findings from source inspection

These are not counted as executed regression cases.

- **High — memory bounds:** `OctetType(char*)` writes `_value[sizeof(_value)]`, one byte beyond the member array (compiler warning confirms this). The copy also leaves maximum-length input without an in-bounds terminator.
- **High — receive bounds:** `receivePacket` limits the read but uses the original datagram length for `_packetBuffer[len]`. Lengths at or above the backing array size write out of bounds; smaller oversized datagrams are parsed after truncation. The actual read result is ignored. `testParsePacket` similarly has no token-count bound.
- **High — parser bounds:** BER parsing has no input-size parameter, trusts encoded lengths, and response traversal dereferences missing child nodes (including a valid empty varbind list). Malformed/truncated network packets can cause invalid reads or null dereferences.
- **High — initialization:** `_udp` has no member initializer and is read by `setUDP`, `begin`, and `loop`. Automatic-storage managers are unsafe before assignment; global zero-initialized managers can hide this. The default constructor also leaves `_community` unset.
- **Medium — OID handler:** `addOIDHandler` allocates but never copies the requested OID; response dispatch has no OID update case.
- **Medium — ownership:** manager/request classes have no destructors for their allocations. `SNMPGet::build` drops an existing packet without deleting it, and repeated `SNMPGetResponse::parseFrom` neither resets parsing state nor releases the preceding packet.
- **Medium — portability:** OID decoding formats a `long` with `%d`, confirmed by a compiler warning. Integer storage uses platform-dependent `unsigned long`; negative INTEGER decoding lacks sign extension. OID handling assumes a `.1.3` root.
- **Medium — transport results:** send ignores beginPacket/write failures; begin ignores the UDP begin result; loop returns true even when response parsing fails. Error-status and request-ID correlation are not used to guard callback updates.

## Issue #66 comparison

Inspected [issue #66](https://github.com/0neblock/Arduino_SNMP/issues/66) and [fork v3.3.1](https://github.com/syntax1269/Arduino_SNMP/tree/bffb43c2f3b1d2a434989e932e171dda24aaf78e). The fork is an agent; this repository implements manager GetRequest/response handling. The comparison suggested 13 independent cases, integrated into the standard suite in `tests.cpp` with behavior-based names; no production edits or fork dependencies.

| Comparison | Local result |
| --- | --- |
| Sequence length exactly 256 | **Fixed:** the length header now uses two octets at exactly 256 content bytes. This check and adjacent sequence sizes pass in the baseline. |
| Long-form header accounting | Nested sequence/sibling alignment passes. Counter64 long-form length decoding fails here. Local parsers return bool, so the fork's incorrect consumed-byte return is not literally present. |
| Defensive parser bounds | Child overrun of its enclosing sequence, a dangling child tag, and a truncated UDP response are accepted. Added three failures. |
| Three-byte negative integer | Callback gets an incorrect positive number. Related sign-extension defect; the fork's specific compound-assignment expression is absent. |
| UDP initialization | Failed bind is reported as success. Fake UDP now supports begin failure injection. |
| Community truncation | A 1024-byte incoming community matches its 253-byte prefix after lossy decoding. The test uses the public parser helper to isolate this from UDP packet-size truncation. |
| Embedded-zero community | A distinct community beginning with `public` followed by NUL and another byte is accepted. Related comparison weakness, not claimed to be the exact fork fix. |
| Callback destruction | A type-trait regression confirms no virtual base destructor. This is a latent public-API hazard, not evidence of an existing manager deletion path: callbacks currently leak rather than being polymorphically deleted. |

The headline explicit request destruction/ASNPool double-release mechanism is absent. A nested-child destruction control passes, but is not a long-duration heap or concurrency test. Fixed callback-array overflow, trap/INFORM ownership, GetBulk generation limits, SET-change flags and pool sizing have no matching implementation here. Existing empty-response and PDU-error tests cover the manager's side of handling tooBig responses.

The fork is not a conformance oracle: inspected `BEREncode.cpp` still uses fixed-width INTEGER/Counter64 contents, and `SNMPParser.cpp` still compares community C strings. Tests retain independent expected values and do not assume every reported hardening measure closes every edge case.

## Changelog and commit-history review

Seven additional cases are integrated into `tests.cpp`, using behavior-based names and the existing baseline/regression groups. The review used local commit diffs rather than treating release notes as proof of correctness.

| Historical fix | Coverage and result |
| --- | --- |
| 1.1.13, `bd224c6`: ten-digit OID segments | New nonterminal `.1.3.1000000000.0` encoding fails: five base-128 octets are needed, while the encoder emits at most four. New `.1.3.4294967295.0` decoding fails because the unsigned subidentifier is formatted as signed. Existing maximum-subidentifier encoding also fails. The buffer enlargement is not proof of full-range support. |
| 1.1.7, `6679e43`: large OID integers | New four-octet `268435455` encoding passes; the exact `2097152` boundary fails, extending the existing `16384` boundary finding. |
| 1.1.1/1.1.5, `ae41389` / `ca5c04f`: OIDs longer than 50 characters | New 52- and 124-character OID cases pass through callback lookup, exact request encoding, and response dispatch. |
| `2b50823`: same OID on multiple devices | New end-to-end responses from two registered peers update only their own destinations. Passes. |
| 1.1.3, `1fce429`: unexpected OIDs and malformed packets | New unregistered-OID response leaves the callback intact and a following valid response succeeds. Existing wrong-top-level-tag test also exercises safe response destruction after rejection. |
| 1.1.11, `64144da`: minimal INTEGER encoding | Already covered by small-value passes and signed/minimal encoding failures; no duplicate case added. |
| 1.1.8, `8325ec7`: unsigned timeticks | Existing UINT32_MAX decode and manager timestamp dispatch checks cover the signed boundary. |
| 1.1.6, `69995ec`: custom destination port | Existing request wire/port test checks default 161 and custom 1161. |
| 1.1.4, `2adad9a`: responses longer than 128 bytes | Existing 484-byte response and long-form wrapper checks cover this path. |
| `9c05f31` / `19eefa3`: long strings | Existing string length boundaries and 434-byte string callback cover the historical size limits. |

The additions contribute four passing cases and three failing cases (related OID defects, not necessarily three independent root causes). Both Make groups were executed normally and with ASan/UBSan. ESP8266 alignment/UDP fixes require target hardware validation; logging suppression and example-only changes are not covered by these native behavioral additions.

## Validation before library fixes

On the supplied macOS environment using Apple Clang:

- Baseline: **34 passed, 0 failed**.
- Regression group: **0 passed, 35 failed**, demonstrating the listed defects.
- Baseline with AddressSanitizer and UndefinedBehaviorSanitizer: **34 passed, 0 failed**.
- Regression group with ASan/UBSan: **0 passed, 35 failed**, including the isolated empty-list diagnostic.
- Compiler reports the string-member out-of-bounds write and OID format mismatch. Runtime sanitizer success is limited to exercised behavior and does not override those findings.

No hardware or real SNMP agent was used. See [README.md](README.md) for commands, fixture design, and explicit coverage gaps.

## BER length-boundary fix

String and sequence serialization now select a two-octet length at 256 bytes (`82 01 00`). Both existing exact-wire regressions are promoted to baseline; neighboring length checks remain passing.

Validation: `make -C tests/native check` reports 36 baseline passes and 33 remaining regression failures. `make -C tests/native sanitize` reports 36 baseline passes with ASan/UBSan. The complete check still returns nonzero for the remaining defects.
