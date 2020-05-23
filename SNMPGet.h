/*
	MFR: SNMPGet.h
	This file is almost an entire duplication of the SNMPTrap.h
	This needs refactoring to avoid code duplication.
*/

#ifndef SNMPGet_h
#define SNMPGet_h

class SNMPGet {
public:
	SNMPGet(const char* community, short version) : _community(community), _version(version) {
		if (version == 0) {
			version1 = true;
		}
		if (version == 1) {
			version2 = true;
		}
	};
	short _version;
	const char* _community;
	IPAddress agentIP;
	//OIDType* getOID;
	//TimestampCallback* uptimeCallback;
	short requestID;
	short errorID = 0;
	short errorIndex = 0;

	// the setters that need to be configured for each Get.

	void setRequestID(short request) {
		requestID = request;
	}

	void setIP(IPAddress ip) {
		agentIP = ip;
	}

	void setUDP(UDP* udp) {
		_udp = udp;
	}

// MFR: Remove commented out code? This is present in traps functions
/*	void setUptimeCallback(TimestampCallback* uptime) {
		uptimeCallback = uptime;
	}
*/	

    void addOIDPointer(ValueCallback* callback);


    ValueCallbacks* callbacks = new ValueCallbacks();
    ValueCallbacks* callbacksCursor = callbacks;


    UDP* _udp = 0;
    bool sendTo(IPAddress ip){
        if(!_udp){
            return false;
        }
        if(!build()){
            Serial.println("Failed Building packet..");
            delete packet;
            packet = 0;
            return false;
        }
        unsigned char _packetBuffer[SNMP_PACKET_LENGTH*3];
        memset(_packetBuffer, 0, SNMP_PACKET_LENGTH*3);
        int length = packet->serialise(_packetBuffer);
        delete packet;
        packet = 0;
        _udp->beginPacket(ip, 161); // MFR: Trap 162, Get 161
        _udp->write(_packetBuffer, length);
        return _udp->endPacket();
    }

    ComplexType* packet = 0;
    bool build();
    
    bool version1 = false;
    bool version2 = false;

	void clearOIDList() { // this just removes the list, does not kill the values in the list
		callbacksCursor = callbacks;
		delete callbacksCursor;
		callbacks = new ValueCallbacks();
		callbacksCursor = callbacks;
	}
};

bool SNMPGet::build() {
	if (packet) { packet = 0; }
	packet = new ComplexType(STRUCTURE);
	packet->addValueToList(new IntegerType((int)_version));
	packet->addValueToList(new OctetType((char*)_community));
	ComplexType* getPDU;
	getPDU = new ComplexType(GetRequestPDU);
	
	getPDU->addValueToList(new IntegerType(requestID));
	getPDU->addValueToList(new IntegerType(errorID));
	getPDU->addValueToList(new IntegerType(errorIndex));
	ComplexType* varBindList = new ComplexType(STRUCTURE);

	// MFR: removed commented out code?
/*	getPDU->addValueToList(new TimestampType(*(uptimeCallback->value)));
	getPDU->addValueToList(new OIDType(getOID->_value));
	getPDU->addValueToList(new NetworkAddress(agentIP));
*/

	//getPDU->addValueToList(new TimestampType(*(uptimeCallback->value)));
	

	callbacksCursor = callbacks;
	if (callbacksCursor->value) {
		while (true) {
			ComplexType* varBind = new ComplexType(STRUCTURE);
			varBind->addValueToList(new OIDType(callbacksCursor->value->OID));
			BER_CONTAINER* value;
			switch (callbacksCursor->value->type) {
				case INTEGER:
					{
						// MFR: Trap code uses callback which is commented out here?
						value = new IntegerType(0);//*((IntegerCallback*)callbacksCursor->value)->value);
						// value = new IntegerType(*((IntegerCallback*)callbacksCursor->value)->value);
					}
				break;
				case TIMESTAMP:
					{
						value = new TimestampType(*((TimestampCallback*)callbacksCursor->value)->value);
					}
				break;
				case STRING:
					{
						value = new OctetType(*((StringCallback*)callbacksCursor->value)->value);
					}
				break;
				case COUNTER32:
					{
						value = new IntegerType(0);
						// value = new Counter32(0);
						// value = new Counter32(*((Counter32Callback*)callbacksCursor->value)->value);
					}
				break;
				// case COUNTER64:
				// 	{
				// 		// value = new Counter64(0);
				// 		// value = new Counter64(*((Counter64Callback*)callbacksCursor->value)->value);
				// 	}
				// break;
				// case GUAGE32:
				// 	{
				// 		// value = new Guage(0);
				// 		// value = new Guage(*((Guage32Callback*)callbacksCursor->value)->value);
				// 	}
				// break;
			}
			
			varBind->addValueToList(value);
			varBindList->addValueToList(varBind);

			if (callbacksCursor->next) {
				callbacksCursor = callbacksCursor->next;
			} else {
				break;
			}
		}
	}

	getPDU->addValueToList(varBindList);
	packet->addValueToList(getPDU);
	return true;
}

void SNMPGet::addOIDPointer(ValueCallback* callback) {
    callbacksCursor = callbacks;
    if(callbacksCursor->value){
        while(callbacksCursor->next != 0){
            callbacksCursor = callbacksCursor->next;
        }
        callbacksCursor->next = new ValueCallbacks();
        callbacksCursor = callbacksCursor->next;
        callbacksCursor->value = callback;
        callbacksCursor->next = 0;
    } else 
        callbacks->value = callback;
}


#endif