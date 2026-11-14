"""dragchk.py - rows of a list window dragged (SRC/WIN/WLIST.H, WL_DRAGS):
INVWIN's items dragged from the split view's contents onto the tree, and
TEST/WLISTT.C's rows dragged within its list, in the snapshots of their
key scripts, against states worked out by hand.

usage: python3 -I TEST/dragchk.py

INVWIN.  The split view's contents (WLIST 221) drag their rows; the tree
(WLIST 208) is the target, its row under the pointer marked DROP (the
snapshot's state word of a row that wl_target marks).  Each case of
CASES names a golden, a snapshot and what must hold there, in
TEST/mousechk.py's words (TEST/splitchk.py's, held, caption, line) and
these:

  drop     {list window: [its rows marked DROP]}; no other row of the
           snapshot may be marked
  help     the status bar's help panel: DRAG (IW_S_DRAG, the drag's
           help), a note, or '' for none
  same     a snapshot of the same golden whose tree and contents rows
           this one's equal: nothing moved
  tree     the same for the tree's rows alone
  says     a snapshot of the same golden whose dialog's error line
           (' Static 99 "text"', Move's) a message box here shows as its
           text (' Static 65535 "text"')

and in every snapshot of the drag's goldens (DRAGS): at most one row
marked, and only a row of the tree, while the contents' box holds the
mouse and the help panel shows DRAG; DRAG only while that box holds the
mouse.  No other golden of TEST/GOLDEN/WIN marks a row DROP but
WLISTTX and WLISTTY.  IW_S_DRAG of SRC/WIN/INVWIN.RC is DRAG, and each
golden's snapshots are its script's S lines, in order, then =END.

WLISTTX.  TEST/WLISTTX.KEY runs WLISTT /DRAG, whose list (WLIST 1, its
box ListBox 1) is its own target; its owner puts each WL_DRAG note in
the frame's title, "DRAG n s i key" (n the notes so far, s the WL_DR_
step, i and key the row under the drag's last point, -1 and - for none)
or "DRAG n s" for WL_DR_END.  Each step of STEPS names a snapshot, what
the title starts with, whether the box holds the mouse (None: either),
its selected row and its rows marked DROP; GONE, the list destroyed
during a drag, shows no list and no new note.  TEST/WLISTTY.KEY runs it
once its window has scrolled within eleven items, its steps STEPS_Y.

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
    'mousechk', os.path.join(HERE, 'mousechk.py'))
mousechk = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(mousechk)
splitchk = mousechk.splitchk
framchk = mousechk.framchk
colchk = mousechk.colchk

DROP = 'DROP'
HELD = mousechk.HELD
TREE = framchk.C_LIST
CONT = framchk.C_CLIST
DRAGGING = frozenset(['box %d' % CONT])
NONE = mousechk.NONE
TOOL = framchk.TOOL
OFFICE = mousechk.OFFICE
DRAG = 'Drop the item on a location to move it there; Esc cancels'
TO_B2 = 'Item 1 moved to Office > Shelf A1 > Box B2'
TO_A1 = 'Item 1 moved to Office > Shelf A1'
INSTALLED = 'Item 3 installed in item 1'
MV_INST = ' Static 99 "Installed in an assembly"'
DRAGS = ('INVWINDA', 'INVWINDR', 'INVWINDX', 'INVWINDT', 'INVWINDL')
# The drags of several rows and of every row, whose cases are
# TEST/selchk.py's.
SELDRAG = ('INVWINDS', 'INVWINEG')
WLISTTX = 'WLISTTX'
WLISTTY = 'WLISTTY'
TO_OFF = 'Item 3 moved to Office'
A1_ALL = '3 items in Shelf A1'
A1_LEFT = '2 items in Shelf A1'
A1_NONE = '0 items in Shelf A1'
ALL_OFF = 'Office (all items, everything below)'
NODROP = {}
# What every drag shows: the contents' box holding the mouse, the help
# panel saying so, the tree's selection and the contents kept, so that
# the target is not the selection and no settle loads another row.
OVER = {'held': DRAGGING, 'help': DRAG, 'sel': 'Office', 'focus': 1,
        'count': OFFICE}
# What a drag that ended shows: nothing held, marked or said.
ENDED = {'held': NONE, 'help': '', 'drop': NODROP, 'dialog': 0,
         'menu': 0}


def case(gold, snap, *parts, **more):
    """A case of gold's snapshot snap: the dicts parts, then more."""
    want = {}
    for p in parts + (more,):
        want.update(p)
    return gold, snap, want


CASES = (
    # A drag of item 2 starts past the threshold: the help panel says
    # so, the contents' box holds the mouse, its row selected, no row
    # marked.  Over the tree the row under the pointer alone is marked
    # (wl_target, the old row unmarked); on the header line and below
    # the last row none is (the hit test bounded to the box's rows).
    case('INVWINDA', 'LOCS', split=1, sel='Office', ids=[1, 3, 2],
         focus=0, held=NONE, help='', drop=NODROP),
    case('INVWINDA', 'DRAGGED', OVER, csel=2, ids=[1, 3, 2],
         rec='contents', tool=TOOL, drop=NODROP),
    case('INVWINDA', 'OVERB2', OVER, csel=2, same='LOCS', drop={TREE: [2]}),
    case('INVWINDA', 'OVERA1', OVER, csel=2, same='LOCS', drop={TREE: [1]}),
    case('INVWINDA', 'OVERHEAD', OVER, drop=NODROP),
    case('INVWINDA', 'OVERLOW', OVER, drop=NODROP),
    # Dropped on its own location: no request, no note, nothing loaded.
    case('INVWINDA', 'OWNLOC', ENDED, same='LOCS', sel='Office', focus=1,
         count=OFFICE),
    # Released on the contents, on the splitter, below the tree's rows:
    # nothing moved.
    case('INVWINDA', 'UPCONT', ENDED, same='LOCS', sel='Office'),
    case('INVWINDA', 'UPSPLIT', ENDED, same='LOCS', sel='Office'),
    case('INVWINDA', 'UPLOW', ENDED, same='LOCS', sel='Office'),
    # Dropped on Box B2: Move's move, its note, the contents loaded again
    # with item 1 there, the tree kept.
    case('INVWINDA', 'MOVED', ENDED, help=TO_B2, tree='LOCS', sel='Office',
         idset=[1, 2, 3], loc=(1, 'Box B2'), focus=1, count=OFFICE),
    # The tree's selection was kept on Office through every drag (the key
    # read put it back): DOWN from it gives Shelf A1, whose contents,
    # everything below, follow.
    case('INVWINDA', 'DOWNA1', ENDED, sel='Shelf A1', focus=0,
         idset=[1, 2, 3]),
    # Item 3 dropped on Office leaves Shelf A1: the contents counted
    # again (count_rows), the tree's selection kept.
    case('INVWINDA', 'LEFTA1', ENDED, help=TO_OFF, sel='Shelf A1', focus=1,
         idset=[1, 2], count=A1_LEFT),
    # Shelf A1's own items alone: none, the empty text, which a press
    # does not drag (no help, no mark) nor hold, the contents choosing
    # several rows and so taking their presses themselves (WLIST.H,
    # Several rows).
    case('INVWINDA', 'OWNA1', ENDED, sel='Shelf A1', ids=[], count=A1_NONE),
    case('INVWINDA', 'EMPTYDR', held=NONE, help='', drop=NODROP, ids=[]),
    case('INVWINDA', 'EMPTYUP', ENDED, sel='Shelf A1', ids=[],
         count=A1_NONE),
    # Item 3, installed in item 1, is at Office; Move to Box B2 is refused
    # in its dialog, and the drop with the same text in a message box,
    # after which the contents are as they were.
    case('INVWINDR', 'INSTLD', help=INSTALLED),
    case('INVWINDR', 'LOCS', split=1, ids=[1, 3, 2], loc=(3, 'Office'),
         drop=NODROP),
    case('INVWINDR', 'MVINST', caption='Move item 3 to', line=MV_INST),
    case('INVWINDR', 'MVBACK', dialog=0, same='LOCS'),
    case('INVWINDR', 'REFUSED', says='MVINST', held=NONE, drop=NODROP),
    case('INVWINDR', 'REFBACK', ENDED, same='LOCS', loc=(3, 'Office')),
    # ESC ends a drag, the screen kept, its release moving nothing.
    case('INVWINDX', 'LOCS', split=1, sel='Office', ids=[1, 3, 2]),
    case('INVWINDX', 'ESCOVER', OVER, csel=1, drop={TREE: [1]}),
    case('INVWINDX', 'ESCED', ENDED, title='Locations', split=1, csel=1,
         same='LOCS'),
    case('INVWINDX', 'ESCUP', ENDED, same='LOCS', sel='Office'),
    # A drag that ESC ended keeps the tree's selection on Office: ENTER
    # opens its contents, ESC the split view again.  (ESC reads no key;
    # INVWINDA's DOWNA1 and WLISTTY's OPEN check that the drop's key
    # read puts the selection back.)
    case('INVWINDX', 'ESCOPEN', ENDED, title=ALL_OFF, split=0,
         ids=[1, 3, 2]),
    case('INVWINDX', 'ESCBACK', ENDED, split=1, sel='Office', same='LOCS'),
    # F1's help window ends it as it opens (WM_CANCELMODE to the box).
    case('INVWINDX', 'F1HELP', caption='RETROINV Help', held=NONE,
         drop=NODROP, help=''),
    case('INVWINDX', 'F1UP', ENDED, same='LOCS', sel='Office'),
    # A right click during a drag: no pop-up, no row selected, the drag
    # still on; its drop on Shelf A1 moves the item.
    case('INVWINDX', 'RIGHT', OVER, menu=0, csel=1, drop={TREE: [1]}),
    case('INVWINDX', 'RIGHTUP', ENDED, help=TO_A1, tree='LOCS',
         sel='Office', idset=[1, 2, 3], loc=(1, 'Shelf A1')),
    # ALT-F during a drag opens no menu; ESC then ends the drag.
    case('INVWINDX', 'ALTF', OVER, menu=0, drop={TREE: [2]}),
    case('INVWINDX', 'ALTESC', ENDED, title='Locations', same='RIGHTUP'),
    case('INVWINDX', 'ALTUP', ENDED, same='RIGHTUP', sel='Office'),
    # 4 pixels is a click (the box's own press, held until its release),
    # 5 down or left a drag; a double click opens the row; a drag across
    # the contents' own rows keeps their selection.
    case('INVWINDT', 'LOCS', split=1, sel='Office', ids=[1, 3, 2]),
    case('INVWINDT', 'FOUR', held=DRAGGING, help='', drop=NODROP, csel=3),
    case('INVWINDT', 'CLICK', ENDED, csel=3, focus=1, count=OFFICE,
         same='LOCS'),
    case('INVWINDT', 'FIVEDOWN', OVER, csel=3),
    case('INVWINDT', 'DOWNESC', ENDED, csel=3, same='LOCS'),
    case('INVWINDT', 'FIVELEFT', OVER, csel=3),
    case('INVWINDT', 'LEFTESC', ENDED, csel=3, same='LOCS'),
    case('INVWINDT', 'DBLOPEN', caption='Item 1', drop=NODROP),
    case('INVWINDT', 'DBLESC', ENDED, csel=1, same='LOCS'),
    case('INVWINDT', 'OWNROWS', OVER, csel=1, drop=NODROP),
    case('INVWINDT', 'OWNESC', ENDED, csel=1),
    case('INVWINDT', 'OWNUP', ENDED, csel=1, same='LOCS'),
    # A press not yet dragged, and F1's help window opening: the box's
    # WM_CANCELMODE goes on to it, so it no longer holds the mouse.
    case('INVWINDT', 'ARMF1', caption='RETROINV Help', held=NONE,
         drop=NODROP, help=''),
    case('INVWINDT', 'ARMF1UP', ENDED, csel=1, same='LOCS'),
    # Level 1: no drag, no mark, no move, no box.  The contents' box
    # tracks its own press, so its selection is left out.
    case('INVWINDL', 'LOCS', split=1, sel='Office', ids=[1, 3, 2]),
    case('INVWINDL', 'L1OVER', help='', drop=NODROP, dialog=0, menu=0),
    case('INVWINDL', 'L1UP', ENDED, same='LOCS', sel='Office'),
)
OWN = ('drop', 'help', 'same', 'tree', 'says')
MOUSE = ('held', 'caption', 'line')     # mousechk's keys; the rest splitchk's

# WLISTTX: (snapshot, the title's start, the box holding the mouse,
# its selected row, its rows marked DROP).
STEPS = (
    ('START', 'WLISTT', False, 0, []),
    # 4 pixels: the box's own press, then its click; no note.
    ('ARMED', 'WLISTT', True, 0, []),
    ('CLICK', 'WLISTT', False, 0, []),
    # WM_CANCELMODE while pressed: no note, the press the box's own, a
    # move past the threshold then no drag, its release a click.
    ('ARMCANC', 'WLISTT', True, 1, []),
    ('ARMMOVE', 'WLISTT', True, 1, []),
    ('ARMUP', 'WLISTT', False, 1, []),
    # A double click's second press starts no drag.
    ('DBLMOVE', 'OPEN 1', None, 0, []),
    ('DBLUP', 'OPEN 1', False, 0, []),
    # 5 pixels down: START, then OVER at that move, then one more OVER
    # with no row: as the box ends its own press and takes the capture
    # again, Windows queues a WM_MOUSEMOVE at the screen's pointer, which
    # the driver never moves, so it lies off the rows, as a real pointer
    # still on the press's side would at the start.  Each drag start
    # below so counts one note more.  Moved to row 1 it alone is marked;
    # outside the box none; the drop at row 1.
    ('STARTED', 'DRAG 3 1 -1 -', True, 0, []),
    ('OVER1', 'DRAG 4 1 1 2', True, 0, [1]),
    ('OUTSIDE', 'DRAG 5 1 -1 -', True, 0, []),
    ('DROPPED', 'DRAG 7 2 1 2', False, 0, []),
    # Ends: ESC, WM_CANCELMODE to the list window (cancel_drags), the
    # capture lost; each release after them nothing.
    ('ESC', 'DRAG 11 3', False, 0, []),
    ('ESCUP', 'DRAG 11 3', False, 0, []),
    ('CANCEL', 'DRAG 16 3', False, 0, []),
    ('CANCELUP', 'DRAG 16 3', False, 0, []),
    ('LOST', 'DRAG 20 3', False, 0, []),
    ('LOSTUP', 'DRAG 20 3', False, 0, []),
)
# The list destroyed while dragged: no note after the start's.  Its last
# OVER is the start's extra move at the pointer the driver never moves
# (STEPS), so no row.
GONE = ('GONE', 'DRAG 23 1 -1 -')
# WLISTTY, the same once the window has scrolled within the rows held
# (eleven items): two DOWNs leave rows 2 and 3 in the box, the pager's
# first row 1, row 3 selected; row 2 pressed, over the box's row 1 the
# key is the pager's row 3's (first + 1, not the box's index), and after
# ESC the selection is still row 2, which ENTER opens (the key read put
# the pager's selection back).
STEPS_Y = (
    ('START', 'WLISTT', False, 0, []),
    ('SCROLLED', 'WLISTT', False, 1, []),
    ('OVER1', 'DRAG 4 1 1 3', True, 0, [1]),
    ('ESC', 'DRAG 5 3', False, 0, []),
    ('OPEN', 'OPEN 2', False, 0, []),
)
TITLE = re.compile(r'^WLISTT "(.*) #\d+(?: [^"]*)?"')
ROWLN = re.compile(r' *ROW ')


def marks(lines):
    """The rows of a snapshot marked DROP, {list window: [rows]}, any
    other row as {line: [row]}, and those SELECTED alike."""
    drop = {}
    sel = {}
    owner = None
    n = 0
    for ln in lines:
        d = mousechk.depth(ln)
        words = ln.split()
        if d == 1:
            owner = int(words[1]) if words[:1] == ['WLIST'] else ln
        if d == 2 and words[:1] == ['ListBox']:
            n = 0
        if ROWLN.match(ln):
            st = mousechk.states(ln)
            key = owner if d == 3 and owner is not None else ln
            if DROP in st:
                drop.setdefault(key, []).append(n)
            if 'SELECTED' in st:
                sel.setdefault(key, []).append(n)
            n += 1
    return drop, sel


def help_of(lines):
    """The status bar's help panel, or None."""
    m = framchk.WSTAT.match(framchk.chrome(lines)[3] or '')
    panels = m.group(2).split('|') if m else []
    return panels[3] if len(panels) == 4 else None


def rows_of(lines, wid):
    """The rows' texts of list window wid."""
    _, ps, _ = splitchk.view(lines)
    return ps.get(wid, {}).get('rows')


def boxed(lines):
    """The lines of a snapshot's dialog and message box windows."""
    out = []
    inside = False
    for ln in lines:
        if not ln.startswith(' '):
            inside = ln.startswith('#32770 ')
        elif inside:
            out.append(ln)
    return out


def own_bad(where, lines, snaps, want):
    """Why the keys of OWN in want fail, one line each."""
    bad = []
    for key in OWN:
        if key not in want:
            continue
        w = want[key]
        if key == 'drop':
            got = marks(lines)[0]
        elif key == 'help':
            got = help_of(lines)
        elif key in ('same', 'tree'):
            other = snaps.get(w)
            if other is None:
                bad.append('%s: no snapshot %s to compare' % (where, w))
                continue
            wids = (TREE, CONT) if key == 'same' else (TREE,)
            got = [rows_of(lines, i) for i in wids]
            w = [rows_of(other, i) for i in wids]
        else:
            said = [ln for ln in snaps.get(w, ())
                    if ln.startswith(' Static 99 "')]
            got = [ln for ln in boxed(lines)
                   if ln.startswith(' Static 65535 "') and not
                   ln.endswith(' ""')]
            w = [ln.replace(' Static 99 ', ' Static 65535 ', 1)
                 for ln in said[:1]] or ['(no error line in %s)' % want[key]]
        if got != w:
            bad.append('%s: %s\n  want %s\n  got  %s' % (where, key, w, got))
    return bad


def invariant_bad(gold, snap, lines):
    """Why a snapshot of the drag's goldens breaks their rules."""
    where = '%s.SNP %s' % (gold, snap)
    drop = marks(lines)[0]
    held = mousechk.held_by(lines)
    help_ = help_of(lines)
    bad = []
    n = sum(len(r) for r in drop.values())
    if n and (n > 1 or list(drop) != [TREE] or 'box %d' % CONT not in held
              or help_ != DRAG):
        bad.append('%s: DROP on %s, held %s, help %r' % (
            where, drop, sorted(held), help_))
    if help_ == DRAG and 'box %d' % CONT not in held:
        bad.append('%s: the drag\'s help, the contents not holding the '
                   'mouse: %s' % (where, sorted(held)))
    return bad


def script(name):
    """The S lines' names of TEST/name.KEY, or None."""
    try:
        with open(os.path.join(HERE, name + '.KEY'), encoding='latin-1') as f:
            words = [ln.split() for ln in f]
    except OSError:
        return None
    return [w[1] for w in words if len(w) > 1 and w[0].upper() == 'S']


def run_bad(name):
    """Why golden name is not its script's run to the end."""
    snaps = colchk.snaps(name)
    if snaps is None:
        return ['%s.SNP: cannot read it' % name]
    want = script(name)
    bad = []
    if list(snaps) != want:
        bad.append('%s.SNP: snapshots %s, want %s' % (
            name, ' '.join(snaps), ' '.join(want or ())))
    if '=END' not in (mousechk.golden(name) or ()):
        bad.append('%s.SNP: no =END line' % name)
    return bad


def wlistt_bad(name, steps, gone):
    """Why WLISTT golden name's snapshots differ from steps and, unless
    it is None, gone."""
    snaps = colchk.snaps(name)
    if snaps is None:
        return ['%s.SNP: cannot read it' % name]
    bad = []
    for snap, what, held, sel, drop in steps:
        where = '%s.SNP %s' % (name, snap)
        lines = snaps.get(snap)
        if lines is None:
            bad.append('%s: no snapshot' % where)
            continue
        tops = [ln for ln in lines if ln.startswith('WLISTT "')]
        m = TITLE.match(tops[0]) if tops else None
        boxes = [ln for ln in lines if ln.startswith('  ListBox 1 "')]
        if not m or len(boxes) != 1:
            bad.append('%s: no frame or no list: %s' % (where, tops))
            continue
        dmarks, smarks = marks(lines)
        got = (m.group(1), HELD in mousechk.states(boxes[0]),
               smarks.get(1), dmarks.get(1, []), len(dmarks) <= 1)
        exp = (what, got[1] if held is None else held, [sel], drop, True)
        if got != exp:
            bad.append('%s: (title, held, selected, DROP, only the list\'s)'
                       '\n  want %s\n  got  %s' % (where, exp, got))
        for ln in lines:
            if HELD in mousechk.states(ln) and ln is not boxes[0]:
                bad.append('%s: %s holds the mouse' % (where, ln))
    if gone is None:
        return bad
    snap, what = gone
    lines = snaps.get(snap) or []
    tops = [ln for ln in lines if ln.startswith('WLISTT "')]
    alone = re.compile(r'WLISTT "%s #\d+"' % re.escape(what))
    if not tops or not alone.match(tops[0]) or \
            any(ln.startswith(' WLIST ') for ln in lines):
        bad.append('%s.SNP %s: want "%s #n" without a list, got %s' % (
            name, snap, what, tops))
    return bad


def source_bad():
    """Why IW_S_DRAG of INVWIN.RC is not DRAG."""
    names = framchk.defines(os.path.join(framchk.WIN, 'INVWIN.H'))
    got = framchk.strings(names).get(names.get('IW_S_DRAG'))
    if got != DRAG:
        return ['INVWIN.RC: IW_S_DRAG is %r, want %r' % (got, DRAG)]
    return []


def main():
    bad = source_bad()
    cache = {}
    for gold, snap, want in CASES:
        for keys, check in ((MOUSE, mousechk.case_bad),
                            (None, splitchk.case_bad)):
            some = dict((k, v) for k, v in want.items() if k not in OWN and
                        (k in keys if keys else k not in MOUSE))
            if some:
                bad += check((gold, snap, some), cache)
        snaps = splitchk.load(cache, gold) or {}
        lines = snaps.get(snap)
        if lines is None:
            bad.append('%s.SNP %s: no snapshot' % (gold, snap))
            continue
        bad += own_bad('%s.SNP %s' % (gold, snap), lines, snaps, want)
    for gold in DRAGS:
        bad += run_bad(gold)
        for snap, lines in sorted((splitchk.load(cache, gold) or {}).items()):
            bad += invariant_bad(gold, snap, lines)
    for name, steps, gone in ((WLISTTX, STEPS, GONE),
                              (WLISTTY, STEPS_Y, None)):
        bad += run_bad(name)
        bad += wlistt_bad(name, steps, gone)
    for f in sorted(os.listdir(colchk.GOLD)):
        gold = f[:-4]
        if not f.endswith('.SNP') or gold in DRAGS + SELDRAG + (WLISTTX,
                                                                WLISTTY):
            continue
        for snap, lines in sorted((colchk.snaps(gold) or {}).items()):
            if marks(lines)[0]:
                bad.append('%s %s: a row marked DROP' % (f, snap))
    bad = list(dict.fromkeys(bad))
    for why in bad:
        print('dragchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('dragchk: %d cases ok, WLISTTX and WLISTTY ok' % len(CASES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
