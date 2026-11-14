"""menuchk.py - INVWIN's menus: the help texts of their entries in its
resource scripts, and the menu bar, its marks and the Record menu's
pop-up in its snapshots, against lines worked out apart from the
program.

usage: python3 -I TEST/menuchk.py

The help of a menu entry is the string IW_H_MENU (SRC/WIN/INVWIN.H) plus
the number of the text the entry shows: its message MN_ of SRC/CLIENT/
INVMENU.MSG, or its string IW_S_ of INVWIN.H, written in a resource
script of SRC/WIN as "IW_H_MENU + name, text".  The entries shown are
those of the shared menu table (SRC/CLIENT/INVMENU.C), the one of
command TUI_CMD_QUIT showing IW_S_MEXIT, the Items menu's own Browse by
category (IW_S_MBYCAT), the View menu's two
(IW_S_VPANE, IW_S_VREFRESH), the Help menu's four (IW_S_MHELPON,
IW_S_MSTART, IW_S_MKEYS, IW_S_MABOUT) and the Record menu's (RECORD
below); popup titles and separators have none.  Each
help text is 1 to IW_H_MAXLEN (INVWIN.H) bytes of the Windows character
set (CP1252) without a TAB or an '&'.  A help text too long, empty or
of other characters, an entry shown without help, a help for a text no entry
shows, two helps for one text, two texts of one number, or a name not
defined, fails.  INVWIN.HTX's @KEYS section names the pop-up's key,
Shift+F10.

Every snapshot of TEST/GOLDEN/WIN with INVWIN's frame (a line 'INVWIN
"') and its menu bar (the POPUP and MENU lines under it, TAB written as
'?') holds:

  the popups &File, &Items, &Locations, &Search, &Reports, &Admin,
  &Options, Re&cord, &View and &Help, in that order; File holds Log out,
  a separator and E&xit?Alt+F4, Options Settings... and Change
  password..., Help its four entries, a separator before About;
  View holds &Pane beside the list (261), CHECKED exactly while the
  frame shows a splitter (' WSPLIT 220 ') and DISABLED exactly while it
  shows no list of a screen that has a pane (a ' WLIST 208 ' under a
  title "RETROINV - Items ...", "RETROINV - Assemblies", "RETROINV -
  Locations", "RETROINV - Categories", "RETROINV - Users" or that of a
  view of the loans and holds, LOANS), and
  &Refresh?F5 (628), DISABLED exactly without a list;
  each entry's text known, with '...' after its name (before a '?' key)
  when choosing it always opens a dialog box (DOTTED), else without;
  the Browse entry of the order of the item list (the frame titled
  "RETROINV - Items by ID", "by location", "by maker" or "by serial
  number") CHECKED, Browse by category (262) on the categories (the
  frame titled "RETROINV - Categories"), no other entry outside the
  Record and View menus CHECKED, and none on any other screen;
  once the status bar's user panel names the level (' WSTAT n "conn|NAME,
  level N|...'): every Admin entry DISABLED below level 3, New item and
  New location below level 2, and no other entry outside the Record and
  View menus DISABLED.  Some snapshot must name a level, so that a user
  panel of another form fails rather than skipping this.

Then each case of CASES names a golden, a snapshot in it and what must
hold there:

  ('bar', name, snap, lines)  the menu bar is exactly these lines
  ('checked', name, snap, id)  the Browse entry id CHECKED, None for none
  ('states', name, snap, pairs)  each Record entry (id, grayed): DISABLED
      or not
  ('popup', name, snap)  a menu window open, '#32768 ""' without HIDDEN:
      INVWIN's pop-up of its Record menu, whose entries the bar's Record
      lines show
  ('closed', name, snap, title)  no menu window open, no dialog, and
      the frame titled title (None: any) ACTIVE
  ('dialog', name, snap, caption)  the dialog of that caption shown
  ('login', name, snap)  the login dialog, its user name and password
  ('gone', name, snap)  the program has ended: no frame

TEST/INVWINMP.KEY drives the pop-up: SHIFT-F10 on the item list opens
it at the selected row, ESC closes it, a letter chooses an entry as the
entry's key does (E Edit... item 2's form in edit mode, M Move... its
move dialog), on the location tree too; Items > Browse by maker moves
the check mark, gone on Assemblies, empty, whose pop-up grays Open,
Edit and Delete but not New, and on the tree, back with ESC; File >
Log out shows the login dialog; at level 1 the pop-up's entries that
change records are grayed and E gives nothing, P opens item 1's parts;
File > Exit ends the program.

A golden, snapshot or line missing, or one different, fails.  Exit
status 1 if any check fails.
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WIN = os.path.join(ROOT, 'SRC', 'WIN')
_SPEC = importlib.util.spec_from_file_location(
    'colchk', os.path.join(HERE, 'colchk.py'))
colchk = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(colchk)

IW_H_MENU = 4096
OWN = ('IW_S_MBYCAT',)
HELP = ('IW_S_MHELPON', 'IW_S_MSTART', 'IW_S_MKEYS', 'IW_S_MABOUT')
VIEW = ('IW_S_VPANE', 'IW_S_VREFRESH')
RECORD = ('IW_S_ROPEN', 'IW_S_REDIT', 'IW_S_RNEW', 'IW_S_RDEL',
          'IW_S_RCOPY', 'IW_S_RMOVE', 'IW_S_RINTO', 'IW_S_ROUT',
          'IW_S_RPART', 'IW_S_RRET', 'IW_S_RPARTS', 'IW_S_RSVC',
          'IW_S_RLOANS', 'IW_S_RUSED', 'IW_S_RBELOW', 'IW_S_RNEXT',
          'IW_S_RORDER', 'IW_S_RBACK', 'IW_S_RSPLIT', 'IW_S_RTOP',
          'IW_S_RLEND', 'IW_S_RRES', 'IW_S_RUSE', 'IW_S_RITEM',
          'IW_S_RPANE', 'IW_S_RALL')

POPUPS = ('&File', '&Items', '&Locations', '&Search', '&Reports', '&Admin',
          '&Options', 'Re&cord', '&View', '&Help')
FRAME = 'INVWIN "'
PREFIX = 'INVWIN "RETROINV - '
SEP = (0, '', 'DISABLED')
# Each entry's text, its '...' left out, and whether it has them.
DOTTED = (
    '&New item', '&Categories', '&New location', 'Location &types',
    'Find &text', 'Find &serial number',
    '&Filter items', '&Item listing', '&Valuation', '&Loans and holds',
    'I&tem labels', '&Bin labels', '&Backup', '&Restore', '&Audit log',
    '&Sessions', '&Export CSV', 'I&mport CSV', '&Settings',
    'Change &password', '&Edit?Ctrl+E', '&New?Ins', 'Dupli&cate?Ctrl+D',
    'Open &item', '&Move', '&Install in', '&Take out', '&Install part?Ins',
    '&Return or release', 'Split &units', '&Lend?Ctrl+L',
    'Reser&ve?Ctrl+R', 'Use wit&h?Ctrl+U', '&About RETROINV')
PLAIN = (
    'Browse by &ID', 'Browse by &location', 'Browse by &maker',
    'Browse by &serial', 'Browse by ca&tegory', '&Assemblies', 'S&ervice',
    'L&oans and holds',
    '&Browse locations', '&Users', '&Pack', 'Re&index', '&Log out',
    'E&xit?Alt+F4', '&Open?Enter', '&Delete?Del', '&Parts',
    '&Service records', 'Loans &and holds', 'Used &with it',
    'Everythin&g below', 'Next li&st?Ctrl+PgDn', 'Next o&rder', '&Back?Esc',
    'Not installed onl&y', 'Ne&xt pane?F6', '&Pane beside the list',
    '&Refresh?F5', '&Help on this screen?F1', '&Getting started',
    '&Keyboard', 'Select all?Ctrl+/')
FILE = ((251, '&Log out', ''), SEP, (250, 'E&xit?Alt+F4', ''))
OPTIONS = ((30, '&Settings...', ''), (31, 'Change &password...', ''))
HELPMENU = ((257, '&Help on this screen?F1', ''),
            (259, '&Getting started', ''), (258, '&Keyboard', ''), SEP,
            (256, '&About RETROINV...', ''))
FIXED = {'&File': FILE, '&Options': OPTIONS, '&Help': HELPMENU}
# The View menu's entries, and the titles, by their start, of the
# screens that have a pane.
PANE = 261
REFRESH = 628
VIEWMENU = ((PANE, '&Pane beside the list'), (REFRESH, '&Refresh?F5'))
LOANS = ('Open loans and holds', 'Open loans', 'Open reservations', 'In use',
         'Overdue loans and reservations', 'All loans and holds')
PANED = ('Items ', 'Assemblies', 'Locations', 'Categories', 'Users') + LOANS
C_LIST = 208
C_SPLIT = 220
BROWSE = {'ID': 2, 'location': 3, 'maker': 4, 'serial number': 5}
BYCAT = 262
OPER = (6, 11)                  # New item, New location

# The bar's menus but File, Options and Help: (id, text, least level).
ITEMS = ((2, 'Browse by &ID', 1), (3, 'Browse by &location', 1),
         (4, 'Browse by &maker', 1), (5, 'Browse by &serial', 1),
         (BYCAT, 'Browse by ca&tegory', 1), (6, '&New item...', 2),
         (7, '&Assemblies', 1), (8, 'S&ervice', 1),
         (9, 'L&oans and holds', 1), (12, '&Categories...', 1))
LOCS = ((10, '&Browse locations', 1), (11, '&New location...', 2),
        (13, 'Location &types...', 1))
SRCH = ((20, 'Find &text...', 1), (21, 'Find &serial number...', 1),
        (22, '&Filter items...', 1))
RPT = ((40, '&Item listing...', 1), (41, '&Valuation...', 1),
       (42, '&Loans and holds...', 1), (43, 'I&tem labels...', 1),
       (44, '&Bin labels...', 1))
ADMIN = ((52, '&Users', 3), (53, '&Backup...', 3), (54, '&Restore...', 3),
         (55, '&Pack', 3), (56, 'Re&index', 3), (57, '&Audit log...', 3),
         (58, '&Sessions...', 3), (50, '&Export CSV...', 3),
         (51, 'I&mport CSV...', 3))
# The Record menu of the item list; level 2 for those that change records,
# which Select all, of every level, is not.
REC = ((525, '&Open?Enter', 1), (1093, '&Edit...?Ctrl+E', 2),
       (557, '&New...?Ins', 2), (558, '&Delete?Del', 2), None,
       (1215, 'Select all?Ctrl+/', 1), None,
       (1536, '&Move...', 2), (1092, 'Dupli&cate...?Ctrl+D', 2),
       (1537, '&Install in...', 2), (1539, '&Take out...', 2),
       (1541, 'Split &units...', 2), None,
       (1100, '&Lend...?Ctrl+L', 2), (1106, 'Reser&ve...?Ctrl+R', 2),
       (1109, 'Use wit&h...?Ctrl+U', 2), None,
       (1542, '&Parts', 1), (1543, '&Service records', 1),
       (1544, 'Loans &and holds', 1), (1545, 'Used &with it', 1), None,
       (1546, 'Next o&rder', 1))


def entry(eid, text, state):
    """A MENU line."""
    return '  MENU %d "%s"%s' % (eid, text, ' ' + state if state else '')


def item_bar(level, checked, split=False):
    """The menu bar of the item list at level, Browse entry checked,
    with split beside its preview."""
    out = []

    def menu(title, entries):
        out.append(' POPUP "%s"' % title)
        for e in entries:
            if e is None:
                out.append(entry(*SEP))
                continue
            eid, text, least = e
            state = 'DISABLED' if level < least else (
                'CHECKED' if eid == checked else '')
            out.append(entry(eid, text, state))

    menu('&File', [e[:2] + (1,) if e != SEP else None for e in FILE])
    menu('&Items', ITEMS)
    menu('&Locations', LOCS)
    menu('&Search', SRCH)
    menu('&Reports', RPT)
    menu('&Admin', ADMIN)
    menu('&Options', [e[:2] + (1,) for e in OPTIONS])
    menu('Re&cord', REC + (((629, 'Ne&xt pane?F6', 1),) if split else ()))
    menu('&View', [e + (1,) for e in VIEWMENU])
    if split:
        out[-2] += ' CHECKED'
    menu('&Help', [e[:2] + (1,) if e != SEP else None for e in HELPMENU])
    return tuple(out)


# The level-3 bar of the item list by location, as the interface gives it.
BAR3LOC = item_bar(3, 3, split=True)

CASES = (
    ('bar', 'INVWINN', 'LOC', BAR3LOC),
    ('bar', 'INVWINJ', 'OPEN', BAR3LOC),
    ('bar', 'INVWINQ', 'Q0', item_bar(2, 2)),
    ('bar', 'INVWINQB', 'ITEMS', item_bar(1, 2)),
    # Next order through every order and round
    ('checked', 'INVWINN', 'LOC', 3),
    ('checked', 'INVWINN', 'MFR', 4),
    ('checked', 'INVWINN', 'SERIAL', 5),
    ('checked', 'INVWINN', 'ID', 2),
    ('checked', 'INVWINN', 'LOC2', 3),
    # the pop-up
    ('bar', 'INVWINMP', 'ITEMS', item_bar(3, 2)),
    ('closed', 'INVWINMP', 'ITEMS', 'Items by ID'),
    ('popup', 'INVWINMP', 'POPUP'),
    ('bar', 'INVWINMP', 'POPUP', item_bar(3, 2)),
    ('closed', 'INVWINMP', 'CLOSED', 'Items by ID'),
    ('popup', 'INVWINMP', 'F10MENU'),
    ('dialog', 'INVWINMP', 'EDIT', 'Item 2 - Edit'),
    ('closed', 'INVWINMP', 'EDITBACK', 'Items by ID'),
    ('dialog', 'INVWINMP', 'MOVE', 'Move item 2 to'),
    ('checked', 'INVWINMP', 'MAKER', 4),
    ('checked', 'INVWINMP', 'ASSY', None),
    ('closed', 'INVWINMP', 'ASSY', 'Assemblies'),
    ('popup', 'INVWINMP', 'ASSYPOP'),
    ('states', 'INVWINMP', 'ASSYPOP',
     ((525, True), (1093, True), (557, False), (558, True))),
    ('checked', 'INVWINMP', 'ASSYBACK', 2),
    ('checked', 'INVWINMP', 'LOCS', None),
    ('popup', 'INVWINMP', 'TREEPOP'),
    ('dialog', 'INVWINMP', 'TREEMOVE', 'Move Office into'),
    ('checked', 'INVWINMP', 'LISTBACK', 2),
    ('closed', 'INVWINMP', 'LISTBACK', 'Items by ID'),
    ('login', 'INVWINMP', 'LOGOUT'),
    ('bar', 'INVWINMP', 'READER', item_bar(1, 2)),
    ('popup', 'INVWINMP', 'RPOPUP'),
    ('bar', 'INVWINMP', 'RPOPUP', item_bar(1, 2)),
    ('closed', 'INVWINMP', 'NOEDIT', 'Items by ID'),
    ('closed', 'INVWINMP', 'PARTS', 'Parts of item 1 IBM 5150'),
    ('checked', 'INVWINMP', 'PARTS', None),
    ('checked', 'INVWINMP', 'PARTBACK', 2),
    ('gone', 'INVWINMP', 'GONE'),
    # the categories: Open, Edit, New and Delete, grayed at level 1 but
    # Open, and the check mark of Browse by category
    ('checked', 'INVWINBA', 'CATS', BYCAT),
    ('states', 'INVWINBA', 'CATS',
     ((525, False), (1093, False), (557, False), (558, False))),
    ('checked', 'INVWINBA', 'ALONE', None),
    ('closed', 'INVWINBA', 'ALONE', 'Category CBL Cables'),
    ('checked', 'INVWINBA', 'BACK', BYCAT),
    ('checked', 'INVWINBA', 'ITEMS', 2),
    ('states', 'INVWINBC', 'READER',
     ((525, False), (1093, True), (557, True), (558, True))),
)

MENU = re.compile(r'  MENU (\d+) "(.*)"((?: [A-Z]+)*)$')
POPUP = re.compile(r' POPUP "(.*)"((?: [A-Z]+)*)$')
TITLE = re.compile(r'INVWIN "RETROINV - Items by (ID|location|maker|'
                   r'serial number)"(?: |$)')
LEVEL = re.compile(r'^ WSTAT \d+ "[^|]*\|[^|]*, level (\d)\|')
DEFINE = re.compile(r'#define\s+(IW_S_\w+|IW_H_MENU|IW_H_MAXLEN)\s+(\d+)')
HELPSTR = re.compile(r'^\s*IW_H_MENU\s*\+\s*(\w+)\s*,\s*"((?:[^"]|"")*)"\s*$')
TABLE = re.compile(r'\{\s*(MN_\w+)\s*,\s*TUI_LVL_\w+\s*,\s*(\w+)\s*\}')
MESSAGE = re.compile(r'(\d+) (MN_\w+) ')


def read(path):
    """The lines of the file at path, bytes as CP1252 where they are
    such, or None."""
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError:
        return None
    return data.decode('cp1252', errors='replace').splitlines()


def sources(bad):
    """The numbers of the names (MN_ and IW_S_) and the help texts by
    name, read from the sources; why they fail appended to bad."""
    nums = {}
    for line in read(os.path.join(WIN, 'INVWIN.H')) or ():
        m = DEFINE.match(line.strip())
        if m:
            nums[m.group(1)] = int(m.group(2))
    if nums.get('IW_H_MENU') != IW_H_MENU:
        bad.append('INVWIN.H: IW_H_MENU is not %d' % IW_H_MENU)
    if 'IW_H_MAXLEN' not in nums:
        bad.append('INVWIN.H: no IW_H_MAXLEN')
    for line in read(os.path.join(ROOT, 'SRC', 'CLIENT', 'INVMENU.MSG')) or ():
        m = MESSAGE.match(line)
        if m:
            nums[m.group(2)] = int(m.group(1))
    helps = {}
    for name in sorted(os.listdir(WIN)):
        if not name.endswith('.RC'):
            continue
        for line in read(os.path.join(WIN, name)):
            m = HELPSTR.match(line)
            if not m:
                continue
            key, text = m.group(1), m.group(2).replace('""', '"')
            if key in helps:
                bad.append('%s: two helps for %s' % (name, key))
            helps[key] = text
    return nums, helps


def shown():
    """The names of the texts the menu entries show."""
    path = os.path.join(ROOT, 'SRC', 'CLIENT', 'INVMENU.C')
    with open(path) as f:
        table = TABLE.findall(f.read())
    bar = ['IW_S_MEXIT' if cmd == 'TUI_CMD_QUIT' else name
           for name, cmd in table]
    return tuple(bar) + OWN + HELP + VIEW + RECORD


def help_bad():
    """Why the help texts fail, one line each."""
    bad = []
    nums, helps = sources(bad)
    maxlen = nums.get('IW_H_MAXLEN', 0)
    names = shown()
    seen = {}
    for name in names:
        if name not in nums:
            bad.append('%s: not defined' % name)
        elif nums[name] in seen and seen[nums[name]] != name:
            bad.append('%s and %s: both number %d' % (
                seen[nums[name]], name, nums[name]))
        else:
            seen[nums[name]] = name
        if name not in helps:
            bad.append('%s: shown without help' % name)
    for name, text in sorted(helps.items()):
        if name not in names:
            bad.append('IW_H_MENU + %s: no entry shows it' % name)
        raw = text.encode('cp1252', errors='replace')
        if not text or len(raw) > maxlen:
            bad.append('IW_H_MENU + %s: %d bytes, not 1 to %d: %s' % (
                name, len(raw), maxlen, text))
        if '�' in text or '\t' in text or '\\t' in text or '&' in text:
            bad.append('IW_H_MENU + %s: a TAB, an & or a byte not of '
                       'CP1252: %s' % (name, text))
    keys = []
    for line in read(os.path.join(WIN, 'INVWIN.HTX')) or ():
        if line.startswith('@'):
            keys.append(line.startswith('@KEYS'))
        elif keys and keys[-1] and 'Shift+F10' in line:
            break
    else:
        bad.append('INVWIN.HTX: @KEYS names no Shift+F10')
    return bad


def frame(lines):
    """The frame's line and its menu bar's lines in a snapshot, or
    (None, ())."""
    for i, line in enumerate(lines):
        if line.startswith(FRAME):
            j = i + 1
            while j < len(lines) and (lines[j].startswith(' POPUP ') or
                                      lines[j].startswith('  MENU ')):
                j += 1
            return line, tuple(lines[i + 1:j])
    return None, ()


def menus(bar):
    """The menus of a bar: (title, [(id, text, state)])."""
    out = []
    for line in bar:
        m = POPUP.match(line)
        if m:
            out.append((m.group(1), []))
            continue
        m = MENU.match(line)
        if m and out:
            out[-1][1].append((int(m.group(1)), m.group(2),
                               m.group(3).strip()))
    return out


def level(lines):
    """The user's level the status bar of a snapshot names, or None."""
    for ln in lines:
        lv = LEVEL.search(ln)
        if lv:
            return int(lv.group(1))
    return None


def bar_bad(where, lines):
    """Why the menu bar of a snapshot fails, one line each."""
    line, bar = frame(lines)
    if not bar:
        return []
    bad = []
    got = menus(bar)
    if tuple(t for t, _ in got) != POPUPS:
        return ['%s: popups %s' % (where, ' '.join(t for t, _ in got))]
    m = TITLE.match(line)
    want = BROWSE[m.group(1)] if m else (
        BYCAT if line.startswith(PREFIX + 'Categories"') else None)
    lvl = level(lines)
    own = lines[lines.index(line) + 1:]
    own = own[:next((i for i, ln in enumerate(own)
                     if not ln.startswith(' ')), len(own))]
    listed = any(ln.startswith(' WLIST %d ' % C_LIST) for ln in own)
    name = line[len(PREFIX):] if line.startswith(PREFIX) else ''
    view = ((PANE, VIEWMENU[0][1], ' '.join(
        (['DISABLED'] if not (listed and name.startswith(PANED)) else []) +
        (['CHECKED'] if any(ln.startswith(' WSPLIT %d ' % C_SPLIT)
                            for ln in own) else []))),
            (REFRESH, VIEWMENU[1][1], '' if listed else 'DISABLED'))
    for title, entries in got:
        if title in FIXED and tuple(entries) != FIXED[title] or \
                title == '&View' and tuple(entries) != view:
            bad.append('%s: %s holds %s' % (where, title, entries))
        for eid, text, state in entries:
            if eid == 0:
                continue
            base = text.replace('...', '', 1)
            dots = base != text
            if base not in DOTTED and base not in PLAIN:
                bad.append('%s: %d "%s" unknown' % (where, eid, text))
            elif dots != (base in DOTTED):
                bad.append('%s: %d "%s" %s' % (
                    where, eid, text, 'has ...' if dots else 'lacks ...'))
            if title in ('Re&cord', '&View'):
                continue
            checked = 'CHECKED' in state.split()
            if checked != (eid == want):
                bad.append('%s: %d "%s" %s (frame %s)' % (
                    where, eid, text,
                    'CHECKED' if checked else 'not CHECKED', line))
            if lvl is None:
                continue
            least = 3 if title == '&Admin' else (2 if eid in OPER else 1)
            off = 'DISABLED' in state.split()
            if off != (lvl < least):
                bad.append('%s: %d "%s" %s at level %d' % (
                    where, eid, text,
                    'DISABLED' if off else 'not DISABLED', lvl))
    return bad


def case_bad(case, cache):
    """None when case holds, else why not."""
    kind, name, snap = case[:3]
    if name not in cache:
        cache[name] = colchk.snaps(name)
    if cache[name] is None:
        return '%s.SNP: cannot read it' % name
    lines = cache[name].get(snap)
    if lines is None:
        return '%s.SNP: no snapshot %s' % (name, snap)
    where = '%s.SNP %s' % (name, snap)
    line, bar = frame(lines)
    tops = [ln for ln in lines if not ln.startswith(' ')]
    if kind == 'bar':
        if bar == case[3]:
            return None
        i = 0
        while i < min(len(bar), len(case[3])) and bar[i] == case[3][i]:
            i += 1
        return '%s: bar line %d\n  want %s\n  got  %s' % (
            where, i + 1, case[3][i] if i < len(case[3]) else '(end)',
            bar[i] if i < len(bar) else '(end)')
    if kind == 'checked':
        got = [eid for title, entries in menus(bar)
               if title not in ('Re&cord', '&View')
               for eid, _, state in entries if 'CHECKED' in state.split()]
        want = [case[3]] if case[3] is not None else []
        return None if bar and got == want else '%s: checked %s, want %s' % (
            where, got, want)
    if kind == 'states':
        rec = dict((eid, 'DISABLED' in state.split())
                   for title, entries in menus(bar) if title == 'Re&cord'
                   for eid, _, state in entries)
        got = tuple((eid, rec.get(eid)) for eid, _ in case[3])
        return None if got == case[3] else '%s: Record %s, want %s' % (
            where, got, case[3])
    if kind == 'popup':
        return None if '#32768 ""' in tops else '%s: no menu open' % where
    if kind == 'closed':
        title = case[3]
        if '#32768 ""' in tops or any(t.startswith('#32770 ') for t in tops):
            return '%s: a menu or dialog open: %s' % (where, ' | '.join(tops))
        if line is None or not line.endswith(' ACTIVE') or (
                title is not None and
                not line.startswith(PREFIX + title + '" ')):
            return '%s: frame %s, want "%s" ACTIVE' % (where, line, title)
        return None
    if kind == 'dialog':
        head = '#32770 "%s" ' % case[3]
        return None if any(t.startswith(head) for t in tops) else \
            '%s: no dialog "%s"' % (where, case[3])
    if kind == 'login':
        need = (' Static 65535 "&User name:"', ' Static 65535 "&Password:"')
        if all(n in lines for n in need) and bar == ():
            return None
        return '%s: not the login dialog over a frame without a bar' % where
    if kind == 'gone':
        return None if line is None else '%s: frame still there' % where
    return '%s: unknown case %s' % (where, kind)


def main():
    bad = help_bad()
    cache = {}
    bad += [why for why in (case_bad(c, cache) for c in CASES) if why]
    bars = 0
    levels = 0
    for f in sorted(os.listdir(colchk.GOLD)):
        if not f.endswith('.SNP'):
            continue
        name = f[:-4]
        snaps = cache.get(name) or colchk.snaps(name) or {}
        for snap, lines in sorted(snaps.items()):
            if frame(lines)[1]:
                bars += 1
                levels += level(lines) is not None
                bad += bar_bad('%s.SNP %s' % (name, snap), lines)
    if not levels:
        bad.append('no menu bar under a status bar naming a level')
    for why in bad:
        print('menuchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('menuchk: help texts ok, %d cases ok, %d menu bars ok, %d at a '
          'level' % (len(CASES), bars, levels))
    return 0


if __name__ == '__main__':
    sys.exit(main())
