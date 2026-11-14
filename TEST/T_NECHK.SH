#!/bin/sh
# T_NECHK.SH - host test of TEST/NECHK.PY on synthetic Windows programs.
#
# usage: sh TEST/T_NECHK.SH      (run from the repository root)
#
# Each check writes, in a scratch directory under OBJ/, a program of a
# DOS header and an NE header that holds what the script must accept: an
# application marked for protected mode only, a code and a data segment,
# a local heap of 1024 and a stack of 8192 bytes, and a code segment that
# imports KERNEL.132 by ordinal and USER _WSPRINTF by name, both in a
# list of exports beside it, and a group icon named INVWIN; or that
# program with one field changed.  It checks that:
#   - the program exits 0, given its heap, stack, the list and its icon;
#   - each fault exits 1: a target system other than Windows, the
#     expected version 3.10, a library, one data segment for all
#     instances, no mark for protected mode only, a fixed or a
#     nondiscardable code segment, a fixed or a discardable data segment,
#     a code segment as the automatic data segment, no automatic data
#     segment, a heap or a stack of another size, an import by ordinal and
#     one by name that the list lacks, also after a code segment of 64 KB
#     (size 0); without the list the ordinal it lacks exits 0, as it does
#     in records of a segment not flagged as having them; an icon of
#     another name, and a program without resources, given the icon,
#     exit 1;
#   - a file without the DOS header, one without the NE header, an import
#     from a module the program does not name (reported as such), a list
#     line not "MODULE.ordinal NAME" and a missing file exit 2, as do a
#     switch without its value or with a value not a number and a second
#     file.
#
# Exits 0 when every check passes, 1 when one fails, 2 on a setup error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

script=TEST/NECHK.PY
[ -f "$script" ] || die "run from the repository root"
py=$(command -v python3) || die "python3 not installed"

mkdir -p OBJ || die "cannot create OBJ"
work=$(mktemp -d OBJ/t_nechk.XXXXXX) || die "cannot create a scratch directory"
trap 'rm -rf "$work"' EXIT
on_signals

# mkne.py OUT [FIELD=VALUE...] writes the program to OUT, each FIELD
# (a key of f) given VALUE instead.  The code segment, csize bytes at
# file offset 100h (sector 10h, shift 4; 64 KB for 0), is followed by
# its two relocation records: one for module 1 (KERNEL) and ordinal ord, one for module mod
# (USER when 2) and the imported name iname.  The resource table, at
# offset 70h of the NE header, holds a group icon named icon, or nothing
# for an empty icon.
cat >"$work/mkne.py" <<'EOF'
import struct
import sys

f = dict(sig=1, target=2, winver=0x0300, flags=0x030A, auto=2,
         code=0x1D50, data=0x0C51, heap=1024, stack=8192,
         csize=16, ord=132, mod=2, iname='_WSPRINTF', icon='INVWIN')
for arg in sys.argv[2:]:
    key, value = arg.split('=')
    f[key] = value if isinstance(f[key], str) else int(value, 0)
names = b'\x00'
offsets = []
for name in (b'KERNEL', b'USER', f['iname'].encode()):
    offsets.append(len(names))
    names += bytes([len(name)]) + name
mz = bytearray(0x40)
mz[0:2] = b'MZ'
struct.pack_into('<I', mz, 0x3C, 0x40)
ne = bytearray(0x40)
ne[0:2] = b'NE' if f['sig'] else b'XX'
struct.pack_into('<HHHH', ne, 0x0C, f['flags'], f['auto'], f['heap'],
                 f['stack'])
struct.pack_into('<HH', ne, 0x1C, 2, 2)
struct.pack_into('<H', ne, 0x22, 0x40)
struct.pack_into('<HH', ne, 0x28, 0x50, 0x54)
struct.pack_into('<H', ne, 0x32, 4)
rsrc = b''
if f['icon']:
    rsrc = (struct.pack('<HHHI', 4, 0x800E, 1, 0) +
            struct.pack('<HHHHI', 0, 0, 0, 24, 0) + struct.pack('<H', 0) +
            bytes([len(f['icon'])]) + f['icon'].encode())
struct.pack_into('<HH', ne, 0x24, 0x70, 0x70 + len(rsrc))
ne[0x36] = f['target']
struct.pack_into('<H', ne, 0x3E, f['winver'])
segs = struct.pack('<HHHHHHHH', 0x10, f['csize'], f['code'], f['csize'],
                   0, 0, f['data'], 0)
mods = struct.pack('<HH', offsets[0], offsets[1])
head = (bytes(mz) + bytes(ne) + segs + mods + names).ljust(0xB0, b'\x00')
head += rsrc
relocs = struct.pack('<HBBHHHBBHHH', 2, 3, 1, 0, 1, f['ord'],
                     3, 2, 4, f['mod'], offsets[2])
with open(sys.argv[1], 'wb') as out:
    out.write(head.ljust(0x100, b'\x00') + bytes(f['csize'] or 0x10000) +
              relocs)
EOF
printf '# exports\nKERNEL.132 GETWINFLAGS\nUSER.420 _WSPRINTF\n' >"$work/EXP.TXT"

fails=0
checks=0
# expect WHAT STATUS FILE [ARG...]: run the script on FILE with ARGs
# before it, and count a failure unless it exits with STATUS.
expect() {
    what=$1
    want=$2
    file=$3
    shift 3
    checks=$((checks + 1))
    rc=0
    "$py" -I "$script" "$@" "$file" >"$work/out" 2>&1 || rc=$?
    [ "$rc" = "$want" ] && return
    echo "$prog: FAIL: $what: status $rc, expected $want" >&2
    sed 's/^/    /' "$work/out" >&2
    fails=$((fails + 1))
}

# program NAME [FIELD=VALUE...]: write the program $work/NAME.EXE.
program() {
    name=$1
    shift
    "$py" -I "$work/mkne.py" "$work/$name.EXE" "$@" ||
        die "cannot write the program $name"
}

# fault WHAT STATUS FIELD=VALUE...: the program with each FIELD changed,
# given the list, must exit with STATUS.
fault() {
    what=$1
    want=$2
    shift 2
    program BAD "$@"
    expect "$what" "$want" "$work/BAD.EXE" --imports "$work/EXP.TXT"
}

program OK
expect "the program" 0 "$work/OK.EXE" --heap 1024 --stack 8192 \
    --imports "$work/EXP.TXT" --icon INVWIN
expect "another icon" 1 "$work/OK.EXE" --icon WSMOKE
program NORES icon=
expect "no resources" 1 "$work/NORES.EXE" --icon INVWIN
fault "target OS/2" 1 target=1
fault "Windows 3.10" 1 winver=0x030A
fault "a library" 1 flags=0x830A
fault "single data" 1 flags=0x0309
fault "not protected mode only" 1 flags=0x0302
fault "fixed code" 1 code=0x1D40
fault "nondiscardable code" 1 code=0x0D50
fault "fixed data" 1 data=0x0C41
fault "discardable data" 1 data=0x1C51
fault "code as automatic data" 1 auto=1
fault "no automatic data" 1 auto=0
expect "another heap" 1 "$work/OK.EXE" --heap 2048
expect "another stack" 1 "$work/OK.EXE" --stack 4096
fault "an ordinal not listed" 1 ord=207
fault "a name not listed" 1 iname=NOSUCH
fault "an ordinal not listed, 64 KB code" 1 csize=0 ord=207
fault "an ordinal not listed, records not flagged" 0 code=0x1C50 ord=207
program BAD ord=207
expect "an ordinal not listed, no list" 0 "$work/BAD.EXE"
fault "a module not named" 2 mod=3
grep -q 'names module 3 of 2' "$work/out" || {
    echo "$prog: FAIL: a module not named: not reported" >&2
    fails=$((fails + 1))
}

printf 'MZ' >"$work/SHORT.EXE"
expect "no DOS header" 2 "$work/SHORT.EXE"
program NONE sig=0
expect "no NE header" 2 "$work/NONE.EXE"
printf 'KERNEL132 GETWINFLAGS\n' >"$work/BADLIST.TXT"
expect "a bad list line" 2 "$work/OK.EXE" --imports "$work/BADLIST.TXT"
expect "a missing file" 2 "$work/MISSING.EXE"
expect "a switch without its value" 2 "$work/OK.EXE" --heap
expect "a value not a number" 2 "$work/OK.EXE" --heap many
expect "a second file" 2 "$work/OK.EXE" "$work/OK.EXE"

if [ "$fails" -ne 0 ]; then
    echo "$prog: $fails of $checks checks failed" >&2
    exit 1
fi
echo "$prog: $checks checks passed"
