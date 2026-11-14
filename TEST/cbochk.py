"""cbochk.py - INVWIN's drop-down lists in its snapshots, against rows
worked out apart from the program.

usage: python3 -I TEST/cbochk.py

The form dialog (WFORM.H) shows a code field of a code list, a category
or a location type, and a choice with names as a drop-down list: a
ComboBox line, its text the selected row's ("" for none or the blank
row), and one deeper a ROW line for each row in the list's order, the
selected one SELECTED.  A row is the code or word padded to the field's
width, two blanks and its name, or the code alone when the dialog's row
of the list would pass WF_MAXW with the names; a field that is not
FF_REQ has a blank first row.  Each combo box adds a top-level ComboLBox line, hidden while
its list is not dropped.  A dropped list holds the mouse; it is a
top-level window, whose capture the snapshot leaves out, so a frame
with one list dropped shows one ComboLBox line visible and no window
marked CAPTURE.  A drop-down list that cannot be changed is a
read-only edit control holding its row instead in edit and new mode,
and in view mode a static of the same ID holding it, its label without
a letter.

The rows come from the seeded data (TEST/KEYS/SEED.REQ): the code lists
of SRC/ENGINE/SCHTAB.H in their order, and the names of the choices in
SRC/CLIENT/SCRSRCH.MSG.  The widths: a category LINK_CATLEN 4, a
location type LINK_LTYPELEN 8, an item's status and works state 1, its
condition 3, the search's works filter SCR_WORKSW 3 (LINK.H, SCRSFORM.H).

Each case names a golden of TEST/GOLDEN/WIN and a snapshot in it, and
either a run of lines that must stand together in it, a line and the
times it must occur, or a state word (Word) and the times a line must
end with it or hold it before another word.  A golden or snapshot
missing, a run not found or a wrong count fails.  Exit status 1 if any
case fails.
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_SPEC = importlib.util.spec_from_file_location(
    'colchk', os.path.join(HERE, 'colchk.py'))
colchk = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(colchk)


def entry(width, code, name):
    """A row: code padded to the field's width, two blanks, its name."""
    return '%s  %s' % (code.ljust(width), name)


CATS = [entry(4, c, n) for c, n in (
    ('SYS', 'Systems'), ('MOBO', 'Motherboards'), ('CPU', 'Processors'),
    ('CARD', 'Expansion cards'), ('DRV', 'Drives'),
    ('PERI', 'Peripherals'), ('SW', 'Software'), ('MAN', 'Manuals'),
    ('CBL', 'Cables'), ('PART', 'Parts'))]
LTYPES = [entry(8, c, n) for c, n in (
    ('ROOM', 'Room'), ('SHELF', 'Shelf'), ('RACK', 'Rack'),
    ('CABINET', 'Cabinet'), ('DRAWER', 'Drawer'), ('BOX', 'Box'),
    ('BIN', 'Bin'), ('BUCKET', 'Bucket'))]
STATUS = [entry(1, 'A', 'Active'), entry(1, 'D', 'Disposed of')]
COND = [''] + [entry(3, c, n) for c, n in (
    ('NEW', 'New'), ('EXC', 'Excellent'), ('GD', 'Good'), ('FR', 'Fair'),
    ('PR', 'Poor'))]
WORKS = [entry(1, c, n) for c, n in (
    ('?', 'Not tested'), ('Y', 'Yes'), ('I', 'Intermittently'),
    ('N', 'No'))]
WFILT = [''] + [entry(3, c, n) for c, n in (
    ('?', 'Not tested'), ('Y', 'Yes'), ('I', 'Intermittently'),
    ('N', 'No'), ('Y?I', 'Not known broken'))]
LBOX = 'ComboLBox 1000 "" HIDDEN'
DROPPED = 'ComboLBox 1000 ""'


class Word(str):
    """A state word of a snapshot line (TDRV.H), counted as one."""


CAPTURE = Word('CAPTURE')


def dropped(name, snap, lists):
    """The cases of a frame of golden name with one of its lists
    dropped: that list shown, the others hidden, no capture marked."""
    return [(name, snap, DROPPED, 1), (name, snap, LBOX, lists - 1),
            (name, snap, CAPTURE, 0)]


def codes(rows):
    """The codes of rows, each a row without its name."""
    return [r.split(' ', 1)[0] for r in rows]


def combo(label, cid, rows, sel, after, focus=False):
    """The run of a labelled combo box: its label, its line, its rows,
    row sel selected (an index of rows), and the next control's line."""
    return ((' Static 65535 "%s"' % label,
             ' ComboBox %d "%s"%s' % (cid, rows[sel],
                                      ' FOCUS' if focus else '')) +
            tuple('  ROW "%s"%s' % (r, ' SELECTED' if i == sel else '')
                  for i, r in enumerate(rows)) + (after,))


def shown(label, cid, text):
    """A labelled value in view mode: a static showing text."""
    return (' Static 65535 "%s"' % label, ' Static %d "%s"' % (cid, text))


# Item 1 of the seed: SYS, status A, condition GD, works Y, at OFF.  Its
# Overview page holds Category, Status, Type, Condition and Works in that
# order; T and O are its strip's letters, so Type and Condition take
# others in edit mode; in view mode no label has a letter.
I_DLG = '#32770 "Item 1 - Edit" ACTIVE'
I_ST = ' Static 65535 "&Status"'
I_TYPE = ' Static 65535 "T&ype"'
I_WORKS = ' Static 65535 "&Works"'
I_QTY = ' Static 65535 "&Quantity"'


def item_edit(cat, cond, focus):
    """Item 1's four drop-down lists in edit mode, category row cat and
    condition row cond, focus 'cat' or 'cond' naming the focus."""
    return (
        combo('&Category', 101, CATS, cat, I_ST, focus == 'cat'),
        combo('&Status', 102, STATUS, 0, I_TYPE),
        combo('Cond&ition', 104, COND, cond, I_WORKS, focus == 'cond'),
        combo('&Works', 109, WORKS, 1, I_QTY))


def item_view(cat, cond):
    """Item 1's rows in view mode, each in a static."""
    return (
        shown('Category', 101, CATS[cat]),
        shown('Status', 102, STATUS[0]),
        shown('Condition', 104, COND[cond]),
        shown('Works', 109, WORKS[1]))


# Indexes of the rows the script reaches.
SYS, CPU, CARD, DRV = 0, 2, 3, 4
GD = 3
BOX, BIN, BUCKET = 5, 6, 7

LOC_IN = ' Static 65535 "&Inside"'

CASES = (
    # Item 1 in view mode: each list's row in a static, no combo box.
    [('WFORMTW', 'VIEW', run) for run in item_view(SYS, GD)] + [
        ('WFORMTW', 'VIEW', LBOX, 0)] +
    # In edit mode: four drop-down lists, the category the focus, no
    # pick button after them; Location and Tags keep theirs.
    [('WFORMTW', 'EDIT', run) for run in item_edit(SYS, GD, 'cat')] + [
        ('WFORMTW', 'EDIT', (' Edit 112 "OFF"', ' Button 136 "..."')),
        ('WFORMTW', 'EDIT', (' Edit 119 ""', ' Button 143 "..."')),
        ('WFORMTW', 'EDIT', LBOX, 4),
        # C moves to the next row starting with it, CPU, then CARD.
        ('WFORMTW', 'TYPEC1', combo('&Category', 101, CATS, CPU, I_ST,
                                    True)),
        ('WFORMTW', 'TYPEC2', combo('&Category', 101, CATS, CARD, I_ST,
                                    True)),
        # Esc on the dropped list keeps the row it dropped on; Enter takes
        # the row moved to and leaves the dialog open; each list closed.
        ('WFORMTW', 'DROPESC', combo('&Category', 101, CATS, CARD, I_ST,
                                     True)),
        ('WFORMTW', 'DROPESC', I_DLG, 1),
        ('WFORMTW', 'DROPESC', LBOX, 4),
        ('WFORMTW', 'DROPOK', combo('&Category', 101, CATS, DRV, I_ST,
                                    True)),
        ('WFORMTW', 'DROPOK', I_DLG, 1),
        ('WFORMTW', 'DROPOK', LBOX, 4),
        ('WFORMTW', 'DROPUP', combo('&Category', 101, CATS, CARD, I_ST,
                                    True)),
        # The blank row chosen for the condition.
        ('WFORMTW', 'CONDNONE', combo('Cond&ition', 104, COND, 0, I_WORKS,
                                      True))] +
    # Saved and read again: CARD and no condition, in view mode, then
    # in edit mode once more.
    [('WFORMTW', 'SAVED', run) for run in item_view(CARD, 0)] +
    [('WFORMTW', 'REOPEN', run) for run in item_edit(CARD, 0, 'cat')] + [
        # The search: Category, Status and Works with a blank first row,
        # selected; Location and Tags keep their pick buttons.  Status
        # takes the A that Include inner locations leaves.  The rows of
        # Category and of Status would pass WF_MAXW with their names, so
        # their lists show the codes alone; Works' row fits with them.
        ('INVWINCO', 'SEARCH',
         combo('&Category', 102, [''] + codes(CATS), 0,
               ' Static 65535 "&Location"')),
        ('INVWINCO', 'SEARCH',
         combo('St&atus', 106, [''] + codes(STATUS), 0,
               ' Static 65535 "A&vailable"')),
        ('INVWINCO', 'SEARCH',
         combo('&Works', 109, WFILT, 0, ' Static 99 ""')),
        ('INVWINCO', 'SEARCH', (' Edit 103 ""', ' Button 127 "..."')),
        ('INVWINCO', 'SEARCH', (' Edit 108 ""', ' Button 132 "..."')),
        ('INVWINCO', 'SEARCH', LBOX, 3),
        # Box B2's form: its type a drop-down list, the type's name field
        # and its label hidden, then its memo on the Notes page not shown.
        ('INVWINCO', 'LOCEDIT',
         combo('&Type', 101, LTYPES, BOX, LOC_IN)),
        ('INVWINCO', 'LOCEDIT',
         (' Static 65535 "" HIDDEN', ' Edit 107 "Box" HIDDEN',
          ' Edit 106 "" HIDDEN', ' Static 99 ""')),
        ('INVWINCO', 'LOCEDIT', LBOX, 1),
        # B from BOX: BIN, then BUCKET.
        ('INVWINCO', 'LOCB1',
         combo('&Type', 101, LTYPES, BIN, LOC_IN, True)),
        ('INVWINCO', 'LOCB2',
         combo('&Type', 101, LTYPES, BUCKET, LOC_IN, True)),
        # Saved: the tree's row names the new type, and the form opened
        # again selects it.
        ('INVWINCO', 'LOCSAVED',
         (colchk.rowln(colchk.TREE, 'SELECTED', '    Box B2', 'Bucket',
                       'B2'),)),
        ('INVWINCO', 'LOCAGAIN',
         combo('&Type', 101, LTYPES, BUCKET, LOC_IN)),
        ('INVWINCO', 'LOCAGAIN',
         (' Static 65535 "" HIDDEN', ' Edit 107 "Bucket" HIDDEN')),
        # Its list dropped and moved up to BIN, Esc closes it on Bucket,
        # and the hidden name field follows.
        ('INVWINCO', 'LOCESC',
         combo('&Type', 101, LTYPES, BUCKET, LOC_IN, True)),
        ('INVWINCO', 'LOCESC',
         (' Static 65535 "" HIDDEN', ' Edit 107 "Bucket" HIDDEN')),
        ('INVWINCO', 'LOCESC', LBOX, 1)] +
    # Frames with a list dropped by F4: the item form's category and
    # works, the search's category and a location's type.
    dropped('INVWINPK', 'CAT', 4) + dropped('INVWINPL', 'SRCHCAT', 3) +
    dropped('INVWINKB', 'TYPEPICK', 1) + dropped('INVWINTA', 'WORKS', 4))


def holds(lines, run):
    """Whether the lines of run stand together in lines."""
    n = len(run)
    return any(tuple(lines[i:i + n]) == run
               for i in range(len(lines) - n + 1))


def check(case, cache):
    """None when case holds, else why not."""
    name, snap, what = case[:3]
    if name not in cache:
        cache[name] = colchk.snaps(name)
    if cache[name] is None:
        return '%s.SNP: cannot read it' % name
    lines = cache[name].get(snap)
    if lines is None:
        return '%s.SNP: no snapshot %s' % (name, snap)
    if isinstance(what, Word):
        got = sum(1 for ln in lines
                  if ln.endswith(' ' + what) or (' %s ' % what) in ln)
        if got != case[3]:
            return '%s.SNP %s: %s\n  want %d lines, got %d' % (
                name, snap, what, case[3], got)
        return None
    if len(case) == 4:
        got = lines.count(what)
        if got != case[3]:
            return '%s.SNP %s: %s\n  want %d times, got %d' % (
                name, snap, what, case[3], got)
        return None
    if not holds(lines, what):
        return '%s.SNP %s: no run\n  %s' % (name, snap, '\n  '.join(what))
    return None


def main():
    cache = {}
    bad = [why for why in (check(c, cache) for c in CASES) if why]
    for why in bad:
        print('cbochk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('cbochk: %d cases ok' % len(CASES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
