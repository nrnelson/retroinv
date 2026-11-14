"""chkbad.py - CHKDBF.PY must reject damaged copies of the golden files.

usage: python3 -I TEST/DBASE/chkbad.py

The undamaged files in TEST/GOLDEN/DB3 must pass CHKDBF.PY --strict, as
must three generated indexes with shapes dBASE writes (leaves at
different depths, a full inner page, an inner page with no key).
Then, once per case, the directory is copied to a temporary place, one
thing is damaged, and CHKDBF.PY must report "problem(s)" and exit with
status 1, both with and without --strict.  Cases marked "strict" damage
something that other dBASE-compatible programs do write (no 0x1A after
the last record, a 'Y' logical, a memo ended by one 0x1A, a longer
header, a year byte above 99, a work-area byte above 10), so only
--strict may reject them and the plain run must pass.  The location
cases add a LOCS.DBF of the inventory's layout, a small tree that must
pass, and break its rules: a loop of parents, a chain past the depth
limit, a name twice under one parent, a code twice, a dangling
PARENT_ID, and an item (an ITEMS.DBF with LOC_ID) at a deleted
location.  The counter cases add an ITEMS.DBF with ON_LOAN, RESERVED,
IN_USE and ATTACHED beside a LOANS.DBF with KIND, good ones that must
pass (an ended row and a negative QTY counting nothing) and a wrong
RESERVED, a wrong ATTACHED and more units held than QTY.  A crash of
CHKDBF.PY (exit status 3) or a hang never counts as a rejection.
Exit status 1 if any case is wrong.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GOLD = os.path.join(HERE, '..', 'GOLDEN', 'DB3')
CHK = os.path.join(HERE, '..', 'CHKDBF.PY')

# ITEMS.DBF: header length and record length (21 fields), and the offset
# of each field used below within a record.
ITEMS_HDR, ITEMS_REC = 705, 353
OFF_ACQ_DATE = 1 + 8 + 4 + 20 + 30 + 30 + 30 + 60 + 3 + 5 + 5 + 6 + 8
OFF_ACQ_PRICE = OFF_ACQ_DATE + 8 + 30
OFF_NOTES = ITEMS_REC - 10
# USERS.DBF: record 1, field MUSTCHG (after 11 descriptors and the fields
# before it).
USERS_MUSTCHG = 32 + 11 * 32 + 1 + 1 + 4 + 8 + 30 + 8 + 1 + 4 + 32 + 1
CATS_HDR = 32 + 3 * 32 + 1
# LOANS.DBF: header length (9 fields); LOAN_ID is the first field.
LOANS_HDR = 32 + 9 * 32 + 1


def rec(n):
    return ITEMS_HDR + (n - 1) * ITEMS_REC


# The location fields of LOCS.DBF and a field list of ITEMS.DBF with
# LOC_ID, as (name, type, width); rows are lists of texts, a leading '*'
# row flag marking a deleted record.
LOCS_FIELDS = [('LOC_ID', 'N', 8), ('PARENT_ID', 'N', 8),
               ('LOC_NAME', 'C', 30), ('LOC_CODE', 'C', 10),
               ('LTYPE', 'C', 8)]
ITEMS_LOC_FIELDS = [('ITEM_ID', 'N', 8), ('LOC_ID', 'N', 8)]
GOOD_LOCS = [[' ', '1', '0', 'Office', 'OFF', 'ROOM'],
             [' ', '2', '1', 'Shelf', 'S1', 'SHELF'],
             [' ', '3', '2', 'Bin', '', 'BIN'],
             ['*', '4', '1', 'Gone', '', 'BIN']]
GOOD_ITEMS = [[' ', '1', '3'], [' ', '2', '0']]

# The counters of ITEMS and the rows of LOANS they sum: item 1 has three
# loans (one of blank KIND, one naming item 2 as host, which counts
# nothing on item 2) and a reservation for item 2, item 2 a unit in use;
# an ended row and an open loan of QTY -1 count nothing.
HOLD_ITEMS = [('ITEM_ID', 'N', 8), ('LOC_ID', 'N', 8), ('QTY', 'N', 5),
              ('ON_LOAN', 'N', 5), ('RESERVED', 'N', 5), ('IN_USE', 'N', 5),
              ('ATTACHED', 'N', 5)]
HOLD_LOANS = [('LOAN_ID', 'N', 8), ('ITEM_ID', 'N', 8), ('KIND', 'C', 1),
              ('QTY', 'N', 5), ('HOST_ID', 'N', 8), ('RET_DATE', 'D', 8)]
GOOD_HOLD_ITEMS = [[' ', '1', '0', '5', '3', '1', '0', '0'],
                   [' ', '2', '0', '1', '0', '0', '1', '1']]
GOOD_HOLD_LOANS = [[' ', '1', '1', 'L', '1', '0', ''],
                   [' ', '2', '1', 'R', '1', '2', ''],
                   [' ', '3', '2', 'U', '1', '0', ''],
                   [' ', '4', '2', 'R', '1', '1', '20261001'],
                   [' ', '5', '1', 'L', '-1', '0', ''],
                   [' ', '6', '1', 'L', '1', '2', ''],
                   [' ', '7', '1', '', '1', '0', '']]


def make_dbf(c, name, fields, rows):
    """Write table name as dBASE III writes one, without memo."""
    reclen = 1 + sum(f[2] for f in fields)
    hdr = 32 + 32 * len(fields) + 1
    out = bytearray(struct.pack('<BBBBIHH', 3, 26, 11, 4, len(rows), hdr,
                                reclen)) + bytes(20)
    for fname, ftype, width in fields:
        out += (fname.encode().ljust(11, b'\0') + ftype.encode() +
                bytes(4) + bytes([width, 0]) + bytes(14))
    out += b'\r'
    for row in rows:
        out += row[0].encode()
        for (fname, ftype, width), v in zip(fields, row[1:]):
            v = v.rjust(width) if ftype == 'N' else v.ljust(width)
            out += v.encode()
    out += b'\x1a'
    c.write(name, out)


def locs(rows, items=None):
    """A damage writing LOCS.DBF of GOOD_LOCS and rows, and ITEMS.DBF
    of items when given."""
    def damage(c):
        make_dbf(c, 'LOCS.DBF', LOCS_FIELDS, GOOD_LOCS + rows)
        if items is not None:
            make_dbf(c, 'ITEMS.DBF', ITEMS_LOC_FIELDS, items)
    return damage


def holds(items):
    """A damage writing ITEMS.DBF of items with the counters beside the
    LOANS.DBF of GOOD_HOLD_LOANS."""
    def damage(c):
        make_dbf(c, 'ITEMS.DBF', HOLD_ITEMS, items)
        make_dbf(c, 'LOANS.DBF', HOLD_LOANS, GOOD_HOLD_LOANS)
    return damage


def chain(n):
    """Rows of a chain of n locations from the top, IDs from 10."""
    return [[' ', str(10 + i), str(9 + i) if i else '0', 'L%d' % i, '',
             'BOX'] for i in range(n)]


class Case:
    def __init__(self, work):
        self.dir = os.path.join(work, 'DB3')

    def path(self, name):
        return os.path.join(self.dir, name)

    def read(self, name):
        with open(self.path(name), 'rb') as fh:
            return bytearray(fh.read())

    def write(self, name, data):
        with open(self.path(name), 'wb') as fh:
            fh.write(bytes(data))

    def patch(self, name, off, new):
        d = self.read(name)
        if off < 0:
            off += len(d)
        d[off:off + len(new)] = new
        self.write(name, d)

    def trunc(self, name, n):
        d = self.read(name)
        self.write(name, d[:len(d) - n])


def longer_header(c):
    """CATS.DBF with one 0x00 after the 0x0D terminator."""
    d = c.read('CATS.DBF')
    d[CATS_HDR:CATS_HDR] = b'\0'
    d[8:10] = struct.pack('<H', CATS_HDR + 1)
    c.write('CATS.DBF', d)


def deep_index(c):
    """An index whose first entries form a chain of inner pages 2000
    levels deep, deeper than Python's default recursion limit: the
    checker must stop at its depth limit instead of crashing."""
    levels = 2000
    npages = 1 + 2 * levels + 1
    d = bytearray(npages * 512)
    struct.pack_into('<IIIHHHH', d, 0, 1, npages, 0, 8, 31, 0, 16)
    d[24:27] = b'ID\0'
    for lv in range(levels):
        p = 1 + lv
        leaf = 1 + levels + lv
        base = p * 512
        struct.pack_into('<H', d, base, 1)
        struct.pack_into('<II', d, base + 4, p + 1, 0)
        d[base + 12:base + 20] = b'%8d' % lv
        struct.pack_into('<II', d, base + 20, leaf, 0)
        lb = leaf * 512
        struct.pack_into('<HxxII', d, lb, 1, 0, 1)
        d[lb + 12:lb + 20] = b'%8d' % lv
    last = (1 + 2 * levels) * 512
    struct.pack_into('<HxxII', d, last, 1, 0, 1)
    d[last + 12:last + 20] = b'%8d' % levels
    c.write('DEEP.NDX', d)


def build_index(c, name, tree):
    """Write an index (STR(ID,8)-style keys, 31 keys per page) whose
    shape is tree: a leaf is a list of record numbers (the key of record
    r is r in 8 columns), an inner page is a tuple of subtrees, its keys
    being the highest key of each subtree but the last."""
    pages = []

    def place(node):
        pages.append(None)
        me = len(pages)
        if isinstance(node, list):
            body = struct.pack('<Hxx', len(node))
            for r in node:
                body += struct.pack('<II', 0, r) + b'%8d' % r
            top = node[-1] if node else None
        else:
            kids = [place(k) for k in node]
            body = struct.pack('<Hxx', len(kids) - 1)
            for pg, top in kids[:-1]:
                body += struct.pack('<II', pg, 0) + b'%8d' % top
            body += struct.pack('<I', kids[-1][0])
            top = kids[-1][1]
        pages[me - 1] = body.ljust(512, b'\0')
        return me, top

    root, _ = place(tree)
    head = struct.pack('<IIIHHHH', root, len(pages) + 1, 0, 8, 31, 0, 16)
    head = (head + b'\0\0\0\0' + b'STR(ID,8) \0').ljust(512, b'\0')
    c.write(name, head + b''.join(pages))


# Shapes that dBASE III Plus itself writes: leaves at different depths
# (INDEX ON folds a trailing single-child level into its parent), inner
# pages full to 31 keys plus the rightmost child, and an inner page that
# key deletions left with no key and one child in slot 0.
GOOD_INDEXES = [
    ('unbalanced', ([1, 2], ([3, 4], [5, 6]))),
    ('full inner page', tuple([r] for r in range(1, 33))),
    ('inner page left with no key', ([1, 2], ([3, 4],), [5])),
]

CASES = [
    ('LOAN_ID not ascending', 'both', ['LOANS.DBF'],
     lambda c: c.patch('LOANS.DBF', LOANS_HDR + 1, b'       3')),
    ('memo table with version 0x03', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', 0, b'\x03')),
    ('terminator 0x0D replaced', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', ITEMS_HDR - 1, b'\x00')),
    ('bad delete flag', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', rec(2), b'X')),
    ('N left-justified', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', rec(1) + OFF_ACQ_PRICE, b'1299.99  ')),
    ('N with a superscript digit', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', rec(1) + OFF_ACQ_PRICE, b'  1299.9\xb9')),
    ('invalid date', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', rec(1) + OFF_ACQ_DATE, b'19881345')),
    ('date with a superscript digit', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', rec(1) + OFF_ACQ_DATE, b'1988031\xb2')),
    ('memo pointer past next free', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', rec(1) + OFF_NOTES, b'        99')),
    ('memo pointer superscript', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', rec(1) + OFF_NOTES, b'         \xb3')),
    ('overlapping memos', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', rec(5) + OFF_NOTES, b'         3')),
    ('file truncated', 'both', ['ITEMS.DBF'],
     lambda c: c.trunc('ITEMS.DBF', 2)),
    ('lower-case field name', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', 32, b'item_id')),
    ('DBT next free too small', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBT', 0, b'\x02\0\0\0')),
    ('memo terminator removed', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBT', -2, b'  ')),
    ('record length wrong', 'both', ['ITEMS.DBF'],
     lambda c: c.patch('ITEMS.DBF', 10, b'\x60\x01')),
    ('NDX leaf keys swapped', 'both', ['--table', 'ITEMS.DBF', 'ITEMS1.NDX'],
     lambda c: (c.patch('ITEMS1.NDX', 512 + 4 + 8, b'       2'),
                c.patch('ITEMS1.NDX', 512 + 4 + 16 + 8, b'       1'))),
    ('NDX inner key wrong', 'both', ['BIG1.NDX'],
     lambda c: c.patch('BIG1.NDX', 11 * 512 + 4 + 8, b'      30')),
    ('NDX root out of range', 'both', ['BIG1.NDX'],
     lambda c: c.patch('BIG1.NDX', 0, b'\x40\0\0\0')),
    ('NDX cycle', 'both', ['BIG1.NDX'],
     lambda c: c.patch('BIG1.NDX', 11 * 512 + 4, b'\x0b\0\0\0')),
    ('NDX split index key damaged', 'both', ['BIG2.NDX'],
     lambda c: c.patch('BIG2.NDX', 512 + 4 + 8, b'N999')),
    ('NDX chain too deep', 'both', ['DEEP.NDX'], deep_index),
    ('NDX record indexed twice', 'both', ['DUP.NDX'],
     lambda c: build_index(c, 'DUP.NDX', [1, 1, 2])),
    ('location loop', 'both', ['LOCS.DBF'],
     locs([[' ', '5', '6', 'A', '', 'BOX'], [' ', '6', '5', 'B', '', 'BOX']])),
    ('location chain past depth 8', 'both', ['LOCS.DBF'], locs(chain(9))),
    ('location name twice', 'both', ['LOCS.DBF'],
     locs([[' ', '5', '1', 'SHELF', '', 'SHELF']])),
    ('location code twice', 'both', ['LOCS.DBF'],
     locs([[' ', '5', '0', 'Garage', 's1', 'ROOM']])),
    ('location dangling parent', 'both', ['LOCS.DBF'],
     locs([[' ', '5', '99', 'Lost', '', 'BOX']])),
    ('item at a deleted location', 'both', ['ITEMS.DBF'],
     locs([], [[' ', '1', '4']])),
    ('RESERVED not its open rows', 'both', ['ITEMS.DBF'],
     holds([[' ', '1', '0', '5', '3', '2', '0', '0'],
            GOOD_HOLD_ITEMS[1]])),
    ('ATTACHED not its hosted rows', 'both', ['ITEMS.DBF'],
     holds([GOOD_HOLD_ITEMS[0],
            [' ', '2', '0', '1', '0', '0', '1', '0']])),
    ('more units held than QTY', 'both', ['ITEMS.DBF'],
     holds([GOOD_HOLD_ITEMS[0],
            [' ', '2', '0', '0', '0', '0', '1', '1']])),
    ('strict: no 0x1A at end', 'strict', ['CATS.DBF'],
     lambda c: c.trunc('CATS.DBF', 1)),
    ('strict: logical Y', 'strict', ['USERS.DBF'],
     lambda c: c.patch('USERS.DBF', USERS_MUSTCHG, b'Y')),
    ('strict: memo ends with one 0x1A', 'strict', ['ITEMS.DBF'],
     lambda c: c.trunc('ITEMS.DBT', 1)),
    ('strict: header > 32n+33', 'strict', ['CATS.DBF'], longer_header),
    ('strict: year byte > 99', 'strict', ['CATS.DBF'],
     lambda c: c.patch('CATS.DBF', 1, b'\x7e')),
    ('strict: work-area byte > 10', 'strict', ['CATS.DBF'],
     lambda c: c.patch('CATS.DBF', 32 + 20, b'\x0b')),
]


def run(cwd, args):
    try:
        r = subprocess.run([sys.executable, '-I', CHK] + args, cwd=cwd,
                           capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return None, 'timed out'
    return r.returncode, r.stdout


def rejected(rc, out):
    return rc == 1 and 'problem(s)' in out


def main(argv):
    if argv:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    work = tempfile.mkdtemp(prefix='chkbad.')
    bad = 0
    try:
        c = Case(work)
        shutil.copytree(GOLD, c.dir)
        names = sorted(f for f in os.listdir(c.dir) if f.endswith(('.DBF', '.DBT')))
        checks = [names,
                  ['--table', 'BIG.DBF', 'BIG1.NDX', 'BIG2.NDX', 'BIG3.NDX'],
                  ['--table', 'ITEMS.DBF', 'ITEMS1.NDX'],
                  ['--table', 'NUM2000.DBF', 'NUM2000.NDX'],
                  ['--table', 'NUM993.DBF', 'NUM993.NDX'],
                  ['--table', 'WIDE.DBF', 'WIDE1.NDX'],
                  ['--table', 'WIDED.DBF', 'WIDED1.NDX']]
        for args in checks:
            rc, out = run(c.dir, ['--strict'] + args)
            if rc != 0:
                bad += 1
                print('golden files FAIL\n' + out)
        for name, tree in GOOD_INDEXES:
            build_index(c, 'GOOD.NDX', tree)
            rc, out = run(c.dir, ['--strict', 'GOOD.NDX'])
            if rc != 0:
                bad += 1
                print('good index %s FAIL\n%s' % (name, out))
        locs(chain(8) + [[' ', '5', '1', ' Shelf', 'S2', 'SHELF']],
             GOOD_ITEMS)(c)
        rc, out = run(c.dir, ['--strict', 'LOCS.DBF', 'ITEMS.DBF'])
        if rc != 0:
            bad += 1
            print('good locations FAIL\n' + out)
        holds(GOOD_HOLD_ITEMS)(c)
        rc, out = run(c.dir, ['--strict', 'ITEMS.DBF', 'LOANS.DBF'])
        if rc != 0:
            bad += 1
            print('good counters FAIL\n' + out)
        if not bad:
            print('%-34s PASS' % 'golden files, indexes, locations, counters')
        for name, mode, args, damage in CASES:
            shutil.rmtree(c.dir)
            shutil.copytree(GOLD, c.dir)
            damage(c)
            rc_s, out_s = run(c.dir, ['--strict'] + args)
            rc_n, out_n = run(c.dir, args)
            if mode == 'strict':
                ok = rejected(rc_s, out_s) and rc_n == 0
            else:
                ok = rejected(rc_s, out_s) and rejected(rc_n, out_n)
            bad += not ok
            first = [l.strip() for l in out_s.splitlines() if l.startswith('  ')][:1]
            print('%-34s %s strict=%s plain=%s %s'
                  % (name, 'PASS' if ok else 'FAIL', rc_s, rc_n,
                     first[0] if first else out_s.strip()))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
