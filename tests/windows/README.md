# Windows SNMP system-OID integration test

This is an opt-in, read-only host test for the Microsoft SNMP service. It uses
the library's request encoder and response parser over a real UDP socket; it is
not part of `make test` and never sends an SNMP SET request.

It exercises a multi-value typed GET, v1 GETNEXT streaming walk, v2c GETBULK
streaming walk, and the `SNMPInterfaceRead` table helper. All operations are
read-only. The tested values from the supplied Windows walk are:

- `sysDescr.0` — `.1.3.6.1.2.1.1.1.0` (OCTET STRING)
- `sysObjectID.0` — `.1.3.6.1.2.1.1.2.0` (OID, Microsoft enterprise branch)
- `sysUpTime.0` — `.1.3.6.1.2.1.1.3.0` (TimeTicks)
- `sysContact.0`, `sysName.0`, `sysLocation.0`, and `sysServices.0`
- `ifNumber.0` (INTEGER), `ifHighSpeed.7` (Gauge32), `ifInOctets.7`
  (Counter32), and the target's `ipAdEntAddr` (IpAddress)
- the `ifDescr` interface column through both GETNEXT and GETBULK; their exact
  discovered OID sets must match `ifNumber.0`
- the interface description/inbound/outbound traffic table, including the
  Counter64-to-Counter32 fallback exposed by this Windows agent

The supplied server encodes `ifDescr` values as OCTET STRINGs ending in a NUL
byte. The test deliberately verifies that this binary byte is preserved; it does
not pass those descriptions to `isText()` or treat them as C strings.

Pass the target explicitly; do not place a community string in source control:

```sh
python3 tests/windows/run.py --host 192.168.200.236 --community public \
  --expect-name win2025 --expect-location Proxmox --expect-services 79
```

`WINDOWS_SNMP_HOST` and `WINDOWS_SNMP_COMMUNITY` can replace the first two
options. The three `--expect-*` options are optional; use them to make the test
also detect configuration drift. The service values may legitimately vary by
server configuration.
