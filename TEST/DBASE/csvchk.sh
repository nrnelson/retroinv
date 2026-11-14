#!/bin/sh
# csvchk.sh - check CSV export and import against dBASE III Plus.
#
# usage: csvchk.sh [-w] INVBIN
#
# INVBIN is a host build of INV (OBJ/HOST/INV.bin).  In a scratch data
# directory, INV /CMD /LOCAL with the clock fixed at 8 October 2026 runs
# TEST/DBASE/CSVSEED.REQ: it seeds items (blank and set dates, prices and
# quantities, leading spaces, a comma inside a value, a code page 437
# byte, a deleted item, tags and works states), a small tree of
# locations, a category, maintenance records and loans, and exports
# ITEMS, LOCS, CATS, LTYPES, MAINT, LOANS and AUDIT to <table>.CSV, each
# in one request (a C line in the answer fails the run).  Then
# TEST/DBASE/CSVCHK.PRG runs there through rundb.sh: dBASE writes each
# table with COPY TO <table> DELIMITED, which must equal our <table>.CSV
# byte for byte, and appends our files to copies of the tables with
# APPEND FROM ... DELIMITED and lists them; the listing (CR, 0x1A and
# trailing blanks removed) must equal TEST/GOLDEN/CSVCHK.TXT.
# Finally a second, fresh data directory gets the seed's category and
# INV IMPORTs dBASE's LOCS.TXT and ITEMS.TXT into it ("+OK ADDED=4" and
# "+OK ADDED=5"); INVDUMP's lists of their settable fields, and of the
# locations' LOC_ID, must equal those of the seeded tables, so the tree
# and the items' places came back whole, and the directory must pass
# TEST/CHKDBF.PY --strict and be listed by dBASE through check.sh -n.
#
#   -w   write the listing to TEST/GOLDEN/CSVCHK.TXT instead of comparing
#        (every other check still applies)
#
# DB_DIR and DB_DATE select the dBASE install and DOS date as for
# rundb.sh.  Run from anywhere; paths are resolved from this script.
# Exit status: 0 pass, 1 check failed, 2 usage or setup error; 129, 130
# or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd -P)
. "$here/../shlib.sh"
repo=$(cd "$here/../.." && pwd -P)
golden=$repo/TEST/GOLDEN/CSVCHK.TXT
write=0
if [ "${1:-}" = "-w" ]; then
    write=1
    shift
fi
[ $# -eq 1 ] || die "usage: csvchk.sh [-w] INVBIN"
[ -x "$1" ] || die "not an executable: $1"
inv=$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")
command -v gcc >/dev/null || die "gcc not installed"
vars=$(srcs_vars "$repo/SRCS.MK") || exit 2

work=$(mktemp -d "${TMPDIR:-/tmp}/csvchk.XXXXXX")
trap 'rm -rf "$work"' EXIT
on_signals
mkdir "$work/seed" "$work/imp"
now=/NOW:$SEED_NOW
tables="ITEMS LOCS CATS LTYPES MAINT LOANS AUDIT"

# shellcheck disable=SC2046
(cd "$repo" && gcc -x c -std=c89 -pedantic -Wall -Wextra -Wconversion \
    -Werror -DHOST_LINUX -ISRC/COMPAT -ISRC/DBF -ISRC/TOOLS \
    $(getvar INVDUMP_SRCS) -x none -o "$work/invdump")

# The seed and our exports.
(cd "$work/seed" && "$inv" /CMD /LOCAL ./ $now) \
    < "$here/CSVSEED.REQ" > "$work/seed.out" ||
    fail "INV /CMD failed on CSVSEED.REQ"
if grep -n '^-ERR' "$work/seed.out" >&2; then
    fail "the seed requests were refused"
fi
grep -q '^C ' "$work/seed.out" && fail "an export needed more than one request"
[ "$(grep -c '^K ROWS=' "$work/seed.out")" -eq 7 ] ||
    fail "not every table was exported"

# dBASE's COPY TO and APPEND FROM.
sh "$here/rundb.sh" -t 300 "$work/seed" "$here/CSVCHK.PRG" > /dev/null ||
    fail "the dBASE run failed"
for t in $tables; do
    cmp "$work/seed/$t.TXT" "$work/seed/$t.CSV" >&2 ||
        fail "$t.CSV differs from dBASE's COPY TO $t DELIMITED"
done
tr -d '\r\032' < "$work/seed/OUT.TXT" | sed 's/ *$//' > "$work/out.txt"
[ "$(tail -n 1 "$work/out.txt")" = "END OF CSVCHK" ] ||
    fail "the listing is cut short"
if [ "$write" -eq 1 ]; then
    cp "$work/out.txt" "$golden"
    echo "$prog: $golden written"
else
    diff -u "$golden" "$work/out.txt" >&2 ||
        fail "the listing differs from $golden"
fi

# Our import of dBASE's files.
{ grep -E '^(HELLO|LOGIN|PASSWD|CAT ADD) ' "$here/CSVSEED.REQ"
  echo "IMPORT LOCS FILE=../seed/LOCS.TXT"
  echo "IMPORT ITEMS FILE=../seed/ITEMS.TXT"
  echo "BYE"
} > "$work/imp.req"
(cd "$work/imp" && "$inv" /CMD /LOCAL ./ $now) \
    < "$work/imp.req" > "$work/imp.raw" ||
    fail "INV /CMD failed on the import"
tr -d '\r' < "$work/imp.raw" > "$work/imp.out"
if ! grep -qx '+OK ADDED=4' "$work/imp.out" ||
   ! grep -qx '+OK ADDED=5' "$work/imp.out"; then
    cat "$work/imp.out" >&2
    fail "the import did not add every line"
fi
dump() {
    "$work/invdump" "/F:$2" "$1" | sed -n 's/^ *[0-9][0-9]*: //p'
}
f=CATEGORY,ITEMTYPE,MFR,MODEL,SERIALNO,DESCR,COND,WORKS,QTY,LOC_ID
f=$f,ACQ_DATE,ACQ_SRC,ACQ_PRICE,CUR_VALUE,TAGS,STATUS,CREATED,UPDATED
dump "$work/seed/ITEMS.DBF" "$f" > "$work/seed.items"
dump "$work/imp/ITEMS.DBF" "$f" > "$work/imp.items"
f=LOC_ID,PARENT_ID,LOC_NAME,LOC_CODE,LTYPE,CAPACITY
dump "$work/seed/LOCS.DBF" "$f" > "$work/seed.locs"
dump "$work/imp/LOCS.DBF" "$f" > "$work/imp.locs"
[ -s "$work/seed.items" ] && [ -s "$work/seed.locs" ] ||
    fail "INVDUMP listed nothing"
diff -u "$work/seed.items" "$work/imp.items" >&2 ||
    fail "the imported items differ from the seeded ones"
diff -u "$work/seed.locs" "$work/imp.locs" >&2 ||
    fail "the imported locations differ from the seeded ones"
python3 -I "$repo/TEST/CHKDBF.PY" --strict "$work/imp"/*.DBF \
    "$work/imp"/*.DBT "$work/imp"/*.NDX > /dev/null ||
    fail "CHKDBF.PY refused the imported directory"
sh "$here/check.sh" -n "$work/imp" > /dev/null ||
    fail "dBASE could not list the imported directory"
echo "$prog: dBASE III Plus agrees with our CSV files"
