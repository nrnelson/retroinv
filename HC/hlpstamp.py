"""hlpstamp.py - make a help file of HC.EXE 3.00 the same at every build.

usage: python3 -I HC/hlpstamp.py IN.HLP SOURCE.RTF OUT.HLP

HC.EXE writes, besides the help, the time of the build and whatever its
memory held into the unused ends of fixed fields.  This writes IN.HLP to
OUT.HLP with these bytes set, all that differ between two builds of one
source:
  - the build date (GenDate) of the system file |SYSTEM, set to the
    first four bytes of SOURCE.RTF's SHA-256, a stamp that changes when
    the text does, as Windows Help's annotations and bookmarks, which
    it keys, need;
  - zeroed, the flags word of the system file's header but for
    SYS_FLAGS, and the rest after its NUL of its title field
    (TITLEFIELD bytes) and of each face name field of |FONT;
  - zeroed, in the internal directory and the B-trees of BTREES, the
    keywords' and the titles', the rest of the 16-byte structure field
    after its NUL and the unused end of each page;
  - zeroed, in each record of paragraphs of |TOPIC, the word before its
    flags but for PARA_WORD, and the flags not in PARA_FLAGS, which no
    paragraph property sets and Windows Help never reads.

HC.EXE leaves in these bits of the three words what its stack held,
which a timer interrupt during the compile changes.  It clears bit 0
of the system file's flags, and Windows Help refuses a file with it
set; Windows Help reads bit 0 of the paragraphs' word, 0 in every file
seen, and no other.  Both bits are kept as they are.

The layout is that of Windows Help 3.0 files: a header (magic 3F5F0300h,
then the offset of the directory), each internal file a 9-byte header
(reserved and used length, flags) before its data, the directory a
one-level B-tree of names and offsets.  |TOPIC is blocks of TOPIC_BLOCK
bytes, each a header of TOPIC_HEAD bytes and then links, which run on
from block to block; where HC.EXE starts no link before a block's end,
TOPIC_FILL bytes end the block.  A link is a header of LINK_HEAD bytes
(the link's length, three more longs, the length of its first part
with this header, its kind), the first part and the text.  The first
part of a record of paragraphs is its text's length (a long, in two
bytes when their low bit is clear, else in four), a second such long,
which this knows only in two bytes, the word before the flags, and the
flags.  Every number this relies on is checked; a file not of that
layout is refused, and so is one with a B-tree or a kind of link this
does not know, whose slack it would leave.

Exit status: 0 when OUT.HLP is written, 1 when IN.HLP is refused, 2 on
a usage or read error.
"""
import hashlib
import struct
import sys

HLP_MAGIC = b'?_\x03\x00'
BT_MAGIC = 0x293B       # a B-tree's header
BT_HEAD = 38            # bytes of a B-tree's header
BT_STRUCT = 16          # bytes of its structure field
SYS_MAGIC = 0x036C      # the system file's header
SYS_HEAD = 12           # bytes of the system file's header: magic,
                        # version, date and flags
SYS_FLAGS = 0x0001      # the bit of its flags HC.EXE sets
SYS_VERSION = (15, 1)   # minor and major version of HC.EXE 3.00's files
TITLEFIELD = 33         # bytes of the title field of such a file
FILE_HEAD = 9           # bytes of an internal file's header
BTREES = (b'|KWBTREE', b'|TTLBTREE')    # B-trees of internal files
TOPIC_BLOCK = 2048      # bytes of a block of |TOPIC
TOPIC_HEAD = 12         # bytes of a block's header
TOPIC_LINKS = TOPIC_BLOCK - TOPIC_HEAD  # bytes of links in a block
TOPIC_FILL = b'X'       # the filler that ends a block
LINK_HEAD = 21          # bytes of a link's header
LINK_TOPIC = 2          # a link's kind: a topic's header
LINK_PARAS = 1          # a link's kind: a record of paragraphs
PARA_WORD = 0x0001      # the bit Windows Help reads of the word
                        # before a record of paragraphs' flags
PARA_FLAGS = 0x1FFF     # the flags the paragraph properties set


class Refused(Exception):
    pass


def u16(d, at):
    return struct.unpack_from('<H', d, at)[0]


def u32(d, at):
    return struct.unpack_from('<I', d, at)[0]


def clear_after_nul(d, at, n, what):
    """Zero the bytes of the n-byte field at at after its first NUL."""
    end = d.find(b'\0', at, at + n)
    if end < 0:
        raise Refused('%s without its NUL' % what)
    d[end:at + n] = bytes(at + n - end)


def btree(d, at, what):
    """Clear the slack of the B-tree file at at; return the offset of its
    first page, its page size and its root page and levels."""
    h = at + FILE_HEAD
    if u16(d, h) != BT_MAGIC:
        raise Refused('%s is not a B-tree' % what)
    psize = u16(d, h + 4)
    clear_after_nul(d, h + 6, BT_STRUCT, what + ' structure')
    root, _, total, levels = struct.unpack_from('<hhhh', d, h + 26)
    pages = h + BT_HEAD
    for p in range(total):
        page = pages + p * psize
        unused = u16(d, page)
        if unused > psize or page + psize > len(d):
            raise Refused('%s page %d out of bounds' % (what, p))
        d[page + psize - unused:page + psize] = bytes(unused)
    return pages, psize, root, levels


def topic_at(at, i):
    """The offset of byte i of the links of the |TOPIC file at at."""
    return at + FILE_HEAD + i + (i // TOPIC_LINKS + 1) * TOPIC_HEAD


def topic(d, at):
    """Zero what HC.EXE leaves unset in each record of paragraphs of
    the |TOPIC file at at."""
    used = u32(d, at + 4)
    blocks = (used + TOPIC_BLOCK - 1) // TOPIC_BLOCK
    end = used - blocks * TOPIC_HEAD
    i = 0
    while i < end:
        link = topic_at(at, i)
        room = TOPIC_LINKS - i % TOPIC_LINKS
        size = first = kind = 0
        if room >= LINK_HEAD:
            size, first, kind = struct.unpack_from('<i12xiB', d, link)
        if not LINK_HEAD <= first <= size or \
                kind not in (LINK_TOPIC, LINK_PARAS):
            if d[link:link + room] != TOPIC_FILL * room:
                raise Refused('|TOPIC link %d not known' % i)
            i += room
            continue
        if kind == LINK_PARAS:
            n = i + LINK_HEAD
            n += 4 if d[topic_at(at, n)] & 1 else 2
            if d[topic_at(at, n)] & 1:
                raise Refused('|TOPIC paragraphs %d: second long in four '
                              'bytes' % i)
            n += 2
            if n + 4 > i + first:
                raise Refused('|TOPIC paragraphs %d too short' % i)
            d[topic_at(at, n)] &= PARA_WORD
            d[topic_at(at, n + 1)] = 0
            d[topic_at(at, n + 3)] &= PARA_FLAGS >> 8
        i += size
    if i != end:
        raise Refused('|TOPIC ends inside a link')


def directory(d):
    """The internal files, name and offset, of the help file d."""
    if d[:4] != HLP_MAGIC:
        raise Refused('not a Windows Help file')
    pages, psize, root, levels = btree(d, u32(d, 4), 'directory')
    if levels != 1:
        raise Refused('a directory of %d levels' % levels)
    page = pages + root * psize
    n = u16(d, page + 2)
    at = page + 8
    files = []
    for _ in range(n):
        end = d.index(b'\0', at)
        files.append((bytes(d[at:end]), u32(d, end + 1)))
        at = end + 5
    return files


def stamp(d, rtf):
    files = dict(directory(d))
    if not {b'|SYSTEM', b'|FONT', b'|TOPIC'} <= files.keys():
        raise Refused('no |SYSTEM, |FONT or |TOPIC')
    s = files[b'|SYSTEM'] + FILE_HEAD
    magic, minor, major = struct.unpack_from('<HHH', d, s)
    if magic != SYS_MAGIC or (minor, major) != SYS_VERSION:
        raise Refused('system header %04X %d.%d, not of HC.EXE 3.00'
                      % (magic, major, minor))
    d[s + 6:s + 10] = hashlib.sha256(rtf).digest()[:4]
    d[s + 10:s + SYS_HEAD] = struct.pack('<H', u16(d, s + 10) & SYS_FLAGS)
    clear_after_nul(d, s + SYS_HEAD, TITLEFIELD, 'title')
    f = files[b'|FONT'] + FILE_HEAD
    faces, _, first, descr = struct.unpack_from('<HHHH', d, f)
    if faces == 0 or (descr - first) % faces:
        raise Refused('|FONT of %d face names in %d bytes'
                      % (faces, descr - first))
    width = (descr - first) // faces
    for k in range(faces):
        clear_after_nul(d, f + first + k * width, width, 'face name')
    topic(d, files[b'|TOPIC'])
    for name, at in files.items():
        if name in BTREES:
            btree(d, at, name.decode('latin-1'))
        elif u16(d, at + FILE_HEAD) == BT_MAGIC:
            raise Refused('an unknown B-tree %s' % name.decode('latin-1'))


def main(argv):
    if len(argv) != 3:
        sys.stderr.write('usage: python3 -I HC/hlpstamp.py IN.HLP '
                         'SOURCE.RTF OUT.HLP\n')
        return 2
    try:
        with open(argv[0], 'rb') as f:
            d = bytearray(f.read())
        with open(argv[1], 'rb') as f:
            rtf = f.read()
    except OSError as e:
        sys.stderr.write('hlpstamp: %s\n' % e)
        return 2
    try:
        stamp(d, rtf)
    except (Refused, ValueError, IndexError, struct.error) as e:
        sys.stderr.write('hlpstamp: %s: %s\n' % (argv[0], e))
        return 1
    try:
        with open(argv[2], 'wb') as f:
            f.write(d)
    except OSError as e:
        sys.stderr.write('hlpstamp: %s\n' % e)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
