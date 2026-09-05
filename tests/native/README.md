# Native tests

## PlatformIO (primary workflow)

Requires PlatformIO Core (or the PlatformIO VS Code extension), a POSIX host (macOS/Linux), and a C++17 compiler. PlatformIO downloads the native platform and GoogleTest on first use. No board or Arduino SDK is required.

```sh
pio test -e native                                      # all tests
pio test -e native -a "--gtest_filter=Baseline.*"         # passing baseline
pio test -e native -a "--gtest_filter=Regression.*"       # known regressions
pio test -e native-sanitize                             # all cases with ASan/UBSan
pio test -e native --without-testing                    # build only
```

ASan (AddressSanitizer) detects memory errors such as out-of-bounds accesses and use-after-free. UBSan (UndefinedBehaviorSanitizer) detects invalid C++ operations such as null-pointer dereferences, signed integer overflow, and misaligned accesses. The sanitizer targets enable both runtime checks. A detected error fails that test while the remaining tests continue. These checks only cover executed code paths; a clean run does not prove the library is free of these defects.

In VS Code, use the integrated terminal or PlatformIO Project Tasks → native → Advanced → Test. This step adds PlatformIO testing only; it does not configure the VS Code Testing sidebar.

PlatformIO reports each case under `Baseline` or `Regression`. Use `-v` for full assertion diagnostics. Names replace spaces/punctuation with underscores; pass an exact `--gtest_filter=Group.case_name` to run one case. The same cases and fixtures are shared with the Make runner below, including child-process crash/timeout isolation. Crashes must remain failures, not successful death tests.

Use `pio test` for the exit status: the GoogleTest executable returns zero as required by PlatformIO’s result parser, while PlatformIO itself returns nonzero for test failures. The build-only command compiles without executing tests. Neither environment runs on an ESP board; POSIX process isolation remains host-only.

## Standalone Make (retained)

Requirements: a POSIX host (macOS or Linux), Make, and a C++11 compiler (Clang or GCC). No downloads, Arduino SDK, physical devices, sockets, or third-party test framework are needed. Run from the repository root:

```sh
make -C tests/native test          # baseline tests
make -C tests/native regressions   # regression tests
make -C tests/native check         # normal/debug groups and strict C++11 multi-file check
make -C tests/native sanitize      # baseline with ASan + UBSan, fatal diagnostics
make -C tests/native sanitize-regressions # regressions with ASan + UBSan
make -C tests/native clean
```

Override `CXX` to select a compiler. Sanitizers require compiler/runtime support. To run regressions under sanitizers after building the sanitizer target:

```sh
tests/native/build-sanitize/tests --regressions
```

The regression target returns a nonzero status when defects remain. Failures are not converted into passes or skipped automatically. A passing baseline alone does **not** indicate the library is defect-free. When fixing a defect, remove its `true` regression flag to promote the test to the baseline.

The previously failing cases have been promoted to baseline. The regression group is retained for future unresolved defects and may be empty.

See [RFC review notes](RFC_NOTES.md) for the standards basis and additional coverage needed for protocol conformance.

## Coverage

- Independent BER fixtures for integer, unsigned application types, Counter64, null, IPv4, enterprise OIDs, strings, and nested sequences.
- String decode lengths 0, 1, 127, 128, 255, 256, 257, and 1023; serialization checks on both sides of long-form transitions.
- Response version/community/request/error metadata, multiple bindings, and wrong top-level/version types.
- Exact GetRequest bytes for v1 and v2, default/custom destination ports, missing UDP, endPacket failure, and callback list order/clearing.
- Manager UDP replacement, idle polling, packet reads/flushes, IP/OID matching, integer/counter/gauge/timestamp updates, and rejection of wrong community, peer, and callback type. Exceptions are parsed as valid per-binding results; endOfMibView has a separate traversal test.
- Long OIDs (52 and 124 characters) through request/response dispatch, same-OID routing across devices, and recovery after an unregistered OID response. Four-octet and ten-digit OID checks cover encoding and decoding boundaries.
- Historical findings and fixes are described in [REVIEW.md](REVIEW.md).

`tests.cpp` uses an independent TLV fixture builder rather than the production serializer to construct responses and expected request bytes. This prevents matching encoder/decoder bugs from making packet comparisons pass. Each case runs in a separate child process with a five-second alarm. Assertions, signals, sanitizer aborts, and timeouts count as failures; the parent continues with the next case. Child processes use `_exit`, so this runner does not perform exit-time leak checking. ASan/UBSan still check executed memory accesses and undefined behavior.

## RFC-based additions

- Accept nonminimal definite-length encodings for strings and response wrappers.
- Parse v1 noSuchName metadata and distinguish Get exceptions from traversal exceptions.
- Verify multi-OID request order on the wire and receipt of a 484-byte response.
- Check signed integers, unsigned sign-padding, Counter64, embedded-zero strings, OID roots and maximum subidentifiers.
- Exercise short and long-community tooBig responses with empty lists, PDU-error suppression, mixed exception/success results, and outstanding request-ID matching.

The lifecycle, float scaling, and C-string termination tests are explicitly labeled as library conventions. Exact wire comparisons specify this encoder’s chosen output, not the only BER representation a decoder may accept. Request tracking covers concurrent requests per callback, successful-send registration, duplicate replies, independent callbacks, capacity exhaustion, and explicit cancellation. Callbacks never included in a successful send retain legacy direct-response handling.

Additional cases in `tests.cpp` cover sequence length boundaries and sibling alignment, Counter64 long-form lengths, malformed child lengths, signed callbacks, UDP bind failures, community matching, incomplete responses, and destruction behavior. These use the standard baseline and regression groups. The [review notes](REVIEW.md#issue-66-comparison) record the upstream comparison that suggested this coverage.

## Boundaries and limitations

The stubs implement only the Arduino/UDP methods used by the library. They cannot validate board compilation, real UDP delivery, fragmentation, Wi-Fi behavior, timing, or device interoperability. The hex parser uses no input-sized stack array. Strict C++11 compatibility is checked separately, with exceptions and RTTI disabled.

Native `unsigned long` may be 64 bits while Arduino targets commonly use 32 bits; protocol-facing APIs use fixed-width types. Real ESP builds complement the native checks; AVR is outside the supported scope. The fake transport supports bind, beginPacket, short-write, and endPacket failure injection.

Tests use production initialization and destructors, and verify shared registration lifetime, request rebuilding, and move construction. Caller-owned destinations and transports must remain valid while operations use them. Child-process execution still limits leak-checking coverage.

All BER types expose capacity-aware serialization and bounded decoding. Tests exercise short buffers and oversized request rejection. Legacy calls without sizes retain caller responsibility; they cannot infer allocation sizes. Custom BER subclasses must implement the new capacity-aware virtual signatures.

The default printable OID buffer remains smaller than the protocol's maximum possible OID representation. Full protocol-size OIDs, exhaustive boundary combinations, and target hardware behavior need further work. Allocation-failure injection checks partial-build cleanup and recovery; it does not reproduce every possible heap condition. C-string callbacks support explicit capacities; binary callbacks preserve OCTET STRING and Opaque payloads and report their lengths. Trap dispatch remains outside this Get-oriented manager's scope.

## Compatibility checks

`make -C tests/native compatibility` compiles separate source files with strict C++11, warnings as errors, and exceptions/RTTI disabled. `make -C tests/native debug` runs the shared suite with DEBUG and DEBUG_BER. To run both normal and debug suites under sanitizers:

```sh
make -C tests/native BUILD=build-sanitize CXXFLAGS='-std=c++11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all' check
```

See [embedded builds](../embedded/README.md) for real ESP8266, ESP32, ESP32-C3, and Arduino Nano ESP32 compile/link checks. These do not require attached hardware.

The native builds compile and link the library's `src/*.cpp` sources alongside the tests. `make check` also compiles each public header independently, verifies that matching custom settings link, and checks that inconsistent capacity or logging settings fail to link. These checks use Python 3 and the configured C++ compiler. They also cover the shared `SNMP_CONFIG_HEADER` option. The Arduino serial stub has one shared definition, so logging checks exercise calls from the compiled library.
