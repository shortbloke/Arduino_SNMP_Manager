#include "SNMPConfig.h"

namespace snmp_detail
{
template <unsigned Packet, unsigned Octet, unsigned Oid, unsigned Pending, unsigned Value,
          bool Debug, bool BerDebug, bool SuppressErrors>
void BuildConfiguration<Packet, Octet, Oid, Pending, Value, Debug, BerDebug,
                        SuppressErrors>::verify()
{
}
template struct BuildConfiguration<SNMP_PACKET_LENGTH, SNMP_OCTETSTRING_MAX_LENGTH, MAX_OID_LENGTH,
                                   SNMP_MAX_PENDING_REQUESTS, SNMP_VALUE_MAX_LENGTH, debugEnabled,
                                   berDebugEnabled, suppressParseErrors>;
}
