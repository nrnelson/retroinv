"""hcchk.py - INVWIN.HLP, the Windows Help file that HC/runhc.sh compiles
with the SDK's HC.EXE, against a second and third compile of its input.

usage: python3 -I TEST/hcchk.py INVWIN.HLP INVWIN.RTF INVWIN.HPJ WORK
       (from the repository root)

INVWIN.HLP is the build's (make winhelp), INVWIN.RTF and INVWIN.HPJ
the input hlpchk.py wrote for it, and WORK a scratch directory, made
afresh.  The input is compiled again twice, in directories of other
depths, under other time zones, seconds apart and with the timer
interrupt some thousands of times as often (HC_TIMER of runhc.sh, set
as RUNHC.BAT and TIMER.COM must show), so the help compiler's clock
and paths differ, and what its stack holds wherever an interrupt found
it: both files must be byte for byte the build's, and differ from what
HC.EXE wrote only where it leaves a byte unset, and there only in the
bits it leaves: the |SYSTEM date, its flags word but for bit 0, the
ends after their NUL of its title, of the face names of |FONT and of
the structure fields of the directory and the B-trees, the unused ends
of their pages, and in each record of paragraphs of |TOPIC the word
before the flags but for bit 0 and PARA_UNSET of the flags.  The
build's |SYSTEM date must be the first four bytes, little-endian, of
the RTF's SHA-256, and its |CTXOMAP must hold exactly the context
numbers of the project's [MAP].  HC/hlpstamp.py must leave the build's
file as it is; from a copy with every bit of these three words changed
it must clear those HC.EXE leaves unset and keep the others; and it
must refuse (exit 1, nothing written) a text file, the file with a
wrong |SYSTEM magic, and the file with, in |TOPIC, a link of an
unknown kind, a last link cut short, a record of paragraphs whose
first part ends before its flags, or one whose second long takes four
bytes.  runhc.sh must refuse a jump to no topic (exit 1, naming
HC.EXE's warning R2041, no .HLP written), an HC.EXE not the pinned one
and an HC_TIMER with a leading zero (exit 2).  It needs DOSBox-X and
HC.EXE where runhc.sh finds them (HC/install.sh).  Exit status 1 if
any of this fails, 2 on a usage error.
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import time

HLP_MAGIC = 0x00035F3F
BTREE_MAGIC = 0x293B
BTREEHEADER = 38        # a B-tree's header, before its pages
STRUCTFIELD = 16        # its structure field, at STRUCTAT in the header
STRUCTAT = 6
SYSTEM_MAGIC = 0x036C
SYSTEMHEADER = 12       # Magic, Minor, Major, GenDate, Flags
TITLEFIELD = 33         # the title, after the header
FILEHEADER = 9          # ReservedSpace, UsedSpace, FileFlags
TOPICBLOCK = 2048       # a block of |TOPIC
BLOCKHEADER = 12        # its header, before its part of the links
LINKHEADER = 21         # BlockSize, DataLen2, PrevBlock, NextBlock,
                        # DataLen1, RecordType
LINK_TOPIC = 2          # RecordType: a topic's header
LINK_PARAS = 1          # RecordType: paragraphs
PARA_UNSET = 0xE000     # the paragraph flags no property sets
TIMERS = (8, 12)        # HC_TIMER of the two compiles

fails = []


def bad(what):
    fails.append(what)


def files(hlp):
    """The internal files of hlp, a WinHelp 3.0 file, as {name: offset},
    walked through the directory's B-tree; ValueError when it is not."""
    magic, dirstart = struct.unpack_from('<Ii', hlp, 0)
    if magic != HLP_MAGIC:
        raise ValueError('not a help file')
    at = dirstart + FILEHEADER
    (bmagic, _, pagesize) = struct.unpack_from('<HHH', hlp, at)
    if bmagic != BTREE_MAGIC:
        raise ValueError('directory not a B-tree')
    root, _, _, levels = struct.unpack_from('<HhHH', hlp, at + 26)
    pages = at + BTREEHEADER
    page = root
    for _ in range(levels - 1):
        page = struct.unpack_from('<h', hlp, pages + page * pagesize + 4)[0]
    found = {}
    while page != -1:
        p = pages + page * pagesize
        n, _, page = struct.unpack_from('<Hhh', hlp, p + 2)
        p += 8
        for _ in range(n):
            end = hlp.index(b'\0', p)
            found[hlp[p:end].decode('latin-1')] = \
                struct.unpack_from('<i', hlp, end + 1)[0]
            p = end + 5
    return found


def hpj_map(hpj):
    """The context numbers of the [MAP] of hpj."""
    with open(hpj, encoding='latin-1') as f:
        text = f.read()
    part = text.split('[MAP]', 1)[1].split('[', 1)[0]
    return sorted(int(l.split()[1], 0) for l in part.splitlines()
                  if l.strip())


def runhc(hpj, out, env=None):
    p = subprocess.run(['sh', 'HC/runhc.sh', hpj, out],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=dict(os.environ, **(env or {})))
    return p.returncode, p.stdout.decode('latin-1')


def hlpstamp(src, rtf, out):
    return subprocess.run([sys.executable, '-I', 'HC/hlpstamp.py', src, rtf,
                           out], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT).returncode


def compiles(hlp, rtf, hpj, work, free):
    """Compile twice more; free, when not None, is {offset: bits} of
    what hlpstamp may change of HC.EXE's file."""
    for sub, tz, timer in zip(('A', os.path.join('BB', 'CC', 'D')),
                              ('UTC0', 'EST5'), TIMERS):
        d = os.path.join(work, sub)
        os.makedirs(d)
        for f in (rtf, hpj):
            shutil.copy(f, d)
        out = os.path.join(d, 'OUT.HLP')
        st, log = runhc(os.path.join(d, os.path.basename(hpj)), out,
                        {'TZ': tz, 'HC_TIMER': str(timer)})
        if st != 0:
            bad('runhc in %s: exit %d: %s' % (sub, st, log.strip()))
        else:
            if open(out, 'rb').read() != hlp:
                bad('the compile in %s differs from the build\'s' % sub)
            with open(os.path.join(d, 'INVWIN.HLP'), 'rb') as f:
                raw = f.read()
            if free is not None and (len(raw) != len(hlp) or any(
                    (a ^ b) & ~free.get(i, 0)
                    for i, (a, b) in enumerate(zip(raw, hlp)))):
                bad('hlpstamp changed more of the compile in %s than '
                    'HC.EXE leaves unset' % sub)
            with open(os.path.join(d, 'RUNHC.BAT'), 'rb') as f:
                bat = f.read()
            com = os.path.join(d, 'TIMER.COM')
            if not bat.startswith(b'C:\\TIMER.COM\r\n') or \
                    not os.path.exists(com) or open(com, 'rb').read() != \
                    b'\xB0\x36\xE6\x43\xB0%c\xE6\x40\xB0%c\xE6\x40\xC3' \
                    % (timer & 255, timer >> 8):
                bad('runhc in %s did not set the timer to %d' % (sub, timer))
        # HC's GenDate counts seconds: the next compile is another one.
        time.sleep(2)


def structure(hlp, rtf, hpj):
    """Check the build's date and context numbers; return its internal
    files, or None when it is not read."""
    try:
        found = files(hlp)
        sysat = found['|SYSTEM'] + FILEHEADER
        magic, _, _, gendate = struct.unpack_from('<HHHI', hlp, sysat)
        mapat = found['|CTXOMAP'] + FILEHEADER
        n = struct.unpack_from('<H', hlp, mapat)[0]
        ids = sorted(struct.unpack_from('<I', hlp, mapat + 2 + 8 * k)[0]
                     for k in range(n))
    except (ValueError, KeyError, struct.error) as e:
        bad('the build\'s file not read: %s' % e)
        return None
    with open(rtf, 'rb') as f:
        want = struct.unpack('<I', hashlib.sha256(f.read()).digest()[:4])[0]
    if magic != SYSTEM_MAGIC:
        bad('|SYSTEM magic %04X' % magic)
    if gendate != want:
        bad('GenDate %08X, not the RTF\'s SHA-256 %08X' % (gendate, want))
    if ids != hpj_map(hpj):
        bad('|CTXOMAP %s, not the [MAP] %s' % (ids, hpj_map(hpj)))
    return found


def unset(hlp, found):
    """The three words of hlp with bits HC.EXE leaves unset, as
    {offset: these bits} of each of their bytes: the flags of |SYSTEM,
    all but bit 0, and in each record of paragraphs of |TOPIC the word
    before the flags, all but bit 0, and the flags, PARA_UNSET; then,
    of each such record, the offsets of its link and of that word.  The
    links run on from block to block; a block's end that starts no link
    is filler."""
    sysat = found['|SYSTEM'] + FILEHEADER
    bits = {sysat + 10: 0xFE, sysat + 11: 0xFF}
    paras = []
    used = struct.unpack_from('<I', hlp, found['|TOPIC'] + 4)[0]
    data = found['|TOPIC'] + FILEHEADER
    at = [o for b in range(0, used, TOPICBLOCK)
          for o in range(data + b + BLOCKHEADER,
                         data + min(b + TOPICBLOCK, used))]
    links = bytes(hlp[o] for o in at) + bytes(LINKHEADER)
    i = 0
    while i < len(at):
        room = TOPICBLOCK - BLOCKHEADER - i % (TOPICBLOCK - BLOCKHEADER)
        size, kind = struct.unpack_from('<i16xB', links, i)
        if room < LINKHEADER or kind not in (LINK_TOPIC, LINK_PARAS):
            i += room
            continue
        if kind == LINK_PARAS:
            # after TopicSize, of two bytes or four, and two bytes more
            p = i + LINKHEADER + (6 if links[i + LINKHEADER] & 1 else 4)
            bits.update(zip(at[p:p + 4],
                            (0xFE, 0xFF, PARA_UNSET & 255, PARA_UNSET >> 8)))
            paras.append((at[i], at[p]))
        i += size
    return bits, paras


def after_nul(hlp, at, n):
    """The offsets after its first NUL of the n-byte field at at."""
    return range(hlp.index(b'\0', at, at + n) + 1, at + n)


def slack(hlp, found):
    """The offsets of the bytes of hlp that HC.EXE leaves unset whole,
    and of its date."""
    sysat = found['|SYSTEM'] + FILEHEADER
    free = list(range(sysat + 6, sysat + 10))
    free += after_nul(hlp, sysat + SYSTEMHEADER, TITLEFIELD)
    f = found['|FONT'] + FILEHEADER
    faces, _, first, descr = struct.unpack_from('<HHHH', hlp, f)
    for k in range(faces):
        free += after_nul(hlp, f + first + k * ((descr - first) // faces),
                          (descr - first) // faces)
    for at in (struct.unpack_from('<i', hlp, 4)[0], found['|KWBTREE'],
               found['|TTLBTREE']):
        at += FILEHEADER
        free += after_nul(hlp, at + STRUCTAT, STRUCTFIELD)
        pagesize, total = struct.unpack_from('<H24xH', hlp, at + 4)
        for page in range(at + BTREEHEADER,
                          at + BTREEHEADER + total * pagesize, pagesize):
            end = page + pagesize
            free += range(end - struct.unpack_from('<H', hlp, page)[0], end)
    return free


def refused(src, rtf, out, name):
    if os.path.exists(out):
        os.remove(out)
    st = hlpstamp(src, rtf, out)
    if st != 1 or os.path.exists(out):
        bad('hlpstamp on %s: exit %d%s' % (name, st, ', written'
                                             if os.path.exists(out) else ''))


def stamps(hlp, rtf, hpj, found, bits, paras, work):
    out = os.path.join(work, 'OUT.HLP')
    if hlpstamp(os.path.join(work, 'BUILD.HLP'), rtf, out) != 0 or \
            open(out, 'rb').read() != hlp:
        bad('hlpstamp changed the build\'s file')
    if len(paras) < len(hpj_map(hpj)):
        bad('%d records of paragraphs in |TOPIC, fewer than the topics'
            % len(paras))
        return
    turned = bytearray(hlp)
    want = bytearray(hlp)
    for o, unset_bits in bits.items():
        turned[o] ^= 0xFF
        want[o] ^= 0xFF & ~unset_bits
    src = os.path.join(work, 'TURNED.HLP')
    with open(src, 'wb') as f:
        f.write(turned)
    if hlpstamp(src, rtf, out) != 0 or open(out, 'rb').read() != want:
        bad('hlpstamp did not clear just what HC.EXE leaves unset')
    link, word = paras[0]
    topicat = found['|TOPIC']
    used = struct.unpack_from('<I', hlp, topicat + 4)[0]
    for name, o, new in [
            ('TEXT.HLP', None, b'not a help file\r\n'),
            ('MAGIC.HLP', found['|SYSTEM'] + FILEHEADER,
             bytes([hlp[found['|SYSTEM'] + FILEHEADER] ^ 0xFF])),
            ('KIND.HLP', link + 20, b'\3'),
            ('CUT.HLP', topicat + 4, struct.pack('<I', used - 1)),
            ('SHORT.HLP', link + 16, struct.pack('<i', LINKHEADER)),
            ('LONG.HLP', word - 2, bytes([hlp[word - 2] | 1]))]:
        src = os.path.join(work, name)
        with open(src, 'wb') as f:
            f.write(new if o is None else hlp[:o] + new + hlp[o + len(new):])
        refused(src, rtf, out, name)


def refusals(work):
    d = os.path.join(work, 'JUMP')
    os.makedirs(d)
    with open(os.path.join(d, 'T.HPJ'), 'wb') as f:
        f.write(b'[OPTIONS]\r\nTITLE=T\r\n\r\n[FILES]\r\nT.RTF\r\n')
    with open(os.path.join(d, 'T.RTF'), 'wb') as f:
        f.write(b'{\\rtf1\\ansi{\\fonttbl{\\f0\\fswiss Helv;}}\\f0\r\n'
                b'#{\\footnote A}A {\\uldb x}{\\v NOPE}\\par\r\n}\r\n')
    out = os.path.join(d, 'T.HLP')
    st, log = runhc(os.path.join(d, 'T.HPJ'), out)
    if st != 1 or 'R2041' not in log or os.path.exists(out):
        bad('runhc on a jump to no topic: exit %d, %s written: %s'
            % (st, 'a .HLP' if os.path.exists(out) else 'nothing',
               log.strip()))
    fake = os.path.join(work, 'FAKEHC')
    os.makedirs(fake)
    with open(os.path.join(fake, 'HC.EXE'), 'wb') as f:
        f.write(b'MZ')
    for what, env in [('an HC.EXE not pinned', {'HC_DIR': fake}),
                      ('HC_TIMER=010', {'HC_TIMER': '010'})]:
        st, log = runhc(os.path.join(d, 'T.HPJ'), out, env)
        if st != 2:
            bad('runhc with %s: exit %d, not 2' % (what, st))


def main(argv):
    if len(argv) != 4:
        sys.stderr.write('hcchk: %s\n' % __doc__.split('\n\n')[1])
        return 2
    built, rtf, hpj, work = argv
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    with open(built, 'rb') as f:
        hlp = f.read()
    shutil.copy(built, os.path.join(work, 'BUILD.HLP'))
    found = structure(hlp, rtf, hpj)
    free = None
    if found is not None:
        try:
            bits, paras = unset(hlp, found)
            free = dict.fromkeys(slack(hlp, found), 0xFF)
            free.update(bits)
        except (KeyError, ValueError, IndexError, struct.error) as e:
            bad('the build\'s file not read: %s' % e)
    compiles(hlp, rtf, hpj, work, free)
    if free is not None:
        stamps(hlp, rtf, hpj, found, bits, paras, work)
    refusals(work)
    for f in fails:
        print('hcchk: ' + f)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
