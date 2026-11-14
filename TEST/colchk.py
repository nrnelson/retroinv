"""colchk.py - the column drawing in INVWIN's snapshots, against values
worked out apart from the program.

usage: python3 -I TEST/colchk.py

The test driver (SRC/WIN/TDRV.H) writes how a header and the rows of an
owner-drawn list are drawn in columns (WCOLS.H): a HEAD line under the
header's window line, a DRAW line under the ROW line of a box's first
row and of its selected row.  Each case below names a golden of
TEST/GOLDEN/WIN, a snapshot in it, a line of that snapshot and the line
that must follow it, computed by hand from the rules of WCOLS.H, the
column tables and the header words, with Helv 8's widths on the test
machine (HELVE.FON, 96 dpi, tmAveCharWidth 5, so a cell of 7 pixels and
px(n) = 15 * n / 2).

A list window's row (WROWS.H) is its fields at their cells, a blank
after each, numbers right-aligned (a negative count in the tables
below), its trailing blanks dropped; its header is the words of its
columns laid out the same way.  Each column's slot is its default width
times 5 pixels, x its left edge:

  items, ID order  cells 8R 25 30 30 5 6R 30 9,
                   x 0 55 135 235 335 375 415 515, row end 570
  items, serial    cells 8R 30 30 30 30, x 0 55 155 255 355, end 455
  parts            cells 8R 25 44I 30 6R 30 6,
                   x 0 55 135 255 355 395 495, end 535
  location tree    cells 44I 20 10 8R, x 0 150 230 285, end 340
  service, recent  cells 8R 8R 10 4 9R 60, x 0 55 110 175 210 265,
                   end 565
  service, item's  cells 8R 10 4 9R 60, x 0 55 120 155 210, end 510
  loans, every     cells 1 8R 8 8R 6R 30 10 10 30 30,
                   x 0 15 70 125 180 220 320 385 450 550, end 650
  loans, item's    cells 1 8R 8 6R 30 10 10,
                   x 0 15 70 125 165 265 330, end 395
  users            cells 8 30 8 5R 6 3 10,
                   x 0 67 187 254 289 329 359, end 424
  audit log        cells 10 8 8 16 8 8R 1 10 40 40,
                   x 0 65 115 182 252 297 352 367 449 599, end 749
  categories       cells 4 20 5R, x 0 45 145, end 185
  code list rows   cells 8 20 2R, x 0 67 225, end 247
  code list header cells 8 17 5R, x 0 67 202, end 247
  empty list       one column of LV_ROWTEXT cells, indent kept

A piece's room ends 7 pixels (the gap) before the next column's x; a
right piece ends there, an indented one starts 7 pixels a cell in, and a
piece wider than its room starts at its column's x.  A piece ending past
its room, but in the last column, makes its row or header CUT; the
default widths leave room for every code, date and header word, so only
free text (a long category name, a deep part's maker) is.  Plain text is
000000/FFFFFF, a selected row FFFFFF/000000, a list window's header
000000/C0C0C0, the button face it is drawn on, as is a dialog's header
Static, drawn as flat headings (WDLG.H).

A list window's HEAD line is followed by its COLS line: each column's
heading state, F flat, B a button naming one of the source's orders, A
the button of the order in use (only the item list has orders: its ID,
Maker and Location headings, and in the serial number order its Serial
number heading too), then its slot's x less the scroll and its width.

TEST/WIN/WINCOLS.CFG, INVWINCW's settings, keeps column widths in
average characters: WINCOLS.ITEM=20,0,40 after a malformed ITEM line,
the first good line taken; WINCOLS.TREE=36, a short line, the columns
after it their own; WINCOLS.SER=0,2, 0 the column's own width and 2
held to 3 (WCOL_MINW).  Every list of those tables follows them: the
item list in each order, the split's contents, the location tree, whose
Name and Type make the split's automatic width (279 = 260 + the box's
border and the vertical bar's 17), and the pick lists, whose dialogs
are sized so that no bar shows.  The frame's room is 632 pixels wide
(TEST/splitchk.py), a pane's box 2 less, the splitter 4.

Dates and numbers show in the test machine's [intl] of WIN.INI, which
holds the wire's forms: ISO dates, a decimal point, no thousands
separator.  A quantity's cells and a cost's have room for a thousands
separator (INTL_NUMW of INTL.H), one more than the field's width.
INVWINDE runs with TEST/WIN/INTLDE.INI's in its place: d.M.yyyy, a
decimal comma, a thousands point; its form lines are the control's line
after its label's, a record number as the wire gives it.

A list window whose row end passes its box's client width has a
horizontal bar, and its line ends in HBAR pos max: max the row end less
that width, pos the pixels scrolled, by which every piece's x is less.
The frame's width is fixed only when maximized (640 pixels, the box's
client 638 within its border, no vertical bar with so few rows): an
anchor written with ANYBAR, a list that has not scrolled, matches its
line with or without " HBAR 0 max", any max.  A case with no line after
it needs its line only; one with a number needs that many rows in the
window of its line, a page.  A case of COLS_CASES names a list window's
line and the COLS line that must follow its HEAD line.  A golden or
snapshot missing, a line not found, or a wrong line after it, fails.
Exit status 1 if any case fails.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GOLD = os.path.join(HERE, 'GOLDEN', 'WIN')

# The cells of each list window's columns, right-aligned ones negative.
ITEM = (-8, 25, 30, 30, 5, -6, 30, 9)
SER = (-8, 30, 30, 30, 30)
PART = (-8, 25, 44, 30, -6, 30, 6)
TREE = (44, 20, 10, -8)
SVC = (-8, -8, 10, 4, -9, 60)
SVCI = (-8, 10, 4, -9, 60)
LOAN = (1, -8, 8, -8, -6, 30, 10, 10, 30, 30)
LOANI = (1, -8, 8, -6, 30, 10, 10)
USER = (8, 30, 8, -5, 6, 3, 10)
AUDIT = (10, 8, 8, 16, 8, -8, 1, 10, 40, 40)
CAT = (4, 20, -5)


def row(cells, *fields):
    """The text of a row of columns of cells holding fields."""
    return ' '.join(f.rjust(-n) if n < 0 else f.ljust(n)
                    for n, f in zip(cells, fields)).rstrip()


def header(cells, *words):
    """A list window's line, its header of words, without a bar."""
    return ' WLIST 208 "%s"' % row(cells, *words)


def wlist(cells, *words):
    """A list window's line, unscrolled, at any width of the frame."""
    return header(cells, *words) + ANYBAR


def rowln(cells, st, *fields):
    """A list window's row line, in state st ('' for none)."""
    return '   ROW "%s"%s' % (row(cells, *fields), st and ' ' + st)


ANYBAR = ' HBAR 0 ?'
HEAD = '  HEAD 000000/C0C0C0'
DRAW = '    DRAW 000000/FFFFFF'
DRAWSEL = '    DRAW FFFFFF/000000'

CODE_HDR = ' Static 217 "Code     Name              Order"'
CODE_HEAD = ('  HEAD 000000/C0C0C0 0L@0+25"Code" 1L@67+28"Name"'
             ' 2R@214+26"Order"')

ITEM_SYS = ('1', 'SYS Systems', 'IBM', '5150', 'yes', '1', 'Office')
ITEM_DRV = ('2', 'DRV Drives', 'Tandon', 'TM100-2', 'no', '2', 'Box B2')
DRAW_ITEM = (' 2L@135+19"IBM" 3L@235+24"5150" 4L@335+16"yes" 5R@402+6"1"'
             ' 6L@415+28"Office"')
DRAW_SYS = ' 0R@42+6"1" 1L@55+63"SYS Systems"' + DRAW_ITEM
PART_DRV = ('DRV Drives', 'Tandon', 'TM100-2', '2', 'Office', 'A')
PART_HGC = ('Graphics Card', '1', 'Office', 'A')
DRAW_CARD = ' 0R@42+6"3" 1L@55+111"CARD Expansion cards"'
DRAW_HGC = (' 3L@255+67"Graphics Card" 4R@382+6"1" 5L@395+28"Office"'
            ' 6L@495+7"A"')
TREE_A1 = ('  Shelf A1', 'Shelf', 'A1')
SVC_RP = ('2026-10-01', 'RP', '', 'Recapped the power supply')
LOAN_HDR = wlist(LOAN, '!', 'Loan', 'Kind', 'Item', 'Qty', 'Holder', 'Due',
                 'Ended', 'Maker', 'Model')
LOAN_HEAD = (HEAD + ' 0L@0+3"!" 1R@39+24"Loan" 2L@70+21"Kind"'
             ' 3R@153+20"Item" 4R@197+16"Qty" 5L@220+31"Holder"'
             ' 6L@320+20"Due" 7L@385+31"Ended" 8L@450+30"Maker"'
             ' 9L@550+29"Model"')
AUDIT_WORDS = ('Date', 'Time', 'User', 'Station', 'Table', 'Record', 'A',
               'Field', 'Old value', 'New value')
AUDIT_HEAD = (HEAD + ' 0L@0+23"Date" 1L@65+23"Time" 2L@115+22"User"'
              ' 3L@182+33"Station" 4L@252+27"Table" 5R@310+35"Record"'
              ' 6L@352+7"A" 7L@367+22"Field" 8L@449+45"Old value"'
              ' 9L@599+51"New value"')
AUDIT_ROW = ('2026-10-08', '12:00:00', 'ADMIN', 'SEED', 'ITEMS', '1')
AUDIT_SEL = rowln(AUDIT, 'SELECTED', *(AUDIT_ROW + ('I', 'ITEM_ID', '', '1')))
DRAW_AUDIT = (DRAWSEL + ' 0L@0+54"2026-10-08" 1L@65+42"12:00:00"'
              ' 2L@115+35"ADMIN" 3L@182+29"SEED" 4L@252+33"ITEMS"'
              ' 5R@339+6"1" 6L@352+3"I" 7L@367+43"ITEM_ID" 9L@599+6"1"')

# The audit log in the maximized frame: a bar of 749 - 638 pixels, and
# the selected row scrolled 7 pixels, one cell, and 14.
AUDIT_BAR = header(AUDIT, *AUDIT_WORDS) + ' HBAR %d 111'
DRAW_AUDIT7 = (DRAWSEL + ' 0L@-7+54"2026-10-08" 1L@58+42"12:00:00"'
               ' 2L@108+35"ADMIN" 3L@175+29"SEED" 4L@245+33"ITEMS"'
               ' 5R@332+6"1" 6L@345+3"I" 7L@360+43"ITEM_ID" 9L@592+6"1"')
DRAW_AUDIT14 = (DRAWSEL + ' 0L@-14+54"2026-10-08" 1L@51+42"12:00:00"'
                ' 2L@101+35"ADMIN" 3L@168+29"SEED" 4L@238+33"ITEMS"'
                ' 5R@325+6"1" 6L@338+3"I" 7L@353+43"ITEM_ID" 9L@585+6"1"')

# The audit log of the ITEMS table, the seeding's 66 rows, longer than
# the window: the vertical bar shown, the box's client is 638 - 17, the
# bar's max 705 - 621; the page is 23 rows, where a list without the bar
# has 24: the bar's 17 pixels take the 24th row.
AUDIT_ALL = header(AUDIT, *AUDIT_WORDS) + ' HBAR 0 128'

# The service records in TEST/WIN/INTLDE.INI's forms: record 1 of
# 2026-10-01, and record 2 of 2026-10-05 costing 12345.67.
SVC_DE1 = ('1.10.2026', 'RP', '', 'Recapped the power supply')
SVC_DE2 = ('5.10.2026', 'RP', '12.345,67', 'Cleaned the drive heads')
# Seeded by TEST/KEYS/INTLDE.REQ: item 1001, of 1234 units at 1234.56
# acquired 2020-01-01, lent 1200 of them, due 2020-02-01; item 2's
# reservation, ended 2020-01-10; item 1001's audit rows at SEED_NOW by
# its station INTLDE.
DRAW_ITEM1001 = (DRAWSEL + ' 0R@24+24"1001" 1L@55+56"DRV Drives"'
                 ' 2L@135+37"Shugart" 3L@235+32"SA400" 4L@335+6"?"'
                 ' 5R@381+27"1.234" 6L@415+34"Box B2"')
LOAN_DE = rowln(LOAN, 'SELECTED', '!', '1', 'On loan', '1001', '1.200',
                'Ann Smith', '1.2.2020', '', 'Shugart', 'SA400')
DRAW_LOAN_DE = (DRAWSEL + ' 0L@0+3"!" 1R@57+6"1" 2L@70+37"On loan"'
                ' 3R@149+24"1001" 4R@186+27"1.200" 5L@220+48"Ann Smith"'
                ' 6L@320+42"1.2.2020" 8L@450+37"Shugart" 9L@550+32"SA400"')
AUDIT_DE = ('8.10.2026', '12:00:00', 'ADMIN', 'INTLDE', 'ITEMS', '1001')
DRAW_AUDIT_DE = (DRAWSEL + ' 0L@0+48"8.10.2026" 1L@65+42"12:00:00"'
                 ' 2L@115+35"ADMIN" 3L@182+39"INTLDE" 4L@252+33"ITEMS"'
                 ' 5R@321+24"1001" 6L@352+3"I" 7L@367+43"ITEM_ID"'
                 ' 9L@599+24"1001"')
DRAW_SVCI_DE1 = (' 0R@42+6"1" 1L@55+48"1.10.2026" 2L@120+15"RP"'
                 ' 4L@210+133"Recapped the power supply"')

# INVWINCW, TEST/WIN/WINCOLS.CFG's widths: items 20, own, 40, own...
# (x 0 100 180 380 480 520 560 660, end 715), the serial number order
# own, 3 (2 held), own... (x 0 55 70 170 270, end 370), the tree 36,
# own... (x 0 180 260 315, end 370).  The full item list's box is 630
# wide, the split's tree 279 - 2 and its contents 632 - 279 - 4 - 2.
ITEM_WORDS = ('ID', 'Category', 'Maker', 'Model', 'Works', 'Qty', 'Location',
              'Status')
SER_WORDS = ('ID', 'Maker', 'Model', 'Serial number', 'Location')
TREE_WORDS = ('Name', 'Type', 'Code', 'Capacity')
CW_ITEMHEAD = (HEAD + ' 0R@82+11"ID" 1L@100+42"Category" 2L@180+30"Maker"'
               ' 3L@380+29"Model" 4L@480+31"Works" 5R@537+16"Qty"'
               ' 6L@560+41"Location" 7L@660+30"Status"')
CW_SLOTS = ('0%s@0+100', '1F@100+80', '2%s@180+200', '3F@380+100',
            '4F@480+40', '5F@520+40', '6%s@560+100', '7F@660+55')
CW_SYS = (DRAWSEL + ' 0R@87+6"1" 1L@100+63"SYS Systems" 2L@180+19"IBM"'
          ' 3L@380+24"5150" 4L@480+16"yes" 5R@547+6"1" 6L@560+28"Office"')
CW_TREEHEAD = (HEAD + ' 0I@0+28"Name" 1L@180+24"Type" 2L@260+25"Code"'
               ' 3R@322+41"Capacity"')


def cols(*slots):
    """A COLS line of the slots, each kS@x+w."""
    return '  COLS ' + ' '.join(slots)


def cw_items(id_, mfr, loc):
    """The item list's COLS with the stored widths, the states of its
    ID, Maker and Location headings given."""
    return cols(*(CW_SLOTS[0] % id_, CW_SLOTS[1], CW_SLOTS[2] % mfr) +
                CW_SLOTS[3:6] + (CW_SLOTS[6] % loc, CW_SLOTS[7]))


# (golden, snapshot, line, the line after it, None, or the rows under it)
CASES = (
    # The Categories dialog: the header's Order ends over the digits, at
    # 240 as the order's "1" does; its first row selected.
    ('INVWINKB', 'CATS', CODE_HDR, CODE_HEAD),
    ('INVWINKB', 'CATS', '  ROW "SYS      Systems               1" SELECTED',
     '   DRAW FFFFFF/000000 0L@0+21"SYS" 1L@67+39"Systems" 2R@234+6"1"'),
    # A row added last and selected, the first row plain.
    ('INVWINKB', 'ADDED', '  ROW "SYS      Systems               1"',
     '   DRAW 000000/FFFFFF 0L@0+21"SYS" 1L@67+39"Systems" 2R@234+6"1"'),
    ('INVWINKB', 'ADDED',
     '  ROW "MISC     Odds and ends        60" SELECTED',
     '   DRAW FFFFFF/000000 0L@0+26"MISC" 1L@67+72"Odds and ends"'
     ' 2R@228+12"60"'),
    # The categories' list: Order over the digits, a code of wide
    # capitals whole in its 38 pixels.
    ('INVWINBA', 'CATS', wlist(CAT, 'Code', 'Name', 'Order'),
     HEAD + ' 0L@0+25"Code" 1L@45+28"Name" 2R@152+26"Order"'),
    ('INVWINBA', 'CATS', rowln(CAT, 'SELECTED', 'SYS', 'Systems', '1'),
     DRAWSEL + ' 0L@0+21"SYS" 1L@45+39"Systems" 2R@172+6"1"'),
    ('INVWINBA', 'NOITEMS',
     rowln(CAT, 'SELECTED', 'MOBO', 'Motherboards', '2'),
     DRAWSEL + ' 0L@0+32"MOBO" 1L@45+65"Motherboards" 2R@172+6"2"'),
    # The item list in ID order: its header and its first row,
    # selected, the ID and the quantity right-aligned, the category with
    # its name.
    ('INVWIN', 'ADMIN',
     wlist(ITEM, 'ID', 'Category', 'Maker', 'Model', 'Works', 'Qty',
           'Location', 'Status'),
     HEAD + ' 0R@37+11"ID" 1L@55+42"Category" 2L@135+30"Maker"'
     ' 3L@235+29"Model" 4L@335+31"Works" 5R@392+16"Qty"'
     ' 6L@415+41"Location" 7L@515+30"Status"'),
    ('INVWIN', 'ADMIN', rowln(ITEM, 'SELECTED', *ITEM_SYS),
     DRAWSEL + DRAW_SYS),
    # Expansion cards: its category, 111 pixels, past the room's 73.
    ('INVWIN', 'ADMIN', rowln(ITEM, '', *ITEM_DRV),
     rowln(ITEM, 'CUT', '3', 'CARD Expansion cards', 'Hercules',
           'Graphics Card', '?', '1', 'Shelf A1')),
    # In location order, the last row selected, the first row plain.
    ('INVWIND', 'LOC_END', rowln(ITEM, '', *ITEM_SYS), DRAW + DRAW_SYS),
    ('INVWIND', 'LOC_END', rowln(ITEM, 'SELECTED', *ITEM_DRV),
     DRAWSEL + ' 0R@42+6"2" 1L@55+56"DRV Drives" 2L@135+37"Tandon"'
     ' 3L@235+43"TM100-2" 4L@335+12"no" 5R@402+6"2" 6L@415+34"Box B2"'),
    # The serial number order: its header and a row without a serial
    # number, its empty column left out; the rows after it whole.
    ('INVWINC', 'BYSER',
     wlist(SER, 'ID', 'Maker', 'Model', 'Serial number', 'Location'),
     HEAD + ' 0R@37+11"ID" 1L@55+30"Maker" 2L@155+29"Model"'
     ' 3L@255+64"Serial number" 4L@355+41"Location"'),
    ('INVWINC', 'BYSER',
     rowln(SER, 'SELECTED', '2', 'Tandon', 'TM100-2', '', 'Box B2'),
     DRAWSEL + ' 0R@42+6"2" 1L@55+37"Tandon" 2L@155+43"TM100-2"'
     ' 4L@355+34"Box B2"'),
    ('INVWINC', 'BYSER',
     rowln(SER, '', '1', 'IBM', '5150', '0012345', 'Office'),
     rowln(SER, '', '3', 'Hercules', 'Graphics Card', 'HGC-0042',
           'Shelf A1')),
    # An item's parts: the header, Maker an indented column, and a part
    # at the first level, its category CUT.
    ('INVWINPA', 'ONE',
     wlist(PART, 'ID', 'Category', 'Maker', 'Model', 'Qty', 'Location',
           'Status'),
     HEAD + ' 0R@37+11"ID" 1L@55+42"Category" 2I@135+30"Maker"'
     ' 3L@255+29"Model" 4R@372+16"Qty" 5L@395+41"Location"'
     ' 6L@495+30"Status"'),
    ('INVWINPA', 'ONE',
     rowln(PART, 'SELECTED CUT', '3', 'CARD Expansion cards', 'Hercules',
           *PART_HGC),
     DRAWSEL + DRAW_CARD + ' 2I@135+42"Hercules"' + DRAW_HGC),
    # A part of a part, selected: its maker a level, 2 cells, 14 pixels
    # in.
    ('INVWINPA', 'TREEDN', rowln(PART, '', '2', *PART_DRV),
     DRAW + ' 0R@42+6"2" 1L@55+56"DRV Drives" 2I@135+37"Tandon"'
     ' 3L@255+43"TM100-2" 4R@382+6"2" 5L@395+28"Office" 6L@495+7"A"'),
    ('INVWINPA', 'TREEDN',
     rowln(PART, 'SELECTED CUT', '3', 'CARD Expansion cards', '  Hercules',
           *PART_HGC),
     DRAWSEL + DRAW_CARD + ' 2I@149+42"Hercules"' + DRAW_HGC),
    # Levels 8 and 9 alike indented WROW_BOMLEVELS - 1 levels, 14 cells:
    # 98 pixels in, Tandon ends at 265, past the room's 243.
    ('INVWINJC', 'CHAIN',
     rowln(PART, 'CUT', '9', PART_DRV[0], ' ' * 14 + PART_DRV[1],
           *PART_DRV[2:]),
     rowln(PART, 'CUT', '10', PART_DRV[0], ' ' * 14 + PART_DRV[1],
           *PART_DRV[2:])),
    # The location tree: no leading blank, a level's indent 2 cells;
    # Capacity right-aligned over the digits.
    ('INVWIN6', 'LOCS', wlist(TREE, 'Name', 'Type', 'Code', 'Capacity'),
     HEAD + ' 0I@0+28"Name" 1L@150+24"Type" 2L@230+25"Code"'
     ' 3R@292+41"Capacity"'),
    ('INVWIN6', 'LOCS', rowln(TREE, 'SELECTED', 'Office', 'Room', 'OFF'),
     DRAWSEL + ' 0I@0+28"Office" 1L@150+28"Room" 2L@230+20"OFF"'),
    ('INVWIN6', 'LOCS', rowln(TREE, '', *TREE_A1),
     rowln(TREE, '', '    Box B2', 'Box', 'B2')),
    ('INVWIN6', 'LOCSBACK', rowln(TREE, 'SELECTED', *TREE_A1),
     DRAWSEL + ' 0I@14+40"Shelf A1" 1L@150+24"Shelf" 2L@230+13"A1"'),
    # The recent service records.
    ('INVWIN2', 'RECENT',
     wlist(SVC, 'Rec', 'Item', 'Date', 'Type', 'Cost', 'Summary'),
     HEAD + ' 0R@28+20"Rec" 1R@83+20"Item" 2L@110+23"Date" 3L@175+24"Type"'
     ' 4R@237+21"Cost" 5L@265+43"Summary"'),
    ('INVWIN2', 'RECENT', rowln(SVC, 'SELECTED', '1', '1', *SVC_RP),
     DRAWSEL + ' 0R@42+6"1" 1R@97+6"1" 2L@110+54"2026-10-01" 3L@175+15"RP"'
     ' 5L@265+133"Recapped the power supply"'),
    # One item's service records, without the item.
    ('INVWIN3', 'RECORDS',
     wlist(SVCI, 'Rec', 'Date', 'Type', 'Cost', 'Summary'),
     HEAD + ' 0R@28+20"Rec" 1L@55+23"Date" 2L@120+24"Type" 3R@182+21"Cost"'
     ' 4L@210+43"Summary"'),
    ('INVWIN3', 'RECORDS', rowln(SVCI, 'SELECTED', '1', *SVC_RP),
     DRAWSEL + ' 0R@42+6"1" 1L@55+54"2026-10-01" 2L@120+15"RP"'
     ' 4L@210+133"Recapped the power supply"'),
    # The loans of every item, open: a use's holder its host, without a
    # mark or dates; the rows after it whole.
    ('INVWINVC', 'OPEN', LOAN_HDR, LOAN_HEAD),
    ('INVWINVC', 'OPEN',
     rowln(LOAN, 'SELECTED', '', '3', 'In use', '3', '1', 'With item 1',
           '', '', 'Hercules', 'Graphics Card'),
     DRAWSEL + ' 1R@57+6"3" 2L@70+29"In use" 3R@167+6"3" 4R@207+6"1"'
     ' 5L@220+53"With item 1" 8L@450+42"Hercules"'
     ' 9L@550+67"Graphics Card"'),
    ('INVWINVC', 'OPEN',
     rowln(LOAN, '', '!', '2', 'Reserved', '2', '1', 'Club show',
           '2020-01-10', '', 'Tandon', 'TM100-2'),
     rowln(LOAN, '', '!', '1', 'On loan', '1', '1', 'Ann Smith',
           '2020-02-01', '', 'IBM', '5150')),
    # Every loan, ended ones too: the same columns, a released hold's
    # end date under Ended.
    ('INVWINVA', 'ALL', LOAN_HDR, LOAN_HEAD),
    ('INVWINVA', 'ALL',
     rowln(LOAN, 'SELECTED', '', '1', 'Reserved', '2', '1', 'Club show',
           '', '2026-10-02', 'Tandon', 'TM100-2'),
     DRAWSEL + ' 1R@57+6"1" 2L@70+46"Reserved" 3R@167+6"2" 4R@207+6"1"'
     ' 5L@220+49"Club show" 7L@385+54"2026-10-02" 8L@450+37"Tandon"'
     ' 9L@550+43"TM100-2"'),
    # An item's own loans, without the item, maker and model.
    ('INVWINLB', 'LENT',
     wlist(LOANI, '!', 'Loan', 'Kind', 'Qty', 'Holder', 'Due', 'Ended'),
     HEAD + ' 0L@0+3"!" 1R@39+24"Loan" 2L@70+21"Kind" 3R@142+16"Qty"'
     ' 4L@165+31"Holder" 5L@265+20"Due" 6L@330+31"Ended"'),
    ('INVWINLB', 'LENT',
     rowln(LOANI, 'SELECTED', '!', '1', 'On loan', '1', 'Carol King',
           '2020-02-01'),
     DRAWSEL + ' 0L@0+3"!" 1R@57+6"1" 2L@70+37"On loan" 3R@152+6"1"'
     ' 4L@165+48"Carol King" 5L@265+54"2020-02-01"'),
    # The users: their header, Level right-aligned over the digits, and
    # the rows.
    ('INVWINUA', 'USERS',
     wlist(USER, 'User', 'Full name', 'Group', 'Level', 'Status', 'Chg',
           'Last login'),
     HEAD + ' 0L@0+22"User" 1L@67+45"Full name" 2L@187+29"Group"'
     ' 3R@256+26"Level" 4L@289+30"Status" 5L@329+19"Chg"'
     ' 6L@359+45"Last login"'),
    ('INVWINUA', 'USERS',
     rowln(USER, 'SELECTED', 'ADMIN', 'System Administrator', 'ADMIN', '3',
           'A', 'F', '2026-10-08'),
     DRAWSEL + ' 0L@0+35"ADMIN" 1L@67+97"System Administrator"'
     ' 2L@187+35"ADMIN" 3R@276+6"3" 4L@289+7"A" 5L@329+6"F"'
     ' 6L@359+54"2026-10-08"'),
    ('INVWINUA', 'USERS',
     rowln(USER, '', 'CLERK', 'Inventory Clerk', '', '2', 'A', 'F',
           '2026-10-08'),
     rowln(USER, '', 'NEWBIE', 'New User', '', '1', 'A', 'T')),
    # Item 1's audit log, written by the seeding's station SEED: no
    # leading blank; the time with its seconds.
    ('INVWINUC', 'AUDIT',
     wlist(AUDIT, *AUDIT_WORDS), AUDIT_HEAD),
    ('INVWINUC', 'AUDIT',
     AUDIT_SEL, DRAW_AUDIT),
    ('INVWINUC', 'AUDIT', DRAW_AUDIT,
     rowln(AUDIT, '', *(AUDIT_ROW + ('U', 'NOTES', '', 'MEMO')))),
    # A category renamed while the item list shows: its rows name it
    # anew.
    ('INVWINHS', 'RENAMED',
     rowln(ITEM, 'SELECTED', '1', 'SYS Machines', *ITEM_SYS[2:]),
     DRAWSEL + ' 0R@42+6"1" 1L@55+70"SYS Machines"' + DRAW_ITEM),
    # The audit log scrolled by Right, Right, Left, Left, Left (held at
    # 0) and Right ten times (held at the bar's max); the header and
    # rows unscrolled at 0.
    ('INVWINHS', 'AUDITALL', AUDIT_ALL, 23),
    ('INVWINHS', 'AUDIT', AUDIT_BAR % 0, AUDIT_HEAD),
    ('INVWINHS', 'AUDIT', AUDIT_SEL, DRAW_AUDIT),
    ('INVWINHS', 'RIGHT1', AUDIT_BAR % 7, None),
    ('INVWINHS', 'RIGHT1', AUDIT_SEL, DRAW_AUDIT7),
    ('INVWINHS', 'RIGHT2', AUDIT_BAR % 14, None),
    ('INVWINHS', 'RIGHT2', AUDIT_SEL, DRAW_AUDIT14),
    ('INVWINHS', 'LEFT1', AUDIT_BAR % 7, None),
    ('INVWINHS', 'LEFT1', AUDIT_SEL, DRAW_AUDIT7),
    ('INVWINHS', 'LEFT0', AUDIT_BAR % 0, AUDIT_HEAD),
    ('INVWINHS', 'LEFT0', AUDIT_SEL, DRAW_AUDIT),
    ('INVWINHS', 'RIGHTEND', AUDIT_BAR % 111, None),
    # TEST/WIN/INTLDE.INI's [intl]: item 2's form in edit mode, its
    # read-only dates d.M.yyyy (on the Acquisition page, not shown), its
    # ID and editable quantity as they are.
    ('INVWINDE', 'ITEMEDIT', ' Static 65535 "Created" HIDDEN',
     ' Edit 120 "8.10.2026" HIDDEN'),
    ('INVWINDE', 'ITEMEDIT', ' Static 65535 "Updated" HIDDEN',
     ' Edit 121 "8.10.2026" HIDDEN'),
    ('INVWINDE', 'ITEMEDIT', ' Static 65535 "Item ID"', ' Edit 100 "2"'),
    ('INVWINDE', 'ITEMEDIT', ' Static 65535 "&Quantity"', ' Edit 110 "2"'),
    # Item 1's service records: the date d.M.yyyy, 48 pixels; the cost
    # grouped, 48 pixels, filling its room 155 to 203.
    ('INVWINDE', 'RECORDS',
     wlist(SVCI, 'Rec', 'Date', 'Type', 'Cost', 'Summary'),
     HEAD + ' 0R@28+20"Rec" 1L@55+23"Date" 2L@120+24"Type" 3R@182+21"Cost"'
     ' 4L@210+43"Summary"'),
    ('INVWINDE', 'RECORDS', rowln(SVCI, 'SELECTED', '1', *SVC_DE1),
     DRAWSEL + DRAW_SVCI_DE1),
    ('INVWINDE', 'ADDED', rowln(SVCI, 'SELECTED', '1', *SVC_DE1),
     DRAWSEL + DRAW_SVCI_DE1),
    ('INVWINDE', 'ADDED', DRAWSEL + DRAW_SVCI_DE1,
     rowln(SVCI, '', '2', *SVC_DE2)),
    # The new record in view mode, its values statics, its labels
    # without letters: the date and the cost so, the record and item
    # numbers as they are.
    ('INVWINDE', 'SHOWN', ' Static 65535 "Record"', ' Static 100 "2"'),
    ('INVWINDE', 'SHOWN', ' Static 65535 "Item"', ' Static 101 "1"'),
    ('INVWINDE', 'SHOWN', ' Static 65535 "Date"',
     ' Static 102 "5.10.2026"'),
    ('INVWINDE', 'SHOWN', ' Static 65535 "Cost"',
     ' Static 105 "12.345,67"'),
    # In edit mode the date and the cost as typed, the record read-only.
    ('INVWINDE', 'EDIT', ' Static 65535 "Record"', ' Edit 100 "2"'),
    ('INVWINDE', 'EDIT', ' Static 65535 "&Date"',
     ' Edit 102 "2026-10-05" FOCUS'),
    ('INVWINDE', 'EDIT', ' Static 65535 "&Cost"', ' Edit 105 "12345.67"'),
    # The recent service records: the cost's piece fills its room, 210
    # to 258.
    ('INVWINDE', 'RECENT',
     wlist(SVC, 'Rec', 'Item', 'Date', 'Type', 'Cost', 'Summary'),
     HEAD + ' 0R@28+20"Rec" 1R@83+20"Item" 2L@110+23"Date"'
     ' 3L@175+24"Type" 4R@237+21"Cost" 5L@265+43"Summary"'),
    ('INVWINDE', 'RECENT', rowln(SVC, 'SELECTED', '2', '1', *SVC_DE2),
     DRAWSEL + ' 0R@42+6"2" 1R@97+6"1" 2L@110+48"5.10.2026"'
     ' 3L@175+15"RP" 4R@210+48"12.345,67"'
     ' 5L@265+115"Cleaned the drive heads"'),
    ('INVWINDE', 'RECENT',
     DRAWSEL + ' 0R@42+6"2" 1R@97+6"1" 2L@110+48"5.10.2026"'
     ' 3L@175+15"RP" 4R@210+48"12.345,67"'
     ' 5L@265+115"Cleaned the drive heads"',
     rowln(SVC, '', '1', '1', *SVC_DE1)),
    # Item 1001: its ID as it is, its quantity grouped, in its row
    # (Qty's room 375 to 408) and in view mode, with its dates and price
    # on the Acquisition page, not shown; the strip after the identity
    # row the focus.
    ('INVWINDE', 'ITEMEND',
     rowln(ITEM, 'SELECTED', '1001', 'DRV Drives', 'Shugart', 'SA400', '?',
           '1.234', 'Box B2'), DRAW_ITEM1001),
    ('INVWINDE', 'ITEMVIEW', ' Static 65535 "Item ID"',
     ' Static 100 "1001"'),
    ('INVWINDE', 'ITEMVIEW', ' Edit 106 "SA400"',
     ' WTAB 95 "&Overview|&Technical|&Acquisition|&Notes" CUR 0 FOCUS'),
    ('INVWINDE', 'ITEMVIEW', ' Static 65535 "Quantity"',
     ' Static 110 "1.234"'),
    ('INVWINDE', 'ITEMVIEW', ' Static 65535 "Date acquired" HIDDEN',
     ' Static 115 "1.1.2020" HIDDEN'),
    ('INVWINDE', 'ITEMVIEW', ' Static 65535 "Price paid" HIDDEN',
     ' Static 117 "1.234,56" HIDDEN'),
    ('INVWINDE', 'ITEMVIEW', ' Static 65535 "Created" HIDDEN',
     ' Static 120 "8.10.2026" HIDDEN'),
    # The open loan: its item's ID as it is, its quantity grouped, its
    # due date; every row: the ended reservation's end date after it.
    ('INVWINDE', 'LOANS', LOAN_HDR, LOAN_HEAD),
    ('INVWINDE', 'LOANS', LOAN_DE, DRAW_LOAN_DE),
    ('INVWINDE', 'LOANALL', LOAN_DE, DRAW_LOAN_DE),
    ('INVWINDE', 'LOANALL', DRAW_LOAN_DE,
     rowln(LOAN, '', '', '2', 'Reserved', '2', '1', 'Club show', '',
           '10.1.2020', 'Tandon', 'TM100-2')),
    # The users: CLERK's last login, at the seeding's SEED_NOW.
    ('INVWINDE', 'USERS',
     rowln(USER, '', 'CLERK', 'Inventory Clerk', '', '2', 'A', 'F',
           '8.10.2026'),
     rowln(USER, '', 'NEWBIE', 'New User', '', '1', 'A', 'T')),
    # Item 1001's audit log: the dates so, the record and the values as
    # they are, 1200 too; ON_LOAN, 51 pixels, within the Field room's
    # 75.
    ('INVWINDE', 'AUDIT', wlist(AUDIT, *AUDIT_WORDS), AUDIT_HEAD),
    ('INVWINDE', 'AUDIT',
     rowln(AUDIT, 'SELECTED', *(AUDIT_DE + ('I', 'ITEM_ID', '', '1001'))),
     DRAW_AUDIT_DE),
    ('INVWINDE', 'AUDIT', DRAW_AUDIT_DE,
     rowln(AUDIT, '', *(AUDIT_DE + ('O', 'ON_LOAN', '0', '1200')))),
    # The empty list's text, drawn whole from the left.
    ('INVWINV', 'EMPTY', '   ROW "No items"',
     '    DRAW 000000/FFFFFF 0I@0+41"No items"'),
    ('INVWINSA', 'NONE', '   ROW "No items found"',
     '    DRAW 000000/FFFFFF 0I@0+71"No items found"'),
    # A location type code of eight W, 88 pixels in a room of 60: drawn
    # from the column's left edge and its row CUT; the other rows and
    # the header whole.
    ('INVWINKC', 'WIDE', CODE_HDR, CODE_HEAD),
    ('INVWINKC', 'WIDE', '  ROW "ROOM     Room                  1"',
     '   DRAW 000000/FFFFFF 0L@0+33"ROOM" 1L@67+28"Room" 2R@234+6"1"'),
    ('INVWINKC', 'WIDE',
     '  ROW "WWWWWWWW Wide                  9" SELECTED CUT',
     '   DRAW FFFFFF/000000 0L@0+88"WWWWWWWW" 1L@67+25"Wide"'
     ' 2R@234+6"9"'),
    # Six W, 66 pixels: past its room's edge at 60, short of the next
    # column at 67, so only the gap would hold it: still CUT.
    ('INVWINKC', 'GAP',
     '  ROW "WWWWWW   Wide                 10" SELECTED CUT',
     '   DRAW FFFFFF/000000 0L@0+66"WWWWWW" 1L@67+25"Wide"'
     ' 2R@228+12"10"'),
    # INVWINCW: the item list laid out in the stored widths, past its box
    # by 715 - 630; its first row selected.
    ('INVWINCW', 'ITEMS', header(ITEM, *ITEM_WORDS) + ' HBAR 0 85',
     CW_ITEMHEAD),
    ('INVWINCW', 'ITEMS', rowln(ITEM, 'SELECTED', *ITEM_SYS), CW_SYS),
    ('INVWINCW', 'BYLOC', header(ITEM, *ITEM_WORDS) + ' HBAR 0 85',
     CW_ITEMHEAD),
    # The serial number order: ID its own 55, Maker held to 15, its
    # header word drawn from its room's left edge and CUT.
    ('INVWINCW', 'BYSER', header(SER, *SER_WORDS),
     HEAD + ' 0R@37+11"ID" 1L@55+30"Maker" 2L@70+29"Model"'
     ' 3L@170+64"Serial number" 4L@270+41"Location" CUT'),
    # The item pick list: the first four item columns in the stored
    # widths, its dialog wide enough for them, no bar.
    ('INVWINCW', 'ITEMPICK', ' WLIST 1025 "%s"' % row(ITEM, *ITEM_WORDS[:4]),
     HEAD + ' 0R@82+11"ID" 1L@100+42"Category" 2L@180+30"Maker"'
     ' 3L@380+29"Model"'),
    ('INVWINCW', 'ITEMPICK', rowln(ITEM, 'SELECTED', *ITEM_DRV),
     DRAWSEL + ' 0R@87+6"2" 1L@100+56"DRV Drives" 2L@180+37"Tandon"'
     ' 3L@380+43"TM100-2"'),
    # The split: the tree as wide as its Name and Type in the stored
    # widths, its row past its box by 370 - 277; the contents by 715 -
    # 347.
    ('INVWINCW', 'LOCS', header(TREE, *TREE_WORDS) + ' HBAR 0 93',
     CW_TREEHEAD),
    ('INVWINCW', 'LOCS', ' WSPLIT 220 "" AT 279 LIMITS 40 588', None),
    ('INVWINCW', 'LOCS',
     ' WLIST 221 "%s" HBAR 0 368' % row(ITEM, *ITEM_WORDS), CW_ITEMHEAD),
    ('INVWINCW', 'LOCS', rowln(TREE, 'SELECTED', 'Office', 'Room', 'OFF'),
     DRAWSEL + ' 0I@0+28"Office" 1L@180+28"Room" 2L@260+20"OFF"'),
    # The location pick list: Name and Type, its dialog wide enough.
    ('INVWINCW', 'MOVEPICK', ' WLIST 1025 "%s"' % row(TREE, *TREE_WORDS[:2]),
     HEAD + ' 0I@0+28"Name" 1L@180+24"Type"'),
)

# (golden, snapshot, a list window's line, the COLS line after its HEAD)
COLS_CASES = (
    # The item list by ID: ID the order in use, Maker and Location the
    # other orders' buttons; each Items menu order moves the arrow.
    ('INVWIN', 'ADMIN', wlist(ITEM, *ITEM_WORDS),
     cols('0A@0+55', '1F@55+80', '2B@135+100', '3F@235+100', '4F@335+40',
          '5F@375+40', '6B@415+100', '7F@515+55')),
    ('INVWINC', 'BYLOC', wlist(ITEM, *ITEM_WORDS),
     cols('0B@0+55', '1F@55+80', '2B@135+100', '3F@235+100', '4F@335+40',
          '5F@375+40', '6A@415+100', '7F@515+55')),
    ('INVWINC', 'BYMFR', wlist(ITEM, *ITEM_WORDS),
     cols('0B@0+55', '1F@55+80', '2A@135+100', '3F@235+100', '4F@335+40',
          '5F@375+40', '6B@415+100', '7F@515+55')),
    ('INVWINC', 'BYSER', wlist(SER, *SER_WORDS),
     cols('0B@0+55', '1B@55+100', '2F@155+100', '3A@255+100',
          '4B@355+100')),
    # Lists without orders: flat headings.
    ('INVWINBA', 'CATS', wlist(CAT, 'Code', 'Name', 'Order'),
     cols('0F@0+45', '1F@45+100', '2F@145+40')),
    ('INVWIN6', 'LOCS', wlist(TREE, *TREE_WORDS),
     cols('0F@0+150', '1F@150+80', '2F@230+55', '3F@285+55')),
    ('INVWINSP', 'LOCS',
     ' WLIST 221 "%s" HBAR 0 193' % row(ITEM, *ITEM_WORDS),
     cols('0F@0+55', '1F@55+80', '2F@135+100', '3F@235+100', '4F@335+40',
          '5F@375+40', '6F@415+100', '7F@515+55')),
    ('INVWINOB', 'MOVETO', ' WLIST 1025 "%s"' % row(TREE, *TREE_WORDS[:2]),
     cols('0F@0+150', '1F@150+80')),
    ('INVWINPA', 'PICKIN', ' WLIST 1025 "%s"' % row(ITEM, *ITEM_WORDS[:4]),
     cols('0F@0+55', '1F@55+80', '2F@135+100', '3F@235+100')),
    # The audit log scrolled a cell: every slot 7 pixels left.
    ('INVWINHS', 'RIGHT1', AUDIT_BAR % 7,
     cols('0F@-7+65', '1F@58+50', '2F@108+67', '3F@175+70', '4F@245+45',
          '5F@290+55', '6F@345+15', '7F@360+82', '8F@442+150',
          '9F@592+150')),
    # INVWINCW's stored widths, in every order of the item list, the
    # split's panes and the pick lists.
    ('INVWINCW', 'ITEMS', header(ITEM, *ITEM_WORDS) + ' HBAR 0 85',
     cw_items('A', 'B', 'B')),
    ('INVWINCW', 'BYLOC', header(ITEM, *ITEM_WORDS) + ' HBAR 0 85',
     cw_items('B', 'B', 'A')),
    ('INVWINCW', 'BYSER', header(SER, *SER_WORDS),
     cols('0B@0+55', '1B@55+15', '2F@70+100', '3A@170+100', '4B@270+100')),
    ('INVWINCW', 'ITEMPICK', ' WLIST 1025 "%s"' % row(ITEM, *ITEM_WORDS[:4]),
     cols('0F@0+100', '1F@100+80', '2F@180+200', '3F@380+100')),
    ('INVWINCW', 'LOCS', header(TREE, *TREE_WORDS) + ' HBAR 0 93',
     cols('0F@0+180', '1F@180+80', '2F@260+55', '3F@315+55')),
    ('INVWINCW', 'LOCS',
     ' WLIST 221 "%s" HBAR 0 368' % row(ITEM, *ITEM_WORDS),
     cw_items('F', 'F', 'F')),
    ('INVWINCW', 'MOVEPICK', ' WLIST 1025 "%s"' % row(TREE, *TREE_WORDS[:2]),
     cols('0F@0+180', '1F@180+80')),
)


def snaps(name):
    """The snapshots of golden name, by title: each a list of its lines,
    or None when the golden cannot be read."""
    try:
        with open(os.path.join(GOLD, name + '.SNP'), 'rb') as f:
            text = f.read().decode('latin-1')
    except OSError:
        return None
    out = {}
    cur = None
    for line in text.split('\r\n'):
        if line.startswith('=SNAP '):
            cur = out.setdefault(line[6:], [])
        elif line.startswith('='):
            cur = None
        elif cur is not None:
            cur.append(line)
    return out


def find(lines, line):
    """The index of line in lines, or -1; a line ending in ANYBAR is
    found as itself without it, or with an unscrolled bar."""
    if not line.endswith(ANYBAR):
        return lines.index(line) if line in lines else -1
    base = line[:-len(ANYBAR)]
    bar = base + ' HBAR 0 '
    for i, got in enumerate(lines):
        if got == base or (got.startswith(bar) and got[len(bar):].isdigit()):
            return i
    return -1


def rows(lines, i):
    """The ROW lines of the window of lines[i]: its box's, two deeper,
    up to the next line no deeper than it."""
    depth = len(lines[i]) - len(lines[i].lstrip(' '))
    row = ' ' * (depth + 2) + 'ROW '
    n = 0
    for line in lines[i + 1:]:
        if len(line) - len(line.lstrip(' ')) <= depth:
            break
        n += line.startswith(row)
    return n


def check(case, cache):
    """None when case holds, else why not."""
    name, snap, line, want = case
    if name not in cache:
        cache[name] = snaps(name)
    if cache[name] is None:
        return '%s.SNP: cannot read it' % name
    lines = cache[name].get(snap)
    if lines is None:
        return '%s.SNP: no snapshot %s' % (name, snap)
    i = find(lines, line)
    if i < 0:
        return '%s.SNP %s: no line %s' % (name, snap, line)
    if want is None:
        return None
    if isinstance(want, int):
        got = rows(lines, i)
        if got != want:
            return '%s.SNP %s: under %s\n  want %d rows\n  got  %d' % (
                name, snap, line, want, got)
        return None
    got = lines[i + 1] if i + 1 < len(lines) else '(none)'
    if got != want:
        return '%s.SNP %s: after %s\n  want %s\n  got  %s' % (
            name, snap, line, want, got)
    return None


def check_cols(case, cache):
    """None when case of COLS_CASES holds, else why not."""
    name, snap, line, want = case
    why = check((name, snap, line, None), cache)
    if why:
        return why
    lines = cache[name][snap]
    i = find(lines, line)
    got = lines[i + 2] if i + 2 < len(lines) else '(none)'
    if not lines[i + 1].startswith('  HEAD ') or got != want:
        return '%s.SNP %s: after the HEAD of %s\n  want %s\n  got  %s' % (
            name, snap, line, want, got)
    return None


def main():
    cache = {}
    bad = [why for why in (check(c, cache) for c in CASES) if why]
    bad += [why for why in (check_cols(c, cache) for c in COLS_CASES) if why]
    for why in bad:
        print('colchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('colchk: %d cases ok' % (len(CASES) + len(COLS_CASES)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
