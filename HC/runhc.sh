#!/bin/sh
# runhc.sh - compile a Windows Help project with HC.EXE in headless
# DOSBox-X, then make the result the same at every build.
#
# usage: sh HC/runhc.sh PROJECT.HPJ OUT.HLP
#
#   PROJECT.HPJ  the help project, an 8.3 name, whose directory, which
#                holds its RTF files, HC.EXE works in (mounted as C:)
#   OUT.HLP      the help file written
#
# env:
#   HC_DIR      the directory of HC.EXE (HC/install.sh; default
#               $RETRO_DL/win30hc), mounted as D:
#   HC_TIMEOUT  seconds before DOSBox-X is killed (default 120), times
#               DOSTIME_SCALE (dos_secs of TEST/shlib.sh)
#   HC_TIMER    for TEST/hcchk.py: the divisor the timer's channel 0 is
#               given before HC.EXE runs, 4 to 65535, so that its
#               interrupt comes 1193182/HC_TIMER times a second in
#               place of 18.2 and finds HC.EXE at other instructions
#   DOS_LOCK, DOS_LOCK_WAIT  as for TC/runtc.sh: the emulator lock of the
#               DOS test runs, held shared while DOSBox-X runs
#
# HC.EXE is checked against HC/TOOLS.SHA at every run.  It runs on the
# project in one DOSBox-X instance (TC/TC.CONF filled in), its output
# going to HC.LOG beside the project; HC sets no error level, so the
# build fails when a line of the log holds "error" or "warning" (an
# unresolved jump is "Warning R2041: ..."), its help file then deleted,
# or when no help file was written.  HC/hlpstamp.py then writes OUT.HLP
# from it, the build's date set from the project's RTF file of the same
# name and the bytes HC.EXE leaves unset zeroed.
#
# Exit status: 0 when OUT.HLP is written; 1 when HC.EXE or DOSBox-X fails
# or times out, the log reports an error or a warning, or hlpstamp.py
# refuses the file; 2 on a usage or setup error (HC.EXE missing or not as
# pinned, a project that is not 8.3); 129, 130 or 143 on SIGHUP, SIGINT
# or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

[ $# -eq 2 ] || die "usage: sh HC/runhc.sh PROJECT.HPJ OUT.HLP"
command -v dosbox-x >/dev/null || die "dosbox-x not installed"
HC_DIR=${HC_DIR:-$RETRO_DL/win30hc}
[ -f "$HC_DIR/HC.EXE" ] ||
    die "HC.EXE not found in $HC_DIR (sh HC/install.sh IMG $HC_DIR)"
(cd "$HC_DIR" && sha256sum --quiet --strict -c "$here/TOOLS.SHA") ||
    die "$HC_DIR/HC.EXE is not as HC/TOOLS.SHA pins it"
proj=${1##*/}
base=${proj%.HPJ}
case $base in
    ''|*[!A-Z0-9_]*|?????????*) die "$1: not an 8.3 upper-case .HPJ" ;;
esac
[ "$base.HPJ" = "$proj" ] || die "$1: not an 8.3 upper-case .HPJ"
work=$(cd "$(dirname "$1")" && pwd -P)
HC_DIR=$(cd "$HC_DIR" && pwd -P)
for p in "$work" "$HC_DIR"; do
    case $p in *'|'*|*'&'*|*'"'*) die "path contains | & or \": $p" ;; esac
done
[ -f "$work/$base.RTF" ] || die "$work/$base.RTF not found"

rb_work=$work
trap runbat_cleanup EXIT
on_signals
rb_limit=$(dos_secs "${HC_TIMEOUT:-120}")
rb_timeout=${HC_TIMEOUT:-120}
rb_conf=$here/../TC/TC.CONF
rb_sed="s|@REPO@|$work|g
s|@TOOLDIR@|$HC_DIR|g
/@MOUNTS@/d"
# HC keeps its phrase table PH between runs; a new one is made each time.
rm -f "$work/$base.HLP" "$work/$base.PH" "$work/HC.LOG"
timer=
if [ -n "${HC_TIMER:-}" ]; then
    case $HC_TIMER in 0*|*[!0-9]*|??????*) HC_TIMER=0 ;; esac
    [ "$HC_TIMER" -ge 4 ] && [ "$HC_TIMER" -le 65535 ] ||
        die "HC_TIMER is not 4 to 65535"
    # TIMER.COM: mov al,36h; out 43h,al (channel 0, both bytes, mode 3);
    # mov al,low; out 40h,al; mov al,high; out 40h,al; ret.
    l=$(printf '%03o' $((HC_TIMER % 256)))
    h=$(printf '%03o' $((HC_TIMER / 256)))
    printf "\\260\\066\\346\\103\\260\\$l\\346\\100\\260\\$h\\346\\100\\303" \
        > "$work/TIMER.COM"
    timer='C:\\TIMER.COM\r\n'
fi
printf "${timer}D:\\\\HC.EXE %s\\r\\n" "$proj" > "$work/RUNHC.BAT"
if [ -n "${DOS_LOCK:-}" ]; then
    w=${DOS_LOCK_WAIT:-$rb_limit}
    dos_lock_take s "$w" ||
        die "the DOS test runs still hold $DOS_LOCK after $w s; giving up"
fi
runbat_start HC 'C:\RUNHC.BAT' 'C:\HC.LOG'
runbat_finish
[ -z "${DOS_LOCK:-}" ] || flock -u 8
[ -f "$work/HC.LOG" ] || fail "HC.EXE wrote no log"
tr -d '\r' < "$work/HC.LOG"
if tr -d '\r' < "$work/HC.LOG" | grep -Eiq 'error|warning'; then
    rm -f "$work/$base.HLP"
    fail "HC.EXE reported errors or warnings in $work/HC.LOG"
fi
[ -f "$work/$base.HLP" ] || fail "HC.EXE wrote no $base.HLP"
python3 -I "$here/hlpstamp.py" "$work/$base.HLP" "$work/$base.RTF" "$2" ||
    fail "hlpstamp.py refused $work/$base.HLP"
