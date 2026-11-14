#!/bin/sh
# T_DUMP.SH - host test of the sample generator, the table dumper and
# the packer's command line.
#
# usage: sh TEST/T_DUMP.SH [-u]      (run from the repository root)
#
# Builds the host versions of TEST/MKITEMS.C, INVDUMP and DBPACK (their
# sources from SRCS.MK) with the gcc flags in CFLAGS (default: the strict
# host flags), writes the sample tables into a scratch directory, records
# a checksum and the first header bytes of every file written, runs
# INVDUMP there with a set of switch combinations (relative file names,
# so the output does not depend on the scratch path) and DBPACK with
# recovery-only runs, the command lines it must refuse and the states of
# a stopped pack or rebuild that it must complete or refuse, and compares
# everything printed, with each exit status, against
# TEST/GOLDEN/INVDUMP.TXT.  The checksums catch a writer and reader that
# agree with each other but not with the file format.  With -u the
# golden file is rewritten instead; review the difference before keeping
# it.  A run that writes the golden without -u, or not with it, fails.
#
# Exits 0 when the output matches, 1 otherwise, 2 on a usage or setup
# error, 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

[ -f SRCS.MK ] && [ -f TEST/MKITEMS.C ] || die "run from the repository root"

update=0
case $# in
0) ;;
1) [ "$1" = "-u" ] || die "usage: sh TEST/T_DUMP.SH [-u]"; update=1 ;;
*) die "usage: sh TEST/T_DUMP.SH [-u]" ;;
esac

GOLDEN=TEST/GOLDEN/INVDUMP.TXT
CFLAGS=${CFLAGS:-"-std=c89 -pedantic -Wall -Wextra -Wconversion -Werror \
-DHOST_LINUX -ISRC/COMPAT -ISRC/DBF -ISRC/TOOLS -ITEST"}
vars=$(srcs_vars SRCS.MK) || exit 2
invdump=$(getvar INVDUMP_SRCS)
pathmax=$(cdefine SRC/COMPAT/COMPAT.H DOS_PATHMAX)

scratch=$(mktemp -d "${TMPDIR:-/tmp}/t_dump.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
on_signals

# build NAME SOURCES: build the host program NAME in the scratch
# directory.  Upper-case .C means C++ to gcc, hence -x c ... -x none.
build() {
    # shellcheck disable=SC2086
    gcc $CFLAGS -x c $2 -x none -o "$scratch/$1" || fail "$1 does not build"
}
# MKITEMS links what INVDUMP links, its own source in place of INVDUMP.C,
# the first of INVDUMP_SRCS.
build mkitems "TEST/MKITEMS.C ${invdump#* }"
build invdump "$invdump"
build dbpack "$(getvar DBPACK_SRCS)"
mkdir "$scratch/data"

# Run one command in the data directory, recording its output and status.
run() {
    name=$1
    shift
    echo "== $name $*"
    rc=0
    (cd "$scratch/data" && "$scratch/$name" "$@") || rc=$?
    echo "== exit $rc"
}

# Checksum and first 32 bytes (the table or memo header) of each file.
fingerprint() {
    echo "== files"
    for f in ITEMS.DBF ITEMS.DBT LOCS.DBF LOCS.DBT CATS.DBF LTYPES.DBF \
            USERS.DBF; do
        (cd "$scratch/data" && cksum "$f")
        od -An -tx1 -N32 "$scratch/data/$f"
    done
}

{
    run mkitems .
    fingerprint
    run invdump ITEMS
    run invdump /D /M ITEMS.DBF
    run invdump /H LOCS
    run invdump /S CATS
    run invdump /M LOCS
    run invdump USERS
    run invdump /F:ITEM_ID,mfr,MODEL,PARENT_ID /R:2-4 /D ITEMS
    run invdump /F:NOTES,ITEM_ID /R:3 /M ITEMS
    run invdump /R:6- /F:ITEM_ID,UPDATED ITEMS
    run invdump /R:5-2 ITEMS
    run invdump /F:NOPE ITEMS
    run invdump /X ITEMS
    run invdump /R:-2 /F:ITEM_ID,MODEL ITEMS
    run invdump /R:9- /F:ITEM_ID,MODEL /M ITEMS
    run invdump /R:12- ITEMS
    run invdump /M CATS
    run invdump /H /S USERS
    run invdump /F:ITEM_ID,DESCRIPTION1 ITEMS
    run invdump /F: ITEMS
    run invdump /F ITEMS
    run invdump /R ITEMS
    run invdump /R:2-4 /R:3- /F:ITEM_ID ITEMS
    run invdump /H: ITEMS
    run invdump nofile
    run invdump NOFILE
    echo "not a dBASE table" > "$scratch/data/JUNK.DBF"
    run invdump JUNK
    rm "$scratch/data/LOCS.DBT"
    run invdump /M LOCS
    run dbpack ITEMS /C
    run dbpack ITEMS /R
    run dbpack ITEMS /P /R
    run dbpack ITEMS /C /R
    run dbpack ""
    run dbpack ITEMS /R ""
    # Table names of DOS_PATHMAX - 5 and - 4 characters: with ".DBF" the
    # first fits a path of DOS_PATHMAX bytes, the second is too long.
    run dbpack "$(printf "%0$((pathmax - 5))d" 0)"
    run dbpack "$(printf "%0$((pathmax - 4))d" 0)"
    # A lone scratch index beside the sort file of a rebuild: a state the
    # index's recovery refuses, listing both files.  A rebuild stops there,
    # before the index NONE; a recovery goes on to NONE, which has nothing
    # to recover, and still fails.
    echo junk >"$scratch/data/ODD.%NX"
    echo junk >"$scratch/data/ODD.#NX"
    run dbpack ITEMS /R ODD NONE
    run dbpack ITEMS /C /R ODD NONE
    # A lone new table, which no stopped pack leaves.
    cp "$scratch/data/CATS.DBF" "$scratch/data/ODD.\$DB"
    run dbpack ODD /C
    # A pack of JUNK seemingly stopped after its commit point, but its new
    # table is no table and its old memo file has no new one: refused,
    # with every file listed, the last of the table's extensions too.
    mv "$scratch/data/JUNK.DBF" "$scratch/data/JUNK.%DB"
    echo junk >"$scratch/data/JUNK.\$DB"
    echo junk >"$scratch/data/JUNK.%DT"
    run dbpack JUNK /C
    # A pack of CATS stopped after its commit point: the old table moved
    # to CATS.%DB, the new one, a copy of USERS.DBF, still CATS.$DB.  /C
    # completes it, so CATS.DBF ends with the checksum of USERS.DBF.
    mv "$scratch/data/CATS.DBF" "$scratch/data/CATS.%DB"
    cp "$scratch/data/USERS.DBF" "$scratch/data/CATS.\$DB"
    run dbpack CATS /C
    echo "== files left"
    (cd "$scratch/data" && LC_ALL=C ls CATS.* JUNK.* ODD.* &&
        cksum CATS.DBF USERS.DBF)
} > "$scratch/out.txt" 2>&1

wrote=0
if [ "$update" -eq 1 ]; then
    mkdir -p TEST/GOLDEN
    cp "$scratch/out.txt" "$GOLDEN"
    echo "$prog: wrote $GOLDEN"
    wrote=1
elif [ ! -f "$GOLDEN" ]; then
    die "$GOLDEN missing (create it with -u)"
elif ! diff -u "$GOLDEN" "$scratch/out.txt"; then
    fail "output differs from $GOLDEN"
fi
[ $wrote = "$update" ] || fail "wrote $wrote goldens, -u $update"
echo "$prog: ok"
