# Native tests

## PlatformIO (primary workflow)

Requires PlatformIO Core (or the PlatformIO VS Code extension), a POSIX host (macOS/Linux), and a C++17 compiler. PlatformIO downloads the native platform and GoogleTest on first use. No board or Arduino SDK is required.

```sh
pio test -e native                                      # all tests; currently fails
pio test -e native -a "--gtest_filter=Baseline.*"         # passing baseline
pio test -e native -a "--gtest_filter=Regression.*"       # known regressions
pio test -e native-sanitize                             # all cases with ASan/UBSan
pio test -e native --without-testing                    # build only
```

ASan (AddressSanitizer) detects memory errors such as out-of-bounds accesses and use-after-free. UBSan (UndefinedBehaviorSanitizer) detects invalid C++ operations such as null-pointer dereferences, signed integer overflow, and misaligned accesses. The sanitizer targets enable both runtime checks. A detected error fails that test while the remaining tests continue. These checks only cover executed code paths; a clean run does not prove the library is free of these defects.

In VS Code, use the integrated terminal or PlatformIO Project Tasks → native → Advanced → Test. This step adds PlatformIO testing only; it does not configure the VS Code Testing sidebar.

PlatformIO reports each case under `Baseline` or `Regression`. Use `-v` for full assertion diagnostics. Names replace spaces/punctuation with underscores; pass an exact `--gtest_filter=Group.case_name` to run one case. The same cases and fixtures are shared with the Make runner below, including child-process crash/timeout isolation. Crashes must remain failures, not successful death tests.

Use `pio test` for the exit status: the GoogleTest executable returns zero as required by PlatformIO’s result parser, while PlatformIO itself returns nonzero for test failures. The build-only command succeeds despite existing regressions. Neither environment runs on an ESP board; POSIX process isolation remains host-only.

## Standalone Make (retained)

Requirements: a POSIX host (macOS or Linux), Make, and a C++11 compiler (Clang or GCC). No downloads, Arduino SDK, physical devices, sockets, or third-party test framework are needed. Run from the repository root:

```sh
make -C tests/native test          # baseline tests
make -C tests/native regressions   # regression tests; currently FAILS
make -C tests/native check         # both groups; currently FAILS
make -C tests/native sanitize      # baseline with ASan + UBSan, fatal diagnostics
make -C tests/native sanitize-regressions # regressions with ASan + UBSan; FAILS
make -C tests/native clean
```

Override `CXX` to select a compiler. Sanitizers require compiler/runtime support. To run regressions under sanitizers after building the sanitizer target:

```sh
tests/native/build-sanitize/tests --regressions
```

The regression target returns a nonzero status when defects remain. Failures are not converted into passes or skipped automatically. A passing baseline alone does **not** indicate the library is defect-free. When fixing a defect, remove its `true` regression flag to promote the test to the baseline.

See [RFC review notes](RFC_NOTES.md) for the standards basis and additional coverage needed for protocol conformance.

## Coverage

- Independent BER fixtures for integer, unsigned application types, Counter64, null, IPv4, enterprise OIDs, strings, and nested sequences.
- String decode lengths 0, 1, 127, 128, 255, 256, 257, and 1023; serialization checks on both sides of long-form transitions.
- Response version/community/request/error metadata, multiple bindings, and wrong top-level/version types.
- Exact GetRequest bytes for v1 and v2, default/custom destination ports, missing UDP, endPacket failure, and callback list order/clearing.
- Manager UDP replacement, idle polling, packet reads/flushes, IP/OID matching, integer/counter/gauge/timestamp updates, and rejection of wrong community, peer, and callback type. Exceptions are parsed as valid per-binding results; endOfMibView has a separate traversal test.
- Long OIDs (52 and 124 characters) through request/response dispatch, same-OID routing across devices, and recovery after an unregistered OID response. Four-octet and ten-digit OID checks include known encoding/decoding failures.
- Regression cases described in [REVIEW.md](REVIEW.md).

`tests.cpp` uses an independent TLV fixture builder rather than the production serializer to construct responses and expected request bytes. This prevents matching encoder/decoder bugs from making packet comparisons pass. Each case runs in a separate child process with a five-second alarm. Assertions, signals, sanitizer aborts, and timeouts count as failures; the parent continues with the next case. Child processes use `_exit`, so this runner does not perform exit-time leak checking. ASan/UBSan still check executed memory accesses and undefined behavior.

## RFC-based additions

- Accept nonminimal definite-length encodings for strings and response wrappers.
- Parse v1 noSuchName metadata and distinguish Get exceptions from traversal exceptions.
- Verify multi-OID request order on the wire and receipt of a 484-byte response.
- Expose signed integer, unsigned sign-padding, Counter64, embedded-zero string, OID root and maximum-subidentifier defects.
- Exercise short and long-community tooBig responses with empty lists, PDU-error suppression, mixed exception/success results, and outstanding request-ID matching.

The lifecycle, float scaling, and C-string termination tests are explicitly labeled as library conventions. Exact wire comparisons specify this encoder’s chosen output, not the only BER representation a decoder may accept. The request-ID regression expresses missing manager/request integration; passing it requires production design work, not just a parser correction.

Additional cases in `tests.cpp` cover sequence length boundaries and sibling alignment, Counter64 long-form lengths, malformed child lengths, signed callbacks, UDP bind failures, community matching, incomplete responses, and destruction behavior. These use the standard baseline and regression groups. The [review notes](REVIEW.md#issue-66-comparison) record the upstream comparison that suggested this coverage.

## Boundaries and limitations

The stubs implement only the Arduino/UDP methods used by the headers. They cannot validate board compilation, real UDP delivery, fragmentation, Wi-Fi behavior, timing, or device interoperability. GNU C++11 mode accommodates the existing variable-length array in `testParsePacket`.

Native `unsigned long` may be 64 bits while Arduino targets commonly use 32 bits; these tests do not establish AVR/ESP integer-width compatibility. The fake transport always accepts beginPacket/write and supports begin (UDP bind) and endPacket failure injection.

Test fixtures explicitly initialize the manager's `_udp` pointer and release callback/request allocations because production initialization/ownership is incomplete. Consequently these tests do not certify default-constructor safety or production lifetime/leak behavior. Sanitizers passing does not negate the compiler's out-of-bounds warning: writes into object padding can escape runtime detection.

Incomplete UDP responses and malformed child lengths have regression coverage; broader truncation coverage, oversized datagrams, exhaustive signed boundaries and OID roots, 128-subidentifier OIDs, general maximum-length string handling, and repeated parser/build ownership behavior still need coverage. Empty lists, negative integers, a non-.1.3 OID root, and indefinite lengths now have isolated regressions. Current parsing APIs accept pointers without buffer sizes, so their memory safety cannot be inferred from valid-packet tests. No production source was changed in this test addition.
