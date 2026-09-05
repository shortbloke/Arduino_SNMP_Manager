# Review findings

The original review covered the library headers, package metadata, README, and examples. The original failing regressions are now resolved and promoted to baseline. The sections below preserve historical findings and validation snapshots; later fix entries supersede their descriptions of failures.

Current test layout: cases are now grouped by behavior in `cases/`, with shared fixtures and runners described in [README.md](README.md#source-organization). References below to `tests.cpp`, baseline/regression groups, and header-only implementations describe earlier snapshots. The current suite also checks configured limits at runtime and runs ownership/response cases in a separate lifecycle executable for leak detection.

## Current scope and remaining limitations

All previously failing regression checks pass. The final validation covers native Make and PlatformIO, with and without ASan/UBSan. This is not a board-compatibility or complete protocol-conformance certification.

The additional source-review fixes now cover safe initialization, registration/request ownership, bounded primitive decoding and request serialization, OID callbacks, binary callback delivery, Opaque decoding, and concurrent request tracking. Legacy APIs without buffer sizes still require caller-provided sufficient storage. Destination buffers, community strings, and transports remain borrowed. Pending-request slots are finite and require explicit clearing when requests time out.

The bounded parser caps nesting at 32, and printable OID/string capacities impose limits below some protocol maxima. Hardware behavior and exhaustive conformance remain outside the validated scope; allocation-failure injection and real target compile/link checks are now included. Historical findings below are preserved as review history, not a current defect inventory.

## Specification cross-check

See [RFC review notes](RFC_NOTES.md) for official sources and corrections to the interpretation of this suite. In particular, empty varbind lists can be valid, v2c exceptions are per binding, binary strings need length-aware handling, and float scaling is a library convention. The current suite is not a protocol-conformance suite.

## Original findings confirmed by regression tests

| Priority | Location | Finding / reproducible trigger |
| --- | --- | --- |
| High | `src/BER.h`, `IntegerType::serialise` | Encoding 128 produces incorrect BER bytes rather than `02 02 00 80`. Multi-byte serialization also shifts `_value` itself, destroying the stored value. This affects request IDs and derived integer types. Two regression cases. |
| High | `src/Arduino_SNMP_Manager.h`, `parsePacket` version condition | **Fixed:** responses with versions other than v1/v2c are rejected before callback dispatch. The baseline checks unsupported wire values and subsequent valid responses. |
| High | `src/Arduino_SNMP_Manager.h`, INTEGER float branch | **Fixed:** float callbacks write through a float pointer and divide by 10.0f, preserving fractional tenths. |
| Medium | `src/Arduino_SNMP_Manager.h`, STRING branch | **Fixed:** string dispatch copies the terminator and no longer constructs an unused temporary OctetType. Baseline checks cover shorter, empty, and growing strings. |
| Medium | `src/BER.h`, `OctetType::serialise` | **Fixed:** the length header now uses two octets at exactly 256 bytes. The regression check is promoted to baseline. |
| Medium | `src/BER.h`, `OIDType::serialise` | Strict greater-than comparisons lose a base-128 group at subidentifier 16384. |

## Additional RFC-based regressions

The RFC extension added 15 failing cases, bringing the regression group to 22 before the issue #66 comparison:

| Area | Executed finding |
| --- | --- |
| Signed INTEGER | Negative short encodings are not sign-extended; INT32_MIN serialization is incorrect. |
| Unsigned application integers | Counter32 maximum is encoded incorrectly. **Counter64 serialization fixed:** minimal contents and positive sign padding now pass, including UINT64_MAX. |
| Binary OCTET STRING | Embedded-zero decoding loses subsequent bytes; re-encoding uses C-string length. Two separate cases. |
| OIDs | Decoding assumes .1.3 even for .2.999.3; encoding UINT32_MAX loses the required fifth base-128 octet. |
| BER lengths | An indefinite sequence is accepted even though SNMP forbids it. |
| Empty lists | A short tooBig response is rejected by the size heuristic; a valid long-community variant reaches a null dereference. Two separate cases. |
| PDU errors | **Fixed:** nonzero error-status prevents all callback updates. Baseline coverage checks both v1 and v2c responses and subsequent successful dispatch. |
| Per-binding exceptions | A missing object/instance prevents a subsequent successful binding from updating. |
| Request correlation | After sending request 7, response 8 updates its destination. The public API currently has no outstanding-request integration. |

These cases assert desired behavior and remain failures. In particular, a crash is not an expected-pass condition. Sanitizers identify the empty-list null dereference at `src/SNMPGetResponse.h:184`; process isolation lets later cases execute.

## Original findings from source inspection

These are not counted as executed regression cases.

- **High — memory bounds:** `OctetType(char*)` writes `_value[sizeof(_value)]`, one byte beyond the member array (compiler warning confirms this). The copy also leaves maximum-length input without an in-bounds terminator.
- **High — receive bounds:** `receivePacket` limits the read but uses the original datagram length for `_packetBuffer[len]`. Lengths at or above the backing array size write out of bounds; smaller oversized datagrams are parsed after truncation. The actual read result is ignored. `testParsePacket` similarly has no token-count bound.
- **High — parser bounds:** BER parsing has no input-size parameter, trusts encoded lengths, and response traversal dereferences missing child nodes (including a valid empty varbind list). Malformed/truncated network packets can cause invalid reads or null dereferences.
- **High — initialization:** `_udp` has no member initializer and is read by `setUDP`, `begin`, and `loop`. Automatic-storage managers are unsafe before assignment; global zero-initialized managers can hide this. The default constructor also leaves `_community` unset.
- **Medium — OID handler:** `addOIDHandler` allocates but never copies the requested OID; response dispatch has no OID update case.
- **Medium — ownership:** manager/request classes have no destructors for their allocations. `SNMPGet::build` drops an existing packet without deleting it, and repeated `SNMPGetResponse::parseFrom` neither resets parsing state nor releases the preceding packet.
- **Medium — portability:** OID decoding formats a `long` with `%d`, confirmed by a compiler warning. Integer storage uses platform-dependent `unsigned long`; negative INTEGER decoding lacks sign extension. OID handling assumes a `.1.3` root.
- **Medium — transport results:** send ignores beginPacket/write failures; loop returns true even when response parsing fails. Request-ID correlation is not used to guard callback updates.

## Issue #66 comparison

Inspected [issue #66](https://github.com/0neblock/Arduino_SNMP/issues/66) and [fork v3.3.1](https://github.com/syntax1269/Arduino_SNMP/tree/bffb43c2f3b1d2a434989e932e171dda24aaf78e). The fork is an agent; this repository implements manager GetRequest/response handling. The comparison suggested 13 independent cases, integrated into the standard suite in `tests.cpp` with behavior-based names; no production edits or fork dependencies.

| Comparison | Local result |
| --- | --- |
| Sequence length exactly 256 | **Fixed:** the length header now uses two octets at exactly 256 content bytes. This check and adjacent sequence sizes pass in the baseline. |
| Long-form header accounting | Nested sequence/sibling alignment passes. **Counter64 fixed:** long-form length headers are decoded before reading the value. Local parsers return bool, so the fork's incorrect consumed-byte return is not literally present. |
| Defensive parser bounds | Child overrun of its enclosing sequence, a dangling child tag, and a truncated UDP response are accepted. Added three failures. |
| Three-byte negative integer | Callback gets an incorrect positive number. Related sign-extension defect; the fork's specific compound-assignment expression is absent. |
| UDP initialization | **Fixed:** `begin()` returns the UDP bind result. The baseline checks failure and a successful retry on port 162. Fake UDP supports begin failure injection. |
| Community truncation | A 1024-byte incoming community matches its 253-byte prefix after lossy decoding. The test uses the public parser helper to isolate this from UDP packet-size truncation. |
| Embedded-zero community | A distinct community beginning with `public` followed by NUL and another byte is accepted. Related comparison weakness, not claimed to be the exact fork fix. |
| Callback destruction | **Fixed:** ValueCallback has a virtual destructor. The baseline verifies deletion through the base pointer invokes the derived destructor once. Manager ownership and allocation cleanup remain separate concerns. |

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

## UDP bind-result fix

`SNMPManager::begin()` now returns false when the transport fails to bind, instead of unconditionally reporting success. Its regression check is promoted to baseline and also verifies a successful retry on port 162.

Validation: `make -C tests/native check` reports 37 baseline passes and 32 remaining regression failures. `make -C tests/native sanitize` reports 37 baseline passes with ASan/UBSan. The complete check still returns nonzero for the remaining defects.

## Unsupported-version rejection fix

`parsePacket()` now rejects a response when its decoded version is neither v1 nor v2c. Previously the version rejection expression was always false. The existing regression is promoted to baseline and expanded to reject wire values 2, 3, and 127, then accept valid v1/v2c responses after each rejection.

Validation: `make -C tests/native check` reports 38 baseline passes and 31 remaining regression failures. `make -C tests/native sanitize` reports 38 baseline passes with ASan/UBSan. The complete check still returns nonzero for the remaining defects.

## PDU error-status fix

`parsePacket()` now stops before callback dispatch when a parsed response has nonzero error-status. The promoted baseline check exercises error statuses 1–5 for v1 and v2c, verifies both destinations remain unchanged, and confirms a subsequent successful response updates both.

Validation: `make -C tests/native check` reports 39 baseline passes and 30 remaining regression failures. `make -C tests/native sanitize` reports 39 baseline passes with ASan/UBSan. Empty-varbind parsing defects remain separate regressions; this guard applies after successful parsing.

## String callback termination fix

String callbacks now copy the terminating NUL, including for empty responses. Removed the unused temporary OctetType allocation from this dispatch path. Caller-provided storage must still be large enough. Normal checks report 40 baseline passes and 29 remaining failures; the sanitizer baseline also passes all 40 cases.

## Float callback fix

Float callback dispatch restores the registered float pointer before writing and uses floating-point division. The unused temporary IntegerType is removed. Baseline checks cover zero, fractional, integral, and repeated updates. Normal checks report 41 baseline passes and 28 remaining failures; the sanitizer baseline also passes all 41 cases. Signed INTEGER decoding remains a separate defect.

## Polymorphic callback destruction fix

Added a virtual ValueCallback destructor without changing registration ownership or freeing caller-owned destinations. The baseline verifies derived destruction through a base pointer. This adds a vtable pointer to callback objects, increasing their memory footprint. Normal checks report 42 baseline passes and 27 remaining failures; the sanitizer baseline also passes all 42 cases.

## Counter64 serialization fix

Counter64 serialization now emits minimal big-endian contents with a leading zero when needed to preserve the positive sign. Tests cover zero, byte/sign boundaries, the 64-bit sign boundary, UINT64_MAX, value preservation, and repeat serialization. Normal checks report 44 baseline passes and 25 remaining failures; the sanitizer baseline also passes all 44 cases. Long-form Counter64 decoding was addressed in the subsequent fix below.

## Counter64 length decoding fix

Counter64 decoding now consumes the definite-length header before reading contents, including nonminimal long-form lengths with leading zero octets. It rejects indefinite/reserved lengths, empty or oversized contents, negative encodings, and values exceeding 64 bits without changing the stored value. Exact-sized fixtures replace the padded regression input and cover UINT64_MAX.

Normal checks report 46 baseline passes and 24 remaining regression failures; the sanitizer baseline also passes all 46 cases. The pointer-only API still requires a complete input TLV, and parent parser error propagation remains separate work.

## Integer serialization fix

IntegerType now serializes signed Integer32 contents in big-endian order with redundant sign octets removed, without modifying its stored value. Derived Counter32, Gauge and TimeTicks retain a leading zero where required for positive unsigned values. The existing unsigned-long API is retained; wire serialization uses its low 32 bits consistently across host widths. Four regressions are promoted and a boundary/repeated-serialization check is added.

Normal checks report 51 baseline passes and 20 remaining failures; the sanitizer baseline also passes all 51 cases. This supersedes the earlier INTEGER and unsigned-application serialization findings; signed decoding is addressed separately.

## Signed integer decoding fix

IntegerType now sign-extends signed INTEGER contents while keeping application integer values unsigned. Definite-length headers are decoded before the contents; invalid lengths and unsigned values outside the 32-bit range are rejected. The unsigned-long storage API is preserved. Float dispatch interprets signed Integer32 values before scaling, including negative tenths.

Two regressions are promoted; additional checks cover INT32_MIN/MAX, long-form lengths, invalid lengths, and negative float dispatch. Normal checks report 54 baseline passes and 18 remaining failures; the sanitizer baseline also passes all 54 cases. This supersedes the earlier signed decoding findings. Pointer-only input bounds and parent parser error propagation remain separate work.

## Bounded BER parsing fix

Added bounded sequence and response parsing, definite-length validation, child-bound checks, decoder error propagation, and a nesting limit. UDP parsing uses the actual read length and rejects oversized or short reads; hex input checks its buffer capacity. Legacy pointer-only overloads still require complete input. Four regressions are promoted, with new truncated-prefix and oversized-datagram checks. Validation: 59 baseline passes normally and under ASan/UBSan; 14 remaining failures.

## Response structure and empty-list fix

Replaced the packet-size heuristic and unchecked traversal with explicit message/PDU/varbind shape validation. Valid empty lists are accepted, manager dispatch handles them, and repeated parsing clears prior state. Two regressions are promoted and malformed/reused response checks added. Validation: 62 baseline passes normally and under ASan/UBSan; 12 remaining failures.

## Binary strings and exact community matching

OCTET STRING decoding preserves binary data and its length; re-encoding uses that length. Oversized strings are rejected rather than truncated, constructor termination is in bounds, and serialization errors propagate to request sending. Community comparison checks both length and bytes. Legacy direct C-string population remains supported; string callbacks still expose the existing C-string API. Four regressions are promoted and oversized construction/decoding checks added. Validation: 67 baseline passes normally and under ASan/UBSan; eight remaining failures.

## OID encoding and decoding fix

Replaced fixed-width OID arithmetic with integer base-128 loops, including five-octet subidentifiers and the combined first arcs. Decimal formatting is unsigned and bounded, input buffers are unchanged, and malformed/oversized OIDs are rejected. Six regressions are promoted and root/overflow/truncated-subidentifier fixtures added. Validation: 74 baseline passes normally and under ASan/UBSan; two remaining failures.

## Per-binding exception fix

noSuchObject, noSuchInstance, and endOfMibView skip only their binding, preserving its destination while allowing later successful bindings to update. Removed the obsolete whole-response exception error branches. Validation: 75 baseline passes normally and under ASan/UBSan; request-ID correlation remains the final regression.

## Request correlation fix

Successful SNMPGet sends record the request ID, peer, and transport per callback. Matching replies consume that pending request; mismatches, duplicates, and superseded replies cannot update it. Failed beginPacket/write/endPacket calls do not replace pending state. Matching PDU errors retire pending callbacks. Callbacks never sent by SNMPGet retain legacy direct-response behavior. One outstanding request per callback is supported; distinct callbacks remain independent. IDs must not be reused while an older reply could still arrive. Validation: 78 baseline passes normally and under ASan/UBSan; no failing regression cases remain.

## Final validation of the remaining regression fixes

All 78 cases pass through standalone Make and PlatformIO native. All 78 also pass with ASan/UBSan in both runners. The regression group is empty after promotion of the resolved cases. No board or live SNMP agent was used.

## Additional source-review fixes

- `c2f3449`: initialize default community, transport and callback pointers.
- `1602c37`: production manager/request cleanup, shared registration lifetime, and replacement-packet cleanup; test-only cleanup removed.
- `027fa70`: bounded serialization/primitive decoding and request-capacity preflight. Legacy size-free entry points remain caller-responsible.
- `56b2486`: Opaque and exceptions are primitive payloads; unsupported tags are rejected.
- `43835e9`: complete OID registration/dispatch, bounded string/OID callbacks, and binary OCTET STRING/Opaque callbacks.
- `40d5b7b`: configurable concurrent pending requests, no-transmission capacity failures, retransmission, and explicit clearing.
- `756db44`: prevent shallow copies of owning packets; test request rebuild and move lifetime behavior.

Validation: all 85 tests pass in both Make and PlatformIO, normally and with ASan/UBSan. These changes do not add trap dispatch, which remains outside the manager's scope.

## Modern-platform portability and C++ review fixes

The supported scope is now ESP8266/ESP32 and tested modern Arduino variants, not AVR. Fixed debug hex overreads, removed variable-length stack arrays, made header definitions inline across translation units, and added a strict warning-clean C++11 check without exceptions/RTTI. Protocol IDs, ports, and callback values have explicit widths; floats have their own destination pointer. Packet buffers no longer reserve triple capacity, and dispatch no longer allocates throwaway counter objects.

Library object allocations use nothrow allocation with checked failure paths; OID allocation is checked, partially built request trees use temporary unique ownership, and linked-list teardown is iterative. An allocation-failure test sweeps request/parser failure positions and recovery. Internal pending records are private. The undefined response serialization declaration and unused MIN macro were removed. clang-format standardizes source and test style in a separate formatting commit.

Validation: all 89 native cases pass normally and with debug logging, with and without ASan/UBSan. Strict multi-file C++11 passes with warnings as errors and exceptions/RTTI disabled. Both PlatformIO native configurations pass. NodeMCU ESP8266, ESP32, ESP32-C3, and Arduino Nano ESP32 smoke builds compile and link using real Arduino cores. These results are not a hardware runtime or live-agent certification. CI configuration reproduces these checks; hosted CI itself has not been run in this session.
