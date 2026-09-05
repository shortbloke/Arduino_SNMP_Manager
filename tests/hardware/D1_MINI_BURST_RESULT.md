# D1 Mini notification burst test

Contributor reference: this document covers library validation, not application
setup. To read data from your device, start with [getting started](../../docs/GETTING_STARTED.md).

These are historical results, not hardware tests of the current checkout.
The baseline below predates the flush fix. See the final section for its hardware retest.

Physical-board run on 2026-09-05, using unchanged library commit `6da6ec6` and
this change's burst harness. This evaluates real WiFiUDP reception and INFORM
acknowledgements, not an AsyncUDP implementation. The display firmware was replaced
temporarily; restoration verification is recorded below.

Configuration: user-identified D1 Mini, ESP8266EX, Arduino ESP8266 core 3.1.2,
PlatformIO espressif8266 4.2.1, `d1_mini_burst` environment. Wi-Fi settings were left
at the core defaults. The computer sent independently encoded SNMPv2c traps and
INFORMs to UDP 1162. Each carried the required uptime/trap OID bindings plus an
OCTET STRING payload. The firmware checked sequence IDs and decoded payloads.
The host checked complete acknowledgement values and source endpoints.

Each overload burst was sent once, without INFORM retries. Recovery probes sent
ten paced INFORMs with no simulated loop delay, allowing up to three retries of
unacknowledged IDs. Requested pacing is approximate; actual send durations are in
the local logs. Application delays use `delay()`, which yields to Wi-Fi; they do
not simulate disabling interrupts or starving the network stack.

An initial diagnostic run stopped after a recovery probe received/acknowledged
nine of ten packets without retries. The final harness distinguishes that single
UDP loss from inability to recover by recording initial counts and bounded retry
traffic separately. It also records unexpected empty datagrams separately from
valid acknowledgements.

## Main matrix results

Ranges below cover both payload sizes and both notification types. ACK pairs
are the INFORM results for 32-byte / 256-byte payloads. Traps have no ACK.

| Loop delay | Sent / requested gap | Unique received range | INFORM ACKs (32 / 256) |
| --- | --- | --- | --- |
| 0 ms | 20 / 10 ms | 20–20 | 20 / 20 |
| 0 ms | 100 / 1 ms | 37–39 | 7 / 16 |
| 0 ms | 500 / 0 ms | 130–167 | 16 / 28 |
| 10 ms | 20 / 10 ms | 20–20 | 20 / 20 |
| 10 ms | 100 / 1 ms | 11–17 | 11 / 11 |
| 10 ms | 500 / 0 ms | 39–47 | 39 / 45 |
| 50 ms | 20 / 10 ms | 8–9 | 8 / 8 |
| 50 ms | 100 / 1 ms | 7–7 | 7 / 7 |
| 50 ms | 500 / 0 ms | 7–13 | 9 / 9 |

All 36 bursts and their recovery probes completed. Every final-run recovery
probe received and acknowledged all ten INFORMs without retries. No invalid
payload, unexpected nonempty response, or reset was observed. With no simulated
loop delay, the main matrix sent its paced 20-packet cases over approximately
0.251–0.257 seconds; this is not a simultaneous 20-packet burst.

Minimum sampled free heap was 41,184 bytes; minimum sampled largest free block
was 40,240 bytes. Recovery free heap stayed at 49,312 bytes throughout this final
matrix. This excludes earlier warm-up/diagnostic runs and is not a leak proof.

## Short back-to-back bursts

No application loop delay; 32-byte payloads. Each burst was submitted over
approximately 1–5 ms. These are single observations, not statistical loss rates.

| Sent | Traps received | INFORMs received | INFORMs acknowledged |
| --- | --- | --- | --- |
| 5 | 5 | 5 | 5 |
| 10 | 6 | 7 | 6 |
| 20 | 9 | 8 | 7 |

All six following recovery probes received and acknowledged every INFORM without
retries. No crash or invalid decoded payload was observed. Five-packet reception
succeeded here; the larger short bursts demonstrate that the current setup must
not be described as reliably lossless under bursts.

## Receive-side flush finding

The library calls `UDP::flush()` after reading a packet. In the tested ESP8266
core, `WiFiUDP::flush()` calls `endPacket()`, so after the socket has a transmission
destination this produces unsolicited empty datagrams. The sender observed them
on the wire; they are not counted as INFORM acknowledgements. Both the friendly
client and legacy manager contain receive-side flush calls. This warrants a
separate portable fix and regression checks, followed by another hardware run.
The library was not changed during this test, so the measured results include
this extra traffic.

## Limits and recommendation

These measurements do not certify lossless UDP delivery, locate every packet loss,
or establish a universal maximum rate. There was no packet capture inside the
network stack. This run exercises v2c notifications, not simultaneous manager
queries, v1 traps, the attached display workload, an ESP32, or exhausted heap.
Sampled free heap is not an exact allocation high-water mark or a long soak test.

Keep the portable transport while addressing the demonstrated flush problem.
Frequent client servicing matters; overload losses alone do not establish that
AsyncUDP would solve the problem. Retest short bursts after the fix before deciding
whether bounded processing of several packets per loop is needed.

## Local evidence

Raw logs are ignored by Git and contain the local board address. SHA-256:

- `burst.log`: `c17fffc2f326924419ea2ce2fd0593eef0e93a101e43bc159017b2d238404b01`
- `burst-small.log`: `59db471d33a7d8e8241af9e4891b305db42818408863607abe53aa52eb33e427`
- `burst-initial.log`: `138a72690a9b62a8a1e1d64bc4fb20113bc8f8aeea525c6d9eca409760191da2`

Both final logs end with `done: true`. The initial log is diagnostic and incomplete.

## Firmware restoration and checks

Before uploading, `verify-flash` confirmed that the existing private 4 MB backup
matched the attached board. After both final matrices, the full original image was
written back, the write verification passed, and a separate full-image
`verify-flash` reported a matching digest. The board was then reset into its
original firmware. The backup remains outside the repository.

Both D1 Mini hardware environments compiled successfully. The host encoding/ACK
comparison tests and existing read/walk log-validator tests passed, as did formatting
and whitespace checks. CI now runs the host burst tests and compiles the new burst
environment with the existing hardware profiles. No library implementation changed.

## Flush fix retest

Repeated the short back-to-back matrix on 2026-09-05 with the receive-side flush
fix applied. All burst and recovery cases reported **zero empty datagrams**, zero
invalid payloads, and zero unexpected nonempty replies. Every recovery probe
received and acknowledged all INFORMs; one recovery probe needed one retry.
The host harness now fails on empty datagrams to retain this hardware regression check.

| Sent | Traps received | INFORMs received | INFORMs acknowledged |
| --- | --- | --- | --- |
| 5 | 5 | 5 | 5 |
| 10 | 10 | 6 | 6 |
| 20 | 8 | 9 | 9 |

The extra transmissions are fixed; short-burst loss still occurs. These individual
runs do not establish a throughput improvement. The full pacing/delay matrix was
not repeated for this focused fix. No AsyncUDP or receive scheduling change was made.

Local log: `burst-flush-fixed.log` (ignored). SHA-256:
`740167dd4d1cbff4fb7eee72621b67521cfa0b1c9405d3b6b49488d6acb02d81`.

After the retest, the original full-flash image was restored and a separate
`verify-flash` confirmed its digest before resetting the board. Native regression,
ASan/UBSan, standalone-header/configuration checks, and all five embedded build
profiles passed for the fix.
