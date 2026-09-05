#ifndef SNMP_PROTOCOL_H
#define SNMP_PROTOCOL_H

#include <stdint.h>

/// Supported community-based versions; enum values match the encoded version INTEGER.
enum class SNMPVersion : uint8_t
{
    Version1 = 0,
    Version2c = 1
};

/// Parser-stage labels retained for low-level diagnostics; not a query completion status.
enum SNMPExpect
{
    HEADER,
    SNMPVERSION,
    COMMUNITY,
    PDU,
    REQUESTID,
    ERRORSTATUS,
    ERRORID,
    VARBINDS,
    VARBIND,
    DONE
};

#endif
