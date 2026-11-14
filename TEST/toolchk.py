"""toolchk.py - the tool bar's bitmap that SRC/WIN/INVTOOL.PY writes,
against the format WTOOL.H reads and the glyphs' own text art.

usage: python3 -I TEST/toolchk.py

INVTOOL.PY is run twice into a scratch directory: both runs must exit 0
and write the same bytes, a Windows 3.0 BMP of 16 colours: the file
header, a BITMAPINFOHEADER (WTB_GLYPH pixels wide, two glyphs high for
each of GLYPHS, one plane, 4 bits a pixel, BI_RGB, biSizeImage set,
every other field 0), the colour table of SRC/WIN/INVICON.PY's PALETTE,
then the pixels, bottom row first, two a byte, the left one in the high
half.  From the top, glyph k's WTB_GLYPH rows must be its art in
GLYPHS, each letter the colour of LETTERS, and the WTB_GLYPH rows below
them its grayed form: dark grey where the art draws (neither the face
'.' nor white 'W') at an even x + y, the face everywhere else.  GLYPHS
must be the eight glyphs WTOOL's buttons show, in that order, each of
those letters only and with a black outline, and three of them the
colour they are known by.  INVTOOL.PY must exit 2 when not given one
file, and 1 when a glyph is not WTB_GLYPH rows of WTB_GLYPH letters,
each time without a traceback.  Exit status 1 if any of this fails.
"""
import importlib.machinery
import importlib.util
import os
import re
import struct
import subprocess
import sys
import tempfile

# INVTOOL.PY is loaded for its GLYPHS: no __pycache__ beside it, whose
# copy might outlive an edit within the same second.
sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
WIN = os.path.join(HERE, '..', 'SRC', 'WIN')
TOOL = os.path.join(WIN, 'INVTOOL.PY')

NAMES = ('New', 'Open', 'Edit', 'Delete', 'Move', 'Install', 'Lend', 'Find')
# The art's letters and the VGA colours they stand for.
LETTERS = {'.': 7, 'K': 0, 'r': 1, 'g': 2, 'y': 3, 'b': 4, 'm': 5, 'c': 6,
           'D': 8, 'R': 9, 'G': 10, 'Y': 11, 'B': 12, 'M': 13, 'C': 14,
           'W': 15}
FACE, GRAY = LETTERS['.'], LETTERS['D']
# The colours three glyphs are known by: a white sheet, a yellow folder,
# a red cross.
KNOWN = {'New': 'W', 'Open': 'yY', 'Delete': 'rR'}


def module(name, path):
    """The Python file path loaded as module name."""
    spec = importlib.util.spec_from_file_location(
        name, path, loader=importlib.machinery.SourceFileLoader(name, path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def glyph_side():
    """WTB_GLYPH of WTOOL.H."""
    with open(os.path.join(WIN, 'WTOOL.H'), encoding='latin-1') as f:
        m = re.search(r'^#define\s+WTB_GLYPH\s+(\d+)\s', f.read(), re.M)
    return int(m.group(1))


def run(tool, *args):
    """The exit status of INVTOOL.PY tool run with args, or -1 when it
    ended in a traceback."""
    done = subprocess.run([sys.executable, '-I', tool] + list(args),
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return -1 if b'Traceback' in done.stderr else done.returncode


def art(glyphs, side):
    """The art of glyphs, by name: its rows, or why it is not side rows
    of side letters of LETTERS with a black outline."""
    out = {}
    bad = []
    for name, text in glyphs:
        rows = text.split()
        if (len(rows) != side or any(len(r) != side for r in rows)
                or set(''.join(rows)) - set(LETTERS)):
            bad.append('%s: not %d rows of %d letters' % (name, side, side))
        elif 'K' not in ''.join(rows):
            bad.append('%s: no black' % name)
        else:
            out[name] = rows
    for name, colours in KNOWN.items():
        if name in out and not set(''.join(out[name])) & set(colours):
            bad.append('%s: none of %s' % (name, colours))
    return out, bad


def image(data, side, count, palette):
    """The pixel rows of the BMP data, top first, or why not."""
    width = side
    height = 2 * side * count
    stride = (width // 2 + 3) & ~3
    off = 14 + 40 + 4 * len(palette)
    size = off + stride * height
    if len(data) != size:
        return 'size %d, want %d' % (len(data), size)
    head = struct.unpack('<2sIHHI', data[:14])
    if head != (b'BM', size, 0, 0, off):
        return 'file header %r' % (head,)
    info = struct.unpack('<IiiHHIIiiII', data[14:54])
    want = (40, width, height, 1, 4, 0, stride * height, 0, 0, 0, 0)
    if info != want:
        return 'info header %r, want %r' % (info, want)
    table = b''.join(struct.pack('<BBBB', b, g, r, 0)
                     for r, g, b in palette)
    if data[54:off] != table:
        return 'colour table not PALETTE'
    rows = []
    for y in range(height):
        raw = data[off + y * stride:off + y * stride + width // 2]
        rows.append([n for b in raw for n in (b >> 4, b & 15)])
    rows.reverse()
    return rows


def grayed(rows):
    """The grayed form of a glyph's art rows, as colours."""
    return [[GRAY if ch not in '.W' and (x + y) % 2 == 0 else FACE
             for x, ch in enumerate(row)] for y, row in enumerate(rows)]


def check(tmp):
    """The reasons INVTOOL.PY fails, none when it holds."""
    side = glyph_side()
    palette = module('invicon', os.path.join(WIN, 'INVICON.PY')).PALETTE
    glyphs = module('invtool', TOOL).GLYPHS
    bad = []
    names = tuple(name for name, _ in glyphs)
    if names != NAMES:
        bad.append('GLYPHS %s, want %s' % (' '.join(names), ' '.join(NAMES)))
    arts, why = art(glyphs, side)
    bad += why
    outs = [os.path.join(tmp, n) for n in ('ONE.BMP', 'TWO.BMP')]
    if any(run(TOOL, o) != 0 for o in outs):
        return bad + ['a run did not exit 0']
    with open(outs[0], 'rb') as f:
        data = f.read()
    with open(outs[1], 'rb') as f:
        if f.read() != data:
            bad.append('two runs wrote different bytes')
    rows = image(data, side, len(glyphs), palette)
    if isinstance(rows, str):
        return bad + [rows]
    for k, name in enumerate(names):
        if name not in arts:
            continue
        top = 2 * side * k
        if rows[top:top + side] != [[LETTERS[ch] for ch in r]
                                    for r in arts[name]]:
            bad.append('%s: rows %d.. not its art' % (name, top))
        if rows[top + side:top + 2 * side] != grayed(arts[name]):
            bad.append('%s: rows %d.. not its grayed form' % (
                name, top + side))
    for args in ((), (outs[0], outs[1])):
        if run(TOOL, *args) != 2:
            bad.append('%d arguments: exit not 2' % len(args))
    with open(TOOL, encoding='latin-1') as f:
        src = f.read()
    first = arts.get(names[0] if names else '', [''])[0]
    line = re.compile('^%s$' % re.escape(first), re.M) if first else None
    if line and line.search(src):
        for broken in (first[:-1], first[:-1] + 'S'):
            path = os.path.join(tmp, 'INVTOOL.PY')
            with open(path, 'w', encoding='latin-1') as f:
                f.write(line.sub(broken, src, 1))
            if run(path, os.path.join(tmp, 'BAD.BMP')) != 1:
                bad.append('glyph row %r: exit not 1' % broken)
    return bad


def main():
    with tempfile.TemporaryDirectory() as tmp:
        try:
            bad = check(tmp)
        except (OSError, AttributeError, SyntaxError) as e:
            bad = ['cannot read INVTOOL.PY: %s' % e]
    for why in bad:
        print('toolchk: ' + why, file=sys.stderr)
    if bad:
        return 1
    print('toolchk: %d glyphs ok' % len(NAMES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
