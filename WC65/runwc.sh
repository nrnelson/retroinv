#!/bin/sh
# runwc.sh - build the Watcom C 6.5 executables in headless DOSBox-X.
#
# usage: sh WC65/runwc.sh          (run from the repository root; "make wc65")
#
# env:
#   WC65_DIR      Watcom C 6.5 install mounted as D: (default:
#                 $RETRO_DL/wc65/WATCOM), as made by WC65/install.sh:
#                 BIN\WCC.EXE, BIN\WLINK.EXE, H\ and LIB\ at its root
#   WC65_TIMEOUT  seconds before a DOSBox-X instance is killed, and the
#                 longest wait for another build to release WC65/.LOCK
#                 (default: 600), each times DOSTIME_SCALE (dos_secs of
#                 TEST/shlib.sh)
#   WC65_JOBS     DOSBox-X instances compiling at once (default: 1)
#   DOS_LOCK      when set, the emulator lock of the MAKEFILE's DOS test
#                 runs (files $DOS_LOCK.Q and $DOS_LOCK.LCK), held shared
#                 while DOSBox-X runs so that a test run that must run
#                 alone does not run beside this build
#   DOS_LOCK_WAIT the longest wait for DOS_LOCK, in seconds (default:
#                 WC65_TIMEOUT times DOSTIME_SCALE)
#   WC65_SRCS, WC65_SRCROOT, WC65_GENINC  passed through to WC65/mkbat.sh
#                 (the source list, its root and the generated headers)
#   WC65_CHECK    a shell command run once the build has succeeded, still
#                 under the lock below, so that another build cannot replace
#                 BIN/WC65 while it reads it; the build fails if it fails
#
# WC65/mkbat.sh writes the compile batch files BIN/WC65/CC<k>.BAT and the
# link batch file BIN/WC65/BUILD.BAT.  The compile batch files run at
# once, each in its own DOSBox-X instance with its own configuration
# (TC/TC.CONF filled in, with BIN/WC65 mounted as E:) and scratch
# directory, writing BIN/WC65/CC<k>.LOG; when all have ended,
# BIN/WC65/BUILD.BAT runs in one more instance, writing BIN/WC65/LINK.LOG.
# BIN/WC65/BUILD.LOG then holds every compile log in batch order followed
# by the link log.  Only one build at a time owns BIN/WC65: the script
# holds the lock WC65/.LOCK while it runs.
#
# Each executable's map, BIN/WC65/<NAME>.MAP, is in the WLINK 4.1 format,
# which TEST/mapchk.sh reads; this script does not check the maps itself.
#
# Before starting DOSBox-X the run-time libraries of every memory model in
# use (CLIB<m>.LIB, MATH<m>.LIB) are checked with WC65/chklib.py, so a
# damaged library copy is reported by name, and each CLIB<m>.LIB that
# WC65/CLIBREC.SHA lists (the reconstructed CLIBS.LIB and CLIBC.LIB that
# WC65/install.sh installs) must match its SHA-256 there, so that no build
# links with another copy.
#
# Exit status: 0 when every executable is built; 1 if a DOSBox-X instance
# fails or times out, if BIN/WC65/BUILD.LOG reports any error or warning or
# lacks one of the end markers, if an expected EXE is missing, or if
# WC65_CHECK fails; 2 on a setup error (WC65/mkbat.sh's and a failed
# library check included) or when a lock is not had in time; 129, 130 or
# 143 on SIGHUP, SIGINT or SIGTERM.  On any failure the executables of
# this run are deleted: WLINK 4.1 sets no error level for undefined
# symbols and still writes the EXE.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

trap runbat_cleanup EXIT
on_signals

[ -f SRCS.MK ] && [ -d WC65 ] && [ -f TC/TC.CONF ] ||
    die "run from the repository root"
command -v dosbox-x >/dev/null || die "dosbox-x not installed"

REPO=$(pwd -P)
WORK=$REPO/BIN/WC65
WC65_DIR=${WC65_DIR:-$RETRO_DL/wc65/WATCOM}
WC65_TIMEOUT=${WC65_TIMEOUT:-600}
WC65_JOBS=${WC65_JOBS:-1}
jobs_ok WC65_JOBS "$WC65_JOBS"
export WC65_JOBS
for f in BIN/WCC.EXE BIN/WCG.EXE BIN/WLINK.EXE H/STDIO.H; do
    [ -f "$WC65_DIR/$f" ] ||
        die "$f not found in $WC65_DIR (set WC65_DIR, or run WC65/install.sh)"
done
WC65_DIR=$(cd "$WC65_DIR" && pwd -P)
# The paths are pasted into the DOSBox-X configuration through sed and
# quoted mount commands, which these characters would break.
for p in "$REPO" "$WC65_DIR" "$WORK"; do
    case $p in *'|'*|*'&'*|*'"'*) die "path contains | & or \": $p" ;; esac
done

# generate - write the batch files, then check the run-time libraries of
# every memory model they use.
generate() {
    sh WC65/mkbat.sh
    libs=
    for m in $(cat BIN/WC65/MODELS.LST); do
        mu=$(printf '%s' "$m" | tr 'a-z' 'A-Z')
        lib=$WC65_DIR/LIB/CLIB$mu.LIB
        libs="$libs $lib $WC65_DIR/LIB/MATH$mu.LIB"
        want=$(awk -v f="CLIB$mu.LIB" '$2 == f { print $1 }' WC65/CLIBREC.SHA)
        [ -z "$want" ] || [ ! -f "$lib" ] ||
            [ "$(sha256sum < "$lib" | cut -d' ' -f1)" = "$want" ] ||
            die "$lib is not the reconstructed library (sha256 $want);" \
                "install it with WC65/install.sh"
    done
    # shellcheck disable=SC2086
    python3 -I WC65/chklib.py $libs ||
        die "run-time library damaged; reinstall it with WC65/install.sh"
}

rb_dir=WC65
rb_work=BIN/WC65
rb_timeout=$WC65_TIMEOUT
rb_conf=TC/TC.CONF
rb_sed="s|@REPO@|$REPO|g
s|@TOOLDIR@|$WC65_DIR|g
s|@MOUNTS@|mount e \"$WORK\" -nocachedir|g"

runbat_prepare generate
runbat_build 'E:\' 'E:\BUILD.BAT' 'E:\LINK.LOG'
# Messages that fail the build: WCC's "file(line): Error! Ennn: ...",
# "Warning! Wnnn: ...", "Unable to open/find ...", "Not enough memory",
# "Too many errors"; WLINK's "*** Error *** ...", "Warnings:" block,
# "can't open ..." (a missing library is only a warning), "... is an
# undefined reference", "Directive error", "Premature end of file"; DOS's
# "Bad command or filename", "Out of environment space", "File not found",
# "Insufficient memory"; and the "*** FAILED" lines of BUILD.BAT.  The
# patterns are anchored on these forms so that a file or symbol name such
# as T_ERRORS.C cannot trip them.  Not counted: the two W008, "Missing
# return value", that this compiler gives even at warning level 0 in
# WATTCP's kernel, for icmp_Reply of PCICMP.C and sock_close of PCTCP.C;
# WATTCP's C is changed only as WATTCP.DIF needs.
errs='(^|: )(error|warning)! [ew][0-9]{3}|^\*\*\* |^warnings:'
errs=$errs'|undefined (reference|symbol)|^unable to |^not enough memory'
errs=$errs'|^out of memory|too many errors|can.t (open|read)'
errs=$errs'|^cannot (open|read)|directive error|premature end of file'
errs=$errs'|^bad command|out of environment|^file not found'
errs=$errs'|^path not found|insufficient memory|stack overflow'
skip='\\(pcicmp\.c\([0-9]+\): warning! w008: .* .icmp_reply.'
skip=$skip'|pctcp\.c\([0-9]+\): warning! w008: .* .sock_close.)$'
# The checks after the build: WC65/toolarge.sh, then WC65_CHECK.
check="sh WC65/toolarge.sh WC65/TOOLARGE.TXT $rb_work"
runbat_check "$errs" "$check${WC65_CHECK:+ && $WC65_CHECK}" "$skip"
