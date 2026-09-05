#ifndef SNMPGetResponse_h
#define SNMPGetResponse_h

class SNMPGetResponse
{

public:
    SNMPGetResponse() {};
    SNMPGetResponse(const SNMPGetResponse &other)
    {
        *this = other;
    }
    SNMPGetResponse &operator=(const SNMPGetResponse &other)
    {
        if (this == &other)
            return *this;
        if (!other.SNMPPacket)
        {
            delete varBinds;
            delete SNMPPacket;
            varBinds = varBindsCursor = nullptr;
            SNMPPacket = nullptr;
            communityString = nullptr;
            communityLength = 0;
            isCorrupt = other.isCorrupt;
            return *this;
        }
        std::unique_ptr<BER_CONTAINER> tree(other.SNMPPacket->clone());
        if (!tree)
            return *this;
        const int size = tree->serialise(nullptr);
        if (size < 0)
            return *this;
        std::unique_ptr<unsigned char[]> bytes(new (std::nothrow) unsigned char[size]);
        if (!bytes || tree->serialise(bytes.get(), size) != size)
            return *this;
        SNMPGetResponse copy;
        if (!copy.parseFrom(bytes.get(), size))
            return *this;
        std::swap(varBinds, copy.varBinds);
        std::swap(varBindsCursor, copy.varBindsCursor);
        std::swap(SNMPPacket, copy.SNMPPacket);
        communityString = copy.communityString;
        communityLength = copy.communityLength;
        version = copy.version;
        requestType = copy.requestType;
        requestID = copy.requestID;
        errorStatus = copy.errorStatus;
        errorIndex = copy.errorIndex;
        EXPECTING = copy.EXPECTING;
        isCorrupt = copy.isCorrupt;
        return *this;
    }
    ~SNMPGetResponse()
    {
        delete varBinds;
        delete SNMPPacket;
    };
    char *communityString = nullptr;
    size_t communityLength = 0;
    int version = 0;
    ASN_TYPE requestType = GetResponsePDU;
    unsigned long requestID = 0;
    int errorStatus = 0;
    int errorIndex = 0;
    VarBindList *varBinds = 0;
    VarBindList *varBindsCursor = 0;

    ComplexType *SNMPPacket = 0;
    bool parseFrom(unsigned char *buf)
    {
        return parseFrom(buf, static_cast<size_t>(-1));
    }
    bool parseFrom(unsigned char *buf, size_t available);
    enum SNMPExpect EXPECTING = SNMPVERSION;
    bool isCorrupt = false;
};

inline bool SNMPGetResponse::parseFrom(unsigned char *buf, size_t available)
{
    delete varBinds;
    varBinds = varBindsCursor = nullptr;
    delete SNMPPacket;
    SNMPPacket = nullptr;
    communityString = nullptr;
    communityLength = 0;
    EXPECTING = SNMPVERSION;
    isCorrupt = true;
    if (!buf || available < 2 || buf[0] != STRUCTURE)
        return false;
    SNMPPacket = new (std::nothrow) ComplexType(STRUCTURE);
    if (!SNMPPacket || !SNMPPacket->fromBuffer(buf, available))
        return false;

    // Validate the message and PDU shapes before dereferencing their fields.
    ValuesList *versionField = SNMPPacket->_values;
    ValuesList *communityField = versionField ? versionField->next : nullptr;
    ValuesList *pduField = communityField ? communityField->next : nullptr;
    if (!versionField || !communityField || !pduField || pduField->next ||
        versionField->value->_type != INTEGER || communityField->value->_type != STRING)
        return false;
    requestType = pduField->value->_type;
    if (requestType != GetRequestPDU && requestType != GetNextRequestPDU &&
        requestType != GetResponsePDU && requestType != SetRequestPDU)
        return false;
    version = static_cast<IntegerType *>(versionField->value)->_value + 1;
    communityString = static_cast<OctetType *>(communityField->value)->_value;
    communityLength = static_cast<OctetType *>(communityField->value)->getLength();
    ValuesList *id = static_cast<ComplexType *>(pduField->value)->_values;
    ValuesList *status = id ? id->next : nullptr;
    ValuesList *index = status ? status->next : nullptr;
    ValuesList *bindings = index ? index->next : nullptr;
    if (!id || !status || !index || !bindings || bindings->next || id->value->_type != INTEGER ||
        status->value->_type != INTEGER || index->value->_type != INTEGER ||
        bindings->value->_type != STRUCTURE)
        return false;
    requestID = static_cast<IntegerType *>(id->value)->_value;
    errorStatus = static_cast<IntegerType *>(status->value)->_value;
    errorIndex = static_cast<IntegerType *>(index->value)->_value;
    varBinds = new (std::nothrow) VarBindList();
    if (!varBinds)
        return false;
    varBindsCursor = varBinds;
    for (ValuesList *entry = static_cast<ComplexType *>(bindings->value)->_values; entry;
         entry = entry->next)
    {
        if (entry->value->_type != STRUCTURE)
            return false;
        ValuesList *oid = static_cast<ComplexType *>(entry->value)->_values;
        if (!oid || oid->value->_type != OID || !oid->next || oid->next->next)
            return false;
        VarBind *binding = new (std::nothrow) VarBind();
        if (!binding)
            return false;
        binding->oid = static_cast<OIDType *>(oid->value);
        binding->type = oid->next->value->_type;
        binding->value = oid->next->value;
        varBindsCursor->value = binding;
        varBindsCursor->next = new (std::nothrow) VarBindList();
        if (!varBindsCursor->next)
            return false;
        varBindsCursor = varBindsCursor->next;
    }
    EXPECTING = DONE;
    isCorrupt = false;
    return true;
}

#endif
