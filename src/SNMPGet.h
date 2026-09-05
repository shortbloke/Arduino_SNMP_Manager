#ifndef SNMPGet_h
#define SNMPGet_h

enum SNMPExpect
{
	HEADER,
	SNMPVERSION,
	COMMUNITY,
	PDU,
	REQUESTID,
	ERRORSTATUS,
	ERRORID,
	VARBINDS,
	VARBIND,
	DONE
};

class SNMPGet
{
public:
	SNMPGet(const char *community, short version) : _community(community), _version(version)
	{
		if (version == 0)
		{
			version1 = true;
		}
		if (version == 1)
		{
			version2 = true;
		}
	};
    ~SNMPGet() { releaseCallbacks(); delete packet; }
    SNMPGet(const SNMPGet&) = delete;
    SNMPGet& operator=(const SNMPGet&) = delete;
    SNMPGet(SNMPGet&& other) : SNMPGet(other._community, other._version)
    {
        std::swap(callbacks,other.callbacks);
        std::swap(packet,other.packet);
        callbacksCursor=callbacks;
        other.callbacksCursor=other.callbacks;
        _udp=other._udp;
        agentIP=other.agentIP;
        port=other.port;
        requestID=other.requestID;
        errorID=other.errorID;
        errorIndex=other.errorIndex;
    }
    void releaseCallbacks()
    {
        for (ValueCallbacks *entry=callbacks; entry; entry=entry->next)
            if (entry->value) entry->value->release();
        delete callbacks;
        callbacks=callbacksCursor=nullptr;
    }
	const char *_community;
	short _version;
	IPAddress agentIP;
	uint16_t port = 161;
	int32_t requestID = 0;
	short errorID = 0;
	short errorIndex = 0;

	// Configure the request ID, destination, port, and transport.

	void setRequestID(int32_t request)
	{
		requestID = request;
	}

	void setIP(IPAddress ip)
	{
		agentIP = ip;
	}

	void setPort(uint16_t portnumber)
	{
		port = portnumber;
	}

	void setUDP(UDP *udp)
	{
		_udp = udp;
	}

	bool addOIDPointer(ValueCallback *callback);
	ValueCallbacks *callbacks = new (std::nothrow) ValueCallbacks();
	ValueCallbacks *callbacksCursor = callbacks;

	UDP *_udp = 0;
	bool sendTo(IPAddress ip)
	{
		if (!_udp)
		{
			return false;
		}
        // Refuse a send before touching the transport if any callback has no free slot.
        for (ValueCallbacks *entry=callbacks; entry && entry->value; entry=entry->next)
            if (!entry->value->canTrack(static_cast<unsigned long>(requestID),_udp,ip)) return false;
		if (!build())
		{
			Serial.println(F("Failed Building packet.."));
			delete packet;
			packet = 0;
			return false;
		}
		unsigned char _packetBuffer[SNMP_PACKET_LENGTH];
		memset(_packetBuffer, 0, SNMP_PACKET_LENGTH);
		int length = packet->serialise(_packetBuffer, sizeof(_packetBuffer));
		delete packet;
		packet = 0;
        if (length < 0) return false;
#ifdef DEBUG
    Serial.print(F("[DEBUG] SNMPGet: Sending UDP packet to: "));
    Serial.print(ip);
    Serial.print(F(":"));
    Serial.println(port);
		Serial.print("[DEBUG] composed packet: ");
    for (int i = 0; i < length; i++)
    {
        if (_packetBuffer[i] < 16) Serial.print('0');
        Serial.print(_packetBuffer[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
#endif
        if (!_udp->beginPacket(ip, port)) return false;
        if (_udp->write(_packetBuffer, length) != static_cast<size_t>(length)) return false;
        if (!_udp->endPacket()) return false;
        for (ValueCallbacks *entry = callbacks; entry && entry->value; entry = entry->next)
        {
            ValueCallback *callback = entry->value;
            callback->track(static_cast<unsigned long>(requestID),_udp,ip);
        }
        return true;
	}

	ComplexType *packet = 0;
	bool build();

	bool version1 = false;
	bool version2 = false;

	void clearOIDList()
    { // Release this request's references; registrations may still belong to a manager.
        releaseCallbacks();
		callbacks = new (std::nothrow) ValueCallbacks();
		callbacksCursor = callbacks;
	}
};

inline bool SNMPGet::build()
{
    delete packet;
    packet = nullptr;
    if (!_community) return false;
    std::unique_ptr<ComplexType> root(new (std::nothrow) ComplexType(STRUCTURE));
    std::unique_ptr<ComplexType> pdu(new (std::nothrow) ComplexType(GetRequestPDU));
    std::unique_ptr<ComplexType> bindings(new (std::nothrow) ComplexType(STRUCTURE));
    if (!root || !pdu || !bindings) return false;
    if (!root->addValueToList(new (std::nothrow) IntegerType(_version)) ||
        !root->addValueToList(new (std::nothrow) OctetType(const_cast<char *>(_community))) ||
        !pdu->addValueToList(new (std::nothrow) IntegerType(requestID)) ||
        !pdu->addValueToList(new (std::nothrow) IntegerType(errorID)) ||
        !pdu->addValueToList(new (std::nothrow) IntegerType(errorIndex))) return false;
    for (ValueCallbacks *entry=callbacks; entry && entry->value; entry=entry->next)
    {
        std::unique_ptr<ComplexType> binding(new (std::nothrow) ComplexType(STRUCTURE));
        if (!binding || !binding->addValueToList(new (std::nothrow) OIDType(entry->value->OID)) ||
            !binding->addValueToList(new (std::nothrow) NullType()) ||
            !bindings->addValueToList(binding.release())) return false;
    }
    if (!pdu->addValueToList(bindings.release()) || !root->addValueToList(pdu.release())) return false;
    packet = root.release();
    return true;
}

inline bool SNMPGet::addOIDPointer(ValueCallback *callback)
{
    if (!callback) return false;
    ValueCallbacks **tail = &callbacks;
    while (*tail && (*tail)->value) tail = &(*tail)->next;
    if (!*tail) *tail = new (std::nothrow) ValueCallbacks();
    if (!*tail) return false;
    callback->retain();
    (*tail)->value = callback;
    callbacksCursor = *tail;
    return true;
}

#endif
