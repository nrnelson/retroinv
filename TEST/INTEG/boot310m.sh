#!/bin/sh
# boot310m.sh - TEST/INTEG/boot310.sh with a RAM disk sized so that the
# programs of the command lines start with a chosen amount of free
# conventional memory, checked by CHKDSK in the same boot.
#
# usage: sh TEST/INTEG/boot310m.sh KB [boot310.sh options] COMMAND LINE...
#
# The boot gets a CONFIG.SYS of its own (free310_config of
# TEST/INTEG/free310.sh): "FILES=45" and a VDISK.SYS RAM disk (D: when
# the hard disk is attached), sized by free310_vdisk so that a program
# run straight from a command line has KB kilobytes free, less than one
# more; the options must not name another CONFIG.SYS.  "A:\CHKDSK D:"
# runs before the command lines, with the memory they start with, and its
# figure must be KB kilobytes.  The options end at the first word that is
# not one of boot310.sh's (-i, -f, -h, -t, -e, -m and -p, each with its
# value, -s, -c and -k).  Everything boot310.sh prints is passed on, CHKDSK's
# report included.
#
# Exit status: 1 when the figure is not KB kilobytes, else boot310.sh's
# (for a completed run the errorlevel of the last command line); 2 on a
# usage error; 129, 130 or 143 when this script got SIGHUP, SIGINT or
# SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../shlib.sh"
[ $# -ge 2 ] || die "usage: boot310m.sh KB [boot310.sh options] COMMAND LINE..."
kb=$1
shift
case $kb in ''|0*|*[!0-9]*) die "bad KB: $kb" ;; esac
for a in "$@"; do
    case $(basename -- "$a" | tr '[:lower:]' '[:upper:]') in
    CONFIG.SYS) die "the CONFIG.SYS is boot310m.sh's own" ;;
    esac
done
. "$here/free310.sh"
n=$(free310_vdisk "$kb" "$FREE310_TOP") || die "no RAM disk leaves $kb KB free"

work=$(mktemp -d "${TMPDIR:-/tmp}/boot310m.XXXXXX")
trap 'rm -rf "$work"' EXIT
on_signals
free310_config "$work/CONFIG.SYS" "$n"

# The options, then CHKDSK, then the command lines: count the option
# words, then move the command lines round to the end of the list,
# behind the options and CHKDSK.
nopt=0
want=
for a in "$@"; do
    if [ -n "$want" ]; then
        want=
    else
        case $a in
        -[ifhtemp]) want=1 ;;
        -[sck]) ;;
        *) break ;;
        esac
    fi
    nopt=$((nopt + 1))
done
[ -z "$want" ] || die "an option without its value"
ncmd=$(($# - nopt))
[ "$ncmd" -ge 1 ] || die "no command line"
i=0
while [ $i -lt "$nopt" ]; do a=$1; shift; set -- "$@" "$a"; i=$((i + 1)); done
set -- "$@" 'A:\CHKDSK D:'
i=0
while [ $i -lt "$ncmd" ]; do a=$1; shift; set -- "$@" "$a"; i=$((i + 1)); done
rc=0
sh "$here/boot310.sh" -f "$work/CONFIG.SYS" "$@" >"$work/out" 2>&1 || rc=$?
cat "$work/out"
why=$(free310_check "$work/out" "$kb") || fail "$why"
exit "$rc"
