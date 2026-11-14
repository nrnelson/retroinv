"""headchk.py - the headings of the list windows in the Windows programs'
snapshots: their COLS lines in every golden, and WLISTTV's heading
clicks and divider drags against a model worked out apart from the
program.

usage: python3 -I TEST/headchk.py

The test driver (SRC/WIN/TDRV.H) writes a list window's (WLIST.H) header
as its HEAD line, then a COLS line, one deeper than the window: for each
column of its table a space and kS@x+w, k the column, S its heading's
state (F flat, B a button naming one of the source's orders, A the
order in use's, with the arrow, P a button held down), x its slot's
left edge less the columns' scroll and w its width.  In every golden of
TEST/GOLDEN/WIN:

  every list window's line is followed by its HEAD line, drawn in
  000000/C0C0C0 (COLOR_BTNTEXT on COLOR_BTNFACE), then its COLS line;
  a dialog's header, a Static (WDLG.H), is followed by the same HEAD
  line and a COLS line of flat headings only;
  no other line is a COLS line;
  the slots start at the scroll's negative (HBAR pos, 0 without the
  bar) and follow each other without a gap, each at least a pixel;
  a list holds at most one A and at most one P, and one with buttons
  (B or P) holds an A or a P;
  each HEAD piece and each DRAW piece of the list's rows starts in its
  column's slot or past it, its column one of the table's; a HEAD
  piece but the last column's ends within its slot unless the line is
  CUT;
  P shows only in WLISTTV, the one script that holds a heading down;
  INVWINHA's settings save, TEST/GOLDEN/WIN/INV.CFG, holds no WINCOLS
  line: a table whose widths are all its own is not added.

TEST/WLISTTV.KEY, on the seed's three items, drives the list of
TEST/WLISTT.C, whose two orders' tables (ID: cells 8 4 15 15 5 5 10 6,
Qty right-aligned; SERIAL: 8 15 15 20 10) have no default widths, their
slots px(cells + 1) (WCOLS.H: px(n) = 15 * n / 2 with Helv 8's
tmAveCharWidth 5) until a width is stored.  ID's heading names order 0,
SERIAL's Serial number order 1, and the ID table's Location a third
order the source lacks, so it is flat.  The list's box is 570 pixels
wide (its window made wl_width wide for the ID table, the vertical bar
shown), so a row ending past 570 shows HBAR pos (row end - 570).

The model: a stored width n of column k, held to at least WCOL_MINW
(3), makes its slot 5 * n; a column without one is laid out from the
last column before it that has one (WCOLS.H).  A drag of column k's
divider by d pixels from its slot's left edge L stores round(d / 5) =
(d + 2) / 5, held to WCOL_MINW..WCOL_MAXW, the guide itself held to L
+ 15 .. L + 1275.  The keys of TEST/WLISTT.C post the mouse at the
header's middle: I at a divider (the slot's right edge, SHIFT-I two
pixels left of it, CTRL-I two right, both within WCOL_GRIP), M a click
on a heading's middle, K a move of WLT_STEP (20) pixels, CTRL-K and
CTRL-SHIFT-K WLT_FAR (2000) pixels, Q the release.  STEPS gives for
each snapshot the frame title's what and move count, the order, the
two tables' stored widths, the scroll and the column held down; its
COLS, HEAD and list window lines are worked out from them, the list
window's line with CAPTURE while a heading or a divider is held.  Z's
PIX a b c: GetPixel of the screen at the last point's x, at the header's
middle (a) and on the box's bottom border (b), and at the press's x at
the header's middle (c): the face C0C0C0 and the border 000000, under
the inverted guide 808080 (the VGA's light gray inverted) or 3F3F3F
and FFFFFF.

A golden missing, a line not found or a line different fails.  Exit
status 1 if any check fails.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GOLD = os.path.join(HERE, 'GOLDEN', 'WIN')

HEADCOL = '000000/C0C0C0'
WLISTLN = re.compile(r'^( +)WLIST \d+ "(.*)"(?: HBAR (\d+) (\d+))?( .*)?$')
STATICLN = re.compile(r'^ +Static ')
COLSLN = re.compile(r'^COLS((?: \d+[FBAP]@-?\d+\+\d+)+)$')
SLOT = re.compile(r'(\d+)([FBAP])@(-?\d+)\+(\d+)')
PIECE = re.compile(r' (\d+)[LRI]@(-?\d+)\+(\d+)"')

# WLISTT's tables: cells, right-aligned columns, the order each heading
# names (None for none), the header's words and their widths (HEAD).
WLT_TABS = (
    {'cells': (8, 4, 15, 15, 5, 5, 10, 6), 'right': (5,),
     'ord': (0, None, None, None, None, None, 2, None),
     'words': ('ID', 'Cat', 'Maker', 'Model', 'Works', 'Qty', 'Location',
               'Status'),
     'w': (11, 16, 30, 29, 31, 16, 41, 30),
     'hdr': '      ID Cat  Maker           Model           Works   Qty'
            ' Location   Status'},
    {'cells': (8, 15, 15, 20, 10), 'right': (),
     'ord': (0, None, None, 1, None),
     'words': ('ID', 'Maker', 'Model', 'Serial number', 'Location'),
     'w': (11, 30, 29, 64, 41),
     'hdr': '      ID Maker           Model           Serial number'
            '        Location'},
)
NORDERS = 2
AVEW = 5                        # tmAveCharWidth of Helv 8
CELL = 7                        # px(1)
MINW = 3                        # WCOL_MINW
BOXW = 570                      # WLISTT's box


def px(n):
    """n cells in pixels."""
    return n * AVEW * 3 // 2


def layout(cells, widths):
    """The slots' left edges x[0..n] of a table of cells laid out with
    the stored widths (0 for the column's own)."""
    s = [0]
    for c in cells:
        s.append(s[-1] + c + 1)
    x = [0]
    j = 0
    for k in range(len(cells)):
        if widths[k]:
            x.append(x[k] + max(widths[k], MINW) * AVEW)
            j = k + 1
        else:
            x.append(x[j] + px(s[k + 1] - s[j]))
    return x


def drag(x, k, d):
    """The width stored by a drag of column k's divider to d pixels from
    its slot's left edge."""
    d = max(MINW * AVEW, min(255 * AVEW, d))
    return max(MINW, min(255, (d + AVEW // 2) // AVEW))


def lines_for(order, widths, hx, press, held):
    """The list window's line, its HEAD and its COLS in a snapshot, held
    1 while the window holds the mouse."""
    t = WLT_TABS[order]
    x = layout(t['cells'], widths)
    n = len(t['cells'])
    end = x[n]
    wl = ' WLIST 1 "%s"' % t['hdr']
    if end > BOXW:
        wl += ' HBAR %d %d' % (hx, end - BOXW)
    if held:
        wl += ' CAPTURE'
    head = []
    cut = 0
    for k in range(n):
        w = t['w'][k]
        room = x[k + 1] - CELL
        if k in t['right'] and w <= room - x[k]:
            px_ = room - w
        else:
            px_ = x[k]
        if k < n - 1 and px_ + w > room:
            cut = 1
        px_ += -hx + (1 if press == k else 0)
        head.append(' %d%s@%d+%d"%s"' % (
            k, 'R' if k in t['right'] else 'L', px_, w, t['words'][k]))
    slots = []
    for k in range(n):
        o = t['ord'][k]
        st = 'F' if o is None or o >= NORDERS else 'A' if o == order else 'B'
        if press == k and st != 'F':
            st = 'P'
        slots.append(' %d%s@%d+%d' % (k, st, x[k] - hx, x[k + 1] - x[k]))
    return (wl, '  HEAD ' + HEADCOL + ''.join(head) + (' CUT' if cut else ''),
            '  COLS' + ''.join(slots))


Z0 = [0] * 10                   # a row of the store, all own widths


def put(row, k, n):
    """row with column k's width n."""
    row = list(row)
    row[k] = n
    return row


# The widths the drags store, from the model (each drag's comment gives
# the arithmetic): ID's table (store row 1), SERIAL's (row 2).
_ID = WLT_TABS[0]['cells']
_SER = WLT_TABS[1]['cells']
# WIDEN0: column 0's divider 67 + 20: d 87 -> 17 (85).
ID1 = put(Z0, 0, drag(layout(_ID, Z0), 0, 87))
# WIDEN6: column 6, L 452, its divider 535 + 20: d 103 -> 21 (105).
ID2 = put(ID1, 6, drag(layout(_ID, ID1), 6, 103))
# MIN4: column 4, L 362, its divider 407 - 2000: held at L + 15 -> 3.
ID3 = put(ID2, 4, drag(layout(_ID, ID2), 4, 45 - 2000))
# MAX7: column 7, L 527, its divider 579 + 2000: held at L + 1275 -> 255.
ID4 = put(ID3, 7, drag(layout(_ID, ID3), 7, 52 + 2000))
# SCROLLED: scrolled 7, column 5, L 377, its divider 422 + 20: d 65 -> 13.
ID5 = put(ID4, 5, drag(layout(_ID, ID4), 5, 65))
# SERWIDE: SERIAL's column 3, L 307, pressed 2 left of its divider 465
# and moved 20: the grab kept, d 158 + 20 -> 36 (180), not 35.
SER1 = put(Z0, 3, drag(layout(_SER, Z0), 3, 178))
# SERWIDE2: column 2, L 187, pressed 2 right of its divider 307, in
# Serial number's heading, and moved 20: d 140 -> 28; no order switch.
SER2 = put(SER1, 2, drag(layout(_SER, SER1), 2, 140))

INV = ('808080', '3F3F3F')      # the face under the inverted guide
PIX_ON = ('PIX', INV, 'FFFFFF', INV)
PIX_MOVED = ('PIX', INV, 'FFFFFF', ('C0C0C0',))
PIX_OFF = ('PIX', ('C0C0C0',), '000000', ('C0C0C0',))

# (snapshot, what (a PIX tuple for Z's), moves, order, ID's widths,
# SERIAL's widths, scroll, column held down or None)
STEPS = (
    ('START', 'WLISTT', 1, 0, Z0, Z0, 0, None),
    ('STORE0', 'W 1 0', 1, 0, Z0, Z0, 0, None),
    ('COL4', 'COL 4', 1, 0, Z0, Z0, 0, None),
    # The guide at the press, then moved; erased by Esc, the Down key
    # during the drag dropped, the release after it doing nothing.
    ('GUIDE0', PIX_ON, 1, 0, Z0, Z0, 0, None),
    ('GUIDE1', PIX_MOVED, 1, 0, Z0, Z0, 0, None),
    ('UNGUIDE', PIX_OFF, 1, 0, Z0, Z0, 0, None),
    ('ESCUP', PIX_OFF, 1, 0, Z0, Z0, 0, None),
    # WM_CANCELMODE likewise.
    ('CANCEL', PIX_OFF, 1, 0, Z0, Z0, 0, None),
    ('CANCELUP', PIX_OFF, 1, 0, Z0, Z0, 0, None),
    # Drags: a note and the list laid out again, no move.
    ('WIDEN0', 'COLS 1', 1, 0, ID1, Z0, 0, None),
    ('STORE1', 'W 1 17', 1, 0, ID1, Z0, 0, None),
    # A release where the drag began: the width it has, no note.
    ('SAME0', 'W 1 17', 1, 0, ID1, Z0, 0, None),
    ('WIDEN6', 'COLS 1', 1, 0, ID2, Z0, 0, None),
    ('MIN4', 'COLS 1', 1, 0, ID3, Z0, 0, None),
    ('MAX7', 'COLS 1', 1, 0, ID4, Z0, 0, None),
    ('STORE2', 'W 1 17,0,0,0,3,0,21,255', 1, 0, ID4, Z0, 0, None),
    ('RIGHT', 'W 1 17,0,0,0,3,0,21,255', 1, 0, ID4, Z0, 7, None),
    ('SCROLLED', 'COLS 1', 1, 0, ID5, Z0, 7, None),
    # Clicks on a flat heading, on a heading tagged with an order the
    # source lacks, and on the order in use's: nothing.
    ('FLAT', 'COL 1', 1, 0, ID5, Z0, 0, None),
    ('NOORD', 'COL 6', 1, 0, ID5, Z0, 0, None),
    ('SAMEORD', 'COL 0', 1, 0, ID5, Z0, 0, None),
    # TAB, then a click on ID's button: one busy move each, the arrow
    # following; each table keeps its own widths.
    ('SERIAL', 'COL 0', 2, 1, ID5, Z0, 0, None),
    ('SERSTORE', 'W 2 0', 2, 1, ID5, Z0, 0, None),
    ('CLICKID', 'COL 0', 3, 0, ID5, Z0, 0, None),
    ('SERIAL2', 'COL 0', 4, 1, ID5, Z0, 0, None),
    # Held down, moved off, back on, released on it: the switch.
    ('PRESSED', 'COL 0', 4, 1, ID5, Z0, 0, 0),
    ('AWAY', 'COL 0', 4, 1, ID5, Z0, 0, None),
    ('BACKON', 'COL 0', 4, 1, ID5, Z0, 0, 0),
    ('BACKUP', 'COL 0', 5, 0, ID5, Z0, 0, None),
    # Released off it, cancelled by Esc, by WM_CANCELMODE: no switch.
    ('NOSWITCH', 'COL 0', 6, 1, ID5, Z0, 0, None),
    ('PESC', 'COL 0', 6, 1, ID5, Z0, 0, None),
    ('PESCUP', 'COL 0', 6, 1, ID5, Z0, 0, None),
    ('PCANCEL', 'COL 0', 6, 1, ID5, Z0, 0, None),
    # The order in use's button held down: P, released: nothing.
    ('PRESSCUR', 'COL 3', 6, 1, ID5, Z0, 0, 3),
    ('CURUP', 'COL 3', 6, 1, ID5, Z0, 0, None),
    ('SERWIDE', 'COLS 2', 6, 1, ID5, SER1, 0, None),
    ('SERWIDE2', 'COLS 2', 6, 1, ID5, SER2, 0, None),
    ('STORE3', 'W 2 0,0,28,36', 6, 1, ID5, SER2, 0, None),
    ('IDBACK', 'W 2 0,0,28,36', 7, 0, ID5, SER2, 0, None),
)
# The snapshots taken while a divider's drag or a heading's press is held
# but drawn raised; one with a column held down (P) holds the mouse too.
HELD = ('GUIDE0', 'GUIDE1', 'AWAY')
TITLE = re.compile(r'^WLISTT "(.*) #(\d+)(?: [^"]*)?" ')


def snaps(name):
    """The snapshots of golden name, by title, each a list of its lines
    (a snapshot written twice keeps the last), or None."""
    try:
        with open(os.path.join(GOLD, name), 'rb') as f:
            text = f.read().decode('latin-1')
    except OSError:
        return None
    out = {}
    cur = None
    for line in text.split('\r\n'):
        if line.startswith('=SNAP '):
            cur = out[line[6:]] = []
        elif line.startswith('='):
            cur = None
        elif cur is not None:
            cur.append(line)
    return out


def depth(line):
    """The blanks a line starts with."""
    return len(line) - len(line.lstrip(' '))


def list_bad(lines, i, where):
    """Why the list window of lines[i] breaks the rules, or []."""
    m = WLISTLN.match(lines[i])
    d = len(m.group(1))
    pos = int(m.group(3)) if m.group(3) else 0
    head = lines[i + 1] if i + 1 < len(lines) else ''
    cl = lines[i + 2] if i + 2 < len(lines) else ''
    if not head.startswith(' ' * (d + 1) + 'HEAD ' + HEADCOL) or \
            depth(head) != d + 1:
        return ['%s: no HEAD %s after %s' % (where, HEADCOL, lines[i])]
    c = COLSLN.match(cl[d + 1:]) if depth(cl) == d + 1 else None
    if not c:
        return ['%s: no COLS line after the HEAD of %s' % (where, lines[i])]
    slots = [(int(k), st, int(x), int(w)) for k, st, x, w in
             SLOT.findall(c.group(1))]
    bad = []
    at = -pos
    for j, (k, st, x, w) in enumerate(slots):
        if k != j or x != at or w < 1:
            bad.append('%s: slot %d of %s' % (where, j, cl))
            break
        at = x + w
    sts = [s[1] for s in slots]
    if sts.count('A') > 1 or sts.count('P') > 1 or (
            'B' in sts and 'A' not in sts and 'P' not in sts):
        bad.append('%s: states of %s' % (where, cl))
    n = len(slots)
    for k, x, w in ((int(k), int(x), int(w)) for k, x, w in
                    PIECE.findall(head)):
        if k >= n or x < slots[k][2] or (
                k < n - 1 and not head.endswith(' CUT') and
                x + w > slots[k][2] + slots[k][3]):
            bad.append('%s: piece %d of %s' % (where, k, head))
    for line in lines[i + 3:]:
        if depth(line) <= d:
            break
        if line.lstrip(' ').startswith('DRAW '):
            for k, x, w in ((int(k), int(x), int(w)) for k, x, w in
                            PIECE.findall(line)):
                if k >= n or x < slots[k][2]:
                    bad.append('%s: piece %d of %s' % (where, k, line))
    return bad


def static_bad(lines, i, where):
    """Why the Static of lines[i], when a HEAD line follows it, breaks
    the rules of a dialog's header, or []."""
    d = depth(lines[i])
    head = lines[i + 1] if i + 1 < len(lines) else ''
    if depth(head) != d + 1 or not head.lstrip(' ').startswith('HEAD '):
        return []
    cl = lines[i + 2] if i + 2 < len(lines) else ''
    c = COLSLN.match(cl[d + 1:]) if depth(cl) == d + 1 else None
    if not head.startswith(' ' * (d + 1) + 'HEAD ' + HEADCOL) or not c:
        return ['%s: no HEAD %s and COLS after %s' %
                (where, HEADCOL, lines[i])]
    at = 0
    for j, (k, st, x, w) in enumerate(SLOT.findall(c.group(1))):
        if int(k) != j or st != 'F' or int(x) != at or int(w) < 1:
            return ['%s: slot %d of %s' % (where, j, cl)]
        at = int(x) + int(w)
    return []


def golden_bad(name, snapsof):
    """Why golden name breaks the rules of every golden, or []."""
    bad = []
    for snap, lines in snapsof.items():
        where = '%s %s' % (name, snap)
        owned = set()
        for i, line in enumerate(lines):
            if WLISTLN.match(line):
                bad += list_bad(lines, i, where)
                owned.add(i + 2)
            elif STATICLN.match(line):
                bad += static_bad(lines, i, where)
                if i + 1 < len(lines) and \
                        lines[i + 1].lstrip(' ').startswith('HEAD '):
                    owned.add(i + 2)
        for i, line in enumerate(lines):
            if line.lstrip(' ').startswith('COLS ') and i not in owned:
                bad.append('%s: a COLS line not a header\'s: %s' %
                           (where, line))
            if re.match(r' *COLS .* \d+P@', line) and \
                    name != 'WLISTTV.SNP':
                bad.append('%s: a heading held down: %s' % (where, line))
    return bad


def step_bad(step, snapsof):
    """Why WLISTTV's snapshot of step differs from the model, or []."""
    snap, what, moves, order, id_w, ser_w, hx, press = step
    lines = snapsof.get(snap)
    if lines is None:
        return ['WLISTTV.SNP: no snapshot %s' % snap]
    bad = []
    tops = [ln for ln in lines if ln.startswith('WLISTT "')]
    m = TITLE.match(tops[0]) if tops else None
    if not m:
        return ['WLISTTV.SNP %s: no frame' % snap]
    got = m.group(1)
    if isinstance(what, tuple):
        parts = got.split(' ')
        ok = len(parts) == 4 and parts[0] == what[0] and \
            parts[1] in what[1] and parts[2] == what[2] and \
            parts[3] in what[3]
    else:
        ok = got == what
    if not ok or int(m.group(2)) != moves:
        bad.append('WLISTTV.SNP %s: title %s\n  want %s #%d' %
                   (snap, tops[0], what, moves))
    want = lines_for(order, id_w if order == 0 else ser_w, hx, press,
                     press is not None or snap in HELD)
    try:
        i = next(j for j, ln in enumerate(lines) if WLISTLN.match(ln))
    except StopIteration:
        return bad + ['WLISTTV.SNP %s: no list window' % snap]
    for k, w in enumerate(want):
        g = lines[i + k] if i + k < len(lines) else '(none)'
        if g != w:
            bad.append('WLISTTV.SNP %s:\n  want %s\n  got  %s' % (snap, w, g))
    return bad


def wlisttv_bad(snapsof):
    """Why WLISTTV's snapshots differ from STEPS, or []."""
    bad = []
    for step in STEPS:
        bad += step_bad(step, snapsof)
    # Esc after the drags reaches the owner, which closes the frame.
    if snapsof.get('CLOSED') != []:
        bad.append('WLISTTV.SNP CLOSED: the frame still shows')
    # Esc during a drag: the box's own, the selection kept.
    sel = [ln for ln in snapsof.get('UNGUIDE', []) if ln.startswith(
        '   ROW ') and ln.endswith(' SELECTED')]
    if len(sel) != 1 or not sel[0].startswith('   ROW "1 '):
        bad.append('WLISTTV.SNP UNGUIDE: the first row not selected')
    return bad


def cfg_bad():
    """Why INVWINHA's settings save holds a WINCOLS line, or []."""
    try:
        with open(os.path.join(GOLD, 'INV.CFG'), 'rb') as f:
            text = f.read().decode('latin-1')
    except OSError:
        return ['INV.CFG: cannot read it']
    return ['INV.CFG: %s' % ln for ln in text.split('\r\n')
            if ln.upper().startswith('WINCOLS.')]


def main():
    bad = []
    names = sorted(n for n in os.listdir(GOLD) if n.endswith('.SNP'))
    tv = None
    for name in names:
        s = snaps(name)
        bad += golden_bad(name, s)
        if name == 'WLISTTV.SNP':
            tv = s
    bad += wlisttv_bad(tv) if tv is not None else [
        'WLISTTV.SNP: cannot read it']
    bad += cfg_bad()
    for why in bad:
        print('headchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('headchk: %d goldens ok' % len(names))
    return 0


if __name__ == '__main__':
    sys.exit(main())
