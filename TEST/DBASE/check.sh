#!/bin/sh
# check.sh - list a sample database with dBASE III Plus and compare.
#
# usage: check.sh [-n | -e EXPECTED] WORKDIR
#
# WORKDIR must hold ITEMS.DBF/.DBT, LOCS.DBF/.DBT, CATS.DBF, LTYPES.DBF and
# USERS.DBF.
# CHECK.PRG runs there through rundb.sh and writes its listing to
# WORKDIR/OUT.TXT (rundb.sh's helper files and the copy of CHECK.PRG are
# removed again).  The listing is printed and must end with CHECK.PRG's
# "END OF CHECK" line.  It is then compared with EXPECTED (default
# TEST/GOLDEN/DB3CHK.TXT, the reviewed listing of the tables that
# TEST/MKITEMS.C writes), ignoring CR, 0x1A and trailing blanks; -n skips
# the comparison.  dBASE reads many damaged tables without complaint, so
# the comparison is what proves the tables hold the intended values.
#
# Exit status: that of rundb.sh when it fails; 4 when the listing is
# missing, cut short or differs from EXPECTED; 2 on a usage error; 129,
# 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd -P)
. "$here/../shlib.sh"
usage() { die "usage: check.sh [-n | -e EXPECTED] WORKDIR"; }
expected=$here/../GOLDEN/DB3CHK.TXT
while getopts ne: opt; do
    case $opt in
    n) expected= ;;
    e) expected=$OPTARG ;;
    *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || usage
work=$1
for f in ITEMS.DBF ITEMS.DBT LOCS.DBF LOCS.DBT CATS.DBF LTYPES.DBF \
        USERS.DBF; do
    [ -f "$work/$f" ] || die "$work/$f missing"
done
[ -z "$expected" ] || [ -f "$expected" ] || die "no such file: $expected"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/check.XXXXXX")
pid=
# On a signal, rundb.sh is stopped and waited for (it stops its own
# DOSBox-X) before the temporary files are removed.
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$tmp"
    rm -f "$work/CHECK.PRG"
}
trap cleanup EXIT
on_signals

status=0
sh "$here/rundb.sh" -t 120 "$work" "$here/CHECK.PRG" &
pid=$!
wait "$pid" || status=$?
pid=
[ "$status" -eq 0 ] || exit "$status"

[ -f "$work/OUT.TXT" ] || { echo "$prog: no listing written" >&2; exit 4; }
tr -d '\r\032' < "$work/OUT.TXT" | sed 's/ *$//' > "$tmp/got"
[ "$(tail -n 1 "$tmp/got")" = "END OF CHECK" ] ||
    { echo "$prog: listing does not end with END OF CHECK" >&2; exit 4; }
if [ -n "$expected" ]; then
    tr -d '\r\032' < "$expected" | sed 's/ *$//' > "$tmp/want"
    diff -u "$tmp/want" "$tmp/got" >&2 ||
        { echo "$prog: listing differs from $expected" >&2; exit 4; }
    echo "$prog: listing matches $expected"
fi
