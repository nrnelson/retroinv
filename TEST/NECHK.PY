"""NECHK.PY - check a Win16 program's NE header for protected-mode Windows 3.0.

usage: python3 -I TEST/NECHK.PY [--heap N] [--stack N] [--imports LIST]
           [--icon NAME] EXE

EXE is a Win16 program as wlink and wrc -30 -t write it: a DOS stub whose
double word at 3Ch gives the file offset of the NE header, which must
say:

- target system Windows (byte 36h is 2) and expected Windows version
  3.00 (word 3Eh is 0300h), as wrc -30 marks a program written for
  Windows 3.0;
- an application (module flags, word 0Ch: bit 15 clear) with a data
  segment per instance (bits 0-1 are 2), marked for protected mode only
  (bit 3 set), as wrc -t marks it, so that Windows 3.0 refuses to start
  it in real mode;
- every code segment moveable and discardable (segment flags 0010h and
  1000h) and every data segment moveable and not discardable, the
  automatic data segment (word 0Eh) among them, so that Windows may
  move them, and drop and reload the code, to make room;
- with --heap N and --stack N, a local heap (word 10h) and a stack (word
  12h) of N bytes;
- with --imports LIST, every function it imports exported by Windows
  3.0: the relocation records of its segments (those with flag 0100h)
  name each import by module and ordinal or by module and name, the
  module through the module reference table, and LIST holds one
  "MODULE.ordinal NAME" line per export of Windows 3.0 ("#" starts a
  comment).  An import Windows 3.0 lacks still loads there, and fails
  only when the program reaches it;
- with --icon NAME, a group-icon resource (type 800Eh of the resource
  table, word 24h) named NAME, the icon Windows shows for the program.

When all of this holds it prints one line of the facts; otherwise one
line for each that does not.  Exit status 0 when all hold, 1 when one
does not, 2 on a usage or read error or a file that is not an NE
program.
"""
import struct
import sys

NE_TARGET_WINDOWS = 2
NE_WINVER = 0x0300
MF_DATA_MASK = 0x0003           # module flags: the data segment type,
MF_DATA_MULTIPLE = 0x0002       # one per instance;
MF_PMODE_ONLY = 0x0008          # protected mode only;
MF_LIBRARY = 0x8000             # a DLL
SF_DATA = 0x0001                # segment flags: a data segment,
SF_MOVEABLE = 0x0010            # moveable,
SF_RELOCS = 0x0100              # with relocation records,
SF_DISCARDABLE = 0x1000         # discardable
RT_KIND = 0x03                  # relocation flags: the target kind,
RT_ORDINAL = 0x01               # an import by ordinal,
RT_NAME = 0x02                  # an import by name
RT_GROUP_ICON = 0x800E          # resource type: a group icon
RID_NUMBER = 0x8000             # resource ID: a number, not a name


class NeError(Exception):
    """The file is not an NE program, or its tables lie outside it."""


def word(data, at):
    """The little-endian word at offset at of data."""
    if at < 0 or at + 2 > len(data):
        raise NeError('offset %#x is past the end of the file' % at)
    return struct.unpack_from('<H', data, at)[0]


def pstr(data, at):
    """The string of a length byte and its characters at offset at."""
    if at >= len(data) or at + 1 + data[at] > len(data):
        raise NeError('name at %#x is past the end of the file' % at)
    return data[at + 1:at + 1 + data[at]].decode('latin-1')


def imports(data, ne, segtab, nsegs):
    """The set of the program's imports, each "MODULE.ordinal" or
    "MODULE NAME", read from the relocation records of its segments."""
    shift = word(data, ne + 0x32) or 9
    modtab = ne + word(data, ne + 0x28)
    impnames = ne + word(data, ne + 0x2A)
    nmods = word(data, ne + 0x1E)
    found = set()
    for i in range(nsegs):
        sector, size, sf = (word(data, segtab + 8 * i + k) for k in (0, 2, 4))
        if not sf & SF_RELOCS or not sector:
            continue
        at = (sector << shift) + (size or 0x10000)
        for r in range(word(data, at)):
            rec = at + 2 + 8 * r
            if rec + 8 > len(data):
                raise NeError('relocations of segment %d past the end of '
                              'the file' % (i + 1))
            kind = data[rec + 1] & RT_KIND
            if kind not in (RT_ORDINAL, RT_NAME):
                continue
            mod, value = word(data, rec + 4), word(data, rec + 6)
            if not 1 <= mod <= nmods:
                raise NeError('relocation %d of segment %d names module %d '
                              'of %d' % (r + 1, i + 1, mod, nmods))
            module = pstr(data, impnames + word(data, modtab + 2 * mod - 2))
            if kind == RT_ORDINAL:
                found.add('%s.%d' % (module, value))
            else:
                found.add('%s %s' % (module, pstr(data, impnames + value)))
    return found


def group_icons(data, ne):
    """The names of the program's group-icon resources, upper case."""
    rtab = ne + word(data, ne + 0x24)
    found = set()
    if rtab == ne + word(data, ne + 0x26):
        return found
    at = rtab + 2
    while word(data, at):
        kind, count = word(data, at), word(data, at + 2)
        for k in range(count):
            rid = word(data, at + 8 + 12 * k + 6)
            if kind == RT_GROUP_ICON and not rid & RID_NUMBER:
                found.add(pstr(data, rtab + rid).upper())
        at += 8 + 12 * count
    return found


def read_list(path):
    """The exports of LIST at path, each as "MODULE.ordinal" and as
    "MODULE NAME"."""
    known = set()
    with open(path, 'rb') as f:
        for num, line in enumerate(f.read().decode('latin-1').split('\n'), 1):
            line = line.split('#')[0].split()
            if not line:
                continue
            if len(line) != 2 or '.' not in line[0]:
                raise NeError('%s:%d: not "MODULE.ordinal NAME"' % (path, num))
            known.add(line[0])
            known.add('%s %s' % (line[0].split('.')[0], line[1]))
    return known


def check(path, heap, stack, known, icon):
    """Print the facts of the program at path, or what is wrong with
    them, its imports held to known unless that is None and its icon
    named icon unless that is None; return the number of problems."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:2] != b'MZ' or len(data) < 0x40:
        raise NeError('no DOS header')
    ne = struct.unpack_from('<I', data, 0x3C)[0]
    if data[ne:ne + 2] != b'NE' or ne + 0x40 > len(data):
        raise NeError('no NE header')
    flags = word(data, ne + 0x0C)
    auto = word(data, ne + 0x0E)
    nsegs = word(data, ne + 0x1C)
    segtab = ne + word(data, ne + 0x22)
    target = data[ne + 0x36]
    winver = word(data, ne + 0x3E)

    bad = []
    if target != NE_TARGET_WINDOWS:
        bad.append('target system %d, not Windows (%d)'
                   % (target, NE_TARGET_WINDOWS))
    if winver != NE_WINVER:
        bad.append('expected Windows version %04Xh, not %04Xh'
                   % (winver, NE_WINVER))
    if flags & MF_LIBRARY:
        bad.append('a library, not an application')
    if flags & MF_DATA_MASK != MF_DATA_MULTIPLE:
        bad.append('module flags %04Xh: not a data segment per instance'
                   % flags)
    if not flags & MF_PMODE_ONLY:
        bad.append('not marked for protected mode only')
    if not 1 <= auto <= nsegs:
        bad.append('automatic data segment %d of %d segments'
                   % (auto, nsegs))
    ncode = 0
    for i in range(nsegs):
        sf = word(data, segtab + 8 * i + 4)
        what = 'segment %d (flags %04Xh)' % (i + 1, sf)
        if not sf & SF_MOVEABLE:
            bad.append(what + ' is fixed')
        if sf & SF_DATA:
            if sf & SF_DISCARDABLE:
                bad.append(what + ', data, is discardable')
        else:
            ncode += 1
            if i + 1 == auto:
                bad.append(what + ', the automatic data segment, is code')
            if not sf & SF_DISCARDABLE:
                bad.append(what + ', code, is not discardable')
    for name, want, at in (('heap', heap, 0x10), ('stack', stack, 0x12)):
        if want is not None and word(data, ne + at) != want:
            bad.append('%s of %d bytes, not %d'
                       % (name, word(data, ne + at), want))
    used = imports(data, ne, segtab, nsegs)
    if known is not None:
        bad += ['imports %s, which Windows 3.0 lacks' % imp
                for imp in sorted(used - known)]
    if icon is not None and icon.upper() not in group_icons(data, ne):
        bad.append('no group icon %s' % icon)

    for what in bad:
        print('%s: %s' % (path, what))
    if not bad:
        print('%s: Windows %d.%02d, protected mode, heap %d, stack %d, '
              '%d code and %d data segments, %d imports%s%s'
              % (path, winver >> 8, winver & 0xFF, word(data, ne + 0x10),
                 word(data, ne + 0x12), ncode, nsegs - ncode, len(used),
                 '' if known is None else ' of Windows 3.0',
                 '' if icon is None else ', icon ' + icon))
    return len(bad)


def main(argv):
    opts = {}
    try:
        while len(argv) > 2 and argv[0] in ('--heap', '--stack', '--imports',
                                            '--icon'):
            opts[argv[0]] = (argv[1] if argv[0] in ('--imports', '--icon')
                             else int(argv[1]))
            argv = argv[2:]
    except ValueError:
        argv = []
    if len(argv) != 1:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    try:
        known = opts.get('--imports')
        if known is not None:
            known = read_list(known)
        return 1 if check(argv[0], opts.get('--heap'), opts.get('--stack'),
                          known, opts.get('--icon')) else 0
    except (OSError, NeError) as e:
        sys.stderr.write('nechk: %s: %s\n' % (argv[0], e))
        return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
