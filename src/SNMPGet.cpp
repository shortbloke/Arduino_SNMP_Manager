#include "SNMPGet.h"

#include <memory>
#include <utility>

SNMPGet::SNMPGet(const char *community, SNMPVersion version)
    : _community(community), _version(version)
{
}

SNMPGet::SNMPGet(SNMPGet &&other) : SNMPGet(other._community, other._version)
{
    std::swap(callbacks, other.callbacks);
    std::swap(packet, other.packet);
    callbacksCursor = callbacks;
    other.callbacksCursor = other.callbacks;
    _udp = other._udp;
    port = other.port;
    requestID = other.requestID;
}

void SNMPGet::releaseCallbacks()
{
    for (ValueCallbacks *entry = callbacks; entry; entry = entry->next)
        if (entry->value)
            entry->value->release();
    delete callbacks;
    callbacks = callbacksCursor = nullptr;
}

bool SNMPGet::sendTo(IPAddress ip)
{
    if (!_udp)
    {
        return false;
    }
    // Refuse a send before touching the transport if any callback has no free slot.
    for (ValueCallbacks *entry = callbacks; entry && entry->value; entry = entry->next)
        if (!entry->value->canTrack(requestID, _udp, ip))
            return false;
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
    if (length < 0)
        return false;
#ifdef DEBUG
    Serial.print(F("[DEBUG] SNMPGet: Sending UDP packet to: "));
    Serial.print(ip);
    Serial.print(F(":"));
    Serial.println(port);
    Serial.print("[DEBUG] composed packet: ");
    for (int i = 0; i < length; i++)
    {
        if (_packetBuffer[i] < 16)
            Serial.print('0');
        Serial.print(_packetBuffer[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
#endif
    if (!_udp->beginPacket(ip, port))
        return false;
    if (_udp->write(_packetBuffer, length) != static_cast<size_t>(length))
        return false;
    if (!_udp->endPacket())
        return false;
    // Commit tracking only after the transport reports a complete send. Failed
    // writes must not consume callback slots and block subsequent retry attempts.
    for (ValueCallbacks *entry = callbacks; entry && entry->value; entry = entry->next)
    {
        ValueCallback *callback = entry->value;
        callback->track(requestID, _udp, ip);
    }
    return true;
}

void SNMPGet::clearOIDList()
{ // Release this request's references; registrations may still belong to a manager.
    releaseCallbacks();
    callbacks = new (std::nothrow) ValueCallbacks();
    callbacksCursor = callbacks;
}

// Cancellation applies to registrations currently in this builder. Clearing the
// builder list alone cannot cancel them: a manager or another request can still
// hold references to the same callbacks.
void SNMPGet::cancelPendingRequests()
{
    for (ValueCallbacks *entry = callbacks; entry && entry->value; entry = entry->next)
        entry->value->clearPendingRequests();
}

bool SNMPGet::build()
{
    delete packet;
    packet = nullptr;
    if (!_community)
        return false;
    std::unique_ptr<ComplexType> root(new (std::nothrow) ComplexType(STRUCTURE));
    std::unique_ptr<ComplexType> pdu(new (std::nothrow) ComplexType(GetRequestPDU));
    std::unique_ptr<ComplexType> bindings(new (std::nothrow) ComplexType(STRUCTURE));
    if (!root || !pdu || !bindings)
        return false;
    if (!root->addValueToList(new (std::nothrow)
                                  IntegerType(static_cast<unsigned long>(_version))) ||
        !root->addValueToList(new (std::nothrow) OctetType(const_cast<char *>(_community))) ||
        !pdu->addValueToList(new (std::nothrow) IntegerType(requestID)) ||
        !pdu->addValueToList(new (std::nothrow) IntegerType(0)) ||
        !pdu->addValueToList(new (std::nothrow) IntegerType(0)))
        return false;
    for (ValueCallbacks *entry = callbacks; entry && entry->value; entry = entry->next)
    {
        std::unique_ptr<ComplexType> binding(new (std::nothrow) ComplexType(STRUCTURE));
        if (!binding || !binding->addValueToList(new (std::nothrow) OIDType(entry->value->OID)) ||
            !binding->addValueToList(new (std::nothrow) NullType()) ||
            !bindings->addValueToList(binding.release()))
            return false;
    }
    if (!pdu->addValueToList(bindings.release()) || !root->addValueToList(pdu.release()))
        return false;
    packet = root.release();
    return true;
}

bool SNMPGet::addOIDPointer(ValueCallback *callback)
{
    if (!callback)
        return false;
    ValueCallbacks **tail = &callbacks;
    while (*tail && (*tail)->value)
        tail = &(*tail)->next;
    if (!*tail)
        *tail = new (std::nothrow) ValueCallbacks();
    if (!*tail)
        return false;
    callback->retain();
    (*tail)->value = callback;
    callbacksCursor = *tail;
    return true;
}
