#pragma once

#include <Udp.h>
#include <stddef.h>

namespace snmp_detail
{
// Consume only the current datagram, using the caller's existing packet buffer.
// UDP::flush() is not portable receive cleanup: ESP8266 uses it to transmit.
/**
 * @brief Drain only the current datagram without calling transport-dependent flush().
 * @param udp Borrowed transport positioned at the current datagram.
 * @param remaining Unread bytes in that datagram; nonpositive means nothing to do.
 * @param buffer Caller-owned scratch memory; never retained.
 * @param capacity Accessible scratch bytes; provide a non-null buffer and positive size.
 * @note Stops on a stalled/inconsistent read; returns no value and sends no packets.
 */
inline void discardDatagram(UDP &udp, int remaining, unsigned char *buffer, size_t capacity)
{
    while (remaining > 0)
    {
        const size_t chunk =
            static_cast<size_t>(remaining) < capacity ? static_cast<size_t>(remaining) : capacity;
        const int consumed = udp.read(buffer, chunk);
        // Stop on a stalled or inconsistent transport instead of spinning forever
        // in loop(), which would prevent other requests and board work progressing.
        if (consumed <= 0 || static_cast<size_t>(consumed) > chunk)
            break;
        remaining -= consumed;
    }
}
}
