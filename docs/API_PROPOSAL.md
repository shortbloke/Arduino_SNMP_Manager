# Proposed device and query API

Status: design proposal for the next stage of the 2.0 refactor. None of the new
API names below is implemented. These sketches are usage contracts, not runnable
examples; existing examples continue to demonstrate the supported API.

The goal is to let users request information without creating callback objects,
managing destination pointers, building packets, or assigning request IDs. The
primary use case is [issue #28](https://github.com/shortbloke/Arduino_SNMP_Manager/issues/28):
reading traffic counters from many switch interfaces.

## Public model

- `SNMPClient` services a shared UDP transport through `loop()`.
- `SNMPDevice` holds the address, port, version, community, and timeout policy.
- A reusable read operation owns its bounded result storage and pending state.
- `start()` schedules work and returns a status immediately. Network progress
  happens through `manager.loop()`; no user notification function is required.
- `takeCompleted()` consumes a completion event. Results remain readable until
  the next successful start. `status()` distinguishes success, partial success,
  timeout, cancellation, and capacity or protocol failures.

The device stores its own checked copy of the community. Operations borrow their
device, and devices borrow the manager. Declare them in that order and keep them
alive together. Destruction cancels outstanding work without calling user code.
Public owning objects are noncopyable; move support is not required initially.

## Sketch 1: one reading

These sketches assume an already connected network. `WiFiUDP` is illustrative;
the manager should accept any supported Arduino `UDP` implementation. Network
connection and reconnection remain application responsibilities.

```cpp
#include <WiFiUdp.h>
#include <Arduino_SNMP_Manager.h>

WiFiUDP udp;
SNMPClient manager(udp);
SNMPDevice router(manager, "192.168.1.1", "public",
                  SNMPVersion::Version2c);
SNMPRead<SystemUptime> uptime(router);
bool ready = false;
uint32_t lastPoll = 0;

void setup()
{
    Serial.begin(115200);
    // Connect the network here before starting the manager.
    ready = manager.begin().ok();
    if (!ready)
        Serial.println("Could not start SNMP transport");
}

void loop()
{
    if (!ready)
        return;
    manager.loop();

    // Consume completion before starting another read.
    if (uptime.takeCompleted()) {
        if (uptime.result().ok())
            Serial.println(uptime.result().value().seconds());
        else
            Serial.println(uptime.status().message());
    }

    const uint32_t now = millis();
    if (!uptime.pending() && uint32_t(now - lastPoll) >= 5000) {
        lastPoll = now;
        const auto status = uptime.start();
        if (!status.ok())
            Serial.println(status.message());
    }
}
```

`SystemUptime` supplies the scalar's full instance OID and expected TimeTicks
type. Its value retains raw ticks and provides a named conversion to seconds.
Generic numeric-OID reads remain available through a typed descriptor, without
requiring a compiled catalogue of every possible device object.

## Sketch 2: multiple devices

Using the same transport setup as sketch 1, replace its device, operation, and
loop declarations with the following. Each device can have its own community,
version, and destination port. Reading the same OID on two devices requires no
special registration logic.

```cpp
SNMPDevice first(manager, "192.168.1.1", "public",
                 SNMPVersion::Version2c);
SNMPDevice second(manager, "192.168.1.2", "public",
                  SNMPVersion::Version2c);
SNMPRead<SystemUptime> firstUptime(first);
SNMPRead<SystemUptime> secondUptime(second);

void report(const char *label, SNMPRead<SystemUptime> &read)
{
    if (!read.takeCompleted())
        return;
    Serial.print(label);
    Serial.print(": ");
    if (read.result().ok())
        Serial.println(read.result().value().seconds());
    else
        Serial.println(read.status().message());
}

void loop()
{
    if (!ready)
        return;
    manager.loop();
    report("First device", firstUptime);
    report("Second device", secondUptime);

    const uint32_t now = millis();
    if (uint32_t(now - lastPoll) < 5000)
        return;
    lastPoll = now;

    if (!firstUptime.pending()) {
        const auto status = firstUptime.start();
        if (!status.ok())
            Serial.println(status.message());
    }
    if (!secondUptime.pending()) {
        const auto status = secondUptime.start();
        if (!status.ok())
            Serial.println(status.message());
    }
}
```

An unresponsive device must not block another operation. Manager queue exhaustion
is reported by `start()` without creating a partially scheduled request.

## Sketch 3: interface traffic

Using the same transport setup, declare a switch and a bounded interface read.
This operation discovers actual interface indices and joins selected columns by
index. Capacity is the maximum number of interface rows stored, not a statement
that indices run from 1 through 48 or that every row is a physical port.

```cpp
SNMPDevice networkSwitch(manager, "192.168.1.10", "public",
                         SNMPVersion::Version2c);
SNMPInterfaceRead<48> traffic(networkSwitch);

void loop()
{
    if (!ready)
        return;
    manager.loop();

    if (traffic.takeCompleted()) {
        if (!traffic.status().ok())
            Serial.println(traffic.status().message());

        for (const auto &row : traffic.rows()) {
            Serial.print(row.index());
            if (row.name().ok()) {
                Serial.print(" ");
                Serial.print(row.name().value().c_str());
            }
            Serial.println();
            // Feed these checked values into the application's LED logic.
            // A successful row may coexist with failed or missing cells.
            if (row.receivedOctets().ok() && row.sentOctets().ok()) {
                // Each counter exposes its raw value and original width.
                // These are cumulative byte counts, not bytes per second.
            }
        }
    }

    const uint32_t now = millis();
    if (!traffic.pending() && uint32_t(now - lastPoll) >= 5000) {
        lastPoll = now;
        const auto status = traffic.start();
        if (!status.ok())
            Serial.println(status.message());
    }
}
```

The interface helper should request a display name and incoming/outgoing octet
counters. Prefer high-capacity counters when available; explicitly record a
fallback to 32-bit counters and retain their width. Keep rate calculations out of
the initial helper: correct rates also need elapsed time, counter discontinuity
handling, and a policy for ambiguous wraps. Names have a bounded text capacity;
oversized or binary names receive a cell error rather than silent truncation.

A switch can expose more logical interfaces than physical ports. Reaching row
capacity must produce `CapacityExceeded` with the collected rows accessible.
An optional explicit index selection should let applications select the desired
interfaces after discovery.

## Address input and compatibility strategy

Accept both `IPAddress` and dotted IPv4 text in `SNMPDevice`. The beginner form is
`SNMPDevice device(client, "192.168.1.1", "public", SNMPVersion::Version2c)`.
Parse and store the address during construction, retain any configuration error,
and have `start()` return `InvalidAddress` before scheduling work. Never silently
substitute a default address. Hostnames and DNS resolution are a separate future
feature; a text overload must not imply hostname support.

Both target cores expose checked `IPAddress::fromString(const char *)` parsing:
[ESP8266](https://github.com/esp8266/Arduino/blob/master/cores/esp8266/IPAddress.h)
and [ESP32](https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/IPAddress.h).
Use a common validated IPv4 input contract rather than inheriting differences in
what each core accepts. Test four decimal octets, missing/extra octets, out-of-range
values, null/empty input, trailing characters, and rejection of hostnames/IPv6.
For existing code, checked `IPAddress::fromString()` already provides a usable
helper; a new free function is not required just to shorten construction syntax.

Recommendation: introduce a modern API while preserving ordinary use of the
current refactored API. This does not restore source compatibility with every
1.x sketch; the existing migration guide still applies.

- Use the new name `SNMPClient` for the proposed engine. Existing
  `SNMPManager::begin()` and `loop()` return booleans; changing their return types
  to rich status objects would break callers. Separate names let the new API have
  consistent status and ownership semantics without those constraints.
- Preserve ordinary `SNMPManager`, `SNMPGet`, and bounded handler usage, including
  explicit request IDs and polling, with regression and compilation fixtures.
- Share BER encoding, decoding, packet validation, and transport utilities. Build
  the new request scheduler and result routing independently of value callbacks.
  The current manager validates one manager-wide community, finds destinations
  by IP/OID, and tracks pending requests on each callback; these are unsuitable
  as the foundation for per-device credentials and query-owned table results.
- Keep compatibility entry points separate from the beginner documentation. Avoid
  maintaining duplicate protocol parsers or requiring both interfaces to allocate
  their state when only one is used. Measure flash/RAM effects on real toolchains.
- Do not promise compatibility for direct mutation of public callback lists,
  packet pointers, or other implementation fields if those internals change.
  Inventory such changes explicitly before removal and document replacements.
- Do not restore unsafe unbounded buffer writes to obtain compatibility. Existing
  migration requirements for capacities and explicit protocol types remain.
- Initially, old and new engines must not consume the same UDP instance. Mixing
  them on one transport would require a single dispatcher with tested routing;
  two independent `loop()` methods could consume each other's responses.

Compatibility is an implementation target, not a proven property yet. Add fixtures
for normal legacy registration/send/receive/cancellation alongside each new API
feature. Prefer an adapter over the shared engine only when it preserves the old
observable behaviour; do not force the new query model through old callbacks.

## Generic operations beneath the helpers

| Operation | Meaning |
| --- | --- |
| Read one or several objects | Fetch exactly the requested instance OIDs. |
| Read an index range or explicit indices | Append known indices to a column OID; batch exact reads. |
| Read selected table columns | Discover rows and join cells by their actual index. |
| Walk a subtree | Deliver bounded chunks for applications that cannot retain all results. |

Named descriptors should contain an OID, type, and useful units. Ship optional
definitions for common system and interface objects; permit custom descriptors
for vendor data. Runtime MIB parsing is outside the initial scope. Generic table
indices must preserve the complete OID suffix, including composite indices; the
interface helper can expose its integer index directly.

## Required lifecycle and failure semantics

- Constructors do no networking. Invalid configuration is observable at start.
- Starting a pending operation returns `Busy`. A rejected start preserves its
  results and state. An accepted start clears completion and marks all cells
  pending, so an earlier sample cannot appear fresh.
- Each cell carries a checked value and status. Missing objects, type mismatches,
  timeouts, and buffer limits never become a numeric zero or an empty success.
- Each accepted operation reaches one terminal state, including on timeout or
  cancellation. An optional completion notification can be added later and must
  run from `manager.loop()`, not a network interrupt.
- Request correlation includes the remote address and port, transport, version,
  community, and request ID. Late and duplicate replies cannot update a new poll.
- Scheduling is bounded and fair. Packet batching considers encoded sizes and
  response limits. A `tooBig` response reduces the batch; a single value that
  still cannot fit receives an explicit error. All retries have a finite budget.
- Walking must stop at the subtree boundary or end of data, reject nonadvancing
  OIDs, and enforce a deadline and row/byte limits. Use GETNEXT for v1 and GETBULK
  where supported in v2c. Sparse or missing cells do not shift other rows.
- Cancellation removes queued work and releases pending slots. Result storage is
  owned by the operation and valid until its next accepted start or destruction.
- Use C++11, bounded storage, and no exception-dependent control flow. Avoid heap
  churn during polling. Specify and measure each helper's memory footprint on
  ESP8266 and ESP32 before settling its default capacities.

## Implementation and regression plan

Keep functional steps in separate commits, with tests alongside each feature:

1. Device configuration, reusable scalar reads, result/status types, and bounded
   scheduling. Test setup failures, ownership, cancellation, and independent peers.
2. Automatic correlation, timeouts, and bounded retries. Test late/duplicate
   replies, queue pressure, request-ID reuse policy, and `millis()` rollover.
3. Multi-object reads, explicit indices/ranges, and packet batching. Test boundary
   indices, overflow, `tooBig`, partial replies, and a poll containing 96 counters.
4. GETNEXT/GETBULK and selected-column walking. Test sparse/composite indices,
   uneven columns, end-of-table responses, nonadvancing agents, and capacity limits.
5. System descriptors and the interface helper. Test type checking, high-capacity
   counter fallback, name limits, and complete versus partial interface results.
6. Replace these proposed sketches with compiling examples. Extend the ESP8266,
   ESP32, ESP32-C3, and Nano ESP32 compatibility builds; update the migration guide
   and public documentation around the finished API.

Keep the existing low-level API available during implementation. Decide its final
2.0 public surface after the replacement workflows pass their tests; do not make
novice users choose between two equally prominent ways to perform ordinary reads.

## Design references

- [Python SNMP 1.3.0](https://python-snmp.readthedocs.io/1.3.0/index.html): device
  managers with multi-OID reads and returned response objects.
- [EasySNMP](https://easysnmp.readthedocs.io/en/latest/): reusable sessions and
  symbolic names for common objects.
- [Node net-snmp](https://github.com/markabrahams/node-net-snmp#sessiontablecolumns-oid-columns-maxrepetitions-callback):
  selected table columns returned as rows keyed by index.
- [GoSNMP](https://pkg.go.dev/github.com/gosnmp/gosnmp): walking with incremental
  delivery as well as operations that collect all results.
