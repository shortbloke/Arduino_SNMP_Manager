#ifndef SNMP_CONFIG_H
#define SNMP_CONFIG_H

// Define overrides in compiler flags for both the sketch and library sources.
// Alternatively, define SNMP_CONFIG_HEADER to a quoted, shared configuration header.
#ifdef SNMP_CONFIG_HEADER
#include SNMP_CONFIG_HEADER
#endif

#ifndef SNMP_PACKET_LENGTH
#ifdef ESP32
#define SNMP_PACKET_LENGTH 1500
#else
#define SNMP_PACKET_LENGTH 512
#endif
#endif
#ifndef SNMP_OCTETSTRING_MAX_LENGTH
#define SNMP_OCTETSTRING_MAX_LENGTH 1024
#endif
#ifndef MAX_OID_LENGTH
#define MAX_OID_LENGTH 128
#endif
#ifndef SNMP_MAX_PENDING_REQUESTS
#define SNMP_MAX_PENDING_REQUESTS 4
#endif
static_assert(SNMP_MAX_PENDING_REQUESTS > 0, "At least one pending request slot is required");

namespace snmp_detail
{
#ifdef DEBUG
constexpr bool debugEnabled = true;
#else
constexpr bool debugEnabled = false;
#endif
#ifdef DEBUG_BER
constexpr bool berDebugEnabled = true;
#else
constexpr bool berDebugEnabled = false;
#endif
#ifdef SUPPRESS_ERROR_FAILED_PARSE
constexpr bool suppressParseErrors = true;
#else
constexpr bool suppressParseErrors = false;
#endif

// Each translation unit references the configuration compiled into the library.
// Inconsistent settings produce a link error instead of incompatible class layouts.
template <unsigned Packet, unsigned Octet, unsigned Oid, unsigned Pending, bool Debug,
          bool BerDebug, bool SuppressErrors>
struct BuildConfiguration
{
    static void verify();
    BuildConfiguration()
    {
        verify();
    }
};
using CurrentBuildConfiguration =
    BuildConfiguration<SNMP_PACKET_LENGTH, SNMP_OCTETSTRING_MAX_LENGTH, MAX_OID_LENGTH,
                       SNMP_MAX_PENDING_REQUESTS, debugEnabled, berDebugEnabled,
                       suppressParseErrors>;
}
namespace
{
const snmp_detail::CurrentBuildConfiguration snmpConfigurationGuard;
}
#endif
