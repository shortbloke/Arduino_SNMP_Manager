#pragma once

#include <Udp.h>
#include <stddef.h>

namespace snmp_detail
{
// Consume only the current datagram, using the caller's existing packet buffer.
// UDP::flush() is not portable receive cleanup: ESP8266 uses it to transmit.
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
