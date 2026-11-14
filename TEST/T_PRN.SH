#!/bin/sh
# T_PRN.SH - printing on the Compaq MS-DOS 3.10 of TEST/INTEG/boot310.sh
# under QEMU, whose first parallel port (LPT1) writes what it receives to
# a host file (boot310.sh -p): the bytes that reach the port must be the
# goldens that the host runs of TEST/T_SCR.SH capture with /PRN:.
#
# usage (from the repository root):
#   sh TEST/T_PRN.SH [-m KB] EXE...
#
# For each EXE, an INV.EXE, four boots, each with a fresh printer file:
#   1. T_PRN.EXE of the same build (beside EXE, else in its TEST/
#      subdirectory) with the argument LPT: the byte values 00h..FFh, then
#      "LPT" CR LF, through INT 17h on LPT1, and the same with "PRN"
#      through the DOS device PRN in raw mode; the printer file must hold
#      exactly those two jobs, and T_PRN must pass.
#   2. PRN1: INV with the key script TEST/KEYS/PRN1.KEY and the settings
#      TEST/KEYS/PRNLPT.CFG (LPT1, EPSON); the printer file must equal
#      TEST/GOLDEN/PRT/PRN1.PRN.
#   3. PRN2: the same with PRN2.KEY, against PRN2.PRN.
#   4. PRN1 again with TEST/KEYS/PRNDOS.CFG, printing through the DOS
#      device PRN, against PRN1.PRN.
# The INV boots hold the program, INV.MSG and INV.HLP (beside EXE, else in
# BIN/), the key script, TEST/KEYS/SEED.REQ and the settings file on the
# scratch hard disk C:, the data directory D:\TDATA on a VDISK RAM disk;
# "INV /CMD /LOCAL D:\TDATA" seeds it from SEED.REQ (every answer +OK),
# then "INV /LOCAL D:\TDATA /NOW:20261008120000 /CFG:file /KEYS:file"
# runs the script without /PRN:, so the jobs go to the printer of the
# settings, and must end with status 0.  With -m KB the RAM disk is sized
# by TEST/INTEG/boot310m.sh so that INV starts with KB kilobytes of free
# conventional memory, which CHKDSK checks in the same boot; without -m
# it is the least RAM disk, FREE310_MINKB KB (TEST/INTEG/free310.sh),
# which leaves every build more than it needs.  A file that differs is
# shown with "cmp -l | head"; each boot that passes prints a line
# "T_PRN.SH: EXE: what ran: ok".
#
# Exits 0 when every boot passes, 1 on a failure, 2 on a usage or setup
# error, 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"
. "$here/INTEG/free310.sh"

[ -f SRCS.MK ] && [ -d TEST/KEYS ] || die "run from the repository root"

NOW=/NOW:$SEED_NOW
KEYS=TEST/KEYS
PGOLD=TEST/GOLDEN/PRT
freekb=
if [ "${1:-}" = -m ]; then
    [ $# -ge 3 ] || die "usage: sh TEST/T_PRN.SH [-m KB] EXE..."
    freekb=$2
    shift 2
    case $freekb in ''|0*|*[!0-9]*) die "bad KB: $freekb" ;; esac
fi
[ $# -ge 1 ] || die "usage: sh TEST/T_PRN.SH [-m KB] EXE..."
for f in "$KEYS/PRN1.KEY" "$KEYS/PRN2.KEY" "$KEYS/PRNLPT.CFG" \
         "$KEYS/PRNDOS.CFG" "$KEYS/SEED.REQ" "$PGOLD/PRN1.PRN" \
         "$PGOLD/PRN2.PRN"; do
    [ -f "$f" ] || die "missing $f"
done

scratch=$(mktemp -d /tmp/t_prn.XXXXXX)
trap 'rm -rf "$scratch"' EXIT
on_signals

fails=0
check_fail() { echo "$prog: FAIL: $*" >&2; fails=$((fails + 1)); }
# passed WHAT BEFORE: a line for a boot that added no failure to the
# BEFORE failures.
passed() { [ "$fails" != "$2" ] || echo "$prog: $1: ok"; }

# The two jobs of T_PRN LPT: each the byte values 00h..FFh, a tag, CR LF.
job() {
    i=0
    while [ $i -lt 256 ]; do
        printf "\\$(printf %03o $i)"
        i=$((i + 1))
    done
    printf '%s\r\n' "$1"
}
{ job LPT; job PRN; } >"$scratch/T_PRN.EXP"

if [ -z "$freekb" ]; then
    free310_config "$scratch/CONFIG.SYS" "$FREE310_MINKB"
fi

# boot WHAT LOG PRINTERFILE [boot310.sh options] COMMAND LINE...: one boot
# with its output in LOG; 0 when it ran and its last command line, the
# program under test, ended with status 0 (and with -m the free memory
# check of boot310m.sh passed).
boot() {
    what=$1 log=$2 lpt=$3
    shift 3
    rc=0
    if [ -n "$freekb" ]; then
        sh TEST/INTEG/boot310m.sh "$freekb" -t 300 -p "$lpt" "$@" \
            </dev/null >"$log" 2>&1 || rc=$?
    else
        sh TEST/INTEG/boot310.sh -t 300 -p "$lpt" -f "$scratch/CONFIG.SYS" \
            "$@" </dev/null >"$log" 2>&1 || rc=$?
    fi
    # Both scripts end with the status of the last command line; their
    # own failures are 200 and above, or 1 for boot310m.sh's check.
    if [ $rc -ne 0 ]; then
        check_fail "$what: status $rc"
        cat "$log" >&2
        return 1
    fi
    return 0
}

# same WHAT FILE EXPECTED: FILE must equal EXPECTED byte for byte.
same() {
    if ! cmp -s "$3" "$2"; then
        check_fail "$1: the printer file differs from $3"
        cmp -l "$3" "$2" | head -10 >&2 || true
    fi
}

for exe in "$@"; do
    [ -f "$exe" ] || die "no such file: $exe"
    dir=$(dirname "$exe")
    tprn=$dir/T_PRN.EXE
    [ -f "$tprn" ] || tprn=$dir/TEST/T_PRN.EXE
    [ -f "$tprn" ] || die "no T_PRN.EXE beside $exe or in $dir/TEST/"
    run=$scratch/run
    rm -rf "$run"
    mkdir "$run"
    cp "$exe" "$run/INV.EXE"
    for f in INV.MSG INV.HLP; do
        src=$dir/$f
        [ -f "$src" ] || src=BIN/$f
        [ -f "$src" ] || die "no $f beside $exe or in BIN/"
        cp "$src" "$run/$f"
    done
    cp "$tprn" "$run/T_PRN.EXE"
    for f in PRN1.KEY PRN2.KEY PRNLPT.CFG PRNDOS.CFG SEED.REQ; do
        dos_text "$KEYS/$f" >"$run/$f"
    done

    what="$exe: T_PRN LPT"
    f0=$fails
    if boot "$what" "$run/T_PRN.LOG" "$run/T_PRN.LPT" \
        -h "$run/T_PRN.EXE" 'C:\T_PRN C:\ LPT'; then
        grep -q '^T_PRN PASS' "$run/T_PRN.LOG" ||
            { check_fail "$what: no T_PRN PASS line"
              cat "$run/T_PRN.LOG" >&2; }
        same "$what" "$run/T_PRN.LPT" "$scratch/T_PRN.EXP"
    fi
    passed "$what" "$f0"

    for b in PRN1:PRNLPT.CFG PRN2:PRNLPT.CFG PRN1:PRNDOS.CFG; do
        s=${b%%:*} cfg=${b#*:}
        what="$exe: $s with $cfg"
        log=$run/$s.LOG
        lpt=$run/$s.LPT
        f0=$fails
        if boot "$what" "$log" "$lpt" \
            -h "$run/INV.EXE" -h "$run/INV.MSG" -h "$run/INV.HLP" \
            -h "$run/$s.KEY" -h "$run/SEED.REQ" -h "$run/$cfg" \
            'C:' 'MD D:\TDATA' "INV /CMD /LOCAL D:\\TDATA $NOW <SEED.REQ" \
            "INV /LOCAL D:\\TDATA $NOW /CFG:$cfg /KEYS:$s.KEY"; then
            if tr -d '\r' <"$log" | grep -q '^-ERR'; then
                check_fail "$what: a seed request was refused"
                cat "$log" >&2
            fi
            same "$what" "$lpt" "$PGOLD/$s.PRN"
        fi
        passed "$what" "$f0"
    done
done
[ $fails = 0 ] || fail "$fails failures"
echo "$prog: ok ($# builds)"
