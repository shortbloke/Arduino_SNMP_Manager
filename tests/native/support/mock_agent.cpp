#include "mock_agent.h"
#include <algorithm>
#include <climits>

namespace
{
struct TLV
{
    unsigned tag;
    Bytes value;
};
TLV take(const Bytes &bytes, size_t &offset)
{
    CHECK(offset + 2 <= bytes.size());
    unsigned tag = bytes[offset++];
    size_t length = bytes[offset++];
    if (length & 128)
    {
        unsigned count = length & 127;
        CHECK(count && count <= sizeof(size_t) && offset + count <= bytes.size());
        length = 0;
        while (count--)
        {
            CHECK(length <= (SIZE_MAX >> 8));
            length = length * 256 + bytes[offset++];
        }
    }
    CHECK(length <= bytes.size() - offset);
    TLV result{tag, Bytes(bytes.begin() + offset, bytes.begin() + offset + length)};
    offset += length;
    return result;
}
std::vector<TLV> fields(const TLV &parent)
{
    std::vector<TLV> result;
    size_t offset = 0;
    while (offset < parent.value.size())
        result.push_back(take(parent.value, offset));
    return result;
}
int32_t number(const TLV &value)
{
    CHECK(value.tag == 2 && !value.value.empty() && value.value.size() <= 4);
    int64_t result = value.value.front() & 128 ? -1 : 0;
    for (unsigned byte : value.value)
        result = result * 256 + byte;
    CHECK(result >= INT32_MIN && result <= INT32_MAX);
    return static_cast<int32_t>(result);
}
MockAgent::OID decodeOID(const TLV &value)
{
    CHECK(value.tag == 6 && !value.value.empty());
    std::vector<uint64_t> arcs;
    uint64_t arc = 0;
    for (unsigned byte : value.value)
    {
        CHECK(arc <= (UINT64_MAX >> 7));
        arc = arc * 128 + (byte & 127);
        if (!(byte & 128))
        {
            arcs.push_back(arc);
            arc = 0;
        }
    }
    CHECK(!(value.value.back() & 128));
    uint32_t first = arcs[0] < 40 ? 0 : (arcs[0] < 80 ? 1 : 2);
    CHECK(arcs[0] - first * 40 <= UINT32_MAX);
    MockAgent::OID result{first, static_cast<uint32_t>(arcs[0] - first * 40)};
    for (size_t i = 1; i < arcs.size(); ++i)
    {
        CHECK(arcs[i] <= UINT32_MAX);
        result.push_back(static_cast<uint32_t>(arcs[i]));
    }
    return result;
}
void concatenate(Bytes &target, const Bytes &source)
{
    target.insert(target.end(), source.begin(), source.end());
}
}

MockAgent::OID MockAgent::oid(const char *text)
{
    CHECK(text && *text);
    if (*text == '.')
        ++text;
    OID result;
    while (*text)
    {
        CHECK(*text >= '0' && *text <= '9');
        uint64_t arc = 0;
        while (*text >= '0' && *text <= '9')
        {
            arc = arc * 10 + (*text++ - '0');
            CHECK(arc <= UINT32_MAX);
        }
        result.push_back(static_cast<uint32_t>(arc));
        if (!*text)
            break;
        CHECK(*text++ == '.' && *text);
    }
    CHECK(result.size() >= 2 && result[0] <= 2 && (result[0] == 2 || result[1] < 40));
    return result;
}
Bytes MockAgent::wireOID(const OID &name)
{
    CHECK(name.size() >= 2);
    Bytes contents;
    for (size_t i = 1; i < name.size(); ++i)
    {
        uint64_t arc = i == 1 ? uint64_t(name[0]) * 40 + name[1] : name[i];
        Bytes bytes{static_cast<unsigned char>(arc & 127)};
        while ((arc >>= 7))
            bytes.insert(bytes.begin(), static_cast<unsigned char>((arc & 127) | 128));
        concatenate(contents, bytes);
    }
    return tlv(6, contents);
}
Bytes MockAgent::integer(int32_t value)
{
    uint32_t raw = static_cast<uint32_t>(value);
    Bytes bytes{static_cast<unsigned char>(raw >> 24), static_cast<unsigned char>(raw >> 16),
                static_cast<unsigned char>(raw >> 8), static_cast<unsigned char>(raw)};
    while (bytes.size() > 1 &&
           ((bytes[0] == 0 && !(bytes[1] & 128)) || (bytes[0] == 255 && (bytes[1] & 128))))
        bytes.erase(bytes.begin());
    return tlv(2, bytes);
}
void MockAgent::put(const char *name, Bytes value)
{
    size_t offset = 0;
    take(value, offset);
    CHECK(offset == value.size());
    values_[oid(name)] = std::move(value);
}
Bytes MockAgent::answer(const Bytes &request)
{
    size_t offset = 0;
    TLV packet = take(request, offset);
    CHECK(packet.tag == 0x30 && offset == request.size());
    auto envelope = fields(packet);
    CHECK(envelope.size() == 3 && envelope[1].tag == 4);
    int32_t version = number(envelope[0]);
    CHECK(version == 0 || version == 1);
    unsigned type = envelope[2].tag;
    CHECK(type == 0xa0 || type == 0xa1 || (version == 1 && type == 0xa5));
    auto pdu = fields(envelope[2]);
    CHECK(pdu.size() == 4 && pdu[3].tag == 0x30);
    int32_t nonRepeaters = number(pdu[1]), repetitions = number(pdu[2]);
    CHECK(nonRepeaters >= 0 && repetitions >= 0 && repetitions <= 256);
    Exchange exchange;
    exchange.version = static_cast<unsigned>(version);
    exchange.pdu = type;
    exchange.id = number(pdu[0]);
    exchange.nonRepeaters = nonRepeaters;
    exchange.maxRepetitions = repetitions;
    for (const auto &binding : fields(pdu[3]))
    {
        CHECK(binding.tag == 0x30);
        auto pair = fields(binding);
        CHECK(pair.size() == 2 && pair[1].tag == 5 && pair[1].value.empty());
        exchange.requested.push_back(decodeOID(pair[0]));
    }
    CHECK(!exchange.requested.empty());
    Bytes bindings;
    auto emit = [&](const OID &name, const Bytes &value)
    {
        exchange.returned.push_back(name);
        concatenate(bindings, tlv(0x30, join({wireOID(name), value})));
    };
    auto next = [&](OID &cursor, unsigned requestIndex)
    {
        auto found = values_.upper_bound(cursor);
        if (found == values_.end())
        {
            if (version == 0)
            {
                exchange.error = 2;
                exchange.errorIndex = requestIndex + 1;
            }
            else
                emit(cursor, {0x82, 0});
        }
        else
        {
            cursor = found->first;
            emit(cursor, found->second);
        }
    };
    if (repeatNext && type != 0xa0)
    {
        repeatNext = false;
        emit(exchange.requested[0], {2, 1, 0});
    }
    else if (type == 0xa0)
    {
        for (size_t i = 0; i < exchange.requested.size(); ++i)
        {
            const auto &name = exchange.requested[i];
            auto found = values_.find(name);
            if (found != values_.end())
                emit(name, found->second);
            else if (version == 0)
            {
                exchange.error = 2;
                exchange.errorIndex = i + 1;
                break;
            }
            else
            {
                // Fixtures model scalar/column object identity as all but the last arc.
                OID prefix(name.begin(), name.end() - 1);
                bool known = false;
                for (const auto &entry : values_)
                    if (entry.first.size() == name.size() &&
                        std::equal(prefix.begin(), prefix.end(), entry.first.begin()))
                        known = true;
                emit(name, {static_cast<unsigned char>(known ? 0x81 : 0x80), 0});
            }
        }
    }
    else
    {
        std::vector<OID> cursors = exchange.requested;
        size_t singles =
            type == 0xa1 ? cursors.size() : std::min<size_t>(nonRepeaters, cursors.size());
        for (size_t i = 0; i < singles && !exchange.error; ++i)
            next(cursors[i], i);
        if (type == 0xa5)
            for (int32_t r = 0; r < repetitions; ++r)
                for (size_t i = singles; i < cursors.size(); ++i)
                    next(cursors[i], i);
    }
    auto build = [&]()
    {
        return tlv(0x30,
                   join({integer(version), tlv(4, envelope[1].value),
                         tlv(0xa2, join({integer(exchange.id), integer(exchange.error),
                                         integer(exchange.errorIndex), tlv(0x30, bindings)}))}));
    };
    if (exchange.error)
    {
        bindings.clear();
        exchange.returned.clear();
        for (const auto &name : exchange.requested)
            emit(name, {5, 0});
    }
    Bytes response = build();
    if (response.size() > responseLimit)
    {
        exchange.error = 1;
        exchange.errorIndex = 0;
        bindings.clear();
        exchange.returned.clear();
        if (version == 0)
            for (const auto &name : exchange.requested)
                emit(name, {5, 0});
        response = build();
    }
    if (truncateNext && !response.empty())
    {
        truncateNext = false;
        response.pop_back();
    }
    exchange.response = response;
    exchanges.push_back(exchange);
    return response;
}
bool MockAgent::service(UDP &udp)
{
    if (udp.packets == processedPackets_)
        return false;
    CHECK(udp.packets == processedPackets_ + 1);
    processedPackets_ = udp.packets;
    Bytes response = answer(udp.outgoing);
    if (dropNext || response.size() > responseLimit)
    {
        dropNext = false;
        return true;
    }
    CHECK(udp.incoming.empty());
    udp.peer = udp.destination;
    udp.peerPort = udp.destinationPort;
    udp.incoming = std::move(response);
    return true;
}
