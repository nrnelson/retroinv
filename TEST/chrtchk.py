"""chrtchk.py - the tool bar (WTOOL.H), the status bar (WSTAT.H), the
splitter (WSPLIT.H) and the preview panel (WPREV.H) in their test's
snapshots, against states worked out apart from the program.

usage: python3 -I TEST/chrtchk.py

TEST/WCHRT.KEY drives a frame holding, in this order, the tool bar with
an Edit in its slot, an Edit holding the focus, the Static "SEEN n CMD c
CODE k" (n the WM_COMMANDs the frame has had, c the last one's wParam, k
its notification code, HIWORD of lParam), the Static "ANS ..." (the last
answer of the bars' API that a key asked for, or the splitter's state),
the status bar, the splitter (SRC/WIN/WSPLIT.H) and the preview panel
(SRC/WIN/WPREV.H).  Each step below
names a snapshot of TEST/GOLDEN/WIN/WCHRT.SNP and what its key changed;
every snapshot must be exactly the frame's nine lines of that state and
the panel's PV lines, so
a stray line (a button of the bar written), a state word gained or lost
(FOCUS taken from the Edit but by a drag held open, which gives the
splitter FOCUS and CAPTURE, or by a click on the panel, OVERLAP, the
status bar's CUT while a panel
holds the long digits cut to its room, or a panel but the last holds
them) or a text
differing fails, as does a snapshot missing, an extra one or a golden
not ending in =END.  The preview panel's lines are the program's
fields and notes, a label cut to WPV_LABELW characters and a value or a
note to WPV_VALW; its scroll bar (VBAR pos max) depends on the test
machine's font, so the most rows it scrolls, max, is read from PVFILL,
where it must be at least WCH_THUMB, and from PVNARROW, where the panel
is narrower and it must be more, and each step says where its top row
must then be.  The control IDs, the commands and the splitter's
places and the limits SHIFT F1 sets are TEST/WCHRT.H's; the limits it
is made with, 0 and its parent's client width less its own, are read
from START and must hold until then; the slot's rectangle and the panels'
rooms are worked out from the constants of SRC/WIN/WTOOL.H and
SRC/WIN/WSTAT.H.  The point of Open's
glyph that the PIXEL step reads, PIXEL, must be black in its art
(SRC/WIN/INVTOOL.PY) and dark grey in its grayed form.  Exit status 1 if
any step fails.
"""
import importlib.machinery
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True


def module(name, path):
    """The Python file at path, loaded as module name."""
    spec = importlib.util.spec_from_file_location(
        name, path, loader=importlib.machinery.SourceFileLoader(name, path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


colchk = module('colchk', os.path.join(HERE, 'colchk.py'))
invtool = module('invtool', os.path.join(HERE, '..', 'SRC', 'WIN',
                                         'INVTOOL.PY'))

GOLDEN = 'WCHRT'
DEFINE_RX = re.compile(r'^#define\s+(\w+)\s+(\d+|\'.\')\s')


def defines(*paths):
    """The numbers and characters #defined in the headers paths."""
    out = {}
    for path in paths:
        with open(os.path.join(HERE, path), encoding='latin-1') as f:
            for line in f:
                m = DEFINE_RX.match(line)
                if m:
                    v = m.group(2)
                    out[m.group(1)] = v[1] if v[0] == "'" else int(v)
    return out


C = defines('WCHRT.H', os.path.join('..', 'SRC', 'WIN', 'WTOOL.H'),
            os.path.join('..', 'SRC', 'WIN', 'WSTAT.H'),
            os.path.join('..', 'SRC', 'WIN', 'WSPLIT.H'),
            os.path.join('..', 'SRC', 'WIN', 'WPREV.H'))

# The bar's pieces and its buttons, in order (the program's string
# WCH_S_TOOL); button k's command is WCH_CMD + k.
PIECES = ('New', 'Open', 'Edit', 'Delete', C['WTB_GAP'], 'Move', 'Install',
          'Lend', C['WTB_GAP'], C['WTB_SLOT'], 'Find')
BUTTONS = [p for p in PIECES if p not in (C['WTB_GAP'], C['WTB_SLOT'])]


def cmd(name):
    """The command of the button name."""
    return C['WCH_CMD'] + BUTTONS.index(name)


def slot(width):
    """The slot's rectangle, left top right bottom, when it is width
    pixels wide: each piece where the one before it ends, a button
    after a button a pixel sooner."""
    x = C['WTB_PADX']
    prev = None
    for p in PIECES:
        if p == C['WTB_SLOT']:
            top = C['WTB_PADY']
            return (x, top, x + width, top + C['WTB_BTNH'])
        if p == C['WTB_GAP']:
            x += C['WTB_SPACE']
        else:
            if prev in BUTTONS:
                x -= 1
            x += C['WTB_BTNW']
        prev = p
    raise ValueError('no slot')


def room(panel, panels):
    """The bytes a panel's text keeps in a bar of panels panels."""
    if panel < panels - 1:
        return C['WSB_MAXCELL']
    return C['WSB_MAXTEXT'] - (panels - 1) * (C['WSB_MAXCELL'] + 1)


# The program's 100-byte text, and the texts F7 sets in panels 0..3.
LONG = '0123456789' * 10
F7 = ('Local', 'ADMIN (Admin)', '125 items', 'Ready')
L1 = LONG[:room(0, 1)]
LCELL = LONG[:room(0, 4)]
LLAST = LONG[:room(3, 4)]
WIDE = 10                       # LONG's digits wider than a fixed panel


def ans(*nums):
    """The Static's text after an answer of nums."""
    return 'ANS ' + ' '.join('%d' % n for n in nums)


# The point of Open's glyph that PIXEL reads, (x, y) in the glyph: black
# where x + y is even, so that the grayed form draws it dark grey.  The
# program reads it at (WTB_BTNW - WTB_GLYPH) / 2 + x, (WTB_BTNH -
# WTB_GLYPH) / 2 + y of the button, (6, 5).  Its colours, GetPixel's in
# hex: black, then COLOR_GRAYTEXT as the VGA shows it.
PIXEL = (2, 2)
PIXELS = 'ANS 000000 808080'


def pixel_bad():
    """Why PIXEL is not such a point of Open's art, or None."""
    art = dict(invtool.GLYPHS)['Open'].split()
    x, y = PIXEL
    if art[y][x] != 'K' or (x + y) % 2:
        return 'Open glyph: (%d, %d) is %s, want K where x + y is even' % (
            x, y, art[y][x])
    return None


# The splitter's state that CTRL G, CTRL U, CTRL K and CTRL B say: a, 1
# when it held the mouse right after the key's press (0 without one); c,
# 1 when it holds the mouse now; f, 1 when it has the focus; the frame's
# colours at the guide's two places of CTRL G, GetPixel's in hex: the
# frame's white, black where the inverted guide lies.
WHITE = 'FFFFFF'
BLACK = '000000'


def guide(a, c, f, p1, p2):
    """The Static's text of a splitter state."""
    return 'ANS %d %d %d %s %s' % (a, c, f, p1, p2)


def moved(n, at):
    """The change a drag of the splitter to at makes: its left edge, and
    the frame's n-th WM_COMMAND, its id with WSPN_MOVED."""
    return {'at': at, 'seen': (n, C['WCH_SPLIT'], C['WSPN_MOVED'])}


# The preview panel's lines after CTRL P, as the snapshot writes them:
# the program's fields, a label and a value, then its notes.
FIELDS = (('Item ID', '1'),
          ('Maker', 'International Business Machines Corporation of Armonk'),
          ('Part of', ''),
          ('', 'Available 1'),
          ('A label of forty characters, cut at 31..', '0123456789' * 8 +
           '012345678'),
          ('Caf\x82', 'na\x8bve & co'))
NOTES = ('Bought at a swap meet in 1991.', '',
         '0123456789' * 7 + '012 9') + tuple(
    'Line %d' % i for i in range(1, C['WCH_NLINES'] + 1))
PV = tuple('  PV "%s" "%s"' % (a[:C['WPV_LABELW']], b[:C['WPV_VALW']])
           for a, b in FIELDS) + tuple(
    '  PV "%s"' % n[:C['WPV_VALW']] for n in NOTES)
# The fields CTRL L's block takes, and WPVM_LINE's answers for the last
# of them, a field, and for the one after, none.
CAP = C['WPV_BYTES'] // C['WCH_RECSZ']
WPV_FIELD = 1


def bar(pos, top=None):
    """The panel's scroll bar of a step: its top row must be pos(m), m
    the most it scrolls, which is PVFILL's, or with top PVNARROW's."""
    return (pos, top)


# Each snapshot of WCHRT.KEY in order and what its key changed from the
# one before: 'seen' (n, c, k) of the Static, 'ans' its text, 'gray' the
# grayed buttons, 'stat' the panels' texts, 'out' the slot's Edit outside
# the bar's client area (OVERLAP), 'at' the splitter's left edge, 'lim'
# its limits, None for those it is made with (default_lim), 'drag' the
# focus and the mouse the splitter's while a drag is held open, 'ptext'
# the preview panel's text, 'pv' its lines, 'bar' its scroll bar (bar),
# None for none, 'pfocus' the focus the panel's.
START = {'seen': (0, 0, 0), 'ans': ans(1, *slot(80)), 'gray': (),
         'stat': ('',), 'out': False, 'at': C['WCH_SPX'], 'lim': None,
         'drag': False, 'ptext': '', 'pv': (), 'bar': None,
         'pfocus': False}
STEPS = (
    ('START', {}),
    # Open clicked: one WM_COMMAND, its command, BN_CLICKED (0).
    ('CLICK', {'seen': (1, cmd('Open'), 0)}),
    # New let go below itself: nothing sent.
    ('DRAGOFF', {}),
    # Lend dragged off and back, let go over itself: sent.
    ('DRAGBACK', {'seen': (2, cmd('Lend'), 0)}),
    ('GRAY', {'gray': ('Edit', 'Delete')}),
    # A click posted to a disabled button: nothing.
    ('GRAYCLK', {}),
    ('UNGRAY', {'gray': ()}),
    ('EDITCLK', {'seen': (3, cmd('Edit'), 0)}),
    # The slot's EN_CHANGE (300h) passed on unchanged.
    ('FORWARD', {'seen': (4, C['WCH_FIND'], 0x300)}),
    # A click on the bar itself: nothing, the focus kept.
    ('BARCLK', {}),
    # WTBM_BUTTON 7 is Find's window, 8 none.
    ('BUTTONS', {'ans': ans(1, 0)}),
    ('SLOT40', {'ans': ans(1, *slot(40))}),
    ('CLICK2', {'seen': (5, cmd('Open'), 0)}),
    # Find, the button after the slot, is button 7.
    ('FINDCLK', {'seen': (6, cmd('Find'), 0)}),
    # One panel, the last: the 100 bytes kept; panel 3 none.
    ('S1LONG', {'ans': ans(1), 'stat': (L1,)}),
    ('S1NOP3', {'ans': ans(0)}),
    # Four panels: panel 0 cut to its room, three new empty ones.
    ('S4', {'ans': ans(4), 'stat': (LCELL, '', '', '')}),
    ('S4TEXT', {'ans': ans(1), 'stat': F7}),
    ('S4TAB', {'stat': F7[:3] + ('Tab here',)}),
    ('S4NOP4', {'ans': ans(0)}),
    ('S4NULL', {'ans': ans(1), 'stat': (F7[0], '', F7[2], 'Tab here')}),
    ('S4LAST', {'stat': (F7[0], '', F7[2], LLAST)}),
    ('S4CELL', {'stat': (LCELL, '', F7[2], LLAST)}),
    ('S4NOP5', {'ans': ans(0)}),
    # Two panels: the last two dropped; F7 sets two, panel 3's answer.
    ('S2', {'ans': ans(2), 'stat': (LCELL, '')}),
    ('S2TEXT', {'ans': ans(0), 'stat': F7[:2]}),
    ('S4AGAIN', {'ans': ans(4), 'stat': F7[:2] + ('', '')}),
    # One panel (no widths): panel 0 kept; none refused; the one
    # panel's room holds the 100 bytes again.
    ('S1AGAIN', {'ans': ans(1), 'stat': F7[:1]}),
    ('S0NOP', {'ans': ans(0)}),
    ('S1LONG2', {'ans': ans(1), 'stat': (L1,)}),
    # The frame narrowed: the slot past the bar's right edge.
    ('NARROW', {'out': True}),
    ('WIDE', {'out': False}),
    # Open pressed (state 4, the capture its own), then grayed: up and
    # the capture gone; its release sends nothing, and Open is enabled
    # again.
    ('PRESSGRY', {'ans': ans(4, 1, 0, 1)}),
    # Open's glyph point, enabled, then grayed.
    ('PIXEL', {'ans': PIXELS}),
    # Open pressed, then the bar sent WM_CANCELMODE: as when grayed.
    ('CANCEL', {'ans': ans(4, 1, 0, 1)}),
    # Open pressed (as before), let go while the frame is disabled:
    # nothing sent.
    ('OWNEROFF', {'ans': ans(4, 1)}),
    # Down, up, double click, up on Open: two clicks, Edit 102 keeping
    # the focus.
    ('DBLCLK', {'seen': (8, cmd('Open'), 0)}),
    # The splitter dragged WCH_STEP right within the limits it has before
    # any WSPM_LIMITS, the parent's width less its own: moved, told.
    ('SPDRAG', moved(9, C['WCH_SPX'] + C['WCH_STEP'])),
    # WSPM_LIMITS, then WCH_FAR right: held at the highest left edge;
    # WCH_FAR left, the pointer's x negative: held at the lowest.
    ('SPMAX', dict(moved(10, C['WCH_SPHI']),
                   lim=(C['WCH_SPLO'], C['WCH_SPHI']))),
    ('SPMIN', moved(11, C['WCH_SPLO'])),
    # Dragged away and back to where it was: nothing sent.
    ('SPSAME', {}),
    # A drag held open: the mouse and the focus its own, the guide drawn
    # at its second place only, the splitter not yet moved.
    ('SPMID', {'ans': guide(1, 1, 1, WHITE, BLACK), 'drag': True}),
    # ESC: the drag cancelled, the focus back on Edit 102.
    ('SPESC', {'drag': False}),
    # Then a release where the guide was: nothing sent, the mouse free,
    # the guide erased.
    ('SPESCUP', {'ans': guide(0, 0, 0, WHITE, WHITE)}),
    # A press and a move, then WM_CANCELMODE: cancelled as by ESC, its
    # release sending nothing.
    ('SPCANCEL', {'ans': guide(1, 0, 0, WHITE, WHITE)}),
    # Disabled: the press taken by nothing, the release sending nothing.
    ('SPGRAY', {'ans': guide(0, 0, 0, WHITE, WHITE)}),
    # A double click's press drags as a press does.
    ('SPDBL', moved(12, C['WCH_SPX'])),
    # The panel's text of string WCH_S_LOAD with its argument.
    ('PVTEXT', {'ptext': 'Loading item 12...'}),
    # Filled: every line taken, the text gone, shown from the first.
    ('PVFILL', {'ans': ans(len(PV)), 'ptext': '', 'pv': PV,
                'bar': bar(lambda m: 0)}),
    # No black pixel where the focus rectangle runs; a click gives the
    # panel the focus, and one of the two is black.
    ('PVPLAIN', {'ans': ans(0)}),
    ('PVCLICK', {'pfocus': True}),
    ('PVFOCUS', {'ans': ans(1)}),
    # Down a row; PgDn further, short of the last; End the last, where
    # Down stays; Up one back; PgUp further back, short of the first;
    # Home the first, where Up stays; the thumb let go at WCH_THUMB.
    ('PVDOWN', {'bar': bar(lambda m: 1)}),
    ('PVPGDN', {'bar': bar(lambda m: range(3, m))}),
    ('PVEND', {'bar': bar(lambda m: m)}),
    ('PVLAST', {}),
    ('PVUP', {'bar': bar(lambda m: m - 1)}),
    ('PVPGUP', {'bar': bar(lambda m: range(1, m - 2))}),
    ('PVHOME', {'bar': bar(lambda m: 0)}),
    ('PVFIRST', {}),
    ('PVTHUMB', {'bar': bar(lambda m: C['WCH_THUMB'])}),
    # ENTER (13), ESC (27) and TAB (9) are the owner's, WPVN_KEY (1), and
    # a double click is ENTER.
    ('PVENTER', {'seen': (13, C['WCH_PREV'], C['WPVN_KEY']), 'ans': ans(13)}),
    ('PVESC', {'seen': (14, C['WCH_PREV'], C['WPVN_KEY']), 'ans': ans(27)}),
    ('PVTAB', {'seen': (15, C['WCH_PREV'], C['WPVN_KEY']), 'ans': ans(9)}),
    ('PVDBL', {'seen': (16, C['WCH_PREV'], C['WPVN_KEY']), 'ans': ans(13)}),
    # SHIFT and CTRL alone: nothing sent.
    ('PVMODS', {}),
    # At its last row, then narrow: more rows, the top row kept; wide
    # again, the rows it had, the top row their last.
    ('PVNARROW', {'out': True, 'bar': bar(lambda m: m, top=True)}),
    ('PVWIDE', {'out': False, 'bar': bar(lambda m: m)}),
    # The block's fields counted, the last one read, none after it; then
    # empty, without lines or scroll bar, and its text again.
    ('PVCAP', {'ans': ans(CAP, WPV_FIELD, 0), 'pv': (), 'bar': None}),
    # After a field a byte longer the block is a byte short of the last
    # of them: two fewer.
    ('PVCAPODD', {'ans': ans(CAP - 2, WPV_FIELD, 0)}),
    ('PVTEXT2', {'ptext': 'Loading item 12...'}),
)


def cut(stat):
    """1 when a panel holds LONG's digits cut to its room, or a panel but
    the last holds them at all, wider than any panel the steps make but
    the last; the other texts fit theirs."""
    return any(p.startswith(LONG[:WIDE]) and
               (i < len(stat) - 1 or len(p) < len(LONG))
               for i, p in enumerate(stat))


LIMITS = re.compile(r' WSPLIT \d+ "" AT \d+ LIMITS (\d+) (\d+)')


def default_lim(snaps):
    """The limits the splitter is made with, read from START: 0 and its
    parent's client width less its own, which only the frame's border
    makes unknown here, but past WCH_SPX + WCH_STEP, where SPDRAG takes
    it; None when START does not show such limits."""
    for line in snaps.get('START', ()):
        m = LIMITS.match(line)
        if m and int(m.group(1)) == 0 and \
                int(m.group(2)) >= C['WCH_SPX'] + C['WCH_STEP']:
            return (0, int(m.group(2)))
    return None


VBAR = re.compile(r' WPREV \d+ "[^"]*" VBAR (\d+) (\d+)')


def bar_max(snaps, name):
    """The most rows the panel scrolls in snapshot name, or 0 when it
    shows no scroll bar there."""
    for line in snaps.get(name, ()):
        m = VBAR.match(line)
        if m:
            return int(m.group(2))
    return 0


def bar_line(st, got, maxes):
    """The words of the panel's scroll bar in state st, ' VBAR pos max',
    or '' without one: max that of PVFILL, or of PVNARROW for a bar of
    it (maxes), and pos the top row the step asks for, the one the
    snapshot got shows where the step allows several."""
    if st['bar'] is None:
        return ''
    pos, top = st['bar']
    m = maxes[1 if top else 0]
    want = pos(maxes[0])
    if isinstance(want, range):
        seen = [int(x.group(1)) for x in map(VBAR.match, got or ()) if x]
        want = seen[0] if seen and seen[0] in want else want.start
    return ' VBAR %d %d' % (want, m)


def lines(st, lim0, got, maxes):
    """The snapshot of the frame in state st, lim0 the limits the
    splitter is made with, got the snapshot and maxes the scroll bar's
    ranges for bar_line."""
    tool = '|'.join(('~' if p in st['gray'] else '') + p for p in PIECES)
    return [
        'WCHRT "WCHRT" ACTIVE',
        ' WTOOL %d "%s"' % (C['WCH_TOOL'], tool),
        '  Edit %d ""%s' % (C['WCH_FIND'], ' OVERLAP' if st['out'] else ''),
        ' Edit %d ""%s' % (C['WCH_EDIT'],
                           '' if st['drag'] or st['pfocus'] else ' FOCUS'),
        ' Static %d "SEEN %d CMD %d CODE %d"' % (
            (C['WCH_SEEN'],) + st['seen']),
        ' Static %d "%s"' % (C['WCH_ANS'], st['ans']),
        ' WSTAT %d "%s"%s' % (C['WCH_STAT'], '|'.join(st['stat']),
                              ' CUT' if cut(st['stat']) else ''),
        ' WSPLIT %d "" AT %d LIMITS %d %d%s' % (
            (C['WCH_SPLIT'], st['at']) + (st['lim'] or lim0) +
            (' FOCUS CAPTURE' if st['drag'] else '',)),
        ' WPREV %d "%s"%s%s' % (C['WCH_PREV'], st['ptext'],
                               bar_line(st, got, maxes),
                               ' FOCUS' if st['pfocus'] else ''),
    ] + list(st['pv'])


def check(snaps):
    """The reasons the snapshots snaps fail, none when they hold."""
    bad = []
    names = [name for name, _ in STEPS]
    if list(snaps) != names:
        bad.append('snapshots %s, want %s' % (
            ' '.join(snaps), ' '.join(names)))
    lim0 = default_lim(snaps)
    if lim0 is None:
        bad.append('START: no splitter LIMITS 0 and at least %d' % (
            C['WCH_SPX'] + C['WCH_STEP']))
        lim0 = (0, 0)
    maxes = (bar_max(snaps, 'PVFILL'), bar_max(snaps, 'PVNARROW'))
    if maxes[0] < C['WCH_THUMB'] or maxes[1] <= maxes[0]:
        bad.append('the panel scrolls %d rows in PVFILL, %d in PVNARROW: '
                   'want at least %d, then more' % (
                       maxes + (C['WCH_THUMB'],)))
    st = dict(START)
    for name, change in STEPS:
        st.update(change)
        got = snaps.get(name)
        want = lines(st, lim0, got, maxes)
        if got is None:
            bad.append('%s: no snapshot' % name)
        elif got != want:
            bad.append('%s:\n  want %s\n  got  %s' % (
                name, '\n       '.join(want), '\n       '.join(got)))
    return bad


def ended():
    """1 when the golden's run ended with =END."""
    try:
        with open(os.path.join(colchk.GOLD, GOLDEN + '.SNP'), 'rb') as f:
            return b'\r\n=END\r\n' in f.read()
    except OSError:
        return 0


def main():
    snaps = colchk.snaps(GOLDEN)
    if snaps is None:
        bad = ['cannot read it']
    else:
        bad = check(snaps)
        why = pixel_bad()
        if why:
            bad.append(why)
        if not ended():
            bad.append('no =END line')
    for why in bad:
        print('chrtchk: %s.SNP %s' % (GOLDEN, why), file=sys.stderr)
    if bad:
        return 1
    print('chrtchk: %d snapshots ok' % len(STEPS))
    return 0


if __name__ == '__main__':
    sys.exit(main())
