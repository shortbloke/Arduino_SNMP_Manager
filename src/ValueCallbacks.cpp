#include "ValueCallbacks.h"

bool ValueCallback::canTrack(unsigned long id, UDP *udp, IPAddress peer) const
{
    for (const auto &entry : pending)
        if (!entry.active || (entry.id == id && entry.udp == udp && entry.peer == peer))
            return true;
    return false;
}

void ValueCallback::track(unsigned long id, UDP *udp, IPAddress peer)
{
    PendingRequest *slot = nullptr;
    for (auto &entry : pending)
    {
        if (entry.active && entry.id == id && entry.udp == udp && entry.peer == peer)
        {
            slot = &entry;
            break;
        }
        if (!entry.active && !slot)
            slot = &entry;
    }
    if (!slot)
        return;
    slot->active = true;
    slot->id = id;
    slot->udp = udp;
    slot->peer = peer;
    trackingEnabled = true;
    requestTracked = requestPending = true;
    expectedRequestID = id;
    requestUDP = udp;
    requestPeer = peer;
}

bool ValueCallback::consume(unsigned long id, UDP *udp, IPAddress peer)
{
    bool found = false;
    requestPending = false;
    for (auto &entry : pending)
    {
        if (entry.active && entry.id == id && entry.udp == udp && entry.peer == peer)
        {
            entry.active = false;
            found = true;
        }
        requestPending = requestPending || entry.active;
    }
    return found;
}

void ValueCallback::clearPendingRequests()
{
    for (auto &entry : pending)
        entry.active = false;
    requestPending = false;
}

ValueCallbackList::~ValueCallbackList()
{
    while (next)
    {
        auto *node = next;
        next = node->next;
        node->next = nullptr;
        delete node;
    }
}
