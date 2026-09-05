#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
using byte = uint8_t;
#define F(x) x
class IPAddress {
    byte bytes[4]{};
public:
    IPAddress() = default;
    IPAddress(byte a, byte b, byte c, byte d) : bytes{a,b,c,d} {}
    IPAddress(const byte* p) { memcpy(bytes,p,4); }
    byte operator[](int i) const { return bytes[i]; }
    bool operator==(const IPAddress& other) const { return memcmp(bytes,other.bytes,4)==0; }
};
class String {
    std::string value;
public:
    String(const char* s) : value(s) {}
    size_t length() const { return value.size(); }
    void toCharArray(char* dst, size_t n) const { if(n) { strncpy(dst,value.c_str(),n); dst[n-1]=0; } }
};
struct SerialStub {
    template<class... T> void print(T...) {}
    template<class... T> void println(T...) {}
    template<class... T> void printf(T...) {}
};
static SerialStub Serial;
inline void delay(unsigned long) {}
