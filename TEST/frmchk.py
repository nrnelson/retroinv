"""frmchk.py - the form dialog's pages, record buttons, buttons, captions,
reserved access letters and view-mode statics in the snapshots of its
test and of INVWIN, against lines worked out apart from the program.

usage: python3 -I TEST/frmchk.py

TEST/WFORMTX.KEY drives TEST/WFORMT.C over the seeded data (TEST/KEYS/
SEED.REQ), whose item 1 is an IBM 5150: category SYS, status A,
condition GD, works Y, at OFF in Office, serial 0012345, described "IBM
Personal Computer", nothing else set; WFORMT loads every item's CREATED
and UPDATED empty, and its item form's memo is empty and read-only.

The item form (WFORM.H, the pages of WROWS.H's wrow_item_pages) shows
ITEM_ID, MAKER and MODEL in an identity row above its tab strip, WTAB
95 "&Overview|&Technical|&Acquisition|&Notes", then each page's fields
in the table's order, page by page: Overview, Technical, Acquisition,
Notes, whose memo has no label.  The fields of the pages not shown
lack WS_VISIBLE (HIDDEN), their labels and pick buttons too.  Then the
record buttons, 148 to 151, the message line, Static 99, and the
dialog's buttons: in view mode Close (IDCANCEL, the default) and Edit,
without a letter; in edit and new mode OK (the default) and Cancel;
each set with &Help (94) last once the program has set the help (DEL),
as the item form names a topic.  Its caption is "Item 1" in view mode,
"Item 1 - Edit" in edit mode and "New item" in new mode.

Access letters: every letter of the strip's tabs and of the buttons is
reserved, so O, T, A, N and H are none of a label's; in edit and new
mode the labels of the fields that can be changed take theirs in the
order above.

View mode: no field can be changed, so no label shows a letter (a
label's own '&' stays doubled), and every value but a memo, a password,
a check box and free text that may be longer than its box is a Static
of the field's control ID (WF_ID0 + i), its text the read-only edit
control's of edit mode, as it is: no pick button, no Tab stop, never
DISABLED.  The memo and that free text (VEDITS: Maker, Model,
Description, the held line, Part of, Tags) stay read-only edit
controls, a password a disabled password edit, a check box a disabled
check box.  As the dialog opens the focus is the strip on a form of
pages, else Close; choosing a tab keeps it on the strip, and Tab goes
from the strip to the read-only edit controls of the page shown (Part
of and Tags on Overview, the memo on Notes), the record buttons, then
Close.

The form of fixed formats in edit mode (SHIFT-F5) and a returned loan
(CTRL-F6, its three dates on one row of INV's table, the return date
carried on to a line of its own) are checked by WFORMT's own VCLIP.

The form of every type (WFORMT.C's all_def) has no table and no topic:
in view mode Close alone.  The form of fixed formats (SHIFT-F6, fix_def)
is one page: its choice of sixteen W's, 99999, 999999.99, 1988-12-28
(the test machine's ISO dates, no thousands separator) and ID 88888888.

Each case names a snapshot of TEST/GOLDEN/WIN/WFORMTX.SNP, or a golden
and a snapshot in it as a pair, and what must hold in it:

  ('seq', snap, caption, lines)  the dialog of that caption has exactly
      these child lines, FOCUS included, in order, but its labels
      without text (Static 65535 ""), which carry no letter, and the
      rows of its drop-down lists, one deeper
  ('has', snap, caption, lines)  each of these lines is one of the
      dialog's children; a caption None is the snapshot's one dialog
  ('lacks', snap, caption, prefixes)  no child line starts with one of
      these
  ('labels', snap, caption, labels)  the texts of the dialog's labels
      (Static 65535) that are not empty, hidden ones too, in order
  ('frame', snap, prefix)  the frame's line, WFORMT's window, starts
      with prefix
  ('nodlg', snap)  the snapshot holds no dialog

A golden, snapshot, dialog or line missing, or one different, fails.
Exit status 1 if any case fails.
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

GOLDEN = 'WFORMTX'
LABEL = ' Static 65535 '
NOLABEL = (LABEL + '""', LABEL + '"" HIDDEN')
TABS = '&Overview|&Technical|&Acquisition|&Notes'
SAVEFIRST = 'Save with OK or leave with Cancel first'
NEGNUM = 'The number may not be negative'
VIEW, EDIT, NEW = 'view', 'edit', 'new'
TOP = None
ID0 = 100                       # WF_ID0, field 0's control
STRIP = 95                      # WF_STRIPID
CLOSE = 2                       # IDCANCEL, Close in view mode
MEMO = 122                      # the item form's memo

# The item form's fields in Z-order: control ID, page (TOP for the
# identity row), label in edit and new mode ('' for none), the class of
# its control there, its text for item 1 and for a new item, and its
# pick button's ID there, or None.
ITEM = (
    (100, TOP, 'Item ID', 'Edit', '1', '', None),
    (105, TOP, '&Maker', 'Edit', 'IBM', '', None),
    (106, TOP, 'Mo&del', 'Edit', '5150', '', None),
    (101, 0, '&Category', 'ComboBox', 'SYS   Systems', '', None),
    (102, 0, '&Status', 'ComboBox', 'A  Active', 'A  Active', None),
    (103, 0, 'T&ype', 'Edit', '', '', None),
    (104, 0, 'Cond&ition', 'ComboBox', 'GD   Good', '', None),
    (109, 0, '&Works', 'ComboBox', 'Y  Yes', '?  Not tested', None),
    (110, 0, '&Quantity', 'Edit', '1', '1', None),
    (111, 0, '', 'Edit', '', '', None),
    (112, 0, '&Location', 'Edit', 'OFF', '', 136),
    (113, 0, 'Part of', 'Edit', '0', '', None),
    (114, 0, '', 'Edit', 'Office', '(no location)', None),
    (119, 0, 'Ta&gs', 'Edit', '', '', 143),
    (107, 1, 'S&erial', 'Edit', '0012345', '', None),
    (108, 1, 'Desc&ription', 'Edit', 'IBM Personal Computer', '', None),
    (115, 2, 'Date acq&uired', 'Edit', '', '', None),
    (116, 2, 'Acquired &from', 'Edit', '', '', None),
    (117, 2, '&Price paid', 'Edit', '', '', None),
    (118, 2, 'Current &value', 'Edit', '', '', None),
    (120, 2, 'Created', 'Edit', '', '', None),
    (121, 2, 'Updated', 'Edit', '', '', None),
    (MEMO, 3, '', 'Edit', '', '', None),
)
ACTS = ('Parts', 'Service', 'Loans', 'Used with')
# The item form's controls that stay read-only edit controls in view
# mode: the memo, and free text that may be longer than its box (Maker,
# Model, Description, the held line, Part of, Tags).
VEDITS = (MEMO, 105, 106, 108, 111, 113, 119)
ACT0 = 148                      # WF_ACT0, record button 0


def line(cls, cid, text, hidden=False, focus=False, default=False,
         disabled=False, checked=False):
    """A child's line: ' Class id "text"', then its states in TDRV.H's
    order."""
    return ' %s %d "%s"%s%s%s%s%s' % (cls, cid, text,
                                      ' DISABLED' if disabled else '',
                                      ' HIDDEN' if hidden else '',
                                      ' FOCUS' if focus else '',
                                      ' CHECKED' if checked else '',
                                      ' DEFAULT' if default else '')


def plain(label):
    """label as view mode shows it: its access letter's '&' left out, a
    doubled '&' kept."""
    return re.sub('&(.)', lambda m: '&&' if m.group(1) == '&' else
                  m.group(1), label)


def closing(focus=None, edit=True, helped=False, default=True):
    """View mode's buttons: Close, Edit for a form that OK saves,
    &Help with the help set, the control of ID focus the focus; Close
    the default unless default is False."""
    out = [line('Button', CLOSE, 'Close', focus=focus == CLOSE,
                default=default)]
    if edit:
        out.append(line('Button', 98, 'Edit', focus=focus == 98))
    if helped:
        out.append(line('Button', 94, '&Help', focus=focus == 94))
    return tuple(out)


def statics(pairs, hidden=False, edits=()):
    """A view form's values from WF_ID0 on, each a (label, text) pair:
    its label, but an empty one, then its static, or its read-only edit
    control for an ID in edits (free text longer than its box)."""
    out = []
    for i, (lab, text) in enumerate(pairs):
        if lab:
            out.append(line('Static', 65535, lab, hidden))
        out.append(line('Edit' if ID0 + i in edits else 'Static', ID0 + i,
                        text, hidden))
    return tuple(out)


def item(mode, cur, focus, texts=None, msg='', helped=False):
    """The child lines of item 1's dialog (a new item's in NEW) in mode,
    page cur shown, the control of ID focus the focus (STRIP the strip),
    texts a dict of control IDs to the texts they hold instead, msg the
    message line, helped the Help button there."""
    out = []
    texts = texts or {}
    for cid, page, lab, cls, text, newtext, pick in ITEM:
        hidden = page is not TOP and page != cur
        if mode == VIEW:
            lab, cls = plain(lab), 'Edit' if cid in VEDITS else 'Static'
        if lab:
            out.append(line('Static', 65535, lab, hidden))
        text = texts.get(cid, newtext if mode == NEW else text)
        out.append(line(cls, cid, text, hidden, cid == focus))
        if pick is not None and mode != VIEW:
            out.append(line('Button', pick, '...', hidden))
        if cid == ITEM[2][0]:     # the strip after the identity row
            out.append(' WTAB %d "%s" CUR %d%s' % (
                STRIP, TABS, cur, ' FOCUS' if focus == STRIP else ''))
    # A push button of Windows 3.0 that gets the focus is the default
    # one while it has it, so a record button with the focus takes
    # DEFAULT from Close or OK.
    acted = ACT0 <= focus < ACT0 + len(ACTS)
    out += [line('Button', ACT0 + i, t, focus=ACT0 + i == focus,
                 default=ACT0 + i == focus)
            for i, t in enumerate(ACTS)]
    out.append(line('Static', 99, msg))
    if mode == VIEW:
        out += closing(focus, True, helped, not acted)
    else:
        out += [line('Button', 1, 'OK', default=not acted),
                line('Button', 2, 'Cancel')]
        if helped:
            out.append(line('Button', 94, '&Help'))
    return tuple(out)


# The form of every type in view mode: its fields' labels and classes
# (None for the memo, which has none), the check box and the password
# disabled.
ALLVIEW = (('Text', 'Static'), ('Location', 'Static'),
           ('Quantity', 'Static'), ('Price paid', 'Static'),
           ('Date acquired', 'Static'), ('Overdue', 'Button'),
           ('Status', 'Static'), ('Condition', 'Static'),
           ('Item ID', 'Static'), ('Browse by &&ID', 'Static'),
           ('New password', 'Edit'), (None, 'Edit'))


def all_view(texts, checked, cur=0, focus=STRIP):
    """The form of every type in view mode, its controls' texts texts,
    the check box checked, page cur shown (1 the memo's), the control of
    ID focus the focus."""
    out = [' WTAB %d "&Record|&Notes" CUR %d%s' % (
        STRIP, cur, ' FOCUS' if focus == STRIP else '')]
    for i, ((lab, cls), text) in enumerate(zip(ALLVIEW, texts)):
        hidden = (lab is None) != (cur == 1)
        if lab is not None:
            out.append(line('Static', 65535, lab, hidden))
        out.append(line(cls, ID0 + i, text, hidden, ID0 + i == focus,
                        disabled=cls == 'Button' or lab == 'New password',
                        checked=cls == 'Button' and checked))
    return tuple(out) + (line('Static', 99, ''),) + closing(focus, False)


VCAP = 'Item 1'
ECAP = 'Item 1 - Edit'
NCAP = 'New item'
ALLCAP = 'Search items'
HELP = ' Button 94 '

# The form of reserved letters (WFORMT.C's res_def): F4 shows its 4, its
# F taken and the Edit button having no letter; Holder's H is free until
# the Help button reserves it; R and N are the strip's.
RES = ('&File', 'F&4', '&Holder', 'R&ecord', 'New &password')
RESHELP = ('&File', 'F&4', 'H&older', 'R&ecord', 'New &password')
RECNOTES = ' WTAB 95 "&Record|&Notes" CUR %d'

# The form of every type (WFORMT.C's all_def) in edit mode: N is the
# strip's, so New password takes its e; the memo, NOTES (111), has no
# label and shows WFORMT's g_memo, its CR LF as two '?'.
MEMOTEXT = 'First line??Caf\x82 au lait'
ALLMEMO = ' Edit 111 "%s"' % MEMOTEXT
ALL = ('&Text', '&Location', '&Quantity', '&Price paid', '&Date acquired',
       '&Overdue', '&Status', '&Condition', 'Item ID', 'Browse by &&ID',
       'N&ew password')
# WFORMTA's values as typed, and WFORMTX's '&' typed into Text, its
# condition NEW, its memo WFORMT's.
TA = ['Mixed Case', 'B2X', '12', '1234.50', '1983-11-01', '', 'A', 'NEW',
      'R-1', '', '*******', MEMOTEXT + '??Third line']
TA2 = TA[:9] + ['SN-9'] + TA[10:]
AMP = ['R&R Repairs', '', '', '', '', '', '', 'NEW', 'R-1', '', '',
       MEMOTEXT]
# The form of fixed formats, each value at its widest.
FIX = statics((('Status', 'W' * 16), ('Quantity', '99999'),
               ('Price paid', '999999.99'), ('Date acquired', '1988-12-28'),
               ('Item ID', '88888888'))) + (line('Static', 99, ''),) + \
    closing(CLOSE, False)

# INVWIN's item 1, its service record 1, user ADMIN and loan 1 in view
# mode, as their goldens' scripts open them, Help set.
IA = {111: 'Available 1', 113: '', 120: '2026-10-08', 121: '2026-10-08',
      MEMO: 'Bought at a swap meet in 1991.??Two full-height drives, '
            'keyboard.'}
SVC = ((' WTAB 95 "&Record|&Notes" CUR 0 FOCUS',) +
       statics((('Record', '1'), ('Item', '1'), ('Date', '2026-10-01'),
                ('Type', 'RP'), ('Summary', 'Recapped the power supply'),
                ('Cost', ''), ('Done by', '')), edits=(104,)) +
       (line('Edit', 107, 'Two caps.??Tested OK.', True),
        line('Static', 99, '')) + closing(None, True, True))
USER = (statics((('Status', 'A'), ('User name', 'ADMIN'),
                 ('Full name', 'System Administrator'), ('Group', 'ADMIN'),
                 ('Level', '3'))) +
        (line('Static', 65535, 'New password'),
         line('Edit', 105, '', disabled=True),
         line('Static', 65535, 'Repeat password'),
         line('Edit', 106, '', disabled=True),
         line('Static', 99, '')) + closing(CLOSE, True, True))
LOAN = (statics((('ID', '1'), ('Item', '1'), ('Quantity', '1'),
                 ('', 'IBM 5150'), ('Borrower', 'Carol King'),
                 ('Contact', ''), ('Date out', '2020-01-01'),
                 ('Due date', '2020-02-01'), ('Date returned', ''),
                 ('Notes', '')), edits=(109,)) +
        (line('Static', 99, ''),) + closing(CLOSE, True, True))

CASES = (
    ('nodlg', 'START'),
    ('labels', 'RES', None, RES),
    ('has', 'RES', None, (RECNOTES % 0, line('Button', 1, 'OK', default=True),
                          line('Button', 2, 'Cancel'))),
    ('lacks', 'RES', None, (HELP,)),
    # View mode: each page in turn, the others hidden, the focus staying
    # on the strip.
    ('seq', 'VIEW', VCAP, item(VIEW, 0, STRIP)),
    ('seq', 'VIEWE', VCAP, item(VIEW, 0, STRIP)),
    ('seq', 'VCTAB', VCAP, item(VIEW, 1, STRIP)),
    ('seq', 'VCTAB3', VCAP, item(VIEW, 3, STRIP)),
    ('seq', 'VWRAP', VCAP, item(VIEW, 0, STRIP)),
    ('seq', 'VCSTAB', VCAP, item(VIEW, 3, STRIP)),
    ('seq', 'VCSTAB2', VCAP, item(VIEW, 2, STRIP)),
    ('seq', 'VALTT', VCAP, item(VIEW, 1, STRIP)),
    # A click on the second record button ends the dialog with WF_ACT + 1.
    ('nodlg', 'VACT'),
    ('frame', 'VACT', 'WFORMT "VIEW 8 1 #'),
    # A click on tab 1: Technical, the strip the focus.
    ('seq', 'VCLICK', VCAP, item(VIEW, 1, STRIP)),
    # Edit mode: Category the focus; Acquisition by its letter, the focus
    # on its first field; back to Overview, the focus on Category.
    ('seq', 'EDIT', ECAP, item(EDIT, 0, 101)),
    ('seq', 'EALTA', ECAP, item(EDIT, 2, 115)),
    ('seq', 'EALTO', ECAP, item(EDIT, 0, 101, {117: '-5'})),
    # OK's check fails on the price: its page shown, the focus on it.
    ('seq', 'ECHECK', ECAP, item(EDIT, 2, 117, {117: '-5'}, NEGNUM)),
    # A click on a record button: the focus on it, save first.
    ('seq', 'ESAVE', ECAP, item(EDIT, 2, ACT0, {117: '-5'}, SAVEFIRST)),
    ('nodlg', 'EDONE'),
    ('frame', 'EDONE', 'WFORMT "EDIT 0 1 #'),
    # New mode.
    ('seq', 'NEW', NCAP, item(NEW, 0, 101)),
    ('seq', 'NEWACT', NCAP, item(NEW, 0, ACT0, msg=SAVEFIRST)),
    ('nodlg', 'NEWDONE'),
    # The help set: a Help button in each mode, which calls the help
    # with the item form's topic, over the dialog.
    ('seq', 'VHELP', VCAP, item(VIEW, 0, STRIP, helped=True)),
    ('has', 'HELPED', VCAP, (line('Button', 94, '&Help'),)),
    ('frame', 'HELPED', 'WFORMT "HELP 1 ITEM #'),
    ('seq', 'EHELP', ECAP, item(EDIT, 0, 101, helped=True)),
    ('labels', 'RESHELP', None, RESHELP),
    ('has', 'RESHELP', None, (RECNOTES % 0, line('Button', 94, '&Help'))),
    # The form of every type: no topic, so no Help button; Record and
    # Notes, the memo alone on Notes.
    ('labels', 'ALL', ALLCAP, ALL),
    ('has', 'ALL', ALLCAP, (RECNOTES % 0, ALLMEMO + ' HIDDEN')),
    ('lacks', 'ALL', ALLCAP, (HELP,)),
    ('labels', 'ALLNOTES', ALLCAP, ALL),
    ('has', 'ALLNOTES', ALLCAP, (RECNOTES % 1, ' Edit 100 "" HIDDEN',
                                 ALLMEMO + ' FOCUS')),
    ('nodlg', 'DONE'),
    # CTRL+E keeps the page shown: Technical, the focus on Serial; the
    # password form's Notes, the focus on its memo.
    ('seq', 'EKEEP', ECAP, item(EDIT, 1, 107, helped=True)),
    ('has', 'MKEEP', None, (RECNOTES % 1, ' Edit 101 "First line??Caf\x82'
                            ' au lait" FOCUS')),
    # A tab's letter alone on a control that takes no characters shows
    # its page, Close keeping the focus; a hidden field's letter alone on
    # the strip in edit mode shows the field's page and focuses it.
    ('seq', 'PLAINT', VCAP, item(VIEW, 1, CLOSE, helped=True)),
    ('seq', 'PLAINP', ECAP, item(EDIT, 2, 117, helped=True)),
    ('nodlg', 'LAST'),
    # Tab in view mode: strip, record buttons, Close; on Notes the memo.
    ('seq', 'VTAB', VCAP, item(VIEW, 0, ACT0, helped=True)),
    ('seq', 'VTABC', VCAP, item(VIEW, 0, CLOSE, helped=True)),
    ('seq', 'VTABN', VCAP, item(VIEW, 3, MEMO, helped=True)),
    # A field's edit-mode letter, with Alt or alone, moves nothing.
    ('seq', 'VALTC', VCAP, item(VIEW, 0, STRIP, helped=True)),
    # A '&' in a value shows as it is, no letter.
    ('seq', 'VAMP', ALLCAP, all_view(AMP, False)),
    # One page: the focus on Close; the widest fixed formats.
    ('seq', 'VFIX', ALLCAP, FIX),
    ('nodlg', 'END'),
    # The form of every type in WFORMTA: view mode, then its Notes page
    # with the memo the focus, then after new mode, and Alt with a
    # field's edit-mode letter moving nothing.
    ('seq', ('WFORMTA', 'VIEW'), ALLCAP, all_view(TA, True)),
    ('seq', ('WFORMTA', 'VIEWRO'), ALLCAP, all_view(TA, True, 1, 111)),
    ('seq', ('WFORMTA', 'VIEW2'), ALLCAP, all_view(TA2, True)),
    ('seq', ('WFORMTA', 'VIEWALTI'), ALLCAP, all_view(TA2, True)),
    ('seq', ('WFORMTA', 'VIEWALTS'), ALLCAP, all_view(TA2, True)),
    # INVWIN: forms of pages open on the strip, one-page forms on Close.
    ('seq', ('INVWINIA', 'VIEW'), VCAP,
     item(VIEW, 0, STRIP, IA, helped=True)),
    ('seq', ('INVWIN2', 'VIEW'), 'Service record 1 Recapped the power'
     ' supply', SVC),
    ('seq', ('INVWINUA', 'VIEW'), 'User ADMIN', USER),
    ('seq', ('INVWINLB', 'VIEW'), 'Loan 1', LOAN),
)


def dialogs(lines):
    """Each dialog of a snapshot: its caption and its children's lines,
    those one blank deep."""
    out = []
    for ln in lines:
        if ln.startswith('#32770 "'):
            out.append((ln[len('#32770 "'):].rsplit('"', 1)[0], []))
        elif not ln.startswith(' '):
            out.append((None, []))
        elif out and not ln.startswith('  '):
            out[-1][1].append(ln)
    return [d for d in out if d[0] is not None]


def where(snap):
    """The golden and snapshot a case's snap names."""
    return snap if isinstance(snap, tuple) else (GOLDEN, snap)


def check(case, lines):
    """The reasons case fails, none when it holds, lines its snapshot's
    or None."""
    kind = case[0]
    snap = '%s.SNP %s' % where(case[1])
    if lines is None:
        return ['no snapshot %s' % snap]
    dlgs = dialogs(lines)
    if kind == 'nodlg':
        return ['%s: a dialog, "%s"' % (snap, d[0]) for d in dlgs]
    if kind == 'frame':
        got = [ln for ln in lines if ln.startswith('WFORMT "')]
        if len(got) == 1 and got[0].startswith(case[2]):
            return []
        return ['%s: the frame\n  want %s...\n  got  %s' % (
            snap, case[2], ' | '.join(got) if got else '(none)')]
    caption = case[2]
    found = [kids for cap, kids in dlgs if caption is None or cap == caption]
    if len(found) != 1:
        return ['%s: %d dialogs "%s", want 1' % (
            snap, len(found), caption if caption else '(any)')]
    kids = found[0]
    if kind == 'seq':
        want = case[3]
        got = tuple(ln for ln in kids if ln not in NOLABEL)
        if got == want:
            return []
        return ['%s %s: the controls\n  want %s\n  got  %s' % (
            snap, caption, '\n       '.join(want), '\n       '.join(got))]
    if kind == 'has':
        return ['%s %s: want once %s, got %d' % (
            snap, caption, ln, kids.count(ln))
            for ln in case[3] if kids.count(ln) != 1]
    if kind == 'lacks':
        return ['%s %s: %s' % (snap, caption, ln) for ln in kids
                for pre in case[3] if ln.startswith(pre)]
    got = tuple(ln[len(LABEL) + 1:].rsplit('"', 1)[0] for ln in kids
                if ln.startswith(LABEL) and ln not in NOLABEL)
    if got == case[3]:
        return []
    return ['%s %s: the labels\n  want %s\n  got  %s' % (
        snap, caption, ' | '.join(case[3]), ' | '.join(got))]


def main():
    cache = {}
    bad = []
    for case in CASES:
        golden, snap = where(case[1])
        if golden not in cache:
            cache[golden] = colchk.snaps(golden)
        if cache[golden] is None:
            why = ['%s.SNP: cannot read it' % golden]
        else:
            why = check(case, cache[golden].get(snap))
        bad += [w for w in why if w not in bad]
    for why in bad:
        print('frmchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('frmchk: %d cases ok' % len(CASES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
