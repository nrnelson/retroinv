"""splitchk.py - INVWIN's split view of the locations in the snapshots of
its key scripts, and the settings key of its width, against what is
worked out apart from the program.

usage: python3 -I TEST/splitchk.py

The Locations screen shows the tree (WLIST 208), the splitter (WSPLIT
220, 'AT x' its left edge in pixels, 'LIMITS lo hi' those of a drag)
and the selected location's contents (WLIST 221), TEST/framchk.py
checking their lines and the count panel in every snapshot.  Here each case of CASES names a golden,
a snapshot in it and what must hold there:

  title    the frame titled "RETROINV - title"
  split    1 when the split shows, 0 when it does not
  sel      the name of the tree's selected location
  ids      the IDs of the contents' rows, in order; on a screen of one
           list, that list's
  idset    the same, in any order
  csel     the ID of the contents' selected row
  focus    the pane whose list box has the focus, 0 the tree, 1 the
           contents
  count    the status bar's count panel
  rec      the Record menu's entries, id and text: 'tree' the tree's
           (TREE_REC), 'alone' the tree's without Next pane, 'contents'
           those of a location's contents as their own screen shows them
           (INVWIN6's CONTENTS), with Next pane
  states   {id: 1 when grayed} of Record entries
  checked  {id: 1 when checked} of Record entries
  menu     1 when a menu window shows
  dialog   1 when a dialog box or message box shows
  tool     the tool bar's text
  loc      (id, name): the contents' row of item id in location name
  treerow  a location the tree shows

The widths, worked out with the list font's average character width cw
(the tree of TEST/WIN/SPLIT20.CFG's WINSPLIT=20 is 20 * cw wide) and,
as TEST/colchk.py has them, the tree's row end TREE_END, the item rows'
ITEM_END, a box's border, and the maximized frame's MAXW wide room:

  a pane whose box, its width less the border, is narrower than its
  rows shows a horizontal bar of their end less that width, else none;
  INVWINSW keeps one x, a multiple of 20, over a new size of the frame,
  WINPANE.LOC=1,30 of its settings setting no width;
  INVWINSX's WINSPLIT=255, wider than the frame, is held so that the
  contents keep IW_PANEMIN (SRC/WIN/INVWIN.C) characters: maximized, x
  is MAXW - WSP_WIDTH (SRC/WIN/WSPLIT.H) - IW_PANEMIN * cw;
  maximized, INVWINSW's and INVWINSX's splitter holds the limits
  IW_PANEMIN * cw and MAXW - WSP_WIDTH - IW_PANEMIN * cw;
  INVWINSP's automatic tree shows its Name and Type columns, its Code
  behind the bar;
  no window of these goldens OVERLAP.

INVWINSY's WINPANE.LOC=0 (TEST/WIN/PANE0.CFG) shows the tree alone: no
split in any snapshot of it, its Record menu is the tree's without Next
pane, and its selection stays over a new size of the frame.  INVWINSZ's
contents are counted under the name a restore gives their location, and
load again once a restore brings back the only location, deleted.

TEST/GOLDEN/WIN/INV.CFG, INVWINHA's settings save, holds WINSPLIT=0 and
no WINPANE line but those it had, KEPT (TEST/WIN/INV.CFG).

A golden, snapshot or line missing, or one different, fails.  Exit
status 1 if any check fails.
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WIN = os.path.join(os.path.dirname(HERE), 'SRC', 'WIN')
sys.dont_write_bytecode = True
_SPEC = importlib.util.spec_from_file_location(
    'framchk', os.path.join(HERE, 'framchk.py'))
framchk = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(framchk)
colchk = framchk.colchk
menuchk = framchk.menuchk

TREE_END = 340                  # the tree's row end (colchk)
CODE_ROOM = 223                 # Type's room ends a gap before Code's x
ITEM_END = 570                  # an item row's end, by ID
BORDER = 2                      # a box's border, both sides
MAXW = 640                      # the room of the maximized frame
WIDE = 20                       # SPLIT20.CFG's WINSPLIT
CFGLINE = 'WINSPLIT=0'
KEPT = ['WINPANE.ITEM=0', 'WINPANE.ASSY=0', 'WINPANE.LOAN=0',
        'WINPANE.USER=0']

TOOL = framchk.TOOL
TREE_TOOL = 'New|Open|Edit|Delete|-|Move|~Install|~Lend|-|*|Find'
# Next pane, which either pane of the split view has before Back.
PANE = (629, 'Ne&xt pane?F6')
BACK = 539
TREE_REC = ((525, '&Open?Enter'), (1093, '&Edit...?Ctrl+E'),
            (557, '&New...?Ins'), (558, '&Delete?Del'), (0, ''),
            (1536, '&Move...'), (0, ''), PANE, (BACK, '&Back?Esc'))
ALONE_REC = tuple(e for e in TREE_REC if e != PANE)
TREE1 = {525: 0, 1093: 1, 557: 1, 558: 1, 1536: 1, 629: 0, BACK: 0}
# Lab's contents at level 1: empty, so Open (525) and Parts (1542), which
# need a selected row, are grayed too.
CONT1 = {525: 1, 1093: 1, 557: 1, 558: 1, 1536: 1, 1092: 1, 1100: 1,
         1542: 1, 1547: 0, 629: 0, BACK: 0}
BELOW = 1548                    # Everything below
ALL = 'Office (all items, everything below)'
THIS = 'Office (all items, this location)'
LOCS = '3 locations'


def tree(sel, ids, **more):
    """A split with the tree focused on sel, the contents ids."""
    out = {'title': 'Locations', 'split': 1, 'sel': sel, 'ids': ids,
           'focus': 0}
    out.update(more)
    return out


def contents(sel, ids, count, **more):
    """A split with the contents of sel focused."""
    out = tree(sel, ids, focus=1, count=count)
    out.update(more)
    return out


def alone(sel):
    """The tree without its pane, focused on sel."""
    return {'title': 'Locations', 'split': 0, 'sel': sel, 'focus': 0,
            'count': LOCS, 'rec': 'alone', 'tool': TREE_TOOL}


CASES = (
    # Opened on Office, its contents at once; each arrow loads the
    # contents of the location it selects.
    ('INVWINSP', 'LOCS', tree('Office', [1, 3, 2], count=LOCS, rec='tree',
                              tool=TREE_TOOL, csel=1)),
    ('INVWINSP', 'A1', tree('Shelf A1', [3, 2], count=LOCS)),
    ('INVWINSP', 'B2', tree('Box B2', [2], count=LOCS)),
    ('INVWINSP', 'A1BACK', tree('Shelf A1', [3, 2], count=LOCS)),
    # TAB: the contents focused, their count, menu and buttons; their
    # keys move them alone; Everything below reloads them, the panel
    # following.
    ('INVWINSP', 'PANE', contents('Shelf A1', [3, 2], '2 items in Shelf A1',
                                  rec='contents', tool=TOOL, csel=3)),
    ('INVWINSP', 'CDOWN', contents('Shelf A1', [3, 2], '2 items in Shelf A1',
                                   csel=2)),
    ('INVWINSP', 'HERE', contents('Shelf A1', [3], '1 item in Shelf A1',
                                  checked={BELOW: 0})),
    # ESC: the tree focused, the contents kept until it moves; a move
    # shows everything below again.
    ('INVWINSP', 'ESCTREE', tree('Shelf A1', [3], count=LOCS, rec='tree',
                                 tool=TREE_TOOL)),
    ('INVWINSP', 'B2AGAIN', tree('Box B2', [2], count=LOCS)),
    ('INVWINSP', 'A1RESET', tree('Shelf A1', [3, 2], count=LOCS)),
    ('INVWINSP', 'OFFICE', tree('Office', [1, 3, 2], count=LOCS)),
    # ENTER: the contents' own screen; ESC back keeps what was set there.
    ('INVWINSP', 'FULL', {'title': ALL, 'split': 0, 'ids': [1, 3, 2],
                          'count': '3 items'}),
    ('INVWINSP', 'FULLHERE', {'title': THIS, 'split': 0, 'ids': [1],
                              'count': '1 item'}),
    ('INVWINSP', 'SPLITBK', tree('Office', [1], count=LOCS)),
    ('INVWINSP', 'BKPANE', contents('Office', [1], '1 item in Office',
                                    checked={BELOW: 0})),
    # Parts from the contents: the item's parts, a screen of its own;
    # their ESC back to the split, the contents focused as they were.
    ('INVWINSP', 'PARTS', {'split': 0}),
    ('INVWINSP', 'PARTSBK', contents('Office', [1], '1 item in Office')),
    # F6 and SHIFT-F6, TAB and SHIFT-TAB: the focus to the other pane.
    ('INVWINSP', 'F6TREE', tree('Office', [1], count=LOCS)),
    ('INVWINSP', 'SF6PANE', contents('Office', [1], '1 item in Office')),
    ('INVWINSP', 'STABTREE', tree('Office', [1], count=LOCS)),
    ('INVWINSP', 'F6PANE', contents('Office', [1], '1 item in Office')),
    ('INVWINSP', 'ITEMS', {'title': 'Items by ID', 'split': 0}),
    # The Record menu and pop-up of each pane.
    ('INVWINSQ', 'LOCS', tree('Office', [1, 3, 2], count=LOCS)),
    ('INVWINSQ', 'TREEMENU', tree('Office', [1, 3, 2], menu=1, rec='tree',
                                  tool=TREE_TOOL)),
    ('INVWINSQ', 'TREEPOP', {'menu': 1, 'rec': 'tree', 'tool': TREE_TOOL}),
    ('INVWINSQ', 'CMENU', {'menu': 1, 'rec': 'contents', 'tool': TOOL}),
    ('INVWINSQ', 'CPOP', {'menu': 1, 'rec': 'contents', 'tool': TOOL}),
    # A copy in the contents loads them again, counted.
    ('INVWINSQ', 'COPIED', {'split': 1, 'sel': 'Office', 'focus': 1,
                            'idset': [1, 2, 3, 4],
                            'count': '4 items in Office'}),
    # New location from the contents: the tree focused first, the
    # location added selected there, its contents empty.
    ('INVWINSQ', 'NEWDLG', {'dialog': 1}),
    ('INVWINSQ', 'NEWLOC', tree('Lab', [], count='4 locations')),
    # A letter's seek in the tree loads the contents: everything below
    # Shelf A1, the copy (4) in Box B2 among them.
    ('INVWINSQ', 'SEEK', tree('Shelf A1', [3, 2, 4])),
    # A rename loads the tree alone; Reindex both.  The copy is still
    # below the renamed shelf.
    ('INVWINSQ', 'RENAMED', tree('Shelf Z9', [3, 2, 4],
                                 loc=(3, 'Shelf A1'))),
    ('INVWINSQ', 'REINDEX', {'dialog': 1}),
    ('INVWINSQ', 'REINDEXD', {'split': 1, 'treerow': 'Shelf Z9',
                              'loc': (3, 'Shelf Z9')}),
    # Level 1: Next pane not grayed, TAB moving the focus.  The tree
    # lists Lab, added at the top level, before Office, so it opens on
    # Lab and its empty contents.
    ('INVWINSQ', 'RLOCS', {'split': 1, 'sel': 'Lab', 'focus': 0,
                           'idset': [], 'rec': 'tree',
                           'states': TREE1}),
    ('INVWINSQ', 'RPANE', {'split': 1, 'sel': 'Lab', 'focus': 1,
                           'rec': 'contents', 'states': CONT1,
                           'count': '0 items in Lab'}),
    # The set width, kept with the selection over a new size.
    ('INVWINSW', 'LOCS', tree('Office', [1, 3, 2])),
    ('INVWINSW', 'B2', tree('Box B2', [2])),
    ('INVWINSW', 'MAX', tree('Box B2', [2])),
    ('INVWINSW', 'RESTORE', tree('Box B2', [2])),
    ('INVWINSX', 'LOCS', tree('Office', [1, 3, 2])),
    ('INVWINSX', 'MAX', tree('Office', [1, 3, 2])),
    # WINPANE.LOC=0: the tree alone, the pane's keys doing nothing; ENTER
    # still opens the contents, and ESC comes back to the tree alone.
    ('INVWINSY', 'LOCS', alone('Office')),
    ('INVWINSY', 'A1', alone('Shelf A1')),
    ('INVWINSY', 'MAX', alone('Shelf A1')),
    ('INVWINSY', 'RESTORE', alone('Shelf A1')),
    ('INVWINSY', 'KEYS', alone('Shelf A1')),
    ('INVWINSY', 'FULL', {'title': 'Office > Shelf A1 (all items, '
                                   'everything below)',
                          'split': 0, 'ids': [3, 2], 'count': '2 items'}),
    ('INVWINSY', 'BACK', alone('Shelf A1')),
    # The data changed under the pane: a restore's name on the tree
    # opened again, and the contents back after the tree was empty.
    ('INVWINSZ', 'RENAMED', contents('Bureau', [], '0 items in Bureau')),
    ('INVWINSZ', 'REOPENED', contents('Office', [], '0 items in Office')),
    ('INVWINSZ', 'EMPTY', {'title': 'Locations', 'split': 1, 'sel': None,
                           'focus': 0, 'count': '0 locations'}),
    ('INVWINSZ', 'AGAIN', tree('Office', [], count='1 location')),
    ('INVWINSZ', 'PANE', contents('Office', [], '0 items in Office')),
)

ROWID = re.compile(r'\s*(\d+) ')


def item_id(row):
    """The ID of an item's row, or None (an empty list's text)."""
    m = ROWID.match(row)
    return int(m.group(1)) if m else None


def item_loc(row):
    """The location of an item's row by ID."""
    cells = colchk.ITEM
    start = sum(abs(c) + 1 for c in cells[:6])
    return row[start:start + abs(cells[6])].strip()


def record(lines):
    """The Record menu's entries of a snapshot: (id, text, state)."""
    for title, entries in menuchk.menus(menuchk.frame(lines)[1]):
        if title == 'Re&cord':
            return entries
    return []


def contents_rec(cache):
    """The Record entries of a location's contents beside the tree, as
    (id, text): those of their own screen in INVWIN6's CONTENTS, with
    Next pane before Back."""
    lines = (load(cache, 'INVWIN6') or {}).get('CONTENTS', ())
    out = []
    for e, t, _ in record(lines):
        if e == BACK:
            out.append(PANE)
        out.append((e, t))
    return tuple(out)


def load(cache, gold):
    """The snapshots of golden gold, cached."""
    if gold not in cache:
        cache[gold] = colchk.snaps(gold)
    return cache[gold]


def defines(*paths):
    """The numbers #defined in the files of SRC/WIN named."""
    out = {}
    for p in paths:
        out.update(framchk.defines(os.path.join(WIN, p)))
    return out


def view(lines):
    """The frame's line, its children by ID and its count panel."""
    line, _, _, sbar, after = framchk.chrome(lines)
    m = framchk.WSTAT.match(sbar or '')
    panels = m.group(2).split('|') if m else []
    return line, framchk.panes(lines, after), \
        panels[2] if len(panels) > 2 else None


def case_bad(case, cache):
    """Why case fails, one line each."""
    gold, snap, want = case
    where = '%s.SNP %s' % (gold, snap)
    snaps = load(cache, gold)
    if snaps is None:
        return ['%s.SNP: cannot read it' % gold]
    lines = snaps.get(snap)
    if lines is None:
        return ['%s.SNP: no snapshot %s' % (gold, snap)]
    line, ps, count = view(lines)
    if line is None:
        return ['%s: no frame' % where]
    tops = [ln for ln in lines if not ln.startswith(' ')]
    split = framchk.C_CLIST in ps and framchk.C_SPLIT in ps
    tree = ps.get(framchk.C_LIST, {'rows': [], 'sel': None, 'focus': 0})
    cont = ps.get(framchk.C_CLIST, {'rows': [], 'sel': None, 'focus': 0})
    items = cont if split else tree
    got = {}
    for key in want:
        if key == 'title':
            got[key] = framchk.title_of(line)
        elif key == 'split':
            got[key] = int(split)
        elif key == 'sel':
            got[key] = tree['sel'] and framchk.loc_name(tree['sel'])
        elif key == 'ids':
            got[key] = [i for i in map(item_id, items['rows'])
                        if i is not None]
        elif key == 'idset':
            got[key] = sorted(i for i in map(item_id, items['rows'])
                              if i is not None)
        elif key == 'csel':
            got[key] = cont['sel'] and item_id(cont['sel'])
        elif key == 'focus':
            boxes = [p for p, w in ((0, tree), (1, cont)) if w['focus']]
            got[key] = boxes[0] if len(boxes) == 1 else boxes
        elif key == 'count':
            got[key] = count
        elif key == 'rec':
            entries = tuple((e, t) for e, t, _ in record(lines))
            menu = {'tree': TREE_REC, 'alone': ALONE_REC}.get(
                want[key]) or contents_rec(cache)
            got[key] = want[key] if menu and entries == menu else entries
        elif key in ('states', 'checked'):
            word = 'DISABLED' if key == 'states' else 'CHECKED'
            have = dict((e, int(word in st.split()))
                        for e, _, st in record(lines))
            got[key] = dict((e, have.get(e)) for e in want[key])
        elif key == 'menu':
            got[key] = int('#32768 ""' in tops)
        elif key == 'dialog':
            got[key] = int(any(t.startswith('#32770 ') for t in tops))
        elif key == 'tool':
            m = framchk.WTOOL.match(framchk.chrome(lines)[1] or '')
            got[key] = m.group(2) if m else None
        elif key == 'loc':
            rows = [r for r in items['rows'] if item_id(r) == want[key][0]]
            got[key] = (want[key][0], item_loc(rows[0]) if rows else None)
        elif key == 'treerow':
            names = [framchk.loc_name(r) for r in tree['rows']]
            got[key] = want[key] if want[key] in names else names
    return ['%s: %s\n  want %s\n  got  %s' % (where, key, want[key], got[key])
            for key in want if got[key] != want[key]]


def split_line(cache, gold, snap):
    """The splitter's left edge and limits in a snapshot, (x, lo, hi), or
    None."""
    lines = (load(cache, gold) or {}).get(snap)
    if not lines:
        return None
    m = framchk.WSPLITLN.match(
        view(lines)[1].get(framchk.C_SPLIT, {}).get('line', ''))
    return tuple(int(m.group(i)) for i in (2, 3, 4)) if m else None


def at(cache, gold, snap):
    """The splitter's left edge in a snapshot, or None."""
    got = split_line(cache, gold, snap)
    return got[0] if got else None


def bar_bad(where, pane, width, end):
    """Why a pane width pixels wide, of rows ending at end, shows the
    wrong horizontal bar, or None."""
    room = width - BORDER
    want = end - room if room < end else None
    got = pane['bar'][1] if pane['bar'] else None
    if got == want:
        return None
    return '%s: %s, a bar to %s for a pane %d wide' % (
        where, pane['line'][:24], want, width)


def width_bad(cache):
    """Why the widths of the split fail, one line each."""
    bad = []
    c = defines('INVWIN.C', 'INVWIN.H', 'WSPLIT.H')
    gap = c.get('WSP_WIDTH')
    least = c.get('IW_PANEMIN')
    if gap is None or least is None:
        return ['WSP_WIDTH (WSPLIT.H) or IW_PANEMIN (INVWIN.C) not defined']
    for gold, snaps in (('INVWINSP', ('LOCS',)),
                        ('INVWINSW', ('LOCS', 'B2', 'MAX', 'RESTORE')),
                        ('INVWINSX', ('LOCS', 'MAX'))):
        for snap in snaps:
            x = at(cache, gold, snap)
            where = '%s.SNP %s' % (gold, snap)
            if x is None:
                bad.append('%s: no splitter' % where)
                continue
            ps = view(load(cache, gold)[snap])[1]
            why = bar_bad(where, ps[framchk.C_LIST], x, TREE_END)
            if why:
                bad.append(why)
            if snap == 'MAX':
                why = bar_bad(where, ps[framchk.C_CLIST], MAXW - x - gap,
                              ITEM_END)
                if why:
                    bad.append(why)
    auto = at(cache, 'INVWINSP', 'LOCS')
    if auto is not None and not CODE_ROOM <= auto - BORDER < TREE_END:
        bad.append('INVWINSP.SNP LOCS: the automatic tree %d wide, want '
                   'Name and Type shown, Code behind the bar' % auto)
    wide = [at(cache, 'INVWINSW', s) for s in ('LOCS', 'B2', 'MAX',
                                               'RESTORE')]
    if None in wide:
        return bad
    if len(set(wide)) != 1 or wide[0] % WIDE:
        bad.append('INVWINSW.SNP: the tree %s wide, want one multiple of '
                   '%d' % (wide, WIDE))
        return bad
    cw = wide[0] // WIDE
    held = at(cache, 'INVWINSX', 'MAX')
    if held is not None and held != MAXW - gap - least * cw:
        bad.append('INVWINSX.SNP MAX: the tree %d wide, want %d, the '
                   'contents %d characters of %d pixels' % (
                       held, MAXW - gap - least * cw, least, cw))
    # The limits INVWIN gives the splitter (WSPM_LIMITS): each pane at
    # least IW_PANEMIN characters, the contents' in the maximized room.
    for gold, snap in (('INVWINSW', 'MAX'), ('INVWINSX', 'MAX')):
        got = split_line(cache, gold, snap)
        want = (least * cw, MAXW - gap - least * cw)
        if got is not None and got[1:] != want:
            bad.append('%s.SNP %s: the splitter limits %s, want %s' % (
                gold, snap, got[1:], want))
    first = at(cache, 'INVWINSX', 'LOCS')
    if first is not None and first <= wide[0]:
        bad.append('INVWINSX.SNP LOCS: the tree %d wide, want past %d' % (
            first, wide[0]))
    return bad


def overlap_bad(cache):
    """Why a window of the split's goldens overlaps another, or None."""
    bad = []
    for gold in ('INVWINSP', 'INVWINSQ', 'INVWINSW', 'INVWINSX',
                 'INVWINSY', 'INVWINSZ'):
        for snap, lines in sorted((load(cache, gold) or {}).items()):
            bad += ['%s.SNP %s: %s' % (gold, snap, ln) for ln in lines
                    if ' OVERLAP' in ln]
    return bad


def head_bad(cache):
    """Why the contents' header in the split differs from their own
    screen's, or None."""
    full = (load(cache, 'INVWIN6') or {}).get('CONTENTS', [])
    split = (load(cache, 'INVWINSP') or {}).get('LOCS', [])
    hdr = ' WLIST %d "%s"' % (framchk.C_CLIST, colchk.row(
        colchk.ITEM, 'ID', 'Category', 'Maker', 'Model', 'Works', 'Qty',
        'Location', 'Status'))
    want = [full[i + 1] for i, ln in enumerate(full[:-1])
            if ln.startswith(' WLIST %d "' % framchk.C_LIST)]
    got = [split[i + 1] for i, ln in enumerate(split[:-1])
           if ln == hdr or ln.startswith(hdr + ' HBAR 0 ')]
    if want and got == want[:1]:
        return None
    return 'INVWINSP.SNP LOCS: under %s\n  want %s\n  got  %s' % (
        hdr, want[:1], got)


def cfg_bad():
    """Why INVWINHA's settings save lacks WINSPLIT=0 or holds a WINPANE
    line other than those of KEPT, or None."""
    path = os.path.join(colchk.GOLD, 'INV.CFG')
    try:
        with open(path, 'rb') as f:
            lines = f.read().decode('latin-1').split('\r\n')
    except OSError:
        return 'INV.CFG: cannot read it'
    if CFGLINE not in lines:
        return 'INV.CFG: no %s line' % CFGLINE
    pane = [ln for ln in lines if ln.upper().startswith('WINPANE.')]
    return None if pane == KEPT else 'INV.CFG: WINPANE lines %s' % pane


def main():
    cache = {}
    bad = []
    for case in CASES:
        bad += case_bad(case, cache)
    bad += width_bad(cache) + overlap_bad(cache)
    bad += [why for why in (head_bad(cache), cfg_bad()) if why]
    bad = list(dict.fromkeys(bad))
    for why in bad:
        print('splitchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('splitchk: %d cases ok, widths ok' % len(CASES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
