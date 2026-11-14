"""labchk.py - the labels of INVWIN's form dialogs in its snapshots and
those of the form dialog's test, against texts worked out apart from the
program.

usage: python3 -I TEST/labchk.py

A form dialog (WFORM.H) shows each field's label as a Static line just
before the field's control: the field's Windows string, the shared
message's number plus WF_S_LABEL (WFSTR.H), else the shared message
itself, each '&' of it doubled.  Sixteen shared labels have a Windows
string: ID reads "Item ID", Descr "Description", Parent "Part of",
Price "Price paid", Acquired "Date acquired", Source "Acquired from",
Value "Current value", a loan's Out "Date out", Due "Due date" and
Returned "Date returned", the search's and the reports' All below
"Include inner locations", a report's Order "Sort by", a code list's
Order "Sort order", the CSV forms' Field names "Field names in first
line" and the settings' RTS/CTS "RTS/CTS flow control"; every other
label is the shared one ("Serial", a loan's "ID", the test's menu entry
"Browse by &&ID").

A field that can be changed shows an access letter, an '&' before it:
the first digit or letter not yet used that starts a word, else the
first not yet used.  Every '&' letter of the dialog's tab strip (WTAB)
and of its buttons is used before any; the Edit button has none.  In
edit and new mode a field that cannot be changed shows none and uses
none; in view mode no field can be changed, so no label shows a letter,
on every form, and each value shown as text is a static (SS_NOPREFIX)
whose '&' is no letter.
A form with a memo and no pages of its own has two, "&Record"
and "&Notes", the memo alone on Notes without a label; the item form's
are "&Overview", "&Technical", "&Acquisition" and "&Notes", its Item
ID, Maker and Model above them.  The caption of a form that OK saves
says " - Edit" after the record's name in edit mode, and the item
form's name is its ID alone.

Each case names a golden of TEST/GOLDEN/WIN, a snapshot in it, the
caption of a dialog (#32770) in it and the labels of its fields in
order: the dialog's Static lines of ID 65535, those empty or HIDDEN
(those of the pages not shown) left out.  A golden, snapshot or dialog
missing, or other labels, fails.  Then every dialog of every golden
shows each access letter once among its labels, buttons and tab strips
(the message line, Static WF_MSGID, and the values, Statics of a
field's control ID from WF_ID0, left out), and no title of the test's
frame says KEYS, CLIP, VCLIP or LOOK, the failures of its own checks of
the letters, of the labels' widths, of the fixed-format values' widths
and of the dialogs' grey 3D look (TEST/WFORMT.C).  Exit status 1 if
any fails.
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_SPEC = importlib.util.spec_from_file_location(
    'colchk', os.path.join(HERE, 'colchk.py'))
colchk = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(colchk)

MSGID = 99                      # WF_MSGID, the message line
ID0 = 100                       # WF_ID0, field 0's control
MAXFIELDS = 24                  # FORM_MAXFIELDS of FMODEL.H

# The item form in edit and new mode as it opens, its identity row and
# Overview page: ID and Parent cannot be changed; O and T are the
# strip's.
ITEM = ('Item ID', '&Maker', 'Mo&del', '&Category', '&Status', 'T&ype',
        'Cond&ition', '&Works', '&Quantity', '&Location', 'Part of',
        'Ta&gs')
CODE = ('&Name', '&Sort order')
USER = ('&Full name', '&Group', '&Level', '&New password',
        '&Repeat password')
# The item form in view mode as it opens: no letters.
ITEMV = ('Item ID', 'Maker', 'Model', 'Category', 'Status', 'Type',
         'Condition', 'Works', 'Quantity', 'Location', 'Part of', 'Tags')

CASES = (
    ('INVWINH', 'NEW', 'New item', ITEM),
    ('INVWINIA', 'EDIT', 'Item 1 - Edit', ITEM),
    ('WFORMTX', 'NEW', 'New item', ITEM),
    ('WFORMTX', 'EDIT', 'Item 1 - Edit', ITEM),
    # View mode: no field has a letter, whatever form.
    ('WFORMTX', 'VIEW', 'Item 1', ITEMV),
    ('INVWINIA', 'VIEW', 'Item 1', ITEMV),
    ('INVWIN2', 'VIEW', 'Service record 1 Recapped the power supply',
     ('Record', 'Item', 'Date', 'Type', 'Summary', 'Cost', 'Done by')),
    ('INVWINUA', 'VIEW', 'User ADMIN',
     ('Status', 'User name', 'Full name', 'Group', 'Level', 'New password',
      'Repeat password')),
    ('INVWINLB', 'VIEW', 'Loan 1',
     ('ID', 'Item', 'Quantity', 'Borrower', 'Contact', 'Date out',
      'Due date', 'Date returned', 'Notes')),
    ('WFORMTA', 'VIEW', 'Search items',
     ('Text', 'Location', 'Quantity', 'Price paid', 'Date acquired',
      'Overdue', 'Status', 'Condition', 'Item ID', 'Browse by &&ID',
      'New password')),
    # Loans, reservations and in-use records keep their notes in a
    # character field, not a memo: one page, no strip, Notes a label.  A
    # new loan: its quantity and date out too, Date out taking D.
    ('INVWINLA', 'NEW', 'Lend',
     ('ID', 'Item', '&Quantity', '&Borrower', '&Contact', '&Date out',
      'D&ue date', 'Date returned', '&Notes')),
    ('INVWINLB', 'EDIT', 'Loan 1 - Edit',
     ('ID', 'Item', 'Quantity', '&Borrower', '&Contact', 'Date out',
      '&Due date', 'Date returned', '&Notes')),
    ('INVWINVA', 'NEW', 'Reserve',
     ('ID', 'Item', '&Quantity', '&Reserved for', '&For item',
      '&Contact', 'Fr&om', '&Until', 'Released', '&Notes')),
    ('INVWINVB', 'NEW', 'Use with',
     ('ID', 'Item', '&Quantity', '&In use with', '&With item',
      '&Contact', '&Since', 'Released', '&Notes')),
    ('INVWIN3', 'NEW', 'New service record',
     ('Record', 'Item', '&Date', '&Type', '&Summary', '&Cost',
      'Done &by')),
    ('INVWIN2', 'EDIT', 'Service record 1 Recapped the power supply - Edit',
     ('Record', 'Item', '&Date', '&Type', '&Summary', '&Cost',
      'Done &by')),
    ('INVWIN7', 'NEW', 'New location',
     ('N&ame', '&Type', '&Inside', '&Code', 'Ca&pacity')),
    ('INVWIN6', 'EDIT', 'Location Box B2 - Edit',
     ('N&ame', '&Type', '&Inside', '&Code', 'Ca&pacity')),
    ('INVWINKB', 'NEW', 'New category', ('&Code',) + CODE),
    ('INVWINKB', 'EDIT', 'Category MISC - Edit', ('Code',) + CODE),
    ('INVWINKB', 'LTNEW', 'New location type', ('&Code',) + CODE),
    ('INVWINKB', 'LTEDIT', 'Location type SHELF - Edit',
     ('Code',) + CODE),
    ('INVWINUA', 'NEW', 'New user', ('&User name',) + USER),
    ('INVWINUB', 'NODISABL', 'User ADMIN - Edit',
     ('&Status', 'User name') + USER),
    ('INVWIN', 'FIND', 'Search items',
     ('&Text', '&Serial', '&Category', '&Location',
      '&Include inner locations', 'St&atus', 'A&vailable', 'Ta&gs',
      '&Works')),
    ('INVWINRA', 'LISTING', 'Item listing',
     ('&Category', '&Location', '&Include inner locations', '&Sort by',
      'S&tatus', '&Works', 'T&ags', '&Output', '&File')),
    ('INVWINRA', 'BACK', 'Valuation',
     ('&Category', '&Location', '&Include inner locations', '&Works',
      '&Tags', '&Output', '&File')),
    ('INVWINRB', 'LOANS', 'Loans and holds',
     ('&Overdue', '&As of', 'O&utput', '&File')),
    ('INVWINPL', 'LABEL', 'Item labels',
     ('&Category', '&Location', '&Include inner locations', '&Works',
      '&Tags', 'It&em ID', '&Output', '&File')),
    ('INVWINPL', 'BIN', 'Bin labels',
     ('&Location', '&Include inner locations', '&Output', '&File')),
    ('INVWINUC', 'FILTER', 'Audit log', ('&Table', '&Record', '&Station')),
    ('INVWINCA', 'FORM', 'Export to a CSV file',
     ('&Table', '&File', 'Field &names in first line')),
    ('INVWINCC', 'FORM', 'Import a CSV file',
     ('&Table', '&File', 'Field &names in first line')),
    ('INVWINHA', 'SETTINGS', 'Settings',
     ('&Data directory', '&Station name', 'Default &user',
      '&Item list order', '&Baud rate', '&RTS/CTS flow control',
      'Settings file', 'Program')),
    ('INVWINUC', 'DIRASK', 'Back up the data', ('&Directory',)),
    ('INVWINLA', 'ASKRET', 'Return loan 2?',
     ('&Date (YYYY-MM-DD, empty for today)',)),
    ('INVWINPA', 'ASKPART', 'Install an item in item 1',
     ('&Item ID, or text to find it',)),
    ('INVWINPA', 'ATHOME', 'Move item 1 to', ('&Location',)),
    ('INVWINWA', 'DIALOG', 'Split item 2', ('&Quantity',)),
    # The test's form of every type in edit mode, on Record: ID and
    # Serial (FF_NEWONLY) cannot be changed there; N is the strip's.
    ('WFORMTA', 'OPEN', 'Search items',
     ('&Text', '&Location', '&Quantity', '&Price paid', '&Date acquired',
      '&Overdue', '&Status', '&Condition', 'Item ID', 'Browse by &&ID',
      'N&ew password')),
)

CHILD = re.compile(r' (\S+) (\d+) "(.*)"(.*)$')


def letter(text):
    """The access letter of text, upper case, or None: the character
    after its first '&' that does not double another."""
    i = 0
    while i < len(text) - 1:
        if text[i] == '&':
            if text[i + 1] != '&':
                return text[i + 1].upper()
            i += 1
        i += 1
    return None


def all_letters(text):
    """Every access letter of text, a tab strip's, upper case: the
    character after each '&' that does not double another."""
    out = []
    while True:
        k = letter(text)
        if k is None:
            return out
        out.append(k)
        i = 0
        while text[i] != '&' or text[i + 1] == '&':
            i += 2 if text[i] == '&' else 1
        text = text[i + 2:]


def dialogs(lines):
    """Each dialog of a snapshot: its line and its children's lines,
    those one blank deep."""
    out = []
    for line in lines:
        if line.startswith('#32770 '):
            out.append((line, []))
        elif not line.startswith(' '):
            out.append((None, []))
        elif out and not line.startswith('  '):
            out[-1][1].append(line)
    return [d for d in out if d[0] is not None]


def labels(children):
    """The labels shown in a dialog's children."""
    out = []
    for line in children:
        m = CHILD.match(line)
        if (m and m.group(1) == 'Static' and m.group(2) == '65535' and
                m.group(3) and 'HIDDEN' not in m.group(4)):
            out.append(m.group(3))
    return tuple(out)


def check(case, cache):
    """None when case holds, else why not."""
    name, snap, caption, want = case
    if name not in cache:
        cache[name] = colchk.snaps(name)
    if cache[name] is None:
        return '%s.SNP: cannot read it' % name
    lines = cache[name].get(snap)
    if lines is None:
        return '%s.SNP: no snapshot %s' % (name, snap)
    head = '#32770 "%s"' % caption
    for line, children in dialogs(lines):
        if line == head or line.startswith(head + ' '):
            got = labels(children)
            if got == want:
                return None
            return '%s.SNP %s %s\n  want %s\n  got  %s' % (
                name, snap, caption, ' | '.join(want), ' | '.join(got))
    return '%s.SNP %s: no dialog "%s"' % (name, snap, caption)


def value(cid):
    """Whether a Static of ID cid is the message line or a field's
    value, which shows no access letter."""
    return cid == MSGID or ID0 <= cid < ID0 + MAXFIELDS


def letters(name, snaps):
    """Why the dialogs of golden name (its snapshots snaps) fail the
    letters, one line each."""
    bad = []
    for snap, lines in sorted(snaps.items()):
        if name.startswith('WFORMT'):
            bad += ['%s.SNP %s: %s' % (name, snap, ln) for ln in lines
                    if ln.startswith('WFORMT "') and
                    (' KEYS ' in ln or ' CLIP' in ln or ' VCLIP' in ln or
                     ' LOOK' in ln)]
        for line, children in dialogs(lines):
            seen = {}
            for child in children:
                m = CHILD.match(child)
                if not m or m.group(1) not in ('Static', 'Button', 'WTAB') or \
                        (m.group(1) == 'Static' and value(int(m.group(2)))):
                    continue
                ks = (all_letters(m.group(3)) if m.group(1) == 'WTAB' else
                      [letter(m.group(3))])
                for k in ks:
                    if k is not None and k in seen:
                        bad.append('%s.SNP %s %s: %s twice, "%s" and "%s"' % (
                            name, snap, line, k, seen[k], m.group(3)))
                    elif k is not None:
                        seen[k] = m.group(3)
    return bad


def main():
    cache = {}
    bad = [why for why in (check(c, cache) for c in CASES) if why]
    names = sorted(f[:-4] for f in os.listdir(colchk.GOLD)
                   if f.endswith('.SNP'))
    for name in names:
        snaps = cache.get(name) or colchk.snaps(name)
        if snaps is not None:
            bad += letters(name, snaps)
    for why in bad:
        print('labchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('labchk: %d cases ok, letters of %d goldens ok' % (
        len(CASES), len(names)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
