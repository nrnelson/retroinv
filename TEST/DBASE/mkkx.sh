#!/bin/sh
# mkkx.sh - make the key expression reference file with dBASE III Plus.
#
# usage: mkkx.sh [OUTFILE]    (default: TEST/GOLDEN/KEYEXPR.TXT)
#
# Builds the T_KEYEX test on the host with gcc, has it write its fixture
# table KXFIX.DBF to a scratch directory ("T_KEYEX -f"), and runs
# KXCHECK.PRG there through rundb.sh.  The KEYEXPR.TXT that dBASE writes
# is copied to OUTFILE unchanged (CR/LF and the final 0x1A kept).
#
# KXCHECK.PRG also builds an index KXnnn.NDX for each expression it
# evaluates on every record; kxndx.py compares each index's key length
# and leaf keys with the listing (see kxndx.py), and any problem fails
# the run before OUTFILE is written.
#
# DB_DIR and DB_DATE select the dBASE install and DOS date as for
# rundb.sh.  Run from anywhere; paths are resolved from this script.
#
# Exit status: 0 when OUTFILE was written; 2 on a setup error; that of
# the failing step otherwise; 129, 130 or 143 on SIGHUP, SIGINT or
# SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd -P)
. "$here/../shlib.sh"
repo=$(cd "$here/../.." && pwd -P)
out=${1:-$repo/TEST/GOLDEN/KEYEXPR.TXT}
for t in gcc python3; do
    command -v $t >/dev/null || die "$t not installed"
done
vars=$(srcs_vars "$repo/SRCS.MK") || exit 2

work=$(mktemp -d "${TMPDIR:-/tmp}/mkkx.XXXXXX")
trap 'rm -rf "$work"' EXIT
on_signals
mkdir "$work/db"

# shellcheck disable=SC2046
(cd "$repo" && gcc -x c -std=c89 -pedantic -Wall -Wextra -Wconversion \
    -Werror -DHOST_LINUX -ISRC/COMPAT -ISRC/DBF -ITEST \
    $(getvar T_KEYEX_SRCS) -x none -o "$work/t_keyex")
"$work/t_keyex" -f "$work/db/"
sh "$here/rundb.sh" -t 600 -o KEYEXPR.TXT "$work/db" "$here/KXCHECK.PRG" \
    > /dev/null

python3 -I "$here/kxndx.py" "$work/db"
cp "$work/db/KEYEXPR.TXT" "$out"
echo "mkkx.sh: $out written"
