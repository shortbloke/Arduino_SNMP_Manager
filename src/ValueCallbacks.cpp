#include "ValueCallbacks.h"

bool ValueCallback::canTrack(int32_t id, UDP *udp, IPAddress peer) const
{
    for (const auto &entry : pending)
        if (!entry.active || (entry.id == id && entry.udp == udp && entry.peer == peer))
            return true;
    return false;
}

// Retrying the same request reuses its slot. Distinct outstanding requests need
// distinct slots so a reply consumes its own request and a duplicate cannot
// consume another outstanding request's slot.
void ValueCallback::track(int32_t id, UDP *udp, IPAddress peer)
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

bool ValueCallback::consume(int32_t id, UDP *udp, IPAddress peer)
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

// Cancellation clears pending work but deliberately leaves tracking enabled.
// Otherwise a late reply could be accepted as an unsolicited legacy update.
void ValueCallback::clearPendingRequests()
{
    for (auto &entry : pending)
        entry.active = false;
    requestPending = false;
}

// Unlink each successor before deleting it: recursive list destruction would
// consume stack space proportional to the number of registered callbacks.
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

bool ValueCallback::matches(int32_t id, UDP *udp, IPAddress peer) const
{
    for (const auto &entry : pending)
        if (entry.active && entry.id == id && entry.udp == udp && entry.peer == peer)
            return true;
    return false;
}
