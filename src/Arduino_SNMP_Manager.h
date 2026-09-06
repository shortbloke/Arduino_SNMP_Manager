#ifndef SNMPManager_h
#define SNMPManager_h

#include "SNMPConfig.h"

#include <Udp.h>

#include "BER.h"
#include "VarBinds.h"

#include "ValueCallbacks.h"

#include "SNMPGet.h"
#include "SNMPGetResponse.h"

/**
 * @brief Low-level reply dispatcher writing to registered caller-owned destinations.
 * @note Public list/cursor fields expose internal bookkeeping; use registration methods
 *  rather than changing links. The friendly SNMPClient API owns results instead.
 */
class SNMPManager
{
public:
    /**
     * @brief Create a low-level manager using the default community; setUDP() before use.
     */
    SNMPManager() {};
    /**
     * @brief Create a low-level manager without opening a socket.
     * @param community Borrowed zero-terminated access string; keep it alive and unchanged.
     *  Null selects the default community. This differs from SNMPDevice, which copies it.
     */
    SNMPManager(const char *community) : _community(community ? community : "public") {};
    /**
     * @brief Release registration references; does not stop or delete the borrowed UDP transport.
     */
    ~SNMPManager();
    SNMPManager(const SNMPManager &) = delete;
    SNMPManager &operator=(const SNMPManager &) = delete;
    /**
     * @brief Transfer owned registrations and transport association.
     * @param other Source manager, left safe to destroy. Caller-owned destinations must remain
     * alive.
     */
    SNMPManager(SNMPManager &&other);
    const char *_community = "public";

    ValueCallbacks *callbacks = new (std::nothrow) ValueCallbacks();
    ValueCallbacks *callbacksCursor = callbacks;
    /**
     * @brief Find a registration by device and OID; does not correlate a request ID.
     * @param ip Device address to match.
     * @param oid Non-null numeric OID text to match.
     * @return Borrowed matching callback, or nullptr. Do not delete a manager-owned callback.
     */
    ValueCallback *
    findCallback(IPAddress ip, const char *oid); // Find based on responding host IP address and OID
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value float destination; receives the signed INTEGER value divided by ten; keep it
     * alive while registered.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addFloatHandler(IPAddress ip, const char *oid, float *value);
    // Capacity includes the C terminator.
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value pointer to a caller-owned text-buffer pointer; both must stay valid; keep it
     * alive while registered.
     * @param capacity Destination bytes including the final zero; oversized values are not
     * truncated.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addStringHandler(IPAddress ip, const char *oid, char **value, size_t capacity);
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value caller-owned binary OCTET STRING buffer; keep it alive while registered.
     * @param capacity Buffer size in bytes; no text terminator is added.
     * @param length Caller-owned output byte count; updated only with a successful value.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addOctetHandler(IPAddress ip, const char *oid, unsigned char *value,
                                   size_t capacity, size_t *length);
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value caller-owned Opaque byte buffer; keep it alive while registered.
     * @param capacity Buffer size in bytes; no text terminator is added.
     * @param length Caller-owned output byte count; updated only with a successful value.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addOpaqueHandler(IPAddress ip, const char *oid, unsigned char *value,
                                    size_t capacity, size_t *length);
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value caller-owned byte buffer; keep it alive while registered.
     * @param type STRING or OPAQUE only.
     * @param capacity Buffer size in bytes, without a text terminator requirement.
     * @param length Caller-owned output byte count, kept alive with the buffer.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addBinaryHandler(ASN_TYPE type, IPAddress ip, const char *oid,
                                    unsigned char *value, size_t capacity, size_t *length);
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value caller-owned int32_t destination for signed INTEGER; keep it alive while
     * registered.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addIntegerHandler(IPAddress ip, const char *oid, int32_t *value);
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value caller-owned uint32_t destination for TimeTicks (hundredths of a second); keep
     * it alive while registered.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addTimestampHandler(IPAddress ip, const char *oid, uint32_t *value);
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value caller-owned text buffer for a returned OBJECT IDENTIFIER; keep it alive while
     * registered.
     * @param capacity Destination bytes including the final zero; no truncation.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addOIDHandler(IPAddress ip, const char *oid, char *value, size_t capacity);
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value caller-owned uint64_t destination for Counter64 (v2c only); keep it alive while
     * registered.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addCounter64Handler(IPAddress ip, const char *oid, uint64_t *value);
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value caller-owned uint32_t destination for Counter32; keep it alive while registered.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addCounter32Handler(IPAddress ip, const char *oid, uint32_t *value);
    /**
     * @brief Register a destination to update after a matching valid response.
     * @param ip Device address to match.
     * @param oid Zero-terminated instance OID; the registration owns a copy.
     * @param value caller-owned uint32_t destination for Gauge32; keep it alive while registered.
     * @return Borrowed manager-owned callback, or nullptr for invalid arguments/allocation failure.
     * @note Register once, add the returned callback to SNMPGet, and check updateCount() for
     * freshness.
     */
    ValueCallback *addGaugeHandler(IPAddress ip, const char *oid, uint32_t *value);

    /**
     * @brief Stop the previous transport, select the supplied transport, and attempt begin().
     * @param udp Borrowed transport that must outlive the manager; null detaches it.
     * @note Returns no value; call begin() to check binding success. Even the same transport is
     * stopped and rebound. Pending callback slots are preserved; cancel obsolete requests
     * explicitly before changing transports.
     */
    void setUDP(UDP *udp);
    /**
     * @brief Bind the configured transport to local UDP port 162.
     * @return True if binding succeeds; false if no transport is set or binding fails.
     */
    bool begin();
    /**
     * @brief Attempt to receive and dispatch a reply using the configured transport.
     * @return False only when no UDP object is configured. True does not mean a reply arrived
     *  or passed validation; compare callback updateCount() to detect successful writes.
     */
    bool loop();
    /**
     * @brief Decode a hexadecimal test packet through the manager's response path.
     * @param testPacket Hexadecimal byte pairs separated by whitespace; leading/trailing whitespace
     * is allowed.
     * @return False for invalid text, oversized data, or parse/dispatch failure.
     * @note Intended for diagnostics; configure a valid UDP object first because dispatch
     * uses its peer context.
     */
    bool testParsePacket(String testPacket);
    char OIDBuf[MAX_OID_LENGTH];
    UDP *_udp = nullptr;
    /**
     * @brief Adopt a fully configured custom callback on success only.
     * @param callback Heap-created registration with valid OID/destination configuration.
     * @return True if registered (including an already registered pointer), false on null or
     *  allocation failure. On failure the caller retains ownership; on success the manager owns it.
     */
    bool addHandler(ValueCallback *callback);

private:
    unsigned char _packetBuffer[SNMP_PACKET_LENGTH];
    bool receivePacket(int length);
    bool parsePacket(size_t length);
    void printPacket(int len);
};

#endif
