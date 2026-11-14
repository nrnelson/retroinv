"""tabchk.py - the tab strip (WTAB.H) in its test's snapshots, against
states worked out apart from the program.

usage: python3 -I TEST/tabchk.py

TEST/WTABT.KEY drives a dialog holding the strip, an Edit and a Static
"SEEN n CUR c[ RECT g]", n the WTN_CHANGE notifications the dialog has
had and c the current tab the last of them or the last WTM_SETCUR
answered (-1 for no such tab), g what WTM_GETRECT answered for that
tab when the program asked it too (0 for none).  Each case names a
snapshot of TEST/GOLDEN/WIN/WTABT.SNP and the strip's tabs, its current
tab, whether it has the focus (else the Edit has it), n, c and, where
the Static says it, g.  The snapshot must hold exactly one line of each
control below the dialog, these two:

 WTAB id "tabs" CUR cur[ FOCUS]
 Static id "SEEN n CUR c[ RECT g]"

and its one focus window must be the strip or the Edit.  The control
IDs are the program's, so each is taken from snapshot START and held to
in every other.  A golden or snapshot missing, or a line missing,
repeated or different, fails.  Exit status 1 if any case fails.
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_SPEC = importlib.util.spec_from_file_location(
    'colchk', os.path.join(HERE, 'colchk.py'))
colchk = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(colchk)

GOLDEN = 'WTABT'
TABS4 = 'Overview|Technical|Ac&quisition|Notes'
TABS2 = 'One|Two'
# Thirteen tabs in more than WT_MAXTEXT (64) bytes, of which the first
# WT_MAXTABS (8), Zero to Seven, count.
TABS13 = ('Zero|One|Two|Three|Four|Five|Six|Seven|Eight|Nine|Ten|Eleven|'
          'Twelve')
STRIP, EDIT = True, False

# Each snapshot of WTABT.KEY in order: its name, the tabs, the current
# tab, the focus, the notifications, the Static's current tab and, where
# the Static says it, WTM_GETRECT's answer.
CASES = (
    ('START', TABS4, 0, EDIT, 0, 0),
    # Ctrl+Tab from the Edit, then wrapping from the last tab.
    ('CTAB', TABS4, 1, EDIT, 1, 1),
    ('CTAB3', TABS4, 3, EDIT, 3, 3),
    ('CTABWRAP', TABS4, 0, EDIT, 4, 0),
    # Ctrl+Shift+Tab wrapping from the first tab, then back one.
    ('CSTABWRP', TABS4, 3, EDIT, 5, 3),
    ('CSTAB', TABS4, 2, EDIT, 6, 2),
    # Left in the Edit leaves the strip alone.
    ('EDITLEFT', TABS4, 2, EDIT, 6, 2),
    # Shift+Tab focuses the strip; Right and Left stop at either end.
    ('FOCUS', TABS4, 2, STRIP, 6, 2),
    ('RIGHT', TABS4, 3, STRIP, 7, 3),
    ('RIGHTEND', TABS4, 3, STRIP, 7, 3),
    ('LEFT0', TABS4, 0, STRIP, 10, 0),
    ('LEFTEND', TABS4, 0, STRIP, 10, 0),
    ('CTABFOC', TABS4, 1, STRIP, 11, 1),
    ('TAB', TABS4, 1, EDIT, 11, 1),
    # Alt+Q the marked mnemonic, Alt+T a first letter; the current
    # tab's, A and Z send nothing.
    ('ALTQ', TABS4, 2, EDIT, 12, 2),
    ('ALTT', TABS4, 1, EDIT, 13, 1),
    ('ALTTCUR', TABS4, 1, EDIT, 13, 1),
    ('ALTA', TABS4, 1, EDIT, 13, 1),
    ('ALTZ', TABS4, 1, EDIT, 13, 1),
    # Clicks from the Edit: tab 2, the current tab, outside the tabs.
    ('CLICK2', TABS4, 2, STRIP, 14, 2),
    ('TAB2', TABS4, 2, EDIT, 14, 2),
    ('CLICKCUR', TABS4, 2, STRIP, 14, 2),
    ('TAB3', TABS4, 2, EDIT, 14, 2),
    ('CLICKOUT', TABS4, 2, STRIP, 14, 2),
    # WTM_SETCUR 3, then 4, one past the last tab, with WTM_GETRECT on
    # 4: no tab, no rectangle; then two tabs, tab 3 becoming 1: no
    # notice.
    ('SETCUR', TABS4, 3, STRIP, 14, 3),
    ('SETNONE', TABS4, 3, STRIP, 14, -1, 0),
    ('TEXT', TABS2, 1, STRIP, 14, -1, 0),
    # The new tabs: Ctrl+Tab wraps from the last, Alt+T is Two's.
    ('TEXTCTAB', TABS2, 0, STRIP, 15, 0),
    ('TEXTALTT', TABS2, 1, STRIP, 16, 1),
    # Thirteen tabs: tab 1 stays, without a notice; Ctrl+Shift+Tab goes
    # to tab 0, then wraps to the eighth, tab 7, the last that counts,
    # and Ctrl+Tab wraps from it to tab 0.
    ('MANY', TABS13, 1, STRIP, 16, 1),
    ('MANY0', TABS13, 0, STRIP, 17, 0),
    ('MANYWRAP', TABS13, 7, STRIP, 18, 7),
    ('MANYCTAB', TABS13, 0, STRIP, 19, 0),
    # Ctrl+PgUp and Ctrl+PgDn as Ctrl+Shift+Tab and Ctrl+Tab: wrapping
    # from tab 0 to tab 7 and back, then on to tab 1.
    ('PGUPWRAP', TABS13, 7, STRIP, 20, 7),
    ('PGDNWRAP', TABS13, 0, STRIP, 21, 0),
    ('PGDN', TABS13, 1, STRIP, 22, 1),
)

WTAB_RE = re.compile(r'^ WTAB (\d+) ')
SEEN_RE = re.compile(r'^ Static (\d+) "SEEN ')
STATES = frozenset(('DISABLED', 'HIDDEN', 'ACTIVE', 'FOCUS', 'CAPTURE',
                    'CHECKED', 'MIXED', 'DEFAULT', 'SELECTED', 'DROP',
                    'LONG', 'OVERLAP', 'CUT'))


def ids(lines):
    """The strip's and the Static's IDs in lines, or why not."""
    strip = [m.group(1) for m in map(WTAB_RE.match, lines) if m]
    seen = [m.group(1) for m in map(SEEN_RE.match, lines) if m]
    if len(strip) != 1 or len(seen) != 1:
        return 'START: %d WTAB and %d SEEN lines, want 1 each' % (
            len(strip), len(seen))
    return strip[0], seen[0]


def one(lines, rx, want, what):
    """None when lines hold exactly one line matching rx, want, else
    why not."""
    got = [ln for ln in lines if rx.match(ln)]
    if got == [want]:
        return None
    return '%s\n  want %s\n  got  %s' % (
        what, want, '\n       '.join(got) if got else '(none)')


def focus(lines):
    """The classes of the lines whose state words, those of TDRV.H
    ending the line, hold FOCUS."""
    out = []
    for ln in lines:
        words = ln.split()
        st = set()
        while len(words) > 1 and words[-1] in STATES:
            st.add(words.pop())
        if 'FOCUS' in st:
            out.append(words[0])
    return out


def check(case, snaps, sid, tid):
    """The reasons case fails, none when it holds."""
    name, tabs, cur, strip, seen, scur = case[:6]
    rect = '' if len(case) < 7 else ' RECT %d' % case[6]
    lines = snaps.get(name)
    if lines is None:
        return ['no snapshot %s' % name]
    bad = [
        one(lines, WTAB_RE, ' WTAB %s "%s" CUR %d%s' % (
            sid, tabs, cur, ' FOCUS' if strip else ''), 'the strip'),
        one(lines, SEEN_RE, ' Static %s "SEEN %d CUR %d%s"' % (
            tid, seen, scur, rect), 'the notifications')]
    want = 'WTAB' if strip else 'Edit'
    got = focus(lines)
    if got != [want]:
        bad.append('the focus: want %s, got %s' % (
            want, ' '.join(got) if got else 'none'))
    return ['%s: %s' % (name, why) for why in bad if why]


def main():
    snaps = colchk.snaps(GOLDEN)
    if snaps is None:
        bad = ['%s.SNP: cannot read it' % GOLDEN]
    elif 'START' not in snaps:
        bad = ['%s.SNP: no snapshot START' % GOLDEN]
    else:
        got = ids(snaps['START'])
        if isinstance(got, str):
            bad = ['%s.SNP %s' % (GOLDEN, got)]
        else:
            bad = ['%s.SNP %s' % (GOLDEN, why) for c in CASES
                   for why in check(c, snaps, got[0], got[1])]
    for why in bad:
        print('tabchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('tabchk: %d cases ok' % len(CASES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
