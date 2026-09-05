# Get your first reading from a device

SNMP lets a device answer questions such as “How long have you been running?” or
“How many bytes has this interface received?” This library lets an ESP8266 or
ESP32 ask those questions. You do not need to write network packets or manage
callbacks to read values.

## Can I use SNMP with my device?

Check the device's settings or manual for **SNMP**. Routers, managed switches,
access points, servers/NAS systems, and printers may support it, but support varies
by model and firmware. The library cannot add SNMP support to a device that lacks it.

You need:

- An ESP8266 or ESP32 that can reach the device over the network.
- The device's IP address, such as `192.168.1.10`.
- SNMP enabled on the device, using **v2c** or **v1**. Prefer v2c when available;
  it supports larger traffic counters and more efficient reads. A device configured
  only for SNMPv3 cannot be queried by this library.
- The configured **community string**: a shared access string, separate from your
  Wi-Fi password. `public` is a common example, not a value you should assume works.
- Permission in the device's SNMP settings for the board to read the desired data.
  Start with read-only access; no write access is needed for monitoring.

SNMPv1/v2c do not encrypt the community or readings. Use them on a trusted network;
do not expose the device's SNMP service to the Internet.

## Install the matching library

These examples use **2.x**. The current 2.x preview is an unpublished draft; installing
stable 1.x from Library Manager will not provide `SNMPClient.h`.

To try the preview, download the [2.x source ZIP](https://github.com/shortbloke/Arduino_SNMP_Manager/archive/refs/heads/main.zip)
and use Arduino IDE's **Sketch > Include Library > Add .ZIP Library**. Avoid keeping
another copy of SNMP Manager in the same library search path. The `main` download
changes as development continues; record the commit you tested when reporting a
problem. For an existing PlatformIO project, the repository's example compile
projects show how to use a local checkout as a library dependency.

Once 2.x is published and indexed, select its version explicitly in Library Manager.
For stable 1.x projects and reproducible dependency settings, see
[version selection](../README.md#pin-your-projects-library-version).

## Run one read before trying a whole table

1. Open [Simple_Read](../examples/Simple_Read/Simple_Read.ino). Start with this
   complete sketch rather than pasting an isolated API fragment into a blank file.
2. Replace `YOUR_SSID` and `YOUR_PASSWORD` with your Wi-Fi settings.
3. In the `SNMPDevice` declaration, replace the sample address and `public` with
   your device's address and community. It defaults to v2c. For a v1-only device,
   pass `SNMPVersion::Version1` as the fourth constructor argument.
4. Select your ESP board and serial port, then compile and upload.
5. Open Serial Monitor at **115200 baud**. The sketch connects to Wi-Fi, starts
   the client, and requests uptime periodically. Successful output reports
   whole seconds; the sketch divides the underlying hundredths-of-a-second value
   by 100 for you. This is the time since
   the device's SNMP management subsystem started, which may differ from OS uptime.
6. If it fails, use the [setup and error checklist](TROUBLESHOOTING.md). Repeated
   timeouts do not prove the device lacks SNMP; check its configuration first.

`SNMPClient` handles the network work. `SNMPDevice` tells it which device to ask.
`SNMPRead<SystemUptime>` describes the reading you want. The example calls
`start()` to request it, keeps calling `client.loop()` while waiting, and uses
`takeCompleted()` to find out when to inspect the result. It checks success before
printing a value, so a failed read cannot look like a valid zero.

## Choose the data you want

| I want to… | Start with | What to expect |
| --- | --- | --- |
| Check that a device responds | [Simple_Read](../examples/Simple_Read/Simple_Read.ino) | Uptime from one device |
| Read several devices | [Multiple_Devices](../examples/Multiple_Devices/Multiple_Devices.ino) | Separate results without one device blocking another |
| Read network traffic counters | [Interface_Traffic](../examples/Interface_Traffic/Interface_Traffic.ino) | Interface names and cumulative byte counts, including virtual interfaces |
| Read server/NAS storage | [Host_Storage](../examples/Host_Storage/Host_Storage.ino) | Storage descriptions, used bytes, and total bytes; entries can include memory and mounts |
| Read printer supplies | [Printer_Supplies](../examples/Printer_Supplies/Printer_Supplies.ino) | Available levels and explicit unknown/unavailable states |
| Explore available readings | [Walk_Values](../examples/Walk_Values/Walk_Values.ino) | Values below a chosen numeric OID; prints them as they arrive |

Open these sketches through the [example guide](../examples/README.md). A device
may not expose the standard objects an example uses. Missing data is not proof
that a disk, interface, or cartridge does not exist.

Traffic counters are **totals, not current speeds**. A rate needs two successful
samples and elapsed time. Counter resets and wraparound must be handled; see the
query guide before adapting counters into a bandwidth display.

## How do I ask for a different reading?

An **OID** is a numeric address for a piece of information. For example,
`.1.3.6.1.2.1.1.3.0` identifies the standard uptime reading. You cannot invent an
OID by guessing a device setting's name.

Look in your device manufacturer's SNMP documentation or MIB files for the object
you need. A **MIB** is a catalogue explaining object names, numeric OIDs, data types,
and units. The library uses the numeric OIDs; it does not load the MIB file or
translate symbolic names on the board.

- A **scalar** is a single reading, such as a device name. Its instance normally
  ends in `.0`; include that suffix when reading it.
- A **table** contains repeated rows, such as network interfaces. Each row has
  an **index** assigned by the device. Indices can have gaps and need not match
  physical port numbers. A **column** selects one field across those rows.
- A **walk** asks for successive OIDs beneath a starting point, called the root
  or subtree. It discovers the instances actually exposed by the device.
- A **type** tells you how to interpret a value. INTEGER is signed whole-number
  data; OCTET STRING may be text or binary; TimeTicks counts hundredths of a
  second; Counter32 and Counter64 are accumulating unsigned counters. The MIB
  supplies the meaning and units, not just the type.

After finding the OID, use `SNMPQuery` to request exact instances or `SNMPTableRead`
to discover selected columns. See [arbitrary OIDs](QUERY_API.md#arbitrary-oids-and-ranges)
for the methods and an example. Begin with one object and check every result.

## Do I need to learn GETNEXT or GETBULK?

For ordinary reads and tables, the library chooses the appropriate operation:

- **GET** asks for exact readings; **RESPONSE** carries the answer.
- **GETNEXT** asks for the next OID. **GETBULK** asks for several successors at once.
  Walks and tables use them to discover rows; v1 uses GETNEXT.
- **SET** writes a value and requires explicit application intent and device
  permission. It is not part of the read-only examples above.
- A **trap** is an event sent by a device without polling. An **INFORM** is an
  event for which the sender expects an acknowledgement. The library can receive
  these events; ordinary polling does not require configuring them.

## When the device has more data than the board can hold

Examples reserve a fixed maximum number of rows: their **capacity**. A NAS can
expose hundreds of entries, even with only a few physical disks. A switch can have
more logical interfaces than ports. A full table stops with an error instead of
silently allocating more memory.

Follow [what capacity means](TROUBLESHOOTING.md#what-does-capacity-mean) to identify
which constant to edit, rebuild, and check memory use. Streaming is an alternative
when you can process individual values without retaining a complete table.

You do not need to run the repository's tests, understand BER encoding, or use the
release workflows to use the library. Those documents are contributor references.
