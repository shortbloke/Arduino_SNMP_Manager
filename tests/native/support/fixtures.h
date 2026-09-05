#pragma once
#include <Arduino_SNMP_Manager.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <limits>
#include <type_traits>
#include <utility>
struct FailAllocations
{
    explicit FailAllocations(int count);
    ~FailAllocations();
    FailAllocations(const FailAllocations &) = delete;
    FailAllocations &operator=(const FailAllocations &) = delete;
};
using Bytes = std::vector<unsigned char>;
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (0)

Bytes encode(BER_CONTAINER &value);
Bytes tlv(unsigned char tag, Bytes value);
Bytes join(std::initializer_list<Bytes> items);
extern const char *oid;
extern Bytes oidWire;
Bytes binding(Bytes value);
Bytes message(Bytes bindings, int version = 1, const char *community = "public", int pdu = 0xa2,
              int requestId = 7, int errorStatus = 0, int errorIndex = 0);
using Manager = SNMPManager;
struct Request : SNMPGet
{
    Request(SNMPVersion version = SNMPVersion::Version2c) : SNMPGet("public", version)
    {
        setRequestID(7);
    }
};
