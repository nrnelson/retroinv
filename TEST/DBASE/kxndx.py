"""kxndx.py - check the indexes KXCHECK.PRG built against its key listing.

usage: python3 -I TEST/DBASE/kxndx.py WORKDIR

WORKDIR holds KEYEXPR.TXT and the KXnnn.NDX files of one KXCHECK.PRG run.
Every expression in KEYEXPR.TXT that has an "I KXnnn" line was indexed
into KXnnn.NDX.  For an "X" expression without an ERR line the index
must have a key length (u16 at header offset 12) equal to the LEN() of
the K lines, one leaf entry per K line, and each leaf key equal byte for
byte to the K line's key for the same record.  For a "V" expression the
key length must differ from LEN().  For an X expression that dBASE
refused at run time (an ERR line) dBASE may still write the index; every
key in it must then start with a NUL, the empty value it was left with.
Problems are printed; exit status 1 if there are any, 0
otherwise, 2 on a usage or read error.
"""
import os
import struct
import sys

PAGE = 512


def leaves(data):
    """Return the leaf entries {record number: key} reachable from the root
    and the key length."""
    root, pages = struct.unpack('<II', data[0:8])
    keylen = struct.unpack('<H', data[12:14])[0]
    size = struct.unpack('<H', data[18:20])[0]
    out = {}
    todo = [root]
    seen = set()
    while todo:
        pg = todo.pop()
        if pg in seen or pg <= 0 or pg >= pages or (pg + 1) * PAGE > len(data):
            continue
        seen.add(pg)
        p = data[pg * PAGE:(pg + 1) * PAGE]
        n = struct.unpack('<H', p[0:2])[0]
        if n == 0:
            continue
        first = struct.unpack('<I', p[4:8])[0]
        if first == 0:
            for i in range(n):
                e = p[4 + i * size:4 + (i + 1) * size]
                rec = struct.unpack('<I', e[4:8])[0]
                out[rec] = e[8:8 + keylen]
        else:
            for i in range(n + 1):
                e = p[4 + i * size:4 + i * size + 4]
                if len(e) == 4:
                    todo.append(struct.unpack('<I', e)[0])
    return keylen, out


def blocks(text):
    """Yield (kind, expression, {recno: key}, LEN, errors, index name)."""
    cur = None
    for raw in text.split(b'\n'):
        line = raw.rstrip(b'\r').rstrip(b'\x1a')
        if line[:2] in (b'X ', b'V '):
            if cur:
                yield cur
            cur = [line[:1].decode(), line[2:], {}, None, 0, None]
        elif cur is None:
            continue
        elif line.startswith(b'ERR'):
            cur[4] += 1
        elif line.startswith(b'I '):
            cur[5] = line[2:].decode().strip()
        elif line.startswith(b'K') and len(line) >= 12:
            rec = int(line[1:5])
            cur[3] = int(line[5:9])
            cur[2][rec] = line[11:-1]
    if cur:
        yield cur


def main(argv):
    if len(argv) != 1:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    work = argv[0]
    try:
        with open(os.path.join(work, 'KEYEXPR.TXT'), 'rb') as f:
            text = f.read()
    except OSError as e:
        sys.stderr.write('kxndx: %s\n' % e)
        return 2
    bad = 0
    count = 0
    for kind, expr, keys, length, errs, name in blocks(text):
        if name is None:
            continue
        count += 1
        what = '%s (%s)' % (name, expr.decode('latin-1'))
        path = os.path.join(work, name + '.NDX')
        if not os.path.exists(path):
            if kind == 'X' and errs == 0 or kind == 'V':
                print('kxndx: %s: no index file' % what)
                bad += 1
            continue
        with open(path, 'rb') as f:
            keylen, entries = leaves(f.read())
        if kind == 'X' and errs > 0:
            if any(k[:1] != b'\0' for k in entries.values()):
                print('kxndx: %s: refused but has keys' % what)
                bad += 1
        elif kind == 'V':
            if keylen == length:
                print('kxndx: %s: key length equals LEN() %d'
                      % (what, length))
                bad += 1
        else:
            if keylen != length:
                print('kxndx: %s: key length %d, LEN() %d'
                      % (what, keylen, length))
                bad += 1
            elif entries != keys:
                diff = sorted(r for r in set(entries) | set(keys)
                              if entries.get(r) != keys.get(r))
                print('kxndx: %s: leaf keys differ for records %s'
                      % (what, diff[:8]))
                bad += 1
    if count == 0:
        print('kxndx: no indexed expressions in KEYEXPR.TXT')
        return 1
    print('kxndx: %d indexes checked, %d problems' % (count, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
