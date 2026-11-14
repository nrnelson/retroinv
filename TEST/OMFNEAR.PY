"""OMFNEAR.PY - check the near data of each OMF object file against its budget.

usage: python3 -I TEST/OMFNEAR.PY [-q | -v] [--max-near FILE] BUDGETS OBJ...

BUDGETS is a budget file such as TEST/NEARBUD.TXT; each OBJ is an object
file or a directory searched recursively for *.OBJ.  The same reader serves
Open Watcom, Watcom C 6.5 and Turbo C 2.01 objects, which all write the
Intel/Microsoft OMF format.

An object's near data is what it adds to DGROUP, the stack excluded:

- every segment it defines (SEGDEF) whose name a GRPDEF record places in
  DGROUP, unless its class is STACK.  Membership is collected over all the
  objects given, as the linker collects it, so a segment that one object
  defines and another groups still counts.  Segments of the classes
  DATA, BSS, CONST, FAR_DATA and FAR_BSS that no object places in DGROUP
  are far data; -v lists them, and they are not counted.
- every near communal variable (a COMDEF or LCOMDEF entry, which a
  compiler may write for a tentative definition such as "int x;" at file
  scope): data type 62h, or a Borland segment index naming a DGROUP
  segment.  Far communals (61h) are not counted.  Open Watcom, Watcom C
  6.5 and Turbo C 2.01 place tentative definitions in _BSS instead,
  which the first rule counts.
  The linker allocates a public communal once however many objects
  declare it, so in the total each name counts once, at its largest size;
  each object's own figure includes it in full.

The object's module name is its file name without the extension.  Each
module with a budget line must be within its budget; a module without one
is a problem, reported and left out of the total.  The total of the
budgeted modules measured plus the fixed "LIB" allowance must be within
MAX_NEAR, and so must the sum of all budgets.  MAX_NEAR is read from the
"MAX_NEAR=" line of FILE, by default mapchk.sh next to this script.  -v
lists the counted segments and communals of every object, and its far
ones; -q prints only the problems, and nothing when everything fits.

Each file must be a single object: it starts with a THEADR or LHEADR
record and ends with MODEND, with no bytes after it; every record's
checksum is 0 or makes the record's bytes sum to 0 modulo 256, and every
name and segment index is in range.  Libraries, COMDAT (C2h, C3h) and
its line-number records LINSYM (C4h, C5h), and LLNAMES (CAh) are not
handled: such a file is refused.

Budget file: one "NAME BYTES" pair per line; "#" starts a comment, blank
lines are ignored.  NAME is a module name, or LIB for the near data of the
C library, which no object here holds and which is only added to the total.

Exit status: 0 when everything fits, 1 when a module is over its budget
or has none, or a total is over MAX_NEAR, 2 on a usage, read or format
error.
"""
import os
import re
import sys

T_THEADR = 0x80
T_LHEADR = 0x82
T_MODEND = 0x8A
T_MODEND32 = 0x8B
T_LNAMES = 0x96
T_SEGDEF = 0x98
T_SEGDEF32 = 0x99
T_GRPDEF = 0x9A
T_COMDEF = 0xB0
T_LCOMDEF = 0xB8
T_LIBHDR = 0xF0
# Records this reader does not handle: COMDAT (C2h, C3h) and its
# line-number records LINSYM (C4h, C5h), and LLNAMES (CAh), which would
# shift the name indexes.  An object holding any of them is refused.
UNHANDLED = {0xC2: 'COMDAT', 0xC3: 'COMDAT', 0xC4: 'LINSYM', 0xC5: 'LINSYM',
             0xCA: 'LLNAMES'}

COM_FAR = 0x61
COM_NEAR = 0x62

FAR_CLASSES = ('DATA', 'BSS', 'CONST', 'FAR_DATA', 'FAR_BSS')


class OmfError(Exception):
    pass


class Record:
    """The contents of one record (checksum excluded) and a read position;
    every read past the end raises OmfError."""

    def __init__(self, rtype, body):
        self.rtype = rtype
        self.body = body
        self.i = 0

    def more(self):
        return self.i < len(self.body)

    def take(self, n):
        if self.i + n > len(self.body):
            raise OmfError('record %02Xh ends early' % self.rtype)
        b = self.body[self.i:self.i + n]
        self.i += n
        return b

    def byte(self):
        return self.take(1)[0]

    def word(self, width):
        return int.from_bytes(self.take(width), 'little')

    def index(self):
        """A one- or two-byte OMF index."""
        v = self.byte()
        if v & 0x80:
            v = ((v & 0x7F) << 8) | self.byte()
        return v

    def name(self):
        n = self.byte()
        return self.take(n).decode('latin-1')

    def com_length(self):
        """A communal length: one byte up to 80h, or 81h, 84h or 88h
        followed by a 2-, 3- or 4-byte little-endian value."""
        b = self.byte()
        if b <= 0x80:
            return b
        width = {0x81: 2, 0x84: 3, 0x88: 4}.get(b)
        if width is None:
            raise OmfError('bad communal length byte %02Xh' % b)
        return self.word(width)


def lookup(table, idx, what, rtype):
    """table[idx - 1] for a 1-based OMF index, refusing 0 and indexes past
    the end."""
    if idx < 1 or idx > len(table):
        raise OmfError('record %02Xh: %s index %d out of range (1..%d)'
                       % (rtype, what, idx, len(table)))
    return table[idx - 1]


def records(data):
    """Yield each record of an object file as a Record, checking its
    length and checksum, the header record at the start and the MODEND
    record at the end with nothing after it.  WC65/chklib.py walks the
    modules of a library by the same length and checksum rules."""
    p = 0
    first = True
    while True:
        if p + 3 > len(data):
            raise OmfError('no MODEND record (file ends at offset %d)' % p)
        rtype = data[p]
        rlen = data[p + 1] | data[p + 2] << 8
        if first and rtype not in (T_THEADR, T_LHEADR):
            if rtype == T_LIBHDR:
                raise OmfError('a library, not an object file')
            raise OmfError('not an OMF object (first record %02Xh)' % rtype)
        first = False
        if rlen == 0 or p + 3 + rlen > len(data):
            raise OmfError('record %02Xh at offset %d is truncated'
                           % (rtype, p))
        raw = data[p:p + 3 + rlen]
        if raw[-1] != 0 and sum(raw) & 0xFF:
            raise OmfError('record %02Xh at offset %d: bad checksum'
                           % (rtype, p))
        if rtype in UNHANDLED:
            raise OmfError('record %02Xh (%s) at offset %d is not handled'
                           % (rtype, UNHANDLED[rtype], p))
        p += 3 + rlen
        yield Record(rtype, raw[3:-1])
        if rtype in (T_MODEND, T_MODEND32):
            if p != len(data):
                raise OmfError('%d bytes after the MODEND record'
                               % (len(data) - p))
            return


def read_object(path):
    """Return (segments, groups, communals) of one object file.

    segments: list of (name, class, length) in SEGDEF order;
    groups: dict group name -> set of segment names;
    communals: list of (name, kind, length, segname, local) with kind
    'near' or 'far' (for a Borland segment-index communal, 'seg').
    Errors are raised as OmfError naming the file."""
    try:
        with open(path, 'rb') as f:
            data = f.read()
        return parse_object(data)
    except OSError as e:
        raise OmfError('%s: %s' % (path, e.strerror or e))
    except OmfError as e:
        raise OmfError('%s: %s' % (path, e))


def parse_object(data):
    names = []
    segs = []
    groups = {}
    coms = []
    for rec in records(data):
        rt = rec.rtype
        if rt == T_LNAMES:
            while rec.more():
                names.append(rec.name().upper())
        elif rt in (T_SEGDEF, T_SEGDEF32):
            acbp = rec.byte()
            if acbp >> 5 == 0:
                rec.take(3)
            width = 2 if rt == T_SEGDEF else 4
            length = rec.word(width)
            if acbp & 0x02:
                length = 1 << (8 * width)
            sname = lookup(names, rec.index(), 'segment name', rt)
            cname = lookup(names, rec.index(), 'class name', rt)
            segs.append((sname, cname, length))
        elif rt == T_GRPDEF:
            gname = lookup(names, rec.index(), 'group name', rt)
            members = groups.setdefault(gname, set())
            while rec.more():
                kind = rec.byte()
                if kind != 0xFF:
                    raise OmfError('unsupported GRPDEF entry %02Xh' % kind)
                members.add(lookup(segs, rec.index(), 'segment', rt)[0])
        elif rt in (T_COMDEF, T_LCOMDEF):
            local = rt == T_LCOMDEF
            while rec.more():
                cname = rec.name()
                rec.index()
                dtype = rec.byte()
                if dtype == COM_FAR:
                    count = rec.com_length()
                    size = rec.com_length()
                    coms.append((cname, 'far', count * size, '', local))
                elif dtype == COM_NEAR:
                    coms.append((cname, 'near', rec.com_length(), '', local))
                elif 1 <= dtype <= 0x5F:
                    seg = lookup(segs, dtype, 'communal segment', rt)[0]
                    coms.append((cname, 'seg', rec.com_length(), seg, local))
                else:
                    raise OmfError('unknown communal data type %02Xh' % dtype)
    return segs, groups, coms


def read_budgets(path):
    budgets = {}
    with open(path) as f:
        for num, line in enumerate(f, 1):
            words = line.split('#', 1)[0].split()
            if not words:
                continue
            if len(words) != 2 or not words[1].isdigit():
                raise OmfError('%s:%d: expected "NAME BYTES"' % (path, num))
            name = words[0].upper()
            if name in budgets:
                raise OmfError('%s:%d: %s listed twice' % (path, num, name))
            budgets[name] = int(words[1])
    return budgets


def read_max_near(path):
    """The value of the "MAX_NEAR=" line of the file at path."""
    with open(path) as f:
        m = re.search(r'^MAX_NEAR=(\d+)\s*$', f.read(), re.M)
    if not m:
        raise OmfError('MAX_NEAR not found in %s' % os.path.basename(path))
    return int(m.group(1))


def object_files(args):
    found = []
    for a in args:
        if os.path.isdir(a):
            for root, dirs, files in os.walk(a):
                dirs.sort()
                for fn in sorted(files):
                    if fn.upper().endswith('.OBJ'):
                        found.append(os.path.join(root, fn))
        elif os.path.isfile(a):
            found.append(a)
        else:
            raise OmfError('%s: no such file or directory' % a)
    return found


def main(argv):
    verbose = quiet = False
    limits = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          'mapchk.sh')
    if argv and argv[0] in ('-v', '-q'):
        verbose = argv[0] == '-v'
        quiet = not verbose
        argv = argv[1:]
    if len(argv) > 1 and argv[0] == '--max-near':
        limits = argv[1]
        argv = argv[2:]
    if len(argv) < 2:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    try:
        budgets = read_budgets(argv[0])
        max_near = read_max_near(limits)
        paths = object_files(argv[1:])
        objs = [(p, read_object(p)) for p in paths]
    except (OSError, OmfError) as e:
        sys.stderr.write('omfnear: %s\n' % e)
        return 2
    if not objs:
        sys.stderr.write('omfnear: no object files found\n')
        return 2

    dgroup = set()
    for _, (_, groups, _) in objs:
        dgroup |= groups.get('DGROUP', set())

    bad = False
    total = 0
    seen = {}
    publics = {}
    over = []
    unbudgeted = []
    for path, (segs, _, coms) in objs:
        module = os.path.splitext(os.path.basename(path))[0].upper()
        if module in seen:
            sys.stderr.write('omfnear: module %s in both %s and %s\n'
                             % (module, seen[module], path))
            return 2
        seen[module] = path
        budget = None if module == 'LIB' else budgets.get(module)
        near = 0
        detail = []
        for name, cls, length in segs:
            if name in dgroup and cls != 'STACK':
                near += length
                if length:
                    detail.append('%s (class %s) %d' % (name, cls, length))
            elif cls in FAR_CLASSES and length:
                detail.append('far: %s (class %s) %d' % (name, cls, length))
        for name, kind, length, segname, local in coms:
            rec = 'LCOMDEF' if local else 'COMDEF'
            if kind == 'near' or (kind == 'seg' and segname in dgroup):
                near += length
                detail.append('%s %s %d' % (rec, name, length))
                if budget is None:
                    continue
                if local:
                    total += length
                else:
                    publics[name] = max(publics.get(name, 0), length)
            else:
                detail.append('far: %s %s %d' % (rec, name, length))
        if budget is None:
            state = 'no budget, not in the total'
            unbudgeted.append(module)
        else:
            total += near - sum(l for n, k, l, sg, loc in coms
                                if k == 'near' or
                                (k == 'seg' and sg in dgroup))
            if near > budget:
                state = 'OVER BUDGET by %d' % (near - budget)
                over.append(module)
            else:
                state = 'ok'
        if not quiet or state != 'ok':
            print('%-40s %6d / %-6s %s' % (path, near,
                                           '-' if budget is None else budget,
                                           state))
        if verbose:
            for d in detail:
                print('    %s' % d)
    total += sum(publics.values())
    if over:
        print('omfnear: over budget: %s' % ' '.join(over))
        bad = True
    if unbudgeted:
        print('omfnear: no budget: %s' % ' '.join(unbudgeted))
        bad = True
    lib = budgets.get('LIB', 0)
    missing = sorted(m for m in budgets if m != 'LIB' and m not in seen)
    if missing and not quiet:
        print('budgeted modules not found: %s' % ' '.join(missing))
    if not quiet or total + lib > max_near:
        print('modules %d + LIB allowance %d = %d bytes of near data '
              '(limit %d)' % (total, lib, total + lib, max_near))
    if total + lib > max_near:
        print('omfnear: near data total over MAX_NEAR by %d bytes'
              % (total + lib - max_near))
        bad = True
    planned = sum(budgets.values())
    if not quiet or planned > max_near:
        print('sum of all budgets %d bytes (limit %d)' % (planned, max_near))
    if planned > max_near:
        print('omfnear: the budgets add up to more than MAX_NEAR')
        bad = True
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
