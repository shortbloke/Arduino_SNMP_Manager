#ifndef SNMPGet_h
#define SNMPGet_h

#include "BER.h"
#include "ValueCallbacks.h"
#include "SNMPProtocol.h"

/**
 * @brief Low-level GET builder sharing registration ownership with a manager.
 * @note Public list/packet fields are exposed for compatibility, not for independent deletion.
 *  Use methods to change the list; keep community, destinations, and transport alive.
 */
class SNMPGet
{
public:
    /**
     * @brief Construct a reusable low-level GET builder without sending.
     * @param community Borrowed zero-terminated access string; keep it alive and unchanged.
     * @param version Version1 or Version2c. Configure transport and callbacks before sending.
     */
    SNMPGet(const char *community, SNMPVersion version);
    /**
     * @brief Release callback references and any owned packet; does not delete the UDP object.
     */
    ~SNMPGet()
    {
        releaseCallbacks();
        delete packet;
    }
    SNMPGet(const SNMPGet &) = delete;
    SNMPGet &operator=(const SNMPGet &) = delete;
    /**
     * @brief Transfer builder ownership and callback references.
     * @param other Source builder, left safe to destroy; destinations remain caller-owned.
     */
    SNMPGet(SNMPGet &&other);
    /**
     * @brief Release this builder's registration references and its list.
     * @note Does not cancel shared pending requests. Returns no value; call clearOIDList() for
     * reuse.
     */
    void releaseCallbacks();
    const char *_community;
    SNMPVersion _version;
    uint16_t port = 161;
    int32_t requestID = 0;

    // Configure the request ID, port, and transport.

    /**
     * @brief Set the correlation ID for later sends.
     * @param request Signed 32-bit ID; choose distinct IDs while earlier replies can still arrive.
     * @note Returns no value; does not cancel existing tracked sends.
     */
    void setRequestID(int32_t request)
    {
        requestID = request;
    }

    /**
     * @brief Set the remote service port for later sends.
     * @param portnumber Destination UDP port, normally 161; supply a usable nonzero port.
     * @note Returns no value and sends nothing.
     */
    void setPort(uint16_t portnumber)
    {
        port = portnumber;
    }

    /**
     * @brief Select the transport used by sendTo().
     * @param udp Borrowed UDP object; keep it alive while used. Null makes sends fail.
     * @note Does not bind the socket; returns no value.
     */
    void setUDP(UDP *udp)
    {
        _udp = udp;
    }

    /**
     * @brief Append an existing registration and retain a shared reference.
     * @param callback Non-null registration; its destination must remain valid.
     * @return True if appended, false on null/allocation failure. Failure does not retain it.
     */
    bool addOIDPointer(ValueCallback *callback);
    ValueCallbacks *callbacks = new (std::nothrow) ValueCallbacks();
    ValueCallbacks *callbacksCursor = callbacks;

    UDP *_udp = 0;
    /**
     * @brief Build and send a GET, then track it only after a complete transport send.
     * @param ip Destination IPv4 address, copied into tracking state.
     * @return True when transmission is accepted, not when a reply arrives. False on missing
     *  transport, full tracking slots, invalid/oversized encoding, allocation, or UDP failure.
     */
    bool sendTo(IPAddress ip);

    ComplexType *packet = 0;
    /**
     * @brief Replace packet with a constructed GET from the current callback list.
     * @return True when an owned packet is ready, false for invalid input/allocation failure.
     * @note Does not transmit; packet belongs to this builder and can change on the next build.
     */
    bool build();

    /**
     * @brief Release the builder's callback list and prepare an empty list for reuse.
     * @note Does not abandon callbacks' outstanding requests. Returns no value; later additions
     *  report allocation failure if the new list could not be created.
     */
    void clearOIDList();
    // Abandon outstanding requests for the callbacks currently in this request.
    /**
     * @brief Abandon all pending sends for callbacks currently attached to this builder.
     * @note Also affects their shared use by other builders; late replies remain rejected.
     *  Returns no value. Do this before removing callbacks if those requests must be cancelled.
     */
    void cancelPendingRequests();
};

#endif
