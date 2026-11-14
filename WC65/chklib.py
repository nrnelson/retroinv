"""chklib.py - check that OMF object libraries are intact.

usage: python3 -I WC65/chklib.py LIB...

Walks every module of each library (a page-aligned sequence of OMF
records from THEADR or LHEADR to MODEND, up to the dictionary whose
offset the library header gives) and verifies each record's length and
checksum; a record whose type is not in KNOWN (the record types a
Watcom C 6.5 library holds) is refused as well.  Prints one line per
library, so a build can refuse a memory model whose run-time library is
unusable instead of failing later with an obscure linker message.

Exit status: 0 when every library is intact; 1 when one is damaged; 2 on
a usage error or when a file cannot be read.

The script only reads the files; it never runs anything from them.
"""
import sys

MODEND = (0x8A, 0x8B)
MODSTART = (0x80, 0x82)   # THEADR, LHEADR
KNOWN = {0x80, 0x82, 0x88, 0x8A, 0x8B, 0x8C, 0x8E, 0x90, 0x91, 0x94, 0x95,
         0x96, 0x98, 0x99, 0x9A, 0x9C, 0x9D, 0xA0, 0xA1, 0xA2, 0xA3,
         0xB0, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8}


def check(path):
    """Return (modules, problem) for the library at path; problem is
    None when the library is intact.  TEST/OMFNEAR.PY walks the records
    of an object file by the same length and checksum rules."""
    with open(path, 'rb') as f:
        d = f.read()
    if len(d) < 16 or d[0] != 0xF0:
        return 0, 'not an OMF library'
    page = (d[1] | d[2] << 8) + 3
    dict_at = d[3] | d[4] << 8 | d[5] << 16 | d[6] << 24
    if dict_at > len(d) or dict_at < page:
        return 0, 'dictionary offset outside the file'
    i = page
    mods = 0
    while i < dict_at:
        if d[i] == 0xF1:
            break
        if d[i] not in MODSTART:
            return mods, ('module %d at offset %d does not start with '
                          'THEADR or LHEADR' % (mods + 1, i))
        while True:
            if i + 3 > dict_at:
                return mods, 'module %d runs into the dictionary' % (mods + 1)
            t = d[i]
            n = d[i + 1] | d[i + 2] << 8
            end = i + 3 + n
            if t not in KNOWN:
                return mods, ('record %02Xh at offset %d: unknown type'
                              % (t, i))
            if n == 0:
                return mods, 'record %02Xh at offset %d: empty' % (t, i)
            if end > dict_at:
                return mods, ('record %02Xh at offset %d: truncated'
                              % (t, i))
            if d[end - 1] != 0 and sum(d[i:end]) & 0xFF:
                return mods, ('record %02Xh at offset %d: bad checksum'
                              % (t, i))
            i = end
            if t in MODEND:
                break
        mods += 1
        i = (i + page - 1) // page * page
    if mods == 0:
        return 0, 'no modules'
    return mods, None


def usage(reason):
    """Write reason and the usage line on standard error; return 2."""
    sys.stderr.write('chklib: %s\n%s\n' % (reason, __doc__.split('\n\n')[1]))
    return 2


def main(args):
    if not args:
        return usage('no LIB')
    bad = 0
    for path in args:
        try:
            mods, problem = check(path)
        except OSError as e:
            bad = 2
            sys.stderr.write('chklib: %s: cannot read: %s\n'
                             % (path, e.strerror or e))
            continue
        if problem:
            bad = max(bad, 1)
            print('chklib: %s: DAMAGED after %d modules: %s'
                  % (path, mods, problem))
        else:
            print('chklib: %s: %d modules ok' % (path, mods))
    return bad


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
