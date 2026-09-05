#ifndef SNMPGetResponse_h
#define SNMPGetResponse_h

#include "BER.h"
#include "VarBinds.h"
#include "SNMPProtocol.h"

namespace snmp_detail
{
// ASN.1 VarBind value alternatives, with SNMPv1 restrictions.
/**
 * @param type Candidate ASN.1 wire tag for a variable-binding value.
 * @param version2 True for v2c; false applies v1 restrictions.
 * @return True only for supported primitive/exception alternatives in that version.
 */
bool isValidBindingType(ASN_TYPE type, bool version2);
}

class SNMPGetResponse
{

public:
    /**
     * @brief Create an empty parser; metadata is meaningful only after parseFrom() succeeds.
     */
    SNMPGetResponse() {};
    SNMPGetResponse(const SNMPGetResponse &) = delete;
    SNMPGetResponse &operator=(const SNMPGetResponse &) = delete;
    /**
     * @brief Release the owned parse tree and binding wrappers, invalidating borrowed fields.
     */
    ~SNMPGetResponse()
    {
        delete varBinds;
        delete SNMPPacket;
    };
    char *communityString = nullptr; ///< Borrowed tree-owned bytes; use communityLength.
    size_t communityLength = 0;
    int version = 0; ///< Parsed v1/v2c as 1/2; wire values are 0/1.
    ASN_TYPE requestType = STRUCTURE;
    int32_t requestID = 0;
    int errorStatus = 0;
    int errorIndex = 0; ///< One-based failing binding, or zero; check only after successful parse.
    VarBindList *varBinds = 0;
    VarBindList *varBindsCursor = 0;

    ComplexType *SNMPPacket =
        0; ///< Owned tree; do not delete or retain children past the next parse.
    /**
     * @brief Replace the previous parsed message with a bounded decode.
     * @param buf Input bytes containing a complete SNMP message; borrowed for this call only.
     * @param available Number of accessible bytes. Always pass the real size; the legacy default
     *  cannot check the allocation bounds.
     * @return True for a structurally valid supported message, not proof of request correlation
     *  or successful device status. False on malformed input, capacity, or allocation failure.
     * @note Borrowed fields from the previous parse become invalid even when this parse fails.
     *  v1 trap PDUs are decoded separately by SNMPClient notification handling.
     */
    bool parseFrom(unsigned char *buf, size_t available = static_cast<size_t>(-1));
    enum SNMPExpect EXPECTING = SNMPVERSION;
    bool isCorrupt = false;
};

#endif
