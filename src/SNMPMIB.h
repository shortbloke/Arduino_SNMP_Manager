#ifndef SNMP_MIB_H
#define SNMP_MIB_H
#include "SNMPClient.h"

// Call only after checking result/cell status. These helpers validate wire types
// and ranges, and leave output unchanged on failure. No allocations are needed.
namespace SNMPMIB
{
bool storageBytes(const SNMPValue &allocationUnits, const SNMPValue &blocks, uint64_t &bytes);
bool truthValue(const SNMPValue &value, bool &result);
// ENTITY-SENSOR precision: positive means fractional digits; negative describes
// accuracy, not an extra multiplier. Supply the scale as a decimal exponent.
// Units and operational status remain separate MIB metadata.
bool fixedPoint(const SNMPValue &value, int decimalExponent, int precision, double &result);
enum class SupplyState
{
    Known,
    Other,
    Unknown,
    SomeRemaining,
    Invalid
};
SupplyState supplyState(const SNMPValue &level);
// The two values must use the same unit. Unknown/sentinel values are not percentages.
bool supplyPercent(const SNMPValue &level, const SNMPValue &capacity, double &result);
bool formatMAC(const SNMPValue &value, char *destination, size_t capacity);
// INET-ADDRESS-MIB ipv4(1) / ipv6(2); no transport or DNS operation is performed.
bool formatAddress(const SNMPValue &addressType, const SNMPValue &value, char *destination,
                   size_t capacity);
}
#endif
