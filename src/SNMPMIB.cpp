#include "SNMPMIB.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace SNMPMIB
{
bool storageBytes(const SNMPValue &allocationUnits, const SNMPValue &blocks, uint64_t &bytes)
{
    if (allocationUnits.type != INTEGER || blocks.type != INTEGER ||
        allocationUnits.integer() <= 0 || blocks.integer() < 0)
        return false;
    bytes =
        static_cast<uint64_t>(allocationUnits.integer()) * static_cast<uint32_t>(blocks.integer());
    return true;
}
bool truthValue(const SNMPValue &value, bool &result)
{
    if (value.type != INTEGER || (value.integer() != 1 && value.integer() != 2))
        return false;
    result = value.integer() == 1;
    return true;
}
bool fixedPoint(const SNMPValue &value, int exponent, int precision, double &result)
{
    if (value.type != INTEGER || exponent < -24 || exponent > 24 || precision < -8 ||
        precision > 9 || value.integer() <= -1000000000 || value.integer() >= 1000000000)
        return false;
    double converted = value.integer() * std::pow(10.0, exponent - (precision > 0 ? precision : 0));
    if (!std::isfinite(converted))
        return false;
    result = converted;
    return true;
}
SupplyState supplyState(const SNMPValue &level)
{
    if (level.type != INTEGER)
        return SupplyState::Invalid;
    switch (level.integer())
    {
    case -1:
        return SupplyState::Other;
    case -2:
        return SupplyState::Unknown;
    case -3:
        return SupplyState::SomeRemaining;
    default:
        return level.integer() >= 0 ? SupplyState::Known : SupplyState::Invalid;
    }
}
bool supplyPercent(const SNMPValue &level, const SNMPValue &capacity, double &result)
{
    if (supplyState(level) != SupplyState::Known || capacity.type != INTEGER ||
        capacity.integer() <= 0 || level.integer() > capacity.integer())
        return false;
    result = 100.0 * level.integer() / capacity.integer();
    return true;
}
bool formatMAC(const SNMPValue &value, char *destination, size_t capacity)
{
    if (value.type != STRING || value.length != 6 || !destination || capacity < 18)
        return false;
    char text[18];
    snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", value.bytes[0], value.bytes[1],
             value.bytes[2], value.bytes[3], value.bytes[4], value.bytes[5]);
    memcpy(destination, text, sizeof(text));
    return true;
}
bool formatAddress(const SNMPValue &addressType, const SNMPValue &value, char *destination,
                   size_t capacity)
{
    if (addressType.type != INTEGER || value.type != STRING || !destination)
        return false;
    char text[40];
    int length;
    if (addressType.integer() == 1 && value.length == 4)
        length = snprintf(text, sizeof(text), "%u.%u.%u.%u", value.bytes[0], value.bytes[1],
                          value.bytes[2], value.bytes[3]);
    else if (addressType.integer() == 2 && value.length == 16)
    {
        unsigned groups[8];
        for (unsigned i = 0; i < 8; ++i)
            groups[i] = unsigned(value.bytes[i * 2]) * 256 + value.bytes[i * 2 + 1];
        length = snprintf(text, sizeof(text), "%x:%x:%x:%x:%x:%x:%x:%x", groups[0], groups[1],
                          groups[2], groups[3], groups[4], groups[5], groups[6], groups[7]);
    }
    else
        return false;
    if (length < 0 || static_cast<size_t>(length) >= capacity)
        return false;
    memcpy(destination, text, static_cast<size_t>(length) + 1);
    return true;
}
}
