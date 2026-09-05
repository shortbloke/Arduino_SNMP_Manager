# Proposed device and query API

Status: design proposal for the next stage of the 2.0 refactor. None of the new
API names below is implemented. These sketches are usage contracts, not runnable
examples; existing examples continue to demonstrate the supported API.

The goal is to let users request information without creating callback objects,
managing destination pointers, building packets, or assigning request IDs. The
primary use case is [issue #28](https://github.com/shortbloke/Arduino_SNMP_Manager/issues/28):
reading traffic counters from many switch interfaces.

## Public model

- `SNMPManager` services a shared UDP transport through `loop()`.
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
SNMPManager manager(udp);
SNMPDevice router(manager, IPAddress(192, 168, 1, 1), "public",
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
SNMPDevice first(manager, IPAddress(192, 168, 1, 1), "public",
                 SNMPVersion::Version2c);
SNMPDevice second(manager, IPAddress(192, 168, 1, 2), "public",
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
SNMPDevice networkSwitch(manager, IPAddress(192, 168, 1, 10), "public",
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
