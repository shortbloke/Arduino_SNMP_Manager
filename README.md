# SNMP Manager for ESP8266 and ESP32

Version 2.0.0-alpha.1 is under development on `main`. It introduces a friendly query API
and intentional API/build changes. Stable 1.x releases remain supported on
[`release/1.x`](https://github.com/shortbloke/Arduino_SNMP_Manager/tree/release/1.x).
Read [MIGRATION.md](MIGRATION.md) before moving an existing sketch to 2.x.

## Start with a device and a query

```cpp
#include <SNMPClient.h>
#include <SNMPTable.h>

// udp is your WiFiUDP or EthernetUDP object, initialized by your sketch.
SNMPClient client(udp);
SNMPDevice router(client, "192.168.1.1", "public", SNMPVersion::Version2c);
SNMPRead<SystemUptime> uptime(router);
```

Configure your Wi-Fi credentials, device address, community and SNMP version.
After bringing up the network, check `client.begin()`, call `client.loop()` often,
and check query status before using returned values. The complete
[Simple Read sketch](examples/Simple_Read/Simple_Read.ino) shows the lifecycle;
the [query API guide](docs/QUERY_API.md) documents exact methods and ownership.

The library supports SNMPv1 and SNMPv2c GET, GETNEXT, GETBULK (v2c), SET, bounded
walks/tables, and receiving traps and v2c INFORMs with acknowledgement. It does not
implement SNMPv3, DNS resolution, runtime MIB parsing, or sending traps/INFORMs.
The [example guide](examples/README.md) maps each supported operation to a sketch.

## Value types and low-level handlers

The friendly API owns returned values. A lower-level `SNMPManager`/`SNMPGet` API
also remains, with these SNMP type names:

| SNMP type | Low-level handler | Destination |
| --- | --- | --- |
| INTEGER | `addIntegerHandler` | `int32_t` |
| INTEGER | `addFloatHandler` | `float`, legacy integer value divided by ten |
| OCTET STRING | `addStringHandler` | Bounded text buffer via `char**` |
| OCTET STRING | `addOctetHandler` | Byte buffer and length |
| OBJECT IDENTIFIER | `addOIDHandler` | Bounded dotted-text buffer |
| Counter32 | `addCounter32Handler` | `uint32_t` |
| Gauge32 | `addGaugeHandler` | `uint32_t` |
| TimeTicks | `addTimestampHandler` | `uint32_t`, hundredths of a second |
| Opaque | `addOpaqueHandler` | Byte buffer and length |
| Counter64 | `addCounter64Handler` | `uint64_t`, SNMPv2c only |

Register handlers once, keep destinations alive, check allocation/send results,
and process only fresh responses. `updateCount()` increments after a successful
write even if its value is unchanged. Tracked duplicates, errors and rejected
values do not increment it. The maintained low-level examples use local `Polling.h`
helpers for complete samples and timeouts. Fixed-width destinations, mandatory
capacities, named versions and ownership changes are detailed in the migration guide.

## Configuration and memory

Arduino and PlatformIO compile the implementation in `src/*.cpp`; 2.x is not
header-only. Apply configuration macros to the application and library together
through build flags. Sketch-local defines are not sufficient. See
[configuration migration](MIGRATION.md#compile-and-link-the-implementation-files).

Use numeric OIDs with the appropriate instance suffix. Interface indices need not
match physical port numbers; scalar instances end in `.0`. Match handler types to
the agent's MIB. Limit packet sizes, outstanding operations and table/walk storage
to suit the board; this is a bounded embedded implementation. See the
[standards and limits](tests/native/RFC_NOTES.md) and [memory profiles](docs/QUERY_API.md).

The examples mark SSID/password, address, community, OID and timing settings.
Counter32 calculations require fresh samples, account for one rollover, and reset
on backwards uptime. They cannot detect multiple wraps or an interface reset that
does not change uptime; use suitable Counter64/discontinuity objects where needed.

## Pin your project's library version

Use an exact release for reproducible builds, or a major-version range if you want
compatible updates. Avoid an unversioned Git URL or a moving branch such as `master`
when your project must remain on the 1.x API. The default `main` branch develops 2.x; `release/1.x` maintains 1.x.
2.0.0 is under development and has not been released.

### Arduino IDE and Arduino CLI

Install **SNMP Manager** from Arduino's community Library Manager. In Arduino IDE,
open **Tools > Manage Libraries**, search for **SNMP Manager**, and select the
required version. For existing 1.x projects, select **1.2.1** (or another compatible
1.x release). There is no need to add a GitHub URL or a custom library index.

Review upgrade prompts instead of accepting a future 2.x upgrade for a 1.x project.
Library Manager installation is shared by sketches using that library directory;
it is not a per-project major-version lock. Record the exact version in your
project's setup instructions.

For scripted Arduino CLI setup, request an exact indexed version:

```sh
arduino-cli lib update-index
arduino-cli lib install "SNMP Manager@1.2.1"
```

New releases can take time to appear in the community index. Refresh the index
and retry if the requested version is not yet available. To use a future 2.x
release, install its exact version intentionally after adapting the sketch;
the CLI command installs a version, not a persistent major-version constraint.

### PlatformIO

Use the **shortbloke/SNMP Manager** registry package in your environment's
`platformio.ini`, alongside its existing board/framework settings:

```ini
lib_deps =
    shortbloke/SNMP Manager@^1.2.1
```

Choose the constraint that suits your project:

| Requirement | `lib_deps` entry | Allowed versions |
| --- | --- | --- |
| Remain on 1.x | `shortbloke/SNMP Manager@^1.1.13` | At least 1.1.13, below 2.0.0 |
| Require this patch, remain on 1.x | `shortbloke/SNMP Manager@^1.2.1` | At least 1.2.1, below 2.0.0 |
| Exact patch | `shortbloke/SNMP Manager@1.2.1` | Only 1.2.1 |
| 2.x opt-in after release | `shortbloke/SNMP Manager@^2.0.0` | At least 2.0.0, below 3.0.0 |

Constraints only work for versions published in the PlatformIO Registry, which is
separate from Arduino's community index. The 2.x constraint is illustrative until
2.0.0 is published; migrate your code before changing to it. See
[PlatformIO dependency configuration](https://docs.platformio.org/en/latest/projectconf/sections/env/options/library/lib_deps.html).

### Optional source installation

If you need a release before it appears in your package index, the
[GitHub releases](https://github.com/shortbloke/Arduino_SNMP_Manager/releases)
provide source archives. This is an optional fallback, not required for normal
Library Manager installation. For a PlatformIO Git dependency, append the exact
tag (for example, `#v1.2.1`) to the repository URL; Git tags do not accept
Registry-style version ranges. Avoid duplicate manual copies in your library
search paths.

## Tests and releases

Run `make -C tests/native check`, `make -C tests/native sanitize`, and
`pio test -e native`. See [native tests](tests/native/README.md),
[embedded builds](tests/embedded/README.md), [wire interoperability](tests/interop/README.md)
and [hardware tests](tests/hardware/README.md) for scope and prerequisites.
Hardware logs describe specific historical runs, not certification of every revision.

CI validates both active major lines. Release preparation updates manifests,
README version references and changelog headings through a PR; publication checks
the exact commit before tagging. See [maintaining and releasing](docs/RELEASING.md).

**Changelog:** [CHANGELOG.md](CHANGELOG.md)

## Projects using this library

I'd love to hear about projects that find this library useful.

- [Broadband Utilisation Display](https://github.com/shortbloke/Broadband_Usage_Display) - An LED display showing broadband upstream and downstream utilisation.
- [Dekatron-speed](https://github.com/elegantalchemist/dekatron-speed) - Uses a Dekatron (1950s era neon counting tube) spinning based on broadband utilisation rate.
- [Wio Terminal - Router Graph LCD](https://github.com/dbzoo/wio_terminal/tree/master/router_graph_lcd) - Uses the SeeedStudio [Wio Terminal](https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/) to plot traffic received and transmitted rates on the integrated display.
- [Wio Terminal - Router Stats LCD](https://github.com/dbzoo/wio_terminal/tree/master/router_stats_lcd) - Uses the SeeedStudio [Wio Terminal](https://wiki.seeedstudio.com/Wio-Terminal-Getting-Started/) to show the current receive and transmit rates on the integrated display.

## Acknowledgements

This project a derived from an [SNMP Agent project](https://github.com/fusionps/Arduino_SNMP). With Manager functionality adapted from work by [Niich's fork](https://github.com/Niich/Arduino_SNMP).

## Embedded development

Use `make -C tests/native check` for normal/debug regressions and strict C++11 multi-file compatibility. The compatibility executable builds with exceptions and RTTI disabled. `pio run -d tests/embedded` builds the real board toolchains; CI runs the same checks.

Source formatting uses clang-format 19.1.7 and the checked-in `.clang-format`. Version 2.0 API changes are documented in the migration guide. Internal pending-request state is private; request summary fields are informational.

Packet buffers use `SNMP_PACKET_LENGTH` directly (512 bytes by default, 1500 on ESP32). Configure this limit as described below if the application requires another value. Registration APIs return null on allocation failure; `addOIDPointer`, `addHandler`, and request building return false on failure. `addHandler` adopts a supplied callback only on success; `addValueToList` consumes a supplied BER child even on failure. Pending sends are not registered until transmission succeeds. Constructors can remain empty after allocation failure, and subsequent operations report failure or retry allocation safely.

### Library compilation and configuration

Public headers contain declarations and small inline operations; the corresponding `src/*.cpp` files implement encoding, decoding, request handling, and callback tracking. Arduino and PlatformIO compile these sources automatically when the library is installed. Custom build systems must compile and link all `src/*.cpp` files. `Arduino_SNMP_Manager.h` remains the umbrella header; individual headers can also be included independently.

Defaults live in `src/SNMPConfig.h`. Apply overrides to **both the application and library sources**, for example in PlatformIO:

```ini
build_flags =
    -DSNMP_PACKET_LENGTH=1024
    -DSNMP_MAX_PENDING_REQUESTS=8
    -DDEBUG
```

The other capacity settings are `SNMP_OCTETSTRING_MAX_LENGTH`, `MAX_OID_LENGTH` (256 bytes by default, including termination), and `SNMP_VALUE_MAX_LENGTH` (1024 bytes per owned query payload by default). See [MIB value helpers](docs/QUERY_API.md#common-mib-values) for checked conversions of common readings. For a shared configuration file, define `SNMP_CONFIG_HEADER` as a quoted header filename in compiler flags and make its include directory available to all sources. Arduino CLI users can pass these `-D` options through `compiler.cpp.extra_flags`.

A `#define` placed only before the include in a sketch no longer configures the separately compiled library. Inconsistent capacity or logging settings produce a linker error mentioning `snmp_detail::BuildConfiguration`; rebuild all sources with the same settings to resolve it. The check uses no heap allocation and performs no I/O.
