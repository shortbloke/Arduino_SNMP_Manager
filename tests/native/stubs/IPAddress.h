#pragma once
#include "Arduino.h"

class IPAddress
{
    byte bytes[4]{};

public:
    IPAddress() = default;
    IPAddress(byte a, byte b, byte c, byte d) : bytes{a, b, c, d} {}
    IPAddress(const byte *p)
    {
        memcpy(bytes, p, 4);
    }
    byte operator[](int i) const
    {
        return bytes[i];
    }
    bool operator==(const IPAddress &other) const
    {
        return memcmp(bytes, other.bytes, 4) == 0;
    }
};
