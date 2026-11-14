"""ARPWAIT.PY - wait until a packet capture holds an ARP request for an
IPv4 address.

usage: python3 -I TEST/INTEG/ARPWAIT.PY PCAP ADDR SECS

PCAP is the capture QEMU's filter-dump object writes as the frames pass,
a libpcap file of Ethernet frames; it may not exist yet.  The script
reads it again every POLL seconds and ends with status 0 once a frame in
it is an ARP request (EtherType 0806h, operation 1) asking for ADDR,
dotted, which a station sends before its first frame to an address of
its own segment; with 1 when SECS seconds pass first, and with 2 on a
usage error.
"""
import os
import socket
import sys
import time

POLL = 0.1                      # seconds between reads of the capture
HEADER = 24                     # bytes of the file header
RECORD = 16                     # bytes of each frame's header


def asks(data, target):
    """True when the capture data holds an ARP request for target."""
    order = 'little' if data[:4] == b'\xd4\xc3\xb2\xa1' else 'big'
    pos = HEADER
    while pos + RECORD <= len(data):
        n = int.from_bytes(data[pos + 8:pos + 12], order)
        frame = data[pos + RECORD:pos + RECORD + n]
        pos += RECORD + n
        if (len(frame) >= 42 and frame[12:14] == b'\x08\x06' and
                frame[20:22] == b'\x00\x01' and frame[38:42] == target):
            return True
    return False


def main(argv):
    if len(argv) != 3:
        sys.stderr.write('arpwait: wrong number of arguments\n' +
                         __doc__.split('\n\n')[1] + '\n')
        return 2
    path, addr, secs = argv
    try:
        target = socket.inet_aton(addr)
        end = time.monotonic() + float(secs)
    except (OSError, ValueError) as e:
        sys.stderr.write('arpwait: %s\n' % e)
        return 2
    while time.monotonic() < end:
        if os.path.exists(path):
            with open(path, 'rb') as f:
                if asks(f.read(), target):
                    return 0
        time.sleep(POLL)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
