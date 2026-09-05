# Examples

Open the `.ino` matching its directory name in the Arduino IDE. Select an ESP8266
or ESP32 board, install this library, and set Wi-Fi credentials, target IPv4 address,
and community. The target device must have SNMP enabled and expose the selected MIB.
Use Serial Monitor at 115200 baud. No MIB files are needed on the board.

Before uploading, follow [setup and error recovery](../docs/TROUBLESHOOTING.md).
It explains credentials, device settings, row limits, and how to act on errors.

## Start with the reading you need

Run `Simple_Read` first to confirm the device answers. Then use
`Interface_Traffic` for network counters, `Host_Storage` for NAS/server storage,
`Printer_Supplies` for supplies, or `Multiple_Devices` for several targets.
`Walk_Values` explores a chosen group of readings. The tables below link each sketch.

If terms such as community, OID, MIB, or walk are unfamiliar, read
[getting started](../docs/GETTING_STARTED.md) first. The operation names below
explain what the examples do; they are not prerequisites for running them.

## Operation reference

| Operation | Example | What it demonstrates |
| --- | --- | --- |
| GET / RESPONSE | [Simple_Read](Simple_Read/Simple_Read.ino) | Read uptime and check completion before using the result |
| Concurrent GET | [Multiple_Devices](Multiple_Devices/Multiple_Devices.ino) | Independent queries sharing one UDP client |
| GETNEXT | [Walk_Values](Walk_Values/Walk_Values.ino) | Set the device version to `SNMPVersion::Version1`; walk a subtree one successor at a time |
| GETBULK | [Walk_Values](Walk_Values/Walk_Values.ino) | Default v2c walk fetches multiple successors and streams typed values |
| SET / read-back GET | [Set_Location](Set_Location/Set_Location.ino) | Send `w` to write sysLocation; read back even after a timeout; no automatic write retry |
| SNMPv1 trap | [Receive_Notifications](Receive_Notifications/Receive_Notifications.ino) | Enterprise and generic/specific trap fields |
| SNMPv2c trap | [Receive_Notifications](Receive_Notifications/Receive_Notifications.ino) | Event OID and variable bindings |
| SNMPv2c INFORM / RESPONSE | [Receive_Notifications](Receive_Notifications/Receive_Notifications.ino) | Return true to accept and acknowledge; failed binding reads leave the INFORM unacknowledged |

RESPONSE handling is automatic. GETNEXT and GETBULK are exposed through walks and
tables rather than raw packet construction. The library receives notifications;
it does not originate traps or INFORMs. To try the receiver, configure an agent
to send notifications to the board IP printed at startup, UDP port 162, with the
matching community. Repeated INFORMs may print repeated events. Serial printing is
for demonstration; keep production handlers short.

## Choose a device

| Device | Example | Interpretation |
| --- | --- | --- |
| Router or switch | [Interface_Traffic](Interface_Traffic/Interface_Traffic.ino) | Discovered interface indices, Counter64 with Counter32 fallback; cumulative bytes, not rates |
| Server or NAS | [Host_Storage](Host_Storage/Host_Storage.ino) | Storage descriptions, allocation units, total and used bytes using 64-bit multiplication |
| Printer | [Printer_Supplies](Printer_Supplies/Printer_Supplies.ino) | Compound device/supply indices, known percentages and unknown/some-remaining states |

Storage columns follow [HOST-RESOURCES-MIB](https://www.rfc-editor.org/rfc/rfc2790.html).
Printer levels follow [Printer-MIB](https://www.rfc-editor.org/rfc/rfc3805.html);
for waste receptacles, the level means remaining space. Device support and access
views vary: an empty table does not prove that a device has no disks or supplies.

The new walk and device-table examples run once after connecting. Reset to repeat,
or adapt the periodic scheduling in Simple_Read. Table templates bound retained
rows; increase them only with sufficient RAM. Interface_Traffic reserves 64 rows
because logical interfaces can outnumber physical ports; a full table reports
`CapacityExceeded` and may stop before counter columns are read.
Interface and storage examples use
16-byte index buffers; the printer example uses 24 bytes for compound indices.
These capacities include termination; longer indices cause `CapacityExceeded`. Missing cells remain unavailable,
and capacity errors leave collected rows inspectable. Table columns are queried
sequentially, so their readings are not an atomic snapshot.

### Large NAS storage tables

`Host_Storage` retains up to 16 entries. HOST-RESOURCES-MIB can expose memory,
virtual filesystems, and datasets as well as disks, so a NAS can exceed this limit.
`CapacityExceeded` means the retained table is incomplete; later columns may not
have been read. Increase the template capacity only within the board's RAM budget.
For bounded streaming, use `Walk_Values` with root `.1.3.6.1.2.1.25.2.3.1`;
it prints individual bindings rather than joining them into storage rows.

Storage bytes are allocation units multiplied by block counts using 64-bit
arithmetic. Some agents expose entries with zero allocation units. The helper
rejects those conversions and leaves the output unchanged; the example reports
an unavailable size rather than treating it as a valid zero-byte disk. Other
rows remain usable. Columns are read sequentially and usage can change between
reads, so the results are not an atomic snapshot.

## Value primitives

`Walk_Values::printValue` handles every supported value tag without assuming its
meaning from its size. The default system subtree is deliberately small and will
not return every type. Change the root to a subtree exposed by your agent.

| Wire value | Handling / useful objects |
| --- | --- |
| INTEGER | Signed output; host storage block counts, printer supply levels |
| OCTET STRING | Hex preserves embedded zeros and binary data; device examples display descriptions after `isText()` |
| OBJECT IDENTIFIER | Dotted text, for example sysObjectID |
| IpAddress | Four-byte IPv4 value, distinct from an InetAddress OCTET STRING |
| Counter32 | Unsigned cumulative counter; interface octets |
| Gauge32 / Unsigned32 | Unsigned value; the two names share a wire tag |
| TimeTicks | Unsigned hundredths of a second; sysUpTime |
| Counter64 | Full decimal output without narrowing; v2c interface high-capacity counters |
| Opaque | Raw hex; vendor-specific payload interpretation belongs to the application |
| NULL | Explicit display if returned; request NULL placeholders are generated automatically |
| noSuchObject / noSuchInstance | Reported through result status rather than read as a numeric value |
| endOfMibView | Terminates the walk automatically |

BITS, MAC addresses, DateAndTime, and IPv6 InetAddress values are OCTET STRING
conventions, not additional primitive tags. See [MIB helpers](../docs/QUERY_API.md#common-mib-values)
for checked conversions. `isText()` rejects embedded NULs; it does not validate
encoding or terminal control characters. A real device need not implement an
Opaque object or return NULL in a successful read.

## Compile checks

`pio run -d tests/examples` compiles the sketches on ESP8266 and ESP32; the legacy
Ethernet sketch is checked on ESP8266. These checks also run in CI. They verify
API/build compatibility, not live agent behavior. The native mock-agent suite
separately exercises sparse GETNEXT/GETBULK traversal and protocol error handling.
The older callback examples remain available for applications using that API.
