#!/bin/sh
# T_BOOT.SH - test of what TEST/INTEG/boot310.sh does to the shell of the
# boot floppy and of what it reports on a time-out.
#
# usage: sh TEST/T_BOOT.SH SHELLCHK      (run from the repository root;
#        SHELLCHK is BIN/SHELLCHK.COM)
#
# The boot floppy must be the Compaq MS-DOS 3.10 disk, whose COMMAND.COM
# has the SHA-256 SHELL310_SHA.  A copy of that shell, VAR.COM, differs in
# one byte of a message, so an unknown one.  First, on the host,
# shell_patch of TEST/shlib.sh, on a copy of each shell of the 3.10, 3.30
# and 5.00 disks there are: status 0, shell_sum the shell's SHA-256, the
# copy changed exactly as PATCH_CMP, PATCH330_CMP or PATCH500_CMP says; on
# VAR.COM: status 1, shell_sum its SHA-256, the file kept byte for byte,
# nothing printed; DOS_SHELLS lists exactly those three shells.  VAR.IMG,
# a copy of the floppy, holds VAR.COM.  These boots run side by side,
# under QEMU unless DOSBox-X is named:
#   - SHELLCHK then ECHO SHELL OK, under QEMU and under DOSBox-X: exit 0
#     and SHELL OK, so the shell reloaded the transient part SHELLCHK
#     garbled while keeping its word sum; nothing on standard error;
#   - the same on VAR.IMG with a limit of 30 s: the unknown shell is
#     named with its SHA-256 on standard error and, trusting its sum,
#     hangs: status 200, no SHELL OK, the time-out line, then the CPU line;
#   - VER and CHKDSK, the floppy kept: exit 0, the DOS version; the kept
#     COMMAND.COM differs from the original exactly as PATCH_CMP says;
#   - the same on VAR.IMG: exit 0 and the DOS version, so the unknown
#     shell runs normally otherwise; its COMMAND.COM is kept byte for
#     byte; CHKDSK shows the free memory of the patched shell, so the
#     patch changes no resident size;
#   - VER on a copy of the floppy without COMMAND.COM: the missing shell
#     is named on standard error;
#   - PAUSE, which waits on the CTTY console, with a limit of 10 s under
#     QEMU, time for QEMU to make its monitor socket on a loaded host,
#     and 5 s under DOSBox-X: status 200, the time-out line, then, under
#     QEMU, the CPU line ("CPU at SEG:OFF, SS:SP SEG:OFF, interrupts on"
#     or "off"), the floppy kept and no socket (QEMU's monitor) in it, as
#     QEMU, killed, does not remove its own; under DOSBox-X, no CPU line;
#   - on the MS-DOS 3.30 floppy (DOS330_FLOPPY of TEST/shlib.sh), whose
#     COMMAND.COM has the SHA-256 SHELL330_SHA, SHELLCHK then ECHO SHELL
#     OK: exit 0 and SHELL OK, nothing on standard error; and VER, the
#     floppy kept: exit 0, the DOS version, the kept COMMAND.COM differs
#     from the original exactly as PATCH330_CMP says; both skipped, and
#     said so, when that image is absent;
#   - SHELLCHK then ECHO SHELL OK on disk 1 of MS-DOS 5.00 (DOS500_DIR of
#     TEST/shlib.sh), whose COMMAND.COM has the SHA-256 SHELL500_SHA: exit
#     0 and SHELL OK, nothing on standard error; skipped, and said so,
#     when it is absent.
# No boot changes a floppy image it was given.  Afterwards no socket is
# left in any boot's own TMPDIR, in a kept directory, or in
# TEST/INTEG/vm/ outside the boot310.* scratch directories (those of
# runs still going may hold theirs); and no boot that is not kept leaves
# its scratch directory under TEST/INTEG/vm/, found by a file of this
# run, MARK, on A:.
#
# Exits 0 when every check passes, 1 when one fails, 2 on a setup error,
# 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

# The Compaq MS-DOS 3.10 Rev C shell, the one boot310.sh patches.
SHELL310_SHA=55dd70c1658cd9d1f9bf3ebc0334901e182f2f5943ca99397743ac4603a62beb
# cmp -l of the original against the patched shell: bytes 509 and 510
# (counted from 1), the JZ short that skips the reload (74h 16h), made
# two NOPs (90h), all in octal.
PATCH_CMP='509 164 220
510 26 220'
# The MS-DOS 3.30 shell, and its patch: the same JZ at bytes 590 and 591.
SHELL330_SHA=cd76da9fa330349af658e9568b0b0b0d6789cc0ad73d0c10d3465cded69e9f05
PATCH330_CMP='590 164 220
591 26 220'
# The MS-DOS 5.00 shell, and its JZ at bytes 3755 and 3756.
SHELL500_SHA=74ad25067e7d9f5c3a0ac2f2967347defb0ffef9e6ed60b60e6499f0b1a8cc89
PATCH500_CMP='3755 164 220
3756 26 220'
# The offset of the byte VAR.IMG's shell changes: the B of "Bad command
# or file name", made a b.
VAR_AT=18000
# The tail of the line that names an unknown shell.
NOT_OURS="it reloads its transient part only when that part's sum has changed"
TIMED_OUT='^boot310\.sh: timed out after [0-9]+s'
HEX4='[0-9A-F][0-9A-F][0-9A-F][0-9A-F]'
CPU_LINE="^boot310\\.sh: CPU at $HEX4:$HEX4, SS:SP $HEX4:$HEX4, interrupts (on|off)\$"
DOS_VER='DOS Version +3\.'

[ $# -eq 1 ] || die "usage: sh TEST/T_BOOT.SH SHELLCHK"
shellchk=$1
[ -f TEST/INTEG/boot310.sh ] || die "run from the repository root"
[ -f "$shellchk" ] || die "no such file: $shellchk"
[ -f "$DOS310_FLOPPY" ] || die "boot image not found: $DOS310_FLOPPY"
for t in mcopy mdel cmp sha256sum; do
    command -v $t >/dev/null || die "$t not installed"
done
export MTOOLS_SKIP_CHECK=1

work=$(mktemp -d "${TMPDIR:-/tmp}/t_boot.XXXXXX") || die "cannot create a scratch directory"
pids=
cleanup() {
    for f in "$work"/*.err; do
        [ -f "$f" ] || continue
        sed -n 's/^boot310\.sh: kept //p' "$f" | while IFS= read -r d; do
            rm -rf "$d"
        done
    done
    rm -rf "$work"
}
stop() {
    [ -z "$pids" ] || kill $pids 2>/dev/null || true
    for p in $pids; do wait "$p" 2>/dev/null || true; done
}
trap cleanup EXIT
on_signals stop

fails=0
checks=0

# bad WHAT - count a failed check named WHAT.
bad() {
    echo "$prog: FAIL: $1" >&2
    fails=$((fails + 1))
}

# check WHAT CMD... - count a check, failed unless CMD succeeds.
check() {
    checks=$((checks + 1))
    c_what=$1
    shift
    "$@" || bad "$c_what"
}

# shell_of IMAGE OUT - copy IMAGE's COMMAND.COM to OUT.
shell_of() {
    mcopy -n -i "$1" ::COMMAND.COM "$2" || die "cannot read COMMAND.COM of $1"
}

# sha FILE - the SHA-256 of FILE.
sha() {
    sha256sum <"$1" | cut -d' ' -f1
}

# variant IMAGE - a writable copy of the boot floppy as IMAGE.
variant() {
    { cp "$DOS310_FLOPPY" "$1" && chmod u+w "$1"; } || die "cannot copy $DOS310_FLOPPY"
}

# boot NAME ARG... - start boot310.sh ARG... in the background with a
# TMPDIR of its own, NAME.tmp, its standard output in NAME.out and its
# standard error in NAME.err.
boot() {
    b_name=$1
    shift
    mkdir "$work/$b_name.tmp"
    TMPDIR=$work/$b_name.tmp sh TEST/INTEG/boot310.sh "$@" \
        >"$work/$b_name.out" 2>"$work/$b_name.err" &
    pids="$pids $!"
    eval "pid_$b_name=\$!"
}

# finish NAME - wait for boot NAME and set rc to its exit status.
finish() {
    eval "f_pid=\$pid_$1"
    rc=0
    wait "$f_pid" || rc=$?
}

# kept NAME - the scratch directory boot NAME kept.
kept() {
    sed -n 's/^boot310\.sh: kept //p' "$work/$1.err"
}

# has FILE ERE - true when a line of FILE, under the scratch directory,
# matches ERE.
has() {
    grep -Eq "$2" "$work/$1"
}

# lacks FILE ERE - true when no line of FILE matches ERE.
lacks() {
    ! has "$@"
}

# only_kept NAME - true when boot NAME's standard error holds no line but
# the one that names its kept directory.
only_kept() {
    [ -z "$(grep -v '^boot310\.sh: kept ' "$work/$1.err")" ]
}

# says NAME LINE - true when boot NAME's standard error holds LINE.
says() {
    grep -Fqx "$2" "$work/$1.err"
}

# not_ours IMAGE SUM - the line that names IMAGE's unknown shell.
not_ours() {
    echo "boot310.sh: COMMAND.COM of $1 is not a shell this script patches (SHA-256 $2); $NOT_OURS"
}

# memory NAME - the free-memory lines of CHKDSK in boot NAME's output.
memory() {
    sed -n '/bytes total memory/{p;n;p;}' "$work/$1.out"
}

# same_memory A B - true when boots A and B show the same CHKDSK memory.
same_memory() {
    [ -n "$(memory "$1")" ] && [ "$(memory "$1")" = "$(memory "$2")" ]
}

# then_cpu NAME - true when the time-out line of boot NAME's standard
# error is followed by the CPU line.
then_cpu() {
    re_t=$TIMED_OUT re_c=$CPU_LINE awk '
        found { ok = $0 ~ ENVIRON["re_c"]; exit }
        $0 ~ ENVIRON["re_t"] { found = 1 }
        END { exit !ok }' "$work/$1.err"
}

# same_cmp A B WANT - true when cmp -l A B prints the lines WANT, spacing
# aside (WANT empty: the files are equal).
same_cmp() {
    sc_got=$(cmp -l "$1" "$2" 2>&1 | sed 's/^ *//; s/  */ /g') || true
    [ "$sc_got" = "$3" ]
}

# try_patch FILE - shell_patch on FILE, in a subshell, as it may exit: sets
# rc to its status, and writes its shell_sum to FILE.sum and what it
# prints to FILE.out.
try_patch() {
    rc=0
    (p_rc=0
     shell_patch "$1" || p_rc=$?
     echo "${shell_sum:-}" >"$1.sum"
     exit $p_rc) >"$1.out" 2>&1 || rc=$?
}

# patched WHAT SHELL WANT - shell_patch on a copy of SHELL, the shell of
# WHAT: status 0, shell_sum SHELL's SHA-256, and the copy differing from
# SHELL as the cmp -l lines WANT say.
patched() {
    cp "$2" "$2.P"
    try_patch "$2.P"
    check "shell_patch on $1: status $rc, expected 0" [ "$rc" = 0 ]
    check "shell_patch on $1: shell_sum not the shell's SHA-256" \
        [ "$(cat "$2.P.sum")" = "$(sha "$2")" ]
    check "shell_patch on $1: bytes changed other than those expected" \
        same_cmp "$2" "$2.P" "$3"
}

# shell_ok NAME WHAT - the checks of SHELLCHK ECHO boot NAME, named WHAT,
# on a shell that reloads: exit 0, SHELL OK, nothing on standard error.
shell_ok() {
    finish "$1"
    check "$2: status $rc, expected 0" [ "$rc" = 0 ]
    check "$2: no SHELL OK" has "$1.out" '^SHELL OK *$'
    check "$2: standard error not empty" [ ! -s "$work/$1.err" ]
}

# kept_shell NAME OUT - copy the COMMAND.COM of boot NAME's kept floppy to
# OUT; false when the boot kept none.
kept_shell() {
    ks_dir=$(kept "$1")
    [ -f "$ks_dir/A.IMG" ] && shell_of "$ks_dir/A.IMG" "$2"
}

# no_socket NAME - true when boot NAME's TMPDIR and kept directory hold
# no socket.
no_socket() {
    ns_dir=$(kept "$1")
    [ -z "$(find "$work/$1.tmp" ${ns_dir:+"$ns_dir"} -type s)" ]
}

shell_of "$DOS310_FLOPPY" "$work/C310.COM"
[ "$(sha "$work/C310.COM")" = $SHELL310_SHA ] ||
    die "the boot floppy's COMMAND.COM is not the Compaq MS-DOS 3.10 one: $DOS310_FLOPPY"
sha310=$(sha "$DOS310_FLOPPY")

var=$work/VAR.IMG
variant "$var"
cp "$work/C310.COM" "$work/VAR.COM"
printf b | dd of="$work/VAR.COM" bs=1 seek=$VAR_AT conv=notrunc 2>/dev/null
mcopy -o -i "$var" "$work/VAR.COM" ::COMMAND.COM || die "cannot write $var"
varsum=$(sha "$work/VAR.COM")
shavar=$(sha "$var")

noshell=$work/NOSHELL.IMG
variant "$noshell"
mdel -i "$noshell" ::COMMAND.COM || die "cannot write $noshell"

patched "MS-DOS 3.10" "$work/C310.COM" "$PATCH_CMP"
cp "$work/VAR.COM" "$work/VARP.COM"
try_patch "$work/VARP.COM"
what="shell_patch on an unknown shell"
check "$what: status $rc, expected 1" [ "$rc" = 1 ]
check "$what: shell_sum not its SHA-256" \
    [ "$(cat "$work/VARP.COM.sum")" = "$varsum" ]
check "$what: the file changed" same_cmp "$work/VAR.COM" "$work/VARP.COM" ''
check "$what: something printed" [ ! -s "$work/VARP.COM.out" ]
known=$(printf '%s\n' "${DOS_SHELLS:-}" | awk '{ print $1 }' | sort)
want=$(printf '%s\n' $SHELL310_SHA $SHELL330_SHA $SHELL500_SHA | sort)
check "DOS_SHELLS lists other than the MS-DOS 3.10, 3.30 and 5.00 shells" \
    [ "$known" = "$want" ]
if [ -f "$DOS330_FLOPPY" ]; then
    shell_of "$DOS330_FLOPPY" "$work/C330.COM"
    [ "$(sha "$work/C330.COM")" = $SHELL330_SHA ] ||
        die "the MS-DOS 3.30 floppy's COMMAND.COM is not the one this" \
            "test knows: $DOS330_FLOPPY"
    sha330=$(sha "$DOS330_FLOPPY")
    patched "MS-DOS 3.30" "$work/C330.COM" "$PATCH330_CMP"
fi
dos500=
if [ -d "$DOS500_DIR" ]; then
    dos500=$(find_ci "$DOS500_DIR" DISK01.IMG)
    shell_of "$dos500" "$work/C500.COM"
    [ "$(sha "$work/C500.COM")" = $SHELL500_SHA ] ||
        die "the MS-DOS 5.00 disk's COMMAND.COM is not the one this test" \
            "knows: $dos500"
    sha500=$(sha "$dos500")
    patched "MS-DOS 5.00" "$work/C500.COM" "$PATCH500_CMP"
fi

mark=$work/MARK
echo "T_BOOT.SH $work" >"$mark"

boot shell -t 60 -f "$shellchk" -f "$mark" SHELLCHK 'ECHO SHELL OK'
boot dbshell -e dosbox -t 60 -f "$shellchk" -f "$mark" SHELLCHK 'ECHO SHELL OK'
boot varshell -t 30 -i "$var" -f "$shellchk" -f "$mark" SHELLCHK 'ECHO SHELL OK'
boot ver -k -t 30 VER CHKDSK
boot varver -k -t 30 -i "$var" VER CHKDSK
boot noshell -t 5 -i "$noshell" -f "$mark" VER
boot pause -k -t 10 PAUSE
boot dbpause -e dosbox -t 5 -f "$mark" PAUSE
if [ -f "$DOS330_FLOPPY" ]; then
    boot shell330 -t 60 -i "$DOS330_FLOPPY" -f "$shellchk" -f "$mark" \
        SHELLCHK 'ECHO SHELL OK'
    boot dos330 -k -t 30 -i "$DOS330_FLOPPY" VER
else
    echo "$prog: MS-DOS 3.30 shell check skipped, no image: $DOS330_FLOPPY"
fi
if [ -n "$dos500" ]; then
    boot shell500 -t 60 -i "$dos500" -f "$shellchk" -f "$mark" \
        SHELLCHK 'ECHO SHELL OK'
else
    echo "$prog: MS-DOS 5.00 shell check skipped, no disk: $DOS500_DIR"
fi

shell_ok shell "SHELLCHK ECHO"
shell_ok dbshell "SHELLCHK ECHO under DOSBox-X"

finish varshell
what="SHELLCHK ECHO on an unknown shell"
check "$what: status $rc, expected 200" [ "$rc" = 200 ]
check "$what: SHELL OK although the shell was not patched" lacks varshell.out '^SHELL OK'
check "$what: the shell not named" says varshell "$(not_ours "$var" "$varsum")"
check "$what: no time-out line followed by the CPU line" then_cpu varshell

finish ver
check "VER CHKDSK: status $rc, expected 0" [ "$rc" = 0 ]
check "VER CHKDSK: no DOS version line" has ver.out "$DOS_VER"
check "VER CHKDSK: standard error holds more than the kept line" only_kept ver
if kept_shell ver "$work/VER.COM"; then
    check "the patched COMMAND.COM: bytes changed other than those of PATCH_CMP" \
        same_cmp "$work/C310.COM" "$work/VER.COM" "$PATCH_CMP"
else
    check "VER CHKDSK: no kept floppy" false
fi
check "VER CHKDSK: a socket left" no_socket ver

finish varver
what="VER CHKDSK on an unknown shell"
check "$what: status $rc, expected 0" [ "$rc" = 0 ]
check "$what: no DOS version line" has varver.out "$DOS_VER"
check "$what: the shell not named" says varver "$(not_ours "$var" "$varsum")"
check "$what: no CHKDSK memory lines, or not those of the patched shell" \
    same_memory varver ver
if kept_shell varver "$work/VARK.COM"; then
    check "$what: COMMAND.COM changed" same_cmp "$work/VAR.COM" "$work/VARK.COM" ''
else
    check "$what: no kept floppy" false
fi
check "$what: a socket left" no_socket varver
check "VAR.IMG changed" [ "$(sha "$var")" = "$shavar" ]

finish noshell
check "VER without COMMAND.COM: the missing shell not named" \
    says noshell "boot310.sh: $noshell has no COMMAND.COM to patch"

finish pause
check "PAUSE: status $rc, expected 200" [ "$rc" = 200 ]
check "PAUSE: no time-out line followed by the CPU line" then_cpu pause
if [ -d "$(kept pause)" ]; then
    check "PAUSE: a socket left in the kept directory" no_socket pause
else
    check "PAUSE: no kept directory" false
fi

finish dbpause
check "PAUSE under DOSBox-X: status $rc, expected 200" [ "$rc" = 200 ]
check "PAUSE under DOSBox-X: no time-out line" has dbpause.err "$TIMED_OUT"
check "PAUSE under DOSBox-X: a CPU line" lacks dbpause.err '^boot310\.sh: CPU at'

if [ -f "$DOS330_FLOPPY" ]; then
    shell_ok shell330 "SHELLCHK ECHO on MS-DOS 3.30"
    finish dos330
    what="VER on MS-DOS 3.30"
    check "$what: status $rc, expected 0" [ "$rc" = 0 ]
    check "$what: no DOS version line" has dos330.out "$DOS_VER"
    check "$what: standard error holds more than the kept line" \
        only_kept dos330
    if kept_shell dos330 "$work/C330K.COM"; then
        check "$what: bytes changed other than those of PATCH330_CMP" \
            same_cmp "$work/C330.COM" "$work/C330K.COM" "$PATCH330_CMP"
    else
        check "$what: no kept floppy" false
    fi
    check "the MS-DOS 3.30 floppy image changed" [ "$(sha "$DOS330_FLOPPY")" = "$sha330" ]
fi
if [ -n "$dos500" ]; then
    shell_ok shell500 "SHELLCHK ECHO on MS-DOS 5.00"
    check "the MS-DOS 5.00 disk image changed" [ "$(sha "$dos500")" = "$sha500" ]
fi
pids=
check "the MS-DOS 3.10 floppy image changed" [ "$(sha "$DOS310_FLOPPY")" = "$sha310" ]

checks=$((checks + 1))
left=$(find "$work" TEST/INTEG/vm -type s ! -path 'TEST/INTEG/vm/boot310.*/*' 2>/dev/null) || true
[ -z "$left" ] || bad "sockets left: $(echo $left)"
checks=$((checks + 1))
left=$(grep -lF "T_BOOT.SH $work" TEST/INTEG/vm/*/A.IMG 2>/dev/null) || true
[ -z "$left" ] || bad "scratch directories left under TEST/INTEG/vm/: $(echo $left)"

if [ $fails -ne 0 ]; then
    for f in "$work"/*.out "$work"/*.err; do
        echo "--- ${f##*/}"
        cat "$f"
    done
    echo "$prog: $fails of $checks checks failed" >&2
    exit 1
fi
echo "$prog: $checks checks passed"
