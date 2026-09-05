#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
using byte = uint8_t;
#define F(x) x
#define HEX 16
class String
{
    std::string value;

public:
    String(const char *s) : value(s) {}
    const char *c_str() const
    {
        return value.c_str();
    }
    size_t length() const
    {
        return value.size();
    }
    void toCharArray(char *dst, size_t n) const
    {
        if (n)
        {
            strncpy(dst, value.c_str(), n);
            dst[n - 1] = 0;
        }
    }
};
struct SerialStub
{
    size_t hexWrites = 0;
    void print(unsigned char, int base)
    {
        if (base == HEX)
            ++hexWrites;
    }
    template <class... T> void print(T...) {}
    template <class... T> void println(T...) {}
    template <class... T> void printf(T...) {}
};
extern SerialStub Serial;
inline void delay(unsigned long) {}

inline unsigned long millis()
{
    return 0;
}
