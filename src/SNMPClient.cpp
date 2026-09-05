#include "SNMPClient.h"
#include <cstring>
#include <cstdio>
#include <limits.h>

namespace
{
bool parseAddress(const char *text, IPAddress &address)
{
    if (!text)
        return false;
    uint8_t bytes[4];
    for (unsigned i = 0; i < 4; ++i)
    {
        unsigned value = 0, digits = 0;
        while (*text >= '0' && *text <= '9')
        {
            value = value * 10 + (*text++ - '0');
            if (++digits > 3 || value > 255)
                return false;
        }
        if (!digits)
            return false;
        bytes[i] = static_cast<uint8_t>(value);
        if (i != 3 && *text++ != '.')
            return false;
    }
    if (*text)
        return false;
    address = IPAddress(bytes);
    return true;
}

// Wrap already encoded contents in-place. No BER object tree is allocated to send.
bool wrap(unsigned char *buffer, size_t capacity, size_t start, size_t &end, ASN_TYPE tag)
{
    size_t length = end - start;
    unsigned char header[4] = {static_cast<unsigned char>(tag), 0, 0, 0};
    size_t count = 2;
    if (length < 128)
        header[1] = static_cast<unsigned char>(length);
    else if (length <= 255)
    {
        header[1] = 0x81;
        header[2] = static_cast<unsigned char>(length);
        count = 3;
    }
    else if (length <= 65535)
    {
        header[1] = 0x82;
        header[2] = length >> 8;
        header[3] = length & 255;
        count = 4;
    }
    else
        return false;
    if (end > capacity || count > capacity - end)
        return false;
    memmove(buffer + start + count, buffer + start, length);
    memcpy(buffer + start, header, count);
    end += count;
    return true;
}
bool append(unsigned char *buffer, size_t capacity, size_t &end, BER_CONTAINER &value)
{
    int size = value.serialise(buffer + end, capacity - end);
    if (size < 0)
        return false;
    end += static_cast<size_t>(size);
    return true;
}
bool appendValue(unsigned char *buffer, size_t capacity, size_t &end, const SNMPValue &value)
{
    if ((value.type == INTEGER || value.type == COUNTER32 || value.type == GAUGE32 ||
         value.type == TIMESTAMP) &&
        value.number > UINT32_MAX)
        return false;
    switch (value.type)
    {
    case INTEGER:
    {
        IntegerType n(static_cast<uint32_t>(value.number));
        return append(buffer, capacity, end, n);
    }
    case COUNTER32:
    case GAUGE32:
    case TIMESTAMP:
    {
        IntegerType n(static_cast<uint32_t>(value.number));
        n._type = value.type;
        return append(buffer, capacity, end, n);
    }
    case COUNTER64:
    {
        Counter64 n(value.number);
        return append(buffer, capacity, end, n);
    }
    case STRING:
    case OPAQUE:
    {
        if (value.length > SNMP_VALUE_MAX_LENGTH || value.length > capacity - end)
            return false;
        size_t start = end;
        memcpy(buffer + end, value.bytes, value.length);
        end += value.length;
        return wrap(buffer, capacity, start, end, value.type);
    }
    case OID:
    {
        if (value.length > SNMP_VALUE_MAX_LENGTH || value.bytes[value.length] ||
            memchr(value.bytes, 0, value.length))
            return false;
        OIDType oid(const_cast<char *>(value.text()));
        return append(buffer, capacity, end, oid);
    }
    case NETWORK_ADDRESS:
    {
        if (value.length != 4)
            return false;
        NetworkAddress ip(IPAddress(value.bytes));
        return append(buffer, capacity, end, ip);
    }
    default:
        return false;
    }
}
// Canonical dotted OIDs are compared by numeric arcs, not textual ordering.
int compareOID(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a == '.')
            ++a;
        if (*b == '.')
            ++b;
        uint64_t x = 0, y = 0;
        while (*a >= '0' && *a <= '9')
            x = x * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9')
            y = y * 10 + (*b++ - '0');
        if (x != y)
            return x < y ? -1 : 1;
    }
    return *a ? 1 : (*b ? -1 : 0);
}

SNMPStatus copyValue(BER_CONTAINER &source, SNMPValue &target)
{
    target = SNMPValue();
    target.type = source._type;
    switch (source._type)
    {
    case INTEGER:
    case COUNTER32:
    case GAUGE32:
    case TIMESTAMP:
        target.number = static_cast<IntegerType &>(source)._value;
        break;
    case COUNTER64:
        target.number = static_cast<Counter64 &>(source)._value;
        break;
    case NETWORK_ADDRESS:
    {
        IPAddress ip = static_cast<NetworkAddress &>(source)._value;
        unsigned char octets[4];
        for (unsigned i = 0; i < 4; ++i)
            octets[i] = ip[i];
        return target.setBytes(octets, 4, NETWORK_ADDRESS);
    }
    case OID:
    {
        const char *text = static_cast<OIDType &>(source)._value;
        return target.setBytes(reinterpret_cast<const unsigned char *>(text), strlen(text), OID);
    }
    case STRING:
    case OPAQUE:
    {
        const unsigned char *data =
            source._type == STRING
                ? reinterpret_cast<const unsigned char *>(static_cast<OctetType &>(source)._value)
                : static_cast<RawType &>(source)._value;
        return target.setBytes(data, static_cast<size_t>(source.getLength()), source._type);
    }
    case NOSUCHOBJECT:
    case NOSUCHINSTANCE:
    case ENDOFMIBVIEW:
        return SNMPStatus::Missing;
    case NULLTYPE:
        break;
    default:
        return SNMPStatus::Unsupported;
    }
    return SNMPStatus::Success;
}
}

const char *SNMPStatus::message() const
{
    static const char *const messages[] = {"Success",
                                           "Pending",
                                           "Operation is busy",
                                           "Invalid IPv4 address",
                                           "Invalid device configuration",
                                           "Invalid OID",
                                           "Result or packet capacity exceeded",
                                           "Client has not started",
                                           "UDP operation failed",
                                           "Device did not respond",
                                           "Cancelled",
                                           "Device does not provide this value",
                                           "Unexpected value type",
                                           "Invalid response or agent error",
                                           "Some values could not be read",
                                           "Unsupported operation",
                                           "Could not allocate result storage"};
    return messages[static_cast<unsigned>(code_)];
}
SNMPValue SNMPValue::integer32(int32_t n)
{
    SNMPValue value;
    value.type = INTEGER;
    value.number = static_cast<uint32_t>(n);
    return value;
}
SNMPValue SNMPValue::counter32(uint32_t n)
{
    SNMPValue value;
    value.type = COUNTER32;
    value.number = n;
    return value;
}
void SNMPValue::release()
{
    if (payload_ && --payload_->references == 0)
    {
        payload_->~Payload();
        ::operator delete(payload_);
    }
}
SNMPValue::~SNMPValue()
{
    release();
}
SNMPValue::SNMPValue(const SNMPValue &other)
    : type(other.type), number(other.number), bytes(other.bytes), length(other.length),
      payload_(other.payload_)
{
    if (payload_)
        ++payload_->references;
}
SNMPValue &SNMPValue::operator=(const SNMPValue &other)
{
    if (this != &other)
    {
        release();
        type = other.type;
        number = other.number;
        bytes = other.bytes;
        length = other.length;
        payload_ = other.payload_;
        if (payload_)
            ++payload_->references;
    }
    return *this;
}
SNMPStatus SNMPValue::setBytes(const unsigned char *data, size_t size, ASN_TYPE tag)
{
    if ((tag != STRING && tag != OPAQUE && tag != OID && tag != NETWORK_ADDRESS) || (!data && size))
        return SNMPStatus::InvalidConfiguration;
    if (size > SNMP_VALUE_MAX_LENGTH || (tag == OID && size >= MAX_OID_LENGTH))
        return SNMPStatus::CapacityExceeded;
    if ((tag == NETWORK_ADDRESS && size != 4) || (tag == OID && (!size || memchr(data, 0, size))))
        return SNMPStatus::InvalidConfiguration;
    Payload *storage = nullptr;
    if (size)
    {
        void *memory = ::operator new(sizeof(Payload) + size + 1, std::nothrow);
        if (!memory)
            return SNMPStatus::AllocationFailure;
        storage = new (memory) Payload{1};
        unsigned char *destination = reinterpret_cast<unsigned char *>(storage + 1);
        memcpy(destination, data, size);
        destination[size] = 0;
    }
    release();
    payload_ = storage;
    type = tag;
    number = 0;
    length = size;
    bytes = storage ? reinterpret_cast<const unsigned char *>(storage + 1)
                    : reinterpret_cast<const unsigned char *>("");
    return SNMPStatus::Success;
}
SNMPDevice::SNMPDevice(SNMPClient &client, IPAddress address, const char *community,
                       SNMPVersion version)
    : client_(client), address_(address), version_(version), status_(SNMPStatus::Success)
{
    if (!community || strlen(community) >= sizeof(community_) ||
        (version != SNMPVersion::Version1 && version != SNMPVersion::Version2c))
        status_ = SNMPStatus::InvalidConfiguration;
    else
        strcpy(community_, community);
}
SNMPDevice::SNMPDevice(SNMPClient &client, const char *address, const char *community,
                       SNMPVersion version)
    : SNMPDevice(client, IPAddress(), community, version)
{
    if (!parseAddress(address, address_))
        status_ = SNMPStatus::InvalidAddress;
}
SNMPOperation::SNMPOperation(SNMPDevice &device, SNMPResult *results, size_t capacity)
    : device_(device), results_(results), capacity_(capacity)
{
}
SNMPOperation::~SNMPOperation()
{
    device_.client_.remove(*this);
}
SNMPStatus SNMPOperation::add(const char *oid, ASN_TYPE expected, const SNMPValue *value)
{
    if (pending())
        return SNMPStatus::Busy;
    if (count_ == capacity_)
        return SNMPStatus::CapacityExceeded;
    if (!oid || strlen(oid) >= MAX_OID_LENGTH)
        return SNMPStatus::InvalidOID;
    OIDType parsed(const_cast<char *>(oid));
    unsigned char wire[MAX_OID_LENGTH];
    int n = parsed.serialise(wire, sizeof(wire));
    if (n < 0 || !parsed.fromBuffer(wire, n))
        return SNMPStatus::InvalidOID;
    for (size_t i = 0; i < count_; ++i)
        if (strcmp(results_[i].oid, parsed._value) == 0)
            return SNMPStatus::InvalidOID;
    if (value)
    {
        switch (value->type)
        {
        case INTEGER:
        case COUNTER32:
        case GAUGE32:
        case TIMESTAMP:
            if (value->number > UINT32_MAX)
                return SNMPStatus::InvalidConfiguration;
            break;
        case COUNTER64:
            break;
        case STRING:
        case OPAQUE:
            if (value->length > SNMP_VALUE_MAX_LENGTH)
                return SNMPStatus::CapacityExceeded;
            break;
        case OID:
        {
            if (value->length >= MAX_OID_LENGTH || value->length > SNMP_VALUE_MAX_LENGTH ||
                memchr(value->bytes, 0, value->length))
                return SNMPStatus::InvalidOID;
            OIDType object(const_cast<char *>(value->text()));
            if (object.serialise(nullptr) < 0)
                return SNMPStatus::InvalidOID;
            break;
        }
        case NETWORK_ADDRESS:
            if (value->length != 4)
                return SNMPStatus::InvalidConfiguration;
            break;
        default:
            return SNMPStatus::Unsupported;
        }
        if (value->type == COUNTER64 && device_.version_ == SNMPVersion::Version1)
            return SNMPStatus::Unsupported;
    }
    SNMPResult &result = results_[count_++];
    result = SNMPResult();
    strcpy(result.oid, parsed._value);
    result.expected = expected;
    if (value)
        result.value = *value;
    return SNMPStatus::Success;
}
SNMPStatus SNMPOperation::setRoot(const char *root)
{
    if (pending())
        return SNMPStatus::Busy;
    size_t old = count_;
    count_ = 0;
    SNMPStatus status = add(root, NULLTYPE);
    if (status.ok())
        strcpy(root_, results_[0].oid);
    count_ = old;
    return status;
}
SNMPStatus SNMPOperation::start()
{
    return device_.client_.schedule(*this);
}
void SNMPOperation::cancel()
{
    if (pending())
        finish(SNMPStatus::Cancelled);
}
void SNMPOperation::finish(SNMPStatus status)
{
    for (size_t i = 0; i < count_; ++i)
        if (results_[i].status.code() == SNMPStatus::Pending)
            results_[i].status = status;
    status_ = status;
    completed_ = true;
    device_.client_.remove(*this);
    if (completionHandler_)
        completionHandler_(*this, completionContext_);
}
SNMPClient::~SNMPClient()
{
    bool wasBegun = begun_;
    begun_ = false;
    for (auto *operation : pending_)
        if (operation)
            operation->finish(SNMPStatus::Cancelled);
    if (wasBegun)
        udp_.stop();
}
SNMPStatus SNMPClient::begin(uint16_t port)
{
    if (begun_)
        return SNMPStatus::Success;
    begun_ = udp_.begin(port) != 0;
    return begun_ ? SNMPStatus::Success : SNMPStatus::TransportError;
}
SNMPStatus SNMPClient::schedule(SNMPOperation &operation)
{
    if (operation.pending())
        return SNMPStatus::Busy;
    if (!begun_)
        return SNMPStatus::NotStarted;
    if (!operation.device_.status().ok())
        return operation.device_.status();
    if ((!operation.walking_ && !operation.count_) || (operation.walking_ && !operation.root_[0]) ||
        !operation.device_.port || !operation.device_.timeoutMs ||
        operation.device_.timeoutMs > INT32_MAX)
        return SNMPStatus::InvalidConfiguration;
    for (auto &slot : pending_)
        if (!slot)
        {
            slot = &operation;
            operation.status_ = SNMPStatus::Pending;
            if (operation.walking_)
            {
                operation.count_ = 0;
                strcpy(operation.cursor_, operation.root_);
                operation.mode_ = operation.device_.version_ == SNMPVersion::Version1
                                      ? GetNextRequestPDU
                                      : GetBulkRequestPDU;
            }
            operation.startedAt_ = 0;
            operation.agentError_ = 0;
            operation.agentErrorIndex_ = 0;
            operation.completed_ = false;
            operation.offset_ = 0;
            operation.sent_ = false;
            operation.batchLimit_ = operation.count_;
            operation.attempts_ = 0;
            operation.id_ = 0;
            for (size_t i = 0; i < operation.count_; ++i)
            {
                operation.results_[i].status = SNMPStatus::Pending;
                if (operation.mode_ != SetRequestPDU)
                    operation.results_[i].value = SNMPValue();
            }
            return SNMPStatus::Success;
        }
    return SNMPStatus::CapacityExceeded;
}
void SNMPClient::remove(SNMPOperation &operation)
{
    for (auto &slot : pending_)
        if (slot == &operation)
            slot = nullptr;
}

bool SNMPClient::send(SNMPOperation &op, uint32_t now)
{
    // Reserve envelope space while filling the binding list.
    size_t end = 0;
    op.batch_ = 0;
    const size_t reserve = strlen(op.device_.community_) + 40;
    if (reserve >= sizeof(buffer_))
    {
        op.finish(SNMPStatus::CapacityExceeded);
        return false;
    }
    if (op.walking_)
    {
        OIDType name(op.cursor_);
        NullType null;
        if (!append(buffer_, sizeof(buffer_) - reserve, end, name) ||
            !append(buffer_, sizeof(buffer_) - reserve, end, null) ||
            !wrap(buffer_, sizeof(buffer_) - reserve, 0, end, STRUCTURE))
        {
            op.finish(SNMPStatus::CapacityExceeded);
            return false;
        }
        op.batch_ = 1;
    }
    for (size_t i = op.offset_; !op.walking_ && i < op.count_ && op.batch_ < op.batchLimit_; ++i)
    {
        size_t start = end;
        OIDType name(op.results_[i].oid);
        NullType null;
        if (!append(buffer_, sizeof(buffer_) - reserve, end, name) ||
            !(op.mode_ == SetRequestPDU
                  ? appendValue(buffer_, sizeof(buffer_) - reserve, end, op.results_[i].value)
                  : append(buffer_, sizeof(buffer_) - reserve, end, null)) ||
            !wrap(buffer_, sizeof(buffer_) - reserve, start, end, STRUCTURE))
        {
            end = start;
            break;
        }
        ++op.batch_;
        if (end + (op.batch_ + 1) * 16 >= sizeof(buffer_) - reserve)
            break;
    }
    if (op.mode_ == SetRequestPDU && op.batch_ != op.count_)
    {
        op.finish(SNMPStatus::CapacityExceeded);
        return false;
    }
    if (!op.batch_)
    {
        op.finish(SNMPStatus::CapacityExceeded);
        return false;
    }
    if (!op.sent_)
    {
        // Exhaustion requires a new client instead of recycling IDs into late replies.
        if (nextID_ == INT32_MAX)
        {
            op.finish(SNMPStatus::CapacityExceeded);
            return false;
        }
        op.id_ = static_cast<int32_t>(++nextID_);
    }
    if (!wrap(buffer_, sizeof(buffer_), 0, end, STRUCTURE))
    {
        op.finish(SNMPStatus::CapacityExceeded);
        return false;
    }
    unsigned char prefix[96];
    size_t p = 0;
    IntegerType id(static_cast<uint32_t>(op.id_)), zero(0);
    append(prefix, sizeof(prefix), p, id);
    append(prefix, sizeof(prefix), p, zero);
    IntegerType repetitions(op.walking_ && op.mode_ == GetBulkRequestPDU ? 4 : 0);
    append(prefix, sizeof(prefix), p, repetitions);
    memmove(buffer_ + p, buffer_, end);
    memcpy(buffer_, prefix, p);
    end += p;
    if (!wrap(buffer_, sizeof(buffer_), 0, end, op.mode_))
    {
        op.finish(SNMPStatus::CapacityExceeded);
        return false;
    }
    p = 0;
    IntegerType version(static_cast<uint8_t>(op.device_.version_));
    append(prefix, sizeof(prefix), p, version);
    size_t communityStart = p, length = strlen(op.device_.community_);
    memcpy(prefix + p, op.device_.community_, length);
    p += length;
    wrap(prefix, sizeof(prefix), communityStart, p, STRING);
    memmove(buffer_ + p, buffer_, end);
    memcpy(buffer_, prefix, p);
    end += p;
    if (!wrap(buffer_, sizeof(buffer_), 0, end, STRUCTURE))
    {
        op.finish(SNMPStatus::CapacityExceeded);
        return false;
    }
    if (!udp_.beginPacket(op.device_.address_, op.device_.port) ||
        udp_.write(buffer_, end) != end || !udp_.endPacket())
    {
        op.finish(SNMPStatus::TransportError);
        return false;
    }
    op.sent_ = true;
    op.sentAt_ = now;
    return true;
}
void SNMPClient::receive()
{
    int size = udp_.parsePacket();
    if (size <= 0)
        return;
    if (static_cast<size_t>(size) > sizeof(buffer_))
    {
        udp_.flush();
        return;
    }
    IPAddress peer = udp_.remoteIP();
    uint16_t port = udp_.remotePort();
    int read = udp_.read(buffer_, size);
    udp_.flush();
    if (read != size)
        return;
    size_t header, length;
    if (!readBERHeader(buffer_, size, header, length) ||
        header + length != static_cast<size_t>(size))
        return;
    SNMPGetResponse response;
    if (!response.parseFrom(buffer_, size) || response.requestType != GetResponsePDU)
    {
        // Release the response parser's tree before parsing notification metadata.
        delete response.varBinds;
        response.varBinds = nullptr;
        delete response.SNMPPacket;
        response.SNMPPacket = nullptr;
        if (notificationHandler_)
            notify(size, peer, port);
        return;
    }
    for (auto *op : pending_)
    {
        if (!op || !op->sent_ || op->id_ != response.requestID || !(peer == op->device_.address_) ||
            port != op->device_.port ||
            response.version != static_cast<int>(op->device_.version_) + 1 ||
            response.communityLength != strlen(op->device_.community_) ||
            memcmp(response.communityString, op->device_.community_, response.communityLength))
            continue;
        op->agentError_ = response.errorStatus;
        op->agentErrorIndex_ = response.errorIndex;
        if (response.errorStatus < 0 || response.errorStatus > 18 || response.errorIndex < 0)
        {
            op->finish(SNMPStatus::ProtocolError);
            return;
        }
        if (!op->walking_ && op->mode_ == GetRequestPDU && response.errorStatus == 2 &&
            op->device_.version_ == SNMPVersion::Version1)
        {
            if (op->batch_ > 1)
            {
                op->batchLimit_ = 1;
            }
            else
            {
                op->results_[op->offset_++].status = SNMPStatus::Missing;
                if (op->offset_ == op->count_)
                {
                    op->finish(op->count_ == 1 ? SNMPStatus::Missing : SNMPStatus::Partial);
                    return;
                }
            }
            op->sent_ = false;
            op->attempts_ = 0;
            return;
        }
        if (response.errorStatus == 1 && op->batch_ > 1 && op->mode_ != SetRequestPDU)
        {
            op->batchLimit_ = op->batch_ / 2;
            op->sent_ = false;
            op->attempts_ = 0;
            return;
        }
        if (op->walking_ && response.errorStatus == 2 &&
            op->device_.version_ == SNMPVersion::Version1)
        {
            op->finish(SNMPStatus::Success);
            return;
        }
        if (op->walking_ && response.errorStatus == 1 && op->mode_ == GetBulkRequestPDU)
        {
            op->mode_ = GetNextRequestPDU;
            op->sent_ = false;
            op->attempts_ = 0;
            return;
        }
        if (response.errorStatus)
        {
            op->finish(response.errorStatus == 1 ? SNMPStatus::CapacityExceeded
                                                 : SNMPStatus::ProtocolError);
            return;
        }
        if (op->walking_)
        {
            bool any = false;
            for (VarBindList *b = response.varBinds; b && b->value; b = b->next)
            {
                const char *name = b->value->oid->_value;
                size_t rootLength = strlen(op->root_);
                if (b->value->type == ENDOFMIBVIEW)
                {
                    op->finish(SNMPStatus::Success);
                    return;
                }
                if (compareOID(name, op->cursor_) <= 0)
                {
                    op->finish(SNMPStatus::ProtocolError);
                    return;
                }
                if (strncmp(name, op->root_, rootLength) || name[rootLength] != '.')
                {
                    op->finish(SNMPStatus::Success);
                    return;
                }
                any = true;
                SNMPResult value;
                strcpy(value.oid, name);
                value.status = copyValue(*b->value->value, value.value);
                if (op->onValue_)
                {
                    int32_t generation = op->id_;
                    if (!op->onValue_(value, op->context_))
                    {
                        op->finish(SNMPStatus::CapacityExceeded);
                        return;
                    }
                    // Callbacks may cancel but must not destroy the active operation.
                    if (!op->pending() || op->id_ != generation)
                        return;
                }
                else
                {
                    if (op->count_ == op->capacity_)
                    {
                        op->finish(SNMPStatus::CapacityExceeded);
                        return;
                    }
                    op->results_[op->count_++] = value;
                }
                strcpy(op->cursor_, name);
            }
            if (!any)
            {
                op->finish(SNMPStatus::ProtocolError);
                return;
            }
            op->sent_ = false;
            op->attempts_ = 0;
            return;
        }
        // Require a complete, ordered response before committing any destinations.
        VarBindList *binding = response.varBinds;
        for (size_t i = 0; i < op->batch_; ++i)
        {
            if (!binding || !binding->value ||
                strcmp(binding->value->oid->_value, op->results_[op->offset_ + i].oid))
            {
                op->finish(SNMPStatus::ProtocolError);
                return;
            }
            binding = binding->next;
        }
        if (binding && binding->value)
        {
            op->finish(SNMPStatus::ProtocolError);
            return;
        }
        binding = response.varBinds;
        for (size_t i = 0; i < op->batch_; ++i, binding = binding->next)
        {
            SNMPResult &result = op->results_[op->offset_ + i];
            result.status = copyValue(*binding->value->value, result.value);
            if (result.status.ok() && result.expected != NULLTYPE &&
                result.expected != result.value.type)
                result.status = SNMPStatus::TypeMismatch;
        }
        op->offset_ += op->batch_;
        op->sent_ = false;
        op->attempts_ = 0;
        if (op->offset_ == op->count_)
        {
            SNMPStatus status = SNMPStatus::Success;
            for (size_t i = 0; i < op->count_; ++i)
                if (!op->results_[i].ok())
                    status =
                        op->count_ == 1 ? op->results_[i].status : SNMPStatus(SNMPStatus::Partial);
            op->finish(status);
        }
        return;
    }
}
void SNMPClient::loop()
{
    loop(static_cast<uint32_t>(millis()));
}
void SNMPClient::loop(uint32_t now)
{
    if (!begun_)
        return;
    receive();
    for (auto *op : pending_)
        if (op)
        {
            if (op->walking_ && op->id_ && uint32_t(now - op->startedAt_) >= 60000)
            {
                op->finish(SNMPStatus::Timeout);
                continue;
            }
            if (!op->sent_)
            {
                if (!op->id_)
                    op->startedAt_ = now;
                send(*op, now);
            }
            else if (uint32_t(now - op->sentAt_) >= op->device_.timeoutMs)
            {
                if (op->mode_ != SetRequestPDU && op->attempts_ < op->device_.retries)
                {
                    ++op->attempts_;
                    send(*op, now);
                }
                else
                    op->finish(SNMPStatus::Timeout);
            }
        }
}

size_t SNMPNotification::size() const
{
    size_t count = 0;
    for (const ValuesList *b = bindings; b; b = b->next)
        ++count;
    return count;
}
SNMPStatus SNMPNotification::read(size_t index, SNMPResult &result) const
{
    const ValuesList *b = bindings;
    while (b && index--)
        b = b->next;
    if (!b)
        return SNMPStatus::Missing;
    const ValuesList *fields = static_cast<ComplexType *>(b->value)->_values;
    result = SNMPResult();
    strcpy(result.oid, static_cast<OIDType *>(fields->value)->_value);
    result.status = copyValue(*fields->next->value, result.value);
    return result.status;
}
SNMPStatus SNMPClient::notifications(const char *community,
                                     bool (*handler)(const SNMPNotification &, void *),
                                     void *context)
{
    if (!community || strlen(community) >= sizeof(notificationCommunity_))
        return SNMPStatus::InvalidConfiguration;
    strcpy(notificationCommunity_, community);
    notificationHandler_ = handler;
    notificationContext_ = context;
    return SNMPStatus::Success;
}
void SNMPClient::notify(size_t size, IPAddress peer, uint16_t port)
{
    ComplexType packet(STRUCTURE);
    if (!packet.fromBuffer(buffer_, size))
        return;
    ValuesList *v = packet._values, *c = v ? v->next : nullptr, *p = c ? c->next : nullptr;
    if (!v || !c || !p || p->next || v->value->_type != INTEGER || c->value->_type != STRING)
        return;
    unsigned long version = static_cast<IntegerType *>(v->value)->_value;
    OctetType *community = static_cast<OctetType *>(c->value);
    if (community->getLength() != static_cast<int>(strlen(notificationCommunity_)) ||
        memcmp(community->_value, notificationCommunity_, community->getLength()))
        return;
    ASN_TYPE type = p->value->_type;
    if (!((version == 0 && type == TrapPDU) ||
          (version == 1 && (type == Trapv2PDU || type == InformRequestPDU))))
        return;
    ComplexType *pdu = static_cast<ComplexType *>(p->value);
    ValuesList *fields[6] = {};
    size_t count = 0;
    for (ValuesList *f = pdu->_values; f; f = f->next)
    {
        if (count == 6)
            return;
        fields[count++] = f;
    }
    SNMPNotification notification;
    notification.peer = peer;
    notification.port = port;
    notification.version = static_cast<SNMPVersion>(version);
    ValuesList *bindings;
    if (version == 0)
    {
        if (count != 6 || fields[0]->value->_type != OID ||
            fields[1]->value->_type != NETWORK_ADDRESS || fields[2]->value->_type != INTEGER ||
            fields[3]->value->_type != INTEGER || fields[4]->value->_type != TIMESTAMP ||
            fields[5]->value->_type != STRUCTURE)
            return;
        notification.enterprise = static_cast<OIDType *>(fields[0]->value)->_value;
        notification.agentAddress = static_cast<NetworkAddress *>(fields[1]->value)->_value;
        notification.genericTrap =
            static_cast<int32_t>(static_cast<IntegerType *>(fields[2]->value)->_value);
        notification.specificTrap =
            static_cast<int32_t>(static_cast<IntegerType *>(fields[3]->value)->_value);
        notification.uptime = static_cast<IntegerType *>(fields[4]->value)->_value;
        if (notification.genericTrap < 0 || notification.genericTrap > 6 ||
            notification.specificTrap < 0)
            return;
        bindings = static_cast<ComplexType *>(fields[5]->value)->_values;
    }
    else
    {
        if (count != 4 || fields[0]->value->_type != INTEGER ||
            fields[1]->value->_type != INTEGER || fields[2]->value->_type != INTEGER ||
            fields[3]->value->_type != STRUCTURE)
            return;
        if (static_cast<IntegerType *>(fields[1]->value)->_value ||
            static_cast<IntegerType *>(fields[2]->value)->_value)
            return;
        notification.requestID =
            static_cast<int32_t>(static_cast<IntegerType *>(fields[0]->value)->_value);
        notification.inform = type == InformRequestPDU;
        bindings = static_cast<ComplexType *>(fields[3]->value)->_values;
    }
    for (ValuesList *b = bindings; b; b = b->next)
    {
        if (b->value->_type != STRUCTURE)
            return;
        ValuesList *f = static_cast<ComplexType *>(b->value)->_values;
        if (!f || !f->next || f->next->next || f->value->_type != OID)
            return;
    }
    notification.bindings = bindings;
    if (version == 1)
    {
        SNMPResult uptime, trapOID;
        if (!notification.read(0, uptime).ok() || !notification.read(1, trapOID).ok() ||
            strcmp(uptime.oid, ".1.3.6.1.2.1.1.3.0") || uptime.value.type != TIMESTAMP ||
            strcmp(trapOID.oid, ".1.3.6.1.6.3.1.1.4.1.0") || trapOID.value.type != OID)
            return;
        notification.uptime = uptime.value.unsigned32();
    }
    if (!notificationHandler_(notification, notificationContext_) || !notification.inform)
        return;
    pdu->_type = GetResponsePDU;
    int length = packet.serialise(buffer_, sizeof(buffer_));
    if (length < 0)
        return;
    if (udp_.beginPacket(peer, port) && udp_.write(buffer_, length) == static_cast<size_t>(length))
        udp_.endPacket();
}
