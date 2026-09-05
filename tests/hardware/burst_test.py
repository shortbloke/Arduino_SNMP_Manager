#!/usr/bin/env python3
"""Send independently encoded v2c notifications; requires pyserial and burst firmware."""
import argparse
import json
import re
import select
import socket
import time


def tlv(tag, value):
    n = len(value)
    length = bytes([n]) if n < 128 else bytes([0x82, n >> 8, n & 255])
    return bytes([tag]) + length + value


def integer(n):
    return tlv(2, n.to_bytes(max(1, (n.bit_length() + 8) // 8), 'big'))


def oid(text):
    arcs = list(map(int, text.strip('.').split('.')))
    result = bytearray()
    for n in [40 * arcs[0] + arcs[1]] + arcs[2:]:
        part = [n & 127]
        n >>= 7
        while n:
            part.insert(0, (n & 127) | 128)
            n >>= 7
        result.extend(part)
    return tlv(6, bytes(result))


def packet(identifier, payload, inform=False, response=False):
    def binding(name, value):
        return tlv(0x30, oid(name) + value)
    values = (binding('1.3.6.1.2.1.1.3.0', tlv(0x43, b'\x01')) +
              binding('1.3.6.1.6.3.1.1.4.1.0', oid('1.3.6.1.6.3.1.1.5.1')) +
              binding('1.3.6.1.4.1.8072.9999.1.0', tlv(4, b'x' * payload)))
    pdu = tlv(0xa2 if response else (0xa6 if inform else 0xa7),
              integer(identifier) + integer(0) + integer(0) + tlv(0x30, values))
    return tlv(0x30, integer(1) + tlv(4, b'burst-test') + pdu)


def main():
    import serial

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--small', action='store_true',
                        help='Only short back-to-back bursts with no loop delay')
    args = parser.parse_args()
    with serial.Serial(args.serial, 115200, timeout=0.2) as board, \
            socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock, \
            open(args.output, 'w') as log:
        def record(value):
            line = json.dumps(value, sort_keys=True)
            print(line, flush=True)
            log.write(line + '\n')
            log.flush()

        def command(cmd, prefix):
            board.write((cmd + '\n').encode())
            deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                line = board.readline().decode(errors='replace').strip()
                if line.startswith(prefix):
                    return dict(re.findall(r'(\w+)=([^ ]+)', line))
                if line:
                    record({'serial': line})
            raise RuntimeError('Missing serial response: ' + prefix)

        time.sleep(2)
        info = command('I', 'BURST_READY')
        if info['ok'] != '1':
            raise RuntimeError('Board not ready')
        record({'board': info})
        target = (info['ip'], 1162)
        sock.bind(('', 0))
        sock.setblocking(False)

        def run(name, count, gap, pause, payload, inform):
            # Prior run has drained before resetting its sequence-number accounting.
            command('R ' + str(pause), 'RESET')
            while select.select([sock], [], [], 0)[0]:
                sock.recvfrom(65535)
            expected = {packet(i, payload, True, True): i for i in range(1, count + 1)}
            acknowledgements = set()
            bad_ack = 0
            empty_datagrams = 0
            def drain():
                nonlocal bad_ack, empty_datagrams
                while select.select([sock], [], [], 0)[0]:
                    data, peer = sock.recvfrom(65535)
                    if not data and peer == target:
                        empty_datagrams += 1
                        continue
                    # Decode/re-encoding can canonicalise BER lengths; compare BER trees.
                    key = canonical(data)
                    identifier = canonical_expected.get(key)
                    if peer != target or identifier is None:
                        bad_ack += 1
                        record({"unexpected_peer": peer, "unexpected_packet": data.hex()})
                    else:
                        acknowledgements.add(identifier)
            canonical_expected = {canonical(k): v for k, v in expected.items()}
            started = time.monotonic()
            for i in range(1, count + 1):
                sock.sendto(packet(i, payload, inform), target)
                drain()
                if gap:
                    time.sleep(gap)
            send_seconds = time.monotonic() - started
            deadline = time.monotonic() + max(3, count * pause / 1000 + 1)
            while time.monotonic() < deadline:
                drain()
                time.sleep(0.01)
            stats = {k: int(v) for k, v in command('S', 'STATS').items()}
            initial_unique = stats['unique']
            initial_acknowledged = len(acknowledgements)
            retry_packets = 0
            if name == 'recovery':
                for attempt in range(3):
                    missing = set(range(1, count + 1)) - acknowledgements
                    if not missing:
                        break
                    for identifier in sorted(missing):
                        sock.sendto(packet(identifier, payload, True), target)
                        retry_packets += 1
                        time.sleep(.1)
                        drain()
                    deadline = time.monotonic() + 1
                    while time.monotonic() < deadline:
                        drain()
                        time.sleep(.01)
                stats = {k: int(v) for k, v in command('S', 'STATS').items()}
            result = dict(name=name, sent=count, gap_ms=gap * 1000, loop_delay_ms=pause,
                          payload_bytes=payload, packet_bytes=len(packet(1, payload, inform)),
                          inform=inform, send_seconds=round(send_seconds, 3),
                          acknowledged=len(acknowledgements), bad_ack=bad_ack,
                          empty_datagrams=empty_datagrams, initial_unique=initial_unique,
                          initial_acknowledged=initial_acknowledged, retry_packets=retry_packets, **stats)
            record(result)
            if stats['invalid'] or bad_ack or empty_datagrams:
                raise RuntimeError('Invalid payload or unexpected UDP response')
            if name == 'recovery' and (stats['unique'] != count or len(acknowledgements) != count):
                raise RuntimeError('Recovery failed')

        pauses = (0,) if args.small else (0, 10, 50)
        payloads = (32,) if args.small else (32, 256)
        loads = ((5, 0), (10, 0), (20, 0)) if args.small else ((20, .01), (100, .001), (500, 0))
        for pause in pauses:
            for payload in payloads:
                for inform in (False, True):
                    for count, gap in loads:
                        run('burst', count, gap, pause, payload, inform)
                        run('recovery', 10, .1, 0, 32, True)
        record({'done': True})


def canonical(data):
    """Compare BER values independently of short/long definite length encoding."""
    items = []
    pos = 0
    while pos < len(data):
        tag, n = data[pos:pos + 2]
        pos += 2
        if n & 128:
            count = n & 127
            if not count or pos + count > len(data):
                raise ValueError('Invalid BER length')
            n = int.from_bytes(data[pos:pos + count], 'big')
            pos += count
        value = data[pos:pos + n]
        if len(value) != n:
            raise ValueError('Truncated BER')
        pos += n
        items.append((tag, canonical(value) if tag & 32 else value))
    return tuple(items)


if __name__ == '__main__':
    main()
