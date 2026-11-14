"""szdd.py - compress a file in Microsoft's COMPRESS format (SZDD).

usage: python3 -I REL/szdd.py IN OUT

Writes OUT as COMPRESS does, so that DOS 5's EXPAND, Windows 3.1's
LZEXPAND and the expander of SRC/TOOLS/LZEXP.C read it: the header (MAGIC,
METHOD, the last character of IN's extension or 0 when it has none, and
IN's length as four bytes, least significant first), then LZSS tokens.
Each group of up to eight tokens follows a flag byte whose bits, least
significant first, mark a literal byte (1) or a match (0) of two bytes:
the low eight bits of the window position, then its high four bits over
the length less MINLEN.  The window holds WINSZ bytes, all blanks at the
start, and the first byte goes to position WINSZ - START.

The parse is optimal for the format: each token costs a fixed number of
bits (a literal 9, a match 17) and a match may be cut to any length from
MINLEN, so the cheapest parse follows from the longest match at each
position, found by hash chains over the window.  The output depends on
IN's bytes and name alone, so every build writes the same file.

Exit status: 0 done; 1 IN cannot be read or OUT written; 2 a usage
error.
"""

import os
import struct
import sys

MAGIC = b'SZDD\x88\xf0\x27\x33'
METHOD = b'A'
WINSZ = 4096
START = 16
MINLEN = 3
MAXLEN = MINLEN + 15
LITBITS = 9
MATCHBITS = 17


def usage(reason):
    """Write reason and the usage lines on standard error; return 2."""
    sys.stderr.write('szdd: %s\n%s\n' % (reason, __doc__.split('\n\n')[1]))
    return 2


def longest(v, first):
    """Return, for each position of v from first on, the length and the
    source of the longest earlier match within the window (0, 0 for none
    of MINLEN)."""
    n = len(v)
    head = {}
    prev = [0] * n
    out = []
    for i in range(n):
        best = bestj = 0
        if i + MINLEN <= n:
            key = v[i:i + MINLEN]
            j = head.get(key, -1)
            prev[i] = j
            head[key] = i
            if i >= first:
                top = min(MAXLEN, n - i)
                while j >= 0 and i - j < WINSZ:
                    if v[j + best] == v[i + best]:
                        k = MINLEN
                        while k < top and v[j + k] == v[i + k]:
                            k += 1
                        if k > best:
                            best, bestj = k, j
                            if k == top:
                                break
                    j = prev[j]
        if i >= first:
            out.append((best, bestj))
    return out


def compress(data, name):
    """Return the compressed file of data, named name."""
    v = b' ' * WINSZ + data
    found = longest(v, WINSZ)
    n = len(data)
    cost = [0] * (n + 1)
    pick = [0] * n
    for i in range(n - 1, -1, -1):
        cost[i] = cost[i + 1] + LITBITS
        for k in range(MINLEN, found[i][0] + 1):
            if cost[i + k] + MATCHBITS < cost[i]:
                cost[i] = cost[i + k] + MATCHBITS
                pick[i] = k
    ext = os.path.splitext(name)[1][1:]
    out = bytearray(MAGIC + METHOD + (ext[-1:].encode('ascii') or b'\0') +
                    struct.pack('<I', n))
    i = 0
    while i < n:
        flag = len(out)
        out.append(0)
        for bit in range(8):
            if i == n:
                break
            k = pick[i]
            if k == 0:
                out[flag] |= 1 << bit
                out.append(data[i])
                i += 1
            else:
                pos = (found[i][1] - START) % WINSZ
                out += bytes((pos & 0xff, (pos >> 4 & 0xf0) | (k - MINLEN)))
                i += k
    return bytes(out)


def main(argv):
    if len(argv) != 2:
        return usage('not IN OUT')
    try:
        with open(argv[0], 'rb') as f:
            data = f.read()
        out = compress(data, os.path.basename(argv[0]))
        with open(argv[1], 'wb') as f:
            f.write(out)
    except (OSError, UnicodeEncodeError) as e:
        sys.stderr.write('szdd: %s\n' % e)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
