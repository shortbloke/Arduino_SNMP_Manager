#include <BER.h>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

int main()
{
    // RFC 2578 maximum: 128 arcs, with every unrestricted arc at UINT32_MAX.
    std::string name = ".2.4294967295";
    for (unsigned i = 2; i < 128; ++i)
        name += ".4294967295";
    assert(name.size() == 1399);
    OIDType value(const_cast<char *>(name.c_str()));
    std::vector<unsigned char> expected{6, 0x82, 0x02, 0x7b, 0x90, 0x80, 0x80, 0x80, 0x4f};
    // The first encoded subidentifier includes 2*40; the rest encode 2^32-1.
    for (unsigned i = 2; i < 128; ++i)
        expected.insert(expected.end(), {0x8f, 0xff, 0xff, 0xff, 0x7f});
    std::vector<unsigned char> wire(expected.size());
    assert(value.serialise(wire.data(), wire.size()) == static_cast<int>(wire.size()));
    assert(wire == expected);
    OIDType decoded;
    assert(decoded.fromBuffer(wire.data(), wire.size()));
    assert(std::strcmp(decoded._value, name.c_str()) == 0);
}
