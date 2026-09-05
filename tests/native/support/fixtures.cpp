#include "fixtures.h"

Bytes encode(BER_CONTAINER &value)
{
    unsigned char b[8192]{};
    int n = value.serialise(b, sizeof(b));
    CHECK(n >= 0 && n <= 8192);
    return Bytes(b, b + n);
}
// Independent fixture builder: never uses the library's serializer.
Bytes tlv(unsigned char tag, Bytes value)
{
    Bytes out{tag};
    if (value.size() < 128)
        out.push_back(value.size());
    else if (value.size() < 256)
    {
        out.push_back(0x81);
        out.push_back(value.size());
    }
    else
    {
        out.push_back(0x82);
        out.push_back(value.size() >> 8);
        out.push_back(value.size());
    }
    out.insert(out.end(), value.begin(), value.end());
    return out;
}
Bytes join(std::initializer_list<Bytes> items)
{
    Bytes b;
    for (auto &i : items)
        b.insert(b.end(), i.begin(), i.end());
    return b;
}
const char *oid = ".1.3.6.1.2.1.1.1.0";
Bytes oidWire{6, 8, 43, 6, 1, 2, 1, 1, 1, 0};
Bytes binding(Bytes value)
{
    return tlv(0x30, join({oidWire, value}));
}
Bytes message(Bytes bindings, int version, const char *community, int pdu, int requestId,
              int errorStatus, int errorIndex)
{
    Bytes c(community, community + strlen(community));
    return tlv(0x30, join({tlv(2, {static_cast<unsigned char>(version)}), tlv(4, c),
                           tlv(pdu, join({tlv(2, {static_cast<unsigned char>(requestId)}),
                                          tlv(2, {static_cast<unsigned char>(errorStatus)}),
                                          tlv(2, {static_cast<unsigned char>(errorIndex)}),
                                          tlv(0x30, bindings)}))}));
}
