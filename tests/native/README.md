# Native tests

## PlatformIO (primary workflow)

Requires PlatformIO Core (or the PlatformIO VS Code extension), a POSIX host (macOS/Linux), and a C++17 compiler. PlatformIO downloads the native platform and GoogleTest on first use. No board or Arduino SDK is required.

```sh
pio test -e native                                      # all tests
pio test -e native -a "--gtest_filter=Ber.*"              # BER cases only
pio test -e native-sanitize                             # all cases with ASan/UBSan
pio test -e native --without-testing                    # build only
```

ASan (AddressSanitizer) detects memory errors such as out-of-bounds accesses and use-after-free. UBSan (UndefinedBehaviorSanitizer) detects invalid C++ operations such as null-pointer dereferences, signed integer overflow, and misaligned accesses. The sanitizer targets enable both runtime checks. A detected error fails that test while the remaining tests continue. These checks only cover executed code paths; a clean run does not prove the library is free of these defects.

In VS Code, use the integrated terminal or PlatformIO Project Tasks → native → Advanced → Test. This step adds PlatformIO testing only; it does not configure the VS Code Testing sidebar.

PlatformIO reports each case under `Ber`, `Requests`, `Responses`, `Manager`, `Tracking`, `Ownership`, or `Configuration`. Use `-v` for full assertion diagnostics. Names replace spaces/punctuation with underscores; pass an exact `--gtest_filter=Group.case_name` to run one case. The same cases and fixtures are shared with the Make runner below, including child-process crash/timeout isolation. Crashes must remain failures, not successful death tests.

Use `pio test` for the exit status: the GoogleTest executable returns zero as required by PlatformIO’s result parser, while PlatformIO itself returns nonzero for test failures. The build-only command compiles without executing tests. Neither environment runs on an ESP board; POSIX process isolation remains host-only.

## Standalone Make (retained)

Requirements: a POSIX host (macOS or Linux), Make, Python 3 for header checks, and a C++11 compiler (Clang or GCC). No downloads, Arduino SDK, physical devices, sockets, or third-party test framework are needed. Run from the repository root:

```sh
make -C tests/native test          # all behavior groups
make -C tests/native check         # suites, configuration, lifecycle and compatibility checks
make -C tests/native sanitize      # all groups with ASan + UBSan
make -C tests/native clean
```

Override `CXX` to select a compiler. Sanitizers require compiler/runtime support. Fixed bugs remain ordinary regression tests in their behavior group; there is no separate baseline or unresolved-defect group. The obsolete `regressions` and `sanitize-regressions` targets have been removed.

The Make build compiles sources into separate object files, links the library as an archive, and tracks included headers with generated dependencies. Use a separate `BUILD` directory when changing compiler flags; normal, debug, sanitizer, configuration, leak, and strict compatibility builds already use separate directories.

## Source organization

- `cases/`: BER, requests, responses, manager integration, tracking, and ownership tests.
- `support/fixtures.*`: independent wire fixtures and shared test helpers.
- `support/allocations.cpp`: shared allocation-failure injection.
- `support/registry.*`: registers all behavior groups for either runner.
- `support/isolation.*`: shared child-process execution and failure diagnostics.
- `runner.cpp`: standalone reporting; `../platformio/test_snmp/test_main.cpp`: GoogleTest adapter.
- `stubs/`: host Arduino, IPAddress, and UDP implementations.
- `compatibility/`: strict multi-file compilation and header/configuration link checks.
- `lifecycle/`: direct execution of ownership and response cases for exit-time leak checking.

PlatformIO compiles the same case, support, and stub sources through `../platformio/build_shared.py`. Neither runner includes implementation `.cpp` files.

See [protocol references](RFC_NOTES.md) for the standards behind test expectations and the distinction between protocol requirements and library conventions.

## Coverage

- Independent BER fixtures for integer, unsigned application types, Counter64, null, IPv4, enterprise OIDs, strings, and nested sequences.
- String decode lengths 0, 1, 127, 128, 255, 256, 257, and 1023; serialization checks on both sides of long-form transitions.
- Response version/community/request/error metadata, multiple bindings, and wrong top-level/version types.
- Exact GetRequest bytes for v1 and v2, default/custom destination ports, missing UDP, endPacket failure, and callback list order/clearing.
- Manager UDP replacement, idle polling, packet reads/flushes, IP/OID matching, integer/counter/gauge/timestamp updates, and rejection of wrong community, peer, and callback type. Exceptions are parsed as valid per-binding results; endOfMibView has a separate traversal test.
- Long OIDs (52 and 124 characters) through request/response dispatch, same-OID routing across devices, and recovery after an unregistered OID response. Four-octet and ten-digit OID checks cover encoding and decoding boundaries.

`support/fixtures.cpp` provides an independent TLV fixture builder rather than the production serializer to construct responses and expected request bytes. This prevents matching encoder/decoder bugs from making packet comparisons pass. Each case runs in a separate child process with a five-second alarm. Assertions, signals, sanitizer aborts, and timeouts count as failures; the parent continues with the next case. Child processes use `_exit`, so this runner does not perform exit-time leak checking. ASan/UBSan still check executed memory accesses and undefined behavior.

## RFC-based additions

- Accept nonminimal definite-length encodings for strings and response wrappers.
- Parse v1 noSuchName metadata and distinguish Get exceptions from traversal exceptions.
- Verify multi-OID request order on the wire and receipt of a 484-byte response.
- Check signed integers, unsigned sign-padding, Counter64, embedded-zero strings, OID roots and maximum subidentifiers.
- Exercise short and long-community tooBig responses with empty lists, PDU-error suppression, mixed exception/success results, and outstanding request-ID matching.

The lifecycle, float scaling, and C-string termination tests are explicitly labeled as library conventions. Exact wire comparisons specify this encoder’s chosen output, not the only BER representation a decoder may accept. Request tracking covers concurrent requests per callback, successful-send registration, duplicate replies, independent callbacks, capacity exhaustion, and explicit cancellation. Callbacks never included in a successful send retain legacy direct-response handling.

Additional cases in `cases/` cover sequence length boundaries and sibling alignment, Counter64 long-form lengths, malformed child lengths, signed callbacks, UDP bind failures, community matching, incomplete responses, and destruction behavior. These are part of the normal behavior groups.

## Boundaries and limitations

The stubs implement only the Arduino/UDP methods used by the library. They cannot validate board compilation, real UDP delivery, fragmentation, Wi-Fi behavior, timing, or device interoperability. The hex parser uses no input-sized stack array. Strict C++11 compatibility is checked separately, with exceptions and RTTI disabled.

Native `unsigned long` may be 64 bits while Arduino targets commonly use 32 bits; protocol-facing APIs use fixed-width types. Real ESP builds complement the native checks; AVR is outside the supported scope. The fake transport supports bind, beginPacket, short-write, and endPacket failure injection.

Tests use production initialization and destructors, and verify shared registration lifetime, request rebuilding, and move construction. Caller-owned destinations and transports must remain valid while operations use them. The separate lifecycle executable runs ownership and response cases without child-process isolation.

All BER types expose capacity-aware serialization and bounded decoding. Tests exercise short buffers and oversized request rejection. Legacy calls without sizes retain caller responsibility; they cannot infer allocation sizes. Custom BER subclasses must implement the new capacity-aware virtual signatures.

The default printable OID buffer remains smaller than the protocol's maximum possible OID representation. Full protocol-size OIDs, exhaustive boundary combinations, and target hardware behavior need further work. Allocation-failure injection checks partial-build cleanup and recovery; it does not reproduce every possible heap condition. C-string callbacks support explicit capacities; binary callbacks preserve OCTET STRING and Opaque payloads and report their lengths. The query client also covers trap reception and INFORM acknowledgement.

## Compatibility checks

`make -C tests/native compatibility` compiles separate source files with strict C++11, warnings as errors, and exceptions/RTTI disabled. `make -C tests/native debug` runs the shared suite with DEBUG and DEBUG_BER. To run both normal and debug suites under sanitizers:

```sh
make -C tests/native BUILD=build-sanitize CXXFLAGS='-std=c++11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all' check
```

See [embedded builds](../embedded/README.md) for real ESP8266, ESP32, ESP32-C3, and Arduino Nano ESP32 compile/link checks. These do not require attached hardware.

The native builds compile and link the library's `src/*.cpp` sources alongside the tests. `make check` also compiles each public header independently, verifies that matching custom settings link, and checks that inconsistent capacity or logging settings fail to link. These checks use Python 3 and the configured C++ compiler. They also cover the shared `SNMP_CONFIG_HEADER` option. The Arduino serial stub has one shared definition, so logging checks exercise calls from the compiled library.

`make -C tests/native configuration` runs the configuration group against separately compiled library archives with smaller and larger limits; it is also part of `make check`. The cases exercise exact receive limits, oversized request rejection before transmission, pending-slot exhaustion/reuse, and octet/opaque/OID capacity boundaries. The normal suite runs these cases with the default settings. To select a standalone group, use `tests/native/build/tests --group Configuration`; an unknown or empty group fails.

## Lifecycle and leak checks

`make -C tests/native lifecycle` runs the shared ownership, response, and MIB cases directly in one process, including allocation-failure recovery, repeated packet building, parser reuse, and destruction. It returns normally so destructors and exit-time leak checking can run; assertions or crashes fail the executable. A process timeout prevents a hang. This target is also part of `make check`.

Run `make -C tests/native leaks` for explicit leak detection. On Linux it uses AddressSanitizer/UndefinedBehaviorSanitizer with LeakSanitizer enabled; CI runs this target on Ubuntu. On macOS it uses the system `leaks --atExit` tool with an unsanitized debug build. Tool failures and detected leaks fail the target. These checks cover the exercised lifecycles; they do not prove all allocation paths are leak-free.

## Device and query regressions

The `Client` group covers checked address/configuration handling, response matching,
owned results, timeout/retry and cancellation, large query batching, SET encoding,
walk progression, sparse table joins and counter fallback, and trap/INFORM handling.
Run `make test` for the complete suite or `./build/tests --group Client` after building.
The existing manager/handler regression groups remain active for compatibility.

## Mock SNMP agent

The `Agent` regression group uses `support/mock_agent.h` and `mock_agent.cpp` to
answer actual outgoing GET, GETNEXT, and GETBULK datagrams from an ordered OID
fixture database. Its BER reader, numeric OID ordering, and traversal logic are
independent of the library under test. Bindings use independent fixture encoding;
no library serializer is used to generate agent responses.

The scenarios verify complete sparse walks, multiple GETBULK pages, non-repeaters
and repetition ordering, subtree boundaries, version-specific end-of-MIB responses,
composite indices, uneven table columns, and interface Counter64/Counter32 fallback.
Fault cases cover packet-size limits, dropped/truncated replies, duplicates,
nonadvancing OIDs, and result capacity exhaustion. The same group runs under small
and large library configurations.

After building, run `./build/tests --group Agent`. To create another scenario,
use `MockAgent::put(oid, primitiveTLV)`, start a client operation, then alternate
`client.loop(now)` and `agent.service(udp)`. `exchanges` records request IDs, PDU
parameters, requested/returned OIDs, errors, and response bytes for assertions.
The service helper requires one outgoing datagram per step; it is not a concurrent
multi-device network emulator. GET missing-instance inference treats the final arc
as an instance index; walking and table joins support complete composite indices.
The mock is test-only and does not replace real-agent interoperability testing.


The `MIB` group exercises IF-MIB descriptions beyond the former inline capacity,
TCP-MIB IPv6 compound instance OIDs, payload ownership across polls, allocation
failure, and checked storage, sensor, printer, truth-value, MAC, and IPv6 conversions.
It runs with the default and custom configurations; smaller configured limits are
checked for explicit rejection. These are synthetic fixtures based on MIB definitions,
not interoperability checks against physical agents. MIB payload lifetimes also run
in the lifecycle/leak executable.

The [example guide](../../examples/README.md) maps protocol operations and value types
to compiling sketches, including server storage, printer supplies, SET read-back,
and notification reception. The examples build matrix checks each new sketch on
ESP8266 and ESP32 through `pio run -d tests/examples`.

Memory regressions cover payload release on accepted walk/table restarts, retention
on rejected starts, and compact index bounds without truncation. See the
[independent Net-SNMP test](../interop/README.md) and
[physical-board procedure and memory measurements](../hardware/README.md).


### Low-heap failure and recovery

The `Heap` group exhausts the library's nonthrowing C++ allocations after each
successive allocation in representative response and notification paths, stopping
only when the complete path runs without an injected failure. A bounded sweep that
never reaches that point fails the test. It checks:

- Query decoder failures never publish successful incomplete values; result-storage
  failure reports `AllocationFailure`, and the same operation can be restarted.
- Walk/table cells either contain the complete value or an explicit failure status;
  recovery does not require reconstructing the client.
- INFORM binding-read failures are not acknowledged. A sender retry succeeds when
  allocations recover, including after acknowledgement encoding could not finish.
- A simulated maximum contiguous block rejects large payloads while allowing small
  allocations; failed replacement preserves existing values and shared snapshots.
- Sending a prepared SET needs no library heap allocation. If its reply cannot be
  decoded under sustained exhaustion, it times out without an automatic duplicate
  write. An explicit subsequent start remains possible.

Run `make -C tests/native test`, or select `tests/native/build/tests --group Heap`
after building. The group also runs with custom capacities, sanitizer builds, the
PlatformIO runner, and the in-process lifecycle/leak target. This checks cleanup of
partially built objects as well as visible status and recovery.

Injection affects `new (std::nothrow)` and the library's matching direct allocation
calls, not the test runner's containers. It does not exhaust the host heap, inject
legacy C `malloc` failures, or reproduce Wi-Fi/RTOS allocation failures. The maximum
block model exercises a fragmentation-related failure condition rather than a real
fragmented ESP heap. Physical heap sampling and soak testing remain complementary;
these tests cannot establish that every possible low-memory condition is crash-free.
