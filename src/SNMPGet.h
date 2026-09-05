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

	void addOIDPointer(ValueCallback *callback);
	ValueCallbacks *callbacks = new ValueCallbacks();
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
		callbacks = new ValueCallbacks();
		callbacksCursor = callbacks;
	}
};

inline bool SNMPGet::build()
{
	// Build the community wrapper and GetRequest PDU.
    delete packet;
    packet = nullptr;
	packet = new ComplexType(STRUCTURE);
	packet->addValueToList(new IntegerType((int)_version));
	packet->addValueToList(new OctetType((char *)_community));
	ComplexType *getPDU;
	getPDU = new ComplexType(GetRequestPDU);
	getPDU->addValueToList(new IntegerType(requestID));
	getPDU->addValueToList(new IntegerType(errorID));
	getPDU->addValueToList(new IntegerType(errorIndex));
	ComplexType *varBindList = new ComplexType(STRUCTURE);

	callbacksCursor = callbacks;
	if (callbacksCursor->value)
	{
		while (true)
		{
			ComplexType *varBind = new ComplexType(STRUCTURE);
			varBind->addValueToList(new OIDType(callbacksCursor->value->OID));
			// Each requested OID uses an ASN.1 NULL value placeholder.
			BER_CONTAINER *value = new NullType();
			varBind->addValueToList(value);
			varBindList->addValueToList(varBind);

			if (callbacksCursor->next)
			{
				callbacksCursor = callbacksCursor->next;
			}
			else
			{
				break;
			}
		}
	}
	getPDU->addValueToList(varBindList);
	packet->addValueToList(getPDU);
	return true;
}

inline void SNMPGet::addOIDPointer(ValueCallback *callback)
{
    if (!callback) return;
    callback->retain();
	callbacksCursor = callbacks;
	if (callbacksCursor->value)
	{
		while (callbacksCursor->next != 0)
		{
			callbacksCursor = callbacksCursor->next;
		}
		callbacksCursor->next = new ValueCallbacks();
		callbacksCursor = callbacksCursor->next;
		callbacksCursor->value = callback;
		callbacksCursor->next = 0;
	}
	else
		callbacks->value = callback;
}

#endif