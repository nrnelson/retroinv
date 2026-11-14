"""lint.py - reject C constructs the DOS compilers accept but the project bans.

usage: python3 -I TEST/lint.py [--wattcp USERS] [--compat | --win] FILE...
       python3 -I TEST/lint.py --scripts TESTDIR [LIST...] [--mono MONO...]
       python3 -I TEST/lint.py --kernel PREP NAME...
       python3 -I TEST/lint.py --docs FILE... [--refs FILE...]
       python3 -I TEST/lint.py --names THIN PROGRAMS FILE...
       python3 -I TEST/lint.py --strings RC SRC NAME

Open Watcom's strict mode still accepts the double-underscore memory-model
keywords and // comments, and its C library reaches INT 21h function 68h
(a DOS 3.3 call, below the DOS 3.1 floor) through fsync(), _commit(),
_dos_commit() and fopen mode "c".  Files linted without --compat must not
use any of these, nor interrupts, port I/O or any #pragma.  Nor may they name
the helpers COMPAT.H keeps for SRC/COMPAT/: COMPAT_INTERNAL, the
far-pointer and far-data helpers FARP, CFP, cmem_ptr, FARD and FARX
(portable code reaches far memory only through cmem_blk handles), the
interrupt-flag helpers IRQ_OFF, IRQ_ON, IRQ_SAVE, IRQ_IF and compat_flags,
BDA_SEG, the segment of the BIOS data area, and cmem_heapsz, the near
heap of the program's near-heap module.

Watcom C 6.5 adds three more: its preprocessor mis-handles #elif (after
a taken group it skips the next one but enables the one after), it warns
"meaningless statement" on "(void)x;", and it refuses (error E091) a
definition whose const pointer parameter points to a struct that was
incomplete at the prototype.  #elif and // comments are rejected in every
file and "(void)x;" in those linted without --compat.  --compat is for
the files the build compiles without the strict-ANSI switch: SRC/COMPAT/
except its portable modules, whose host-only stubs may discard parameters
that way because Watcom C 6.5 never compiles them, and the files of USERS
below.  With it only those first two checks, the negative-size check, the
incomplete-struct check, the block-initializer check, the WATTCP header
check, the private header check and the line-length check below run.

--win is for the Windows sources, which the build also compiles without
the strict-ANSI switch, as windows.h needs its keywords: it runs the
checks of --compat and the keyword checks.  Those files reach the
memory-model and calling-convention keywords only through macros: FAR,
NEAR, PASCAL and CDECL of windows.h, and WCALLBACK of SRC/COMPAT/CWIN.H
(FAR PASCAL __export), which declares every function Windows calls, so
that wlink exports it, as Windows needs.
So a keyword written directly (far, _far, __far, pascal, __export and
the others the portable rules refuse) is rejected; so are WINAPI and
CALLBACK, which the headers of Windows 3.0 lack, and PASCAL anywhere but
before WinMain and in a pointer to a FAR PASCAL function ("PASCAL *"),
so that FAR PASCAL never appears without the export.  A callback written
with neither, then cast to FARPROC, passes lint and the compiler; review
catches it.

The incomplete-struct check rejects, in every file, a const pointer to a
struct that is incomplete where it is written: in a parameter list
outside a function body (of a prototype, a definition or a function
pointer, a struct member's included), and in the declarator of a
typedef; not in a struct member or a file-scope initializer.  A struct
is complete after its body in the same file, and in every other file
when a header defines it; a type name stands for a struct through a
forward typedef "typedef struct tag name;" in a header that is linted
or sits beside a linted file, or in the file itself.

A declaration inside a block has no initializer: its value is assigned
by the statements after the declarations.  Only a static one, whose
initializer runs once, keeps it.  A statement at brace depth 1 or more
that opens with a type and a declarator and holds an "=" that is no
comparison or compound assignment is rejected in every file, --compat
included.

Compile-time checks must not rely on a negative array size, as in
"typedef char x[(cond) ? 1 : -1];" or "sizeof(char[1 - 2 * !(cond)])":
Watcom C 6.5 and Turbo C 2.01 accept such an array without a word.  The
form every compiler refuses is a pair of declarations of one array,
"extern char x[1]; extern char x[(cond) ? 1 : 2];".  An array bound
whose ternary falls back to a negative constant, or that starts
"1 - 2 * !", is rejected in every file, --compat included; the match
spans lines.

WATTCP's headers (the INCLUDE/ directory of its archive) may be included
only by the files of USERS, a space-separated list of paths (WATTCP_USERS
of SRCS.MK; none without --wattcp), which the build compiles with
WATTCP's packing and char flags so that its structs have one layout.
This check runs in every file, --compat included.

Some headers are private to a few modules: SRC/DBF/DBFINT.H to those of
SRC/DBF/, SRC/CLIENT/SCRSHR.H to the client's screens SRC/CLIENT/SCR*.C,
SRC/CLIENT/PAGERINT.H to the pager SRC/CLIENT/PAGER.C and its survey
SRC/CLIENT/PAGERSV.C.
An include of one from any other file outside the unit tests in TEST/
is rejected.  This check also runs in every file, --compat included.

The rows TX(name, text) of SRC/ENGINE/TXTAB.H are numbered by their
position, and a number must keep its text: when TXTAB.H is linted, its
row names must be the names of TXNUM.TXT (beside this script) in the
same order, possibly followed by new rows, which are reported with the
request to append their names to TXNUM.TXT.

The text a user reads lives in the message and help files, so that it
can be reworded or translated without touching the code.  In the sources
of SRC/CLIENT/, SRC/SERVER/ and SRC/TCP/ a string literal that holds a
lower-case letter once its escapes and printf conversions are taken out
is such text and is rejected: protocol words, field names, keys and
choice codes are upper case.  The files of PROSE_FILES are exempt, and
the one function PROSE_FUNCS names in each of its files, each for the
reason the table gives: the texts a program writes on standard error
before its message file is loaded, or in its place when it cannot be.

Comments and string literals are blanked before the code is matched, so
prose such as "far buffer" in a comment is not reported; the three header
checks match the raw #include lines instead.

A line of a C source or header is at most MAX_COLUMNS columns, comment
and string included, a tab counted to the next multiple of 8.  This
check runs in every file, --compat included.

--scripts checks the screen-test tables in TESTDIR (TEST/) against the
files around them.  Each program of SCRIPT_TESTS lists its key scripts in
the th_scripts table of TESTDIR/T_<P>.C, P being the prefix of every
script name in it (SCRE1, SCRE2, ... in T_SCRE.C):

  - every row of the table is read: a row whose name, user, commands
    and flags are not in the expected form, or whose name lacks the
    prefix or repeats another row's, is reported;
  - the key scripts TESTDIR/KEYS/<P>*.KEY are exactly the table's rows;
  - every golden in TESTDIR/GOLDEN/SCR/ and TESTDIR/GOLDEN/PRT/ whose
    name starts with a table's prefix is one that its program compares:
    NAME.SNC for every row, NAME.SNM for a row with SF_MONO, and in
    PRT/ NAME.PRN for a row with SF_PRN and NAME plus the extension of
    each of the row's entries in the outfiles table that gives one; in
    PRT/, every golden starting RPT is a file name written in
    TESTDIR/T_RPT.C;
  - each LIST, a space-separated list of files as the build passes it
    (SCRE_KEYS, or the files of a test's floppy boots), names at least
    one key script, and for each prefix it names, exactly the table's
    rows, once each; its files with other names are ignored;
  - each MONO, one per program of SCRIPT_TESTS in that order, the files
    its Hercules run is given (its <NAME>_DOSBOXDATA), names exactly the
    NAME.SNM goldens of the table's SF_MONO rows, once each; its files
    with other extensions are ignored.

TESTDIR/T_SCR.SH checks the goldens of the other scripts.

--kernel checks the build's list of WATTCP's kernel modules against the
one the preparing script extracts: the NAMEs (the base names of
WATTCP_KERNEL of SRCS.MK) must be those of the KERNEL tuple of PREP
(SRC/TCP/WATPREP.PY), each once, in any order.  PREP is parsed, not run.

--docs checks prose documents (Markdown or plain text) for a passage
pasted twice, into one file or two of one extension (the plain-text
notes of a release disk retell the Markdown documents for a reader on
DOS by design): a paragraph, list item, table row or heading
of DOC_UNIT characters or more, its blanks collapsed, that appears
twice, and DOC_RUN lines in a row of DOC_LINE characters or more each
that appear again in the same order, shorter lines between them
skipped.  A table row or heading is a passage of its own line.  Blank
lines, rules and table separators end a paragraph and are not compared,
nor are the lines of code fences (three backticks, then an info string
without one), which legitimately repeat a command; a fence left open at
the end of the file is reported.  Blanks are spaces and tabs, not the
other characters a code page may put at 0x85 or 0xA0.  A passage below
both limits, such as a table header and its first row, may repeat.
Table rows are compared as paragraphs are: a short row, such as a key's,
stays below DOC_UNIT, while a long row restates a fact.

It also checks the references of the documents and of the files after
--refs: each document named, DOC/ then a name and .md, or README.md, a
directory before either or not (a path through .. is not read), must
exist.  A label named with it must be a heading, a bold label or the
first cell of a table row of it, a heading's extra in parentheses after
its name left out or not, compared without backticks, blanks or case.
The label follows the document, past its closing backtick and a Markdown
link's parenthesis, as ", Label", " (Label)" or ' in "Label"' after a
few words, or precedes it as '"Label" in ' or as "(Label of " when the
document closes the parenthesis.  A label that follows is quoted or
starts with a capital or a digit, a digit only starting prose when no
label does; unquoted, it is the longest label the text starts with, and
a further one may follow after a comma or "and" when it starts so too;
in parentheses, the last one must close them.  Outside the Markdown
documents a line's comment leader (*, #, ; or //) is skipped, so that a
reference may wrap.  In a Markdown document, "(Label, above)" or
"(Label, below)", a lead-in ending in ": " before the label or not, or a
comma-separated path of labels whose last one is so placed, must name a
label of its own on that side.

--names keeps the documents FILE naming the programs as the build does,
so that renaming one stays an edit of the build: each FILE must name
THIN, the network-only client (THIN of SRCS.MK), and every word INV
followed by letters or digits in it must be one of the word list
PROGRAMS, the build's programs, so that a name left from before a rename
is reported.  A word followed by .C or .H names a source module there,
not a program, and the English words of NOT_NAMES are none either.

--strings holds the string tables of the resource script RC to the
buffer its program loads them into, NAME, the size "#define NAME n" of
the source SRC gives, its NUL included, as LoadString cuts a longer
string without a word: every string of RC's own STRINGTABLE blocks (not
those of the scripts it includes) is at most n - 1 bytes, its escapes
and doubled quotes counted as one, and every line in a block is blank
or one string, an id then its text in quotes.

Exit status is 1 when anything is reported, 2 on a usage or read error.
"""
import ast
import bisect
import os
import re
import sys

MAX_LITERAL = 255
MAX_COLUMNS = 79

# The memory-model and calling-convention keywords, refused by the
# portable rules and by --win, which also refuses the macros the Windows
# 3.0 headers lack and PASCAL where WCALLBACK belongs.
KEYWORD_RX = re.compile(r'\b(_*(far|near|huge|interrupt|cdecl|pascal|asm)|'
                        r'_+(based|fortran|loadds|saveregs|export|segment|'
                        r'segname|self))\b')
WIN_RULES = [
    (KEYWORD_RX, 'keyword written directly: use FAR, NEAR, PASCAL or CDECL '
                 'of windows.h'),
    (re.compile(r'\b(WINAPI|CALLBACK)\b'),
     'WINAPI or CALLBACK, which the Windows 3.0 headers lack: write '
     'WCALLBACK, or FAR PASCAL * for a pointer'),
    (re.compile(r'\bPASCAL\b(?!\s*(WinMain\b|\*))'),
     'PASCAL but before WinMain or *: a function Windows calls is WCALLBACK'),
]

CODE_RULES = [
    (KEYWORD_RX, 'memory-model or calling-convention keyword'),
    (re.compile(r'^\s*#\s*pragma\b'), '#pragma'),
    (re.compile(r'\b_Packed\b'), 'packed struct'),
    (re.compile(r'\blong\s+double\b'), 'long double'),
    (re.compile(r'\b(inp|outp|inpw|outpw|inportb?|outportb?|int86x?|'
                r'intdosx?|intr|bdos|bdosptr|geninterrupt|segread|'
                r'MK_FP|FP_SEG|FP_OFF)\s*\('),
     'interrupt, port or segment call'),
    (re.compile(r'\b(_dos_\w+|_bios_\w+|fsync|_commit|commode)\b'),
     'DOS/BIOS library call'),
    (re.compile(r'\b(COMPAT_INTERNAL|FARP|CFP|cmem_ptr|FARD|FARX)\b'),
     'far-pointer helper reserved for SRC/COMPAT/'),
    (re.compile(r'\b(IRQ_OFF|IRQ_ON|IRQ_SAVE|IRQ_IF|compat_flags|BDA_SEG)\b'),
     'interrupt-flag or BIOS-data helper reserved for SRC/COMPAT/'),
    (re.compile(r'\bcmem_heapsz\b'),
     'near-heap size reserved for SRC/COMPAT/'),
]

# Checked against the raw line, because a quoted include name is a string
# literal and is blanked in the code view.
INCLUDE_RULE = re.compile(r'^\s*#\s*include\s*[<"](dos|conio|i86|bios|alloc|'
                          r'malloc|mem|graph|io|direct|process)\.h[>"]', re.I)


def include_of(names):
    """An include line of a header whose base name (any case, without .h)
    the regex names matches; a directory part ("INCLUDE/TCP.H",
    <wattcp\\tcp.h>) does not hide the name."""
    return re.compile(r'^\s*#\s*include\s*[<"]([^<>"]*[/\\])?(' + names +
                      r')\.h[>"]', re.I)


# WATTCP's own header names.
WATTCP_INCLUDE = include_of('bootp|copyrigh|copyright|elib|ethdev|icmp|tcp|'
                            'wattcp')

# The headers private to some modules: an include of each, the files
# besides TEST/ (whose unit tests check its interfaces) that may include
# it, matched against the path upper case with forward slashes and a
# leading one, and the refusal.
PRIVATE_HEADERS = [
    (include_of('dbfint'), re.compile(r'/SRC/DBF/'),
     'DBFINT.H outside SRC/DBF/ and TEST/'),
    (include_of('scrshr'), re.compile(r'/SRC/CLIENT/SCR[^/]*\.C$'),
     'SCRSHR.H outside SRC/CLIENT/SCR*.C and TEST/'),
    (include_of('pagerint'), re.compile(r'/SRC/CLIENT/PAGER(SV)?\.C$'),
     'PAGERINT.H outside SRC/CLIENT/PAGER.C, PAGERSV.C and TEST/'),
]


def private_found(path, raw_lines):
    """The (line, refusal) of each include in raw_lines of a header that
    PRIVATE_HEADERS keeps from path."""
    norm = '/' + path.replace('\\', '/').upper()
    if '/TEST/' in norm:
        return []
    return [(num, what) for include, users, what in PRIVATE_HEADERS
            if not users.search(norm)
            for num, raw in enumerate(raw_lines, 1) if include.search(raw)]


def wattcp_user(path, users):
    """True when path is one of users (any case, either slash)."""
    norm = path.replace('\\', '/').upper()
    return any(norm == u or norm.endswith('/' + u) for u in users)


# A negative array size as a compile-time check: "[c ? 1 : -1]",
# "[c ? 1 : (-1)]" or "[1 - 2 * !c]".  Matched over the whole blanked text,
# since a condition may span lines.
NEG_SIZE = re.compile(r'\?[^;{}?]*:\s*\(?\s*-\s*\d+\s*\)?\s*\]|'
                      r'\[\s*1\s*-\s*2\s*\*\s*!')

# The tokens the incomplete-struct check follows: a preprocessor line
# (skipped), braces, parentheses, the end of a declaration, the start of
# an initializer, typedef, the start of a struct body (its tag, if any),
# and a const pointer to a type, the const written before or after it.
DECL_TOKEN = re.compile(r'^[ \t]*#(?:\\\n|.)*|(?P<lb>\{)|(?P<rb>\})|'
                        r'(?P<lp>\()|(?P<rp>\))|(?P<semi>;)|(?P<eq>=)|'
                        r'\b(?P<typedef>typedef)\b|'
                        r'(?P<body>\bstruct(?:\s+\w+)?)\s*(?=\{)|'
                        r'\bconst\s+(?P<pre>(?:struct\s+)?\w+)\s*\*|'
                        r'\b(?P<post>(?:struct\s+)?\w+)\s+const\s*\*', re.M)
FWD_TYPEDEF = re.compile(r'\btypedef\s+struct\s+(\w+)\s+(\w+)\s*;')
STRUCT_BODY = re.compile(r'\bstruct\s+(\w+)\s*\{')

# A statement that declares: a type, then a declarator ("u16 n",
# "char *p", "const rcol *c", "int (*fn)(void)"), not a keyword before
# an expression.  One with an "=" that is no comparison, at brace depth
# 1 or more, gives a block-scope variable an initializer, which only a
# static one may have.
PP_LINE = re.compile(r'^[ \t]*#(?:\\\n|.)*', re.M)
STMT_END = re.compile(r'[;{}]')
DECL_STMT = re.compile(r'\s*(?!(?:return|else|case|do|if|for|while|switch)'
                       r'\b)[A-Za-z_]\w*(?:\s+|\s*\*)[\s*(]*[A-Za-z_]')
INIT_EQ = re.compile(r'(?<![=!<>+*/%&|^-])=(?!=)')

ELIF = re.compile(r'^\s*#\s*elif\b')
VOID_STMT = re.compile(r'(^|[;{})])\s*\(\s*void\s*\)\s*'
                       r'[A-Za-z_][\w.\[\]>-]*\s*;')

FOPEN = re.compile(r'\b(fopen|freopen|fdopen|_fsopen)\s*\(')
COMMIT_MODE = re.compile(r'^[rwa][bt+]*c[bt+n]*$')

# Turbo C 2.01 under -A defines NULL only in <stddef.h> (and stdio.h).
NULL_RX = re.compile(r'\bNULL\b')
STDDEF_RX = re.compile(r'^\s*#\s*include\s*<(stddef|stdio)\.h>', re.M)

# A string literal's escapes, each counted as one character.
ESCAPE_RX = re.compile(r'\\(x[0-9A-Fa-f]+|[0-7]{1,3}|.)')


def scan(text):
    """Return (code lines with comments/literals blanked, literals, errors).

    literals is a list of (line number, literal body); errors lists
    (line number, kind, message), kind 'comment' for a // comment and
    'literal' for an unterminated or over-long literal.
    """
    out = []
    lits = []
    errs = []
    i = 0
    n = len(text)
    line = 1
    state = 'code'
    lit_start = 0
    buf = []
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ''
        if state == 'code':
            if c == '/' and nxt == '*':
                state = 'comment'
                out.append('  ')
                i += 2
                continue
            if c == '/' and nxt == '/':
                errs.append((line, 'comment', '// comment'))
                out.append('  ')
                i += 2
                continue
            if c in '"\'':
                state = c
                lit_start = line
                buf = []
                out.append(c)
                i += 1
                continue
            out.append(c)
        elif state == 'comment':
            if c == '*' and nxt == '/':
                state = 'code'
                out.append('  ')
                i += 2
                continue
            out.append('\n' if c == '\n' else ' ')
        else:
            if c == '\\' and nxt:
                buf.append(c + nxt)
                out.append('  ' if nxt != '\n' else ' \n')
                if nxt == '\n':
                    line += 1
                i += 2
                continue
            if c == '\n':
                # C89 literals cannot span lines; ending the literal here
                # keeps a stray quote (an apostrophe in #error text, say)
                # from hiding the rest of the file.
                errs.append((lit_start, 'literal', 'unterminated literal'))
                state = 'code'
                out.append(c)
                line += 1
                i += 1
                continue
            if c == state:
                if state == '"':
                    body = ''.join(buf)
                    lits.append((lit_start, body))
                    if len(ESCAPE_RX.sub('x', body)) > MAX_LITERAL:
                        errs.append((lit_start, 'literal',
                                     'string literal longer than '
                                     '%d characters' % MAX_LITERAL))
                state = 'code'
                out.append(c)
                i += 1
                continue
            buf.append(c)
            out.append('\n' if c == '\n' else ' ')
        if c == '\n':
            line += 1
        i += 1
    return ''.join(out).split('\n'), lits, errs


# The directories whose literals must not be user-visible text, and the
# files there that may hold such text, with the reason.
PROSE_DIRS = ('/SRC/CLIENT/', '/SRC/SERVER/', '/SRC/TCP/')
PROSE_FILES = {
    'SRC/CLIENT/INV.C': 'usage and start-up errors on standard error',
    'SRC/CLIENT/INVLOC.C': 'INV.C\'s usage lines of the connections',
    'SRC/CLIENT/INVNOL.C': 'INV.C\'s usage lines of the connections',
    'SRC/CLIENT/INVCOM.C': 'the reasons a serial port did not open, and '
                           'their format, on standard error',
    'SRC/CLIENT/MSG.C': 'why a message file did not load',
    'SRC/CLIENT/MSG.H': 'MSG_NOFILE, written when INV.MSG did not load',
    'SRC/CLIENT/LINKLOC.C': 'why link_open_local failed, written by INV '
                            'on standard error before INV.MSG is loaded',
    'SRC/CLIENT/LINKNOL.C': 'why link_open_local failed, as in LINKLOC.C',
    'SRC/SERVER/INVSERV.C': 'usage and start-up errors on standard error',
    'SRC/TCP/INVTCP.C': 'usage and start-up errors on standard error',
}
# The files there that may hold such text in one function only: the
# function, and the reason.
PROSE_FUNCS = {
    'SRC/SERVER/SRVRUN.C': ('start', 'start-up errors on standard error '
                            'before INVSERV.MSG is loaded'),
}
LOWER_RX = re.compile(r'[a-z]')
PRINTF_RX = re.compile(r'%[-#0 +]*[0-9]*l?[a-zA-Z]')


def prose_lines(path, code):
    """The numbers of the lines of path, whose lines are code with the
    comments and literals blanked, that may hold user-visible text: all
    of them outside PROSE_DIRS and in PROSE_FILES, those of the function
    PROSE_FUNCS names, from its first line to the "}" in column 1 that
    ends it, else none."""
    norm = '/' + path.replace('\\', '/').upper()
    if (not any(d in norm for d in PROSE_DIRS) or
            any(norm.endswith('/' + f) for f in PROSE_FILES)):
        return range(1, len(code) + 1)
    for f, (fn, _) in PROSE_FUNCS.items():
        if not norm.endswith('/' + f):
            continue
        head = re.compile(r'^\w.*\b%s\s*\(' % fn)
        for i, ln in enumerate(code):
            if head.match(ln) and not ln.rstrip().endswith(';'):
                end = next((j for j in range(i, len(code))
                            if code[j].startswith('}')), len(code))
                return range(i + 1, end + 2)
    return range(0)


# A row of SRC/ENGINE/TXTAB.H, and the list of its names in number order.
TX_ROW = re.compile(r'^TX\((\w+),', re.M)
TXNUM = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'TXNUM.TXT')


def check_txnum(path, text):
    """Report each difference between the row names of TXTAB.H (path,
    its text) and TXNUM.TXT; return how many, or None when TXNUM.TXT
    cannot be read."""
    try:
        with open(TXNUM, 'rb') as f:
            want = [ln.strip() for ln in f.read().decode('latin-1').split('\n')
                    if ln.strip() and not ln.startswith('#')]
    except OSError as e:
        sys.stderr.write('lint: %s\n' % e)
        return None
    rows = TX_ROW.findall(text)
    for i, name in enumerate(rows[:len(want)]):
        if name != want[i]:
            print('%s: row %d is %s, but text %d is %s in %s: rows keep '
                  'their numbers' % (path, i + 1, name, i + 1, want[i], TXNUM))
            return 1
    if len(rows) < len(want):
        print('%s: %d rows, but %s names %d: rows are never removed'
              % (path, len(rows), TXNUM, len(want)))
        return 1
    if len(rows) > len(want):
        print('%s: new rows %s: append their names to %s'
              % (path, ' '.join(rows[len(want):]), TXNUM))
        return 1
    return 0


def read_file(path):
    """The text of the file at path, CR LF read as LF."""
    with open(path, 'rb') as f:
        return f.read().decode('latin-1').replace('\r\n', '\n')


def load(path):
    """(text, scan(text), blanked code) of the source at path, CR LF
    read as LF."""
    text = read_file(path)
    scanned = scan(text)
    return text, scanned, '\n'.join(scanned[0])


def struct_index(codes):
    """(names, headers) over codes, a dict path -> blanked code: the
    struct tag of each type name a forward typedef declares, and for
    each tag the headers that give its body."""
    names = {}
    headers = {}
    for path, code in codes.items():
        names.update((n, t) for t, n in FWD_TYPEDEF.findall(code))
        if path.upper().endswith('.H'):
            for tag in STRUCT_BODY.findall(code):
                headers.setdefault(tag, set()).add(path)
    return names, headers


def incomplete_consts(path, code, structs):
    """(line, message) for each const pointer, in a parameter list
    outside a function body or in a typedef's declarator of the blanked
    code of path, to a struct still incomplete there; structs is the
    struct_index of the run."""
    names, headers = structs
    found = []
    done = set()
    blocks = []                 # per open brace: True for a struct body
    parens = 0
    body = typedef = init = False
    for m in DECL_TOKEN.finditer(code):
        kind = m.lastgroup
        top = not blocks
        if kind == 'lb':
            blocks.append(body)
            body = False
        elif kind == 'rb' and blocks:
            blocks.pop()
        elif kind == 'lp':
            parens += 1
        elif kind == 'rp':
            parens -= 1
        elif kind == 'semi' and top:
            typedef = init = False
        elif kind == 'eq' and top:
            init = True
        elif kind == 'typedef' and top:
            typedef = True
        elif kind == 'body':
            body = True
            done.update(m.group(kind).split()[1:])
        elif kind in ('pre', 'post'):
            words = m.group(kind).split()
            tag = words[-1] if words[0] == 'struct' else names.get(words[0])
            if (tag and tag not in done and
                    not headers.get(tag, set()) - {path} and
                    all(blocks) and not init and
                    (parens or typedef and top)):
                found.append((code.count('\n', 0, m.start()) + 1,
                              'const pointer to incomplete struct %s in a '
                              'prototype or typedef' % tag))
    return found


def block_inits(code):
    """(line, message) for each initializer of a block-scope declaration
    but a static one in code, its comments and literals blanked."""
    code = PP_LINE.sub(lambda m: re.sub(r'[^\n]', ' ', m.group()), code)
    found = []
    depth = 0
    start = 0
    for m in STMT_END.finditer(code):
        stmt = code[start:m.start()]
        eq = INIT_EQ.search(stmt)
        if (depth and eq and DECL_STMT.match(stmt) and
                stmt.split()[0] != 'static'):
            found.append((code.count('\n', 0, start + eq.start()) + 1,
                          'initializer in a block-scope declaration'))
        depth += {'{': 1, '}': -1}.get(m.group(), 0)
        start = m.end()
    return found


def lint(path, src, compat, win, users, structs):
    """Print what is forbidden in the source path, src being what load
    gave for it, under the --compat rules when compat is true, with the
    Windows keyword rule when win is; return how many, or None when
    TXNUM.TXT cannot be read."""
    text, (code, lits, errs), joined = src
    bad = 0
    if os.path.basename(path).upper() == 'TXTAB.H':
        bad = check_txnum(path, text)
        if bad is None:
            return None
    raw_lines = text.split('\n')
    found = [(num, what) for num, kind, what in errs
             if not compat or kind == 'comment']
    if not wattcp_user(path, users):
        for num, raw in enumerate(raw_lines, 1):
            if WATTCP_INCLUDE.search(raw):
                found.append((num, 'WATTCP header in a file not listed '
                                   'by --wattcp'))
    found += private_found(path, raw_lines)
    for num, raw in enumerate(raw_lines, 1):
        if len(raw.rstrip('\r').expandtabs()) > MAX_COLUMNS:
            found.append((num, 'line over %d columns' % MAX_COLUMNS))
    for m in NEG_SIZE.finditer(joined):
        found.append((joined.count('\n', 0, m.start()) + 1,
                      'negative array size as a compile-time check'))
    found += incomplete_consts(path, joined, structs)
    found += block_inits(joined)
    for num, src in enumerate(code, 1):
        if ELIF.search(src):
            found.append((num, '#elif'))
        if not compat and VOID_STMT.search(src):
            found.append((num, '(void)x; statement'))
        for rx, what in WIN_RULES if win else ():
            if rx.search(src):
                found.append((num, what))
    if compat:
        for num, what in sorted(found):
            print('%s:%d: %s' % (path, num, what))
        return len(found)
    for num, src in enumerate(code, 1):
        for rx, what in CODE_RULES:
            if rx.search(src):
                found.append((num, what))
    for num, raw in enumerate(raw_lines, 1):
        if INCLUDE_RULE.search(raw):
            found.append((num, 'compiler-specific header'))
    if any(NULL_RX.search(ln) for ln in code) and not STDDEF_RX.search(text):
        found.append((1, 'NULL used without #include <stddef.h>'))
    lit_by_line = {}
    for num, body in lits:
        lit_by_line.setdefault(num, []).append(body)
    allowed = prose_lines(path, code)
    for num, body in lits:
        words = PRINTF_RX.sub('', ESCAPE_RX.sub('', body))
        if num not in allowed and LOWER_RX.search(words):
            found.append((num, 'user-visible text outside the message '
                               'files'))
    for num, src in enumerate(code, 1):
        if FOPEN.search(src):
            # The mode argument may sit on the line after the call.
            for body in (lit_by_line.get(num, []) +
                         lit_by_line.get(num + 1, [])):
                if COMMIT_MODE.match(body):
                    found.append((num, 'fopen commit mode "c"'))
    for num, what in sorted(found):
        print('%s:%d: %s' % (path, num, what))
    return bad + len(found)


# The screen-test programs whose th_scripts tables --scripts checks, by
# script-name prefix (the table is TESTDIR/T_<prefix>.C); the report test,
# whose string literals are the names of its goldens, and their prefix.
SCRIPT_TESTS = ('SCRE', 'SCRR')
REPORT_TEST = 'T_RPT.C'
REPORT_PREFIX = 'RPT'

COMMENT_RX = re.compile(r'/\*.*?\*/', re.S)
# A th_scripts row: { USER, { commands }, flags, ...; row n (from 1) is
# the script named the test's prefix and n.  Every row opens with
# ROW_START, matched once the string literals are blanked (LITERAL_RX);
# one that SCRIPT_ROW does not match is reported, not skipped.
ROW_START = re.compile(r'\{\s*[A-Z]{2}\s*,')
LITERAL_RX = re.compile(r'"(?:[^"\\\n]|\\.)*"')
SCRIPT_ROW = re.compile(r'\{\s*[A-Z]{2}\s*,\s*'
                        r'\{[^{}]*\}\s*,\s*([^,]+),')
# An outfiles row with a golden: { "NAME", "FILE", ".EXT" }
OUTFILE_ROW = re.compile(r'\{\s*"([A-Z]+[0-9]+)"\s*,\s*"[^"]*"\s*,\s*'
                         r'"(\.[A-Z0-9]+)"\s*\}')
REPORT_NAME = re.compile(r'"([A-Z]+[0-9]+\.[A-Z]+)"')


def table(text, name):
    """The initializer of the array name in the C source text, or ''
    when text defines no such array."""
    m = re.search(r'\b%s\[\]\s*=\s*\{(.*?)\n\};' % name, text, re.S)
    return m.group(1) if m else ''


def blank_literals(text):
    """text with the inside of each string literal blanked."""
    return LITERAL_RX.sub(lambda m: '"%s"' % (' ' * (len(m.group()) - 2)),
                          text)


def read_text(path):
    """The C source at path with its comments blanked."""
    with open(path, 'rb') as f:
        return COMMENT_RX.sub(' ', f.read().decode('latin-1'))


def file_names(path):
    """The upper-case names of the files in the directory path."""
    return [f.upper() for f in os.listdir(path)
            if os.path.isfile(os.path.join(path, f))]


def check_scripts(testdir, lists, monos):
    """Print every problem --scripts finds; return how many, or None on a
    read error."""
    keydir = os.path.join(testdir, 'KEYS')
    scrdir = os.path.join(testdir, 'GOLDEN', 'SCR')
    prndir = os.path.join(testdir, 'GOLDEN', 'PRT')
    rows = {}
    errs = []
    expect = {scrdir: set(), prndir: set()}
    mono = {}
    try:
        for prefix in SCRIPT_TESTS:
            path = os.path.join(testdir, 'T_%s.C' % prefix)
            text = read_text(path)
            scripts = table(text, 'th_scripts')
            names = []
            starts = [m.start() for m in
                      ROW_START.finditer(blank_literals(scripts))]
            for num, at in enumerate(starts, 1):
                m = SCRIPT_ROW.match(scripts, at)
                if not m:
                    errs.append('%s: th_scripts row %d not understood'
                                % (path, num))
                    continue
                name = '%s%d' % (prefix, num)
                flags = m.group(1)
                names.append(name)
                expect[scrdir].add(name + '.SNC')
                if 'SF_MONO' in flags:
                    expect[scrdir].add(name + '.SNM')
                    mono.setdefault(prefix, []).append(name + '.SNM')
                if 'SF_PRN' in flags:
                    expect[prndir].add(name + '.PRN')
            for name, ext in OUTFILE_ROW.findall(table(text, 'outfiles')):
                expect[prndir].add(name + ext)
            if not names:
                sys.stderr.write('lint: %s: no th_scripts rows\n' % path)
                return None
            rows[prefix] = (path, names)
        expect[prndir] |= set(REPORT_NAME.findall(
            read_text(os.path.join(testdir, REPORT_TEST))))
        present = dict((d, file_names(d)) for d in (keydir, scrdir, prndir))
    except OSError as e:
        sys.stderr.write('lint: %s\n' % e)
        return None
    for prefix, (path, names) in sorted(rows.items()):
        keys = [f[:-4] for f in present[keydir]
                if f.startswith(prefix) and f.endswith('.KEY')]
        for name in sorted(set(keys) - set(names)):
            errs.append('%s: no row in %s'
                        % (os.path.join(keydir, name + '.KEY'), path))
        for name in names:
            if name not in keys:
                errs.append('%s: %s has no key script in %s'
                            % (path, name, keydir))
    for d in (scrdir, prndir):
        mine = SCRIPT_TESTS + ((REPORT_PREFIX,) if d == prndir else ())
        for f in sorted(present[d]):
            if f.startswith(mine) and f not in expect[d]:
                errs.append('%s: compared by no test' % os.path.join(d, f))
    for num, words in enumerate(lists, 1):
        named = {}
        for w in words.split():
            b = os.path.basename(w.replace('\\', '/')).upper()
            for prefix in SCRIPT_TESTS:
                if b.startswith(prefix) and b.endswith('.KEY'):
                    named.setdefault(prefix, []).append(b[:-4])
        if not named:
            errs.append('script list %d: no key script' % num)
        for prefix, got in sorted(named.items()):
            path, names = rows[prefix]
            missing = [name for name in names if name not in got]
            if missing:
                errs.append('script list %d: %d row(s) of %s without their '
                            'key script, first %s' % (num, len(missing), path,
                                                      missing[0]))
            for name in names:
                if got.count(name) > 1:
                    errs.append('script list %d: %s.KEY %d times'
                                % (num, name, got.count(name)))
            for name in sorted(set(got) - set(names)):
                errs.append('script list %d: %s.KEY has no row in %s'
                            % (num, name, path))
    for prefix, words in zip(SCRIPT_TESTS, monos):
        path = rows[prefix][0]
        want = mono.get(prefix, [])
        got = [os.path.basename(w.replace('\\', '/')).upper()
               for w in words.split()]
        got = [b for b in got if b.endswith('.SNM')]
        for b in want:
            if not got.count(b):
                errs.append('mono list of %s: lacks %s' % (path, b))
            elif got.count(b) > 1:
                errs.append('mono list of %s: %s %d times'
                            % (path, b, got.count(b)))
        for b in sorted(set(got) - set(want)):
            errs.append('mono list of %s: %s, the golden of no SF_MONO row'
                        % (path, b))
    for e in errs:
        print(e)
    return len(errs)


def check_kernel(prep, names):
    """Print every difference between names and the KERNEL list of the
    script prep; return how many, or None when prep cannot be read or
    assigns no KERNEL."""
    try:
        with open(prep, 'rb') as f:
            tree = ast.parse(f.read(), prep)
        kernel = [ast.literal_eval(n.value) for n in tree.body
                  if isinstance(n, ast.Assign) and
                  [getattr(t, 'id', '') for t in n.targets] == ['KERNEL']]
    except (OSError, SyntaxError, ValueError) as e:
        sys.stderr.write('lint: %s: %s\n' % (prep, e))
        return None
    if len(kernel) != 1:
        sys.stderr.write('lint: %s: no single KERNEL list\n' % prep)
        return None
    errs = ['kernel: %s is not in KERNEL of %s' % (n, prep)
            for n in sorted(set(names) - set(kernel[0]))]
    errs += ['kernel: %s of KERNEL of %s is not named' % (n, prep)
             for n in sorted(set(kernel[0]) - set(names))]
    errs += ['kernel: %s named %d times' % (n, names.count(n))
             for n in sorted(set(names)) if names.count(n) > 1]
    for e in errs:
        print(e)
    return len(errs)


# --docs: the length in characters a paragraph, item or row and a line
# must reach to be compared, and the number of such lines in a row that
# make a repeat.  The table headers and field rows the documents repeat on
# purpose stay well below DOC_UNIT and DOC_RUN; a paragraph pasted again
# is caught once it reaches DOC_UNIT, or wraps to DOC_RUN lines.
DOC_UNIT = 60
DOC_LINE = 20
DOC_RUN = 3
DOC_FENCE = re.compile(r'^[ \t]*```[^`]*$')
DOC_ITEM = re.compile(r'^[ \t]*([-*]|\d+\.)[ \t]')
DOC_ALONE = re.compile(r'^[ \t]*[|#]')
DOC_RULE = re.compile(r'^[ \t|:=+-]*$')
DOC_BLANKS = re.compile(r'[ \t]+')


def doc_passages(text):
    """The passages of the document text: [first line, its words] for each
    paragraph, item, row or heading, (line, its words) for each line of
    DOC_LINE characters or more, and the line of a code fence left open,
    else 0."""
    units = []
    lines = []
    fence = 0                   # the line of the open code fence, or 0
    joins = False               # the line before continues on this one
    for num, raw in enumerate(text.split('\n'), 1):
        mark = DOC_FENCE.match(raw)
        if mark:
            fence = 0 if fence else num
        if fence or mark or DOC_RULE.match(raw):
            joins = False
            continue
        words = DOC_BLANKS.sub(' ', raw).strip(' ')
        alone = DOC_ALONE.match(raw)
        if joins and not alone and not DOC_ITEM.match(raw):
            units[-1][1] += ' ' + words
        else:
            units.append([num, words])
        joins = not alone
        if len(words) >= DOC_LINE:
            lines.append((num, words))
    return units, lines, fence


def check_docs(texts):
    """Print each passage the documents texts, (path, text) pairs, repeat,
    within one or across two of one extension, and each code fence left
    open; return how many."""
    found = []                  # (path, line, what)
    kinds = {}
    for path, text in texts:
        kinds.setdefault(os.path.splitext(path)[1].lower(), []).append(
            (path, text))
    for docs in kinds.values():
        units = []              # (path, first line, its words)
        lines = []              # (path, line, its words), the long ones
        for path, text in docs:
            u, ln, fence = doc_passages(text)
            units += [(path, num, words) for num, words in u]
            lines += [(path, num, words) for num, words in ln]
            if fence:
                found.append((path, fence,
                              'code fence opened here is not closed'))
        runs = []               # [start, its first, windows]: in lines
        first = {}
        for i in range(len(lines) - DOC_RUN + 1):
            if lines[i][0] != lines[i + DOC_RUN - 1][0]:
                continue
            j = first.setdefault(
                tuple(w for _, _, w in lines[i:i + DOC_RUN]), i)
            if j == i:
                continue
            if (runs and runs[-1][0] + runs[-1][2] == i and
                    runs[-1][1] + runs[-1][2] == j):
                runs[-1][2] += 1
            else:
                runs.append([i, j, 1])
        spans = []              # (path, first, last), a repeat each
        for i, j, n in runs:
            k = n + DOC_RUN - 2
            path, a, b = lines[i][0], lines[i][1], lines[i + k][1]
            other = '' if lines[j][0] == path else lines[j][0] + ' '
            spans.append((path, a, b))
            found.append((path, a, 'lines %d-%d repeat %slines %d-%d' %
                          (a, b, other, lines[j][1], lines[j + k][1])))
        seen = {}
        for path, num, words in units:
            if len(words) < DOC_UNIT:
                continue
            p, j = seen.setdefault(words, (path, num))
            if (p, j) == (path, num) or any(
                    s == path and a <= num <= b for s, a, b in spans):
                continue
            found.append((path, num, 'repeats the passage of %sline %d' %
                          ('' if p == path else p + ' ', j)))
    for path, num, what in sorted(found):
        print('%s:%d: %s' % (path, num, what))
    return len(found)


# --refs: a reference to a document, a directory before it or not (none
# starting with a dot, so that a path through .. is never read from the
# root); what may follow it, past its closing backtick and a Markdown
# link's parenthesis, to name a label of it (", Label", " (Label)" or a
# few words, then ' in "Label"'); what may precede it to name one ('"Label"
# in ' or "(Label of ", the document then closing the parenthesis); a
# label's start (a quote, a capital or a digit); the joint to a further
# label of the same reference; where the name of a label not found ends;
# the leader of a comment line outside a Markdown document; a
# document's own labels (a heading, a bold label, the first cell of a
# table row) and the extra after a heading's name; a document's reference
# to a label of its own, after a lead-in ending in ": " or not; and the
# most text around a reference that its labels may take.
REF_DOC = re.compile(r'(?<![\w/.-])((?:[\w-][\w.-]*/)*(?:DOC/\w+|README)'
                     r'\.md)\b')
REF_TAIL = re.compile(r'`?(?:, |\)? \(| (?:\w+ ){0,3}in (?="))')
REF_LEAD = re.compile(r'(?:"([^"]+)" in|\(([A-Z0-9][^()"]*?) of) `?$')
REF_CLOSE = re.compile(r'`?\)')
REF_START = re.compile(r'"|[A-Z0-9]')
REF_MORE = re.compile(r'(?:, (?:and )?| and )(?=[A-Z0-9"])')
REF_END = re.compile(r'[);:|"]|\. ')
REF_LEADER = re.compile(r'^[ \t]*(?:[*#;]+|//)?[ \t]*')
REF_HEADING = re.compile(r'^#+[ \t]+(.*?)[ \t#]*$')
REF_BOLD = re.compile(r'\*\*([^*]+)\*\*')
REF_CELL = re.compile(r'^\|([^|]+)\|')
REF_EXTRA = re.compile(r' \(.*')
REF_HERE = re.compile(r'\((?:[^():]*: )?([A-Z0-9][^():]*?), (above|below)\)')
REF_SPAN = 240


def ref_key(text):
    """text as a label is compared: no backticks, blanks collapsed, no
    closing stop or colon, in lower case."""
    return DOC_BLANKS.sub(' ', text.replace('`', '')).strip(' .:').lower()


def doc_labels(text):
    """The labels of the document text, outside code fences: a dict of
    each, as ref_key gives it, to the numbers of its lines."""
    labels = {}
    fence = False
    for num, raw in enumerate(text.split('\n'), 1):
        if DOC_FENCE.match(raw):
            fence = not fence
        if fence:
            continue
        found = REF_BOLD.findall(raw)
        for rx in (REF_HEADING, REF_CELL):
            m = rx.match(raw)
            if m:
                found.append(m.group(1))
        for label in found:
            for key in {ref_key(label), ref_key(REF_EXTRA.sub('', label))}:
                labels.setdefault(key, []).append(num)
    return labels


def ref_names(after, labels, closed):
    """The labels a reference names, given the text after it, that labels,
    a dict as doc_labels returns, lacks: a quoted label whole, else each
    label and its continuations, the longest label the text starts with
    each time, until prose follows, which closed, true when the reference
    opened a parenthesis, allows only as its closing one.  Text starting
    with a digit is prose when no label does."""
    if after.startswith('"'):
        name = after[1:].split('"', 1)[0]
        return [] if ref_key(name) in labels else [name]
    text = after.replace('`', '')
    low = text.lower()
    at = 0
    while True:
        size = max([len(k) for k in labels if low.startswith(k, at) and
                    not low[at + len(k):at + len(k) + 1].isalnum()] or [0])
        more = size and REF_MORE.match(text, at + size)
        if more:
            at = more.end()
            if text.startswith('"', at):
                return ref_names(text[at:], labels, closed)
        elif size and (not closed or text.startswith(')', at + size)):
            return []
        elif not size and low[at:at + 1].isdigit() and not any(
                k[:1].isdigit() for k in labels):
            return []
        else:
            return [REF_END.split(text[at:])[0]]


def check_refs(texts):
    """Print every reference of the files texts, (path, text) pairs, to a
    document that does not exist or to a label its document lacks, and
    every reference of a Markdown document to a label of its own that is
    not above or below it as it says; return how many."""
    docs = {}                   # path: its labels, None if missing
    found = 0
    for path, text in texts:
        md = path.lower().endswith('.md')
        if '\0' in text or not md and '.md' not in text:
            continue
        starts = []             # where each line starts in flat
        parts = []
        at = 0
        for line in text.split('\n'):
            line = DOC_BLANKS.sub(' ', line.strip() if md else
                                  REF_LEADER.sub('', line).rstrip())
            starts.append(at)
            parts.append(line)
            at += len(line) + 1
        flat = ' '.join(parts)
        problems = []           # (position in flat, what)
        for m in REF_DOC.finditer(flat):
            doc = m.group(1)
            if doc not in docs:
                try:
                    docs[doc] = doc_labels(read_file(doc))
                except OSError:
                    docs[doc] = None
            if docs[doc] is None:
                problems.append((m.start(), '%s does not exist' % doc))
                continue
            names = []
            lead = REF_LEAD.search(flat, max(0, m.start() - REF_SPAN),
                                   m.start())
            if lead and (lead.group(1) or REF_CLOSE.match(flat, m.end())):
                name = lead.group(1) or lead.group(2)
                names += [] if ref_key(name) in docs[doc] else [name]
            tail = REF_TAIL.match(flat, m.end())
            if (tail and REF_START.match(flat, tail.end()) and
                    not REF_DOC.match(flat, tail.end())):
                names += ref_names(flat[tail.end():tail.end() + REF_SPAN],
                                   docs[doc], tail.group().endswith('('))
            problems += [(m.start(), '%s has no "%s"' % (doc, name))
                         for name in names]
        own = doc_labels(text) if md else {}
        for m in REF_HERE.finditer(flat) if md else ():
            label, where = m.groups()
            names = [label] if ref_key(label) in own else label.split(', ')
            if all(ref_key(n) in own for n in names):
                num = bisect.bisect(starts, m.start())
                if any(n < num if where == 'above' else n > num
                       for n in own[ref_key(names[-1])]):
                    continue
            problems.append((m.start(), 'no "%s" %s' % (label, where)))
        for pos, what in problems:
            print('%s:%d: %s' % (path, bisect.bisect(starts, pos), what))
        found += len(problems)
    return found


# --names: a word that may be a program's name (not a source module's,
# followed by .C or .H), and the words in capitals that are neither.
NAME_WORD = re.compile(r'\bINV[A-Z0-9]*\b(?!\.[CH]\b)')
NOT_NAMES = ('INVALID', 'INVENTORY')


def check_names(path, text, thin, programs):
    """Print every word of the document path, its text, that looks like
    a program's name but is none of programs, and say so when it never
    names thin; return how many problems."""
    found = []
    if not re.search(r'\b%s\b' % re.escape(thin), text):
        found.append((1, 'never names the network-only client %s' % thin))
    known = programs | set(NOT_NAMES)
    for num, line in enumerate(text.split('\n'), 1):
        for word in NAME_WORD.findall(line):
            if word not in known:
                found.append((num, '%s is no program of the build' % word))
    for num, what in found:
        print('%s:%d: %s' % (path, num, what))
    return len(found)


# --strings: a string table of a resource script, one of its strings and
# a define of the size.
RC_TABLE = re.compile(r'^STRINGTABLE\b[^\n]*\nBEGIN\n(.*?)^END\b',
                      re.M | re.S)
RC_STRING = re.compile(r'\s*[A-Za-z_][\w ]*(?:\+[\w ]+)?,\s*'
                       r'"((?:[^"\\\n]|\\.|"")*)"\s*$')


def check_strings(rc, src, name):
    """Print every string of the tables of the resource script rc longer
    than the size name of the source src leaves it, and every line of a
    table that holds no single string; return how many, or None on a
    read error."""
    try:
        text = read_file(rc)
        size = re.search(r'^#define\s+%s\s+(\d+)\b' % re.escape(name),
                         read_file(src), re.M)
    except OSError as e:
        sys.stderr.write('lint: %s\n' % e)
        return None
    if not size:
        print('%s: no "#define %s n"' % (src, name))
        return 1
    most = int(size.group(1)) - 1
    found = 0
    for table in RC_TABLE.finditer(text):
        num = text.count('\n', 0, table.start(1)) + 1
        for line in table.group(1).split('\n')[:-1]:
            m = RC_STRING.match(line)
            n = len(ESCAPE_RX.sub('x', m.group(1).replace('""', '"'))) \
                if m else 0
            if not m and line.strip():
                print('%s:%d: not one string of the table' % (rc, num))
                found += 1
            elif n > most:
                print('%s:%d: %d bytes, more than the %d of %s (%s)'
                      % (rc, num, n, most, name, src))
                found += 1
            num += 1
    return found


def usage(reason):
    """Write reason and the usage lines on standard error; return 2."""
    sys.stderr.write('lint: %s\n%s\n' % (reason, __doc__.split('\n\n')[1]))
    return 2


def main(args):
    if args and args[0] == '--scripts':
        if len(args) < 2:
            return usage('--scripts without TESTDIR')
        lists = args[2:]
        monos = []
        if '--mono' in lists:
            at = lists.index('--mono')
            lists, monos = lists[:at], lists[at + 1:]
            if len(monos) != len(SCRIPT_TESTS):
                return usage('--mono without one MONO per screen test')
        r = check_scripts(args[1], lists, monos)
        if r is None:
            return 2
        if r:
            sys.stderr.write('lint: %d script table problem(s)\n' % r)
            return 1
        return 0
    if args and args[0] == '--kernel':
        if len(args) < 3:
            return usage('--kernel without PREP and a NAME')
        r = check_kernel(args[1], args[2:])
        if r is None:
            return 2
        if r:
            sys.stderr.write('lint: %d kernel list problem(s)\n' % r)
            return 1
        return 0
    if args and args[0] == '--docs':
        docs, refs = args[1:], []
        if '--refs' in docs:
            at = docs.index('--refs')
            docs, refs = docs[:at], docs[at + 1:]
        if not docs:
            return usage('--docs without a FILE')
        try:
            texts = [(path, read_file(path)) for path in docs]
            refs = [(path, read_file(path)) for path in refs
                    if path not in docs]
        except OSError as e:
            sys.stderr.write('lint: %s\n' % e)
            return 2
        bad = check_docs(texts) + check_refs(texts + refs)
        if bad:
            sys.stderr.write('lint: %d document problem(s)\n' % bad)
            return 1
        return 0
    if args and args[0] == '--names':
        if len(args) < 4:
            return usage('--names without THIN, PROGRAMS and a FILE')
        programs = set(args[2].split())
        try:
            texts = [(path, read_file(path)) for path in args[3:]]
        except OSError as e:
            sys.stderr.write('lint: %s\n' % e)
            return 2
        bad = sum(check_names(path, text, args[1], programs)
                  for path, text in texts)
        if bad:
            sys.stderr.write('lint: %d program name problem(s)\n' % bad)
            return 1
        return 0
    if args and args[0] == '--strings':
        if len(args) != 4:
            return usage('--strings without RC, SRC and NAME')
        r = check_strings(args[1], args[2], args[3])
        if r is None:
            return 2
        if r:
            sys.stderr.write('lint: %d string table problem(s)\n' % r)
            return 1
        return 0
    users = ()
    if len(args) > 1 and args[0] == '--wattcp':
        users = tuple(u.upper() for u in args[1].split())
        args = args[2:]
    win = bool(args) and args[0] == '--win'
    compat = win or bool(args) and args[0] == '--compat'
    if compat:
        args = args[1:]
    if not args:
        return usage('no FILE')
    srcs = {}
    try:
        for path in args:
            srcs[path] = load(path)
        codes = dict((p, src[2]) for p, src in srcs.items())
        for d in set(os.path.dirname(p) for p in args):
            for f in os.listdir(d or '.'):
                h = os.path.join(d, f)
                if f.upper().endswith('.H') and h not in codes:
                    codes[h] = load(h)[2]
    except OSError as e:
        sys.stderr.write('lint: %s\n' % e)
        return 2
    structs = struct_index(codes)
    bad = 0
    for path in args:
        r = lint(path, srcs[path], compat, win, users, structs)
        if r is None:
            return 2
        bad += r
    if bad:
        sys.stderr.write('lint: %d forbidden construct(s)\n' % bad)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
