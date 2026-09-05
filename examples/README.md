# Examples

Open the `.ino` matching its directory name in the Arduino IDE. Select an ESP8266
or ESP32 board, install this library, and set Wi-Fi credentials, target IPv4 address,
and community. The target device must have SNMP enabled and expose the selected MIB.
Use Serial Monitor at 115200 baud. No MIB files are needed on the board.

## Choose an operation

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
rows; increase them only with sufficient RAM. Interface and storage examples use
16-byte index buffers; the printer example uses 24 bytes for compound indices.
These capacities include termination; longer indices cause `CapacityExceeded`. Missing cells remain unavailable,
and capacity errors leave collected rows inspectable. Table columns are queried
sequentially, so their readings are not an atomic snapshot.

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
