"""framchk.py - INVWIN's frame chrome: its tool bar's texts and commands in
the sources, and its tool bar, Find box and status bar in its snapshots,
against lines worked out apart from the program.

usage: python3 -I TEST/framchk.py

The sources (SRC/WIN/INVWIN.H, INVWIN.RC and INVWIN.C):

  string IW_S_TOOL is TOOL; IW_S_USER the user panel's "%s, level %d";
  each count string of NOUNS (even, the plural) is its "%lu" text and
  the one after it the singular;
  the frame's tool_cmds hold, button by button in IW_S_TOOL's order, the
  command of the item list's Record entry of that button (read from the
  Record menu of TEST/GOLDEN/WIN/INVWIN.SNP's ADMIN snapshot), Find's
  being IW_M_FIND, which no other menu entry of any snapshot and no
  other IW_M_ name has.

Every snapshot of TEST/GOLDEN/WIN with INVWIN's frame (a line 'INVWIN
"'): right under the frame and its menu bar the tool bar, the Find box
in it and the status bar, ' WTOOL 218', '  Edit 219', ' WSTAT 201', and
no ' Static 201', no state word on the bars but the tool bar's CAPTURE;
then:

  logged in (the frame has a menu bar): each button of the bar grayed
  ('~') exactly when the Record menu has no entry of its text (BUTTONS)
  or that entry is DISABLED, Find never; the Find box not DISABLED;
  logged out: every button grayed, Find too, the box DISABLED;
  at most one button drawn pressed ('^' after any '~'), never a grayed
  one, and only while the bar holds the mouse (CAPTURE), which it and
  the frame's other windows do only in the goldens of the mouse lines
  (MOUSE), the bar's only in INVWINMT's;
  the box empty and without the focus except in INVWINFB, INVWINWH,
  INVWINPW, INVWINPX and INVWINEA;
  the status bar four panels, written '|' between them: the connection
  (CONN), the user ("NAME, level N", empty logged out), the count and
  the help panel;
  the count: "<n> <noun>" logged in on a link that counts (LOCAL, NET,
  TCP) with a list shown, the noun the screen's (NOUN by the frame's
  title), singular for 1; at least the list's rows shown (one "No ..."
  row for 0); else empty; on the split view, the count of the pane whose
  box has the focus, the tree's "<n> locations" or the contents' "<n>
  items in <name>" (singular for 1, cut to a panel's WSB_MAXCELL bytes),
  name the tree's selected location, and either while neither box has
  it, and likewise beside another list the master's own count or its
  pane's rows in their noun (DETAIL: "<n> parts" beside the
  assemblies, "<n> loans and holds" beside the views of those, "<n>
  entries" beside the users, "<n> items" beside the categories); in
  place of these, on any link, "<n> selected" (NSEL) while the list
  shows n rows SELECTED, n more than one: on the split
  view the focused pane's, or either pane's while neither box has the
  focus; or
  "Shift+F8 add mode: <n> selected" (NADD), n the rows SELECTED of the
  list or its focused pane, at least one, as its commands take the
  focus row when none is; or "All <n> selected" (NALL) while every row
  that list shows is SELECTED, n at least as many;
  the help panel: empty logged out; with a menu open, empty or the help
  of a menu entry (IW_H_MENU + text, as TEST/menuchk.py reads them); with
  none open, never a menu entry's help;
  the split view exactly on the locations' tree (titled "RETROINV -
  Locations", its list WLIST 208), but in the goldens of ALONE, whose
  settings or link show the tree alone: the tree, then 'WSPLIT 220 ""
  AT x LIMITS lo hi' with no state word or with FOCUS CAPTURE, a drag
  held, x within lo..hi, then the contents' WLIST 221, DISABLED only
  without rows, CAPTURE while a divider is held; the same three beside
  the other lists that have a list as their pane (DETAIL: the
  assemblies with the selected one's parts, each view of the loans and
  holds with the history of the selected row's item, the users with
  the selected one's audit entries, the categories with the selected
  one's items), only in the goldens of
  PANED, each of which shows it; no WLIST 221 on any other screen; at
  most one list box with the focus;
  the preview only beside a list of items (the frame titled "RETROINV -
  Items ...") and only in the goldens of
  PREVIEW, each of which shows it: the list, then 'WSPLIT 220' as
  beside the tree, then 'WPREV 222 "text"', with VBAR pos max when its
  lines do not fit, pos within max, DISABLED exactly while the list has
  no row, and then without lines and text, FOCUS only while no list box
  has it, never OVERLAP, its PV lines only without a text; no WSPLIT or
  WPREV 222 on any other screen.

Then each case of CASES names a golden, a snapshot in it and what must
hold there:

  ('tool', name, snap, text)  the tool bar's text
  ('box', name, snap, line)  the Find box's line
  ('stat', name, snap, text)  the status bar's text
  ('help', name, snap, key)  the help panel holds the help of the entry
      showing text key (an MN_ or IW_S_ name)
  ('title', name, snap, title)  the frame titled "RETROINV - title"
  ('list', name, snap)  the list has the focus, no dialog is open
  ('dialog', name, snap, caption, line)  the dialog of that caption is
      open, holding line (None: any)
  ('gray', name, snap, id)  Record entry id shown DISABLED

TEST/INVWINFB.KEY drives the Find box: CTRL-F into it, a text typed and
ENTER finds the items holding it, the box keeping it and the list taking
the focus; CTRL-F selects the box's text, so a text typed replaces it;
ESC and TAB give the focus back to the list unchanged; ESC on the
results opens the search dialog on the text; a box of blanks or empty,
or Search > Find text from it, opens the search dialog; logged out the
box is disabled, and a reader finds too.  TEST/INVWINST.KEY drives the
help panel: the help of the entry highlighted in the File, Items,
Record and Help menus, none on the bar alone or once the menus close,
the pop-up's first entry's, and a grayed entry's at level 1.

A golden, snapshot or line missing, or one different, fails.  Exit
status 1 if any check fails.
"""
import ast
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WIN = os.path.join(ROOT, 'SRC', 'WIN')
_SPEC = importlib.util.spec_from_file_location(
    'menuchk', os.path.join(HERE, 'menuchk.py'))
menuchk = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(menuchk)
colchk = menuchk.colchk

TOOL = 'New|Open|Edit|Delete|-|Move|Install|Lend|-|*|Find'
USER = '%s, level %d'
# Each button's Record entries, by text without '&', '...' and the key.
BUTTONS = {'New': ('New', 'Install part'), 'Open': ('Open',),
           'Edit': ('Edit',), 'Delete': ('Delete',), 'Move': ('Move',),
           'Install': ('Install in',), 'Lend': ('Lend',)}
FIND = 'Find'
# The count strings: name, plural, singular.
NOUNS = (('IW_S_NITEMS', 'items', 'item'),
         ('IW_S_NASSY', 'assemblies', 'assembly'),
         ('IW_S_NPARTS', 'parts', 'part'),
         ('IW_S_NLOCS', 'locations', 'location'),
         ('IW_S_NSVC', 'service records', 'service record'),
         ('IW_S_NHOLDS', 'loans and holds', 'loan or hold'),
         ('IW_S_NUSERS', 'users', 'user'),
         ('IW_S_NAUDIT', 'entries', 'entry'),
         ('IW_S_NCATS', 'categories', 'category'))
NOUNS_OF = dict((n[0], n[1:]) for n in NOUNS)
# The frame's titles after "RETROINV - " and their count's noun (a
# title starting so); any other title's is items.
NOUN = (('Assemblies', 'IW_S_NASSY'), ('Parts of item ', 'IW_S_NPARTS'),
        ('Locations', 'IW_S_NLOCS'), ('Recent service', 'IW_S_NSVC'),
        ('Service records of item ', 'IW_S_NSVC'),
        ('Open loans and holds', 'IW_S_NHOLDS'),
        ('All loans and holds', 'IW_S_NHOLDS'),
        ('Overdue loans and reservations', 'IW_S_NHOLDS'),
        ('Open reservations', 'IW_S_NHOLDS'), ('Open loans', 'IW_S_NHOLDS'),
        ('In use', 'IW_S_NHOLDS'), ('Loans and holds of item ', 'IW_S_NHOLDS'),
        ('Used with item ', 'IW_S_NHOLDS'), ('Users', 'IW_S_NUSERS'),
        ('Audit log', 'IW_S_NAUDIT'), ('Categories', 'IW_S_NCATS'))
LOCAL = 'Data: DATA\\'
# The connection of each golden not on TEST/GOLDEN/WIN's LOCAL data.
CONN = {'INVCALL': 'Server: NOSUCH', 'INVMBX': 'Mailbox: E:\\',
        'INVNB': 'Server: RISRV', 'INVSER': 'Serial port: COM1',
        'INVTCP': 'TCP/IP server: 10.0.2.15',
        'INVTCPC': 'TCP/IP server: 10.0.2.99',
        'INVTCPQ': 'TCP/IP server: 10.0.2.15',
        'INVTCPR': 'TCP/IP server: 10.0.2.15:7001',
        'INVUSR': 'Serial port: COM1', 'INVSERB': 'Serial port: COM1',
        'INVWIN0': 'Server: RISRV',
        'INVWIN5': 'Serial port: COM1', 'INVWINMB': 'Mailbox: MBOX',
        'INVWINMC': 'Mailbox: MBOX', 'INVWINMD': 'Mailbox: MBOX',
        'INVWINME': 'Mailbox: DATA', 'INVWINMH': 'Mailbox: MBXHALF',
        'INVWINMN': 'Mailbox: MBXFULL', 'INVWINTC': 'TCP/IP server: 10.0.2.15'}
UNCOUNTED = ('Serial port: ', 'Mailbox: ')
# The goldens typing into the box.
BOXED = ('INVWINFB', 'INVWINWH', 'INVWINPW', 'INVWINPX', 'INVWINEA')
PRESSED = '^'                   # a tool button drawn pressed (TDRV.H)
HELD = 'CAPTURE'                # the state word of a window holding the mouse
# The goldens whose mouse lines leave a window of the frame holding the
# mouse at a snapshot, and the one holding a tool button down.
MOUSE = ('INVWINMF', 'INVWINMT', 'INVWINMW', 'INVWINDA', 'INVWINDR',
         'INVWINDX', 'INVWINDT', 'INVWINDL', 'INVWINDS', 'INVWINEG',
         'INVWINWG')
TOOLHELD = 'INVWINMT'

C_TOOL = 218
C_FIND = 219
C_STATUS = 201
C_LIST = 208                    # the screen's list, the split's tree
C_SPLIT = 220
C_CLIST = 221                   # the split's contents
C_PREVIEW = 222                 # or its preview panel
SPLIT_TITLE = 'Locations'
# The screens whose pane is a list, by their title's start, and the
# count string of the pane's rows.
DETAIL = (('Locations', 'IW_S_NITEMS'), ('Assemblies', 'IW_S_NPARTS'),
          ('Users', 'IW_S_NAUDIT'), ('Categories', 'IW_S_NITEMS')) + \
    tuple((view, 'IW_S_NHOLDS') for view in menuchk.LOANS)
# The goldens that show such a pane beside another list than the tree:
# those of its own scripts, and those of the links whose requests are
# fast, which open the categories too.
PANED = ('INVWINPT', 'INVWINPX', 'INVWINPS', 'INVWINPR', 'INVWINPH',
         'INVWINPU', 'INVWINBA', 'INVWINBB', 'INVWINBC', 'INVWINEJ',
         'INVNB', 'INVTCP')
# The titles, by their start, of the lists of items that have a preview,
# and the goldens that show it: those of its own scripts, INVWINEA,
# INVWINPU and INVWINBA, which end on the item list with no settings
# file, INVWINBL, which asks for it by View > Pane beside the list,
# those run with a settings file of TEST/KEYS, which has no
# WINPANE.ITEM=0, or with one that cannot be read (INVWINHB), and the
# links', where a fast link shows it and View > Pane beside the list
# shows it on a slow one.
PREVIEWED = ('Items ',)
PREVIEW = ('INVWINPV', 'INVWINPW', 'INVWINPX', 'INVWINPY', 'INVWINEA',
           'INVWINPU', 'INVWINBA', 'INVWINBL', 'INVWINJ', 'INVWINN',
           'INVWINO', 'INVWINRA', 'INVWINRB', 'INVWINRC', 'INVWINHB',
           'INVNB', 'INVTCP', 'INVTCPQ', 'INVSER', 'INVMBX')
# The goldens of the tree without its pane: WINPANE.LOC=0, and the
# mailbox, a link whose requests are slow, without a WINPANE line.
ALONE = ('INVWINSY', 'INVMBX')
TREE_NAME = 44                  # the tree's Name cells (colchk.TREE)
FRAMEBAR = ' Static %d ' % C_STATUS
NSEL = '%d selected'            # the count of several rows chosen
NADD = 'Shift+F8 add mode: %d selected'  # the same in add mode
NALL = re.compile(r'All ([1-9]\d*) selected$')  # every row of the list
LISTFOCUS = '  ListBox 1 "" FOCUS'

ALLGRAY = '~New|~Open|~Edit|~Delete|-|~Move|~Install|~Lend|-|*|~Find'
LEVEL1 = '~New|Open|~Edit|~Delete|-|~Move|~Install|~Lend|-|*|Find'


def stat(user, count, help_=''):
    """The status bar's text on LOCAL data."""
    return '|'.join((LOCAL, user, count, help_))


ADMIN = 'ADMIN, level 3'
CLERK = 'CLERK, level 2'
READER = 'READER, level 1'
CASES = (
    # the item list by ID at each level, and logged out
    ('tool', 'INVWIN', 'ADMIN', TOOL),
    ('box', 'INVWIN', 'ADMIN', '  Edit 219 ""'),
    ('stat', 'INVWIN', 'ADMIN', stat(ADMIN, '3 items')),
    ('tool', 'INVWINJB', 'NONOTE', TOOL),
    ('stat', 'INVWINJB', 'NONOTE', stat(CLERK, '3 items')),
    ('tool', 'INVWINB', 'READER', LEVEL1),
    ('stat', 'INVWINB', 'READER', stat(READER, '3 items')),
    ('tool', 'INVWIN', 'LOGIN', ALLGRAY),
    ('box', 'INVWIN', 'LOGIN', '  Edit 219 "" DISABLED'),
    ('stat', 'INVWIN', 'LOGIN', stat('', '')),
    ('tool', 'INVWINMP', 'LOGOUT', ALLGRAY),
    ('stat', 'INVWINMP', 'LOGOUT', stat('', '')),
    # the buttons on each kind of screen
    ('tool', 'INVWINV', 'NOTHING',
     'New|~Open|~Edit|~Delete|-|~Move|~Install|~Lend|-|*|Find'),
    ('stat', 'INVWINV', 'NOTHING', stat(CLERK, '0 items')),
    ('tool', 'INVWIN6', 'LOCS',
     'New|Open|Edit|Delete|-|Move|~Install|~Lend|-|*|Find'),
    ('stat', 'INVWIN6', 'LOCS', stat(ADMIN, '3 locations')),
    ('tool', 'INVWINDE', 'LOANS',
     'New|Open|Edit|~Delete|-|~Move|~Install|Lend|-|*|Find'),
    ('stat', 'INVWINDE', 'LOANS', stat(ADMIN, '1 loan or hold')),
    ('tool', 'INVWINJA', 'PARTS1',
     'New|Open|Edit|~Delete|-|~Move|~Install|~Lend|-|*|Find'),
    ('stat', 'INVWINJA', 'PARTS1', stat(ADMIN, '1 part')),
    ('tool', 'INVWINDE', 'AUDIT',
     '~New|~Open|~Edit|~Delete|-|~Move|~Install|~Lend|-|*|Find'),
    ('stat', 'INVWINDE', 'AUDIT', stat(ADMIN, '2 entries')),
    # the count's noun on each screen, singular and plural
    ('stat', 'INVWINSA', 'FOUND', stat(ADMIN, '1 item')),
    ('stat', 'INVWINPL', 'FOUND', stat(ADMIN, '1 item')),
    ('stat', 'INVWIN6', 'CONTENTS', stat(ADMIN, '2 items')),
    ('stat', 'INVWINOD', 'ONE', stat(ADMIN, '1 assembly')),
    ('stat', 'INVWINMP', 'ASSY', stat(ADMIN, '0 assemblies')),
    ('stat', 'INVWINJC', 'CHAIN', stat(ADMIN, '9 parts')),
    ('stat', 'INVWIN3', 'RECORDS', stat(ADMIN, '1 service record')),
    ('stat', 'INVWIN3', 'RECENT', stat(ADMIN, '2 service records')),
    ('stat', 'INVWINVC', 'OPEN', stat(CLERK, '3 loans and holds')),
    ('stat', 'INVWINQC', 'NEXT2', stat(ADMIN, '0 loans and holds')),
    ('stat', 'INVWINVB', 'USED', stat(CLERK, '1 loan or hold')),
    ('stat', 'INVWINQA', 'USERS', stat(ADMIN, '4 users')),
    ('stat', 'INVWINHS', 'AUDITALL', stat(ADMIN, '66 entries')),
    # the assemblies and the parts beside them, by the focus
    ('stat', 'INVWINPT', 'ASSY', stat(ADMIN, '3 assemblies')),
    ('list', 'INVWINPT', 'ASSY'),
    ('stat', 'INVWINPT', 'PARTS', stat(ADMIN, '2 parts')),
    ('tool', 'INVWINPT', 'PARTS',
     'New|Open|Edit|~Delete|-|~Move|~Install|~Lend|-|*|Find'),
    ('stat', 'INVWINPT', 'INSTALL',
     stat(ADMIN, '3 parts', 'Item 2 installed in item 1')),
    ('stat', 'INVWINPT', 'READER', stat(READER, '1 part')),
    ('stat', 'INVWINPT', 'LAST', stat(
        ADMIN, '2 assemblies', 'Item 6 taken out to Office > Shelf A1')),
    ('list', 'INVWINPT', 'LAST'),
    ('stat', 'INVWINPT', 'LAST2', stat(
        ADMIN, '1 assembly', 'Item 8 taken out to Office > Shelf A1')),
    ('list', 'INVWINPT', 'LAST2'),
    ('stat', 'INVWINPT', 'EMPTY', stat(
        ADMIN, '0 assemblies', 'Item 4 taken out to Office > Shelf A1')),
    ('stat', 'INVWINPX', 'ASSYCAT', stat(READER, '0 assemblies')),
    ('stat', 'INVWINPS', 'LONG', stat(ADMIN, '30 assemblies')),
    ('stat', 'INVWINPS', 'COUNTED', stat(
        ADMIN, '29 assemblies', 'Item 5 taken out to Office > Shelf A1')),
    ('list', 'INVWINPS', 'COUNTED'),
    ('stat', 'INVWINPS', 'PICKED',
     stat(ADMIN, 'Shift+F8 add mode: 12 selected')),
    ('stat', 'INVWINPS', 'BIGGER', stat(ADMIN, '25 parts')),
    # a row come above the one the parts follow, and that row gone
    ('stat', 'INVWINPR', 'PARTS', stat(ADMIN, '6 parts')),
    ('stat', 'INVWINPR', 'ABOVE', stat(
        ADMIN, '3 parts', 'Item 1 taken out to Office > Shelf A1')),
    ('stat', 'INVWINPR', 'GONE', stat(
        ADMIN, '2 assemblies', 'Item 6 taken out to Office > Shelf A1')),
    ('list', 'INVWINPR', 'GONE'),
    # the loans and holds and an item's history beside them
    ('stat', 'INVWINPH', 'LOANS', stat(ADMIN, '4 loans and holds')),
    ('list', 'INVWINPH', 'LOANS'),
    ('stat', 'INVWINPH', 'SEVERAL', stat(ADMIN, '2 selected')),
    ('stat', 'INVWINPH', 'HISTORY', stat(ADMIN, '2 loans and holds')),
    ('tool', 'INVWINPH', 'HISTORY',
     'New|Open|Edit|~Delete|-|~Move|~Install|Lend|-|*|Find'),
    ('stat', 'INVWINPH', 'KEPT',
     stat(ADMIN, '2 loans and holds', 'Loan 3 returned')),
    ('stat', 'INVWINPH', 'ABOVE',
     stat(ADMIN, '3 loans and holds', 'Loan 6 added')),
    ('stat', 'INVWINPH', 'ENDED',
     stat(ADMIN, '3 loans and holds', 'Loan 6 returned')),
    ('list', 'INVWINPH', 'ENDED'),
    ('stat', 'INVWINPH', 'RELEASED', stat(
        ADMIN, '2 loans and holds', 'Reservation 4 released')),
    ('list', 'INVWINPH', 'RELEASED'),
    ('title', 'INVWINPH', 'NOUSES', 'In use'),
    ('stat', 'INVWINPH', 'NOKEY', stat(ADMIN, '0 loans and holds')),
    ('list', 'INVWINPH', 'NOKEY'),
    ('stat', 'INVWINPH', 'READER', stat(READER, '2 loans and holds')),
    # the users and, with no setting, a user's audit entries beside
    # them, counted past their first page
    ('title', 'INVWINPU', 'AUDIT', 'Audit log'),
    ('stat', 'INVWINPU', 'USERS', stat('BOSS, level 3', '5 users')),
    ('list', 'INVWINPU', 'USERS'),
    ('stat', 'INVWINPU', 'LONG', stat('BOSS, level 3', '27 entries')),
    ('stat', 'INVWINPU', 'EMPTY', stat('BOSS, level 3', '0 entries')),
    ('stat', 'INVWINPU', 'TRAIL', stat('BOSS, level 3', '2 entries')),
    ('tool', 'INVWINPU', 'TRAIL',
     '~New|~Open|~Edit|~Delete|-|~Move|~Install|~Lend|-|*|Find'),
    ('stat', 'INVWINPU', 'EDITED',
     stat('BOSS, level 3', '5 users', 'User CLERK saved')),
    ('title', 'INVWINPU', 'LEVEL2', 'Items by ID'),
    # the categories and a category's items beside them
    ('stat', 'INVWINBA', 'CATS', stat(ADMIN, '10 categories')),
    ('list', 'INVWINBA', 'CATS'),
    ('tool', 'INVWINBA', 'CATS',
     'New|Open|Edit|Delete|-|~Move|~Install|~Lend|-|*|Find'),
    ('stat', 'INVWINBA', 'FROMCATS',
     stat(ADMIN, '10 categories', 'Item 7 added')),
    ('stat', 'INVWINBA', 'EMPTY', stat(ADMIN, '0 items')),
    ('stat', 'INVWINBA', 'PANE', stat(ADMIN, '2 items')),
    ('tool', 'INVWINBA', 'PANE', TOOL),
    ('stat', 'INVWINBA', 'RECAT', stat(ADMIN, '1 item', 'Item 6 saved')),
    ('stat', 'INVWINBA', 'MOVED',
     stat(ADMIN, '2 items', 'Item 6 moved to Office')),
    ('stat', 'INVWINBA', 'ABOVE', stat(ADMIN, '3 items')),
    ('stat', 'INVWINBA', 'OVERAA', stat(ADMIN, '0 items')),
    ('stat', 'INVWINBA', 'AAGONE', stat(ADMIN, '10 categories')),
    ('stat', 'INVWINBA', 'CATADDED',
     stat(ADMIN, '11 categories', 'Odds and ends added')),
    ('stat', 'INVWINBA', 'CATSAVED',
     stat(ADMIN, '11 categories', 'Miscellany saved')),
    ('stat', 'INVWINBA', 'KEPT', stat(ADMIN, '11 categories')),
    ('stat', 'INVWINBA', 'CATGONE',
     stat(ADMIN, '10 categories', 'Miscellany deleted')),
    ('title', 'INVWINBA', 'ALONE', 'Category CBL Cables'),
    ('stat', 'INVWINBA', 'ALONE', stat(ADMIN, '0 items')),
    ('title', 'INVWINBA', 'PARTS', 'Parts of item 9 Belkin'),
    ('title', 'INVWINBA', 'UNPARTS', 'Category CBL Cables'),
    ('title', 'INVWINBA', 'RETITLED', 'Category CBL Leads'),
    ('title', 'INVWINBA', 'BACK', 'Categories'),
    ('help', 'INVWINBA', 'HELP', 'IW_S_MBYCAT'),
    ('title', 'INVWINBA', 'ITEMS', 'Items by ID'),
    ('stat', 'INVWINBB', 'PAGE1', stat(ADMIN, '48 categories')),
    ('stat', 'INVWINBB', 'END', stat(ADMIN, '48 categories')),
    ('stat', 'INVWINBB', 'SIXTY', stat(ADMIN, '60 items')),
    ('stat', 'INVWINBB', 'RECAT', stat(ADMIN, '59 items', 'Item 4 saved')),
    ('stat', 'INVWINBB', 'DIALOG', stat(ADMIN, '49 categories')),
    ('stat', 'INVWINBB', 'LAST',
     stat(ADMIN, '50 categories', 'Last one added')),
    ('stat', 'INVWINBB', 'DELETED',
     stat(ADMIN, '49 categories', 'Processors deleted')),
    ('title', 'INVWINBB', 'OPENED', 'Category ZZ Last one'),
    ('stat', 'INVWINBC', 'READER', stat(READER, '10 categories')),
    ('tool', 'INVWINBC', 'READER', LEVEL1),
    ('dialog', 'INVWINBC', 'NONEW', 'RETROINV', None),
    ('dialog', 'INVWINBC', 'NOEDIT', 'RETROINV', None),
    ('dialog', 'INVWINBC', 'NODEL', 'RETROINV', None),
    ('dialog', 'INVWINBC', 'NOITEM', 'RETROINV', None),
    ('title', 'INVWINBC', 'RALONE', 'Category SYS Systems'),
    ('stat', 'INVWINBC', 'RENAMED',
     stat(CLERK, '10 categories', 'Computers saved')),
    ('dialog', 'INVWINBC', 'INUSE', 'RETROINV', None),
    ('stat', 'INVWINBC', 'DELETED',
     stat(CLERK, '9 categories', 'Motherboards deleted')),
    # a note beside the count, ended by the next key
    ('stat', 'INVWINH', 'ADDED', stat(ADMIN, '4 items', 'Item 4 added')),
    ('stat', 'INVWINH', 'END', stat(ADMIN, '4 items')),
    ('stat', 'INVWINJB', 'OUT', stat(
        CLERK, '3 items', 'Item 3 taken out to Office > Shelf A1 > Box B2')),
    ('stat', 'INVWIN7', 'ADDED',
     stat(ADMIN, '4 locations', 'Location Shelf C3 added')),
    ('stat', 'INVWIN3', 'ADDED',
     stat(ADMIN, '2 service records', 'Service record 2 added')),
    # the Find box
    ('box', 'INVWINFB', 'ITEMS', '  Edit 219 ""'),
    ('stat', 'INVWINFB', 'ITEMS', stat(ADMIN, '3 items')),
    ('list', 'INVWINFB', 'ITEMS'),
    ('box', 'INVWINFB', 'BOX', '  Edit 219 "" FOCUS'),
    ('box', 'INVWINFB', 'TYPED', '  Edit 219 "IBM" FOCUS'),
    ('title', 'INVWINFB', 'FOUND', 'Items containing "IBM"'),
    ('box', 'INVWINFB', 'FOUND', '  Edit 219 "IBM"'),
    ('stat', 'INVWINFB', 'FOUND', stat(ADMIN, '1 item')),
    ('list', 'INVWINFB', 'FOUND'),
    ('box', 'INVWINFB', 'AGAIN', '  Edit 219 "IBM" FOCUS'),
    ('box', 'INVWINFB', 'REPLACED', '  Edit 219 "Tandon" FOCUS'),
    ('title', 'INVWINFB', 'ESCBACK', 'Items containing "IBM"'),
    ('box', 'INVWINFB', 'ESCBACK', '  Edit 219 "Tandon"'),
    ('list', 'INVWINFB', 'ESCBACK'),
    ('title', 'INVWINFB', 'TABBACK', 'Items containing "IBM"'),
    ('box', 'INVWINFB', 'TABBACK', '  Edit 219 "Tandon"'),
    ('list', 'INVWINFB', 'TABBACK'),
    ('title', 'INVWINFB', 'FOUND2', 'Items containing "Tandon"'),
    ('box', 'INVWINFB', 'FOUND2', '  Edit 219 "Tandon"'),
    ('stat', 'INVWINFB', 'FOUND2', stat(ADMIN, '1 item')),
    ('list', 'INVWINFB', 'FOUND2'),
    ('dialog', 'INVWINFB', 'DIALOG', 'Search items', ' Edit 100 "Tandon"'),
    ('title', 'INVWINFB', 'LIST', 'Items by ID'),
    ('box', 'INVWINFB', 'LIST', '  Edit 219 "Tandon"'),
    ('list', 'INVWINFB', 'LIST'),
    ('dialog', 'INVWINFB', 'BLANKS', 'Search items', None),
    ('title', 'INVWINFB', 'BLANKS', 'Items by ID'),
    ('list', 'INVWINFB', 'BLANKBK'),
    ('dialog', 'INVWINFB', 'EMPTY', 'Search items', None),
    ('box', 'INVWINFB', 'EMPTY', '  Edit 219 ""'),
    ('list', 'INVWINFB', 'EMPTYBK'),
    ('dialog', 'INVWINFB', 'F3', 'Search items', None),
    ('box', 'INVWINFB', 'F3', '  Edit 219 "Hercules"'),
    ('tool', 'INVWINFB', 'LOGOUT', ALLGRAY),
    ('stat', 'INVWINFB', 'LOGOUT', stat('', '')),
    ('tool', 'INVWINFB', 'READER', LEVEL1),
    ('stat', 'INVWINFB', 'READER', stat(READER, '3 items')),
    ('title', 'INVWINFB', 'RFOUND', 'Items containing "IBM"'),
    ('box', 'INVWINFB', 'RFOUND', '  Edit 219 "IBM"'),
    ('stat', 'INVWINFB', 'RFOUND', stat(READER, '1 item')),
    ('list', 'INVWINFB', 'RFOUND'),
    # no item holds the text: no rows, and ENTER on them opens nothing
    ('title', 'INVWINFB', 'NOFIND', 'Items containing "zzz"'),
    ('stat', 'INVWINFB', 'NOFIND', stat(READER, '0 items')),
    ('title', 'INVWINFB', 'NOENTER', 'Items containing "zzz"'),
    ('list', 'INVWINFB', 'NOENTER'),
    # the help panel
    ('stat', 'INVWINST', 'ITEMS', stat(ADMIN, '3 items')),
    ('help', 'INVWINST', 'FILE', 'MN_LOGOUT'),
    ('stat', 'INVWINST', 'FILE', stat(
        ADMIN, '3 items', 'Log out, then log in again as any user')),
    ('help', 'INVWINST', 'EXIT', 'IW_S_MEXIT'),
    ('help', 'INVWINST', 'ITEMMENU', 'MN_BYID'),
    ('help', 'INVWINST', 'RECORD', 'IW_S_ROPEN'),
    ('help', 'INVWINST', 'VIEWMENU', 'IW_S_VPANE'),
    ('help', 'INVWINST', 'REFRESH', 'IW_S_VREFRESH'),
    ('help', 'INVWINST', 'HELPMENU', 'IW_S_MHELPON'),
    ('help', 'INVWINST', 'BAR', None),
    ('help', 'INVWINST', 'CLOSED', None),
    ('list', 'INVWINST', 'CLOSED'),
    ('help', 'INVWINST', 'SYSMENU', None),
    ('help', 'INVWINST', 'SYSCLOSE', None),
    ('list', 'INVWINST', 'SYSCLOSE'),
    ('help', 'INVWINST', 'POPUP', 'IW_S_ROPEN'),
    ('help', 'INVWINST', 'POPCLOSE', None),
    ('help', 'INVWINST', 'RREC', 'IW_S_ROPEN'),
    ('help', 'INVWINST', 'RGRAYED', 'IW_S_REDIT'),
    ('gray', 'INVWINST', 'RGRAYED', 1093),
    ('help', 'INVWINST', 'RCLOSED', None),
    ('stat', 'INVWINST', 'RCLOSED', stat(READER, '3 items')),
)

DEFINE = re.compile(r'#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)\b')
STRING = re.compile(r'^\s*(\w+)(?:\s*\+\s*(\w+))?\s*,\s*"((?:[^"]|"")*)"\s*$')
TOOLCMDS = re.compile(r'\btool_cmds\s*\[[^]]*\]\s*=\s*\{([^}]*)\}')
COMMENT = re.compile(r'/\*.*?\*/', re.S)
CAST = re.compile(r'\(\s*(?:UINT|WORD|u16|unsigned)\s*\)')
CHAR = re.compile(r"'(.)'")
WTOOL = re.compile(r' WTOOL (\d+) "(.*)"((?: [A-Z]+)*)$')
BOX = re.compile(r'  Edit (\d+) "(.*)"((?: [A-Z]+)*)$')
WSTAT = re.compile(r' WSTAT (\d+) "(.*)"((?: [A-Z]+)*)$')
USERRX = re.compile(r'^[A-Z0-9_]+, level [1-3]$')
CHILD = re.compile(r' (\w+) (\d+) "(.*)"(.*)$')
WLISTLN = re.compile(r'"(?: HBAR (\d+) (\d+))?((?: [A-Z]+)*)$')
WSPLITLN = re.compile(r' WSPLIT (\d+) "" AT (-?\d+) LIMITS (\d+) (\d+)'
                      r'( FOCUS CAPTURE)?$')
WPREVLN = re.compile(r' WPREV (\d+) "(.*)"(?: VBAR (\d+) (\d+))?'
                     r'((?: [A-Z]+)*)$')
ROWLN = re.compile(r'   ROW "(.*)"((?: [A-Z]+)*)$')
BOXLN = re.compile(r'  ListBox \d+ "(.*)"((?: [A-Z]+)*)$')
# The virtual keys of windows.h that the tool bar's commands name.
VK = {'VK_RETURN': 0x0D, 'VK_INSERT': 0x2D, 'VK_DELETE': 0x2E}


def defines(path):
    """The numbers #defined in the file at path."""
    out = {}
    for line in menuchk.read(path) or ():
        m = DEFINE.search(line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


# The bytes a panel's text keeps, but the last panel's (WSTAT.H).
CELL = defines(os.path.join(WIN, 'WSTAT.H')).get('WSB_MAXCELL', 0)


def value(expr, names):
    """The number of the C constant expression expr, or None."""
    expr = CHAR.sub(lambda m: str(ord(m.group(1))), CAST.sub('', expr))

    def walk(n):
        if isinstance(n, ast.Expression):
            return walk(n.body)
        if isinstance(n, ast.Constant) and isinstance(n.value, int):
            return n.value
        if isinstance(n, ast.Name):
            return names[n.id]
        if isinstance(n, ast.BinOp) and isinstance(n.op, (ast.Add, ast.Sub,
                                                          ast.BitOr)):
            a, b = walk(n.left), walk(n.right)
            return {ast.Add: a + b, ast.Sub: a - b,
                    ast.BitOr: a | b}[type(n.op)]
        raise ValueError(expr)
    try:
        return walk(ast.parse(expr.strip(), mode='eval'))
    except (SyntaxError, ValueError, KeyError):
        return None


def strings(names):
    """INVWIN.RC's strings by number."""
    out = {}
    for line in menuchk.read(os.path.join(WIN, 'INVWIN.RC')) or ():
        m = STRING.match(line)
        if not m or m.group(1) not in names:
            continue
        num = names[m.group(1)]
        if m.group(2):
            add = value(m.group(2), names)
            if add is None:
                continue
            num += add
        out[num] = m.group(3).replace('""', '"')
    return out


def name_of(text):
    """A menu entry's name: its text without '&', '...' and the key."""
    return text.split('?')[0].replace('&', '').replace('...', '')


def record(lines):
    """The Record menu's entries of a snapshot: (id, name, grayed)."""
    return [(eid, name_of(text), 'DISABLED' in state.split())
            for title, entries in menuchk.menus(menuchk.frame(lines)[1])
            if title == 'Re&cord' for eid, text, state in entries if eid]


def source_bad(cache):
    """Why the sources fail, one line each, and the help texts by the
    name of the text their entry shows."""
    bad = []
    names = defines(os.path.join(WIN, 'INVWIN.H'))
    nums, helps = menuchk.sources(bad)
    names.update(nums)
    names.update(VK)
    names.update(defines(os.path.join(WIN, 'INVWIN.C')))
    names.update(defines(os.path.join(WIN, 'WSCR.H')))
    text = strings(names)
    for name, want in (('IW_S_TOOL', TOOL), ('IW_S_USER', USER)):
        got = text.get(names.get(name))
        if got != want:
            bad.append('INVWIN.RC: %s is %r, want %r' % (name, got, want))
    for name, plural, single in NOUNS:
        num = names.get(name)
        if num is None or num % 2:
            bad.append('INVWIN.H: %s %s, want an even number' % (name, num))
            continue
        for n, want in ((num, plural), (num + 1, single)):
            if text.get(n) != '%lu ' + want:
                bad.append('INVWIN.RC: string %d (%s) is %r, want %r' % (
                    n, name, text.get(n), '%lu ' + want))
    find = names.get('IW_M_FIND')
    if find is None:
        bad.append('INVWIN.H: no IW_M_FIND')
    else:
        for name, num in sorted(names.items()):
            if name.startswith('IW_M_') and name != 'IW_M_FIND' and \
                    num == find:
                bad.append('INVWIN.H: IW_M_FIND is %s too' % name)
    cmds = {}
    if cache.get('INVWIN') is None:
        cache['INVWIN'] = colchk.snaps('INVWIN')
    for eid, name, _ in record((cache['INVWIN'] or {}).get('ADMIN', ())):
        for button, texts in BUTTONS.items():
            if name in texts:
                cmds[button] = eid
    want = [cmds.get(p) if p != FIND else find for p in TOOL.split('|')
            if p in BUTTONS or p == FIND]
    with open(os.path.join(WIN, 'INVWIN.C'), encoding='latin-1') as f:
        m = TOOLCMDS.search(COMMENT.sub(' ', f.read()))
    got = [value(e, names) for e in m.group(1).split(',')
           if e.strip()] if m else None
    if got != want:
        bad.append('INVWIN.C: tool_cmds %s, want %s (the Record entries\' '
                   'commands in IW_S_TOOL\'s order, then IW_M_FIND)' % (
                       got, want))
    return bad, helps


def noun(title):
    """The count string's name for the frame titled "RETROINV - title"."""
    for start, name in NOUN:
        if title.startswith(start):
            return name
    return 'IW_S_NITEMS'


def chrome(lines):
    """The frame's line, its tool bar, box and status bar lines (None
    where missing) and the index after them."""
    for i, line in enumerate(lines):
        if line.startswith(menuchk.FRAME):
            j = i + 1 + len(menuchk.frame(lines)[1])
            got = lines[j:j + 3] + [None] * 3
            return line, got[0], got[1], got[2], j + 3
    return None, None, None, None, 0


def title_of(line):
    """The frame's title after "RETROINV - ", '' for none."""
    m = re.match(r'INVWIN "RETROINV(?: - (.*))?"(?: [A-Z]+)*$', line)
    return (m.group(1) or '') if m else None


def rows(lines, start):
    """The rows of the frame's list shown, from index start on."""
    n = 0
    for line in lines[start:]:
        if not line.startswith(' '):
            break
        n += line.startswith('   ROW ')
    return n


def panes(lines, start):
    """The frame's list windows, splitter and preview panel, one deep
    from index start on, by control ID: each a dict of its index 'at',
    its line, its state words 'st', its horizontal bar's (pos, max) or
    None, its rows' texts, its selected row's text or None (the last
    when several are), 'nsel' the rows selected, 'focus', 1 when its box
    has the focus, and 'held', 1 when its box holds the mouse; the
    preview panel's 'rows' are its PV lines."""
    out = {}
    cur = None
    for i in range(start, len(lines)):
        line = lines[i]
        if not line.startswith(' '):
            break
        if not line.startswith('  '):
            m = CHILD.match(line)
            cur = None
            if m and m.group(1) in ('WLIST', 'WSPLIT', 'WPREV'):
                w = WLISTLN.search(line)
                cur = out.setdefault(int(m.group(2)), {
                    'at': i, 'line': line, 'rows': [], 'sel': None,
                    'nsel': 0, 'focus': 0, 'held': 0,
                    'st': w.group(3).split() if w else [],
                    'bar': (int(w.group(1)), int(w.group(2)))
                    if w and w.group(1) else None})
        elif cur is not None:
            if line.startswith('  PV "'):
                cur['rows'].append(line)
            m = BOXLN.match(line)
            if m:
                cur['focus'] = int('FOCUS' in m.group(2).split())
                cur['held'] = int(HELD in m.group(2).split())
            m = ROWLN.match(line)
            if m:
                cur['rows'].append(m.group(1))
                if 'SELECTED' in m.group(2).split():
                    cur['sel'] = m.group(1)
                    cur['nsel'] += 1
    return out


def loc_name(row):
    """A location's name in its row of the tree."""
    return row[:TREE_NAME].strip()


def preview_bad(where, gold, title, ps):
    """Why the preview beside the list of the frame's children ps fails,
    one line each."""
    if gold not in PREVIEW or not title.startswith(PREVIEWED):
        return ['%s: a preview on "%s"' % (where, title)]
    if not all(c in ps for c in (C_LIST, C_SPLIT, C_PREVIEW)):
        return ['%s: the preview without its list or splitter' % where]
    bad = []
    order = [ps[c]['at'] for c in (C_LIST, C_SPLIT, C_PREVIEW)]
    if order != sorted(order):
        bad.append('%s: the panes not list, splitter, preview' % where)
    m = WSPLITLN.match(ps[C_SPLIT]['line'])
    if not m or not int(m.group(3)) <= int(m.group(2)) <= int(m.group(4)):
        bad.append('%s: %s' % (where, ps[C_SPLIT]['line']))
    prev = ps[C_PREVIEW]
    m = WPREVLN.match(prev['line'])
    st = m.group(5).split() if m else ['?']
    empty = not ps[C_LIST]['sel']
    if not m or set(st) - set(['DISABLED', 'FOCUS']) or \
            ('DISABLED' in st) != empty or \
            (empty and (m.group(2) or prev['rows'])) or \
            (m.group(2) and prev['rows']) or \
            (m.group(3) and not int(m.group(3)) <= int(m.group(4))) or \
            ('FOCUS' in st and ps[C_LIST]['focus']):
        bad.append('%s: %s, %d lines' % (where, prev['line'],
                                        len(prev['rows'])))
    return bad


def detail(title):
    """The count string's name of the rows of the pane beside the list
    of the frame titled "RETROINV - title", a list; None without one."""
    for start, name in DETAIL:
        if title.startswith(start):
            return name
    return None


def split_bad(where, gold, title, ps):
    """Why the split view of the frame's children ps fails, or does not
    show where it should, one line each."""
    if title != SPLIT_TITLE and (C_SPLIT in ps or C_PREVIEW in ps) and \
            C_CLIST not in ps:
        return preview_bad(where, gold, title, ps)
    must = title == SPLIT_TITLE and C_LIST in ps and gold not in ALONE
    may = must or (detail(title) is not None and gold in PANED)
    if not may or not (must or C_SPLIT in ps or C_CLIST in ps):
        if C_SPLIT in ps or C_CLIST in ps:
            return ['%s: a split on "%s"' % (where, title)]
        return []
    if C_PREVIEW in ps:
        return ['%s: a preview on "%s"' % (where, title)]
    if C_SPLIT not in ps or C_CLIST not in ps:
        return ['%s: "%s" without the split' % (where, title)]
    bad = []
    order = [ps[c]['at'] for c in (C_LIST, C_SPLIT, C_CLIST)]
    if order != sorted(order):
        bad.append('%s: the panes not master, splitter, pane' % where)
    m = WSPLITLN.match(ps[C_SPLIT]['line'])
    if not m or not int(m.group(3)) <= int(m.group(2)) <= int(m.group(4)):
        bad.append('%s: %s' % (where, ps[C_SPLIT]['line']))
    cont = ps[C_CLIST]
    if set(cont['st']) - set(['DISABLED', HELD]) or (
            'DISABLED' in cont['st'] and cont['rows']):
        bad.append('%s: %s, %d rows' % (where, cont['line'],
                                        len(cont['rows'])))
    if ps[C_LIST]['focus'] and cont['focus']:
        bad.append('%s: both panes with the focus' % where)
    return bad


def counted(count, plural, single, shown, tail=''):
    """1 when count is "<n> <noun><tail>", singular for 1, n at least
    the rows shown (one "No ..." row for 0), the whole cut to a panel's
    room."""
    m = re.match(r'(0|[1-9]\d*) ', count)
    if not m:
        return 0
    n = int(m.group(1))
    want = ('%d %s%s' % (n, single if n == 1 else plural, tail))[:CELL]
    return count == want and (n >= shown or n == 0 and shown <= 1)


def several(count, pane):
    """1 when count is NSEL of the rows selected in pane, more than one."""
    return pane['nsel'] > 1 and count == NSEL % pane['nsel']


def every(count, pane):
    """1 when count is NALL of the rows of pane's list: every row it
    shows SELECTED, and no more of them than it counts."""
    m = NALL.match(count)
    return m is not None and \
        1 <= pane['nsel'] == len(pane['rows']) <= int(m.group(1))


def chosen_bad(where, count, ps):
    """Why the count panel fails for several rows selected in the frame's
    lists ps, for every row of a list chosen or for the add mode's rows,
    '' when it shows them, None when no list decides it: the
    list, or on the split view the pane whose box has the focus, either
    while neither has it."""
    if C_CLIST not in ps:
        lists = [ps[C_LIST]] if C_LIST in ps else []
    else:
        lists = [p for p in (ps[C_LIST], ps[C_CLIST]) if p['focus']] or \
            [p for p in (ps[C_LIST], ps[C_CLIST])
             if several(count, p) or every(count, p)]
    if lists and (count == NADD % max(lists[0]['nsel'], 1) or
                  every(count, lists[0])):
        return ''
    lists = [p for p in lists if p['nsel'] > 1]
    if not lists:
        return None
    if several(count, lists[0]):
        return ''
    return '%s: count "%s", want "%s"' % (where, count,
                                          NSEL % lists[0]['nsel'])


def split_count_bad(where, count, title, ps):
    """Why the count panel of the split view ps fails, on the frame
    titled "RETROINV - title", or None."""
    tree, cont = ps[C_LIST], ps[C_CLIST]
    locs = counted(count, *NOUNS_OF[noun(title)], shown=len(tree['rows']))
    if title != SPLIT_TITLE:
        here = counted(count, *NOUNS_OF[detail(title)],
                       shown=len(cont['rows']))
    elif tree['sel'] is None:
        here = re.match(r'(0|[1-9]\d*) items? in .', count) is not None
    else:
        here = counted(count, *NOUNS_OF['IW_S_NITEMS'],
                       shown=len(cont['rows']),
                       tail=' in ' + loc_name(tree['sel']))
    if (tree['focus'] and locs) or (cont['focus'] and here) or (
            not tree['focus'] and not cont['focus'] and (locs or here)):
        return None
    return '%s: count "%s", want the %s\'s' % (
        where, count, 'pane' if cont['focus'] else 'master')


def held_bad(where, gold, tool, held, ps):
    """Why the tool bar's pressed buttons (its pieces held) or the frame's
    windows holding the mouse fail, one line each."""
    bad = []
    if held and (len(held) > 1 or held[0].startswith('~') or
                 not tool.endswith(' ' + HELD)):
        bad.append('%s: pressed %s on %s' % (where, held, tool))
    if (held or tool.endswith(' ' + HELD)) and gold != TOOLHELD:
        bad.append('%s: a tool button held: %s' % (where, tool))
    if gold not in MOUSE and any(
            p['held'] or HELD in p['st'] or p['line'].endswith(' ' + HELD)
            for p in ps.values()):
        bad.append('%s: a pane or the splitter holds the mouse' % where)
    return bad


def menu_open(lines):
    """1 when a menu window is shown."""
    return '#32768 ""' in lines


def snap_bad(gold, snap, lines, helps):
    """Why a snapshot of the frame fails, one line each."""
    where = '%s.SNP %s' % (gold, snap)
    line, tool, box, sbar, after = chrome(lines)
    if line is None:
        return []
    bad = []
    if any(ln.startswith(FRAMEBAR) for ln in lines):
        bad.append('%s: the status line still there' % where)
    mt = WTOOL.match(tool or '')
    mb = BOX.match(box or '')
    ms = WSTAT.match(sbar or '')
    if not (mt and mb and ms and int(mt.group(1)) == C_TOOL and
            int(mb.group(1)) == C_FIND and int(ms.group(1)) == C_STATUS and
            mt.group(3) in ('', ' ' + HELD) and not ms.group(3)):
        return bad + ['%s: under the frame %s, want WTOOL %d, its Edit %d, '
                      'WSTAT %d' % (where, [tool, box, sbar], C_TOOL, C_FIND,
                                    C_STATUS)]
    bar = menuchk.frame(lines)[1]
    states = mb.group(3).split()
    rec = {}
    for _, name, gray in record(lines):
        rec[name] = rec.get(name, True) and gray
    shown = mt.group(2).split('|')
    held = [p for p in shown if p.lstrip('~').startswith(PRESSED)]
    shown = '|'.join(p.replace(PRESSED, '', 1) if p in held else p
                     for p in shown)
    pieces = []
    for p in TOOL.split('|'):
        if p in BUTTONS:
            off = not bar or all(rec.get(t, True) for t in BUTTONS[p])
        else:
            off = p == FIND and not bar
        pieces.append(('~' if off else '') + p)
    if shown != '|'.join(pieces):
        bad.append('%s: tool bar "%s", want "%s"' % (
            where, mt.group(2), '|'.join(pieces)))
    if ('DISABLED' in states) == bool(bar):
        bad.append('%s: Find box %s' % (where, box))
    if gold not in BOXED and (mb.group(2) or 'FOCUS' in states):
        bad.append('%s: Find box %s, want it empty, without the focus' % (
            where, box))
    panels = ms.group(2).split('|')
    if len(panels) != 4:
        return bad + ['%s: status bar "%s", want 4 panels' % (
            where, ms.group(2))]
    conn, user, count, help_ = panels
    if conn != CONN.get(gold, LOCAL):
        bad.append('%s: connection "%s", want "%s"' % (
            where, conn, CONN.get(gold, LOCAL)))
    if (USERRX.match(user) is None) == bool(bar) or (not bar and user):
        bad.append('%s: user "%s"%s' % (
            where, user, '' if bar else ', logged out'))
    title = title_of(line)
    ps = panes(lines, after)
    bad += split_bad(where, gold, title, ps)
    bad += held_bad(where, gold, tool, held, ps)
    want = None
    why = chosen_bad(where, count, ps) if bar and title else None
    if why is not None:
        if why:
            bad.append(why)
    elif bar and title and not conn.startswith(UNCOUNTED) and \
            C_CLIST in ps and C_LIST in ps:
        why = split_count_bad(where, count, title, ps)
        if why:
            bad.append(why)
    elif bar and title and not conn.startswith(UNCOUNTED):
        plural, single = NOUNS_OF[noun(title)]
        m = re.match(r'(0|[1-9]\d*) (.+)$', count)
        n = int(m.group(1)) if m else -1
        want = '%d %s' % (n, single if n == 1 else plural)
        shown = rows(lines, after)
        if count != want or not (n >= shown or n == 0 and shown <= 1):
            bad.append('%s: count "%s", want "<n> %s" (n %s), %d rows '
                       'shown' % (where, count, plural, single, shown))
    elif count:
        bad.append('%s: count "%s", want none' % (where, count))
    if not bar and help_:
        bad.append('%s: help "%s" logged out' % (where, help_))
    elif menu_open(lines) and help_ and help_ not in helps:
        bad.append('%s: help "%s" with a menu open' % (where, help_))
    elif not menu_open(lines) and help_ in helps:
        bad.append('%s: help "%s" with no menu open' % (where, help_))
    return bad


def case_bad(case, cache, help_of):
    """None when case holds, else why not."""
    kind, gold, snap = case[:3]
    if gold not in cache:
        cache[gold] = colchk.snaps(gold)
    if cache[gold] is None:
        return '%s.SNP: cannot read it' % gold
    lines = cache[gold].get(snap)
    if lines is None:
        return '%s.SNP: no snapshot %s' % (gold, snap)
    where = '%s.SNP %s' % (gold, snap)
    line, tool, box, sbar, _ = chrome(lines)
    if line is None:
        return '%s: no frame' % where
    tops = [ln for ln in lines if not ln.startswith(' ')]
    got = want = None
    if kind == 'tool':
        got, want = tool, ' WTOOL %d "%s"' % (C_TOOL, case[3])
    elif kind == 'box':
        got, want = box, case[3]
    elif kind == 'stat':
        got, want = sbar, ' WSTAT %d "%s"' % (C_STATUS, case[3])
    elif kind == 'help':
        m = WSTAT.match(sbar or '')
        got = m.group(2).split('|')[3:] if m else None
        want = [help_of.get(case[3], '?' + case[3])] if case[3] else ['']
    elif kind == 'title':
        got, want = title_of(line), case[3]
    elif kind == 'list':
        dialogs = [t for t in tops if t.startswith('#32770 ')]
        if LISTFOCUS in lines and not dialogs and line.endswith(' ACTIVE'):
            return None
        return '%s: the list without the focus, or a dialog open: %s' % (
            where, ' | '.join(tops))
    elif kind == 'dialog':
        head = '#32770 "%s" ' % case[3]
        if any(t.startswith(head) for t in tops) and (
                case[4] is None or
                any(ln.startswith(case[4]) for ln in lines)):
            return None
        return '%s: no dialog "%s" holding %s' % (where, case[3], case[4])
    elif kind == 'gray':
        got = [gray for eid, _, gray in record(lines) if eid == case[3]]
        want = [True]
    else:
        return '%s: unknown case %s' % (where, kind)
    if got == want:
        return None
    return '%s: %s\n  want %s\n  got  %s' % (where, kind, want, got)


def main():
    cache = {}
    bad, helps = source_bad(cache)
    frames = 0
    for f in sorted(os.listdir(colchk.GOLD)):
        if not f.endswith('.SNP'):
            continue
        gold = f[:-4]
        if gold not in cache:
            cache[gold] = colchk.snaps(gold)
        for snap, lines in sorted((cache[gold] or {}).items()):
            if chrome(lines)[0] is not None:
                frames += 1
                bad += snap_bad(gold, snap, lines, set(helps.values()))
    bad += [why for why in (case_bad(c, cache, helps) for c in CASES)
            if why]
    for gold in PREVIEW:
        if not any(ln.startswith(' WPREV %d ' % C_PREVIEW)
                   for lines in (cache.get(gold) or {}).values()
                   for ln in lines):
            bad.append('%s.SNP: no preview in any snapshot' % gold)
    for gold in PANED:
        if not any(title_of(chrome(lines)[0] or '') != SPLIT_TITLE and
                   any(ln.startswith(' WLIST %d ' % C_CLIST) for ln in lines)
                   for lines in (cache.get(gold) or {}).values()):
            bad.append('%s.SNP: no pane beside a list in any snapshot' % gold)
    if not frames:
        bad.append('no snapshot of the frame')
    bad = list(dict.fromkeys(bad))
    for why in bad:
        print('framchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('framchk: sources ok, %d frames ok, %d cases ok' % (
        frames, len(CASES)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
