#!/bin/sh
# shlgate.sh - check that the image builders patch the shells of the
# two-VM images, and that the patch is what keeps those shells running
# after a program that garbled their transient part.
#
# usage: sh TEST/INTEG/shlgate.sh SHELLCHK
#
#   SHELLCHK  BIN/SHELLCHK.COM, which garbles the 64 KB below the top of
#             memory while keeping every word sum the shells check
#
# COMMAND.COM reloads its transient part only when a word sum of it has
# changed.  img_shell of imglib.sh, which every image build runs, makes
# two NOPs of the jump that skips the reload in C:\COMMAND.COM and
# C:\DOS\COMMAND.COM when the file is a shell it knows by its SHA-256
# (shell_patch and DOS_SHELLS of TEST/shlib.sh), then proves the patch
# with a boot that runs SHELLCHK.  The NetBIOS, WGA and TCP/IP images are
# built first when they are missing or out of date.  Checks:
#   1. both shells of the NetBIOS image differ from the MS-DOS 3.30 disk's
#      COMMAND.COM exactly in the JZ at SHELL330_JZ, both of the WGA image
#      from the MS-DOS 5.00 disk's at SHELL500_JZ, and the TCP/IP image's
#      equal the NetBIOS image's;
#   2. under run2vm.sh, NET START left out and a limit of VM_SECS, SHELLCHK
#      ends with errorlevel 0 on the NetBIOS and on the WGA image, and times
#      out on a copy of either with the disk's own shells put back, on
#      which VER still runs;
#   3. img_shell, run on a writable copy of the NetBIOS image's disks with
#      the 3.30 shells put back: patches both and boots the proof VM; with
#      C:\COMMAND.COM changed in one byte of a message (MSG330_AT), names
#      it on standard error, keeps it byte for byte, patches the other and
#      boots no proof VM; with DOS_SHELLS pointing at that byte instead of
#      the jump, exits 2 once the proof VM times out; and with a SHELLCHK
#      that ends with errorlevel 1 (EXIT1_COM), exits 2 as the proof VM
#      writes no marker.
# The cases of 3 run beside those of 2.  No period disk and no built image
# changes.
#
# env: DOS500_DIR  directory with the MS-DOS 5.00 disk DISK01.IMG, in any
#                  letter case (TEST/shlib.sh)
#      NET330_DIR, WGA500_DIR, TCP330_DIR  the images' directories
#
# Exit status: 0 when every check passes, 1 when one fails, 2 on a setup
# error, 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/wga500.sh"
. "$here/tcp330.sh"

# The MS-DOS 3.30 and 5.00 shells (COMMAND.COM of disk 1) and the file
# offset of the JZ in each that skips the reload when the sum matches.
SHELL330_SHA=cd76da9fa330349af658e9568b0b0b0d6789cc0ad73d0c10d3465cded69e9f05
SHELL330_JZ=589
SHELL500_SHA=74ad25067e7d9f5c3a0ac2f2967347defb0ffef9e6ed60b60e6499f0b1a8cc89
SHELL500_JZ=3754
# The offset in the 3.30 shell of the B of the transient part's "Bad
# command or file name", made a b for an unknown shell.
MSG330_AT=19744
# The limit of each VM in seconds, and what img_shell may take beyond it
# when its proof VM hangs.
VM_SECS=60
SLACK=60
# The tail of the line that names an unknown shell.
NOT_OURS="it reloads its transient part only when that part's sum has changed"
TIMED_OUT='^run2vm\.sh: VM A timed out after [0-9]+s'
SHELLS='COMMAND.COM DOS/COMMAND.COM'
# A stand-in SHELLCHK that only ends with errorlevel 1: mov ax,4C01h;
# int 21h.
EXIT1_COM='\270\001\114\315\041'

[ $# -eq 1 ] || die "usage: sh TEST/INTEG/shlgate.sh SHELLCHK"
[ -f "$1" ] || die "no such file: $1"
shellchk=$(realpath "$1")
need_tools qemu-system-i386 mcopy cmp sha256sum realpath
[ -f "$DOS330_FLOPPY" ] || die "MS-DOS 3.30 disk not found: $DOS330_FLOPPY"
dos500=$(find_ci "$DOS500_DIR" DISK01.IMG)

image_current || sh "$here/mkimg.sh" ||
    die "the NetBIOS image could not be built: run sh TEST/INTEG/mkimg.sh"
wga_current || sh "$here/mkwga.sh" ||
    die "the WGA image could not be built: run sh TEST/INTEG/mkwga.sh"
tcp_current || sh "$here/mktcpc.sh" ||
    die "the TCP/IP image could not be built: run sh TEST/INTEG/mktcpc.sh"

mkdir -p "$here/vm"
scratch=$(mktemp -d "$here/vm/shlgate.XXXXXX") ||
    die "cannot create a scratch directory"
vm_pid=
t3_pids=
# stop [SIGNAL] - kill the run2vm.sh and the img_shell cases still running,
# their img_shell subshells and their proof VMs.
stop() {
    for p in $vm_pid $t3_pids; do kill "$p" 2>/dev/null || :; done
    [ -z "$t3_pids" ] ||
        kill_pidfiles "$scratch"/t3?.pid "$scratch"/t3?/build/qemu.pid
    for p in $vm_pid $t3_pids; do wait "$p" 2>/dev/null || :; done
}
cleanup() {
    stop
    rm -rf "$scratch"
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

# sha FILE - the SHA-256 of FILE.
sha() {
    sha256sum <"$1" | cut -d' ' -f1
}

# shell_of DISK PATH OUT - copy PATH of the partition of DISK to OUT.
shell_of() {
    mcopy -n -i "$(part_img "$1")" "::$2" "$3" || die "cannot read $2 of $1"
}

# put_shell DISK FILE PATH - write FILE as PATH of the partition of DISK.
put_shell() {
    mcopy -o -i "$(part_img "$1")" "$2" "::$3" || die "cannot write $3 of $1"
}

# jz_cmp OFFSET - what cmp -l prints for a shell whose two bytes at file
# offset OFFSET, a JZ short (74h 16h), are made NOPs (90h), in octal.
jz_cmp() {
    printf '%s 164 220\n%s 26 220' $(($1 + 1)) $(($1 + 2))
}

# same_cmp A B WANT - true when cmp -l A B prints the lines WANT, spacing
# aside (WANT empty: the files are equal).
same_cmp() {
    sc_got=$(cmp -l "$1" "$2" 2>&1 | sed 's/^ *//; s/  */ /g') || true
    [ "$sc_got" = "$3" ]
}

# says FILE LINE - true when FILE, under the scratch directory, holds LINE.
says() {
    grep -Fqx -- "$2" "$scratch/$1"
}

# has FILE ERE - true when a line of FILE matches ERE.
has() {
    grep -Eq -- "$2" "$scratch/$1"
}

# not_ours PATH SUM - the line img_shell prints for an unknown PATH.
not_ours() {
    echo "T3: $1 of C: is not a shell this script patches (SHA-256 $2);" \
        "$NOT_OURS"
}

# unpatched DIR MARKER SHELL V - copy the image directory DIR (MARKER,
# BASE.IMG, PAYLOAD.IMG and INPUTS.SHA, so that it stays current) to V,
# with SHELL put back as both COMMAND.COMs of its system disk.
unpatched() {
    mkdir "$4"
    for f in "$2" BASE.IMG PAYLOAD.IMG INPUTS.SHA; do
        cp --sparse=always "$1/$f" "$4/" || die "cannot copy $1/$f"
    done
    chmod u+w "$4/BASE.IMG"
    for s in $SHELLS; do
        put_shell "$4/BASE.IMG" "$3" "$s"
    done
}

# vm NAME CMD VAR=VALUE... - run2vm.sh running CMD on the payload P in VM
# A, with the variables VAR set, its output in NAME.out and NAME.err and
# its result directory out.NAME; sets rc to its exit status.
vm() {
    vm_name=$1
    vm_cmd=$2
    shift 2
    env NETSTART= TIMEOUT=$VM_SECS OUT="$scratch/out.$vm_name" "$@" \
        sh "$here/run2vm.sh" "$scratch/P" "$vm_cmd" \
        >"$scratch/$vm_name.out" 2>"$scratch/$vm_name.err" &
    vm_pid=$!
    rc=0
    wait "$vm_pid" || rc=$?
    vm_pid=
}

# done_code NAME - the errorlevel run2vm.sh's VM A recorded in D:\DONE.TXT.
done_code() {
    tr -dc '0-9' 2>/dev/null <"$scratch/out.$1/A/D/DONE.TXT" || :
}

# t3 NAME [CMDFILE [SHELLS [SHELLCHK]]] - in the background, img_shell on
# a copy of the NetBIOS image's disks in t3NAME, the 3.30 shells put back
# and, with CMDFILE, C:\COMMAND.COM made CMDFILE; with SHELLS, DOS_SHELLS
# is SHELLS; with SHELLCHK, the proof VM runs that file as SHELLCHK.  Its
# standard error goes to t3NAME.err, the img_shell subshell's PID to
# t3NAME.pid, and its exit status and the seconds img_shell took to
# t3NAME.rc.
t3() {
    t3_new=$scratch/t3$1
    mkdir "$t3_new"
    cp --sparse=always "$BASE_IMG" "$PAYLOAD_TEMPLATE" "$t3_new/"
    chmod u+w "$t3_new/BASE.IMG" "$t3_new/PAYLOAD.IMG"
    for s in $SHELLS; do
        put_shell "$t3_new/BASE.IMG" "$scratch/O330.COM" "$s"
    done
    [ -z "${2:-}" ] || put_shell "$t3_new/BASE.IMG" "$2" COMMAND.COM
    mkdir "$t3_new/build"
    t3_t0=$(date +%s)
    (
        . "$here/net330.sh"
        . "$here/imglib.sh"
        prog=T3
        TIMEOUT=$VM_SECS
        qexit=$repo/BIN/QEXIT.COM
        new=$t3_new
        work=$t3_new/build
        [ -z "${3:-}" ] || DOS_SHELLS=$3
        [ -z "${4:-}" ] || shellchk=$4
        img_shell
    ) 2>"$t3_new.err" &
    echo $! >"$t3_new.pid"
    t3_rc=0
    wait $! || t3_rc=$?
    echo "$t3_rc $(($(date +%s) - t3_t0))" >"$t3_new.rc"
}

# t3_rc NAME - the exit status of t3 NAME; t3_secs NAME - its seconds.
t3_rc() {
    cut -d' ' -f1 "$scratch/t3$1.rc"
}
t3_secs() {
    cut -d' ' -f2 "$scratch/t3$1.rc"
}

# --- the originals ----------------------------------------------------------
o330=$scratch/O330.COM
o500=$scratch/O500.COM
mcopy -n -i "$DOS330_FLOPPY" ::COMMAND.COM "$o330" ||
    die "cannot read COMMAND.COM of $DOS330_FLOPPY"
mcopy -n -i "$dos500" ::COMMAND.COM "$o500" ||
    die "cannot read COMMAND.COM of $dos500"
[ "$(sha "$o330")" = $SHELL330_SHA ] ||
    die "COMMAND.COM of $DOS330_FLOPPY is not the MS-DOS 3.30 shell" \
        "this gate knows"
[ "$(sha "$o500")" = $SHELL500_SHA ] ||
    die "COMMAND.COM of $dos500 is not the MS-DOS 5.00 shell this gate" \
        "knows"
at=$(grep -abo 'Bad command' "$o330" | cut -d: -f1 | tail -n 1)
[ "$at" = $MSG330_AT ] ||
    die "no \"Bad command\" at $MSG330_AT in the MS-DOS 3.30 shell"
sum_d330=$(sha "$DOS330_FLOPPY")
sum_d500=$(sha "$dos500")
sum_net=$(sha "$BASE_IMG")
sum_wga=$(sha "$WGA_BASE_IMG")
cmp330=$(jz_cmp $SHELL330_JZ)
cmp500=$(jz_cmp $SHELL500_JZ)

var=$scratch/VAR.COM
cp "$o330" "$var"
printf b | dd of="$var" bs=1 seek=$MSG330_AT conv=notrunc 2>/dev/null
varsum=$(sha "$var")

# --- 3, in the background ---------------------------------------------------
t3 a &
t3_pids=$!
t3 b "$var" &
t3_pids="$t3_pids $!"
t3 c '' "$SHELL330_SHA $MSG330_AT" &
t3_pids="$t3_pids $!"
printf "$EXIT1_COM" >"$scratch/EXIT1.COM"
t3 e '' '' "$scratch/EXIT1.COM" &
t3_pids="$t3_pids $!"

# --- 1 ----------------------------------------------------------------------
n=0
for s in $SHELLS; do
    n=$((n + 1))
    shell_of "$BASE_IMG" "$s" "$scratch/N$n.COM"
    shell_of "$WGA_BASE_IMG" "$s" "$scratch/W$n.COM"
    shell_of "$TCP_BASE_IMG" "$s" "$scratch/T$n.COM"
    check "NetBIOS image C:/$s: not the 3.30 shell patched at $SHELL330_JZ" \
        same_cmp "$o330" "$scratch/N$n.COM" "$cmp330"
    check "WGA image C:/$s: not the 5.00 shell patched at $SHELL500_JZ" \
        same_cmp "$o500" "$scratch/W$n.COM" "$cmp500"
    check "TCP/IP image C:/$s: not the NetBIOS image's" \
        same_cmp "$scratch/N$n.COM" "$scratch/T$n.COM" ''
done

# --- 2 ----------------------------------------------------------------------
mkdir "$scratch/P"
cp "$shellchk" "$scratch/P/SHELLCHK.COM"
unpatched "$NET330_DIR" .net330 "$o330" "$scratch/vnet"
unpatched "$WGA500_DIR" .wga500 "$o500" "$scratch/vwga"

vm net SHELLCHK
check "SHELLCHK on the NetBIOS image: status $rc, expected 0" [ "$rc" = 0 ]
check "SHELLCHK on the NetBIOS image: D:\\DONE.TXT not 0" \
    [ "$(done_code net)" = 0 ]
vm wga SHELLCHK IMAGE_A=wga500
check "SHELLCHK on the WGA image: status $rc, expected 0" [ "$rc" = 0 ]
check "SHELLCHK on the WGA image: D:\\DONE.TXT not 0" \
    [ "$(done_code wga)" = 0 ]
vm vnet SHELLCHK NET330_DIR="$scratch/vnet"
what="SHELLCHK on the NetBIOS image with its own shells"
check "$what: status $rc, expected 1" [ "$rc" = 1 ]
check "$what: no time-out line" has vnet.err "$TIMED_OUT"
check "$what: D:\\DONE.TXT written" [ ! -e "$scratch/out.vnet/A/D/DONE.TXT" ]
vm vwga SHELLCHK IMAGE_A=wga500 WGA500_DIR="$scratch/vwga"
what="SHELLCHK on the WGA image with its own shells"
check "$what: status $rc, expected 1" [ "$rc" = 1 ]
check "$what: no time-out line" has vwga.err "$TIMED_OUT"
check "$what: D:\\DONE.TXT written" [ ! -e "$scratch/out.vwga/A/D/DONE.TXT" ]
vm vver VER NET330_DIR="$scratch/vnet"
what="VER on the NetBIOS image with its own shells"
check "$what: status $rc, expected 0" [ "$rc" = 0 ]

# --- 3 ----------------------------------------------------------------------
for p in $t3_pids; do wait "$p" || :; done
t3_pids=

what="img_shell on the 3.30 shells"
check "$what: status $(t3_rc a), expected 0" [ "$(t3_rc a)" = 0 ]
check "$what: a shell named unknown" \
    [ -z "$(grep -F "$NOT_OURS" "$scratch/t3a.err")" ]
n=0
for s in $SHELLS; do
    n=$((n + 1))
    shell_of "$scratch/t3a/BASE.IMG" "$s" "$scratch/A$n.COM"
    check "$what: C:/$s not patched at $SHELL330_JZ alone" \
        same_cmp "$o330" "$scratch/A$n.COM" "$cmp330"
done
check "$what: no proof VM" [ -f "$scratch/t3a/build/shellchk.log" ]

what="img_shell on an unknown C:/COMMAND.COM"
check "$what: status $(t3_rc b), expected 0" [ "$(t3_rc b)" = 0 ]
check "$what: standard error not the one line that names it" \
    [ "$(cat "$scratch/t3b.err")" = "$(not_ours COMMAND.COM "$varsum")" ]
shell_of "$scratch/t3b/BASE.IMG" COMMAND.COM "$scratch/B1.COM"
shell_of "$scratch/t3b/BASE.IMG" DOS/COMMAND.COM "$scratch/B2.COM"
check "$what: it changed" same_cmp "$var" "$scratch/B1.COM" ''
check "$what: C:/DOS/COMMAND.COM not patched at $SHELL330_JZ alone" \
    same_cmp "$o330" "$scratch/B2.COM" "$cmp330"
check "$what: a proof VM" [ ! -e "$scratch/t3b/build/shellchk.log" ]

what="img_shell with DOS_SHELLS at $MSG330_AT"
check "$what: status $(t3_rc c), expected 2" [ "$(t3_rc c)" = 2 ]
check "$what: $(t3_secs c)s, more than $((VM_SECS + SLACK))s" \
    [ "$(t3_secs c)" -le $((VM_SECS + SLACK)) ]
check "$what: no time-out line" \
    says t3c.err "T3: shellchk VM timed out after ${VM_SECS}s"

what="img_shell with a SHELLCHK that ends with errorlevel 1"
check "$what: status $(t3_rc e), expected 2" [ "$(t3_rc e)" = 2 ]
check "$what: no line saying the proof VM did not complete" says t3e.err \
    "T3: shellchk VM did not complete (QEMU status 1, no ::DONE.TXT marker)"

check "the NetBIOS image changed" [ "$(sha "$BASE_IMG")" = "$sum_net" ]
check "the WGA image changed" [ "$(sha "$WGA_BASE_IMG")" = "$sum_wga" ]
check "the MS-DOS 3.30 disk changed" \
    [ "$(sha "$DOS330_FLOPPY")" = "$sum_d330" ]
check "the MS-DOS 5.00 disk changed" [ "$(sha "$dos500")" = "$sum_d500" ]

if [ $fails -ne 0 ]; then
    for f in "$scratch"/*.out "$scratch"/*.err "$scratch"/t3?/build/*.log; do
        [ -f "$f" ] || continue
        echo "--- ${f#"$scratch"/}"
        cat "$f"
    done
    echo "$prog: $fails of $checks checks failed" >&2
    exit 1
fi
echo "$prog: $checks checks passed"
