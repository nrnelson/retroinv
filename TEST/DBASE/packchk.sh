#!/bin/sh
# packchk.sh - check DBPACK's output with dBASE III Plus.
#
# usage: packchk.sh [-w]
#
# Builds SRC/TOOLS/DBPACK.C on the host with gcc and, in a scratch
# directory, uses it on copies of the dBASE-written files in
# TEST/GOLDEN/DB3: packs ITEMS.DBF + ITEMS.DBT and rebuilds ITEMS1.NDX,
# rebuilds BIG1.NDX and BIG2.NDX on BIG.DBF and NUM2000.NDX on
# NUM2000.DBF.  Then runs TEST/DBASE/PACKCHK.PRG there through rundb.sh
# and compares its OUT.TXT (CR and 0x1A removed) with
# TEST/GOLDEN/PACKCHK.TXT.  The run fails when dBASE reports an error,
# when the output holds "NOT FOUND", "WRONG RECORD" or "SEEK MISS", a
# nonzero "OUT OF ORDER" or "BAD" count, or lacks the final
# "END OF PACKCHK" line, or when it differs from the golden file.
#
#   -w   write the output to TEST/GOLDEN/PACKCHK.TXT instead of comparing
#        (the failure checks still apply)
#
# DB_DIR and DB_DATE select the dBASE install and DOS date as for
# rundb.sh.  Run from anywhere; paths are resolved from this script.
# Exit status: 0 pass, 1 check failed, 2 usage or setup error; 129, 130
# or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd -P)
. "$here/../shlib.sh"
repo=$(cd "$here/../.." && pwd -P)
golden=$repo/TEST/GOLDEN/PACKCHK.TXT
write=0
case ${1:-} in
'') ;;
-w) write=1 ;;
*) die "usage: packchk.sh [-w]" ;;
esac
command -v gcc >/dev/null || die "gcc not installed"
vars=$(srcs_vars "$repo/SRCS.MK") || exit 2

work=$(mktemp -d "${TMPDIR:-/tmp}/packchk.XXXXXX")
trap 'rm -rf "$work"' EXIT
on_signals
mkdir "$work/db"

# shellcheck disable=SC2046
(cd "$repo" && gcc -x c -std=c89 -pedantic -Wall -Wextra -Wconversion \
    -Werror -DHOST_LINUX -ISRC/COMPAT -ISRC/DBF -ISRC/TOOLS \
    $(getvar DBPACK_SRCS) -x none -o "$work/dbpack")

g=$repo/TEST/GOLDEN/DB3
for f in ITEMS.DBF ITEMS.DBT ITEMS1.NDX BIG.DBF BIG1.NDX BIG2.NDX \
         NUM2000.DBF NUM2000.NDX; do
    cp "$g/$f" "$work/db/$f"
done
(cd "$work/db" &&
    "$work/dbpack" ITEMS /P /R ITEMS1 &&
    "$work/dbpack" BIG /R BIG1 BIG2 &&
    "$work/dbpack" NUM2000 /R NUM2000) || fail "DBPACK failed"

sh "$here/rundb.sh" -t 900 "$work/db" "$here/PACKCHK.PRG" > /dev/null ||
    fail "the dBASE run failed"
tr -d '\r\032' < "$work/db/OUT.TXT" > "$work/out.txt"

bad=0
if grep -E 'NOT FOUND|WRONG RECORD|SEEK MISS' "$work/out.txt" >&2; then
    bad=1
fi
if grep -E '(OUT OF ORDER|BAD) +[1-9]' "$work/out.txt" >&2; then
    bad=1
fi
grep -q '^END OF PACKCHK' "$work/out.txt" ||
    { echo "$prog: the listing is cut short" >&2; bad=1; }
[ "$bad" -eq 0 ] || fail "dBASE found errors"

if [ "$write" -eq 1 ]; then
    cp "$work/out.txt" "$golden"
    echo "$prog: $golden written"
    exit 0
fi
diff -u "$golden" "$work/out.txt" >&2 ||
    fail "output differs from $golden"
echo "$prog: dBASE III Plus agrees with DBPACK"
