#!/bin/sh
# runwc.sh - build the Watcom C 9.5 Windows programs in headless DOSBox-X.
#
# usage: sh WC95/runwc.sh          (run from the repository root; "make wc95")
#
# env:
#   WC95_DIR      Watcom C 9.5 install mounted as D: (default:
#                 $RETRO_DL/wc95/WATCOM), as made by WC95/install.sh:
#                 BIN\WCC.EXE, BIN\WLINK.EXE, BINB\WRC.EXE, H\ and LIB286\
#                 at its root
#   WC95_TIMEOUT  seconds before a DOSBox-X instance is killed, and the
#                 longest wait for another build to release WC95/.LOCK
#                 (default: 600), each times DOSTIME_SCALE (dos_secs of
#                 TEST/shlib.sh)
#   WC95_JOBS     DOSBox-X instances compiling at once (default: 1)
#   DOS_LOCK      when set, the emulator lock of the MAKEFILE's DOS test
#                 runs, held shared while DOSBox-X runs (as WC65/runwc.sh)
#   DOS_LOCK_WAIT the longest wait for DOS_LOCK, in seconds (default:
#                 WC95_TIMEOUT times DOSTIME_SCALE)
#   WC95_SRCS, WC95_SRCROOT, WC95_GENINC, WC95_CFLAGS, WC95_ANSI,
#   WC95_NOANSI, WC95_LINK  passed through to WC95/mkbat.sh
#   WC95_CHECK    a shell command run once the build has succeeded, still
#                 under the lock WC95/.LOCK, so that another build cannot
#                 replace BIN/WC95 while it reads it; the build fails if it
#                 fails
#
# WC95/mkbat.sh writes the compile batch files BIN/WC95/CC<k>.BAT, which
# run at once, each in its own DOSBox-X instance (TC/TC.CONF filled in,
# with BIN/WC95 mounted as E:), and the batch file BIN/WC95/BUILD.BAT of
# the resources, links and binds, which runs in one more once they have
# ended.  BIN/WC95/BUILD.LOG then holds every compile log in batch order
# followed by BIN/WC95/LINK.LOG.
#
# Before DOSBox-X starts, every file of the install that the build reads
# (the compiler, linker and resource compiler, the headers and the
# libraries) must match its SHA-256 in WC95/TOOLS.SHA, so that no build
# runs with another copy or patch level.
#
# Exit status: 0 when every executable is built; 1 if a DOSBox-X instance
# fails or times out, if BIN/WC95/BUILD.LOG reports any error or warning or
# lacks one of the end markers, if an expected EXE is missing, or if
# WC95_CHECK fails; 2 on a setup error (WC95/mkbat.sh's, and an install
# WC95/TOOLS.SHA does not pin, included) or when a lock is not had in
# time; 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.  On any failure the
# executables of this run are deleted.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

trap runbat_cleanup EXIT
on_signals

[ -f SRCS.MK ] && [ -d WC95 ] && [ -f TC/TC.CONF ] ||
    die "run from the repository root"
command -v dosbox-x >/dev/null || die "dosbox-x not installed"

REPO=$(pwd -P)
WORK=$REPO/BIN/WC95
WC95_DIR=${WC95_DIR:-$RETRO_DL/wc95/WATCOM}
WC95_TIMEOUT=${WC95_TIMEOUT:-600}
WC95_JOBS=${WC95_JOBS:-1}
jobs_ok WC95_JOBS "$WC95_JOBS"
export WC95_JOBS
[ -f "$WC95_DIR/BIN/WCC.EXE" ] ||
    die "BIN/WCC.EXE not found in $WC95_DIR" \
        "(set WC95_DIR, or run WC95/install.sh)"
WC95_DIR=$(cd "$WC95_DIR" && pwd -P)
(cd "$WC95_DIR" && sha256sum --quiet --strict -c "$here/TOOLS.SHA") ||
    die "$WC95_DIR is not the install WC95/TOOLS.SHA pins;" \
        "install it with WC95/install.sh"
# The paths are pasted into the DOSBox-X configuration through sed and
# quoted mount commands, which these characters would break.
for p in "$REPO" "$WC95_DIR" "$WORK"; do
    case $p in *'|'*|*'&'*|*'"'*) die "path contains | & or \": $p" ;; esac
done

rb_dir=WC95
rb_work=BIN/WC95
rb_timeout=$WC95_TIMEOUT
rb_conf=TC/TC.CONF
rb_sed="s|@REPO@|$REPO|g
s|@TOOLDIR@|$WC95_DIR|g
s|@MOUNTS@|mount e \"$WORK\" -nocachedir|g"

runbat_prepare sh WC95/mkbat.sh
runbat_build 'E:\' 'E:\BUILD.BAT' 'E:\LINK.LOG'
# Messages that fail the build: WCC's "file(line): Error! Ennn: ..." and
# "Warning! Wnnn: ...", WLINK's "Error(nnnn): ..." and
# "Warning(nnnn): ...", WRC's "Error! ...", "Warning! ..." and "Fatal
# Error! ...", the DOS extenders' "DOS/16M error" and "fatal error",
# and DOS's "Bad command or filename", "Out of environment space", "File
# not found", "Insufficient memory"; and the "*** FAILED" lines of the
# batch files.  The patterns are anchored on these forms so that a file
# or symbol name such as T_ERRORS.C cannot trip them.
errs='(^|: )(error|warning)!|(error|warning)\([0-9]+\)|^\*\*\* '
errs=$errs'|undefined (reference|symbol)|^unable to |not enough memory'
errs=$errs'|^out of memory|too many errors|can.t (open|read)'
errs=$errs'|^cannot (open|read)|directive error|dos/16m error|fatal err'
errs=$errs'|^bad command|out of environment|^file not found'
errs=$errs'|^path not found|insufficient memory'
runbat_check "$errs" "${WC95_CHECK:-}"
