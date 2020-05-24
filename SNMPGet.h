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
	// Build packet for making GetRequest
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
	
	callbacksCursor = callbacks;
	if (callbacksCursor->value) {
		while (true) {
			ComplexType* varBind = new ComplexType(STRUCTURE);
			varBind->addValueToList(new OIDType(callbacksCursor->value->OID));
			// Value can be null for Request payload.
			BER_CONTAINER* value = new NullType();
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