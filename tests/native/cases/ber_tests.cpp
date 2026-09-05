#include "fixtures.h"
#include "registry.h"

void registerBerTests(std::vector<Test> &tests)
{
    auto add = [&](const char *name, std::function<void()> run)
    { tests.push_back({"Ber", name, run}); };
    add("bounded primitive decoding and serialization reject short buffers",
        []
        {
            IntegerType integer(128);
            Counter64 counter(UINT64_MAX);
            NetworkAddress address(IPAddress(1, 2, 3, 4));
            NullType null;
            char text[] = "hello", name[] = ".1.3.6";
            OctetType string(text);
            OIDType oidValue(name);
            ComplexType sequence(STRUCTURE);
            sequence.addValueToList(new IntegerType(42));
            for (BER_CONTAINER *value : std::vector<BER_CONTAINER *>{
                     &integer, &counter, &address, &null, &string, &oidValue, &sequence})
            {
                auto wire = encode(*value);
                for (size_t size = 0; size < wire.size(); ++size)
                {
                    Bytes output(wire.size(), 0xaa);
                    CHECK(value->serialise(output.data(), size) < 0);
                    CHECK(output == Bytes(wire.size(), 0xaa));
                }
                for (size_t size = 0; size < wire.size(); ++size)
                    CHECK(!value->fromBuffer(wire.data(), size));
                CHECK(value->fromBuffer(wire.data(), wire.size()));
            }
            Manager manager;
            int destination = 0;
            UDP udp;
            Request request;
            request.setUDP(&udp);
            auto *callback = manager.addIntegerHandler(udp.peer, oid, &destination);
            for (int i = 0; i < 200; ++i)
                request.addOIDPointer(callback);
            CHECK(!request.sendTo(udp.peer));
            CHECK(udp.packets == 0);
        });
    add("Opaque payload is preserved without nested decoding",
        []
        {
            auto bytes = message(binding(tlv(0x44, {0xff, 0, 0x30, 0x80, 42})));
            SNMPGetResponse response;
            CHECK(response.parseFrom(bytes.data(), bytes.size()));
            CHECK(response.varBinds->value->type == OPAQUE);
            CHECK(response.varBinds->value->value->_isPrimitive);
            CHECK(encode(*response.varBinds->value->value) ==
                  Bytes({0x44, 5, 0xff, 0, 0x30, 0x80, 42}));
            auto unknown = message(binding({0x47, 0}));
            CHECK(!response.parseFrom(unknown.data(), unknown.size()));
        });
    add("integer small wire values",
        []
        {
            for (unsigned long n : {0UL, 1UL, 127UL})
            {
                IntegerType v(n);
                CHECK(encode(v) == Bytes({2, 1, static_cast<unsigned char>(n)}));
            }
        });
    add("integer big endian decode",
        []
        {
            Bytes b{2, 4, 0x12, 0x34, 0x56, 0x78};
            IntegerType v;
            CHECK(v.fromBuffer(b.data()));
            CHECK(v._value == 0x12345678UL);
        });
    add("unsigned application type decoding",
        []
        {
            Bytes b{0x41, 5, 0, 255, 255, 255, 255};
            Counter32 c;
            Gauge g;
            TimestampType t;
            c.fromBuffer(b.data());
            b[0] = 0x42;
            g.fromBuffer(b.data());
            b[0] = 0x43;
            t.fromBuffer(b.data());
            CHECK(c._value == UINT32_MAX);
            CHECK(g._value == UINT32_MAX);
            CHECK(t._value == UINT32_MAX);
        });
    add("counter64 full range decode",
        []
        {
            Bytes b{0x46, 9, 0, 255, 255, 255, 255, 255, 255, 255, 255};
            Counter64 c;
            c.fromBuffer(b.data());
            CHECK(c._value == UINT64_MAX);
            Counter64 zero(0);
            CHECK(encode(zero) == Bytes({0x46, 1, 0}));
        });
    add("null and network address",
        []
        {
            NullType n;
            CHECK(encode(n) == Bytes({5, 0}));
            NetworkAddress ip(IPAddress(192, 0, 2, 1));
            auto b = encode(ip);
            CHECK(b == Bytes({0x40, 4, 192, 0, 2, 1}));
            NetworkAddress decoded;
            decoded.fromBuffer(b.data());
            CHECK(decoded._value == IPAddress(192, 0, 2, 1));
        });
    add("OID wire bytes and enterprise arc",
        []
        {
            char s[] = ".1.3.6.1.4.1.12345.0";
            OIDType o(s);
            auto b = encode(o);
            CHECK(b == Bytes({6, 8, 43, 6, 1, 4, 1, 0xe0, 0x39, 0}));
            OIDType d;
            d.fromBuffer(b.data());
            CHECK(std::string(d._value) == s);
        });
    add("octet decode length boundaries",
        []
        {
            for (size_t n : {0, 1, 127, 128, 255, 256, 257, 1023})
            {
                auto b = tlv(4, Bytes(n, 'x'));
                OctetType o;
                CHECK(o.fromBuffer(b.data()));
                CHECK(o.getLength() == n);
                CHECK(std::string(o._value) == std::string(n, 'x'));
            }
        });
    add("octet encode length boundaries",
        []
        {
            for (size_t n : {0, 1, 127, 128, 255, 257})
            {
                OctetType o;
                memset(o._value, 0, sizeof(o._value));
                memset(o._value, 'x', n);
                CHECK(encode(o) == tlv(4, Bytes(n, 'x')));
            }
        });
    add("nested BER structure",
        []
        {
            auto b = tlv(0x30, join({{2, 1, 42}, tlv(0x30, {5, 0})}));
            ComplexType c(STRUCTURE);
            CHECK(c.fromBuffer(b.data()));
            CHECK(c._values->value->_type == INTEGER);
            CHECK(c._values->next->value->_type == STRUCTURE);
            CHECK(encode(c) == b);
        });
    add("Integer32 serialization byte and sign boundaries",
        []
        {
            const std::vector<std::pair<int32_t, Bytes>> fixtures{{127, {127}},
                                                                  {128, {0, 128}},
                                                                  {255, {0, 255}},
                                                                  {256, {1, 0}},
                                                                  {32767, {0x7f, 255}},
                                                                  {32768, {0, 0x80, 0}},
                                                                  {-1, {255}},
                                                                  {-128, {128}},
                                                                  {-129, {255, 127}},
                                                                  {-32768, {128, 0}},
                                                                  {-32769, {255, 127, 255}},
                                                                  {INT32_MAX, {127, 255, 255, 255}},
                                                                  {INT32_MIN, {128, 0, 0, 0}}};
            for (const auto &fixture : fixtures)
            {
                IntegerType value(static_cast<unsigned long>(fixture.first));
                const auto original = value._value;
                CHECK(encode(value) == tlv(2, fixture.second));
                CHECK(value._value == original);
                CHECK(encode(value) == tlv(2, fixture.second));
            }
            Counter32 counter(UINT32_MAX);
            Gauge gauge(UINT32_MAX);
            TimestampType ticks(UINT32_MAX);
            CHECK(encode(counter) == Bytes({0x41, 5, 0, 255, 255, 255, 255}));
            CHECK(encode(gauge) == Bytes({0x42, 5, 0, 255, 255, 255, 255}));
            CHECK(encode(ticks) == Bytes({0x43, 5, 0, 255, 255, 255, 255}));
        });
    add("integer 128 minimal signed BER encoding",
        []
        {
            IntegerType v(128);
            CHECK(encode(v) == Bytes({2, 2, 0, 128}));
        });
    add("integer serialization preserves value",
        []
        {
            IntegerType v(256);
            encode(v);
            CHECK(v._value == 256);
        });
    add("octet 256 length encoding",
        []
        {
            OctetType v;
            memset(v._value, 0, sizeof(v._value));
            memset(v._value, 'x', 256);
            CHECK(encode(v) == tlv(4, Bytes(256, 'x')));
        });
    add("OID base128 boundary 16384",
        []
        {
            char s[] = ".1.3.16384";
            OIDType v(s);
            CHECK(encode(v) == Bytes({6, 4, 43, 0x81, 0x80, 0}));
        });
    add("octet nonminimal definite length accepted",
        []
        {
            Bytes b{4, 0x82, 0, 3, 'a', 'b', 'c'};
            OctetType v;
            CHECK(v.fromBuffer(b.data()));
            CHECK(v.getLength() == 3);
            CHECK(std::string(v._value) == "abc");
        });
    add("Integer32 decoding boundaries and definite lengths",
        []
        {
            for (Bytes bytes : {Bytes{2, 4, 0x80, 0, 0, 0}, Bytes{2, 0x81, 4, 0x80, 0, 0, 0},
                                Bytes{2, 0x82, 0, 4, 0x80, 0, 0, 0}})
            {
                IntegerType value;
                CHECK(value.fromBuffer(bytes.data()));
                CHECK(value._value == static_cast<unsigned long>(INT32_MIN));
                CHECK(value.getLength() == 4);
            }
            Bytes positive{2, 4, 0x7f, 255, 255, 255};
            IntegerType value;
            CHECK(value.fromBuffer(positive.data()));
            CHECK(value._value == INT32_MAX);
            for (Bytes bytes :
                 {Bytes{2, 0}, Bytes{2, 0x80}, Bytes{2, 0xff}, Bytes{2, 5}, Bytes{2, 0x82, 1, 0}})
            {
                IntegerType invalid(42);
                CHECK(!invalid.fromBuffer(bytes.data()));
                CHECK(invalid._value == 42);
            }
        });
    add("negative INTEGER sign extension",
        []
        {
            for (auto b : {Bytes{2, 1, 0xff}, Bytes{2, 1, 0x80}, Bytes{2, 2, 0xff, 0x7f}})
            {
                IntegerType v;
                CHECK(v.fromBuffer(b.data()));
                long expected = b.size() == 4 ? -129 : (b[2] == 0xff ? -1 : -128);
                CHECK(v._value == static_cast<unsigned long>(expected));
            }
        });
    add("Integer32 signed boundary encoding",
        []
        {
            IntegerType v(static_cast<unsigned long>(INT32_MIN));
            CHECK(encode(v) == Bytes({2, 4, 0x80, 0, 0, 0}));
        });
    add("Counter64 small value uses minimal contents",
        []
        {
            const std::vector<std::pair<uint64_t, Bytes>> fixtures{
                {0, {0}},
                {1, {1}},
                {127, {127}},
                {128, {0, 128}},
                {255, {0, 255}},
                {256, {1, 0}},
                {UINT64_C(0x7fffffffffffffff), {0x7f, 255, 255, 255, 255, 255, 255, 255}},
                {UINT64_C(0x8000000000000000), {0, 0x80, 0, 0, 0, 0, 0, 0, 0}}};
            for (const auto &fixture : fixtures)
            {
                Counter64 value(fixture.first);
                CHECK(encode(value) == tlv(0x46, fixture.second));
                CHECK(value._value == fixture.first);
                CHECK(encode(value) == tlv(0x46, fixture.second));
            }
        });
    add("Counter64 maximum has positive sign octet",
        []
        {
            Counter64 v(UINT64_MAX);
            CHECK(encode(v) == Bytes({0x46, 9, 0, 255, 255, 255, 255, 255, 255, 255, 255}));
        });
    add("unsigned application encoding preserves positive sign",
        []
        {
            Counter32 v(UINT32_MAX);
            CHECK(encode(v) == Bytes({0x41, 5, 0, 255, 255, 255, 255}));
        });
    add("oversized OCTET STRING is rejected without truncation",
        []
        {
            auto bytes = tlv(4, Bytes(SNMP_OCTETSTRING_MAX_LENGTH, 'x'));
            OctetType value;
            CHECK(!value.fromBuffer(bytes.data()));
            char output[2048]{};
            std::string oversized(SNMP_OCTETSTRING_MAX_LENGTH, 'x');
            OctetType constructed(&oversized[0]);
            CHECK(constructed.serialise(reinterpret_cast<unsigned char *>(output)) < 0);
        });
    add("binary OCTET STRING preserves embedded zero",
        []
        {
            auto b = tlv(4, {'a', 0, 'b'});
            OctetType v;
            CHECK(v.fromBuffer(b.data()));
            CHECK(v.getLength() == 3);
            CHECK(memcmp(v._value, "a\0b", 3) == 0);
        });
    add("binary OCTET STRING re-encoding preserves length",
        []
        {
            auto b = tlv(4, {'a', 0, 'b'});
            OctetType v;
            CHECK(v.fromBuffer(b.data()));
            CHECK(encode(v) == b);
        });
    add("OID roots and malformed subidentifiers",
        []
        {
            const std::vector<std::pair<std::string, Bytes>> fixtures{
                {".0.0", {0}},
                {".1.39.0", {79, 0}},
                {".2.0.0", {80, 0}},
                {".2.4294967295.0", {0x90, 0x80, 0x80, 0x80, 0x4f, 0}}};
            for (auto fixture : fixtures)
            {
                OIDType value(&fixture.first[0]);
                const auto wire = tlv(6, fixture.second);
                CHECK(encode(value) == wire);
                auto input = wire;
                OIDType decoded;
                CHECK(decoded.fromBuffer(input.data()));
                CHECK(input == wire);
                CHECK(std::string(decoded._value) == fixture.first);
            }
            for (Bytes bytes : {Bytes{6, 0}, Bytes{6, 1, 0x81}, Bytes{6, 2, 0x80, 0},
                                Bytes{6, 6, 43, 0x90, 0x80, 0x80, 0x80, 0}})
            {
                OIDType value;
                CHECK(!value.fromBuffer(bytes.data()));
            }
        });
    add("OID first arcs are decoded rather than assumed",
        []
        {
            Bytes b{6, 3, 0x88, 0x37, 3};
            OIDType v;
            CHECK(v.fromBuffer(b.data()));
            CHECK(std::string(v._value) == ".2.999.3");
        });
    add("OID maximum subidentifier encodes five base128 octets",
        []
        {
            char oid[] = ".1.3.4294967295";
            OIDType v(oid);
            CHECK(encode(v) == Bytes({6, 6, 43, 0x8f, 0xff, 0xff, 0xff, 0x7f}));
        });
    add("indefinite sequence length rejected",
        []
        {
            Bytes b{0x30, 0x80, 2, 1, 42, 0, 0};
            ComplexType v(STRUCTURE);
            CHECK(!v.fromBuffer(b.data()));
        });
    // RFC 3416 section 4.2.1 requires an empty list in the alternate tooBig response.
    add("sequence content exactly 256 has a two-octet length",
        []
        {
            ComplexType sequence(STRUCTURE);
            Bytes content;
            for (int i = 0; i < 128; ++i)
            {
                sequence.addValueToList(new NullType());
                content.insert(content.end(), {5, 0});
            }
            CHECK(content.size() == 256);
            CHECK(encode(sequence) == tlv(0x30, content));
        });
    add("sequence lengths either side of 256",
        []
        {
            for (size_t contentLength : {127, 128, 255, 257})
            {
                const size_t payloadLength = contentLength - (contentLength < 130 ? 2 : 3);
                auto *value = new OctetType();
                memset(value->_value, 0, sizeof(value->_value));
                memset(value->_value, 'x', payloadLength);
                ComplexType sequence(STRUCTURE);
                sequence.addValueToList(value);
                auto content = tlv(4, Bytes(payloadLength, 'x'));
                CHECK(content.size() == contentLength);
                CHECK(encode(sequence) == tlv(0x30, content));
            }
        });

    // Verify consumed-byte accounting keeps siblings aligned.
    add("long-form nested child leaves sibling aligned",
        []
        {
            for (size_t size : {128, 256, 300})
            {
                auto bytes = tlv(0x30, join({tlv(0x30, tlv(4, Bytes(size, 'x'))), {2, 1, 42}}));
                ComplexType root(STRUCTURE);
                CHECK(root.fromBuffer(bytes.data()));
                CHECK(root._values && root._values->next);
                CHECK(root._values->next->value->_type == INTEGER);
                CHECK(static_cast<IntegerType *>(root._values->next->value)->_value == 42);
                CHECK(root._values->next->next == nullptr);
                auto *child = static_cast<ComplexType *>(root._values->value);
                CHECK(child->_values->value->_type == STRING);
                CHECK(static_cast<OctetType *>(child->_values->value)->getLength() == size);
            }
        });
    add("Counter64 handles a long-form length header",
        []
        {
            for (Bytes bytes : {Bytes{0x46, 0x81, 1, 42}, Bytes{0x46, 0x82, 0, 1, 42},
                                Bytes{0x46, 0x83, 0, 0, 1, 42}})
            {
                Counter64 value;
                CHECK(value.fromBuffer(bytes.data()));
                CHECK(value._value == 42);
                CHECK(value.getLength() == 1);
            }
            Bytes maximum{0x46, 0x81, 9, 0, 255, 255, 255, 255, 255, 255, 255, 255};
            Counter64 value;
            CHECK(value.fromBuffer(maximum.data()));
            CHECK(value._value == UINT64_MAX && value.getLength() == 9);
        });
    add("Counter64 rejects invalid lengths and out-of-range contents",
        []
        {
            for (Bytes bytes : {Bytes{0x46, 0}, Bytes{0x46, 0x80}, Bytes{0x46, 0xff},
                                Bytes{0x46, 10}, Bytes{0x46, 0x82, 1, 0}, Bytes{0x46, 1, 0xff},
                                Bytes{0x46, 9, 1, 0, 0, 0, 0, 0, 0, 0, 0}})
            {
                Counter64 value(42);
                CHECK(!value.fromBuffer(bytes.data()));
                CHECK(value._value == 42);
            }
        });
    add("child length cannot exceed enclosing sequence",
        []
        {
            // Backing bytes exist, but the parent declares only three content bytes.
            Bytes bytes{0x30, 3, 4, 5, 'a', 'b', 'c', 'd', 'e'};
            ComplexType root(STRUCTURE);
            CHECK(!root.fromBuffer(bytes.data()));
        });
    add("dangling child tag must not be silently skipped",
        []
        {
            // The final INTEGER tag is inside the parent; its length is missing.
            Bytes bytes{0x30, 3, 5, 0, 2, 0};
            ComplexType root(STRUCTURE);
            CHECK(!root.fromBuffer(bytes.data()));
        });
    add("OID four-octet subidentifier encoding",
        []
        {
            char name[] = ".1.3.268435455.0";
            OIDType value(name);
            CHECK(encode(value) == Bytes({6, 6, 43, 0xff, 0xff, 0xff, 0x7f, 0}));
        });
    add("OID four-octet boundary encoding",
        []
        {
            char name[] = ".1.3.2097152.0";
            OIDType value(name);
            CHECK(encode(value) == Bytes({6, 6, 43, 0x81, 0x80, 0x80, 0, 0}));
        });
    add("OID ten-digit segment encoding preserves following arc",
        []
        {
            // A nonterminal segment also exercises copying the trailing dot.
            char name[] = ".1.3.1000000000.0";
            OIDType value(name);
            CHECK(encode(value) == Bytes({6, 7, 43, 0x83, 0xdc, 0xeb, 0x94, 0, 0}));
        });
    add("OID unsigned ten-digit segment decoding",
        []
        {
            Bytes bytes{6, 7, 43, 0x8f, 0xff, 0xff, 0xff, 0x7f, 0};
            OIDType value;
            CHECK(value.fromBuffer(bytes.data()));
            CHECK(std::string(value._value) == ".1.3.4294967295.0");
        });
}
