# Terms used in this library

You do not need to memorise these terms to read a device. Begin with
[getting started](GETTING_STARTED.md), then use this page when a document, example,
or error message uses an unfamiliar word.

## Devices and networking

| Term | Meaning here |
| --- | --- |
| SNMP — Simple Network Management Protocol | A set of rules for asking a device for information and receiving its answers. The device must support and enable it. |
| Protocol | Rules that two programs follow to exchange information. |
| Manager / client | The program asking for information: your sketch using this library. |
| Agent | The SNMP software answering on the router, switch, server, or printer. |
| Query / request | A question sent to the device, usually asking for one or more readings. |
| Response / reply | The device's answer. An answer can contain values or an error. |
| Polling | Asking for updated readings repeatedly, for example every five seconds. |
| IP — Internet Protocol; IPv4 | The addressing system used here. An IPv4 address identifies a network destination using four numbers, such as `192.168.1.10`. |
| UDP — User Datagram Protocol | How this library sends individual network messages. Delivery is not guaranteed, so a request can time out and a read may need retrying. The library handles this for you. |
| Packet / datagram | One network message. Packet capacity limits how many bytes it can contain. |
| Port | A numbered destination for a service on a device. SNMP reads normally go to port 161; incoming event messages commonly use 162. It is not a physical socket or switch port. |
| Socket / transport | The software endpoint used to send and receive messages. `WiFiUDP` provides this over Wi-Fi; `EthernetUDP` provides it over wired Ethernet. |
| Community string | An access string configured on the device for SNMPv1/v2c. It is separate from the Wi-Fi password. |
| Credentials | Access details, such as a Wi-Fi password or SNMP community string. |
| SSID — Service Set Identifier | Your Wi-Fi network's name; enter it in the example's `ssid` setting. |
| DNS — Domain Name System | Looks up network addresses from names. This library needs a numeric IPv4 address instead. |
| NAS — Network Attached Storage | A device providing file storage over a network, such as TrueNAS. |
| Interface | A device's network connection. It can be a physical port or a software-created connection, so there can be more interfaces than sockets on the case. |
| Firmware | The software installed on your board or device. Uploading a sketch replaces the board's current program. |

## Readings and their organisation

| Term | Meaning here |
| --- | --- |
| OID — Object Identifier | A numeric address for information, such as `.1.3.6.1.2.1.1.3.0` for uptime. |
| MIB — Management Information Base | A catalogue describing readings, their OIDs, types, and units. You consult it; the board does not need the MIB file. |
| Scalar | One reading, such as a device name. Its instance normally ends in `.0`. |
| Instance | The particular value to read: a scalar's `.0`, or a table column followed by its row index. |
| Table / row / column | Repeated records. A row can describe one interface; columns can be its name and traffic counts. |
| Index / suffix | The final part of a table OID identifying a row. It may contain several numbers and need not be consecutive. |
| Sparse / non-contiguous indices | Row numbers have gaps, for example 1, 7, and 65. The library discovers the actual indices. |
| Composite / compound index | Several numbers together identify a row, such as a printer number followed by a supply number. |
| Root / subtree | A starting OID and the related OIDs beneath it. A walk visits those available readings. |
| Walk / traversal | Asking for successive available OIDs rather than knowing each instance beforehand. |
| Streaming | Processing each returned value as it arrives instead of keeping the whole collection in memory. |
| Binding / VarBind | One OID paired with its returned value or error in a message. |
| Type / primitive | How a value is represented: for example, an INTEGER, text bytes, or a counter. Its meaning and units come from the device's MIB. |
| Octet / byte | Eight bits of data. An OCTET STRING is a sequence of bytes; it is not necessarily readable text. |
| Bit / signed / unsigned | A bit is one binary digit. A signed number can be negative; an unsigned number cannot. `int32_t` is signed 32-bit storage; `uint32_t` and `uint64_t` are unsigned 32/64-bit storage. |
| Hex / hexadecimal | A way of displaying bytes using digits 0–9 and letters A–F. The walk example uses it to preserve binary values that are not readable text. |
| Counter32 / Counter64 | Accumulating nonnegative totals stored in 32 or 64 bits. The larger format holds much larger totals before wrapping. These are not rates. |
| Wrap / rollover | A counter reaches its maximum and starts again from zero. A reset can also make a counter fall, for a different reason. |
| Discontinuity | A break in a counter's history, such as a reset. Values on opposite sides should not be used as an ordinary rate sample. |
| TimeTicks | A value measured in hundredths of a second. Divide by 100 for seconds. |
| Gauge32 | A nonnegative whole-number value that can rise or fall, unlike an accumulating counter. |
| Opaque | A byte payload whose interpretation is defined elsewhere, often by the device manufacturer. |
| GET / GETNEXT / GETBULK | Ask for exact values / the next available OID / several successive OIDs. Reads and walks choose these operations for you. |
| SET | Write a value. Requires device permission and explicit application intent; ordinary monitoring does not need it. |
| Trap / INFORM | Events sent by a device without being polled. An INFORM expects a reply confirming acceptance; a trap does not. |
| Acknowledgement | A reply confirming an event was accepted. It does not guarantee the event will never be delivered again. |

## Memory and C++ terms

| Term | Meaning here |
| --- | --- |
| API — Application Programming Interface | The classes and methods your sketch calls, such as `SNMPDevice` and `start()`. |
| Sketch | Your Arduino program, usually the `.ino` file in an example folder. |
| RAM — Random Access Memory | The board's working memory while the program runs. More stored results leave less room for networking and other work. |
| Heap / allocation | Working memory requested while the program runs. Allocation fails if there is not enough usable space. |
| Contiguous block / fragmentation | Some data needs one uninterrupted block of free memory. Many small gaps may not be enough, even if their total size looks sufficient. |
| Stack | Working memory used for active function calls and local variables. Keep large collections in stable/global objects as the examples do. |
| Buffer | An area of memory holding bytes, such as a reply packet or a text value. |
| Capacity / bounded | A configured maximum, rather than storage that grows without a limit. See [which limit to change](TROUBLESHOOTING.md#what-does-capacity-mean). |
| Null terminator | A zero byte marking the end of C++ text. A buffer for 15 text characters needs at least 16 bytes. |
| Ownership / lifetime | Which object keeps data alive, and how long it remains valid to use. Keep a device alive while its queries use it. |
| Borrowed pointer/reference | Access to data owned elsewhere. It becomes invalid when its owner is destroyed or releases/replaces that data. |
| Callback / handler | A function called when something happens. Ordinary friendly reads use completion checks instead of requiring per-value callbacks. |
| Asynchronous / pending | Work has started but has not finished. Continue calling `client.loop()` and wait for completion before using results. |
| PSRAM — Pseudo-Static Random Access Memory | Extra working memory available on some boards. This library does not automatically put its data there. |
| Thread/task safety | Whether several independently running parts of a program can use an object at once. Use the client from one task unless you coordinate access yourself. |

## Tools and advanced references

| Term | Meaning here |
| --- | --- |
| IDE — Integrated Development Environment | An editor with tools to compile and upload your sketch, such as Arduino IDE. |
| CLI — Command-Line Interface | A tool run by typing commands in a terminal, such as Arduino CLI. |
| Compile / build / link | Translate source code into machine code and combine the application and library into a program to upload. |
| Header-only | Implementation is supplied in header files. This library also has `.cpp` files, which Arduino and PlatformIO compile automatically. |
| Macro / build flag | A setting passed to the compiler, such as `-DSNMP_PACKET_LENGTH=1500`. Apply library-wide settings as described in the configuration guide. |
| Git branch / commit / tag | A changing line of source development / a recorded revision / a name attached to a revision. Pinning selects a specific version instead of following ongoing changes. |
| Registry / library index | A list of available library packages and versions used by installation tools. |
| CI — Continuous Integration | Automated builds and tests run when project changes are submitted. Users do not need to run these to read a device. |
| RFC — Request for Comments | A numbered technical document describing Internet technology, sometimes an Internet standard. The references explain protocol rules; reading them is not a prerequisite for using this library. |
| ASN.1 — Abstract Syntax Notation One | A notation describing structured protocol data. The library handles these details. |
| BER — Basic Encoding Rules | Rules for turning that data into bytes and back. The library's encoder/decoder does this. |
| TLV — Type, Length, Value | The three parts of an encoded item: what it is, how long it is, and its contents. |
| PDU — Protocol Data Unit | The operation-specific portion of an SNMP message, such as a read request or response. |
| RTTI — Runtime Type Information | A C++ feature for identifying object types while running. The library does not require it. |
| Sanitizer / lint | Tools that detect certain runtime programming errors / source or documentation problems. These are contributor tools, not device requirements. |

## Protocol references (optional)

RFCs are the technical source documents behind these explanations. You do not
need to read them to use the library. Follow a link when you need the exact rule
or the definition of a reading; a device may expose only some of the listed objects.

| Topic | Source and what to look for |
| --- | --- |
| SNMPv1 | [RFC 1157](https://www.rfc-editor.org/rfc/rfc1157.html): the original messages, reads, writes, and traps. |
| SNMPv2c community messages | [RFC 1901](https://www.rfc-editor.org/rfc/rfc1901.html): the community-based message wrapper. The operations it carries are described separately below. |
| Reads, walks, writes, and events | [RFC 3416, section 4.2](https://www.rfc-editor.org/rfc/rfc3416.html#section-4.2): GET, GETNEXT, GETBULK, responses, SET, traps, and INFORMs. |
| UDP and service ports | [RFC 768](https://www.rfc-editor.org/rfc/rfc768.html) defines UDP; [RFC 3417, section 3](https://www.rfc-editor.org/rfc/rfc3417.html#section-3) describes SNMP over UDP/IPv4 and ports 161/162. |
| Encoding messages as bytes | [RFC 3417, section 8](https://www.rfc-editor.org/rfc/rfc3417.html#section-8): SNMP's use of Basic Encoding Rules (BER). |
| Value types and units | [RFC 2578, section 7.1](https://www.rfc-editor.org/rfc/rfc2578.html#section-7.1): INTEGER, OCTET STRING, OBJECT IDENTIFIER, counters, Gauge32, TimeTicks, and other types. |
| Table indices | [RFC 2578, section 7.7](https://www.rfc-editor.org/rfc/rfc2578.html#section-7.7): how an index identifies a table row, including indices containing several values. |
| Conventions for interpreting values | [RFC 2579](https://www.rfc-editor.org/rfc/rfc2579.html): named conventions such as TruthValue, MacAddress, and DateAndTime. |
| Device name, location, and uptime | [RFC 3418](https://www.rfc-editor.org/rfc/rfc3418.html): the system objects used by the basic examples. |
| Network interface counters | [RFC 2863](https://www.rfc-editor.org/rfc/rfc2863.html): IF-MIB, including larger counters and counter discontinuities. |
| Storage readings | [RFC 2790](https://www.rfc-editor.org/rfc/rfc2790.html): HOST-RESOURCES-MIB, including storage allocation units and block counts. |
| Printer supplies | [RFC 3805](https://www.rfc-editor.org/rfc/rfc3805.html): Printer-MIB, including supply units and special level values. |
| Sensor scaling | [RFC 3433](https://www.rfc-editor.org/rfc/rfc3433.html): ENTITY-SENSOR-MIB scale, precision, and status. |
| Address values | [RFC 4001](https://www.rfc-editor.org/rfc/rfc4001.html): InetAddressType and InetAddress, including IPv6 values stored as bytes. This does not imply IPv6 transport support in this library. |

These references describe protocol and object definitions. Library settings such
as result capacity, retry counts, and automatic fallback are documented in the
[query guide](QUERY_API.md); they are not all requirements imposed by an RFC.
