#!/bin/sh
# T_ASMCK.SH - host test of TEST/ASMCHK.PY on small source pairs.
#
# usage: sh TEST/T_ASMCK.SH      (run from the repository root)
#
# Needs nasm, as the script does.  Each check writes a nasm source and a C
# file holding its bytes in a scratch directory under OBJ/ and runs the
# script on them.  It checks that:
#   - matching bytes, given as literals, #define names and expressions,
#     with a NAME of the same value in both files, exit 0, also when a
#     name comes from a header the C file includes and when the C file
#     defines again a name of that header, whose own value then counts;
#   - a differing byte, an extra byte and a NAME whose values differ
#     exit 1;
#   - an element outside 0..255, a NAME the source does not define, a
#     NAME the C file does not #define, a source nasm refuses and an
#     array the C file lacks exit 2, as does a wrong number of arguments.
#
# Exits 0 when every check passes, 1 when one fails, 2 on a setup error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

script=TEST/ASMCHK.PY
[ -f "$script" ] || die "run from the repository root"
py=$(command -v python3) || die "python3 not installed"
command -v nasm >/dev/null || die "nasm not installed"

mkdir -p OBJ || die "cannot create OBJ"
work=$(mktemp -d OBJ/t_asmck.XXXXXX) || die "cannot create a scratch directory"
trap 'rm -rf "$work"' EXIT
on_signals

fails=0
checks=0
# expect WHAT STATUS ASM C [NAME...]: run the script on ASM and the array
# up of C, and count a failure unless it exits with STATUS.
expect() {
    what=$1
    want=$2
    shift 2
    checks=$((checks + 1))
    rc=0
    "$py" -I "$script" "$work/$1" "$work/$2" up "$3" 2>"$work/err" || rc=$?
    [ "$rc" = "$want" ] && return
    echo "$prog: FAIL: $what: status $rc, expected $want" >&2
    sed 's/^/    /' "$work/err" >&2
    fails=$((fails + 1))
}

# The source: 4 bytes, PATCH the offset of the operand of its MOV.
cat >"$work/ok.asm" <<'EOF'
        cpu     8086
        org     0
LEN     equ     6
        push    ds
PATCH   equ     $ + 1
        mov     bx, 0
EOF
cat >"$work/ok.c" <<'EOF'
#define LEN     6               /* as in the source */
#define PATCH   02
static unsigned char up[] = {
    0x1E,                       /* push ds */
    0xBB, LEN - 6, (-LEN + 6) & 0xFFu
};
EOF
sed 's/0x1E,/0x1F,/' "$work/ok.c" >"$work/byte.c"
sed 's/0xFFu$/0xFFu, 0x90/' "$work/ok.c" >"$work/extra.c"
sed 's/^#define PATCH   02$/#define PATCH   3/' "$work/ok.c" >"$work/name.c"
sed 's/0x1E,/0x11E,/' "$work/ok.c" >"$work/range.c"
sed '/^#define PATCH/d' "$work/ok.c" >"$work/nodef.c"
sed 's/up\[\]/down[]/' "$work/ok.c" >"$work/noarr.c"
sed 's/push    ds/push    dx, dx/' "$work/ok.asm" >"$work/bad.asm"
sed 's/^PATCH/PATCHED/' "$work/ok.asm" >"$work/noname.asm"
printf '#define LEN     6\n#define PATCH   3\n' >"$work/ok.h"
sed 's/^#define LEN .*/#include "ok.h"/' "$work/ok.c" >"$work/hdr.c"

expect "match" 0 ok.asm ok.c PATCH
expect "header constants" 0 ok.asm hdr.c PATCH
expect "differing byte" 1 ok.asm byte.c PATCH
expect "extra byte" 1 ok.asm extra.c PATCH
expect "NAME differs" 1 ok.asm name.c PATCH
expect "element out of range" 2 ok.asm range.c PATCH
expect "NAME not in the source" 2 noname.asm ok.c PATCH
expect "NAME not #defined" 2 ok.asm nodef.c PATCH
expect "nasm error" 2 bad.asm ok.c PATCH
expect "no array" 2 ok.asm noarr.c PATCH

checks=$((checks + 1))
rc=0
"$py" -I "$script" "$work/ok.asm" "$work/ok.c" 2>/dev/null || rc=$?
if [ "$rc" != 2 ]; then
    echo "$prog: FAIL: two arguments: status $rc, expected 2" >&2
    fails=$((fails + 1))
fi

if [ $fails -ne 0 ]; then
    echo "$prog: $fails of $checks checks failed" >&2
    exit 1
fi
echo "$prog: $checks checks passed"
