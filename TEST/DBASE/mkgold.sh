#!/bin/sh
# mkgold.sh - make the dBASE III Plus reference files with dBASE itself.
#
# usage: mkgold.sh [OUTDIR]      (default: TEST/GOLDEN/DB3 in this repository)
#
# Runs MKGOLD.PRG through rundb.sh in a scratch directory that holds the
# memo texts MEMO1.TXT and MEMO2.TXT as M1.TXT and M2.TXT (converted to
# CR/LF), then copies the tables, memo file and index it wrote
# (ITEMS.DBF, ITEMS.DBT, ITEMS1.NDX, CATS.DBF, LOANS.DBF, USERS.DBF,
# BIG.DBF, BIG1-3.NDX, NUM2000.DBF/.NDX, NUM993.DBF/.NDX, WIDE.DBF,
# WIDE1.NDX, WIDED.DBF, WIDED1.NDX) and its listing (as MKGOLD.TXT, CR/LF
# and 0x1A kept) to OUTDIR.  The DOS date is DB_DATE (default
# 2026-10-08), so the files are the same on every run.  DB_DIR selects
# the dBASE install as for rundb.sh.
#
# Exit status: 0 when every file was written; 1 when the dBASE run failed
# or left a file out; 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd -P)
. "$here/../shlib.sh"
out=${1:-$here/../GOLDEN/DB3}
mkdir -p "$out"

work=$(mktemp -d "${TMPDIR:-/tmp}/mkgold.XXXXXX")
pid=
# On a signal, rundb.sh is stopped and waited for (it stops its own
# DOSBox-X) before the scratch directory is removed.
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -rf "$work"
}
trap cleanup EXIT
on_signals
for n in 1 2; do
    tr -d '\r' < "$here/MEMO$n.TXT" | sed 's/$/\r/' > "$work/M$n.TXT"
done

sh "$here/rundb.sh" -t 180 -o NONE.TXT "$work" "$here/MKGOLD.PRG" &
pid=$!
status=0
wait "$pid" || status=$?
pid=
[ "$status" -eq 0 ] || fail "rundb.sh failed ($status)"

for f in ITEMS.DBF ITEMS.DBT ITEMS1.NDX CATS.DBF LOANS.DBF USERS.DBF \
         BIG.DBF BIG1.NDX BIG2.NDX BIG3.NDX NUM2000.DBF NUM2000.NDX \
         NUM993.DBF NUM993.NDX WIDE.DBF WIDE1.NDX WIDED.DBF WIDED1.NDX; do
    [ -f "$work/$f" ] || fail "$f was not written"
    cp "$work/$f" "$out/$f"
done
cp "$work/OUT.TXT" "$out/MKGOLD.TXT"
echo "$prog: reference files written to $out"
