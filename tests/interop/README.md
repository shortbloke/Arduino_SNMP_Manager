# Independent Net-SNMP wire test

> **PUBLIC** — Tracked in Git and shared in the repository.

Contributor reference: this document covers library validation, not application
setup. To read data from your device, start with [getting started](../../docs/GETTING_STARTED.md).

Run `python3 tests/interop/run.py` with a C++11 compiler and Net-SNMP's `snmpd` and
`snmpget` installed. It starts its own agent on an ephemeral loopback port with a
private configuration and temporary persistent storage, then stops it. It never
uses a production agent or writes a device on the LAN.

The production library encodes and parses messages. A test-transport bridge forwards
complete datagrams over a real host UDP socket; Net-SNMP independently processes
GET, GETNEXT, GETBULK, SET and RESPONSE. The test checks typed v1/v2c GETs, equivalent
walk instance sets, and SET/read-back against the temporary agent. Readiness and
operation timeouts cause failure, as do protocol/status/value mismatches.

This passed locally against Net-SNMP 5.6.2.1 on macOS. It is **host wire interoperability**,
not proof of ESP Wi-Fi/UDP behavior, target memory limits, or hardware compatibility.
Traps/INFORMs and vendor MIBs remain outside this particular test. See the
[physical-board test](../hardware/README.md) for what still needs a real board.

The agent's isolation uses documented [snmpd options](https://www.net-snmp.org/docs/man/snmpd.html)
for explicit configuration, foreground operation, and loopback binding.
