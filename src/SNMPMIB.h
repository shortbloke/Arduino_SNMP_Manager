#ifndef SNMP_MIB_H
#define SNMP_MIB_H
#include "SNMPClient.h"

// Call only after checking result/cell status. These helpers validate wire types
// and ranges, and leave output unchanged on failure. No allocations are needed.
namespace SNMPMIB
{
/**
 * @brief Convert HOST-RESOURCES storage blocks into bytes without 32-bit multiplication overflow.
 * @param allocationUnits INTEGER bytes per block, strictly positive.
 * @param blocks INTEGER block count, nonnegative.
 * @param bytes Output byte count; unchanged on failure.
 * @return True for valid types/ranges, false otherwise. Check source cell statuses before calling.
 */
bool storageBytes(const SNMPValue &allocationUnits, const SNMPValue &blocks, uint64_t &bytes);
/**
 * @brief Interpret the standard TruthValue enumeration.
 * @param value INTEGER true(1) or false(2).
 * @param result Output boolean; unchanged on failure.
 * @return True if converted; false for another type/value. Source status must already be
 * successful.
 */
bool truthValue(const SNMPValue &value, bool &result);
// ENTITY-SENSOR precision: positive means fractional digits; negative describes
// accuracy, not an extra multiplier. Supply the scale as a decimal exponent.
// Units and operational status remain separate MIB metadata.
/**
 * @brief Convert a checked ENTITY-SENSOR fixed-point reading.
 * @param value INTEGER raw value; rejects sentinel/out-of-range readings.
 * @param decimalExponent SI power of ten in -24..24, not the MIB scale enumeration.
 * @param precision Decimal precision in -8..9; positive divides by a power of ten, negative
 *  describes accuracy and is not another multiplier.
 * @param result Output scaled value; unchanged on failure.
 * @return True for a finite valid conversion; false otherwise. Check units, sensor type,
 *  operational status, and the source result separately.
 */
bool fixedPoint(const SNMPValue &value, int decimalExponent, int precision, double &result);
enum class SupplyState
{
    Known,
    Other,
    Unknown,
    SomeRemaining,
    Invalid
};
/**
 * @param level INTEGER supply level or special Printer-MIB value.
 * @return Known for nonnegative levels; Other (-1), Unknown (-2), SomeRemaining (-3),
 *  or Invalid for another negative value/type.
 */
SupplyState supplyState(const SNMPValue &level);
// The two values must use the same unit. Unknown/sentinel values are not percentages.
/**
 * @brief Compute a percentage only from known compatible supply readings.
 * @param level Nonnegative INTEGER amount remaining (or remaining space for a waste receptacle).
 * @param capacity Positive INTEGER maximum in the same units; level must not exceed it.
 * @param result Output percentage in 0..100; unchanged on failure.
 * @return True for valid types/ranges, false for unknown/sentinel/inconsistent readings.
 */
bool supplyPercent(const SNMPValue &level, const SNMPValue &capacity, double &result);
/**
 * @brief Format six address bytes as colon-separated hexadecimal text.
 * @param value OCTET STRING containing exactly six bytes.
 * @param destination Non-null caller-owned text buffer; unchanged on failure.
 * @param capacity Buffer bytes including termination; at least 18.
 * @return True when written, false for wrong type/length or insufficient/null output.
 */
bool formatMAC(const SNMPValue &value, char *destination, size_t capacity);
// INET-ADDRESS-MIB ipv4(1) / ipv6(2); no transport or DNS operation is performed.
/**
 * @brief Format an INET-ADDRESS-MIB IPv4 or IPv6 value; performs no network operation.
 * @param addressType INTEGER 1 (IPv4) or 2 (IPv6); other address kinds are rejected.
 * @param value OCTET STRING of four or sixteen bytes matching addressType.
 * @param destination Non-null caller-owned output text buffer; unchanged on failure.
 * @param capacity Bytes including termination; allow 16 for IPv4 or 40 for IPv6.
 * @return True when formatted, false on type/length/address-kind/output errors.
 * @note IPv6 text uses all eight groups, without zero-group compression.
 */
bool formatAddress(const SNMPValue &addressType, const SNMPValue &value, char *destination,
                   size_t capacity);
}
#endif
