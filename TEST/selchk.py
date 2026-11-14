"""selchk.py - several rows chosen in INVWIN's lists and the record
commands that act on them, in the snapshots of their key scripts,
against what is worked out apart from the program.

usage: python3 -I TEST/selchk.py

Every golden of TEST/GOLDEN/WIN: in each list window's box (SRC/WIN/
WLIST.H) the test driver writes a DRAW line under the first row and
under each SELECTED row, under no other (TDRV.H); more than one row is
SELECTED only in a list of the frame that takes several, its own list
(WLIST 208) or the pane beside it (WLIST 221), never on a screen of
SINGLE (the location tree, the categories, the users, the service
records, the audit log), in the pane of a screen of SINGLE_PANE (the
audit entries beside the users) nor in a dialog (the pick lists), and in
WLISTTZ, whose list is opened to take several.  TEST/framchk.py checks the count
panel's "<n> selected" and "All <n> selected" in every snapshot.  The
frame's Record menu holds Select all (SELALL) exactly while the list
whose menu it is takes several rows: its own list, or on the split view
the pane whose box has the focus (either while neither has it, as under
a dialog); DISABLED exactly while that list is empty, showing one row,
its empty text, not SELECTED.

INVWIN.  Each case of CASES names a golden, a snapshot and what must
hold there, the comment above it the mistake it catches:

  sel      the IDs of the SELECTED rows, in order, of the frame's list,
           on the split view of the pane whose box has the focus
  nsel     how many rows of that list are SELECTED
  tsel     the names of the location tree's SELECTED rows
  ids      the IDs of that list's rows, in order
  locs     {id: (location, status)} of that list's item rows
  count    the status bar's count panel
  help     its help panel
  title    the frame titled "RETROINV - title"
  caption  the active window a dialog of that caption
  field    (id, text): an edit control of the active dialog holds text
  error    1 when the active dialog's error line (Static 99) has a
           text, 0 when it is empty
  box      the text of the active message box
  stopped  (key, done, n): the active message box's text a failure's
           followed by STOPPED's
  line     a line the snapshot holds
  dialog   1 when a dialog or message box shows, 0 when none does
  menu     1 when a menu window shows
  pick     how many rows of the active dialog's list window are SELECTED

and INVWINSN's loans (loans_bad): its three loans, one per item, then
the Return of the two selected naming the first and the count of the
others, and the open loans left the one not selected.

The texts are the contract's: "%s and %u more" in place of a single
row's key in a dialog's caption (MORE), "For each of the %u selected
items" (EACH), "Delete the %u selected items?" (DELN), "Stopped at %s:
%u of %u done." (STOPPED) and the notes of the commands done (DONE);
and for every row of a list chosen (INVWINEA to INVWINEJ) "All %u
selected" (NALL), DEL's question (DELALL), "Stopped: %u of %u done."
(HALTED), the refusal of a list too long (OVER) and "Collecting %u..."
(COLLECT).

WLISTTZ (wlistt_bad): TEST/WLISTT.C /MULTI on 260 items in a window of
two rows, each snapshot of STEPS_Z its title's what, scroll bar, count
and rows chosen (a * while they are all), and the IDs of the rows shown
and of those SELECTED.

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

MORE = '%s and %d more'
EACH = 'For each of the %d selected items'
DELN = 'Delete the %d selected items?'
STOPPED = 'Stopped at %s: %d of %d done.'
DONE = {'move': '%d items moved', 'into': '%d items installed',
        'out': '%d items taken out', 'end': '%d loans and holds ended',
        'lend': '%d loans added', 'res': '%d reservations added',
        'del': '%d items deleted'}
NSEL = framchk.NSEL
NADD = framchk.NADD
# Every row of a list chosen: the count panel, DEL's question, the stop
# by an Esc, the refusal of a list too long and the help panel under it.
NALL = 'All %d selected'
DELALL = 'Delete all %d items in this list, including those not shown?'
HALTED = 'Stopped: %d of %d done.'
OVER = ('Select all takes at most %d rows, fewer in a small window. '
        'Narrow the list first (Find, a filter).')
COLLECT = 'Collecting %d...'
# The scripts of every row chosen: their items, the seed's and the 257
# of TEST/WIN/mkitems.sh, the loans list's count, the items' audit log
# (their adds and the seed's two notes of three writes), a line of the
# preview, the title of the Find box's results and READER's panel of the
# status bar.
ITEMS260 = '260 items'
HOLDS = '%d loans and holds'
AUDITED = '263 entries'
PV_ID = '  PV "Item ID" "%d"'
FOUND = 'Items containing "Hercules"'
READER = ' WSTAT 201 "Data: DATA\\|READER, level 1|All 260 selected|"'
# The frame's screens whose own list takes a single row, by the start
# of their titles, and those whose pane does.
SINGLE = ('Locations', 'Categories', 'Users', 'Service records of ',
          'Recent service', 'Audit log')
SINGLE_PANE = ('Users',)
DENIED = 'Permission denied (your access level is 1)'
ITEMS = '63 items'                  # the seed and TEST/KEYS/RLLONG.REQ
OFFICE, A1, B2 = 'Office', 'Shelf A1', 'Box B2'
# The help panel during a row's drag (IW_S_DRAG).
DRAG = 'Drop the item on a location to move it there; Esc cancels'
INST = 'Installed'
FRAME = framchk.menuchk.FRAME
# The Record menu's Select all: its command is IW_M_KEY (INVWIN.H) plus
# CTRL (INVWIN.C) plus the key of / (WL_VK_ALL of WLIST.H).
SELALL = 'MENU 1215 "Select all?Ctrl+/"'
TEXT = re.compile(r' (\w+) (\d+) "(.*)"((?: [A-Z]+)*)$')


def caption(key, n, form):
    """A command's caption for n rows, the first row's key key."""
    return form % (key if n == 1 else MORE % (key, n - 1))


MOVE = 'Move item %s to'
INTO = 'Install item %s in'
OUT = 'Take item %s out to'
RET = 'Return loan %s?'
REL = 'Release reservation %s?'


def depth(line):
    """The blanks a line starts with."""
    return len(line) - len(line.lstrip(' '))


def states(line):
    """The state words ending a snapshot line, after its text."""
    return line.rsplit('"', 1)[1].split() if '"' in line else []


def boxes(lines):
    """The list window boxes of a snapshot: each (top, wid, rows), top
    the line of its top-level window, wid the list window's ID, rows
    [text, selected, drawn] of its ROW lines."""
    out = []
    top = parent = cur = None
    at = 0
    for ln in lines:
        d = depth(ln)
        words = ln.split()
        first = words[0] if words else ''
        if cur is not None and d <= at:
            cur = None
        if d == 0:
            top, parent = ln, None
        elif first == 'WLIST':
            parent = (d, int(words[1]))
        elif first == 'ListBox' and parent and parent[0] == d - 1:
            cur, at = [], d
            out.append((top, parent[1], cur))
        elif cur is not None and first == 'ROW' and d == at + 1:
            cur.append([ln[d + 5:].rsplit('"', 1)[0],
                        'SELECTED' in states(ln), False])
        elif cur and first == 'DRAW' and d == at + 2:
            cur[-1][2] = True
    return out


def single(top, wid):
    """1 when the list window wid under the top-level line top takes one
    row only."""
    title = framchk.title_of(top) if top.startswith(FRAME) else None
    if title is None:
        return 1
    if wid == framchk.C_CLIST:
        return int(title.startswith(SINGLE_PANE))
    return int(wid != framchk.C_LIST or title.startswith(SINGLE))


def golden_bad(gold, snaps):
    """Why the list boxes of golden gold fail the rules of every golden,
    one line each."""
    bad = []
    for snap, lines in sorted(snaps.items()):
        for top, wid, rows in boxes(lines):
            where = '%s.SNP %s WLIST %d' % (gold, snap, wid)
            drawn = [i for i, r in enumerate(rows) if r[2]]
            want = [i for i, r in enumerate(rows) if i == 0 or r[1]]
            if drawn != want:
                bad.append('%s: DRAW under rows %s, want %s' % (
                    where, drawn, want))
            n = sum(1 for r in rows if r[1])
            if n > 1 and gold != WLISTTZ and single(top, wid):
                bad.append('%s: %d rows SELECTED in a list of one' % (
                    where, n))
    return bad


def active(lines):
    """The lines of the active top-level window, its own first."""
    for i, ln in enumerate(lines):
        if depth(ln) == 0 and 'ACTIVE' in states(ln):
            j = i + 1
            while j < len(lines) and depth(lines[j]) > 0:
                j += 1
            return lines[i:j]
    return []


def frame_list(lines):
    """The rows [text, selected, drawn] of the frame's list, on the split
    view the contents' while their box has the focus, and the tree's
    rows there (None without the split)."""
    lists = dict((wid, rows) for t, wid, rows in boxes(lines)
                 if t.startswith(FRAME))
    _, ps, _ = splitchk.view(lines)
    if framchk.C_CLIST not in lists:
        return lists.get(framchk.C_LIST, []), None
    tree = lists.get(framchk.C_LIST, [])
    if ps.get(framchk.C_CLIST, {}).get('focus'):
        return lists[framchk.C_CLIST], tree
    return tree, tree


def status_of(row):
    """The Status of an item's row by ID."""
    cells = colchk.ITEM
    return row[sum(abs(c) + 1 for c in cells[:7]):].strip()


def msgbox(lines):
    """The text of the active message box, or None."""
    act = active(lines)
    if not act or not act[0].startswith('#32770 "RETROINV" '):
        return None
    texts = [m.group(3) for m in map(TEXT.match, act[1:])
             if m and m.group(1) == 'Static' and m.group(3)]
    return texts[-1] if texts else ''


def got_of(key, want, lines):
    """What the snapshot's lines hold for key."""
    rows, tree = frame_list(lines)
    act = active(lines)
    tops = [ln for ln in lines if depth(ln) == 0]
    _, _, count = splitchk.view(lines)
    m = framchk.WSTAT.match(framchk.chrome(lines)[3] or '')
    panels = m.group(2).split('|') if m else []
    if key == 'sel':
        return [splitchk.item_id(t) for t, s, _ in rows if s]
    if key == 'nsel':
        return sum(1 for r in rows if r[1])
    if key == 'tsel':
        return [framchk.loc_name(t) for t, s, _ in tree or () if s]
    if key == 'ids':
        return [splitchk.item_id(t) for t, _, _ in rows]
    if key == 'locs':
        have = dict((splitchk.item_id(t), (splitchk.item_loc(t),
                                           status_of(t))) for t, _, _ in rows)
        return dict((i, have.get(i)) for i in want)
    if key == 'count':
        return count
    if key == 'help':
        return panels[3] if len(panels) > 3 else None
    if key == 'title':
        frame = [t for t in tops if t.startswith(FRAME)]
        return framchk.title_of(frame[0]) if frame else None
    if key == 'caption':
        head = '#32770 "%s" ' % want
        return want if act and act[0].startswith(head) else act[:1]
    if key == 'field':
        got = [m.group(3) for m in map(TEXT.match, act[1:])
               if m and m.group(1) == 'Edit' and int(m.group(2)) == want[0]]
        return (want[0], got[0] if len(got) == 1 else got)
    if key == 'error':
        got = [m.group(3) for m in map(TEXT.match, act[1:])
               if m and m.group(1) == 'Static' and m.group(2) == '99']
        return int(bool(got[0])) if len(got) == 1 else got
    if key == 'box':
        return msgbox(lines)
    if key == 'stopped':
        text = msgbox(lines) or ''
        tail = STOPPED % want
        return want if text.endswith(tail) and len(text) > len(tail) and \
            text[:-len(tail)].strip(' ?') else text
    if key == 'line':
        return want if want in lines else None
    if key == 'dialog':
        return int(any(t.startswith('#32770 ') for t in tops))
    if key == 'menu':
        return int('#32768 ""' in tops)
    if key == 'pick':
        picks = [r for t, wid, r in boxes(act) if t == act[0]]
        return sum(1 for r in picks[0] if r[1]) if len(picks) == 1 else picks
    return '(unknown key)'


def case_bad(case, cache):
    """Why case fails, one line each."""
    gold, snap, want = case
    where = '%s.SNP %s' % (gold, snap)
    lines = (splitchk.load(cache, gold) or {}).get(snap)
    if lines is None:
        return ['%s: no snapshot' % where]
    bad = []
    for key, w in sorted(want.items()):
        got = got_of(key, w, lines)
        if got != w:
            bad.append('%s: %s\n  want %s\n  got  %s' % (where, key, w, got))
    return bad


def items(sel=None, nsel=None, count=ITEMS, **more):
    """The item list at level 3 with no dialog: sel selected (or nsel
    rows), the count panel NSEL of them or count."""
    out = {'title': 'Items by ID', 'dialog': 0}
    if sel is not None:
        out['sel'] = sel
        nsel = len(sel)
    if nsel is not None:
        out['nsel'] = nsel
        out['count'] = NSEL % nsel if nsel > 1 else count
    out.update(more)
    return out


def three(**more):
    """The seed's three items with one row selected after a reload."""
    return items(nsel=1, count='3 items', **more)


def chosen(n, shown, **more):
    """A list with every one of its n rows chosen, the shown rows of its
    window all SELECTED, and no dialog."""
    out = {'nsel': shown, 'count': NALL % n, 'dialog': 0}
    out.update(more)
    return out


def one(count, **more):
    """A list with one row SELECTED, the count panel count, and no
    dialog: the focus row alone, after every row was chosen."""
    out = {'nsel': 1, 'count': count, 'dialog': 0}
    out.update(more)
    return out


CASES = (
    # INVWINSL: the selection of the item list, 63 items, 20 rows shown.
    ('INVWINSL', 'ITEMS', items([1])),
    # SHIFT-DOWN taking a single list's move (the SHIFT ignored, WLIST.H
    # as it was): one row; the count panel not following WL_SEL.
    ('INVWINSL', 'PAIR', items([1, 2])),
    # Split units on the first chosen row, not the focus row.
    ('INVWINSL', 'SPLITASK', {'caption': 'Split item 2'}),
    # A single-row command dropping the other rows.
    ('INVWINSL', 'SPLITBK', items([1, 2])),
    ('INVWINSL', 'SHDOWN', items([1, 2, 3])),
    # A plain DOWN keeping the rows (rule ONE not clearing them).
    ('INVWINSL', 'DOWN', items([4])),
    # The M line's SHIFT not reaching the box (wParam or key state), or
    # RANGE not reaching from the anchor the plain DOWN set.
    ('INVWINSL', 'SHCLICK', items([4, 5, 6, 7])),
    # CTRL as SHIFT (a range 7..10), or CTRL clearing the others.
    ('INVWINSL', 'CTRLIN', items([4, 5, 6, 7, 10])),
    # CTRL+SHIFT as SHIFT (others cleared), or ADD from the old anchor 4.
    ('INVWINSL', 'ADDRANGE', items([4, 5, 6, 7, 10, 11, 12])),
    # FLIP setting a selected row again instead of taking it away.
    ('INVWINSL', 'CTRLOUT', items([4, 5, 7, 10, 11, 12])),
    # FLIP not moving the anchor (a range 8..10 from item 10), RANGE not
    # clearing the rows outside it.
    ('INVWINSL', 'ANCHOR', items([6, 7, 8])),
    # RANGE by click moving the anchor to the row clicked ([7, 8]).
    ('INVWINSL', 'SHUP', items([6, 7])),
    # CTRL-DOWN extending as SHIFT-DOWN does.
    ('INVWINSL', 'CTRLDN', items([8])),
    # A plain press on a selected row among several selecting it alone
    # at once (no KEEP, no held row).
    ('INVWINSL', 'HELDDN', items([8, 9, 10])),
    # The release of the held press not completing the click.
    ('INVWINSL', 'HELDUP', items([9])),
    ('INVWINSL', 'THREE', items([9, 10, 11])),
    # ENTER opening the first chosen row, not the focus row.
    ('INVWINSL', 'OPEN', {'caption': 'Item 11'}),
    # A single-row command (Open) dropping the other rows.
    ('INVWINSL', 'OPENBK', items([9, 10, 11])),
    ('INVWINSL', 'EDIT', {'caption': 'Item 11 - Edit'}),
    # A cancelled CTRL+E dropping the other rows.
    ('INVWINSL', 'EDITBK', items([9, 10, 11])),
    # The double click opening the old focus row (11), or no Open from
    # WLIST's own WM_LBUTTONDBLCLK (LBN_DBLCLK gone with the native
    # press).
    ('INVWINSL', 'DBLOPEN', {'caption': 'Item 10'}),
    ('INVWINSL', 'DBLBK', items([10])),
    # CTRL taking the only selected row away: none SELECTED, the count
    # the list's (the focus row alone chosen, not "0 selected").
    ('INVWINSL', 'NONESEL', items([])),
    # wl_nchosen 0 without a selected row: Move doing nothing, or asking
    # for "0 more".
    ('INVWINSL', 'NONEMOVE', {'caption': MOVE % 10}),
    # A pick list made WL_MULTI: HOME and SHIFT-DOWN two rows.
    ('INVWINSL', 'PICKSH', {'caption': 'Choose a location', 'pick': 1}),
    ('INVWINSL', 'PAGEBOT', items([19, 20])),
    # A SHIFT-DOWN past the window's last row, in add mode too, refilling
    # the box (the focus row alone chosen) instead of beeping and keeping
    # the two; a plain DOWN then not paging.
    ('INVWINSL', 'OFFPAGE', items([19, 20])),
    ('INVWINSL', 'ADDEDGE', dict(items([19, 20]), count=NADD % 2)),
    ('INVWINSL', 'PLAINDN', items([21])),
    ('INVWINSL', 'PGSEL', items(nsel=2)),
    # PGDN, HOME, Next order and a heading's click (busy moves, a fill each)
    # keeping the selection or its count.
    ('INVWINSL', 'PGDN', items(nsel=1)),
    # A SHIFT-UP above the window's first row, two chosen, refilling the
    # box instead of beeping and keeping them.
    ('INVWINSL', 'PGTOP', items([22, 23])),
    ('INVWINSL', 'UPEDGE', items([22, 23])),
    ('INVWINSL', 'HOMESEL', items(nsel=2)),
    ('INVWINSL', 'HOME', items([1])),
    ('INVWINSL', 'ORDSEL', items([1, 2])),
    ('INVWINSL', 'ORDER', items(nsel=1, title='Items by location')),
    ('INVWINSL', 'HEADSEL', items(nsel=2, title='Items by location')),
    ('INVWINSL', 'HEAD', items(nsel=1, title='Items by maker')),
    # F5's reload refilling the list: the rows chosen dropped.
    ('INVWINSL', 'F5SEL', items([1, 2])),
    ('INVWINSL', 'F5', items(nsel=1)),
    # The tree made WL_MULTI: SHIFT-DOWN selecting Office and Shelf A1.
    ('INVWINSL', 'TREESH', {'title': 'Locations', 'tsel': [A1],
                            'count': '3 locations'}),
    # The contents beside the tree left single.
    ('INVWINSL', 'CONTSH', {'title': 'Locations', 'tsel': [A1], 'nsel': 3,
                            'count': NSEL % 3}),
    ('INVWINSL', 'FLIPF', items([1, 2])),
    # The CTRL click's rule (FLIP) left for the next quiet move.
    ('INVWINSL', 'FLIPDN', items([4])),
    ('INVWINSL', 'KEEPF', items([1, 2, 3])),
    # The right press's rule (KEEP) left for the next quiet move.
    ('INVWINSL', 'KEEPDN', items([4])),
    # popup_click finding the row by LB_GETCURSEL, not the focus row:
    # the press at the box's top selecting item 1 alone.
    ('INVWINSL', 'F10KEEP', items([4, 5], menu=1)),
    # SHIFT-UP at the first row, SHIFT-DOWN at the last, going busy and
    # dropping the rows.
    ('INVWINSL', 'TOPSH', items([1, 2])),
    ('INVWINSL', 'ENDSH', items([62, 63])),
    # A cancelled new loan of several leaving IW_S_EACH on the help
    # panel, or fetching the list again.
    ('INVWINSL', 'LENDESC', items([62, 63], help='')),
    # A held press completed by a release on another row.
    ('INVWINSL', 'RELOTHER', items([1, 2, 3])),
    # A held press kept through a refill, its release selecting item 2.
    ('INVWINSL', 'FILLUP', items([1])),
    # A delete of several on a list longer than a page counting one row
    # less, or none: its count is the list's own, not the rows shown.
    ('INVWINSL', 'DELMANY', items(nsel=1, count='60 items',
                                  help=DONE['del'] % 3)),

    # INVWINDS: a selection of the split view's contents dragged.  The
    # contents' rows by ID: 1 (Office), 3 (Shelf A1), 2 (Box B2).
    ('INVWINDS', 'DSSEL', {'title': 'Locations', 'sel': [1, 3],
                           'count': NSEL % 2}),
    # The press on a selected row choosing it alone at once (no held
    # press in a list that drags its rows).
    ('INVWINDS', 'DSHELD', {'sel': [1, 3]}),
    ('INVWINDS', 'DSDRAG', {'sel': [1, 3], 'help': DRAG}),
    # The drag's start not forgetting the held press: the release after
    # ESC selecting item 1 alone.
    ('INVWINDS', 'DSESC', {'sel': [1, 3], 'help': ''}),
    # The armed press's release not completing the held click.
    ('INVWINDS', 'DSCLICK', {'sel': [1]}),
    ('INVWINDS', 'DSOVER', {'sel': [1, 3], 'help': DRAG}),
    # The drop moving the row pressed alone, or one note per row.
    ('INVWINDS', 'DSMOVED', {'help': DONE['move'] % 2, 'nsel': 1,
                             'locs': {1: (B2, ''), 3: (B2, ''),
                                      2: (B2, '')}}),
    # A drag from a row not chosen carrying the rows chosen.
    ('INVWINDS', 'DSONE', {'help': 'Item 1 moved to Office > Shelf A1',
                           'nsel': 1, 'locs': {1: (A1, ''), 2: (B2, ''),
                                               3: (B2, '')}}),
    # A row already at the location refused, or the drop stopped there.
    ('INVWINDS', 'DSTHERE', {'help': DONE['move'] % 2, 'dialog': 0,
                             'count': '3 items in Office',
                             'locs': {1: (B2, ''), 2: (B2, '')}}),
    # Rows dropped on the location whose own contents they are: not
    # moved again, noted, or fetched again (the rows kept chosen).
    ('INVWINDS', 'DSSAME', {'help': '', 'dialog': 0, 'sel': [1, 2],
                            'locs': {1: (B2, ''), 2: (B2, ''),
                                     3: (B2, '')}}),
    # The installed item's refusal not stopping the drop with its box.
    ('INVWINDS', 'DSREF', {'stopped': (3, 1, 2)}),
    ('INVWINDS', 'DSREFBK', {'dialog': 0, 'locs': {2: (A1, ''),
                                                   3: (B2, INST)}}),

    # INVWINSM: the record commands on several of the seed's items.
    ('INVWINSM', 'THREE', items([1, 2, 3])),
    # The dialog of the focus row (item 3), or of a single row.
    ('INVWINSM', 'ASKIN', {'caption': caption(1, 3, INTO)}),
    # No stop at the refusal (item 3 installed too), the stop box
    # missing or counting wrongly.
    ('INVWINSM', 'INSTOP', {'stopped': (2, 1, 3)}),
    # The rows after the refusal sent, or the list not fetched again
    # (several rows still selected).
    ('INVWINSM', 'INSTDONE', three(locs={1: (B2, INST), 2: (B2, ''),
                                         3: (A1, '')})),
    # The field not starting at row 1's location (its assembly's).
    ('INVWINSM', 'ASKOUT', {'caption': caption(1, 3, OUT),
                            'field': (100, 'B2')}),
    ('INVWINSM', 'OUTSTOP', {'stopped': (2, 1, 3)}),
    ('INVWINSM', 'OUTDONE', three(locs={1: (OFFICE, ''), 2: (B2, ''),
                                        3: (A1, '')})),
    # Row 1's refusal taken as a loop's stop (a box, the dialog gone),
    # not shown in the dialog on its field.
    ('INVWINSM', 'OUTREF', {'caption': caption(1, 3, OUT), 'error': 1}),
    ('INVWINSM', 'OUTREFBK', {'dialog': 0, 'locs': {
        1: (OFFICE, ''), 2: (B2, ''), 3: (A1, '')}}),
    ('INVWINSM', 'ASKMOVE', {'caption': caption(1, 3, MOVE),
                             'field': (100, 'OFF')}),
    # WA_KEEP of row 1 ending the whole command: the others not moved.
    ('INVWINSM', 'KEPT', three(locs={1: (OFFICE, ''), 2: (OFFICE, ''),
                                     3: (OFFICE, '')})),
    # Only the dialog's row moved; the note a single row's; no reload.
    ('INVWINSM', 'MOVED', three(help=DONE['move'] % 3, locs={
        1: (B2, ''), 2: (B2, ''), 3: (B2, '')})),
    # popup_input's right press still a left click: item 1 alone.
    ('INVWINSM', 'RKEEPDN', items([1, 2], menu=0)),
    ('INVWINSM', 'RKEEP', items([1, 2], menu=1)),
    ('INVWINSM', 'RMOVE', {'caption': caption(1, 2, MOVE)}),
    ('INVWINSM', 'RMOVED', three(help=DONE['move'] % 2, locs={
        1: (A1, ''), 2: (A1, ''), 3: (B2, '')})),
    # wl_right keeping the selection on a row not selected.
    ('INVWINSM', 'RONE', items([3], count='3 items', menu=1)),
    # SHIFT-F10's popup_click finding the row by LB_GETCURSEL (no row of
    # an extended box) and clicking it alone.
    ('INVWINSM', 'F10POP', items([1, 2], menu=1)),
    ('INVWINSM', 'F10MOVE', {'caption': caption(1, 2, MOVE)}),
    # One row chosen asking for "0 more".
    ('INVWINSM', 'ONEMOVE', {'caption': caption(1, 1, MOVE)}),
    # The question of the focus row's item, or one per row; Yes the
    # default.
    ('INVWINSM', 'DELASK', {'box': DELN % 2,
                            'line': ' Button 7 "&No" FOCUS DEFAULT'}),
    # No deleting, or dropping the selection.
    ('INVWINSM', 'DELNO', items([1, 2], ids=[1, 2, 3])),
    # Only the focus row deleted; the count not fetched again.
    ('INVWINSM', 'DELYES', {'ids': [3], 'count': '1 item', 'dialog': 0,
                            'help': DONE['del'] % 2}),
    ('INVWINSM', 'DELONE', {'box': 'Delete item 3 Hercules Graphics Card?'}),

    # INVWINSN: the loans of several items.
    # The form's help panel not saying it is for each item.
    ('INVWINSN', 'LEND', items([1, 2, 3], dialog=1,
                               caption='Lend', field=(101, '1'),
                               help=EACH % 3)),
    # Only row 1's loan added (the note a single loan's).
    ('INVWINSN', 'LENT', three(help=DONE['lend'] % 3)),
    ('INVWINSN', 'LOANSEL', {'title': 'Open loans and holds', 'nsel': 2,
                             'count': NSEL % 2}),
    ('INVWINSN', 'RETURNED', {'title': 'Open loans and holds', 'nsel': 1,
                              'count': '1 loan or hold', 'dialog': 0,
                              'help': DONE['end'] % 2}),

    # INVWINSO: level 1.  The refusal once per row (a second box after
    # the first), or the question asked before it.
    ('INVWINSO', 'THREE', items([1, 2, 3])),
    ('INVWINSO', 'NODUP', {'box': DENIED}),
    ('INVWINSO', 'NODUPBK', {'dialog': 0}),
    ('INVWINSO', 'NODEL', {'box': DENIED}),
    ('INVWINSO', 'NODELBK', {'dialog': 0}),
    ('INVWINSO', 'NOLEND', {'box': DENIED}),
    ('INVWINSO', 'NOLENDBK', {'dialog': 0, 'locs': {
        1: (OFFICE, ''), 2: (B2, ''), 3: (A1, '')}}),

    # INVWINEA: every row chosen, 260 items, 19 rows beside the preview.
    ('INVWINEA', 'OPEN', one(ITEMS260, sel=[1])),
    # CTRL-/ not reaching the frame; the count a byte's (4); the rows of
    # the page alone counted (19); the help panel left counting.
    ('INVWINEA', 'ALL', chosen(260, 19, help='', line=PV_ID % 1)),
    # A fill selecting the focus row alone; PGDN, END or HOME ending it.
    ('INVWINEA', 'ALLPG2', chosen(260, 19, sel=list(range(20, 39)),
                                  line=PV_ID % 20)),
    ('INVWINEA', 'ALLEND', chosen(260, 19, sel=list(range(242, 261)))),
    # ENTER on another row than the focus row; ESC from it fetching the
    # list again, which ends the choice.
    ('INVWINEA', 'ALLVIEW', {'caption': 'Item 20', 'count': NALL % 260}),
    ('INVWINEA', 'ALLBACK', chosen(260, 19)),
    # Each of these keeping every row chosen, or leaving another row than
    # the focus row: UP, DOWN, SHIFT-DOWN (a range from an old anchor), a
    # click, a CTRL click (the row toggled out of all).
    ('INVWINEA', 'UPONE', one(ITEMS260, sel=[19])),
    ('INVWINEA', 'DOWNONE', one(ITEMS260, sel=[20])),
    ('INVWINEA', 'SHIFTONE', one(ITEMS260, sel=[21])),
    ('INVWINEA', 'CLICKONE', one(ITEMS260, sel=[22])),
    ('INVWINEA', 'CTRLONE', one(ITEMS260, sel=[24])),
    # SHIFT-F8 keeping all chosen, or not starting the add mode.
    ('INVWINEA', 'ADDONE', one(NADD % 1, sel=[24])),
    # CTRL-/ leaving the add mode on: the UP after it then keeps rows.
    ('INVWINEA', 'ADDALL', chosen(260, 19)),
    ('INVWINEA', 'ADDUP', one(ITEMS260)),
    # CTRL-\ not taken, or not ending a choice of all, of three rows or
    # the add mode.
    ('INVWINEA', 'NONE', one(ITEMS260)),
    ('INVWINEA', 'THREE', {'nsel': 3, 'count': NSEL % 3}),
    ('INVWINEA', 'THREENO', one(ITEMS260)),
    ('INVWINEA', 'ADDNO', one(ITEMS260)),
    # A heading's switch, Next order or F5's reload keeping all chosen.
    ('INVWINEA', 'HEADONE', one(ITEMS260, title='Items by maker')),
    ('INVWINEA', 'ORDERONE', one(ITEMS260,
                                 title='Items by serial number')),
    ('INVWINEA', 'F5ONE', one(ITEMS260, sel=[1])),
    # A save of the focus row's item keeping all chosen (no reload).
    ('INVWINEA', 'EDITONE', one(ITEMS260, sel=[1], help='Item 1 saved')),
    # The Find box's results taking the choice over; all of one row not
    # chosen as all, or DEL then asking as for every item of a list.
    ('INVWINEA', 'FINDONE', one('1 item', sel=[3], title=FOUND)),
    ('INVWINEA', 'ONEALL', chosen(1, 1, title=FOUND)),
    ('INVWINEA', 'ONEDEL', {'box': 'Delete item 3 Hercules Graphics '
                                   'Card?'}),
    # The count panel still saying "All 1 selected" once CTRL-\ or a
    # click has ended a choice of all in a list of one row.
    ('INVWINEA', 'ONENONE', one('1 item', sel=[3], title=FOUND)),
    ('INVWINEA', 'ONECLICK', one('1 item', sel=[3], title=FOUND)),
    # The Record menu without the entry, or the entry not choosing.
    ('INVWINEA', 'MENU', {'menu': 1, 'line': '  ' + SELALL}),
    ('INVWINEA', 'MENUALL', chosen(260, 19, menu=0)),
    # A right press on a row ending the choice before the pop-up.
    ('INVWINEA', 'RPOP', {'menu': 1, 'nsel': 19, 'count': NALL % 260}),
    ('INVWINEA', 'RPOPBK', chosen(260, 19)),
    # An Esc not stopping the collecting, or the stop dropping the
    # choice of all made before it.
    ('INVWINEA', 'STOPPED', chosen(260, 19, help='')),
    # CTRL-\ doing nothing while the preview has the focus.
    ('INVWINEA', 'PVNONE', one(ITEMS260, sel=[3])),
    # A choice of all that an Esc stops leaving the focus row alone
    # where three rows were chosen.
    ('INVWINEA', 'STOP3', {'sel': [3, 4, 5], 'count': NSEL % 3,
                           'dialog': 0, 'help': ''}),
    # A \ without CTRL ending the choice as CTRL-\ does.
    ('INVWINEA', 'BSLASH', chosen(260, 19)),
    # CTRL-/ choosing in a list of one row.
    ('INVWINEA', 'TREE', {'title': 'Locations', 'count': '3 locations',
                          'tsel': ['Office']}),

    # INVWINEB: Move and DEL on all 260.  The caption a byte's count;
    # the loop reading the page's rows alone (items past row 20 left at
    # Box B2); the list not fetched again, or all still chosen after it.
    ('INVWINEB', 'ASKMOVE', {'caption': caption(1, 260, MOVE),
                             'count': NALL % 260}),
    ('INVWINEB', 'MOVED', one(ITEMS260, help=DONE['move'] % 260, locs={
        1: (A1, ''), 20: (A1, '')})),
    ('INVWINEB', 'MOVEDPG', one(ITEMS260, locs={21: (A1, ''),
                                                40: (A1, '')})),
    # The question of several rows, which does not say that rows not
    # shown go too; No deleting, or ending the choice.
    ('INVWINEB', 'DELASK', {'box': DELALL % 260}),
    ('INVWINEB', 'DELNO', chosen(260, 20)),
    # The Esc not stopping the loop; the stop after another row than the
    # first; the rows after it deleted; all still chosen after the stop.
    ('INVWINEB', 'DELSTOP', {'box': HALTED % (1, 260)}),
    ('INVWINEB', 'DELHALT', one('259 items', sel=[21])),
    ('INVWINEB', 'ALL259', chosen(259, 20)),
    # Rows left (a delete of the page's rows, or of 3: 259 in a byte).
    ('INVWINEB', 'DELDONE', {'count': '0 items', 'nsel': 0, 'dialog': 0,
                             'help': DONE['del'] % 259}),
    # CTRL-/ on the empty list: a choice of none, or a box.
    ('INVWINEB', 'EMPTYALL', {'count': '0 items', 'nsel': 0, 'dialog': 0,
                              'help': ''}),

    # INVWINEC: a refusal in the middle, item 50 installed in item 1.
    # The loop going on past it (item 51 moved), stopping elsewhere, or
    # the list not fetched again with one row chosen.
    ('INVWINEC', 'MOVESTOP', {'stopped': (50, 49, 260)}),
    ('INVWINEC', 'MOVEHALT', one(ITEMS260, locs={1: (A1, ''),
                                                 20: (A1, '')})),
    ('INVWINEC', 'MOVEPG3', one(ITEMS260, locs={
        49: (A1, ''), 50: (A1, INST), 51: (B2, ''), 60: (B2, '')})),
    ('INVWINEC', 'ASKIN', {'caption': caption(1, 260, INTO)}),
    ('INVWINEC', 'INSTOP', {'stopped': (2, 1, 260)}),
    ('INVWINEC', 'INSTHALT', one(ITEMS260, locs={1: (A1, INST),
                                                 3: (A1, '')})),
    ('INVWINEC', 'OUTSTOP', {'stopped': (2, 1, 260)}),
    ('INVWINEC', 'OUTHALT', one(ITEMS260, locs={1: (OFFICE, ''),
                                                3: (A1, '')})),

    # INVWINED: loans and holds of all 260.  A byte's count in the help
    # panel or the notes; the loans list's choice not reaching past its
    # page (loans left open); Return or release titled for a loan when
    # the rows are reservations (the kind of a row whose text the list
    # does not hold).
    ('INVWINED', 'LEND', {'caption': 'Lend', 'help': EACH % 260}),
    ('INVWINED', 'LENT', one(ITEMS260, help=DONE['lend'] % 260)),
    ('INVWINED', 'LOANS', {'title': 'Open loans and holds', 'nsel': 1,
                           'count': HOLDS % 260}),
    ('INVWINED', 'LOANSALL', chosen(260, 19)),
    ('INVWINED', 'ASKRET', {'caption': caption(1, 260, RET)}),
    ('INVWINED', 'RETURNED', {'count': HOLDS % 0, 'nsel': 0, 'dialog': 0,
                              'help': DONE['end'] % 260}),
    ('INVWINED', 'RESERVE', {'caption': 'Reserve', 'help': EACH % 260}),
    ('INVWINED', 'RESERVED', one(ITEMS260, help=DONE['res'] % 260)),
    ('INVWINED', 'ASKREL', {'caption': caption(261, 260, REL),
                            'count': NALL % 260}),
    ('INVWINED', 'RELEASED', {'count': HOLDS % 0, 'nsel': 0, 'dialog': 0,
                              'help': DONE['end'] % 260}),

    # INVWINEE: level 1.  Choosing refused; a question or a form before
    # the refusal, or a second box; the choice ended by it; a record
    # changed (the items' audit log longer).
    ('INVWINEE', 'AUDIT0', {'title': 'Audit log', 'count': AUDITED}),
    ('INVWINEE', 'ALL', chosen(260, 20, line=READER)),
    ('INVWINEE', 'NODEL', {'box': DENIED}),
    ('INVWINEE', 'NODELBK', chosen(260, 20)),
    ('INVWINEE', 'NOLEND', {'box': DENIED}),
    ('INVWINEE', 'MENU', {'menu': 1, 'line': '  ' + SELALL,
                          'count': NALL % 260}),
    ('INVWINEE', 'ITEMS', one(ITEMS260, sel=[1])),
    ('INVWINEE', 'AUDIT1', {'title': 'Audit log', 'count': AUDITED}),

    # INVWINEF: 4097 rows, one more than a choice holds.  The list
    # chosen all the same, or its first 4096 rows; no box; the three
    # rows chosen before dropped by the refusal, or the list's count
    # (ONELESS); 4096 rows refused too.
    ('INVWINEF', 'OPEN', one('4097 items', sel=[1])),
    ('INVWINEF', 'OVER', {'box': OVER % 4096, 'count': NSEL % 3,
                          'sel': [1, 2, 3], 'help': COLLECT % 4080}),
    ('INVWINEF', 'OVERBK', {'sel': [1, 2, 3], 'count': NSEL % 3,
                            'dialog': 0, 'help': ''}),
    ('INVWINEF', 'ONELESS', one('4096 items', sel=[4096])),
    ('INVWINEF', 'CAP', chosen(4096, 20)),
    ('INVWINEF', 'CAPEND', chosen(4096, 20,
                                  sel=list(range(4077, 4097)))),

    # INVWINEH: 4120 rows.  The walk going on to the list's end after
    # the set refused a key (the help panel counting 4100), or the stop
    # dropping the list's count.
    ('INVWINEH', 'OVER', {'box': OVER % 4096, 'count': '4120 items',
                          'nsel': 1, 'help': COLLECT % 4080}),
    ('INVWINEH', 'OVERBK', one('4120 items', sel=[1], help='')),

    # INVWINEI: 303 rows, DEL of all stopped at item 270, which has a
    # part.  The rows deleted counted in a byte: 290 items left, not 34.
    ('INVWINEI', 'DELSTOP', {'stopped': (270, 269, 303)}),
    ('INVWINEI', 'DELHALT', one('34 items', sel=[270])),

    # INVWINEG: the contents beside the tree, 258 items at Box B2.  The
    # choice kept when the tree moves and its contents load again; a
    # press held on a row ending it, so that the drag moves one item.
    ('INVWINEG', 'CALL', chosen(258, 19, tsel=['Box B2'])),
    ('INVWINEG', 'CMOVED', {'count': '3 locations', 'tsel': ['Shelf A1'],
                            'dialog': 0}),
    ('INVWINEG', 'CONE', one('258 items in Box B2', sel=[2])),
    ('INVWINEG', 'CHELD', chosen(258, 19)),
    ('INVWINEG', 'COVER', {'count': NALL % 258, 'nsel': 19,
                           'help': DRAG}),
    ('INVWINEG', 'CDROPPED', {'count': '0 items in Box B2', 'nsel': 0,
                              'dialog': 0, 'help': DONE['move'] % 258}),

    # INVWINEJ: the lists that have a pane, and the panes.  All three
    # assemblies chosen, and kept by END and HOME (a pane's load ending
    # the master's choice).
    ('INVWINEJ', 'MALL', chosen(3, 3, title='Assemblies', sel=[1, 5, 7])),
    ('INVWINEJ', 'MEND', chosen(3, 3)),
    ('INVWINEJ', 'MHOME', chosen(3, 3)),
    # Both parts chosen in the pane (the count the master's, or CTRL-/
    # taken for the master), and both taken out (the pane's first row
    # alone): item 1 no assembly, one row chosen.
    ('INVWINEJ', 'PALL', chosen(2, 2, sel=[3, 4])),
    ('INVWINEJ', 'POUT', {'ids': [5, 7], 'sel': [5], 'dialog': 0,
                          'count': '2 assemblies',
                          'help': DONE['out'] % 2}),
    # The categories taking several rows.
    ('INVWINEJ', 'CATS', {'title': 'Categories', 'nsel': 1,
                          'count': '10 categories'}),
    # A category's items chosen and lent, all three; a category's load
    # keeping the choice of the one before.
    ('INVWINEJ', 'CALL', chosen(3, 3, sel=[1, 5, 7])),
    ('INVWINEJ', 'CLENT', one('3 items', help=DONE['lend'] % 3)),
    ('INVWINEJ', 'CNEXT', {'nsel': 1, 'count': '10 categories'}),
    # The loans chosen beside their history, and all three ended (the
    # kind of a row whose text the list does not hold).
    ('INVWINEJ', 'LALL', chosen(3, 3, title='Open loans and holds')),
    ('INVWINEJ', 'LRET', {'count': HOLDS % 0, 'nsel': 0, 'dialog': 0,
                          'help': DONE['end'] % 3}),
    # A user's audit entries taking several rows.
    ('INVWINEJ', 'UAUD', {'title': 'Users', 'nsel': 1,
                          'count': '2 entries'}),
)


def loan_rows(lines):
    """The loan list's rows: (loan, kind, item, holder, selected)."""
    rows, _ = frame_list(lines)
    cells = colchk.LOAN
    out = []
    for text, sel, _ in rows:
        f = []
        at = 0
        for n in cells:
            f.append(text[at:at + abs(n)].strip())
            at += abs(n) + 1
        if f[1].isdigit():
            out.append((int(f[1]), f[2], int(f[3]), f[5], sel))
    return out


def loans_bad(cache):
    """Why INVWINSN's loans and their return fail, one line each."""
    snaps = splitchk.load(cache, 'INVWINSN') or {}
    got = dict((s, loan_rows(snaps.get(s) or ()))
               for s in ('LOANS', 'LOANSEL', 'RETURNED'))
    bad = []
    # One loan per item chosen, of its own item, the borrower typed once.
    lent = sorted((item, kind, who) for _, kind, item, who, _ in
                  got['LOANS'])
    want = [(i, 'On loan', 'Carol King') for i in (1, 2, 3)]
    if lent != want:
        bad.append('INVWINSN.SNP LOANS: %s, want %s' % (lent, want))
    chosen = [r[0] for r in got['LOANSEL'] if r[4]]
    left = [r[0] for r in got['LOANSEL'] if not r[4]]
    if len(chosen) != 2 or len(left) != 1:
        return bad + ['INVWINSN.SNP LOANSEL: %s, want 2 of 3 selected' %
                      got['LOANSEL']]
    # The return of the focus row alone, or a dialog naming it.
    want = caption(chosen[0], 2, RET)
    act = active(snaps.get('ASKRET') or ())
    if not act or not act[0].startswith('#32770 "%s" ' % want):
        bad.append('INVWINSN.SNP ASKRET: %s, want "%s"' % (
            act[:1], want))
    ended = [r[0] for r in got['RETURNED']]
    if ended != left:
        bad.append('INVWINSN.SNP RETURNED: loans %s open, want %s' % (
            ended, left))
    return bad


# WLISTTZ: (snapshot, the start of the title's what, the bar's position
# and range, the list's count, the rows chosen, the IDs shown, those
# SELECTED), the comment above each the mistake it catches.
WLISTTZ = 'WLISTTZ'
STEPS_Z = (
    ('START', 'WLISTT', '0/258', '260', '1', [1, 2], [1]),
    # CTRL-/ choosing the window's rows only, or none.
    ('ALL', 'ALL 0', '0/258', '260', '260*', [1, 2], [1, 2]),
    # A scroll bar's page or thumb ending the choice.
    ('ALLPAGE', 'ALL 0', '2/258', '260', '260*', [3, 4], [3, 4]),
    ('ALLTHUMB', 'ALL 0', '5/258', '260', '260*', [6, 7], [6, 7]),
    # The last row chosen not the list's last, or its text left unset.
    ('ALLLAST', 'LAST 260 []', '5/258', '260', '260*', [6, 7], [6, 7]),
    # A row added since the choice drawn as chosen.
    ('ADDED', 'LAST 260 []', '259/259', '261', '260*', [260, 261], [260]),
    # wl_deselect keeping the choice.
    ('NONE', 'LAST 260 []', '259/259', '261', '1', [260, 261], [261]),
    ('TWO', 'LAST 260 []', '0/259', '261', '2', [1, 2], [1, 2]),
    # wl_chosen giving no text outside a choice of all.
    ('TWOLAST', 'LAST 2 [2       ]', '0/259', '261', '2', [1, 2], [1, 2]),
    # A choice of all that fails or is stopped dropping the rows chosen
    # before it, the list's count or the bar's range.
    ('FAILBOX', 'LAST 2 [2       ]', '0/259', '261', '2', [1, 2], [1, 2]),
    ('FAILED', 'ALL 102', '0/259', '261', '2', [1, 2], [1, 2]),
    ('STOPPED', 'ALL 0', '0/259', '261', '2', [1, 2], [1, 2]),
    ('ALLAGAIN', 'ALL 0', '0/259', '261', '261*', [1, 2], [1, 2]),
    # ... or a choice of all made before it.
    ('ALLKEPT', 'ALL 0', '0/259', '261', '261*', [1, 2], [1, 2]),
    # An empty list chosen as all, of no rows.
    ('EMPTY', 'ALL 0', '0/0', '0', '0', [], []),
    ('ALLBACK', 'ALL 0', '0/259', '261', '261*', [1, 2], [1, 2]),
    # A letter's seek keeping the choice.
    ('LETTER', 'LETTERS 0', '0/259', '261', '1', [1, 2], [2]),
    # A window of one row: 239 pages surveyed, the list not counted.  A
    # choice stopped collapsing the bar's range; one as long as the
    # survey chosen all the same.
    ('ONEROW', 'REOPEN ', '0/238', '-', '1', [1], [1]),
    ('CAPSTOP', 'ALL 0', '0/238', '-', '1', [1], [1]),
    ('CAPOVER', 'ALL 4', '0/238', '-', '1', [1], [1]),
)
TITLE_Z = re.compile(r'WLISTT "(.*) #\d+ (\d+/\d+) (\S+) (\d+\*?)"')


def wlistt_bad(cache):
    """Why WLISTTZ's snapshots differ from STEPS_Z, one line each."""
    snaps = splitchk.load(cache, WLISTTZ) or {}
    bad = []
    for step in STEPS_Z:
        lines = snaps.get(step[0]) or []
        m = [TITLE_Z.match(ln) for ln in lines if ln.startswith('WLISTT "')]
        rows = [r for _, _, rs in boxes(lines) for r in rs
                if r[0][:1].isdigit()]
        got = (m[0].groups() if m and m[0] else (None,) * 4) + (
            [int(r[0].split()[0]) for r in rows],
            [int(r[0].split()[0]) for r in rows if r[1]])
        if not (got[0] or '').startswith(step[1]) or \
                got[1:] != step[2:] or \
                (step[0] == 'FAILBOX') != any(
                    ln.startswith('#32770 ') for ln in lines):
            bad.append('%s.SNP %s: %s, want %s' % (WLISTTZ, step[0], got,
                                                   step[1:]))
    if sorted(snaps) != sorted(s[0] for s in STEPS_Z):
        bad.append('%s.SNP: snapshots %s' % (WLISTTZ, sorted(snaps)))
    return bad


def empty(pane):
    """1 when the list of pane shows no row but its empty text: one row,
    not SELECTED."""
    return not pane['nsel'] and len(pane['rows']) == 1


def all_bad(gold, snap, lines):
    """Why the Record menu's Select all fails in a snapshot of the
    frame, one line each."""
    line, ps, _ = splitchk.view(lines)
    tree, cont = ps.get(framchk.C_LIST), ps.get(framchk.C_CLIST)
    if line is None or (cont and not tree['focus'] and not cont['focus']):
        return []
    wid = framchk.C_CLIST if cont and cont['focus'] else framchk.C_LIST
    pane = ps.get(wid)
    want = []
    if pane and not single(line, wid):
        want = [SELALL + (' DISABLED' if empty(pane) else '')]
    have = [ln.strip() for ln in lines if ln.strip().startswith(SELALL)]
    if have == want:
        return []
    return ['%s.SNP %s: the Record menu has %s, want %s' % (
        gold, snap, have, want)]


def main():
    cache = {}
    bad = []
    for f in sorted(os.listdir(colchk.GOLD)):
        if f.endswith('.SNP'):
            snaps = splitchk.load(cache, f[:-4])
            bad += golden_bad(f[:-4], snaps or {})
            for snap, lines in sorted((snaps or {}).items()):
                bad += all_bad(f[:-4], snap, lines)
    for case in CASES:
        bad += case_bad(case, cache)
    bad += loans_bad(cache)
    bad += wlistt_bad(cache)
    bad = list(dict.fromkeys(bad))
    for why in bad:
        print('selchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('selchk: %d goldens ok, %d cases ok, loans and WLISTTZ ok' % (
        len(cache), len(CASES)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
