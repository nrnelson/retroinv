"""WATPREP.PY - prepare the WATTCP build tree from the checked archive.

usage: python3 -I SRC/TCP/WATPREP.PY ARCHIVE DIF OUTDIR

ARCHIVE is WATTCP's source.zip of 14 Sep 1994, whose SHA-256 must be
ARCHIVE_SHA256.  DIF is the unified diff of our changes to it
(SRC/TCP/WATTCP.DIF), made with "diff -u" against the files as the
archive holds them, CR LF line ends included, under the paths
a/SRC/NAME.C and b/SRC/NAME.C (a/INCLUDE/, b/INCLUDE/ for headers).
OUTDIR (OBJ/WATTCP) receives the tree, in four steps:

  1. The kernel modules of KERNEL under SRC/ and every header under
     INCLUDE/ are extracted to the same paths below OUTDIR, and nothing
     else.  An archive holding a link, an absolute or drive path or a
     ".." component is refused, as is one that lacks a module of KERNEL or
     holds a member to extract twice.
  2. DIF is applied with GNU patch ("patch -p1 --fuzz=0 --binary"),
     which must be installed (Debian package patch): every context and
     removed line of a hunk must match the file exactly, CR included,
     at the hunk's own line.  A hunk patch would place at an offset is
     refused too: the archive is fixed by its SHA-256 and DIF is made
     against it, so an offset only ever means a wrong DIF.  The free
     text before DIF's first "diff " or "--- " line describes it and is
     not given to patch.  The rest may hold only "diff" command lines
     (not "diff --git") and file sections: a "--- "/"+++ " header pair
     naming one file extracted in step 1 under the same name in both,
     then one or more hunks, in order and not overlapping, none of
     which empties the file.  Anything else is refused before patch
     runs, and the files after it must be exactly those extracted, so
     DIF neither creates, renames nor deletes a file.  An empty DIF
     changes nothing; a missing one, or one with text but no file
     section, is an error.
  3. Each extracted file gets the mechanical changes every compiler
     needs: the text from its first Ctrl-Z, DOS's end-of-file mark, is
     dropped; each "//" comment becomes a "/* */" comment, its trailing
     blanks dropped and a "*/" inside it broken up (one ending in a
     backslash, which would continue it onto the next line, is refused);
     and each #include of a WATTCP header names it in upper case, its
     base cut to eight characters as DOS cuts it, in quotes, which Open
     Watcom on Linux needs to find it, while <mem.h> and <values.h>
     become "WTSHIM.H" (SRC/COMPAT/), which gives the compilers that lack
     them the few names WATTCP takes from them.  These changes run after
     DIF, so that DIF is made against the pristine archive and holds none
     of them; none moves a line, so DIF's line numbers are the
     archive's.
  4. OUTDIR/.ok is written with the SHA-256 of ARCHIVE, DIF and this
     script, one "archive HEX", "dif HEX" and "script HEX" line each.

The tree is built in a new directory beside OUTDIR and put in its place
by two renames only when every step succeeded, so a failed run leaves
OUTDIR as it was.  An existing OUTDIR is replaced only when it is a
directory, not a link, and is empty or holds a .ok, so that a wrong
path deletes nothing.

Exit status: 0 on success, 1 when a step fails, 2 on a usage error.
"""
import hashlib
import io
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

ARCHIVE_SHA256 = ('9d46641bc7e806936d14736be8f3c457'
                  '1d502eafcd9607f7816247edb29e4f89')

# The kernel modules a TCP listener with a static address needs; the
# packet-driver interface and the helpers WATTCP keeps in other modules
# are our own code in SRC/COMPAT/.
KERNEL = ('PCARP.C', 'PCBSD.C', 'PCBUF.C', 'PCCONFIG.C', 'PCICMP.C',
          'PCSED.C', 'PCTCP.C', 'UDP_NDS.C')
HEADERS = 'INCLUDE'

SHIM = 'WTSHIM.H'
SHIMMED = ('MEM.H', 'VALUES.H')
STAMP = '.ok'

# A comment, string or character constant, so that a "//" is converted
# only outside them.
TOKEN = re.compile(r'/\*.*?\*/|//[^\n]*'
                   r'|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
INCLUDE = re.compile(r'^([ \t]*#[ \t]*include[ \t]*)[<"]([^<>"\r\n]+)[>"]',
                     re.M)
HUNK = re.compile(rb'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@')


class PrepError(Exception):
    """A failed step; the text says what failed."""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def check_archive(path):
    """The archive's bytes, once their SHA-256 is ARCHIVE_SHA256."""
    try:
        data = read(path)
    except FileNotFoundError:
        raise PrepError('%s: not found; it must be WATTCP\'s source.zip of'
                        ' 14 Sep 1994, SHA-256 %s' % (path, ARCHIVE_SHA256))
    digest = sha256(data)
    if digest != ARCHIVE_SHA256:
        raise PrepError('%s: SHA-256 %s, expected %s'
                        % (path, digest, ARCHIVE_SHA256))
    return data


def is_header(name):
    return name.rpartition('/')[0] == HEADERS and not name.endswith('/')


def extract(path, data, stage):
    """Extract step 1's members of the archive (path, data) into stage;
    return their names."""
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        names = []
        for info in z.infolist():
            names.append(info.filename)
            name = info.filename.replace('\\', '/')
            if stat.S_ISLNK(info.external_attr >> 16) or \
                    name.startswith('/') or ':' in name or \
                    '..' in name.split('/'):
                raise PrepError('%s: unsafe member %s refused'
                                % (path, info.filename))
        wanted = ['SRC/' + n for n in KERNEL] + \
            sorted(n for n in names if is_header(n))
        for name in wanted:
            count = names.count(name)
            if count != 1:
                raise PrepError('%s: %s %s' % (path, name, 'held twice'
                                               if count else 'missing'))
            os.makedirs(os.path.join(stage, os.path.dirname(name)),
                        exist_ok=True)
            with open(os.path.join(stage, name), 'wb') as f:
                f.write(z.read(name))
    return wanted


def dif_path(line):
    """The path a "--- " or "+++ " line names."""
    return line[4:].rstrip(b'\r\n').split(b'\t')[0].decode('latin-1')


def read_dif(path, data, members):
    """The part of the unified diff (path, data) that patch is given,
    from its first "diff " or "--- " line on, once step 2's rules hold
    for it; empty for an empty diff."""
    lines = io.BytesIO(data).readlines()
    i = 0
    while i < len(lines) and not lines[i].startswith((b'diff ', b'--- ')):
        i += 1
    body = i
    name = None
    while i < len(lines):
        line = lines[i]
        i += 1
        if line.startswith(b'diff ') and not line.startswith(b'diff --git'):
            continue
        if line.startswith(b'--- ') and i < len(lines) and \
                lines[i].startswith(b'+++ '):
            if name and not hunks:
                raise PrepError('%s: no hunk for %s' % (path, name))
            before = dif_path(line)
            after = dif_path(lines[i])
            i += 1
            name = after.partition('/')[2]
            if before.partition('/')[2] != name or name not in members:
                raise PrepError('%s:%d: %s and %s do not name one file of'
                                ' the tree' % (path, i, before, after))
            hunks = 0
            end = 0
            continue
        m = HUNK.match(line)
        if not m or not name:
            raise PrepError('%s:%d: not a line of a unified diff'
                            % (path, i))
        start = i
        first = int(m.group(1))
        nold = int(m.group(2) or 1)
        nnew = int(m.group(4) or 1)
        if first < end:
            raise PrepError('%s:%d: hunk before the end of the one before'
                            % (path, i))
        if m.group(3) == b'0' and nnew == 0:
            raise PrepError('%s:%d: hunk empties %s' % (path, i, name))
        end = first + nold
        hunks += 1
        old = 0
        new = 0
        tag = None
        while i < len(lines) and (old < nold or new < nnew or
                                  lines[i].startswith(b'\\')):
            prev = tag
            tag = lines[i][:1]
            i += 1
            if tag == b'\\' and prev in (b' ', b'-', b'+'):
                continue
            if tag not in (b' ', b'-', b'+'):
                break
            old += tag != b'+'
            new += tag != b'-'
        if old != nold or new != nnew:
            raise PrepError('%s:%d: hunk cut short' % (path, start))
    if name and not hunks:
        raise PrepError('%s: no hunk for %s' % (path, name))
    if data and not name:
        raise PrepError('%s: no file section' % path)
    return b''.join(lines[body:])


def patch_dif(tool, path, body, stage):
    """Apply body, the part of the diff at path read_dif gave, to the
    files in stage with patch (tool); a hunk placed at an offset fails,
    as step 2 says."""
    r = subprocess.run([tool, '-p1', '--fuzz=0', '--binary', '--forward',
                        '--batch', '--no-backup-if-mismatch', '-d', stage],
                       input=body, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT,
                       env=dict(os.environ, LC_ALL='C'))
    said = r.stdout.decode('latin-1').rstrip()
    if r.returncode != 0 or re.search(r'offset|fuzz', said):
        raise PrepError('%s: patch failed:\n%s' % (path, said))


def files(top):
    """The paths of the files below top, relative to it, sorted."""
    return sorted(os.path.relpath(os.path.join(d, f), top)
                  for d, _, names in os.walk(top) for f in names)


def dos_name(name):
    """name in upper case, its base cut to DOS's eight characters."""
    base, dot, ext = name.upper().partition('.')
    return base[:8] + dot + ext


def transform(name, text, own):
    """text, of the file name, with step 3's changes made, own being the
    names of WATTCP's headers (dos_name)."""
    def line_comment(m):
        tok = m.group()
        if not tok.startswith('//'):
            return tok
        cr = '\r' if tok.endswith('\r') else ''
        body = tok[2:].rstrip()
        if body.endswith('\\'):
            raise PrepError('%s: a // comment ends in a backslash' % name)
        return '/*' + body.replace('*/', '* /') + ' */' + cr

    def include(m):
        header = dos_name(m.group(2))
        if header in SHIMMED:
            header = SHIM
        elif header not in own:
            return m.group()
        return '%s"%s"' % (m.group(1), header)

    text = TOKEN.sub(line_comment, text.split('\x1a', 1)[0])
    return INCLUDE.sub(include, text)


def replace(stage, old, out):
    """Put the tree stage in the place of out, moving an earlier out to
    old, which the caller removes; on a failure out is as it was."""
    if os.path.lexists(out):
        if os.path.islink(out) or not os.path.isdir(out) or \
                (os.listdir(out) and
                 not os.path.isfile(os.path.join(out, STAMP))):
            raise PrepError('%s: not an empty directory or one holding %s;'
                            ' not replaced' % (out, STAMP))
        os.rename(out, old)
    try:
        os.rename(stage, out)
    except OSError:
        if os.path.isdir(old):
            os.rename(old, out)
        raise


def prepare(archive, dif, out):
    """Run the four steps; raise PrepError or OSError on a failure."""
    zipped = check_archive(archive)
    try:
        difdata = read(dif)
    except FileNotFoundError:
        raise PrepError('%s: DIF not found' % dif)
    tool = shutil.which('patch')
    if not tool:
        raise PrepError('patch not found; install GNU patch (Debian package'
                        ' patch)')
    parent = os.path.dirname(out) or '.'
    os.makedirs(parent, exist_ok=True)
    stage = tempfile.mkdtemp(prefix='.watprep', dir=parent)
    old = stage + '.old'
    try:
        members = extract(archive, zipped, stage)
        body = read_dif(dif, difdata, members)
        if body:
            patch_dif(tool, dif, body, stage)
        if files(stage) != sorted(members):
            raise PrepError('%s: changes the set of files' % dif)
        own = {dos_name(os.path.basename(n)) for n in members
               if is_header(n)}
        for name in members:
            path = os.path.join(stage, name)
            text = transform(name, read(path).decode('latin-1'), own)
            with open(path, 'wb') as f:
                f.write(text.encode('latin-1'))
        with open(os.path.join(stage, STAMP), 'w') as f:
            f.write('archive %s\ndif %s\nscript %s\n'
                    % (sha256(zipped), sha256(difdata),
                       sha256(read(__file__))))
        mask = os.umask(0)
        os.umask(mask)
        os.chmod(stage, 0o777 & ~mask)
        replace(stage, old, out)
    finally:
        for d in (stage, old):
            if os.path.isdir(d):
                shutil.rmtree(d)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    try:
        prepare(argv[0], argv[1], os.path.normpath(argv[2]))
    except (PrepError, OSError, zipfile.BadZipFile) as e:
        sys.stderr.write('watprep: %s\n' % e)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
