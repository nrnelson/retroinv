"""mousechk.py - the test driver's mouse lines (the M line of
SRC/WIN/TDRV.H) in the snapshots of their key scripts, against what is
worked out apart from the program.

usage: python3 -I TEST/mousechk.py

The driver's own proof.  TEST/WTDRV2M.KEY and WTDRV2Q.KEY run WTDRV2
/MOUSE, whose pads (TEST/WTDRV2.C, their places in WTDRV2.H) report each
mouse message in their window text, "n msg x y wParam keys holder": A
(70, a class with CS_DBLCLKS) holding C (70.1) and B (71), both of a
class without; the main window reports in the status line (Static 60)
those it gets while it holds the mouse.  The scripts are replayed on a
model of the driver's contract: a point is the named window's client
origin (AT) or the middle of its client area (MID), plus the offsets;
the buttons held follow the event; the message goes to the window
holding the mouse when one does, at the point in its own client area,
else to the one named; DBL and RDBL come as presses to a class without
CS_DBLCLKS; wParam is MK_LBUTTON and MK_RBUTTON of the buttons held, and
so is the key state the pad reads; an event's SHIFT-, CTRL- and ALT-
prefixes (any order, any case) hold those keys down in the key state the
pad reports after the buttons (S, C, A) until the next mouse line or a K
line's key sets them again, and add MK_SHIFT and MK_CONTROL to wParam.  A
left press or double click on a pad takes the mouse there, a left
release lets it go, a right press or double click gives it to the main
window, whose right release lets it go.  Each snapshot must hold the
model's texts, CAPTURE exactly on the pad or list box holding the mouse
(none while the main window, a top-level window, does) and on no other
line, and ROW's press and release on the list box (ListBox 20) its row
selected and the focus.  WTDRV2H, I and J, and R (the right button's R
before a prefix) and S (a prefix not the driver's after one that is),
must be refused at their M line (=FAIL n SCRIPT, nothing written: H, I
and J at line 4, R and S at line 5, after two comment lines); WTDRV2N
(a path naming no window), O (a row past the list's) and P (BTN of a
window not a tool bar) must stop at their M line (=FAIL 3 TARGET) after
their INIT.

INVWIN.  Each case of CASES names a golden, a snapshot and what must
hold there, in TEST/splitchk.py's words (title, split, sel, ids, csel,
focus, count, rec, menu, dialog, tool) and these:

  held     the windows holding the mouse (CAPTURE): 'WLIST 208' and the
           like for a child of the frame, 'box 208' for a list window's
           box; no other line may hold it
  lsel     the ID of the item list's selected row
  caption  a dialog of that caption shows
  line     a line the snapshot holds
  cols     {list window: {column: (state, x, w)}} of its COLS line

and INVWINMW's drags (drags_bad): the splitter's AT follows the tree's
narrowed Name column at once, the splitter's own drag moves it to whole
average characters (AVEW pixels), the cancelled drags change nothing,
and TEST/GOLDEN/WIN/COLDRAG.CFG, the settings it leaves, starts with
TEST/WIN/COLDRAG.CFG's lines and holds the dragged WINCOLS.ITEM,
WINCOLS.TREE and WINSPLIT once each, CR LF ended.

A golden, snapshot or line missing, or one different, fails.  Exit
status 1 if any check fails.
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
_SPEC = importlib.util.spec_from_file_location(
    'splitchk', os.path.join(HERE, 'splitchk.py'))
splitchk = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(splitchk)
framchk = splitchk.framchk
colchk = splitchk.colchk

# The state words of TDRV.H, in their order.
STATES = ('DISABLED', 'HIDDEN', 'ACTIVE', 'FOCUS', 'CAPTURE', 'CHECKED',
          'MIXED', 'DEFAULT', 'SELECTED', 'DROP', 'LONG', 'OVERLAP', 'CUT')
HELD = 'CAPTURE'
DEFINE = re.compile(r'#define\s+(WT2_\w+)\s+(\d+)\b')

# ---------------------------------------------------------------------
# WTDRV2: the driver's own proof.
# ---------------------------------------------------------------------

# Each event's message (WM_MOUSEMOVE + its number) and what it does to
# the buttons held.
EVENTS = {'MOVE': (0, None, 0), 'DOWN': (1, 'L', 1), 'UP': (2, 'L', 0),
          'DBL': (3, 'L', 1), 'RDOWN': (4, 'R', 1), 'RUP': (5, 'R', 0),
          'RDBL': (6, 'R', 1)}
WM_MOUSEMOVE = 0x200
MAIN = 'W'                      # the main window, as a report names it
LIST = 20                       # WT2_ID_LIST, the single-selection list
STATUS = 60                     # WT2_ID_STATUS, the main window's reports
REFUSED = (('WTDRV2H', 4), ('WTDRV2I', 4), ('WTDRV2J', 4), ('WTDRV2R', 5),
           ('WTDRV2S', 5))
TARGET = ('WTDRV2N', 'WTDRV2O', 'WTDRV2P')
REPLAYED = ('WTDRV2M', 'WTDRV2Q')   # the scripts replayed on the model
# The prefixes of an M line's event, as a K line's (TDRV.H), each with
# its letter in a pad's report and its MK_ bit (ALT has none).
MODS = (('SHIFT-', 'S', 0x04), ('CTRL-', 'C', 0x08), ('ALT-', 'A', 0))


def depth(line):
    """The blanks a line starts with."""
    return len(line) - len(line.lstrip(' '))


def states(line):
    """The state words ending a snapshot line."""
    words = line.split()
    out = []
    while len(words) > 1 and words[-1] in STATES:
        out.insert(0, words.pop())
    return out


def pads(d):
    """The pads of WTDRV2.H's defines d, by path: each its control ID,
    its client area's origin in the main window's and its size, and
    whether its class has CS_DBLCLKS."""
    a = (d['WT2_ID_PADA'],)
    return {
        a: {'id': a[0], 'x': d['WT2_PADX'], 'y': d['WT2_PADY'],
            'w': d['WT2_PADW'], 'h': d['WT2_PADH'], 'dbl': True},
        a + (d['WT2_ID_PADC'],): {
            'id': d['WT2_ID_PADC'], 'x': d['WT2_PADX'] + d['WT2_PADCX'],
            'y': d['WT2_PADY'] + d['WT2_PADCY'], 'w': d['WT2_PADCW'],
            'h': d['WT2_PADCH'], 'dbl': False},
        (d['WT2_ID_PADB'],): {
            'id': d['WT2_ID_PADB'], 'x': d['WT2_PADBX'], 'y': d['WT2_PADY'],
            'w': d['WT2_PADW'], 'h': d['WT2_PADH'], 'dbl': False},
    }


def event(word):
    """An event word's event, upper case, and its prefixes' letters."""
    word = word.upper()
    mods = set()
    while True:
        for prefix, letter, _ in MODS:
            if word.startswith(prefix) and len(word) > len(prefix):
                word = word[len(prefix):]
                mods.add(letter)
                break
        else:
            return word, mods


def report(n, msg, x, y, held, mods, holder):
    """A report's text (WTDRV2.C), holder None, MAIN or a control ID."""
    wp = ('L' in held) | ('R' in held) << 1
    keys = ''
    for _, letter, bit in MODS:
        if letter in mods:
            wp |= bit
            keys += letter
    return '%d %03X %d %d %04X %s%s%s %s' % (
        n, msg, x, y, wp, 'L' if 'L' in held else '-',
        'R' if 'R' in held else '-', keys, '-' if holder is None else holder)


class Model:
    """The pads, the list box and the main window as the script leaves
    them."""

    def __init__(self, d):
        self.pads = pads(d)
        self.text = dict((p, '') for p in self.pads)
        self.count = dict((p, 0) for p in self.pads)
        self.status = 'idle'
        self.mice = 0
        self.held = set()
        self.mods = set()               # the prefixes' letters held down
        self.cap = None                 # a pad's path, MAIN, LIST, None
        self.sel = 0                    # the list's selected row
        self.listfocus = False

    def holder(self):
        """The holder as a report names it."""
        if self.cap in self.pads:
            return self.pads[self.cap]['id']
        return self.cap

    def mouse(self, words):
        """Carry out an M line's words, or say why the model cannot."""
        path = tuple(int(p) for p in words[1].split('.'))
        name, self.mods = event(words[2])
        if name not in EVENTS:
            return 'event %s' % words[2]
        num, button, down = EVENTS[name]
        base = words[3].upper()
        if path == (LIST,):
            if base != 'ROW' or button != 'L':
                return 'list line %s' % ' '.join(words)
            if down:
                self.cap, self.listfocus = LIST, True
                self.sel = int(words[4])
            elif self.cap == LIST:
                self.cap = None
            return None
        pad = self.pads.get(path)
        if pad is None:
            return 'no pad %s' % words[1]
        x, y = (0, 0) if base == 'AT' else (pad['w'] // 2, pad['h'] // 2)
        if len(words) == 6:
            x, y = x + int(words[4]), y + int(words[5])
        x, y = x + pad['x'], y + pad['y']
        if button:
            (self.held.add if down else self.held.discard)(button)
        to = self.cap if self.cap is not None else path
        if to == MAIN:
            if num == EVENTS['RUP'][0]:
                self.cap = None
            self.mice += 1
            self.status = report(self.mice, WM_MOUSEMOVE + num, x, y,
                                 self.held, self.mods, self.holder())
            return None
        rx = self.pads[to]
        if num in (3, 6) and not rx['dbl']:
            num -= 2
        if num in (1, 3):
            self.cap = to
        elif num == 2 and self.cap == to:
            self.cap = None
        elif num in (4, 6):
            self.cap = MAIN
        self.count[to] += 1
        self.text[to] = report(self.count[to], WM_MOUSEMOVE + num,
                               x - rx['x'], y - rx['y'], self.held,
                               self.mods, self.holder())
        return None

    def bad(self, gold, snap, lines):
        """Why the snapshot's lines differ from the model, one each."""
        where = '%s.SNP %s' % (gold, snap)
        bad = []
        want = {' Static %d' % STATUS: (self.status, False)}
        for path, pad in self.pads.items():
            head = ' ' * len(path) + ('WT2PAD' if pad['dbl'] else 'WT2PADN')
            want['%s %d' % (head, pad['id'])] = (self.text[path],
                                                 self.cap == path)
        for head, (text, held) in sorted(want.items()):
            got = [ln for ln in lines if ln.startswith(head + ' "')]
            line = '%s "%s"' % (head, text)
            if len(got) != 1 or not got[0].startswith(line + (
                    '' if got[0] == line else ' ')) or \
                    (HELD in states(got[0])) != held:
                bad.append('%s:\n  want %s%s\n  got  %s' % (
                    where, line, ' CAPTURE' if held else '', got))
        box = [i for i, ln in enumerate(lines)
               if ln.startswith(' ListBox %d "' % LIST)]
        if len(box) != 1:
            return bad + ['%s: no ListBox %d' % (where, LIST)]
        st = states(lines[box[0]])
        rows = []
        for ln in lines[box[0] + 1:]:
            if depth(ln) < 2:
                break
            if ln.startswith('  ROW '):
                rows.append(ln)
        sel = [i for i, ln in enumerate(rows) if 'SELECTED' in states(ln)]
        if ('FOCUS' in st) != self.listfocus or \
                (HELD in st) != (self.cap == LIST) or sel != [self.sel]:
            bad.append('%s: %s, rows %s selected, want row %d%s%s' % (
                where, lines[box[0]], sel, self.sel,
                ' FOCUS' if self.listfocus else '',
                ' CAPTURE' if self.cap == LIST else ''))
        main = [ln for ln in lines if ln.startswith('WTDRV2 "')]
        if len(main) != 1 or ('FOCUS' in states(main[0])) == self.listfocus:
            bad.append('%s: main window %s' % (where, main))
        heads = [h for h in want if want[h][1]] + (
            [' ListBox %d' % LIST] if self.cap == LIST else [])
        for ln in lines:
            if HELD in states(ln) and not any(ln.startswith(h + ' "')
                                              for h in heads):
                bad.append('%s: %s holds the mouse' % (where, ln))
        return bad


def defines(path):
    """The WT2_ numbers #defined in the file at path."""
    out = {}
    with open(path, encoding='latin-1') as f:
        for line in f:
            m = DEFINE.match(line.strip())
            if m:
                out[m.group(1)] = int(m.group(2))
    return out


def golden(name):
    """The lines of golden name.SNP, or None."""
    try:
        with open(os.path.join(colchk.GOLD, name + '.SNP'), 'rb') as f:
            return f.read().decode('latin-1').split('\r\n')
    except OSError:
        return None


def replay_bad(d, gold):
    """Why golden gold of a script replayed on the model fails, one line
    each."""
    model = Model(d)
    snaps = colchk.snaps(gold)
    if snaps is None:
        return ['%s.SNP: cannot read it' % gold]
    bad = []
    names = []
    with open(os.path.join(HERE, gold + '.KEY'), encoding='latin-1') as f:
        for n, line in enumerate(f, 1):
            words = line.split()
            if not words or words[0].startswith('#'):
                continue
            cmd = words[0].upper()
            if cmd == 'M':
                why = model.mouse(words)
                if why:
                    return ['%s.KEY %d: %s' % (gold, n, why)]
            elif cmd == 'K':
                model.mods = set()      # put up at the key's release
            elif cmd == 'S':
                names.append(words[1])
                lines = snaps.get(words[1])
                if lines is None:
                    bad.append('%s.SNP: no snapshot %s' % (gold, words[1]))
                else:
                    bad += model.bad(gold, words[1], lines)
    if list(snaps) != names:
        bad.append('%s.SNP: snapshots %s, want %s' % (
            gold, ' '.join(snaps), ' '.join(names)))
    if '=END' not in (golden(gold) or ()):
        bad.append('%s.SNP: no =END line' % gold)
    return bad


def wtdrv2_bad():
    """Why WTDRV2's mouse goldens fail, one line each."""
    d = defines(os.path.join(HERE, 'WTDRV2.H'))
    bad = []
    for gold in REPLAYED:
        bad += replay_bad(d, gold)
    for name, line in REFUSED:
        want = '=FAIL %d SCRIPT' % line
        got = [ln for ln in golden(name) or () if ln]
        if got != [want]:
            bad.append('%s.SNP: %s, want %s alone' % (name, got, want))
    for name in TARGET:
        got = golden(name) or []
        tops = [ln for ln in got if ln.startswith('=')]
        if tops != ['=SNAP INIT', '=FAIL 3 TARGET']:
            bad.append('%s.SNP: %s, want =SNAP INIT, then =FAIL 3 TARGET' %
                       (name, tops))
    return bad


# ---------------------------------------------------------------------
# INVWIN: the mouse paths of the frame.
# ---------------------------------------------------------------------

TOOL = framchk.TOOL
TREE_TOOL = splitchk.TREE_TOOL
NEW_DOWN = '^' + TOOL               # New drawn pressed
LOCS = '3 locations'
OFFICE = '3 items in Office'
AVEW = 5                            # the list font's average character
                                    # (Helv 8 on the test machine, colchk)
# The item list's Category slot by ID and the tree's Name slot (x, w),
# the default widths of TEST/colchk.py, and the drags of INVWINMW.
CAT = (55, 80)
NAME = (0, 150)
CAT_DRAG = 30
NAME_DRAG = -50
SPLIT_DRAG = 40


def chars(w, d):
    """The average characters a slot w pixels wide stores dragged d."""
    return (w + d + AVEW // 2) // AVEW


CAT_N = chars(CAT[1], CAT_DRAG)
NAME_N = chars(NAME[1], NAME_DRAG)
NONE = frozenset()
SPLITCHK = ('title', 'split', 'sel', 'ids', 'csel', 'focus', 'count', 'rec',
            'menu', 'dialog', 'tool')

CASES = (
    # A right press on another row selects it (popup_input's left
    # click), the pop-up opening at the release over it, its entries
    # acting on that row's item; a right release without a press opens
    # nothing; a right double click counts as a press.
    ('INVWINMR', 'ITEMS', {'title': 'Items by ID', 'lsel': 1, 'held': NONE}),
    ('INVWINMR', 'RDOWN', {'lsel': 3, 'menu': 0, 'dialog': 0, 'focus': 0,
                           'held': NONE}),
    ('INVWINMR', 'RPOPUP', {'lsel': 3, 'menu': 1}),
    ('INVWINMR', 'REDIT', {'caption': 'Item 3 - Edit'}),
    ('INVWINMR', 'REDITBK', {'title': 'Items by ID', 'lsel': 3, 'menu': 0,
                             'dialog': 0}),
    ('INVWINMR', 'NOPOP', {'lsel': 3, 'menu': 0, 'dialog': 0}),
    ('INVWINMR', 'RDBLPOP', {'lsel': 2, 'menu': 1}),
    ('INVWINMR', 'RDBLEDIT', {'caption': 'Item 2 - Edit'}),
    # The split view: a right click on the contents while the tree has
    # the focus focuses them (pane_follow), the pop-up theirs.
    ('INVWINMR', 'LOCS', {'split': 1, 'focus': 0, 'sel': 'Office',
                          'ids': [1, 3, 2], 'rec': 'tree'}),
    ('INVWINMR', 'CPOP', {'menu': 1, 'focus': 1, 'rec': 'contents',
                          'csel': 3, 'count': OFFICE, 'tool': TOOL}),
    ('INVWINMR', 'CPOPESC', {'menu': 0, 'focus': 1, 'rec': 'contents',
                             'csel': 3, 'count': OFFICE}),
    # A click into the contents moves the focus, the Record menu, the
    # tool bar and the count there (pane_follow); a press on a tree row
    # selects it, the box holding the mouse, the contents still Office's;
    # its release, the selection settled, loads Box B2's.
    ('INVWINMF', 'LOCS', {'split': 1, 'focus': 0, 'sel': 'Office',
                          'ids': [1, 3, 2], 'rec': 'tree', 'tool': TREE_TOOL,
                          'count': LOCS, 'held': NONE}),
    ('INVWINMF', 'CLICKED', {'split': 1, 'focus': 1, 'sel': 'Office',
                             'ids': [1, 3, 2], 'csel': 3, 'rec': 'contents',
                             'tool': TOOL, 'count': OFFICE, 'held': NONE}),
    ('INVWINMF', 'TREEDN', {'split': 1, 'focus': 0, 'sel': 'Box B2',
                            'ids': [1, 3, 2], 'rec': 'tree',
                            'tool': TREE_TOOL, 'count': LOCS,
                            'held': frozenset(['box 208'])}),
    ('INVWINMF', 'TREEUP', {'split': 1, 'focus': 0, 'sel': 'Box B2',
                            'ids': [2], 'rec': 'tree', 'count': LOCS,
                            'held': NONE}),
    # New held is drawn pressed, the bar holding the mouse; off it
    # raised, still held; back on pressed; released on it the command
    # reaches the frame.  Open released off itself does nothing.
    ('INVWINMT', 'ITEMS', {'tool': TOOL, 'held': NONE}),
    ('INVWINMT', 'PRESSED', {'tool': NEW_DOWN, 'dialog': 0, 'focus': 0,
                             'held': frozenset(['WTOOL 218'])}),
    ('INVWINMT', 'AWAY', {'tool': TOOL, 'held': frozenset(['WTOOL 218'])}),
    ('INVWINMT', 'BACKON', {'tool': NEW_DOWN,
                            'held': frozenset(['WTOOL 218'])}),
    ('INVWINMT', 'NEWFORM', {'caption': 'New item', 'tool': TOOL,
                             'held': NONE}),
    ('INVWINMT', 'NEWESC', {'title': 'Items by ID', 'dialog': 0,
                            'focus': 0}),
    ('INVWINMT', 'OFFUP', {'title': 'Items by ID', 'dialog': 0, 'tool': TOOL,
                           'focus': 0, 'held': NONE}),
    # New held while DEL's question opens: let go (the frame's
    # cancel_drags, the bar passing WM_CANCELMODE to the button), so its
    # release after No sends nothing.
    ('INVWINMT', 'DELASK', {'caption': 'RETROINV', 'tool': TOOL,
                            'line': ' Static 65535 "Delete item 1 IBM 5150?"',
                            'held': NONE}),
    ('INVWINMT', 'DELUP', {'title': 'Items by ID', 'dialog': 0, 'tool': TOOL,
                           'ids': [1, 2, 3], 'held': NONE}),
    # New held while F5's reload holds the frame busy, with no dialog: let
    # go (busy's cancel_drags), so its release sends nothing.
    ('INVWINMT', 'F5MOVE', {'title': 'Items by ID', 'dialog': 0,
                             'tool': TOOL, 'held': NONE}),
    ('INVWINMT', 'F5UP', {'title': 'Items by ID', 'dialog': 0,
                           'tool': TOOL, 'held': NONE}),
    # A click on Maker's heading sorts by it.
    ('INVWINMT', 'BYMAKER', {'title': 'Items by maker', 'dialog': 0,
                             'cols': {208: {0: ('B', 0, 55),
                                            2: ('A', 135, 100)}},
                             'held': NONE}),
    # A grayed button takes no press and its release sends nothing.
    ('INVWINMT', 'LOCS', {'split': 1, 'tool': TREE_TOOL}),
    ('INVWINMT', 'GRAYDN', {'tool': TREE_TOOL, 'dialog': 0, 'held': NONE}),
    ('INVWINMT', 'GRAYUP', {'title': 'Locations', 'split': 1, 'dialog': 0,
                            'tool': TREE_TOOL, 'held': NONE}),
    # Category's divider held (the list window holds the mouse), then
    # dragged CAT_DRAG pixels: CAT_N characters, the columns after it
    # moved (cols_moved); the split view's contents, the same table,
    # opened after.
    ('INVWINMW', 'ITEMS', {'cols': {208: {1: ('F',) + CAT}}, 'held': NONE}),
    ('INVWINMW', 'DIVDN', {'cols': {208: {1: ('F',) + CAT}}, 'focus': 0,
                           'held': frozenset(['WLIST 208'])}),
    ('INVWINMW', 'WIDER', {'cols': {208: {
        1: ('F', CAT[0], CAT_N * AVEW),
        2: ('B', CAT[0] + CAT_N * AVEW, 100)}}, 'held': NONE}),
    # Held again while F1's help window opens: cancelled (cancel_drags),
    # its release after ESC changing nothing.
    ('INVWINMW', 'DIVF1', {'caption': 'RETROINV Help', 'held': NONE}),
    ('INVWINMW', 'DIVF1UP', {'title': 'Items by ID', 'dialog': 0,
                             'cols': {208: {
                                 1: ('F', CAT[0], CAT_N * AVEW)}},
                             'held': NONE}),
    ('INVWINMW', 'LOCS', {'split': 1, 'cols': {
        208: {0: ('F',) + NAME},
        221: {1: ('F', CAT[0], CAT_N * AVEW)}}, 'held': NONE}),
    # The tree's Name divider dragged NAME_DRAG pixels: NAME_N characters.
    ('INVWINMW', 'TREENAR', {'split': 1, 'focus': 0, 'cols': {208: {
        0: ('F', 0, NAME_N * AVEW), 1: ('F', NAME_N * AVEW, 80)}},
        'held': NONE}),
    # ESC during a drag of it: cancelled, the screen kept; its release
    # changes nothing.
    ('INVWINMW', 'TREEESC', {'title': 'Locations', 'split': 1,
                             'cols': {208: {0: ('F', 0, NAME_N * AVEW)}},
                             'held': NONE}),
    ('INVWINMW', 'TREEUP', {'title': 'Locations', 'split': 1,
                            'cols': {208: {0: ('F', 0, NAME_N * AVEW)}},
                            'held': NONE}),
    # The splitter held: the mouse and the focus its own.
    ('INVWINMW', 'SPLITDN', {'held': frozenset(['WSPLIT 220'])}),
    ('INVWINMW', 'SPLITUP', {'split': 1, 'held': NONE}),
    ('INVWINMW', 'SPLITF1', {'caption': 'RETROINV Help', 'held': NONE}),
    ('INVWINMW', 'SPLITF1U', {'title': 'Locations', 'split': 1,
                              'dialog': 0, 'held': NONE}),
)

SLOT = re.compile(r' (\d+)([FBAP])@(-?\d+)\+(\d+)')


def held_by(lines):
    """The windows of a snapshot holding the mouse: a child of the frame
    as 'CLASS id', a list window's box as 'box id', any other line as
    its first two words."""
    out = set()
    parent = None
    for ln in lines:
        words = ln.split()
        if depth(ln) == 1 and len(words) > 1:
            parent = words[:2]
        if HELD not in states(ln):
            continue
        if depth(ln) == 2 and words[0] == 'ListBox' and parent and \
                parent[0] == 'WLIST':
            out.add('box ' + parent[1])
        else:
            out.add(' '.join(words[:2]))
    return frozenset(out)


def cols_of(lines, wid):
    """The COLS slots of list window wid, {k: (state, x, w)}, or None."""
    head = ' WLIST %d "' % wid
    for i, ln in enumerate(lines):
        if ln.startswith(head):
            for c in lines[i + 1:i + 3]:
                if c.startswith('  COLS '):
                    return dict((int(k), (s, int(x), int(w)))
                                for k, s, x, w in SLOT.findall(c))
    return None


def case_bad(case, cache):
    """Why case fails, one line each."""
    gold, snap, want = case
    where = '%s.SNP %s' % (gold, snap)
    shared = dict((k, v) for k, v in want.items() if k in SPLITCHK)
    bad = splitchk.case_bad((gold, snap, shared), cache) if shared else []
    lines = (splitchk.load(cache, gold) or {}).get(snap)
    if lines is None:
        return bad or ['%s: no snapshot' % where]
    tops = [ln for ln in lines if not ln.startswith(' ')]
    for key, w in sorted(want.items()):
        if key in SPLITCHK:
            continue
        if key == 'held':
            got = held_by(lines)
        elif key == 'lsel':
            _, ps, _ = splitchk.view(lines)
            row = ps.get(framchk.C_LIST, {}).get('sel')
            got = splitchk.item_id(row) if row else None
        elif key == 'caption':
            head = '#32770 "%s" ' % w
            got = w if any(t.startswith(head) for t in tops) else tops
        elif key == 'line':
            got = w if w in lines else None
        elif key == 'cols':
            got = {}
            for wid, slots in w.items():
                have = cols_of(lines, wid) or {}
                got[wid] = dict((k, have.get(k)) for k in slots)
        else:
            got = '(unknown key)'
        if got != w:
            bad.append('%s: %s\n  want %s\n  got  %s' % (where, key, w, got))
    return bad


def at(cache, snap):
    """The splitter's line in INVWINMW's snapshot snap: (x, its state
    words after LIMITS), or None."""
    lines = (splitchk.load(cache, 'INVWINMW') or {}).get(snap) or ()
    for ln in lines:
        m = framchk.WSPLITLN.match(ln)
        if m:
            return int(m.group(2)), (m.group(5) or '').split()
    return None


def drags_bad(cache):
    """Why INVWINMW's splitter places or its settings fail, one line each."""
    bad = []
    got = dict((s, at(cache, s)) for s in (
        'LOCS', 'TREENAR', 'TREEESC', 'TREEUP', 'SPLITDN', 'SPLITUP',
        'SPLITF1', 'SPLITF1U'))
    if None in got.values():
        return ['INVWINMW.SNP: no splitter in %s' % ' '.join(
            s for s, v in got.items() if v is None)]
    x = dict((s, v[0]) for s, v in got.items())
    # The automatic width follows the Name column at once (cols_moved's
    # place); ESC's cancel keeps it.
    narrow = x['LOCS'] + NAME_N * AVEW - NAME[1]
    split = chars(x['TREEUP'], SPLIT_DRAG)
    want = {'TREENAR': narrow, 'TREEESC': narrow, 'TREEUP': narrow,
            'SPLITDN': narrow, 'SPLITUP': split * AVEW,
            'SPLITF1': split * AVEW, 'SPLITF1U': split * AVEW}
    for s, w in sorted(want.items()):
        if x[s] != w:
            bad.append('INVWINMW.SNP %s: AT %d, want %d' % (s, x[s], w))
    for s, v in sorted(got.items()):
        st = ['FOCUS', HELD] if s == 'SPLITDN' else []
        if v[1] != st:
            bad.append('INVWINMW.SNP %s: splitter states %s, want %s' % (
                s, v[1], st))
    bad += cfg_bad(split)
    return bad


def cfg_bad(split):
    """Why the settings INVWINMW leaves fail, split the WINSPLIT its
    splitter's drag gives."""
    try:
        with open(os.path.join(colchk.GOLD, 'COLDRAG.CFG'), 'rb') as f:
            text = f.read().decode('latin-1')
        with open(os.path.join(HERE, 'WIN', 'COLDRAG.CFG'), 'rb') as f:
            first = f.read().decode('latin-1').split('\r\n')[:-1]
    except OSError as e:
        return ['COLDRAG.CFG: %s' % e]
    if not text.endswith('\r\n') or '\n' in text.replace('\r\n', ''):
        return ['COLDRAG.CFG: a line not ended by CR LF']
    lines = text.split('\r\n')[:-1]
    bad = []
    if lines[:len(first)] != first:
        bad.append('COLDRAG.CFG: starts %s, want %s' % (
            lines[:len(first)], first))
    want = ['WINSPLIT=%d' % split, 'WINCOLS.ITEM=0,%d' % CAT_N,
            'WINCOLS.TREE=%d' % NAME_N]
    got = [ln for ln in lines
           if ln.upper().startswith(('WINSPLIT', 'WINCOLS.'))]
    if sorted(got) != sorted(want):
        bad.append('COLDRAG.CFG: %s, want %s once each' % (got, want))
    return bad


def main():
    bad = wtdrv2_bad()
    cache = {}
    for case in CASES:
        bad += case_bad(case, cache)
    bad += drags_bad(cache)
    bad = list(dict.fromkeys(bad))
    for why in bad:
        print('mousechk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('mousechk: WTDRV2 ok, %d cases ok, drags ok' % len(CASES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
