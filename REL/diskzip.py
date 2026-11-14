"""diskzip.py - write or check the zip of a release disk's files.

usage: python3 -I REL/diskzip.py DIR TOP ZIP
       python3 -I REL/diskzip.py --check ZIP OUT DIR TOP

The first form writes ZIP from every file below DIR, each as the entry
TOP/PATH, so that the same files give the same ZIP byte for byte: the
entries in the byte order of their names, deflated at LEVEL, each dated
by its file's time of modification read in UTC, made on Unix (SYSTEM)
as a plain file with the permissions 0644 (ATTR), and no directory
entry, extra field or comment.  The bytes also depend on the deflate of
the host's zlib.

--check holds ZIP to each of those promises on its own, against DIR, a
copy of the files dated as the zip's were (a disk read back): the names
sorted, each under TOP/ and none a directory; no archive comment; for
every entry DEFLATED, SYSTEM, ATTR, no extra field in either header, the
date of the same file of DIR, and stored bytes equal to this host's
deflate of the data at LEVEL.  Then it extracts every entry, its CRC
checked, into the directory OUT, for the caller to compare with DIR.

Exit status: 0 done; 1 ZIP breaks a promise or is damaged, or a file
cannot be read or written; 2 a usage error.
"""

import os
import stat
import sys
import time
import zipfile
import zlib

LEVEL = 9
SYSTEM = 3
ATTR = (stat.S_IFREG | 0o644) << 16


def usage(reason):
    """Write reason and the usage lines on standard error; return 2."""
    sys.stderr.write('diskzip: %s\n%s\n' % (reason, __doc__.split('\n\n')[1]))
    return 2


def reraise(e):
    """Stop os.walk at a directory it cannot read, DIR itself included."""
    raise e


def utc_date(path):
    """Return the zip date of path's time of modification in UTC."""
    t = time.gmtime(os.stat(path).st_mtime)
    return t[:5] + (t[5] // 2 * 2,)


def write(src, top, dst):
    """Write dst from the files below src, each under top/."""
    names = sorted(os.path.relpath(os.path.join(d, f), src)
                   for d, _, fs in os.walk(src, onerror=reraise) for f in fs)
    with zipfile.ZipFile(dst, 'w') as z:
        for name in names:
            path = os.path.join(src, name)
            info = zipfile.ZipInfo(top + '/' + name, utc_date(path))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = SYSTEM
            info.external_attr = ATTR
            with open(path, 'rb') as f:
                z.writestr(info, f.read(), compresslevel=LEVEL)


def local(f, info):
    """Return the extra field of info's local header and its stored bytes."""
    f.seek(info.header_offset)
    head = f.read(30)
    if head[:4] != b'PK\x03\x04':
        raise zipfile.BadZipFile('%s: no local header' % info.filename)
    nlen = int.from_bytes(head[26:28], 'little')
    elen = int.from_bytes(head[28:30], 'little')
    extra = f.read(nlen + elen)[nlen:]
    return extra, f.read(info.compress_size)


def check(zpath, out, src, top):
    """Return the promises zpath breaks; extract it into out if none."""
    bad = []
    with zipfile.ZipFile(zpath) as z, open(zpath, 'rb') as f:
        infos = z.infolist()
        names = [i.filename for i in infos]
        if names != sorted(names):
            bad.append('the entries are not in name order')
        if z.comment:
            bad.append('an archive comment')
        for i in infos:
            n = i.filename
            if not n.startswith(top + '/') or n.endswith('/'):
                bad.append('%s: not a file under %s/' % (n, top))
                continue
            if i.compress_type != zipfile.ZIP_DEFLATED:
                bad.append('%s: not deflated' % n)
            if i.create_system != SYSTEM:
                bad.append('%s: made on system %d' % (n, i.create_system))
            if i.external_attr != ATTR:
                bad.append('%s: attributes %08X' % (n, i.external_attr))
            extra, stored = local(f, i)
            if i.extra or extra:
                bad.append('%s: an extra field' % n)
            if i.date_time != utc_date(os.path.join(src, n[len(top) + 1:])):
                bad.append('%s: dated %s' % (n, i.date_time))
            d = zlib.compressobj(LEVEL, zlib.DEFLATED, -15)
            if i.compress_type == zipfile.ZIP_DEFLATED and \
                    stored != d.compress(z.read(i)) + d.flush():
                bad.append('%s: not deflated at level %d' % (n, LEVEL))
        if not bad:
            z.extractall(out)
    return bad


def main(argv):
    try:
        if argv[:1] == ['--check']:
            if len(argv) != 5:
                return usage('not --check ZIP OUT DIR TOP')
            bad = check(*argv[1:])
            for b in bad:
                sys.stderr.write('diskzip: %s: %s\n' % (argv[1], b))
            return 1 if bad else 0
        if len(argv) != 3:
            return usage('not DIR TOP ZIP')
        write(*argv)
    except (OSError, zipfile.BadZipFile, zlib.error, EOFError,
            NotImplementedError, ValueError) as e:
        sys.stderr.write('diskzip: %s\n' % e)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
