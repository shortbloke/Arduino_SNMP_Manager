#pragma once
#include <Arduino_SNMP_Manager.h>

// Kept beside each sketch so Arduino IDE examples remain self-contained.
template <size_t Capacity> class PollState
{
public:
    bool add(ValueCallback *callback, SNMPGet &request)
    {
        if (!callback || count == Capacity || !request.tryAddOIDPointer(callback))
            return false;
        callbacks[count++] = callback;
        return true;
    }
    bool begin(uint32_t now)
    {
        if (!count || waiting)
            return false;
        for (size_t i = 0; i < count; ++i)
        {
            callbacks[i]->clearPendingRequests();
            before[i] = callbacks[i]->updateCount();
        }
        started = now;
        waiting = true;
        return true;
    }
    bool active() const
    {
        return waiting;
    }
    bool complete() const
    {
        if (!waiting)
            return false;
        for (size_t i = 0; i < count; ++i)
            if (callbacks[i]->updateCount() == before[i])
                return false;
        return true;
    }
    bool expired(uint32_t now, uint32_t timeout) const
    {
        return waiting && uint32_t(now - started) >= timeout;
    }
    void finish()
    {
        for (size_t i = 0; i < count; ++i)
            callbacks[i]->clearPendingRequests();
        waiting = false;
    }

private:
    ValueCallback *callbacks[Capacity] = {};
    uint32_t before[Capacity] = {};
    size_t count = 0;
    uint32_t started = 0;
    bool waiting = false;
};

inline short nextRequestID(short previous)
{
    return previous == 32767 ? 1 : previous + 1;
}

class Counter32Rate
{
public:
    void reset()
    {
        havePrevious = false;
    }
    bool sample(uint32_t octets, uint32_t ticks, uint32_t speed, double &percent)
    {
        if (havePrevious && ticks == previousTicks)
            return false;
        const bool usable = havePrevious && ticks > previousTicks && speed != 0;
        if (usable)
        {
            // Unsigned subtraction accounts for one Counter32 wrap, including its +1.
            // Poll often enough to avoid multiple wraps; use Counter64 for faster links.
            const uint32_t delta = octets - previousOctets;
            const double seconds = double(ticks - previousTicks) / 100.0;
            percent = (double(delta) * 8.0 * 100.0) / (double(speed) * seconds);
        }
        previousOctets = octets;
        previousTicks = ticks;
        havePrevious = true;
        // Reboots and TimeTicks wrap start a new baseline instead of producing a rate.
        return usable;
    }

private:
    uint32_t previousOctets = 0, previousTicks = 0;
    bool havePrevious = false;
};
