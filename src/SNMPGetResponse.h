#ifndef SNMPGetResponse_h
#define SNMPGetResponse_h

class SNMPGetResponse
{

public:
	SNMPGetResponse(){};
	~SNMPGetResponse()
	{
		delete varBinds;
		delete SNMPPacket;
	};
	char *communityString;
	int version;
	ASN_TYPE requestType;
	unsigned long requestID;
	int errorStatus;
	int errorIndex;
	VarBindList *varBinds = 0;
	VarBindList *varBindsCursor = 0;

	ComplexType *SNMPPacket = 0;
	bool parseFrom(unsigned char *buf);
	bool serialise(char *buf);
	enum SNMPExpect EXPECTING = SNMPVERSION;
	bool isCorrupt = false;
};

bool SNMPGetResponse::parseFrom(unsigned char *buf)
{
	// SNMPPacket = new ComplexType(STRUCTURE); // ensure SNMPPacket is initialised to avoid crash in deconstructor
	// confirm that the packet is a STRUCTURE
	if (buf[0] != 0x30)
	{
#ifdef DEBUG
		Serial.printf("[DEBUG] Packet is not an SNMPGetResponse, expected 0x30, received: 0x%02x\n", buf[0]);
#endif
		isCorrupt = true;
		return false;
	}
	SNMPPacket = new ComplexType(STRUCTURE); // ensure SNMPPacket is initialised to avoid crash in deconstructor
	SNMPPacket->fromBuffer(buf);
	int actualResponseLength = SNMPPacket->getLength();
	int declaredResponseLength = buf[1]; // SNMP packet length value

	if (actualResponseLength <= 30)
	{
		Serial.print(F("SNMP packet too short, needs to be > 30. Received only: "));
		Serial.println(actualResponseLength);
		return false;
	}
	if (declaredResponseLength != actualResponseLength)
	{
		Serial.printf("Packet Corrupt. Expected Payload size: %d - Actual size: %d\n", declaredResponseLength, actualResponseLength);
		isCorrupt = true;
		return false;
	}
	// we now have a full ASN.1 packet in SNMPPacket
	ValuesList *cursor = SNMPPacket->_values;
	ValuesList *tempCursor = NULL;
	while (EXPECTING != DONE)
	{
		switch (EXPECTING)
		{
		case SNMPVERSION:
			if (cursor->value->_type == INTEGER)
			{
				version = ((IntegerType *)cursor->value)->_value + 1;
				if (!cursor->next)
				{
					isCorrupt = true;
					return false;
				}
				cursor = cursor->next;
				EXPECTING = COMMUNITY;
			}
			else
			{
				isCorrupt = true;
				return false;
			}
			break;
		case COMMUNITY:
			if (cursor->value->_type == STRING)
			{
				communityString = ((OctetType *)cursor->value)->_value;
				if (!cursor->next)
				{
					isCorrupt = true;
					return false;
				}
				cursor = cursor->next;
				EXPECTING = PDU; // temp
			}
			else
			{
				isCorrupt = true;
				return false;
			}
			break;
		case PDU:
			switch (cursor->value->_type)
			{
			case GetRequestPDU:
			case GetNextRequestPDU:
			case GetResponsePDU:
			case SetRequestPDU:
				requestType = cursor->value->_type;
				break;
			default:
				isCorrupt = true;
				return false;
				break;
			}
			cursor = ((ComplexType *)cursor->value)->_values;
			EXPECTING = REQUESTID;
			break;
		case REQUESTID:
			if (cursor->value->_type == INTEGER)
			{
				requestID = ((IntegerType *)cursor->value)->_value;
				if (!cursor->next)
				{
					isCorrupt = true;
					return false;
				}
				cursor = cursor->next;
				EXPECTING = ERRORSTATUS;
			}
			else
			{
				isCorrupt = true;
				return false;
			}
			break;
		case ERRORSTATUS:
			if (cursor->value->_type == INTEGER)
			{
				errorStatus = ((IntegerType *)cursor->value)->_value;
				if (!cursor->next)
				{
					isCorrupt = true;
					return false;
				}
				cursor = cursor->next;
				EXPECTING = ERRORID;
			}
			else
			{
				isCorrupt = true;
				return false;
			}
			break;
		case ERRORID:
			if (cursor->value->_type == INTEGER)
			{
				errorIndex = ((IntegerType *)cursor->value)->_value;
				if (!cursor->next)
				{
					isCorrupt = true;
					return false;
				}
				cursor = cursor->next;
				EXPECTING = VARBINDS;
			}
			else
			{
				isCorrupt = true;
				return false;
			}
			break;
		case VARBINDS: // we have a varbind structure, lets dive into it.
			if (cursor->value->_type == STRUCTURE)
			{
				varBinds = new VarBindList();
				varBindsCursor = varBinds;
				tempCursor = ((ComplexType *)cursor->value)->_values;
				EXPECTING = VARBIND;
			}
			else
			{
				isCorrupt = true;
				return false;
			}
			break;
		case VARBIND:
			// we need to keep the cursor outside the varbindlist itself so we always have access to the list
			if (tempCursor->value->_type == STRUCTURE && ((ComplexType *)tempCursor->value)->_values->value->_type == OID)
			{
				VarBind *varbind = new VarBind();
				varbind->oid = ((OIDType *)((ComplexType *)tempCursor->value)->_values->value);
				varbind->type = ((ComplexType *)tempCursor->value)->_values->next->value->_type;
				varbind->value = ((ComplexType *)tempCursor->value)->_values->next->value;
				varBindsCursor->value = varbind;
				varBindsCursor->next = new VarBindList();
				if (!tempCursor->next)
				{
					EXPECTING = DONE;
				}
				else
				{
					//                        tempCursor = ((ComplexType*)cursor->next->value)->_values;
					tempCursor = tempCursor->next;
					varBindsCursor = varBindsCursor->next;
				}
			}
			else
			{
				isCorrupt = true;
				return false;
			}
			break;
		default:
			break;
		}
	}
	return true;
}

#endif