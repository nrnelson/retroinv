"""hlpchk.py - check a client's help file against the limits of its viewer.

usage: python3 -I TEST/hlpchk.py [--out BUILT] [--rtf FILE.RTF] HELP LIMITS
           MSG.H SOURCE.C...

HELP holds sections, each starting with a line "@TOPIC"; the lines
before the first one are not shown (they are still checked for width
and control characters).  The client reads one section through the index
at the head of the built file.  LIMITS, the file of the module that
shows it (TUI.C for INV.HLP, WHELP.H for INVWIN.HTX), defines its limits
under one prefix P: P_HELPW, the characters of a line shown, P_HELPKEYS,
the section F1 shows inside the help, and P_HELPLINES, the most lines of
a section shown, when the module has such a limit; MSG.H gives
MSG_IDXLINE.  The limits are read from those files, so this check
follows them when they change.  It reports:
  - a line longer than P_HELPW (it would be cut), or holding a control
    character other than the CR of a CR LF ending;
  - a section of more than P_HELPLINES lines (its end would be lost);
  - a topic named twice, or not made of A-Z, 0-9 and _ directly after the
    '@' (blanks may follow the name), a topic whose index line would pass
    MSG_IDXLINE, and a section without a line;
  - a line before the first section that starts with '=', which the
    client would take for a line of the index;
  - a topic the sources use that has no section, and a section no
    source uses (it could never be shown).  The topics used are, in the
    SOURCE.C files, those of the program that shows the help, the topic
    given to tui_topic(t) or wh_topic(t), to the item lists'
    scr_item_list(t, ...) and il_open(src, t, ...), in a form_def
    table, first in a ws_scr table and by a ".topic = t;" or
    "->topic = t;" assignment, where t is a string literal or the name
    of a "const char name[]" string, static or not, of the same file, or
    one not static of another (other names are variables and are
    skipped), P_HELPKEYS and P_HELPINDEX;
  - two sections of one title (their first line), and a block (lines
    between blank lines) starting "See also: " that is not titles of
    other sections, each separated by "; ", ending ".".
A form's fields may have sections of their own, which F1 in the field
shows: section T_F for field F of a form_def table of topic T given in
a SOURCE.C, its fields' table there too, the names resolved as topics
are.  Once one field of a form has a section, every field of it needs
one; it reports besides:
  - a field of such a form without a section;
  - a number after "up to " in a field's section other than the
    field's width in its table, a literal or a decimal "#define" of
    a header beside a SOURCE.C;
  - a choice of a field of fixed choices that its section does not
    name as a word.
When LIMITS defines P_HELPINDEX, the file is also built for Windows Help
(--rtf), and LIMITS must define P_CTXMUL.  Section P_HELPINDEX is then
the help's index, its title the help file's, and it reports besides:
  - an index title of more than HC_TITLEMAX characters, the field HC.EXE
    keeps it in;
  - a title (a section's first line) that is blank, starts with a
    blank or holds ';', which would split its keyword;
  - a character that Windows' character set (cp1252) lacks;
  - two topics with one context number (ctx below).
A field's section is left out of the index's jumps; its form's topic
ends with a jump to each of them, and each of them with a jump back.
--out writes the built file, only when the file is clean: an index of
the sections, one line "=TOPIC hhhh hhhh" each in the order of the file
(the offset of the section's first line in the built file and the
section's length in bytes, four hex digits each), then HELP, every line
ended CR LF; the index section P_HELPINDEX, which only Windows Help
shows, is left out.
--rtf writes, only when the file is clean, FILE.RTF, the help as
HC.EXE's rich text (rtf below), and beside it the help project FILE.HPJ,
which names FILE.RTF, the index topic and the context number of every
topic, and asks for a compressed help file.

Exit status: 0 when the file is clean, 1 when anything is reported, 2 on
a usage or read error.
"""
import os
import re
import sys

HC_TITLEMAX = 32        # characters of the help file's title in HC.EXE 3.0
SEE_ALSO = 'See also: '
FIELD_LIST = 'Fields: '
KEY_TWIPS = 120         # twips a character of a key list's key column
KEY_MAXTWIPS = 2880     # the widest key column, two inches


def limit(text, name, fn, need=True):
    """Decimal value of "#define name" in text, the source fn; None when
    it is not defined and need is False; ValueError otherwise."""
    m = re.search(r'^#define\s+%s\b[ \t]*(.*)$' % name, text, re.M)
    if not m and not need:
        return None
    if not m:
        raise ValueError('%s not defined in %s' % (name, fn))
    value = re.sub(r'/\*.*?\*/', '', m.group(1)).strip()
    if not value.isdigit():
        raise ValueError('%s in %s is "%s", not a plain number; '
                         'this check reads only decimal constants'
                         % (name, fn, value))
    return int(value)


# Where a source gives a help topic: each pattern's group 1 is a string
# literal or a name.
TOPIC = r'("[^"]*"|\w+)'
TOPIC_AT = [
    r'\b(?:tui_topic|wh_topic|scr_item_list)\(\s*' + TOPIC,
    r'\bil_open\(\s*&?\w+\s*,\s*' + TOPIC,
    r'\bform_def\s+\w+\s*=\s*\{\s*\w+\s*,\s*' + TOPIC,
    r'\bws_scr\s+\w+\s*=\s*\{\s*' + TOPIC,
    r'(?:\.|->)topic\s*=\s*' + TOPIC + r'\s*;',
]


# A "const char name[]" string: group 1 "static" or empty, 2 the name, 3
# the text.
NAMED = r'\b(static\s+)?const\s+char\s+(\w+)\s*\[\s*\]\s*=\s*"([^"]*)"'


# A form_field table: group 1 its name, 2 its rows; one of its rows and
# a form_def table: group 1 the members.
FIELD_TABLE = r'\bform_field\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;'
FORM_TABLE = r'\bform_def\s+\w+\s*=\s*\{([^}]*)\}'
FIELD_ROW = r'\{([^}]*)\}'
# A decimal "#define": group 1 the name, 2 the value.
DEFINE = r'^#define\s+(\w+)[ \t]+(\d+)[ \t]*(?:/\*.*)?$'


def exported(src):
    return {n: t for s, n, t in re.findall(NAMED, src) if not s}


def topics(src, shared):
    names = dict(shared)
    names.update((n, t) for _, n, t in re.findall(NAMED, src))
    found = set()
    for pat in TOPIC_AT:
        for t in re.findall(pat, src):
            if t.startswith('"'):
                found.add(t[1:-1])
            elif t in names:
                found.add(names[t])
    return found


def forms(src, shared, defines):
    """The forms of src's form_def tables whose topic and fields' table
    src gives, each (topic, fields), fields a list of (name, width,
    choices) in table order: width an int or None when not decimal,
    choices a list of words or None."""
    names = dict(shared)
    names.update((n, t) for _, n, t in re.findall(NAMED, src))

    def text(tok):
        tok = tok.strip()
        return tok[1:-1] if tok.startswith('"') else names.get(tok)

    tables = {n: [[m.strip() for m in r.split(',')]
                  for r in re.findall(FIELD_ROW, body)]
              for n, body in re.findall(FIELD_TABLE, src, re.S)}
    found = []
    for members in re.findall(FORM_TABLE, src):
        m = [x.strip() for x in members.split(',')]
        topic = text(m[1])
        if topic is None or m[-1] not in tables:
            continue
        fields = []
        for r in tables[m[-1]]:
            w = r[6]
            width = int(w) if w.isdigit() else defines.get(w)
            ch = text(r[10]) if len(r) > 10 else None
            fields.append((text(r[0]), width, ch.split() if ch else None))
        found.append((topic, fields))
    return found


def prefix(text, fn):
    """The prefix P of the P_HELPW that text, the source fn, defines;
    ValueError when it defines none or more than one."""
    found = re.findall(r'^#define\s+(\w+)_HELPW\b', text, re.M)
    if len(found) != 1:
        raise ValueError('not one <P>_HELPW defined in %s' % fn)
    return found[0]


def keys_topic(text, name, fn):
    m = re.search(r'^#define\s+%s\s+"([^"]*)"' % name, text, re.M)
    if not m:
        raise ValueError('%s not defined as a string in %s' % (name, fn))
    return m.group(1)


def ctx(topic, mul):
    """The context number of topic: h = h * mul + c over its bytes,
    modulo 2 ** 32, as ctx of WHELP.C computes it."""
    h = 0
    for c in topic.encode('latin-1'):
        h = (h * mul + c) & 0xFFFFFFFF
    return h


def blocks(lines):
    """The runs of non-blank lines of lines."""
    run = []
    for l in lines + ['']:
        if l.strip():
            run.append(l)
        elif run:
            yield run
            run = []


def see_also(block):
    """The titles a "See also: " block lists, or None when block is
    not one; ValueError when it is not titles separated by "; " and
    ending "."."""
    if not block[0].startswith(SEE_ALSO):
        return None
    text = ' '.join(l.strip() for l in block)[len(SEE_ALSO):]
    if not text.endswith('.') or not text[:-1]:
        raise ValueError('"See also: " not ended by "."')
    return text[:-1].split('; ')


def key_list(block):
    """The entries of block as (key, description lines) when it is a
    list of keys: each entry a line, indented, of a key, two blanks or
    more and its description, or of a key alone, continued by lines
    indented deeper; a gap of three blanks or more in a description
    starts another entry on the same line ("Enter  save   Esc  cancel").
    Also the column of the descriptions, the most common, counted from
    the indent; else None."""
    entries = []
    cols = []
    indent = None
    for l in block:
        ind = len(l) - len(l.lstrip(' '))
        if entries and ind > indent:
            entries[-1][1].append(l.strip())
            continue
        m = re.match(r'( +)(\S.*?)(?: {2,}(\S.*))?$', l)
        if not m or (entries and not entries[-1][1]):
            return None
        indent = ind
        if m.group(3) is None:
            entries.append((m.group(2), []))
            continue
        cols.append(m.start(3) - ind)
        pieces = re.split(r' {3,}', m.group(3))
        pieces[1:] = [w for x in pieces[1:] for w in re.split(r' {2,}', x)]
        if len(pieces) % 2 == 0:
            return None
        entries.append((m.group(2), [pieces[0]]))
        entries.extend((k, [d]) for k, d in zip(pieces[1::2], pieces[2::2]))
    if not cols or not entries[-1][1]:
        return None
    return max(set(cols), key=cols.count), entries


def numbered(block):
    """The items of block as lists of lines, when it is a numbered list:
    each item a line "N. text", continued by lines indented by exactly
    the width of "N. "; else None."""
    items = []
    for l in block:
        m = re.match(r'\d+\. ', l)
        if m:
            items.append([l])
            width = m.end()
        elif (items and l[:width].strip() == '' and
              not l[width:width + 1].isspace()):
            items[-1].append(l)
        else:
            return None
    return items


def esc(text):
    """text, read as CP437, as RTF text in cp1252."""
    out = []
    for c in text:
        if c in '\\{}':
            out.append('\\' + c)
        elif ord(c) < 0x80:
            out.append(c)
        else:
            out.append("\\'%02x" % c.encode('cp1252')[0])
    return ''.join(out)


def joined(lines):
    """lines as the RTF lines of one wrapped paragraph."""
    return [esc(l.strip()) + ' ' for l in lines[:-1]] + \
        [esc(lines[-1].strip()) + '\\par\\par']


def rtf(sections, index, group, fields):
    """The RTF file of sections (name, title, body lines), index first,
    each a topic whose context string is its name, its title the title,
    the keyword and a heading, all in one browse sequence, group:nnnn, in
    the order of the file.  Of the body's blocks, a "See also: " block
    becomes jumps to its titles, a numbered list a wrapped paragraph per
    item, a list of keys (key_list) a paragraph per entry, its key, a tab
    and its description wrapped with a hanging indent at the tab, which
    is KEY_TWIPS per character of the list's description column, at most
    KEY_MAXTWIPS; any other block with a line that starts with a blank a
    \\keep block of one Courier paragraph per line, kept as it is (HC.EXE
    3.00 ignores \\line), and any other a wrapped paragraph; the index
    ends with a jump to every other section but the fields' (fields, of
    each form's topic the sections of its fields in order), a form's
    topic with "Fields: " and a jump to each, titled by its title after
    its first ": ", and a field's with a "See also: " jump to its
    form's."""
    titles = {t: n for n, t, _ in sections}
    names = {n: t for n, t, _ in sections}
    form_of = {f: n for n, fs in fields.items() for f in fs}
    order = sorted(sections, key=lambda s: s[0] != index)
    out = ['{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Helv;}'
           '{\\f1\\fmodern Courier;}}\\f0\\fs20']
    for seq, (name, title, body) in enumerate(order):
        t = esc(title)
        out.append('#{\\footnote %s}${\\footnote %s}K{\\footnote %s}'
                   '+{\\footnote %s:%04d}' % (name, t, t, group, seq))
        out.append('{\\b\\fs24 %s}\\par\\par' % t)
        for b in blocks(body):
            refs = see_also(b)
            items = numbered(b)
            if refs is not None:
                jumps = ['{\\uldb %s}{\\v %s}' % (esc(r), titles[r])
                         for r in refs]
                out.append(esc(SEE_ALSO) + '; '.join(jumps) + '.\\par\\par')
            elif items is not None:
                for item in items:
                    out.extend(joined(item))
            elif any(l[:1].isspace() for l in b) and key_list(b):
                col, entries = key_list(b)
                n = min(col * KEY_TWIPS, KEY_MAXTWIPS)
                for key, desc in entries:
                    out.append('\\pard\\li%d\\fi-%d\\tx%d %s\\tab'
                               % (n, n, n, esc(key)))
                    out.extend(joined(desc)[:-1] +
                               [esc(desc[-1].strip()) + '\\par'])
                out.append('\\pard\\par')
            elif any(l[:1].isspace() for l in b):
                out.append('\\pard\\keep')
                out.extend('{\\f1 ' + esc(l) + '}\\par' for l in b)
                out.append('\\pard\\par')
            else:
                out.extend(joined(b))
        if name == index:
            out.extend('{\\uldb %s}{\\v %s}\\par' % (esc(t2), n)
                       for n, t2, _ in sections
                       if n != index and n not in form_of)
        if name in fields:
            jumps = ['{\\uldb %s}{\\v %s}'
                     % (esc(names[f].split(': ', 1)[-1]), f)
                     for f in fields[name]]
            jumps[0] = esc(FIELD_LIST) + jumps[0]
            out.extend([j + '; ' for j in jumps[:-1]] + [jumps[-1] + '.\\par'])
        if name in form_of:
            out.append('%s{\\uldb %s}{\\v %s}.\\par'
                       % (esc(SEE_ALSO), esc(names[form_of[name]]),
                          form_of[name]))
        out.append('\\page')
    out.append('}')
    return ''.join(l + '\r\n' for l in out).encode('ascii')


def project(rtfname, sections, index, mul):
    """The help project of the RTF file rtfname."""
    title = [t for n, t, _ in sections if n == index][0]
    lines = ['[OPTIONS]', 'TITLE=' + title, 'INDEX=' + index,
             'COMPRESS=TRUE', 'WARNING=3', '', '[FILES]', rtfname, '',
             '[MAP]']
    lines += ['%s %d' % (n, ctx(n, mul)) for n, _, _ in sections]
    return ''.join(l + '\r\n' for l in lines).encode('latin-1')


def built(lines):
    """The built file of the help file's lines (without line ends): the
    index of its sections, then the lines, all ended CR LF; None when
    an offset passes four hex digits."""
    body = b''.join(l + b'\r\n' for l in lines)
    sections = []
    at = 0
    for l in lines:
        at += len(l) + 2
        if l.startswith(b'@'):
            sections.append([l[1:].rstrip(b' \t'), at, at])
        elif sections:
            sections[-1][2] = at
    index = len(b''.join(b'=%s 0000 0000\r\n' % s[0] for s in sections))
    if index + len(body) > 0xFFFF:
        return None
    return b''.join(b'=%s %04X %04X\r\n' % (name, index + start, end - start)
                    for name, start, end in sections) + body


def usage(reason):
    """Write reason and the usage line on standard error; return 2."""
    sys.stderr.write('hlpchk: %s\n%s\n' % (reason, __doc__.split('\n\n')[1]))
    return 2


def main(argv):
    out = None
    rtfout = None
    while argv[:1] in (['--out'], ['--rtf']) and len(argv) > 1:
        if argv[0] == '--out':
            out = argv[1]
        else:
            rtfout = argv[1]
        argv = argv[2:]
    if len(argv) < 4:
        return usage('not HELP, LIMITS, MSG.H and SOURCE.C')
    hlp, lim, msgh = argv[:3]
    try:
        with open(lim, encoding='latin-1') as f:
            limits = f.read()
        p = prefix(limits, lim)
        width = limit(limits, p + '_HELPW', lim)
        maxlines = limit(limits, p + '_HELPLINES', lim, False)
        with open(msgh, encoding='latin-1') as f:
            idxline = limit(f.read(), 'MSG_IDXLINE', msgh)
        with open(hlp, 'rb') as f:
            data = f.read()
        used = {keys_topic(limits, p + '_HELPKEYS', lim)}
        index = None
        if re.search(r'^#define\s+%s_HELPINDEX\b' % p, limits, re.M):
            index = keys_topic(limits, p + '_HELPINDEX', lim)
            mul = limit(limits, p + '_CTXMUL', lim)
            used.add(index)
        elif rtfout:
            return usage('--rtf without %s_HELPINDEX in %s' % (p, lim))
        srcs = []
        for fn in argv[3:]:
            with open(fn, encoding='latin-1') as f:
                srcs.append(f.read())
        defines = {}
        heads = sorted({os.path.join(os.path.dirname(fn), h)
                        for fn in argv[3:]
                        for h in os.listdir(os.path.dirname(fn) or '.')
                        if h.endswith('.H')})
        for fn in heads:
            with open(fn, encoding='latin-1') as f:
                head = f.read()
            defines.update((n, int(v)) for n, v in
                           re.findall(DEFINE, head, re.M))
        shared = {}
        for src in srcs:
            shared.update(exported(src))
        tables = []
        for src in srcs:
            used.update(topics(src, shared))
            tables.extend(forms(src, shared, defines))
    except (OSError, ValueError) as e:
        sys.stderr.write('hlpchk: %s\n' % e)
        return 2

    bad = []
    sections = {}
    texts = {}
    topic = None
    raws = []
    for num, raw in enumerate(data.split(b'\n'), 1):
        if raw.endswith(b'\r'):
            raw = raw[:-1]
        if num == data.count(b'\n') + 1 and raw == b'':
            break
        raws.append(raw)
        line = raw.decode('latin-1')
        where = '%s:%d' % (hlp, num)
        if any(ord(c) < 0x20 or ord(c) == 0x7F for c in line):
            bad.append('%s: control character in the line' % where)
        text = raw.decode('cp437')
        if index is not None:
            try:
                text.encode('cp1252')
            except UnicodeEncodeError:
                bad.append('%s: a character Windows (cp1252) lacks' % where)
        if line.startswith('@'):
            # The index holds the name right after the '@', without the
            # blanks that may follow it.
            topic = line[1:].rstrip(' \t')
            if not re.match(r'^[A-Z0-9_]+$', topic):
                bad.append('%s: bad topic name "%s"' % (where, topic))
            if topic in sections:
                bad.append('%s: topic %s named twice' % (where, topic))
            if len('=%s 0000 0000' % topic) > idxline:
                bad.append('%s: topic %s too long for an index line '
                           '(MSG_IDXLINE %d)' % (where, topic, idxline))
            sections[topic] = [0, num]
            texts[topic] = []
            continue
        if topic is None and line.startswith('='):
            bad.append('%s: "=" starts a line before the first section, '
                       'as in the index' % where)
        if len(line) > width:
            bad.append('%s: %d characters (%s_HELPW %d)'
                       % (where, len(line), p, width))
        if topic is not None:
            sections[topic][0] += 1
            texts[topic].append(text)
    for name, (lines, num) in sorted(sections.items()):
        if maxlines is not None and lines > maxlines:
            bad.append('%s:%d: section %s has %d lines (%s_HELPLINES %d)'
                       % (hlp, num, name, lines, p, maxlines))
        if lines == 0:
            bad.append('%s:%d: section %s is empty' % (hlp, num, name))
    fields = {}
    for topic, rows in tables:
        names = [(topic + '_' + (f or ''), w, ch) for f, w, ch in rows]
        if not any(n in sections for n, _, _ in names):
            continue
        for n, w, ch in names:
            if n not in sections:
                bad.append('%s: no section for field %s of form %s'
                           % (hlp, n[len(topic) + 1:], topic))
                continue
            used.add(n)
            fields.setdefault(topic, []).append(n)
            num = sections[n][1]
            text = ' '.join(texts[n][1:])
            for up in re.findall(r'\bup to (\d+)\b', text):
                if int(up) != w:
                    bad.append('%s:%d: section %s: "up to %s", the field\'s '
                               'width is %s' % (hlp, num, n, up, w))
            for c in ch or []:
                if not re.search(r'(?<!\S)%s(?=[\s,.;:)])' % re.escape(c),
                                 text + ' '):
                    bad.append('%s:%d: section %s: choice "%s" not named'
                               % (hlp, num, n, c))
    for name in sorted(used - set(sections)):
        bad.append('%s: no section for topic %s, used in the sources'
                   % (hlp, name))
    for name in sorted(set(sections) - used):
        bad.append('%s:%d: section %s is not used in the sources'
                   % (hlp, sections[name][1], name))
    titles = {}
    for name, lines in texts.items():
        if lines and lines[0] in titles:
            bad.append('%s:%d: sections %s and %s have one title "%s"'
                       % (hlp, sections[name][1], titles[lines[0]], name,
                          lines[0]))
        elif lines:
            titles[lines[0]] = name
    for name, lines in texts.items():
        num = sections[name][1]
        for b in blocks(lines[1:]):
            try:
                refs = see_also(b)
            except ValueError as e:
                bad.append('%s:%d: section %s: %s' % (hlp, num, name, e))
                continue
            for r in refs or []:
                if titles.get(r, name) == name:
                    bad.append('%s:%d: section %s: "See also" names "%s", '
                               'not the title of another section'
                               % (hlp, num, name, r))
    if index is not None:
        for name, lines in texts.items():
            num = sections[name][1]
            if lines and (lines[0][:1] in ('', ' ', '\t') or ';' in lines[0]):
                bad.append('%s:%d: section %s: title "%s" blank, indented '
                           'or holding ";"' % (hlp, num, name, lines[0]))
        if index in texts and texts[index] and \
                len(texts[index][0]) > HC_TITLEMAX:
            bad.append('%s:%d: index title over %d characters (HC_TITLEMAX)'
                       % (hlp, sections[index][1], HC_TITLEMAX))
        seen = {}
        for name in texts:
            n = ctx(name, mul)
            if n in seen:
                bad.append('%s: topics %s and %s have one context number %d'
                           % (hlp, seen[n], name, n))
            seen[n] = name
        start = sections.get(index, [0, 0])[1]
        end = min([v[1] for v in sections.values() if v[1] > start] +
                  [len(raws) + 1])
        if start:
            raws = raws[:start - 1] + raws[end - 1:]
    data = built(raws)
    if data is None:
        bad.append('%s: too large for an index of four hex digits' % hlp)
    for b in bad:
        print('hlpchk: ' + b)
    if bad:
        return 1
    files = []
    if out:
        files.append((out, data))
    if rtfout:
        secs = [(n, t[0], t[1:]) for n, t in texts.items()]
        group = os.path.splitext(os.path.basename(rtfout))[0]
        files.append((rtfout, rtf(secs, index, group, fields)))
        files.append((os.path.splitext(rtfout)[0] + '.HPJ',
                      project(os.path.basename(rtfout), secs, index, mul)))
    try:
        for fn, content in files:
            with open(fn, 'wb') as f:
                f.write(content)
    except OSError as e:
        sys.stderr.write('hlpchk: %s\n' % e)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
