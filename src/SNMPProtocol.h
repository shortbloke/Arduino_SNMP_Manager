#ifndef SNMP_PROTOCOL_H
#define SNMP_PROTOCOL_H

#include <stdint.h>

enum class SNMPVersion : uint8_t
{
    Version1 = 0,
    Version2c = 1
};

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
