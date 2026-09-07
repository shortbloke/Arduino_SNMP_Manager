# Ubuntu / Net-SNMP integration test

This opt-in test is read-only and runs the same live transport coverage as the
Windows test against a Net-SNMP agent. It expects the Net-SNMP enterprise
`sysObjectID`, a Counter64 `ifHCInOctets.1` value, v1 GETNEXT and v2c GETBULK
walks of `ifDescr`, and a Counter64 interface traffic table without fallback.

```sh
python3 tests/ubuntu/run.py --host 192.168.200.237 --community public \
  --expect-name ubnt --expect-location 'Sitting on the Dock of the Bay' \
  --expect-services 72
```

Use `UBUNTU_SNMP_HOST` and `UBUNTU_SNMP_COMMUNITY` instead of the first two
options when keeping the connection details out of shell history.
