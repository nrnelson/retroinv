#!/bin/sh
# runtc.sh - build the Turbo C 2.01 executables in headless DOSBox-X.
#
# usage: sh TC/runtc.sh            (run from the repository root; "make tc")
#
# env:
#   TC_DIR      Turbo C install mounted as D: (default: $RETRO_DL/tc201/TC);
#               TCC.EXE, TLINK.EXE, INCLUDE\ and LIB\ sit at its root
#   TC_TIMEOUT  seconds before a DOSBox-X instance is killed, and the
#               longest wait for another build to release TC/.LOCK
#               (default: 600), each times DOSTIME_SCALE (dos_secs of
#               TEST/shlib.sh)
#   TC_JOBS     DOSBox-X instances compiling at once (default: 1)
#   DOS_LOCK    when set, the emulator lock of the MAKEFILE's DOS test runs
#               (files $DOS_LOCK.Q and $DOS_LOCK.LCK), held shared while
#               DOSBox-X runs so that a test run that must run alone
#               does not run beside this build
#   DOS_LOCK_WAIT  the longest wait for DOS_LOCK, in seconds (default:
#               TC_TIMEOUT times DOSTIME_SCALE)
#   TC_SRCS, TC_SRCROOT, TC_GENINC  passed through to TC/mkbat.sh (the
#               source list, its root and the generated headers)
#   TC_CHECK    a shell command run once the build has succeeded, still
#               under the lock below, so that another build cannot replace
#               BIN/TC while it reads it; the build fails if it fails
#
# TC/mkbat.sh writes the compile batch files BIN/TC/CC<k>.BAT and the link
# batch file BIN/TC/BUILD.BAT.  The compile batch files run at once, each
# in its own DOSBox-X instance with its own configuration (TC/TC.CONF
# filled in) and scratch directory, writing BIN/TC/CC<k>.LOG; when all
# have ended, BIN/TC/BUILD.BAT runs in one more instance, writing
# BIN/TC/LINK.LOG.  BIN/TC/BUILD.LOG then holds every compile log in batch
# order followed by the link log.  Only one build at a time owns BIN/TC:
# the script holds the lock TC/.LOCK while it runs.
#
# Exit status: 0 when every executable is built; 1 if a DOSBox-X instance
# fails or times out, if BIN/TC/BUILD.LOG reports any error or warning or
# lacks one of the end markers, if an expected EXE is missing, or if
# TC_CHECK fails; 2 on a setup error (TC/mkbat.sh's included) or when a
# lock is not had in time; 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
# On any failure the executables of this run are deleted, so a linker
# that reports a problem but still writes its output leaves nothing
# behind.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

trap runbat_cleanup EXIT
on_signals

[ -f SRCS.MK ] && [ -f TC/TC.CONF ] || die "run from the repository root"
command -v dosbox-x >/dev/null || die "dosbox-x not installed"

REPO=$(pwd -P)
TC_DIR=${TC_DIR:-$RETRO_DL/tc201/TC}
TC_TIMEOUT=${TC_TIMEOUT:-600}
TC_JOBS=${TC_JOBS:-1}
jobs_ok TC_JOBS "$TC_JOBS"
export TC_JOBS
[ -f "$TC_DIR/TCC.EXE" ] && [ -f "$TC_DIR/TLINK.EXE" ] ||
    die "TCC.EXE/TLINK.EXE not found in $TC_DIR (set TC_DIR)"
TC_DIR=$(cd "$TC_DIR" && pwd -P)
# Both paths are pasted into the DOSBox-X configuration through sed and
# quoted mount commands, which these characters would break.
for p in "$REPO" "$TC_DIR"; do
    case $p in *'|'*|*'&'*|*'"'*) die "path contains | & or \": $p" ;; esac
done

rb_dir=TC
rb_work=BIN/TC
rb_timeout=$TC_TIMEOUT
rb_conf=TC/TC.CONF
rb_sed="s|@REPO@|$REPO|g
s|@TOOLDIR@|$TC_DIR|g
/@MOUNTS@/d"

runbat_prepare sh TC/mkbat.sh
runbat_build 'C:\BIN\TC\' 'C:\BIN\TC\BUILD.BAT' 'C:\BIN\TC\LINK.LOG'
# Messages that fail the build, matched ignoring case.
errs='^(error|warning|fatal)|\*\*\* |undefined symbol|bad command'
errs=$errs'|could not|unable to|illegal command|file not found'
errs=$errs'|path not found|not enough memory|out of memory'
runbat_check "$errs" "${TC_CHECK:-}"
