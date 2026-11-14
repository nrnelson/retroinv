"""KWAJ.PY - expand a file of Microsoft's KWAJ compression, method 3.

usage: python3 -I TEST/WIN/KWAJ.PY FILE OUT

FILE, compressed by COMPRESS.EXE's LZ+Huffman method as the disks of
Windows for Workgroups 3.11 hold their files, is expanded into OUT,
checked against the length its header records, and the name the header
records is printed, upper case.

The format: the magic "KWAJ" 88h F0h 27h D1h, the method, the offset of
the data and the flags, all 16-bit little-endian; then, by the flags,
the expanded length (bit 0, 32 bits), a word (bit 1), a counted block
(bit 2), the name and the extension (bits 3 and 4, each ending in a
zero byte) and a counted text (bit 5).  The data is read most
significant bit first: four bits for each of six table types, five
canonical Huffman tables, then the stream.  A stream item is a match:
its length less 2, coded in table 1, or in table 2 right after a run of
fewer than 32 literals, then its distance back into a 4 KB window that
starts filled with blanks, the high six bits coded in table 4 and the
low six as they are.  A length code of 0 starts instead a run of
literals: its length less 1, coded in table 3, then each literal, coded
in table 5.

Exit status: 0 when FILE was expanded; 1 when it is not of this format
or does not expand to its recorded length; 2 on a usage error.
"""

import struct
import sys

MAGIC = b'KWAJ\x88\xf0\x27\xd1'
METHOD_LZH = 3
WINDOW = 4096
# The symbols of the five tables, and the code length of each when its
# type is 0.
SYMS = (16, 16, 32, 64, 256)
FLAT = {16: 4, 32: 5, 64: 6, 256: 8}
# The longest code a table may hold, in bits.
MAXLEN = 16


class Bad(Exception):
    """A file this script cannot expand: exit status 1."""


class Bits:
    """The data, read most significant bit first; past its end, zeros."""

    def __init__(self, data, pos):
        self.data = data
        self.pos = pos
        self.buf = 0
        self.n = 0

    def peek(self, n):
        while self.n < n:
            b = self.data[self.pos] if self.pos < len(self.data) else 0
            self.pos += 1
            self.buf = (self.buf << 8) | b
            self.n += 8
        return (self.buf >> (self.n - n)) & ((1 << n) - 1)

    def skip(self, n):
        self.n -= n
        self.buf &= (1 << self.n) - 1

    def read(self, n):
        v = self.peek(n)
        self.skip(n)
        return v

    def overrun(self):
        """Whether more bits were taken than the data holds."""
        return self.pos - self.n // 8 > len(self.data)


def read_lens(bits, kind, n):
    """The code lengths of a table of N symbols stored as KIND: 0, every
    length FLAT[N]; 1, a first length in four bits, then for each next
    0 to keep it, 10 to add one, 11 and four bits to set it; 2, a first
    length, then two bits adding -1, 0 or 1, or 3 and four bits setting
    it; 3, four bits each."""
    if kind == 0:
        return [FLAT[n]] * n
    if kind == 3:
        return [bits.read(4) for _ in range(n)]
    if kind not in (1, 2):
        raise Bad('table type %d' % kind)
    c = bits.read(4)
    lens = [c]
    for _ in range(1, n):
        if kind == 1:
            if bits.read(1):
                c = bits.read(4) if bits.read(1) else c + 1
        else:
            sel = bits.read(2)
            c = bits.read(4) if sel == 3 else c + sel - 1
        if not 0 <= c <= MAXLEN:
            raise Bad('a code length of %d' % c)
        lens.append(c)
    return lens


def table(lens):
    """(lookup, width): the canonical code of LENS, shorter codes first
    and symbols in order within a length, as a list indexed by the next
    WIDTH bits giving (symbol, length)."""
    width = max(lens) if lens else 0
    if width == 0:
        return [], 0
    look = [None] * (1 << width)
    code = 0
    for ln in range(1, width + 1):
        for sym, l in enumerate(lens):
            if l != ln:
                continue
            if code >= 1 << ln:
                raise Bad('a code table is oversubscribed')
            base = code << (width - ln)
            for i in range(1 << (width - ln)):
                look[base + i] = (sym, ln)
            code += 1
        code <<= 1
    return look, width


def decode(bits, tab):
    """The next symbol of BITS in the code TAB, as table() made it."""
    look, width = tab
    if width == 0:
        raise Bad('a code from an empty table')
    hit = look[bits.peek(width)]
    if hit is None:
        raise Bad('a code no table entry has')
    bits.skip(hit[1])
    return hit[0]


def expand(data, pos, size):
    """The SIZE bytes the LZ+Huffman stream at DATA[POS:] expands to."""
    bits = Bits(data, pos)
    kinds = [bits.read(4) for _ in range(6)]
    tabs = []
    for kind, n in zip(kinds, SYMS):
        tabs.append(table(read_lens(bits, kind, n)))
    len1, len2, litlen, offset, literal = tabs
    win = bytearray(b' ' * WINDOW)
    out = bytearray()
    wpos = 0
    run = False
    while len(out) < size:
        n = decode(bits, len2 if run else len1)
        if n:
            n += 2
            run = False
            dist = (decode(bits, offset) << 6) | bits.read(6)
            for _ in range(n):
                b = win[(wpos - dist) % WINDOW]
                win[wpos] = b
                out.append(b)
                wpos = (wpos + 1) % WINDOW
        else:
            n = decode(bits, litlen) + 1
            run = n != 32
            for _ in range(n):
                b = decode(bits, literal)
                win[wpos] = b
                out.append(b)
                wpos = (wpos + 1) % WINDOW
        if bits.overrun():
            raise Bad('the data ends before %d bytes' % size)
    return bytes(out[:size])


def cstr(data, pos, most):
    """(text, next): the zero-ended text of at most MOST bytes at POS."""
    end = data.index(b'\0', pos, pos + most + 1)
    return data[pos:end].decode('ascii'), end + 1


def unpack(data):
    """(name, bytes): the name the header of DATA records and DATA
    expanded."""
    if data[:8] != MAGIC or len(data) < 14:
        raise Bad('not a KWAJ file')
    method, start, flags = struct.unpack_from('<HHH', data, 8)
    if method != METHOD_LZH:
        raise Bad('method %d' % method)
    if not flags & 1 or not flags & 8:
        raise Bad('no length or name in the header')
    (size,) = struct.unpack_from('<I', data, 14)
    pos = 18
    if flags & 2:
        pos += 2
    if flags & 4:
        pos += 2 + struct.unpack_from('<H', data, pos)[0]
    name, pos = cstr(data, pos, 8)
    if flags & 16:
        ext, pos = cstr(data, pos, 3)
        # COMPRESS.EXE keeps a blank for an empty extension.
        if ext.strip():
            name += '.' + ext
    return name.upper(), expand(data, start, size)


def main(argv):
    if len(argv) != 2:
        sys.stderr.write('KWAJ.PY: FILE and OUT are needed\n\n%s\n'
                         % __doc__.split('\n\n')[1])
        return 2
    with open(argv[0], 'rb') as f:
        data = f.read()
    try:
        name, out = unpack(data)
    except (Bad, ValueError, IndexError, struct.error) as e:
        sys.stderr.write('KWAJ.PY: %s: %s\n' % (argv[0], e))
        return 1
    with open(argv[1], 'wb') as f:
        f.write(out)
    print(name)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
