"""msgchk.py - check the message sources, build message files and MSGSTMP.H.

usage: python3 -I TEST/msgchk.py [--header MSGSTMP.H] [--names MSGNUM.H]
           [[--consts DIR] FILE.MSG...]...
           [--out FILE [[--consts DIR] FILE.MSG...]...]...

Each FILE.MSG is the message source of one client module, or the
server's texts (SRC/SERVER/INVSERV.MSG), in the format
SRC/CLIENT/MSG.H describes: comment lines starting with '#', blank lines,
and lines "nnn NAME text", NAME being the message's constant in full
(e.g. MT_NOTAVAIL).  The module is the file's base name; its constants
are the "#define <prefix>NAME nnn" lines of the module's C files
(<module>.C, or SRVSCR.C and SRVRUN.C for INVSERV.MSG, and TUI.C, which
matches the key names, for KEYSCR.MSG and shows the Record menu's verbs,
for SCRVERB.MSG) and of every header in the same
directory, with the module's prefix from PREFIX below; its number range
is the module's MSG_R_ constant in MSG.H up to the next one.
Checked:

  - every constant of a module has exactly one line in the module's file
    with the same name and number, inside the module's range, and every
    line has its constant;
  - no number and no name occurs twice, in all the files together, and
    no constant is defined twice;
  - numbers are 1..MSG_MAXNUM - 1, written without leading zeros; names
    are A-Z, 0-9 and _;
  - the source is printable ASCII (20h..7Eh); the only escapes are \\n,
    \\\\ and \\xHH (HH not 00, 01 or 02, which the loader keeps for
    itself), and the only '^' sequences ^1, ^2 and ^^;
  - a line, as the built file holds it (without the name), is at most
    MSG_MAXRAW bytes, and its text after unescaping at most MSG_MAXLEN;
  - a range from MSG_PAGEMIN up to MSG_PAGEEND, which MSG.C reads as a
    page, fits MSG_PAGE bytes: two per number from the range's first to
    its highest with a text, plus every text unescaped with its NUL;
  - a key hint (a name with HINT after its prefix, e.g. MI_HINTLIST,
    MG_HOMEHINT) fits the hint row: its text is at most TUI_HINTBUF - 1
    bytes, at most TUI_HINTLVL of its words start with a level digit
    1..3, and without those digits it is at most VID_COLS - 1 cells
    wide (the row's first cell stays blank).  The three constants are
    read from TUI.H, TUI.C and ../COMPAT/COMPAT.H beside the message
    files, where each, and each constant it is defined from, must have
    one #define outside any conditional block (exit status 2 if not);
  - the msg_rlo table of MSG.C, beside MSG.H, names every MSG_R_
    constant once, in the order of their values, which MSG.C's range
    lookup relies on.

The stamp is a CRC-16 (polynomial 1021h, initial value FFFFh, as in
CCITT X.25 framing without the final inversion) over FORMAT, then one
line "number NAME ones twos\\n" per message in the order of their
numbers, ones and twos being how many ^1 and how many ^2 the text holds.
It changes when a message is added, removed, renumbered, renamed or
gains or loses a placeholder, or when the built files' format and so
FORMAT changes, and not when only its wording changes.

The constants a source is checked against (MSG.H with MSG.C, the
module's C file, the headers, the hint row's) are those of its own
directory, except that the sources after --consts DIR, up to the next
--consts or --out, take MSG.H, MSG.C and the hint row's from DIR: the
server's sources, in a directory without MSG.H, name SRC/CLIENT, where
the message module is.  Each --out FILE starts a group: the sources
after it, up to the next --out, are built into FILE: "=STAMP hhhh", then
each message of the sources in the order given as "nnn text", or "nnn"
for an empty text, the text as the source writes it, all ended CR LF;
sources before the first --out are only checked.  All the files share
one stamp, over the messages of every source, because MSG.C is compiled
once, with one MSG_STAMP, and linked into every program that loads any
of them.  --header writes a C header defining MSG_STAMP, rewritten only
when its contents change, so that MSG.C is compiled again only for a new
stamp.  --names writes a header defining every message's NAME as its
number and nothing else, for a resource script, which cannot read the C
headers that define them for the compiler; it too is rewritten only when
its contents change.  Files are written only when the checks pass, each
whole under a temporary name and then renamed into place.  Exit status 0 when every
check passes, 1 when any fails, 2 on a usage or read error.
"""
import os
import re
import sys

# Constant prefix and MSG_R_ range name of each module, by message file.
PREFIX = {
    'TUI': ('MT_', 'MSG_R_TUI'),
    'INVMENU': ('MN_', 'MSG_R_INV'),
    'LISTV': ('ML_', 'MSG_R_LISTV'),
    'FORM': ('MF_', 'MSG_R_FORM'),
    'MEMO': ('MM_', 'MSG_R_MEMO'),
    'KEYSCR': ('MK_', 'MSG_R_KEYSCR'),
    'SCRLOGIN': ('MG_', 'MSG_R_LOGIN'),
    'SCRITEM': ('MI_', 'MSG_R_ITEM'),
    'SCRLOC': ('MO_', 'MSG_R_LOC'),
    'SCRADMIN': ('MZ_', 'MSG_R_ADMIN'),
    'SCRSRCH': ('MS_', 'MSG_R_SRCH'),
    'SCRCODE': ('ME_', 'MSG_R_CODE'),
    'SCRSET': ('MC_', 'MSG_R_SET'),
    'SCRVERB': ('MW_', 'MSG_R_VERB'),
    'SCRPASS': ('MP_', 'MSG_R_PASS'),
    'SCRUSER': ('MU_', 'MSG_R_USER'),
    'SCRASSY': ('MA_', 'MSG_R_ASSY'),
    'SCRMAINT': ('MV_', 'MSG_R_MAINT'),
    'SCRLOAN': ('MB_', 'MSG_R_LOAN'),
    'SCRNFORM': ('MH_', 'MSG_R_HOLD'),
    'RPT': ('MR_', 'MSG_R_REPORT'),
    'SCRRPT': ('MQ_', 'MSG_R_SCRRPT'),
    'SCRCSV': ('MX_', 'MSG_R_CSV'),
    'INVSERV': ('MD_', 'MSG_R_SRV'),
}
# The C files holding a module's constants, where they are not <module>.C.
CFILE = {'INVSERV': ('SRVSCR.C', 'SRVRUN.C'), 'KEYSCR': ('TUI.C',),
         'SCRVERB': ('TUI.C',)}

LINE = re.compile(r'^([1-9][0-9]*) ([A-Z0-9_]+)(?: (.*))?$')
DEFINE = re.compile(r'^\s*#\s*define\s+(M[A-Z]_[A-Z0-9_]+)\s+([0-9]+)\b')
MSGDEF = re.compile(r'^\s*#\s*define\s+(MSG_[A-Z0-9_]+)\s+'
                    r'([0-9]+\b|MSG_[A-Z0-9_]+)')
CDEF = re.compile(r'^\s*#\s*define\s+([A-Z_][A-Z0-9_]*)\s+([^/]*?)\s*'
                  r'(?:/\*.*)?$')
HINT_CONSTS = ('TUI_HINTBUF', 'TUI_HINTLVL', 'VID_COLS')
RLO = re.compile(r'\bmsg_rlo\s*\[\s*\]\s*=\s*\{([^}]*)\}')
# The start of the stamp's data, naming the format of the built files.
FORMAT = b'=FORMAT 2\n'


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def unescape(text):
    """(unescaped length, (^1 count, ^2 count)) of text; ValueError
    saying why when it holds a bad escape or placeholder."""
    n = 0
    ph = [0, 0]
    i = 0
    while i < len(text):
        c = text[i]
        if c == '\\':
            e = text[i + 1:i + 2]
            if e in ('n', '\\'):
                i += 2
            elif e == 'x':
                h = text[i + 2:i + 4]
                if not re.match(r'^[0-9A-Fa-f]{2}$', h):
                    raise ValueError('bad \\x escape')
                if int(h, 16) < 3:
                    raise ValueError('\\x%s is reserved' % h)
                i += 4
            else:
                raise ValueError('bad escape \\%s' % e)
            n += 1
        elif c == '^':
            e = text[i + 1:i + 2]
            if e in ('1', '2'):
                ph[int(e) - 1] += 1
            elif e != '^':
                raise ValueError('bad placeholder ^%s' % e)
            i += 2
            n += 1
        else:
            i += 1
            n += 1
    return n, tuple(ph)


def read_lines(path):
    with open(path, 'rb') as f:
        data = f.read()
    return data.replace(b'\r\n', b'\n').split(b'\n')


def read_consts(srcdir):
    """The MSG_ constants of srcdir/MSG.H whose value is a number or an
    MSG_ constant defined above them, as a dict name -> value."""
    consts = {}
    for ln in read_lines(os.path.join(srcdir, 'MSG.H')):
        m = MSGDEF.match(ln.decode('latin-1'))
        if m and m.group(2).isdigit():
            consts[m.group(1)] = int(m.group(2))
        elif m and m.group(2) in consts:
            consts[m.group(1)] = consts[m.group(2)]
    return consts


def hint_consts(srcdir):
    """TUI_HINTBUF, TUI_HINTLVL and VID_COLS as a dict, evaluated from
    the #define lines of TUI.H, TUI.C and ../COMPAT/COMPAT.H in srcdir:
    a value is a number or an expression of numbers, + - * / and
    parentheses over other such constants.  Each of them, and each
    constant its value names, must be defined once in those files and
    outside any #if, #ifdef or #ifndef block (a header's include guard
    aside); otherwise ValueError says why.  A read error is raised; a
    constant missing or not of that form is left out."""
    defs = {}
    for f in ('TUI.H', 'TUI.C', os.path.join('..', 'COMPAT', 'COMPAT.H')):
        path = os.path.join(srcdir, f)
        lines = [l.decode('latin-1') for l in read_lines(path)]
        dirs = [(i, l.strip()) for i, l in enumerate(lines, 1)
                if l.strip().startswith('#')]
        guard = None
        if (len(dirs) > 1 and re.match(r'^#\s*ifndef\s+(\w+)$', dirs[0][1])
                and re.match(r'^#\s*define\s+%s$'
                             % re.escape(dirs[0][1].split()[-1]),
                             dirs[1][1])):
            guard = dirs[0][0]
        depth = 0
        for i, ln in enumerate(lines, 1):
            d = ln.strip()
            if re.match(r'^#\s*if', d):
                if i != guard:
                    depth += 1
                continue
            if re.match(r'^#\s*endif', d):
                depth = max(depth - 1, 0)
                continue
            m = CDEF.match(ln)
            if m:
                defs.setdefault(m.group(1), []).append(
                    (m.group(2), '%s:%d' % (path, i), depth > 0))

    def value(name, depth):
        found = defs.get(name)
        if found is None or depth > 8:
            return None
        if len(found) > 1:
            raise ValueError('%s defined more than once (%s)' % (
                name, ', '.join(w for _, w, _ in found)))
        expr, where, cond = found[0]
        if cond:
            raise ValueError('%s defined inside a conditional block (%s)'
                             % (name, where))
        parts = []
        for tok in re.findall(r'[A-Z_][A-Z0-9_]*|[0-9]+|[-+*/()]|\S',
                              expr):
            if re.match(r'^[A-Z_]', tok):
                v = value(tok, depth + 1)
                if v is None:
                    return None
                parts.append('(%d)' % v)
            elif re.match(r'^[0-9]+$|^[-+*()]$', tok):
                parts.append(tok)
            elif tok == '/':
                parts.append('//')
            else:
                return None
        try:
            return int(eval(' '.join(parts), {'__builtins__': {}}, {}))
        except (SyntaxError, ZeroDivisionError, TypeError):
            return None

    vals = {}
    for name in HINT_CONSTS:
        v = value(name, 0)
        if v is not None:
            vals[name] = v
    return vals


def check_hint(chk, where, text, n):
    """The hint row's limits on the text of a key hint, n bytes when
    unescaped."""
    h = chk.hint
    lvl = sum(1 for w in text.split(' ') if w[:1] in ('1', '2', '3'))
    if n > h['TUI_HINTBUF'] - 1:
        chk.err(where, 'hint longer than TUI_HINTBUF - 1 (%d > %d)'
                % (n, h['TUI_HINTBUF'] - 1))
    if lvl > h['TUI_HINTLVL']:
        chk.err(where, 'hint with more level digits than TUI_HINTLVL '
                '(%d > %d)' % (lvl, h['TUI_HINTLVL']))
    if n - lvl > h['VID_COLS'] - 1:
        chk.err(where, 'hint wider than VID_COLS - 1 (%d > %d)'
                % (n - lvl, h['VID_COLS'] - 1))


def header_defs(srcdir):
    """Every message constant defined in a header of srcdir, as a list of
    (name, value, "file:line") in file and line order.  A header that
    cannot be read is skipped."""
    defs = []
    for f in sorted(f for f in os.listdir(srcdir) if f.endswith('.H')):
        src = os.path.join(srcdir, f)
        try:
            lines = read_lines(src)
        except OSError:
            continue
        for i, ln in enumerate(lines, 1):
            m = DEFINE.match(ln.decode('latin-1'))
            if m:
                defs.append((m.group(1), int(m.group(2)),
                             '%s:%d' % (src, i)))
    return defs


class Check:
    """The state of one run over the message files: the MSG.H constants,
    the problems found, the numbers and names seen so far, and the
    messages kept for the outputs."""

    def __init__(self):
        self.consts = None
        self.hint = None
        self.errs = []
        self.seen_num = {}
        self.seen_name = {}
        self.msgs = []
        self.lens = {}
        self.body = []

    def err(self, where, what):
        self.errs.append('%s: %s' % (where, what))


def module_defs(chk, path, cpaths, hdefs, prefix):
    """The constants with prefix that the module of the message file path
    defines, in its C files cpaths and in the headers (hdefs), as a dict
    name -> value."""
    defs = {}
    found = []
    for cpath in cpaths:
        try:
            for i, ln in enumerate(read_lines(cpath), 1):
                m = DEFINE.match(ln.decode('latin-1'))
                if m:
                    found.append((m.group(1), int(m.group(2)),
                                  '%s:%d' % (cpath, i)))
        except OSError as e:
            chk.err(path, 'cannot read %s: %s' % (cpath, e.strerror))
    for name, value, where in found + hdefs:
        if name.startswith(prefix):
            if name in defs:
                chk.err(where, '%s defined twice' % name)
            defs[name] = value
    return defs


def check_file(chk, path, prefix, rname, lo, hi):
    """Check the lines of the message file path, numbered lo..hi - 1 in
    the range rname, and keep its messages; return its messages as a
    dict name -> (number, "file:line").  A read error is raised."""
    maxnum = chk.consts['MSG_MAXNUM']
    lines = read_lines(path)
    if lines and lines[-1] == b'':
        lines.pop()
    found = {}
    for i, raw in enumerate(lines, 1):
        where = '%s:%d' % (path, i)
        try:
            ln = raw.decode('ascii')
        except UnicodeDecodeError:
            chk.err(where, 'non-ASCII byte')
            continue
        if any(ord(c) < 0x20 or ord(c) > 0x7E for c in ln):
            chk.err(where, 'control character')
            continue
        if ln == '' or ln.startswith('#'):
            continue
        m = LINE.match(ln)
        if not m:
            chk.err(where, 'not "nnn NAME text"')
            continue
        num = int(m.group(1))
        name = m.group(2)
        text = m.group(3) or ''
        built = '%d %s' % (num, text) if text else str(num)
        if len(built) > chk.consts['MSG_MAXRAW']:
            chk.err(where, 'line longer than MSG_MAXRAW')
        if not 1 <= num < maxnum:
            chk.err(where, 'number %d out of 1..%d' % (num, maxnum - 1))
        elif not lo <= num < hi:
            chk.err(where, 'number %d outside %s (%d..%d)'
                    % (num, rname, lo, hi - 1))
        if not name.startswith(prefix):
            chk.err(where, '%s lacks the prefix %s' % (name, prefix))
        if num in chk.seen_num:
            chk.err(where, 'number %d also at %s' % (num, chk.seen_num[num]))
        if name in chk.seen_name:
            chk.err(where, 'name %s also at %s'
                    % (name, chk.seen_name[name]))
        chk.seen_num[num] = where
        chk.seen_name[name] = where
        try:
            n, ph = unescape(text)
        except ValueError as e:
            chk.err(where, str(e))
            continue
        if n > chk.consts['MSG_MAXLEN']:
            chk.err(where, 'text longer than MSG_MAXLEN')
        if 'HINT' in name[len(prefix):]:
            check_hint(chk, where, text, n)
        found[name] = (num, where)
        chk.lens[num] = n
        chk.msgs.append((num, name, ph))
        chk.body.append(built)
    return found


def check_module(chk, path, consts, starts, hdefs):
    """Check the message file path of one module against its constants;
    0, or 2 on a read error (reported)."""
    mod = os.path.splitext(os.path.basename(path))[0].upper()
    if mod not in PREFIX:
        chk.err(path, 'not a known module message file')
        return 0
    prefix, rname = PREFIX[mod]
    if rname not in consts:
        chk.err(path, '%s not in MSG.H' % rname)
        return 0
    lo = consts[rname]
    later = [s for s in starts if s > lo]
    hi = later[0] if later else consts['MSG_MAXNUM']
    cpaths = [os.path.join(os.path.dirname(path), c)
              for c in CFILE.get(mod, (mod + '.C',))]
    defs = module_defs(chk, path, cpaths, hdefs, prefix)
    try:
        found = check_file(chk, path, prefix, rname, lo, hi)
    except OSError as e:
        sys.stderr.write('msgchk: %s\n' % e)
        return 2
    for name, num in sorted(defs.items()):
        if name not in found:
            chk.err(path, '%s (%d) has no line' % (name, num))
        elif found[name][0] != num:
            chk.err(found[name][1], '%s is %d in the source, %d here'
                    % (name, num, found[name][0]))
    for name, (num, where) in sorted(found.items()):
        if name not in defs:
            chk.err(where, '%s is not defined in %s or a header'
                    % (name, ' or '.join(cpaths)))
    nums = [num for num, _ in found.values()]
    if consts['MSG_PAGEMIN'] <= lo < consts['MSG_PAGEEND'] and nums:
        size = 2 * (max(nums) - lo + 1) + sum(chk.lens[n] + 1
                                              for n in nums)
        if size > consts['MSG_PAGE']:
            chk.err(path, 'page of %d bytes, more than MSG_PAGE (%d)'
                    % (size, consts['MSG_PAGE']))
    return 0


def write_file(path, data, keep=False):
    """Write data to path, making its directory; with keep, leave a file
    that already holds data untouched.  The data goes to a temporary file
    beside path, named with this process's ID, renamed over it once
    written, so that a reader finds the old file or the new one whole; a
    failed or interrupted write removes it."""
    if keep:
        try:
            with open(path, 'rb') as f:
                if f.read() == data:
                    return
        except OSError:
            pass
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    tmp = '%s.%d' % (path, os.getpid())
    try:
        with open(tmp, 'wb') as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.remove(tmp)
        except OSError:
            pass
        raise


def write_outputs(groups, header, names, msgs):
    """Write each group's message file, the stamp header header and the
    names header names (each None for none) for the messages msgs, list
    of (number, name, placeholder counts), of every group together:
    groups is a list of (file, built lines), the file None for a group
    checked only."""
    crc = crc16(FORMAT)
    for num, name, ph in sorted(msgs):
        crc = crc16(('%d %s %d %d\n' % (num, name, ph[0], ph[1]))
                    .encode('ascii'), crc)
    stamp = '%04X' % crc
    for out, body in groups:
        if out:
            data = ('=STAMP %s\r\n' % stamp +
                    ''.join(l + '\r\n' for l in body))
            write_file(out, data.encode('ascii'))
    if header:
        data = ('/* Generated by TEST/msgchk.py from the message sources; '
                'do not edit. */\n'
                '#ifndef MSGSTMP_H\n#define MSGSTMP_H\n'
                '#define MSG_STAMP 0x%sU\n#endif\n' % stamp)
        write_file(header, data.encode('ascii'), keep=True)
    if names:
        data = ('/* Generated by TEST/msgchk.py from the message sources; '
                'do not edit. */\n'
                '#ifndef MSGNUM_H\n#define MSGNUM_H\n' +
                ''.join('#define %s %d\n' % (name, num)
                        for num, name, ph in sorted(msgs)) +
                '#endif\n')
        write_file(names, data.encode('ascii'), keep=True)


def dir_settings(srcdir, msgdir):
    """(consts, hint, starts, hdefs): the MSG.H constants, the hint
    row's constants and the MSG_R_ range starts of the directory msgdir,
    and the message constants of the headers of the source directory
    srcdir.  OSError or ValueError says why they cannot be had."""
    consts = read_consts(msgdir)
    hint = hint_consts(msgdir)
    for k in ('MSG_MAXNUM', 'MSG_MAXLEN', 'MSG_MAXRAW', 'MSG_PAGE',
              'MSG_PAGEMIN', 'MSG_PAGEEND'):
        if k not in consts:
            raise ValueError('%s not found in MSG.H' % k)
    for k in HINT_CONSTS:
        if k not in hint:
            raise ValueError('%s not found in TUI.H, TUI.C or COMPAT.H' % k)
    starts = sorted(v for k, v in consts.items() if k.startswith('MSG_R_'))
    return consts, hint, starts, header_defs(srcdir)


def check_rlo(chk, msgdir, consts):
    """Check that the msg_rlo table of MSG.C in msgdir names every MSG_R_
    constant of MSG.H (consts) once, in the order of their values, as
    MSG.C's range lookup needs.  A read error is raised."""
    path = os.path.join(msgdir, 'MSG.C')
    text = re.sub(r'/\*.*?\*/', ' ', b'\n'.join(read_lines(path))
                  .decode('latin-1'), flags=re.S)
    m = RLO.search(text)
    if not m:
        chk.err(path, 'no msg_rlo table')
        return
    got = re.findall(r'\w+', m.group(1))
    want = [k for v, k in sorted((v, k) for k, v in consts.items()
                                 if k.startswith('MSG_R_'))]
    for i in range(max(len(got), len(want))):
        g = got[i] if i < len(got) else 'nothing'
        w = want[i] if i < len(want) else 'nothing'
        if g != w:
            chk.err(path, 'msg_rlo entry %d is %s, where MSG.H puts %s'
                    % (i + 1, g, w))
            return


def usage(reason):
    """Write reason and the usage lines on standard error; return 2."""
    sys.stderr.write('msgchk: %s\n%s\n' % (reason, __doc__.split('\n\n')[1]))
    return 2


def main(args):
    header = None
    names = None
    groups = [[None, []]]
    cdir = None
    while args:
        if (args[0] in ('--out', '--header', '--names', '--consts') and
                len(args) > 1):
            if args[0] == '--out':
                groups.append([args[1], []])
                cdir = None
            elif args[0] == '--consts':
                cdir = args[1]
            elif args[0] == '--names':
                names = args[1]
            else:
                header = args[1]
            args = args[2:]
        elif args[0].startswith('--'):
            break
        else:
            groups[-1][1].append((args[0], cdir))
            args = args[1:]
    if args:
        return usage('unknown or incomplete option %s' % args[0])
    if not any(g[1] for g in groups):
        return usage('no FILE.MSG')

    settings = {}
    rlo_done = set()
    chk = Check()
    groups_out = []
    for out, paths in groups:
        first = len(chk.body)
        for path, cdir in paths:
            srcdir = os.path.dirname(path) or '.'
            key = (srcdir, cdir or srcdir)
            try:
                if key not in settings:
                    settings[key] = dir_settings(*key)
                    if key[1] not in rlo_done:
                        check_rlo(chk, key[1], settings[key][0])
                        rlo_done.add(key[1])
            except (OSError, ValueError) as e:
                sys.stderr.write('msgchk: %s\n' % e)
                return 2
            consts, chk.hint, starts, hdefs = settings[key]
            chk.consts = consts
            r = check_module(chk, path, consts, starts, hdefs)
            if r:
                return r
        groups_out.append((out, chk.body[first:]))

    if chk.errs:
        for e in chk.errs:
            print(e)
        sys.stderr.write('msgchk: %d problem(s)\n' % len(chk.errs))
        return 1
    write_outputs(groups_out, header, names, chk.msgs)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
