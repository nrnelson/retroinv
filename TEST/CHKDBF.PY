"""CHKDBF.PY - structural check of dBASE III tables, memo files and indexes.

usage: python3 -I TEST/CHKDBF.PY [--strict] [--table DBF] [--quiet] FILE...

FILE may be a table (.DBF), a memo file (.DBT) or an index (.NDX); the
type is taken from the extension (any case).

Tables: version byte, last-update date, header length against the field
count, the 0x0D terminator, every field descriptor (name, type, width,
decimals), record length, file size (header + records + optional 0x1A),
every record's delete flag and every field's contents (C, N, D, L, M).
When the table has a memo field, the .DBT of the same base name in the
same directory is opened too and every memo pointer is followed: it must
lie inside the memo file, below its next-free block, and the memo must
end with 0x1A 0x1A (or 0x1A at end of file) without running into another
memo.

A table named MAINT.DBF or LOANS.DBF whose first field is its numeric ID
(MAINT_ID, LOAN_ID) must hold strictly ascending IDs in record order,
deleted records included: the inventory engine appends them so and finds
a record by its ID with a binary search over the record numbers, so a
record inserted, reused or renumbered by hand is a problem.  Blank IDs
are left out of the comparison.

A LOCS.DBF with the inventory's location fields (LOC_ID, PARENT_ID,
LOC_NAME, LOC_CODE) must hold a tree, as the engine keeps it: each live
location's LOC_ID unique among the live ones, its PARENT_ID 0 or the
LOC_ID of a live location, no chain of parents looping or longer than
MAX_LOC_DEPTH (the engine's ENG_MAXLOCDEPTH), LOC_NAME unique among the
live locations of one parent (A-Z folded) and a non-blank LOC_CODE unique
among all live ones.  An ITEMS.DBF with a LOC_ID field beside such a
LOCS.DBF must place each live item at 0 or at a live location; one with
the counters ON_LOAN, RESERVED, IN_USE and ATTACHED beside a LOANS.DBF
with KIND must hold what the open loans, reservations and in-use rows
give, and no more units held than its QTY.

Memo files on their own: header block, next-free block against the file
size.

Indexes: header (root, page count, key length and type, entry size, keys
per page, unique flag, key expression) and the whole B-tree: entry
counts, page numbers, key order, that every key in an inner entry is the
highest key of the subtree it points to.  Leaves need not all be at the
same depth: dBASE's INDEX ON builds the tree bottom-up and folds a
trailing single-child level into its parent.  With --table, record
numbers are also checked against that table (in range, no record twice,
and with --strict every record present unless the index is unique).

--strict also reports what dBASE III Plus 1.1 itself never writes:
  - version 0x83 without a memo field;
  - a year byte above 99 (dBASE stores the year modulo 100);
  - a header longer than 32 + 32 * fields + 1 (dBASE writes nothing
    after the 0x0D terminator);
  - descriptor bytes 18-19 or 21-31 not zero, or byte 20 (the work area
    dBASE was using) above 10; bytes 12-15 are not checked because dBASE
    leaves a memory address there;
  - a missing 0x1A after the last record;
  - NUL bytes in C fields, lower-case or Y/N logicals;
  - a memo ended by a single 0x1A instead of 0x1A 0x1A;
  - two records pointing at the same memo, or a memo file whose size
    exceeds next-free * 512;
  - an index whose page count does not match its size, whose keys per
    page is not the maximum, with equal keys not in record-number order,
    or which lacks records of the table.

Index pages outside the tree are only noted: dBASE leaves a page that a
delete emptied in the file.  It also leaves inner pages with no key and
a single child, in slot 0.  A tree deeper than MAX_DEPTH levels is
reported once and not followed further.

Prints one line per file ("ok" or "N problem(s)" followed by the
problems, at most MAX_REPORT each) and exits with status 1 if any file
has a problem, 2 on a usage error, 3 if the checker itself failed on a
file ("internal error").
"""
import math
import os
import struct
import sys

BLOCK = 512
NDX_PAGE = 512
MAX_REPORT = 20
MAX_DEPTH = 32                  # deeper than any index of 2^32 records
NAME_CHARS = set('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_')
# dBASE's UPPER: a-z only.
UPPER = str.maketrans('abcdefghijklmnopqrstuvwxyz',
                      'ABCDEFGHIJKLMNOPQRSTUVWXYZ')
# The limits of dBASE III that DBF.H names DBF_MAXFIELDS and
# DBF_MAXRECLEN; WIDTHS holds DBF_MAXCHAR and DBF_MAXNUM.
MAX_FIELDS = 128
MAX_RECLEN = 4000
WIDTHS = {'C': (1, 254), 'N': (1, 19), 'D': (8, 8), 'L': (1, 1), 'M': (10, 10)}
# Tables whose ID (their first field) ascends in record order.
ID_ORDER = {'MAINT.DBF': 'MAINT_ID', 'LOANS.DBF': 'LOAN_ID'}
# The counters of ITEMS, one per KIND of LOANS (L R U, the order of
# HOLD_KINDS) and the count of rows naming the item as their host.
HOLD_FIELDS = ('ON_LOAN', 'RESERVED', 'IN_USE', 'ATTACHED')
HOLD_KINDS = 'LRU'
# The fields of the location tree, and its depth limit.
LOC_FIELDS = ('LOC_ID', 'PARENT_ID', 'LOC_NAME', 'LOC_CODE')
MAX_LOC_DEPTH = 8


class Report:
    """Problems found in one file."""

    def __init__(self, path):
        self.path = path
        self.problems = []
        self.notes = []

    def bad(self, msg):
        self.problems.append(msg)

    def note(self, msg):
        self.notes.append(msg)

    def lines(self):
        if not self.problems:
            out = ['%s: ok' % self.path]
            out += ['  %s' % n for n in self.notes]
            return out
        out = ['%s: %d problem(s)' % (self.path, len(self.problems))]
        out += ['  %s' % p for p in self.problems[:MAX_REPORT]]
        if len(self.problems) > MAX_REPORT:
            out.append('  ... and %d more' % (len(self.problems) - MAX_REPORT))
        return out


def digits(s):
    """True if s is one or more ASCII digits (str.isdigit also accepts
    characters such as superscript digits)."""
    return s != '' and all('0' <= c <= '9' for c in s)


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def valid_date(y, m, d):
    if not (1 <= m <= 12 and d >= 1):
        return False
    days = [31, 29 if (y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)) else 28,
            31, 30, 31, 30, 31, 31, 30, 31, 30, 31][m - 1]
    return d <= days


def sibling(path, ext):
    """The file next to path with extension ext, matched case-insensitively."""
    folder = os.path.dirname(path) or '.'
    base = os.path.splitext(os.path.basename(path))[0].upper()
    try:
        names = os.listdir(folder)
    except OSError:
        return None
    for n in names:
        if n.upper() == base + ext:
            return os.path.join(folder, n)
    return None


# ---------------------------------------------------------------------
# Field contents
# ---------------------------------------------------------------------

def check_numeric(text, width, dec):
    """None if text is a valid dBASE N field of that width, else why not."""
    if text.strip(' ') == '':
        return None
    if set(text) <= set('* '):
        return None             # dBASE fills a field with '*' on overflow
    if text != text.rstrip(' ').rjust(width):
        return 'not right-justified'
    s = text.lstrip(' ')
    if ' ' in s:
        return 'embedded blank'
    body = s[1:] if s.startswith('-') else s
    if '.' in body:
        whole, _, frac = body.partition('.')
        if dec == 0:
            return 'decimal point in a field without decimals'
        # dBASE drops decimals (keeping at least one) before it overflows,
        # so fewer than dec decimals is valid, more is not.
        if not frac or len(frac) > dec:
            return 'wrong number of decimals'
    else:
        whole, frac = body, ''
    if not digits(whole + frac):
        return 'not a number'
    return None


def check_field(rep, rec_no, f, raw, strict, memo_refs):
    name, ftype, width, dec = f['name'], f['type'], f['len'], f['dec']
    text = raw.decode('latin-1')
    where = 'record %d %s' % (rec_no, name)
    if ftype == 'C':
        if strict and b'\0' in raw:
            rep.bad('%s: NUL byte in a character field' % where)
    elif ftype == 'N':
        why = check_numeric(text, width, dec)
        if why:
            rep.bad('%s: %r %s' % (where, text, why))
    elif ftype == 'D':
        if text != ' ' * 8:
            if not digits(text) or not valid_date(int(text[:4]),
                                                    int(text[4:6]),
                                                    int(text[6:])):
                rep.bad('%s: %r is not a YYYYMMDD date' % (where, text))
    elif ftype == 'L':
        allowed = 'TF? ' if strict else 'TFYNtfyn? '
        if text not in allowed:
            rep.bad('%s: %r is not a logical' % (where, text))
    elif ftype == 'M':
        if text.strip(' ') != '':
            s = text.lstrip(' ')
            if text != s.rjust(width) or not digits(s) or int(s) == 0:
                rep.bad('%s: %r is not a memo block number' % (where, text))
            else:
                memo_refs.append((rec_no, name, int(s)))


# ---------------------------------------------------------------------
# Tables
# ---------------------------------------------------------------------

def parse_fields(rep, data, strict):
    """Field descriptors and the offset of the 0x0D terminator, or None."""
    fields = []
    seen = set()
    pos = 32
    while True:
        if pos >= len(data):
            rep.bad('no 0x0D header terminator')
            return None, None
        if data[pos] == 0x0D:
            return fields, pos
        if pos + 32 > len(data):
            rep.bad('field descriptor %d cut short' % (len(fields) + 1))
            return None, None
        d = data[pos:pos + 32]
        n = len(fields) + 1
        raw = d[:11]
        name_b = raw.split(b'\0', 1)[0]
        name = name_b.decode('latin-1')
        label = 'field %d %r' % (n, name)
        if not name:
            rep.bad('field %d: empty name' % n)
        elif len(name_b) > 10:
            rep.bad('%s: name not NUL-terminated within 11 bytes' % label)
        else:
            if any(c not in NAME_CHARS for c in name) or not name[0].isalpha():
                rep.bad('%s: name must be A-Z, 0-9, _ starting with a letter'
                        % label)
            if raw[len(name_b):] != b'\0' * (11 - len(name_b)):
                rep.bad('%s: name not NUL-padded' % label)
            if name in seen:
                rep.bad('%s: duplicate name' % label)
            seen.add(name)
        ftype = chr(d[11])
        width, dec = d[16], d[17]
        if ftype not in WIDTHS:
            rep.bad('%s: unknown type %r' % (label, ftype))
        else:
            lo, hi = WIDTHS[ftype]
            if not lo <= width <= hi:
                rep.bad('%s: width %d invalid for type %s'
                        % (label, width, ftype))
            if ftype == 'N':
                if dec and (dec > 15 or dec > width - 2):
                    rep.bad('%s: %d decimals in width %d'
                            % (label, dec, width))
            elif dec:
                rep.bad('%s: decimals %d on a %s field' % (label, dec, ftype))
        if strict:
            if d[18:20] != b'\0\0' or d[21:32] != b'\0' * 11:
                rep.bad('%s: descriptor bytes 18-19/21-31 not zero' % label)
            if d[20] > 10:
                rep.bad('%s: descriptor byte 20 (work area) is %d'
                        % (label, d[20]))
        fields.append({'name': name, 'type': ftype, 'len': width, 'dec': dec})
        pos += 32


def check_dbf(path, strict):
    rep = Report(path)
    try:
        with open(path, 'rb') as fh:
            data = fh.read()
    except OSError as e:
        rep.bad('cannot read: %s' % e)
        return rep
    if len(data) < 33:
        rep.bad('file too short for a dBASE III header (%d bytes)' % len(data))
        return rep

    version = data[0]
    if version not in (0x03, 0x83):
        rep.bad('version byte 0x%02X is not 0x03 or 0x83' % version)
        return rep
    yy, mm, dd = data[1], data[2], data[3]
    if strict and yy > 99:
        rep.bad('year byte %d above 99' % yy)
    year = 1900 + yy if yy >= 80 else 2000 + yy
    if not valid_date(year, mm, dd):
        rep.bad('last-update date %02d-%02d-%02d invalid' % (yy, mm, dd))
    count = u32(data, 4)
    hdrlen = u16(data, 8)
    reclen = u16(data, 10)
    if strict and data[12:32] != b'\0' * 20:
        rep.bad('header bytes 12-31 not zero')

    fields, term = parse_fields(rep, data, strict)
    if fields is None:
        return rep
    if not 1 <= len(fields) <= MAX_FIELDS:
        rep.bad('%d fields (1..%d allowed)' % (len(fields), MAX_FIELDS))
    want_hdr = 32 + 32 * len(fields) + 1
    if hdrlen < want_hdr:
        rep.bad('header length %d < %d for %d fields'
                % (hdrlen, want_hdr, len(fields)))
        return rep
    if strict and hdrlen > want_hdr:
        rep.bad('header length %d, dBASE writes %d for %d fields'
                % (hdrlen, want_hdr, len(fields)))
    has_memo = any(f['type'] == 'M' for f in fields)
    if has_memo and version != 0x83:
        rep.bad('memo field in a table with version 0x03')
    if strict and not has_memo and version == 0x83:
        rep.bad('version 0x83 without a memo field')
    want_rec = 1 + sum(f['len'] for f in fields)
    if reclen != want_rec:
        rep.bad('record length %d, fields add up to %d' % (reclen, want_rec))
        return rep
    if reclen > MAX_RECLEN:
        rep.bad('record length %d over %d' % (reclen, MAX_RECLEN))

    end = hdrlen + count * reclen
    if len(data) < end:
        rep.bad('file is %d bytes, header and %d records need %d'
                % (len(data), count, end))
        count = max(0, (len(data) - hdrlen) // reclen)
        end = hdrlen + count * reclen
    elif len(data) == end:
        if strict:
            rep.bad('no 0x1A after the last record')
    elif len(data) == end + 1:
        if data[end] != 0x1A:
            rep.bad('byte after the last record is 0x%02X, not 0x1A'
                    % data[end])
    else:
        rep.bad('%d bytes after the last record (expected 0x1A only)'
                % (len(data) - end))

    memo_refs = []
    deleted = 0
    idname = ID_ORDER.get(os.path.basename(path).upper())
    if idname is not None and (fields[0]['name'] != idname or
                               fields[0]['type'] != 'N'):
        idname = None
    last_id = None
    for r in range(count):
        rec = data[hdrlen + r * reclen:hdrlen + (r + 1) * reclen]
        flag = rec[0:1]
        if flag == b'*':
            deleted += 1
        elif flag != b' ':
            rep.bad('record %d: delete flag 0x%02X' % (r + 1, rec[0]))
        pos = 1
        for f in fields:
            check_field(rep, r + 1, f, rec[pos:pos + f['len']], strict,
                        memo_refs)
            pos += f['len']
        if idname is not None:
            text = rec[1:1 + fields[0]['len']].decode('latin-1').strip(' ')
            if digits(text):
                if last_id is not None and int(text) <= last_id[0]:
                    rep.bad('record %d %s: %s not above %d of record %d'
                            % (r + 1, idname, text, last_id[0], last_id[1]))
                last_id = (int(text), r + 1)

    if has_memo:
        dbt = sibling(path, '.DBT')
        if dbt is None:
            rep.bad('memo field but no .DBT beside the table')
        else:
            check_memo_refs(rep, dbt, memo_refs, strict)
    base = os.path.basename(path).upper()
    rows = live_rows(data, hdrlen, reclen, count, fields)
    if base == 'LOCS.DBF' and all(n in rows.names for n in LOC_FIELDS):
        check_tree(rep, rows)
    elif base == 'ITEMS.DBF' and 'LOC_ID' in rows.names:
        check_item_locs(rep, path, rows)
        if all(n in rows.names for n in HOLD_FIELDS):
            check_item_holds(rep, path, rows)
    rep.note('version 0x%02X, %d records (%d deleted), %d fields, '
             'record %d bytes'
             % (version, count, deleted, len(fields), reclen))
    return rep


class Rows:
    """The live records of a table: names, the field names, and each
    record as its number and a dict of its field texts without trailing
    blanks (leading ones count, as in the engine's keys)."""

    def __init__(self, names, recs):
        self.names = names
        self.recs = recs


def live_rows(data, hdrlen, reclen, count, fields):
    names = [f['name'] for f in fields]
    recs = []
    for r in range(count):
        rec = data[hdrlen + r * reclen:hdrlen + (r + 1) * reclen]
        if rec[0:1] != b' ':
            continue
        vals = {}
        pos = 1
        for f in fields:
            text = rec[pos:pos + f['len']].decode('latin-1')
            vals[f['name']] = text.rstrip()
            pos += f['len']
        recs.append((r + 1, vals))
    return Rows(names, recs)


def number(text):
    """The value of an N field's text, 0 when blank, None when it is not
    a whole number."""
    text = text.strip()
    if text == '':
        return 0
    return int(text) if digits(text.lstrip('-')) else None


def check_tree(rep, rows):
    """The rules of the location tree over the live LOCS records."""
    parent = {}
    names = {}
    codes = {}
    for r, v in rows.recs:
        lid = number(v['LOC_ID'])
        par = number(v['PARENT_ID'])
        if not lid or lid < 0 or lid in parent:
            rep.bad('record %d: LOC_ID %r missing or not unique'
                    % (r, v['LOC_ID'].strip()))
            continue
        parent[lid] = (par, r)
        key = (par, v['LOC_NAME'].translate(UPPER))
        if key in names:
            rep.bad('record %d: LOC_NAME %r also in record %d under the '
                    'same parent' % (r, v['LOC_NAME'], names[key]))
        names[key] = r
        code = v['LOC_CODE'].upper()
        if code:
            if code in codes:
                rep.bad('record %d: LOC_CODE %s also in record %d'
                        % (r, code, codes[code]))
            codes[code] = r
    for lid, (par, r) in parent.items():
        depth = 1
        while par and par in parent and depth <= MAX_LOC_DEPTH:
            par = parent[par][0]
            depth += 1
        if par is None or (par and par not in parent):
            rep.bad('record %d: PARENT_ID names no live location' % r)
        elif depth > MAX_LOC_DEPTH:
            rep.bad('record %d: parents loop or pass depth %d'
                    % (r, MAX_LOC_DEPTH))


def sibling_rows(path, name, field):
    """The live rows of the table name beside path when it exists and has
    the field field, else None."""
    folder = os.path.dirname(path) or '.'
    found = [os.path.join(folder, n) for n in os.listdir(folder)
             if n.upper() == name]
    if not found:
        return None
    with open(found[0], 'rb') as fh:
        data = fh.read()
    fields, _ = parse_fields(Report(found[0]), data, False)
    if fields is None or field not in [f['name'] for f in fields]:
        return None
    return live_rows(data, u16(data, 8), u16(data, 10), u32(data, 4), fields)


def check_item_locs(rep, path, rows):
    """Each live item at 0 or at a live location of the LOCS.DBF beside
    path, when there is one with the location fields."""
    locs = sibling_rows(path, 'LOCS.DBF', 'LOC_ID')
    if locs is None:
        return
    live = set(number(v['LOC_ID']) for _, v in locs.recs)
    for r, v in rows.recs:
        lid = number(v['LOC_ID'])
        if lid != 0 and lid not in live:
            rep.bad('record %d: LOC_ID %r names no live location'
                    % (r, v['LOC_ID'].strip()))


def check_item_holds(rep, path, rows):
    """Each live item's counters what the open rows of the LOANS.DBF
    beside path give, when there is one with KIND: ON_LOAN, RESERVED and
    IN_USE the QTY of its open rows of each kind, ATTACHED the number of
    open R and U rows naming it as HOST_ID; and the units they hold no
    more than its QTY."""
    loans = sibling_rows(path, 'LOANS.DBF', 'KIND')
    if loans is None:
        return
    want = {}
    for _, v in loans.recs:
        if v['RET_DATE'].strip():
            continue
        kind = HOLD_KINDS.find(v['KIND']) if v['KIND'] else -1
        held = want.setdefault(number(v['ITEM_ID']), [0] * 4)
        qty = number(v['QTY']) or 0
        held[max(kind, 0)] += qty if qty > 0 else 0
        if kind > 0 and number(v['HOST_ID']):
            want.setdefault(number(v['HOST_ID']), [0] * 4)[3] += 1
    for r, v in rows.recs:
        got = [number(v[n]) for n in HOLD_FIELDS]
        exp = want.get(number(v['ITEM_ID']), [0] * 4)
        for n, g, w in zip(HOLD_FIELDS, got, exp):
            if g != w:
                rep.bad('record %d: %s %s, its open loans give %d'
                        % (r, n, v[n].strip() or 'blank', w))
        if None not in got and sum(got[:3]) > (number(v['QTY']) or 0):
            rep.bad('record %d: more units held than its QTY' % r)


# ---------------------------------------------------------------------
# Memo files
# ---------------------------------------------------------------------

def read_dbt(rep, path, strict):
    """(data, next_free) after the header checks, or (None, None)."""
    try:
        with open(path, 'rb') as fh:
            data = fh.read()
    except OSError as e:
        rep.bad('%s: cannot read: %s' % (path, e))
        return None, None
    if len(data) < BLOCK:
        rep.bad('%s: %d bytes, shorter than the %d-byte header block'
                % (path, len(data), BLOCK))
        return None, None
    nxt = u32(data, 0)
    if nxt < 1:
        rep.bad('%s: next free block is 0' % path)
        return None, None
    # dBASE does not pad the last memo, so the file may end inside the
    # block before next-free; it must not reach past it, and it must reach
    # into that block unless only the header exists.
    if len(data) > nxt * BLOCK:
        msg = ('%s: %d bytes, beyond next free block %d'
               % (path, len(data), nxt))
        if strict:
            rep.bad(msg)
        else:
            rep.note(msg)
    if nxt > 1 and len(data) <= (nxt - 1) * BLOCK:
        rep.bad('%s: %d bytes, next free block %d says block %d is in use'
                % (path, len(data), nxt, nxt - 1))
    return data, nxt


def memo_end(data, start):
    """(offset just past the memo starting at start, terminator included;
    True if the terminator is the 0x1A 0x1A pair dBASE writes), or
    (None, False) if there is no 0x1A before end of file.  dBASE III
    stops reading a memo at its first 0x1A, so one is enough for it."""
    i = data.find(b'\x1a', start)
    if i < 0:
        return None, False
    if i + 1 < len(data) and data[i + 1] == 0x1A:
        return i + 2, True
    return i + 1, False


def check_memo_refs(rep, path, refs, strict):
    data, nxt = read_dbt(rep, path, strict)
    if data is None:
        return
    spans = []
    for rec_no, name, block in refs:
        where = 'record %d %s -> block %d' % (rec_no, name, block)
        if block >= nxt:
            rep.bad('%s: at or past next free block %d' % (where, nxt))
            continue
        start = block * BLOCK
        if start >= len(data):
            rep.bad('%s: past the end of %s' % (where, os.path.basename(path)))
            continue
        end, pair = memo_end(data, start)
        if end is None:
            rep.bad('%s: no 0x1A terminator before end of file' % where)
            continue
        if strict and not pair:
            rep.bad('%s: memo ends with a single 0x1A, not 0x1A 0x1A' % where)
        if end > nxt * BLOCK:
            rep.bad('%s: memo runs past next free block %d' % (where, nxt))
        spans.append((block, (end + BLOCK - 1) // BLOCK, rec_no, name))
    spans.sort()
    for a, b in zip(spans, spans[1:]):
        if b[0] < a[1]:
            if b[0] == a[0]:
                if strict:
                    rep.bad('records %d and %d share memo block %d'
                            % (a[2], b[2], a[0]))
            else:
                rep.bad('memo of record %d (block %d) runs into the memo of '
                        'record %d (block %d)' % (a[2], a[0], b[2], b[0]))


def check_dbt(path, strict):
    rep = Report(path)
    data, nxt = read_dbt(rep, path, strict)
    if data is not None:
        rep.note('next free block %d, %d bytes' % (nxt, len(data)))
    return rep


# ---------------------------------------------------------------------
# Indexes
# ---------------------------------------------------------------------

def table_count(path):
    with open(path, 'rb') as fh:
        head = fh.read(8)
    if len(head) < 8 or head[0] not in (0x03, 0x83):
        raise ValueError('not a dBASE III table')
    return u32(head, 4)


def check_ndx(path, strict, table):
    rep = Report(path)
    try:
        with open(path, 'rb') as fh:
            data = fh.read()
    except OSError as e:
        rep.bad('cannot read: %s' % e)
        return rep
    if len(data) < NDX_PAGE or len(data) % NDX_PAGE:
        rep.bad('size %d is not a whole number of %d-byte pages'
                % (len(data), NDX_PAGE))
        if len(data) < NDX_PAGE:
            return rep
    pages = len(data) // NDX_PAGE
    root, npages = u32(data, 0), u32(data, 4)
    keylen, kpp, ktype, esize = (u16(data, 12), u16(data, 14), u16(data, 16),
                                 u16(data, 18))
    unique = data[23]
    nul = data.find(b'\0', 24, NDX_PAGE)
    if nul < 0:
        rep.bad('key expression not NUL-terminated in the header')
        expr = ''
    else:
        expr = data[24:nul].decode('latin-1')
        if not expr.strip() or any(not 32 <= ord(c) < 127 for c in expr):
            rep.bad('key expression %r empty or not printable' % expr)
    if npages > pages or npages < 2:
        rep.bad('page count %d, file holds %d pages' % (npages, pages))
        return rep
    if strict and npages != pages:
        rep.bad('page count %d, file holds %d pages' % (npages, pages))
    if not 1 <= root < npages:
        rep.bad('root page %d outside 1..%d' % (root, npages - 1))
        return rep
    if ktype not in (0, 1):
        rep.bad('key type %d (0 character, 1 numeric)' % ktype)
        return rep
    if ktype == 1 and keylen != 8:
        rep.bad('numeric key of length %d (8 expected)' % keylen)
        return rep
    if not 1 <= keylen <= 100:
        rep.bad('key length %d outside 1..100' % keylen)
        return rep
    want_es = (8 + keylen + 3) // 4 * 4
    if esize != want_es:
        rep.bad('entry size %d, %d expected for key length %d'
                % (esize, want_es, keylen))
        return rep
    maxk = (NDX_PAGE - 4) // esize
    if not 1 <= kpp <= maxk:
        rep.bad('keys per page %d outside 1..%d' % (kpp, maxk))
        return rep
    if strict and kpp != maxk:
        rep.bad('keys per page %d, dBASE uses %d' % (kpp, maxk))
    if unique not in (0, 1):
        rep.bad('unique flag %d' % unique)
    if strict and data[8:12] != b'\0' * 4:
        rep.bad('header bytes 8-11 not zero')
    if strict and data[20:23] != b'\0' * 3:
        rep.bad('header bytes 20-22 not zero')

    def key(raw):
        if ktype == 1:
            return struct.unpack('<d', raw)[0]
        return raw

    recnos = []
    depth = None
    stopped = False             # set when the depth limit ends the walk
    visited = set()

    def walk(page, level, lo):
        """Check a subtree; return its highest key (None if empty)."""
        nonlocal depth, stopped
        if stopped:
            return None
        if level >= MAX_DEPTH:
            rep.bad('page %d: tree deeper than %d levels; not followed further'
                    % (page, MAX_DEPTH))
            stopped = True
            return None
        if page in visited:
            rep.bad('page %d reached twice' % page)
            return None
        visited.add(page)
        if len(rep.problems) > MAX_REPORT:
            return None
        base = page * NDX_PAGE
        n = u16(data, base)
        if n > kpp:
            rep.bad('page %d: %d entries, at most %d' % (page, n, kpp))
            return None
        ents = []
        for i in range(n):
            o = base + 4 + i * esize
            ents.append((u32(data, o), u32(data, o + 4),
                         key(data[o + 8:o + 8 + keylen])))
        # An inner page normally has a child page in its first entry.  A
        # delete in dBASE can leave an inner page with 0 keys whose only
        # child sits in slot 0; that page is inner too.
        if n > 0:
            inner = ents[0][0] != 0
        else:
            inner = u32(data, base + 4) != 0
        prev = lo
        top = None
        if inner:
            # An inner page holds up to kpp keys followed by one more
            # child page number (the rightmost child).  That 4-byte slot
            # starts at 4 + n * esize, at most 508, so it always fits.
            for i in range(n):
                child, rec, k = ents[i]
                if ktype == 1 and math.isnan(k):
                    rep.bad('page %d entry %d: NaN key' % (page, i))
                if not 1 <= child < npages:
                    rep.bad('page %d entry %d: child page %d'
                            % (page, i, child))
                    return None
                ctop = walk(child, level + 1, prev)
                if stopped:
                    return None
                if ctop is not None and ctop != k:
                    rep.bad('page %d entry %d: key %r is not the highest key '
                            '%r of page %d' % (page, i, k, ctop, child))
                if prev is not None and k < prev:
                    rep.bad('page %d entry %d: keys out of order' % (page, i))
                prev = k
            last = u32(data, base + 4 + n * esize)
            if not 1 <= last < npages:
                rep.bad('page %d: last child page %d' % (page, last))
                return None
            top = walk(last, level + 1, prev)
            if stopped:
                return None
            if top is None:
                top = prev
        else:
            prev_rec = None
            # dBASE's INDEX ON writes pages bottom-up and folds a trailing
            # single-child level into its parent, so leaves may sit at
            # different depths; only the deepest is recorded.
            if depth is None or level > depth:
                depth = level
            for i in range(n):
                child, rec, k = ents[i]
                if child != 0:
                    rep.bad('leaf page %d entry %d: child page %d'
                            % (page, i, child))
                if rec == 0:
                    rep.bad('leaf page %d entry %d: record number 0'
                            % (page, i))
                if ktype == 1 and math.isnan(k):
                    rep.bad('leaf page %d entry %d: NaN key' % (page, i))
                if prev is not None and k < prev:
                    rep.bad('leaf page %d entry %d: key %r below %r'
                            % (page, i, k, prev))
                elif (strict and prev is not None and k == prev
                      and prev_rec is not None and rec < prev_rec):
                    rep.bad('leaf page %d entry %d: equal keys not in record '
                            'order (%d after %d)' % (page, i, rec, prev_rec))
                prev = k
                prev_rec = rec
                recnos.append(rec)
                top = k
        return top

    walk(root, 0, None)
    # dBASE never reuses an index page: a delete that empties a page
    # leaves it in the file but out of the tree.  Such pages are noted,
    # not counted as problems.
    if not stopped and len(rep.problems) <= MAX_REPORT:
        lost = [p for p in range(1, npages) if p not in visited]
        if lost:
            rep.note('%d page(s) not in the tree (left by deletes), first %d'
                     % (len(lost), lost[0]))

    seen = set()
    dup = []
    for r in recnos:
        if r in seen:
            dup.append(r)
        seen.add(r)
    if dup:
        rep.bad('record %d indexed more than once' % dup[0])
    if table:
        try:
            count = table_count(table)
        except (OSError, ValueError) as e:
            rep.bad('cannot read table %s: %s' % (table, e))
        else:
            out = [r for r in recnos if r > count]
            if out:
                rep.bad('record %d indexed, table %s has %d'
                        % (out[0], os.path.basename(table), count))
            if strict and not unique and len(seen) != count:
                rep.bad('%d records indexed, table %s has %d'
                        % (len(seen), os.path.basename(table), count))
    rep.note('key %r, %s, length %d, %d keys, %d pages, depth %d%s'
             % (expr, 'numeric' if ktype else 'character', keylen, len(recnos),
                npages, (depth or 0) + 1, ', unique' if unique else ''))
    return rep


# ---------------------------------------------------------------------

def usage(reason):
    """Write reason and the usage line on standard error; return 2."""
    sys.stderr.write('chkdbf: %s\n%s\n' % (reason, __doc__.split('\n\n')[1]))
    return 2


def main(argv):
    strict = False
    quiet = False
    table = None
    files = []
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == '--strict':
            strict = True
        elif a == '--quiet':
            quiet = True
        elif a == '--table':
            if not args:
                return usage('--table without DBF')
            table = args.pop(0)
        elif a in ('-h', '--help'):
            sys.stdout.write(__doc__)
            return 0
        elif a.startswith('--'):
            return usage('unknown option %s' % a)
        else:
            files.append(a)
    if not files:
        return usage('no FILE')
    failed = False
    crashed = False
    for f in files:
        ext = os.path.splitext(f)[1].upper()
        try:
            if ext == '.DBF':
                rep = check_dbf(f, strict)
            elif ext == '.DBT':
                rep = check_dbt(f, strict)
            elif ext == '.NDX':
                rep = check_ndx(f, strict, table)
            else:
                rep = Report(f)
                rep.bad('unknown extension (expected .DBF, .DBT or .NDX)')
        except Exception as e:  # a checker bug must not read as a verdict
            crashed = True
            sys.stdout.write('%s: internal error: %s: %s\n'
                             % (f, type(e).__name__, e))
            continue
        failed = failed or bool(rep.problems)
        lines = rep.lines()
        if quiet and not rep.problems:
            lines = lines[:1]
        sys.stdout.write('\n'.join(lines) + '\n')
    if crashed:
        return 3
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
