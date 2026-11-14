"""dlgchk.py - every dialog procedure of the Windows programs starts
with the grey 3D look's head (SRC/WIN/WDLG.H).

usage: python3 -I TEST/dlgchk.py [file.C ...]

A dialog procedure is a definition BOOL WCALLBACK name(HWND d, UINT m,
WPARAM w, LPARAM l) { ... } in SRC/WIN/*.C but SETUP*.C (or in the files
named).  SETUP, the Windows client's installer, links none of the
client's modules, WDLG.C among them, to stay small beside it on the
release disks, so its one dialog keeps Windows' own look.
Its first statement after its declarations, comments left out, must
be

    if (WD_LOOKS(m))
        return wd_look(d, m, w, l);

its own parameters in that order, so that every dialog answers
WM_CTLCOLOR and WM_PAINT with the look before anything else it does.
A prototype (one ending in ';') is passed over; any other BOOL
WCALLBACK that cannot be read so fails, as does a scan finding none.
Exit status 1, naming each procedure that fails; else 0 with the count.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

COMMENT = re.compile(r'/\*.*?\*/', re.S)
HEAD = re.compile(r'\bBOOL\s+WCALLBACK\b')
SIG = re.compile(r'\s*(\w+)\s*\(([^)]*)\)\s*([;{])')
PARAM = re.compile(r'^\s*\w+\s+(\w+)\s*$')
KEYWORDS = {'if', 'return', 'switch', 'while', 'for', 'do', 'goto',
            'break', 'continue', 'case', 'default', 'else'}
DECL = re.compile(r'^(?:(?:static|const|register|unsigned|signed|struct)'
                  r'\s+)*([A-Za-z_]\w*)[\s*]+[A-Za-z_]\w*'
                  r'(?:\s*\[[^\]]*\])?(?:\s*=[^;]*)?'
                  r'(?:\s*,[\s*]*[A-Za-z_]\w*(?:\s*\[[^\]]*\])?'
                  r'(?:\s*=[^;]*)?)*\s*;')


def first_statement(body):
    """The first statement of body, the text after a definition's '{',
    its declarations passed over; whitespace squeezed out."""
    rest = body.lstrip()
    while True:
        m = DECL.match(rest)
        if not m or m.group(1) in KEYWORDS:
            break
        rest = rest[m.end():].lstrip()
    depth = 0
    for i, ch in enumerate(rest):
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        elif ch in ';{}' and depth == 0:
            return re.sub(r'\s+', '', rest[:i + 1])
    return re.sub(r'\s+', '', rest)


def check(path):
    """The procedures of path and why each fails, as (count, [why])."""
    with open(path, encoding='latin-1') as f:
        text = COMMENT.sub(' ', f.read())
    name = os.path.relpath(path, ROOT)
    count = 0
    bad = []
    for h in HEAD.finditer(text):
        m = SIG.match(text, h.end())
        if not m:
            bad.append('%s: a BOOL WCALLBACK that is not read' % name)
            continue
        if m.group(3) == ';':
            continue
        count += 1
        proc = m.group(1)
        params = [PARAM.match(p) for p in m.group(2).split(',')]
        if len(params) != 4 or not all(params):
            bad.append('%s: %s: not (HWND, UINT, WPARAM, LPARAM)'
                       % (name, proc))
            continue
        d, wm, wp, lp = [p.group(1) for p in params]
        want = 'if(WD_LOOKS(%s))returnwd_look(%s,%s,%s,%s);' % (
            wm, d, wm, wp, lp)
        got = first_statement(text[m.end():])
        if got != want:
            bad.append('%s: %s starts with %s, not the WD_LOOKS head'
                       % (name, proc, got[:60] or 'nothing'))
    return count, bad


def main(argv):
    files = argv[1:] or sorted(
        f for f in glob.glob(os.path.join(ROOT, 'SRC', 'WIN', '*.C'))
        if not os.path.basename(f).startswith('SETUP'))
    total = 0
    bad = []
    for path in files:
        n, why = check(path)
        total += n
        bad += why
    if total == 0:
        bad.append('no dialog procedure found')
    for why in bad:
        print('dlgchk: ' + why)
    if bad:
        return 1
    print('dlgchk: %d dialog procedures start with the WD_LOOKS head'
          % total)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
