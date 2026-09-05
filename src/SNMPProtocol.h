#ifndef SNMP_PROTOCOL_H
#define SNMP_PROTOCOL_H

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
