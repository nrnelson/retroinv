"""rtfchk.py - the Windows Help input that TEST/hlpchk.py --rtf writes,
against the help compiler's format and the context numbers of WHELP.C.

usage: python3 -I TEST/rtfchk.py

Fixture help files go into a scratch directory, each beside its own
limits (WH_HELPW, WH_HELPKEYS, WH_HELPINDEX "INDEX", WH_CTXMUL) and a
source naming its topics.  A good one, of an INDEX section, two topics
and KEYS, must build with --out and --rtf: an RTF of CR LF lines under
255 bytes, 7-bit, read back here as RTF reads it (CR and LF ignored, one
blank after a control word its delimiter, \\'hh a cp1252 byte), in which
each section, INDEX first and in file order, is one page with the
footnotes # (its topic), $ and K (its title, its first line) and + (a
browse sequence of rising numbers), its title, and its body: a
paragraph's lines joined by a blank, with \\, { and } kept and CP437's e
acute and pound sign as cp1252's; a numbered item's continuation joined
to it; a list of keys, each entry a paragraph of its key, a tab and its
description joined with its continuation, hanging at the tab, 7 x 120
twips for the fixture's description column (a key alone on its line, and
two entries on one line, included); a block that is no key list kept
with its blanks, each line a Courier (\\f1) paragraph of its own in a
\\keep block, no \\line anywhere, as HC.EXE 3.00 ignores it; a "See
also: " line's titles as jumps to their topics.  INDEX's page must jump
to every other section in file order.  A form's fields' sections (a
form_def table of topic FORM over two fields, NAME 12 wide and KIND of
the choices AB and CD and a width a header beside the source defines)
must build too, each a page with its footnotes, left out of INDEX's
jumps, its form's page ending "Fields: " and a jump to each, titled by
its title after "Form: ", and each ending with a "See also: " jump back
to the form.  Its HPJ beside the RTF must name
the RTF, INDEX's title and INDEX, and map every section in file order to
its context number, the topic's bytes folded by h = h * WH_CTXMUL + c
modulo 2^32 (also with a multiplier of 37, and for a topic long enough
to wrap); the built file for the text window must leave INDEX out.
hlpchk must refuse, exit 1 and no RTF written, a See-also title that is
no section's, one of its own section, two sections of one title, a
character cp1252 lacks (with file and line), two topics of one context
number (AO and B0, both named), an INDEX title over 32 characters (32
builds), and no INDEX section, each line of its report starting "hlpchk:
" (no traceback); and exit 2 for --rtf without WH_HELPINDEX or
WH_CTXMUL; and refuse a field of such a form without a section, a
section naming another width after "up to ", one not naming a choice
as a word (AB within ABC), and a section of a field the form lacks.
Exit status 1 if any of this fails.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MSGH = os.path.join(HERE, '..', 'SRC', 'CLIENT', 'MSG.H')
TITLEMAX = 32       # HC 3.0's title field less its NUL

LIMITS = ('#define WH_HELPW        70\n'
          '#define WH_HELPKEYS     "KEYS"\n')
INDEX = '#define WH_HELPINDEX    "INDEX"\n'

# The good fixture: (topic, title) in file order, and its text (CP437).
GOOD = [('INDEX', 'Fixture Help'), ('ALPHA', 'Alpha topic'),
        ('LONGTOPICNAME_X', 'Long one'), ('KEYS', 'Keys everywhere')]
GOOD_TEXT = (b'Fixture header, not shown.\n'
             b'@INDEX\nFixture Help\nOpening line of the index.\n'
             b'@ALPHA\nAlpha topic\n\n'
             b'Paths like C:\\DATA keep {braces} and caf\x82 for \x9c3.\n'
             b'A second line of the paragraph.\n\n'
             b'1. First step,\n   continued here.\n2. Second step.\n\n'
             b'  F1     the keys,\n         continued\n  Ctrl-Enter\n'
             b'         a new line\n  Enter  save   Esc  cancel\n\n'
             b'  INVWIN /NET RISRV\n\n'
             b'See also: Keys everywhere; Long one.\n'
             b'@LONGTOPICNAME_X\nLong one\n\nPlain text.\n'
             b'@KEYS\nKeys everywhere\n\nSee also: Alpha topic.\n')

fails = []


def bad(what):
    fails.append(what)


def ctx(topic, mul):
    """The context number of WHELP.C's ctx for topic."""
    h = 0
    for c in topic.encode('ascii'):
        h = (h * mul + c) % 0x100000000
    return h


def run(tmp, name, text, used, mul=31, defs=LIMITS + INDEX, out=False,
        src=''):
    """hlpchk --rtf on fixture name: text its help file, used the topics
    its source names, src more of its source, mul its WH_CTXMUL (None for
    none), defs the rest of its limits, in a header beside the source.
    Returns the exit status, the report and the path of the fixture less
    its extension."""
    base = os.path.join(tmp, name)
    with open(base + '.HTX', 'wb') as f:
        f.write(text)
    with open(base + '.H', 'w') as f:
        f.write(defs + ('' if mul is None
                        else '#define WH_CTXMUL       %d\n' % mul))
    with open(base + '.C', 'w') as f:
        f.write(''.join('void f%d(void) { wh_topic("%s"); }\n' % (i, t)
                        for i, t in enumerate(used)) + src)
    args = [sys.executable, '-I', os.path.join(HERE, 'hlpchk.py')]
    if out:
        args += ['--out', base + '.BLT']
    args += ['--rtf', base + '.RTF', base + '.HTX', base + '.H',
             MSGH, base + '.C']
    p = subprocess.run(args, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    return p.returncode, p.stdout.decode('latin-1'), base


def parse(rtf):
    """The groups of rtf as nested lists of text characters and control
    words and symbols (each a tuple of its name), CR and LF dropped."""
    stack = [[]]
    i = 0
    while i < len(rtf):
        c = rtf[i]
        i += 1
        if c in '\r\n':
            continue
        if c == '{':
            stack.append([])
        elif c == '}':
            g = stack.pop()
            stack[-1].append(g)
        elif c != '\\':
            stack[-1].append(c)
        elif rtf[i] in '\\{}':
            stack[-1].append(rtf[i])
            i += 1
        elif rtf[i] == "'":
            stack[-1].append(bytes([int(rtf[i + 1:i + 3], 16)])
                             .decode('cp1252'))
            i += 3
        else:
            m = re.match(r'([a-z]+)(-?\d+)? ?|.', rtf[i:])
            stack[-1].append((m.group(1) or m.group(0),))
            i += m.end()
    return stack[0][0]


def render(group, notes, breaks):
    """The text of group: \\par a LF, \\tab a TAB, \\page a FF, a jump
    "[title]<topic>"; each footnote, with the mark before it, appended
    to notes as (mark, text) and left out, and at each page the count of
    notes so far appended to breaks."""
    out = []
    for x in group:
        if isinstance(x, list):
            head = x[0] if x else None
            if head in (('fonttbl',), ('*',)):
                continue
            text = render(x, notes, breaks)
            if head == ('footnote',):
                notes.append((out.pop() if out else '', text))
            elif head == ('uldb',):
                out.append('[' + text + ']')
            elif head == ('v',):
                out.append('<' + text + '>')
            else:
                out.append(text)
        elif x == ('par',):
            out.append('\n')
        elif x == ('tab',):
            out.append('\t')
        elif x == ('page',):
            out.append('\f')
            breaks.append(len(notes))
        elif isinstance(x, str):
            out.append(x)
    return ''.join(out)


def pages(rtf):
    """The RTF's pages as (footnotes, text) pairs."""
    notes = []
    breaks = []
    text = render(parse(rtf), notes, breaks)
    return [(notes[a:b], t) for a, b, t in
            zip([0] + breaks, breaks, text.split('\f'))]


def check_rtf(raw, name):
    lines = raw.split(b'\r\n')
    if lines[-1] != b'' or any(b'\n' in l or b'\r' in l for l in lines):
        bad('%s: not CR LF lines' % name)
    if max(len(l) for l in lines) >= 255:
        bad('%s: a line of 255 bytes or more' % name)
    if any(b > 0x7F for b in raw):
        bad("%s: a byte above 7Fh not written \\'hh" % name)
    if not raw.startswith(b'{\\rtf1\\ansi'):
        bad('%s: does not start {\\rtf1\\ansi' % name)
    try:
        return pages(raw.decode('ascii', 'replace'))
    except (IndexError, AttributeError, ValueError) as e:
        bad('%s: not RTF as read here (%s)' % (name, e))
        return []


def good(tmp):
    used = ['ALPHA', 'LONGTOPICNAME_X']
    st, rep, base = run(tmp, 'GOOD', GOOD_TEXT, used, out=True)
    if st != 0:
        bad('GOOD: exit %d, not 0: %s' % (st, rep.strip()))
        return
    with open(base + '.RTF', 'rb') as f:
        got = check_rtf(f.read(), 'GOOD.RTF')
    if len(got) != len(GOOD):
        bad('GOOD.RTF: %d pages, not %d' % (len(got), len(GOOD)))
        return
    seq = []
    body = {}
    for (topic, title), (notes, text) in zip(GOOD, got):
        marks = dict(notes)
        if sorted(marks) != ['#', '$', '+', 'K'] or len(notes) != 4:
            bad('%s: footnotes %s, not # $ K +' % (topic, notes))
            continue
        if marks['#'] != topic or marks['$'] != title or \
                marks['K'] != title:
            bad('%s: footnotes # $ K are %s' % (topic, notes))
        m = re.match(r'.*:(\d+)$', marks['+'])
        seq.append(int(m.group(1)) if m else -1)
        if not text.startswith(title + '\n'):
            bad('%s: page does not start with its title' % topic)
        body[topic] = text
    if seq != sorted(set(seq)) or -1 in seq:
        bad('GOOD.RTF: browse numbers %s not rising' % seq)
    for topic, want in [
            ('INDEX', 'Opening line of the index.\n'),
            ('INDEX', '[Alpha topic]<ALPHA>'),
            ('ALPHA', u'Paths like C:\\DATA keep {braces} and caf\xe9 for '
                      u'\xa33. A second line of the paragraph.\n'),
            ('ALPHA', '\n1. First step, continued here.\n'),
            ('ALPHA', '\n2. Second step.\n'),
            ('ALPHA', '\nF1\tthe keys, continued\nCtrl-Enter\ta new line\n'
                      'Enter\tsave\nEsc\tcancel\n'),
            ('ALPHA', '\n  INVWIN /NET RISRV\n'),
            ('ALPHA', 'See also: [Keys everywhere]<KEYS>; '
                      '[Long one]<LONGTOPICNAME_X>.\n'),
            ('LONGTOPICNAME_X', 'Plain text.\n'),
            ('KEYS', 'See also: [Alpha topic]<ALPHA>.\n')]:
        if want not in body.get(topic, ''):
            bad('%s: no %r in %r' % (topic, want, body.get(topic)))
    jumps = re.findall(r'\[([^]]*)\]<([^>]*)>', body.get('INDEX', ''))
    if jumps != [(t, n) for n, t in GOOD[1:]]:
        bad('INDEX: jumps %s, not every other section in order' % jumps)
    with open(base + '.RTF', 'rb') as f:
        raw = f.read().decode('ascii', 'replace')
    alpha = raw[raw.index('{\\footnote ALPHA}'):]
    alpha = alpha[:alpha.index('\\page')]
    if '\\line' in raw:
        bad('GOOD.RTF: a \\line, which HC.EXE 3.00 ignores')
    for key in ('F1', 'Ctrl-Enter', 'Enter', 'Esc'):
        if '\\pard\\li840\\fi-840\\tx840 %s\\tab' % key not in alpha:
            bad('ALPHA: key %s not a \\li840\\fi-840\\tx840 paragraph, its '
                'tab at the description column, 7 x 120 twips' % key)
    if '\\keep' not in alpha or \
            '{\\f1   INVWIN /NET RISRV}\\par' not in alpha:
        bad('ALPHA: the block that is no key list not a \\keep block of '
            '{\\f1 line}\\par')
    check_hpj(base, GOOD, 'Fixture Help', 31)
    with open(base + '.BLT', 'rb') as f:
        index = re.findall(rb'^=(\w+) ', f.read(), re.M)
    if index != [t.encode() for t, _ in GOOD[1:]]:
        bad('GOOD.BLT: index %s, not the sections but INDEX' % index)
    # A multiplier of WH_CTXMUL's, not a fixed one.
    st, rep, base = run(tmp, 'MUL37', GOOD_TEXT, used, mul=37)
    if st != 0:
        bad('MUL37: exit %d, not 0: %s' % (st, rep.strip()))
    else:
        check_hpj(base, GOOD, 'Fixture Help', 37)


# A form of two fields, the second's width defined in the header.
FORM_SRC = ('static const form_field ff[] = {\n'
            '    { "NAME", 1, 0, 0, 0, 1, 12, 0, 0, 0, NULL },\n'
            '    { "KIND", 2, 0, 0, 0, 5, FX_KINDW, 0, 0, 0, "AB CD" }\n'
            '};\n'
            'static const form_def fd = { 1, "FORM", 0, 0, 0, 0, 2, ff };\n')
FORM_DEFS = LIMITS + INDEX + '#define FX_KINDW        2\n'
FORM_HEAD = (b'@INDEX\nFixture Help\nIntro.\n'
             b'@FORM\nThe form\n\nText.\n')
FORM_NAME = b'@FORM_NAME\nForm: Name\n\nIts name, up to 12 characters.\n'
FORM_KIND = b'@FORM_KIND\nForm: Kind\n\nAB one, CD other: up to 2.\n'
FORM_KEYS = b'@KEYS\nKeys everywhere\n\nKeys.\n'


def fields(tmp):
    st, rep, base = run(tmp, 'FIELDS', FORM_HEAD + FORM_NAME + FORM_KIND +
                        FORM_KEYS, [], defs=FORM_DEFS, src=FORM_SRC)
    if st != 0:
        bad('FIELDS: exit %d, not 0: %s' % (st, rep.strip()))
        return
    with open(base + '.RTF', 'rb') as f:
        got = check_rtf(f.read(), 'FIELDS.RTF')
    want = [('INDEX', 'Fixture Help'), ('FORM', 'The form'),
            ('FORM_NAME', 'Form: Name'), ('FORM_KIND', 'Form: Kind'),
            ('KEYS', 'Keys everywhere')]
    if [dict(n).get('#') for n, _ in got] != [t for t, _ in want]:
        bad('FIELDS.RTF: pages %s, not %s'
            % ([dict(n).get('#') for n, _ in got], want))
        return
    body = {t: text for (t, _), (_, text) in zip(want, got)}
    jumps = re.findall(r'\[([^]]*)\]<([^>]*)>', body['INDEX'])
    if jumps != [('The form', 'FORM'), ('Keys everywhere', 'KEYS')]:
        bad('FIELDS: INDEX jumps %s, the fields\' not left out' % jumps)
    if not body['FORM'].endswith('Fields: [Name]<FORM_NAME>; '
                                 '[Kind]<FORM_KIND>.\n'):
        bad('FIELDS: FORM does not end with its fields: %r' % body['FORM'])
    for t in ('FORM_NAME', 'FORM_KIND'):
        if not body[t].endswith('See also: [The form]<FORM>.\n'):
            bad('FIELDS: %s does not end with its form: %r' % (t, body[t]))
    check_hpj(base, want, 'Fixture Help', 31)
    refused(tmp, 'XFMISS', FORM_HEAD + FORM_NAME + FORM_KEYS, [],
            ['no section for field KIND of form FORM'], defs=FORM_DEFS,
            src=FORM_SRC)
    refused(tmp, 'XFWIDTH', FORM_HEAD + FORM_NAME.replace(b'12', b'13') +
            FORM_KIND + FORM_KEYS, [], ['FORM_NAME', '"up to 13"'],
            defs=FORM_DEFS, src=FORM_SRC)
    refused(tmp, 'XFCHOICE', FORM_HEAD + FORM_NAME +
            FORM_KIND.replace(b'CD', b'C D') + FORM_KEYS, [],
            ['FORM_KIND', 'choice "CD"'], defs=FORM_DEFS, src=FORM_SRC)
    refused(tmp, 'XFWORD', FORM_HEAD + FORM_NAME +
            FORM_KIND.replace(b'AB one', b'ABC one') + FORM_KEYS, [],
            ['FORM_KIND', 'choice "AB"'], defs=FORM_DEFS, src=FORM_SRC)
    refused(tmp, 'XFOTHER', FORM_HEAD + FORM_NAME + FORM_KIND +
            b'@FORM_SIZE\nForm: Size\n\nText.\n' + FORM_KEYS, [],
            ['FORM_SIZE'], defs=FORM_DEFS, src=FORM_SRC)


def check_hpj(base, sections, title, mul):
    try:
        with open(base + '.HPJ', 'rb') as f:
            raw = f.read()
    except OSError:
        bad('%s.HPJ not written' % base)
        return
    if raw.replace(b'\r\n', b'').find(b'\n') >= 0:
        bad('%s.HPJ: not CR LF lines' % base)
    text = raw.decode('ascii')
    parts = {}
    part = None
    for l in text.split('\r\n'):
        if l.startswith('['):
            part = parts.setdefault(l, [])
        elif l and part is not None:
            part.append(l)
    opts = parts.get('[OPTIONS]', [])
    for want in ['TITLE=' + title, 'INDEX=INDEX']:
        if want not in opts:
            bad('%s.HPJ: no %s in [OPTIONS]' % (base, want))
    if parts.get('[FILES]') != [os.path.basename(base) + '.RTF']:
        bad('%s.HPJ: [FILES] %s' % (base, parts.get('[FILES]')))
    got = [l.split() for l in parts.get('[MAP]', [])]
    want = [(t, ctx(t, mul)) for t, _ in sections]
    try:
        got = [(t, int(n, 0)) for t, n in got]
    except ValueError:
        pass
    if got != want:
        bad('%s.HPJ: [MAP] %s, not %s' % (base, got, want))


def refused(tmp, name, text, used, says, status=1, **kw):
    st, rep, base = run(tmp, name, text, used, **kw)
    if st != status:
        bad('%s: exit %d, not %d: %s' % (name, st, status, rep.strip()))
    for l in rep.splitlines() if status == 1 else ():
        if not l.startswith('hlpchk: '):
            bad('%s: a report line not of hlpchk: %s' % (name, l))
    for s in says:
        if s not in rep:
            bad('%s: report does not name %s: %s' % (name, s, rep.strip()))
    if os.path.exists(base + '.RTF'):
        bad('%s: refused, yet the RTF written' % name)


def section(topic, title, *lines):
    return b'@%s\n%s\n\n%s' % (topic.encode(), title.encode(),
                               b''.join(l + b'\n' for l in lines))


def refusals(tmp):
    keys = section('KEYS', 'Keys everywhere', b'Keys.')
    head = section('INDEX', 'Fixture Help', b'Intro.')
    alpha = section('ALPHA', 'Alpha topic', b'Text.')
    refused(tmp, 'XBAD', head + section('ALPHA', 'Alpha topic',
                                        b'See also: Nowhere.') + keys,
            ['ALPHA'], ['Nowhere'])
    refused(tmp, 'XTWIN', head + alpha + section('BETA', 'Alpha topic',
                                                 b'Text.') + keys,
            ['ALPHA', 'BETA'], ['XTWIN.HTX', 'Alpha topic'])
    refused(tmp, 'XSELF', head + section('ALPHA', 'Alpha topic',
                                         b'See also: Alpha topic.') + keys,
            ['ALPHA'], ['XSELF.HTX', 'Alpha topic'])
    # CP437 B0h, a shade block, is not in cp1252.
    shade = head + section('ALPHA', 'Alpha topic', b'Shade \xb0.')
    refused(tmp, 'XCHAR', shade + keys, ['ALPHA'],
            ['XCHAR.HTX:%d' % shade.count(b'\n')])
    if ctx('AO', 31) != ctx('B0', 31):
        bad('AO and B0 do not collide: the fixture is wrong')
    refused(tmp, 'XSAME', head + section('AO', 'Topic AO', b'a') +
            section('B0', 'Topic B0', b'b') + keys, ['AO', 'B0'],
            ['AO', 'B0'])
    long_title = 'T' * (TITLEMAX + 1)
    refused(tmp, 'XTITLE', section('INDEX', long_title, b'Intro.') + alpha +
            keys, ['ALPHA'], [str(TITLEMAX)])
    st, rep, _ = run(tmp, 'TITLE32', section('INDEX', long_title[1:],
                                             b'Intro.') + alpha + keys,
                     ['ALPHA'])
    if st != 0:
        bad('TITLE32: a title of %d refused: %s' % (TITLEMAX, rep.strip()))
    refused(tmp, 'XNOINDEX', alpha + keys, ['ALPHA'], ['INDEX'])
    refused(tmp, 'XNOHIDX', head + alpha + keys, ['ALPHA'], [], status=2,
            defs=LIMITS)
    refused(tmp, 'XNOMUL', head + alpha + keys, ['ALPHA'], [], status=2,
            mul=None)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        good(tmp)
        fields(tmp)
        refusals(tmp)
    for f in fails:
        print('rtfchk: ' + f)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
