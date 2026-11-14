"""szddchk.py - check REL/szdd.py by round trips through the expander.

usage: python3 -I TEST/szddchk.py [--msexpand] SZDD T_LZEXP [FILE]...

Compresses each FILE, and first a set of files made here (empty, one to
three bytes, a long run, random bytes, blanks matched against the
window's, a block repeated at the window's size and either side of it,
every byte value), with the compressor SZDD twice, which must give the
same bytes, then has the host T_LZEXP expand the result and compare it
with the file.  --msexpand also expands it with msexpand of the
mscompress package, an independent reader of the format, which must give
the file too.  Prints each file's size and its compressed size, then the
totals.

Exit status: 0 every file passed; 1 one did not, or a file cannot be
read; 2 a usage error.
"""

import os
import random
import subprocess
import sys
import tempfile

WINSZ = 4096


def usage(reason):
    """Write reason and the usage lines on standard error; return 2."""
    sys.stderr.write('szddchk: %s\n%s\n' % (reason, __doc__.split('\n\n')[1]))
    return 2


def corpus():
    """Return the made files as (name, bytes) pairs."""
    r = random.Random(1)

    def noise(n):
        return bytes(r.getrandbits(8) for _ in range(n))
    made = [('EMPTY.BIN', b''), ('ONE.BIN', b'A'), ('TWO.BIN', b'AB'),
            ('THREE.BIN', b'ABA'), ('RUN.BIN', b'x' * 100000),
            ('NOISE.BIN', noise(20000)),
            ('BLANKS.TXT', b' ' * 40 + b'x' + b' ' * 30),
            ('BYTES.BIN', bytes(range(256)) * 40)]
    for d in (WINSZ - 1, WINSZ, WINSZ + 1):
        block = noise(d)
        made.append(('REP%d.BIN' % d, block + block[:300]))
    return made


def check(szdd, expander, ms, path, tmp):
    """Return the problems of the round trip of path and its compressed
    size."""
    bad = []
    outs = []
    for k in range(2):
        out = os.path.join(tmp, '%s.%d' % (os.path.basename(path), k))
        subprocess.run([sys.executable, '-I', szdd, path, out], check=True)
        with open(out, 'rb') as f:
            outs.append(f.read())
    if outs[0] != outs[1]:
        bad.append('two compressions differ')
    if subprocess.run([expander, out, path],
                      stdout=subprocess.DEVNULL).returncode != 0:
        bad.append('T_LZEXP does not give it back')
    if ms:
        with open(out, 'rb') as f:
            got = subprocess.run(['msexpand'], stdin=f, check=True,
                                 stdout=subprocess.PIPE).stdout
        with open(path, 'rb') as f:
            if got != f.read():
                bad.append('msexpand does not give it back')
    return bad, len(outs[0])


def main(argv):
    ms = argv[:1] == ['--msexpand']
    if ms:
        argv = argv[1:]
    if len(argv) < 2:
        return usage('not SZDD T_LZEXP')
    szdd, expander = argv[0], os.path.abspath(argv[1])
    failed = 0
    total = packed = 0
    try:
        with tempfile.TemporaryDirectory() as tmp:
            paths = []
            for name, data in corpus():
                paths.append(os.path.join(tmp, name))
                with open(paths[-1], 'wb') as f:
                    f.write(data)
            for path in paths + argv[2:]:
                bad, n = check(szdd, expander, ms, path, tmp)
                size = os.path.getsize(path)
                total += size
                packed += n
                print('%-40s %8d %8d' % (path if path in argv else
                                         os.path.basename(path), size, n))
                for b in bad:
                    sys.stderr.write('szddchk: %s: %s\n' % (path, b))
                failed += 1 if bad else 0
    except (OSError, subprocess.CalledProcessError) as e:
        sys.stderr.write('szddchk: %s\n' % e)
        return 1
    print('%-40s %8d %8d' % ('total', total, packed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
