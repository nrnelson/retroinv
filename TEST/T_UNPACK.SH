#!/bin/sh
# T_UNPACK.SH - host test of TEST/INTEG/unpack.sh on synthetic archives.
#
# usage: sh TEST/T_UNPACK.SH      (run from the repository root)
#
# The script runs 7z, so without 7z there is nothing to test: the test
# then says it is skipped and exits 0 (a build host may leave 7zip out);
# any other missing tool is a setup error.  No period archive is read:
# each archive is made here under the file name the script's table gives
# a package, a .7z or a .zip holding one text file named for the
# package, and each run gets a destination of its own.  It checks that:
#   - with every needed archive given and no optional one, the run exits
#     0, reporting each needed package "ok" with its file extracted into
#     the package's directory, and each optional archive "skipped", the
#     destination then holding the needed packages' directories alone; a
#     .<package>.tmp left there by an interrupted run is emptied first,
#     so nothing of it ends up in the package;
#   - a second run over that destination reports each package "exists"
#     or "skipped" and exits 0, leaving the files as they were;
#   - without one needed archive the run exits 1 and names it with the
#     make targets that need it ("needed by make ..."), extracting the
#     others;
#   - an archive holding a symbolic link, a member starting with "..", a
#     member with ".." inside its path or an absolute path is refused
#     (exit 1), nothing of it extracted, the other packages extracted;
#   - a corrupt archive, a .7z without compression whose listing passes
#     but whose first file fails its CRC after 7z has written both, fails
#     (exit 1, "failed:"), leaving neither its package directory nor its
#     .<package>.tmp;
#   - a wrong number of arguments exits 2.
#
# Exits 0 when every check passes or 7z is missing, 1 when a check
# fails, 2 on a setup error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

script=TEST/INTEG/unpack.sh
[ -f "$script" ] || die "run from the repository root"
if ! command -v 7z >/dev/null 2>&1; then
    echo "$prog: skipped: no 7z, unpack.sh cannot run here"
    exit 0
fi
need_tools python3

work=$(mktemp -d "${TMPDIR:-/tmp}/t_unpack.XXXXXX")
trap 'rm -rf "$work"' EXIT
on_signals

fails=0
checks=0
# check WHAT GOT WANT: count a check, and a failure when GOT is not WANT.
check() {
    checks=$((checks + 1))
    [ "$2" = "$3" ] && return
    echo "$prog: FAIL: $1: got '$2', expected '$3'" >&2
    fails=$((fails + 1))
}

# The table's rows, package|need|file, read from the script itself, and
# the needed ones.
sed -n "/^map='\$/,/^'\$/{/|/p;}" "$script" >"$work/rows"
grep -v '^[^|]*|-|' "$work/rows" >"$work/needed" ||
    die "no needed package in the table of $script"

# archive FILE MEMBER...: make the .7z or .zip FILE (by its name) of the
# files MEMBER... of $work/m, links stored as links.
archive() {
    af=$1
    shift
    (cd "$work/m" && 7z a -bso0 -bsp0 -snl -t"${af##*.}" "$af" "$@" \
        </dev/null) || die "7z cannot make $af"
}

# Every needed archive, in $work/all.
mkdir "$work/all" "$work/m"
while IFS='|' read -r pkg need file; do
    echo "$pkg" >"$work/m/PKG.TXT"
    archive "$work/all/$file" PKG.TXT
done <"$work/needed"

# unpack SRC DEST: run the script, its status in rc, its standard output
# in out, its standard error in err.
unpack() {
    rc=0
    sh "$script" "$1" "$2" >"$work/out" 2>"$work/err" </dev/null || rc=$?
}

# extracted WHAT DEST PKG: PKG's file was extracted into DEST/PKG.
extracted() {
    check "$1: $3" "$(cat "$2/$3/PKG.TXT" 2>/dev/null)" "$3"
}

# left WHAT DEST PKG...: DEST holds the directories of the needed
# packages but PKG... and nothing else.
left() {
    lw=$1 ld=$2
    shift 2
    check "$lw: directories" "$(ls -A "$ld" | sort)" \
        "$(cut -d'|' -f1 "$work/needed" | grep -vxF "$(printf '%s\n' "$@")" |
            sort)"
}

# copy DIR FILE: DIR holds every needed archive but FILE.
copy() {
    mkdir "$1"
    cp "$work/all/"* "$1/"
    rm "$1/$2"
}

# The first run finds the temporary directory of the first needed
# package as an interrupted run leaves it.
IFS='|' read -r tpkg _ _ <"$work/needed"
mkdir -p "$work/d1/.$tpkg.tmp"
echo stale >"$work/d1/.$tpkg.tmp/STALE"
unpack "$work/all" "$work/d1"
check "first run: status" "$rc" 0
while IFS='|' read -r pkg need file; do
    if [ "$need" = - ]; then
        check "first run: $pkg skipped" \
            "$(grep -Fxc "skipped: $file (optional)" "$work/out")" 1
    else
        check "first run: $pkg ok" "$(grep -Ec "^ok: +$pkg\$" "$work/out")" 1
        extracted "first run" "$work/d1" "$pkg"
    fi
done <"$work/rows"
check "first run: standard error" "$(cat "$work/err")" ""
check "first run: $tpkg's files" "$(ls -A "$work/d1/$tpkg")" PKG.TXT
left "first run" "$work/d1"

unpack "$work/all" "$work/d1"
check "second run: status" "$rc" 0
while IFS='|' read -r pkg need file; do
    check "second run: $pkg exists" \
        "$(grep -Ec "^exists: +$pkg\$" "$work/out")" 1
    extracted "second run" "$work/d1" "$pkg"
done <"$work/needed"
check "second run: skipped" "$(grep -c '^skipped: ' "$work/out")" \
    "$(($(wc -l <"$work/rows") - $(wc -l <"$work/needed")))"
check "second run: lines" "$(wc -l <"$work/out")" "$(wc -l <"$work/rows")"
left "second run" "$work/d1"

# The first needed archive missing.
IFS='|' read -r gone gneed gfile <"$work/needed"
copy "$work/less" "$gfile"
unpack "$work/less" "$work/d2"
check "missing: status" "$rc" 1
check "missing: message" "$(cat "$work/err")" \
    "missing: $gfile (needed by make $gneed)"
while IFS='|' read -r pkg need file; do
    [ "$pkg" = "$gone" ] || extracted "missing" "$work/d2" "$pkg"
done <"$work/needed"
left "missing" "$work/d2" "$gone"

# The first needed .7z and .zip, which the unsafe and corrupt archives
# stand in for.
grep '\.7z$' "$work/needed" | head -n 1 >"$work/s7z"
grep '\.zip$' "$work/needed" | head -n 1 >"$work/szip"
IFS='|' read -r spkg _ sfile <"$work/s7z" || die "the table lacks a .7z"
IFS='|' read -r zpkg _ zfile <"$work/szip" || die "the table lacks a .zip"

# refused WHAT DIR PKG FILE: the run over DIR, whose FILE (PKG's) is
# unsafe, exits 1, refusing FILE and extracting every other package.
refused() {
    unpack "$2" "$work/$1.d"
    check "$1: status" "$rc" 1
    check "$1: refused" \
        "$(grep -Fxc "refused: $4 (link, absolute path or ..)" "$work/err")" 1
    left "$1" "$work/$1.d" "$3"
}

# A symbolic link in the .7z; in the .zip a member starting with "..",
# one with ".." inside its path and an absolute path, each beside a file.
# python3 writes the names 7z would not.
echo text >"$work/m/A.TXT"
ln -s A.TXT "$work/m/LINK"
copy "$work/link" "$sfile"
archive "$work/link/$sfile" A.TXT LINK
refused link "$work/link" "$spkg" "$sfile"
printf '%s\n' 'import sys' 'import zipfile' '' \
    "with zipfile.ZipFile(sys.argv[1], 'w') as z:" \
    "    z.writestr('A.TXT', 'text\\n')" \
    "    z.writestr(sys.argv[2], 'text\\n')" >"$work/mkzip.py"
for c in dotdot:../UP.TXT inside:SUB/../../UP.TXT absolute:/ABS.TXT; do
    copy "$work/${c%%:*}" "$zfile"
    python3 -I "$work/mkzip.py" "$work/${c%%:*}/$zfile" "${c#*:}"
    refused "${c%%:*}" "$work/${c%%:*}" "$zpkg" "$zfile"
done

# A .7z without compression of two files, the first one's data changed:
# 7z lists it, writes both files and then fails the CRC.
copy "$work/corrupt" "$sfile"
printf '%064d\n' 0 >"$work/m/A.TXT"
(cd "$work/m" && 7z a -bso0 -bsp0 -mx0 "$work/corrupt/$sfile" A.TXT PKG.TXT \
    </dev/null) || die "7z cannot make $sfile"
printf X | dd of="$work/corrupt/$sfile" bs=1 seek=40 conv=notrunc 2>/dev/null
unpack "$work/corrupt" "$work/d4"
check "corrupt: status" "$rc" 1
check "corrupt: failed" "$(grep -Fxc "failed:  $sfile" "$work/err")" 1
left "corrupt" "$work/d4" "$spkg"

rc=0
sh "$script" "$work/all" >/dev/null 2>&1 </dev/null || rc=$?
check "one argument: status" "$rc" 2

if [ $fails -ne 0 ]; then
    echo "$prog: $fails of $checks checks failed" >&2
    exit 1
fi
echo "$prog: $checks checks passed"
