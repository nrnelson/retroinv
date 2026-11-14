"""INVICON.PY - write INVWIN's icon, drawn below as text, as an .ICO file.

usage: python3 -I SRC/WIN/INVICON.PY OUT.ICO

The icon is a PC of the 1980s in the grey of its case: a monitor showing
a list in green on black, its power light on, over a system unit with
two floppy drives and a white asset tag with a bar code, the label an
inventory puts on what it counts.  ICON holds it as 32 rows of 32
characters, top row first: a letter of COLOURS for each pixel, or '.'
where the desktop shows through.

OUT.ICO is a Windows 3.0 icon file: one image of 32 x 32 pixels in the
16 colours of the VGA, the order of PALETTE, whose AND mask is set at
each '.'.  The resource compiler binds it into INVWIN.EXE from
SRC/WIN/INVWIN.RC.

Exit status: 0 when the file is written, 1 when it cannot be, 2 on a
usage error.
"""
import struct
import sys

# The 16 colours of the VGA, as a 16-colour icon indexes them (RGB).
PALETTE = (
    (0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0),
    (0, 0, 128), (128, 0, 128), (0, 128, 128), (192, 192, 192),
    (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0),
    (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255))

# The letters of ICON: black, light grey (the case), dark grey (its
# shadows), green (the screen's text and the power light) and white.
COLOURS = {'K': 0, 'S': 7, 'D': 8, 'g': 10, 'W': 15}

ICON = '''
................................
...KKKKKKKKKKKKKKKKKKKKKKKKKK...
...KWWWWWWWWWWWWWWWWWWWWWWWDK...
...KWSSSSSSSSSSSSSSSSSSSSSSDK...
...KWSDDDDDDDDDDDDDDDDDDDSSDK...
...KWSDKKKKKKKKKKKKKKKKKKWSDK...
...KWSDKggKgggggggggKKKKKWSDK...
...KWSDKKKKKKKKKKKKKKKKKKWSDK...
...KWSDKggKgggggggKKKKKKKWSDK...
...KWSDKKKKKKKKKKKKKKKKKKWSDK...
...KWSDKggKggggggggggKKKKWSDK...
...KWSDKKKKKKKKKKKKKKKKKKWSDK...
...KWSDKggKgggggKKKKKKKKKWSDK...
...KWSDKKKKKKKKKKKKKKKKKKWSDK...
...KWSDKgKKKKKKKKKKKKKKKKWSDK...
...KWSWWWWWWWWWWWWWWWWWWWWSDK...
...KWSSSSSSSSSSSSSSSSSSgSSSDK...
...KDDDDDDDDDDDDDDDDDDDDDDDDK...
...KKKKKKKKKKKKKKKKKKKKKKKKKK...
...........KDSSSSSSDK...........
.KKKKKKKKKKKKKKKKKKKKKKKKKKKKKK.
.KWWWWWWWWWWWWWWWWWWWWWWWWWWWDK.
.KWSSSSSSSSSSSSSSSSSSSSSSSSSSDK.
.KWSWWWWWWWWWSDDDDDDDDDDDDDDSDK.
.KWSWKKWKWKKWSDSSSSSDDSSSSSDSDK.
.KWSWKKWKWKKWSDKKKKKDDKKKKKDSDK.
.KWSWKKWKWKKWSDSSSSSDDSSSSSDSDK.
.KWSWWWWWWWWWSDDDDDDDDDDDDDDSDK.
.KWSSSSSSSSSSSSSSSSSSSSSSSSSSDK.
.KDDDDDDDDDDDDDDDDDDDDDDDDDDDDK.
.KKKKKKKKKKKKKKKKKKKKKKKKKKKKKK.
................................
'''

SIZE = 32


def ico(rows):
    """The bytes of the icon file of rows: the directory, then the image,
    a header, the palette, the colour bitmap and the AND mask, both
    bottom row first, a row of each a whole number of 32-bit words."""
    pixels = b''
    mask = b''
    for row in reversed(rows):
        # A '.' is black under a set mask bit: the screen unchanged.
        ix = [COLOURS.get(ch, 0) for ch in row]
        pixels += bytes(ix[x] << 4 | ix[x + 1] for x in range(0, SIZE, 2))
        bits = 0
        for ch in row:
            bits = bits << 1 | (ch == '.')
        mask += struct.pack('>I', bits)
    image = (struct.pack('<IiiHHIIiiII', 40, SIZE, 2 * SIZE, 1, 4, 0,
                         len(pixels) + len(mask), 0, 0, 0, 0) +
             b''.join(struct.pack('<BBBB', b, g, r, 0)
                      for r, g, b in PALETTE) +
             pixels + mask)
    return (struct.pack('<HHH', 0, 1, 1) +
            struct.pack('<BBBBHHII', SIZE, SIZE, len(PALETTE), 0, 1, 4,
                        len(image), 6 + 16) +
            image)


def main(argv):
    if len(argv) != 1:
        sys.stderr.write('invicon: one output file expected\n' +
                         __doc__.split('\n\n')[1] + '\n')
        return 2
    rows = ICON.split()
    pens = set('.') | set(COLOURS)
    if len(rows) != SIZE or any(len(r) != SIZE or set(r) - pens
                                for r in rows):
        sys.stderr.write('invicon: ICON is not %d rows of %d pixels\n' %
                         (SIZE, SIZE))
        return 1
    try:
        with open(argv[0], 'wb') as f:
            f.write(ico(rows))
    except OSError as e:
        sys.stderr.write('invicon: %s\n' % e)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
