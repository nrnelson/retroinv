#!/bin/sh
# T_WATPR.SH - host test of SRC/TCP/WATPREP.PY on synthetic archives.
#
# usage: sh TEST/T_WATPR.SH      (run from the repository root)
#
# Needs GNU patch, as the script does.  No WATTCP file is read.  The
# archives are built here under the member names of WATTCP's, from our
# own text: TEST/WATPR/ORIG.TXT, with CR LF line ends and a Ctrl-Z at its
# end as WATTCP's files have, is SRC/PCTCP.C and INCLUDE/TCP.H; EDIT.TXT,
# made the same way, is ORIG.TXT with two lines changed, the second near
# the end; the other members are one-line stand-ins, beside directory
# entries.  Each check runs a copy of the script that expects the
# archive's SHA-256, except those of the SHA-256 and the arguments.  It
# checks that:
#   - an archive with another SHA-256 and a missing one fail, naming the
#     path and ARCHIVE_SHA256;
#   - a symbolic link, an absolute or drive path and a ".." member are
#     refused, as are an archive without a kernel module and ones holding
#     a kernel module or a header twice;
#   - the kernel modules and the headers directly under INCLUDE/ are the
#     only members extracted;
#   - ORIG.TXT becomes TEST/WATPR/XFORM.TXT (CR LF): the Ctrl-Z goes,
#     "//" comments outside strings, character constants and /* */
#     comments become /* */ comments, WATTCP's header names are upper case
#     with their base cut to eight characters, in quotes, and <mem.h> and
#     <values.h> are "WTSHIM.H"; a "//" comment ending in a backslash is
#     refused;
#   - a DIF made with diff -u from ORIG.TXT to EDIT.TXT, after a "diff"
#     command line, gives the tree of the archive holding EDIT.TXT, and so
#     does the same DIF after free text holding diff-like lines;
#   - refused, leaving no tree and no work directory, are: a DIF made
#     from ORIG.TXT and EDIT.TXT without their first line (which patch
#     would apply at an offset, and says so); one whose first hunk's
#     context is off by one line (made from them without that hunk's
#     first line); one already applied; one cut short; one with a foreign
#     line inside a hunk; one whose hunk opens with a "\ No newline"
#     line; one with CR LF line ends throughout, against members with LF
#     ends (which patch would apply without --binary); one naming
#     a member not extracted, one with other names in its two headers,
#     one creating a file and one with /dev/null in a header; hunks out of
#     order, overlapping, or emptying a file; a file section without a
#     hunk, at the end or before another; a hunk before any file header;
#     a context-diff section, a git diff and a git rename; text with no
#     file section; and a missing DIF;
#   - without patch on PATH the script fails, naming the package;
#   - .ok holds the SHA-256 of the archive, the DIF and the script, and
#     the tree has the mode the umask gives a new directory;
#   - a second run gives the same tree, an empty directory is replaced,
#     and a directory that is not empty and holds no .ok, or a symbolic
#     link, is not replaced and nothing in it is touched;
#   - a wrong number of arguments exits 2.
#
# Exits 0 when every check passes, 1 when one fails, 2 on a setup error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

script=SRC/TCP/WATPREP.PY
fix=TEST/WATPR
[ -f "$script" ] && [ -d "$fix" ] || die "run from the repository root"
py=$(command -v python3) || die "python3 not installed"
command -v patch >/dev/null || die "patch not installed"

work=$(mktemp -d "${TMPDIR:-/tmp}/t_watpr.XXXXXX")
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

# same WHAT DIFFARG...: "diff -r DIFFARG..." must find no difference.
same() {
    sw=$1
    shift
    check "$sw" "$(diff -r "$@" >/dev/null 2>&1 && echo same)" same
}

# said WHAT TEXT: the last run's standard error must hold TEXT.
said() { check "$1: says $2" "$(grep -c -F -e "$2" "$work/err")" 1; }

# sha FILE: the SHA-256 of FILE.
sha() { sha256sum "$1" | cut -d ' ' -f 1; }

# The script's ARCHIVE_SHA256 and KERNEL.
set -- $("$py" -I -c 'import runpy, sys
g = runpy.run_path(sys.argv[1])
print(g["ARCHIVE_SHA256"], *g["KERNEL"])' "$script")
want_sha=$1
shift
kernel=$*

# mkzip ZIP MEMBER=FILE...: write ZIP holding each FILE as MEMBER, stored
# as a symbolic link when MEMBER starts with "@"; a MEMBER ending in "/"
# is a directory entry and takes no FILE.
mkzip() {
    "$py" -I -W ignore -c 'import stat, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w") as z:
    for arg in sys.argv[2:]:
        name, path = arg.split("=", 1)
        info = zipfile.ZipInfo(name.lstrip("@"))
        if name.startswith("@"):
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
        if name.endswith("/"):
            z.writestr(info, b"")
            continue
        with open(path, "rb") as f:
            z.writestr(info, f.read())' "$@"
}

# prep ZIP DIF OUT [PATH]: run $work/WATPREP.PY, a copy of the script
# that expects ZIP's SHA-256, under PATH when given; its status in rc,
# its standard error in $work/err.
prep() {
    sed "/^ARCHIVE_SHA256 = /,/)\$/c\\
ARCHIVE_SHA256 = '$(sha "$1")'" "$script" >"$work/WATPREP.PY"
    grep -q "^ARCHIVE_SHA256 = '[0-9a-f]*'\$" "$work/WATPREP.PY" ||
        die "cannot set ARCHIVE_SHA256 in a copy of $script"
    rc=0
    PATH=${4:-$PATH} "$py" -I "$work/WATPREP.PY" "$1" "$2" "$3" \
        2>"$work/err" || rc=$?
}

# refused WHAT ZIP DIF [TEXT]: prep must fail, saying TEXT when given,
# and leave neither a tree nor a work directory.
refused() {
    rm -rf "$work/out"
    prep "$2" "$3" "$work/out"
    check "$1: status" "$rc" 1
    check "$1: what is left" \
        "$(ls -A "$work" | grep -c -e '^out$' -e '^\.watprep')" 0
    [ $# -lt 4 ] || said "$1" "$4"
}

# The members and their texts.
dos_text "$fix/ORIG.TXT" >"$work/orig.c"
dos_text "$fix/EDIT.TXT" >"$work/edit.c"
printf '\032' >>"$work/orig.c"
printf '\032' >>"$work/edit.c"
dos_text "$fix/XFORM.TXT" >"$work/xform.c"
printf '/* a stand-in */\r\n' >"$work/stub"
printf 'int i;\r\n// a comment spliced \\\r\nint j;\r\n' >"$work/splice.c"
: >"$work/empty.dif"
members='SRC/= INCLUDE/='
for m in $kernel; do
    [ "$m" = PCTCP.C ] || members="$members SRC/$m=$work/stub"
done
members="$members INCLUDE/COPYRIGH.H=$work/stub INCLUDE/WATTCP.H=$work/stub"
members="$members README=$work/stub SRC/PCPKT.C=$work/stub"
members="$members APPS/PING.C=$work/stub INCLUDE/SUB/X.H=$work/stub"
mkzip "$work/orig.zip" $members SRC/PCTCP.C="$work/orig.c" \
    INCLUDE/TCP.H="$work/orig.c"
mkzip "$work/edit.zip" $members SRC/PCTCP.C="$work/edit.c" \
    INCLUDE/TCP.H="$work/orig.c"

# The DIFs.
# mkdif OLD NEW DIF: DIF is diff -u from OLD to NEW as SRC/PCTCP.C.
mkdif() {
    diff -u --label a/SRC/PCTCP.C --label b/SRC/PCTCP.C "$1" "$2" >"$3" ||
        [ $? -eq 1 ] || die "diff failed"
}
mkdif "$work/orig.c" "$work/edit.c" "$work/edit.dif"
{ echo 'diff -u a/SRC/PCTCP.C b/SRC/PCTCP.C'; cat "$work/edit.dif"; } \
    >"$work/good.dif"
printf '%s\n' 'Free text before the diff, not given to patch:' \
    '+++ b/SRC/PCARP.C' '@@ -1 +1 @@' '-/* a stand-in */' '+changed' '' |
    cat - "$work/good.dif" >"$work/preamble.dif"
# The parts of edit.dif: its two header lines (part0) and its hunks.
(cd "$work" && awk '/^@@/ { n++ } { print > ("part" n + 0) }' edit.dif)
cat "$work/part0" "$work/part2" "$work/part1" >"$work/order.dif"
cat "$work/part0" "$work/part1" "$work/part1" >"$work/overlap.dif"
sed 1d "$work/orig.c" >"$work/orig1"
sed 1d "$work/edit.c" >"$work/edit1"
mkdif "$work/orig1" "$work/edit1" "$work/shift.dif"
first=$(sed -n 's/^@@ -\([0-9]*\),.*/\1/p' "$work/good.dif" | head -n 1)
sed "${first}d" "$work/orig.c" >"$work/origx"
sed "${first}d" "$work/edit.c" >"$work/editx"
mkdif "$work/origx" "$work/editx" "$work/off.dif"
sed '$d' "$work/good.dif" | sed '$d' >"$work/cut.dif"
sed '/^@@/{n;s/^ /X/;}' "$work/good.dif" >"$work/foreign.dif"
# A DIF with CR LF line ends throughout, against members with LF ends,
# which patch without --binary would apply by dropping the CRs.
mkdif "$fix/ORIG.TXT" "$fix/EDIT.TXT" "$work/lf.dif"
dos_text "$work/lf.dif" >"$work/crlf.dif"
mkzip "$work/lf.zip" $members SRC/PCTCP.C="$fix/ORIG.TXT"
awk '{ print } /^@@/ && !n++ { print "\\ No newline at end of file" }' \
    "$work/good.dif" >"$work/marker.dif"
sed 's,/SRC/PCTCP\.C,/SRC/PCPKT.C,' "$work/good.dif" >"$work/other.dif"
sed 's,^--- a/SRC/PCTCP\.C,--- a/SRC/PCARP.C,' "$work/good.dif" \
    >"$work/mixed.dif"
printf '%s\n' '--- a/SRC/NEW.C' '+++ b/SRC/NEW.C' '@@ -0,0 +1 @@' '+x' \
    >"$work/create.dif"
printf '%s\n' '--- /dev/null' '+++ b/SRC/PCTCP.C' '@@ -0,0 +1 @@' '+x' \
    >"$work/devnull.dif"
printf -- '--- a/SRC/PCARP.C\n+++ b/SRC/PCARP.C\n@@ -1 +0,0 @@\n' \
    >"$work/empties.dif"
printf -- '-/* a stand-in */\r\n' >>"$work/empties.dif"
printf -- '--- a/SRC/PCARP.C\n+++ b/SRC/PCARP.C\n' >"$work/nohunk"
cat "$work/good.dif" "$work/nohunk" >"$work/nohunk1.dif"
cat "$work/nohunk" "$work/good.dif" >"$work/nohunk2.dif"
printf '%s\n' 'diff -u a/SRC/PCTCP.C b/SRC/PCTCP.C' '@@ -1 +1 @@' '-x' '+y' \
    >"$work/headless.dif"
printf '%s\n' '*** a/SRC/NEW.C' '--- b/SRC/NEW.C' '***************' \
    '*** 0 ****' '--- 1 ----' '+ x' >"$work/context.dif"
{ echo 'diff --git a/SRC/PCTCP.C b/SRC/PCTCP.C'; cat "$work/edit.dif"; } \
    >"$work/git.dif"
printf '%s\n' 'diff --git a/SRC/PCTCP.C b/SRC/PCX.C' 'similarity index 100%' \
    'rename from SRC/PCTCP.C' 'rename to SRC/PCX.C' >"$work/rename.dif"
echo 'no diff here' >"$work/text.dif"
check "the edit DIF has two hunks" "$(grep -c '^@@' "$work/good.dif")" 2
check "the edit DIF marks its last line as one without a newline" \
    "$(grep -c '^\\' "$work/good.dif")" 1

# ----------------------------------------------------------- the archive
rc=0
"$py" -I "$script" "$work/orig.zip" "$work/empty.dif" "$work/out" \
    2>"$work/err" || rc=$?
check "another SHA-256: status" "$rc" 1
check "another SHA-256: message" \
    "$(grep -c "$work/orig.zip.*expected $want_sha" "$work/err")" 1
rc=0
"$py" -I "$script" "$work/none.zip" "$work/empty.dif" "$work/out" \
    2>"$work/err" || rc=$?
check "no archive: status" "$rc" 1
check "no archive: message" \
    "$(grep -c "$work/none.zip.*$want_sha" "$work/err")" 1
check "no archive: no tree" "$(ls -A "$work" | grep -c '^out$')" 0

mkzip "$work/bad.zip" $members SRC/PCTCP.C="$work/orig.c" \
    @INCLUDE/TCP.H="$work/stub"
refused "a link" "$work/bad.zip" "$work/empty.dif" 'unsafe member'
for bad in /SRC/X.C 'C:/SRC/X.C' SRC/../X.C 'INCLUDE/..\X.H' ../X.C; do
    mkzip "$work/bad.zip" $members SRC/PCTCP.C="$work/orig.c" \
        "$bad=$work/stub"
    refused "member $bad" "$work/bad.zip" "$work/empty.dif" 'unsafe member'
done
mkzip "$work/bad.zip" $members
refused "no SRC/PCTCP.C" "$work/bad.zip" "$work/empty.dif" \
    'SRC/PCTCP.C missing'
mkzip "$work/bad.zip" $members SRC/PCTCP.C="$work/orig.c" \
    SRC/PCTCP.C="$work/orig.c"
refused "a kernel module twice" "$work/bad.zip" "$work/empty.dif" \
    'SRC/PCTCP.C held twice'
mkzip "$work/bad.zip" $members SRC/PCTCP.C="$work/orig.c" \
    INCLUDE/TCP.H="$work/orig.c" INCLUDE/TCP.H="$work/orig.c"
refused "a header twice" "$work/bad.zip" "$work/empty.dif" \
    'INCLUDE/TCP.H held twice'
mkzip "$work/bad.zip" $members SRC/PCTCP.C="$work/splice.c"
refused "a spliced // comment" "$work/bad.zip" "$work/empty.dif" \
    'SRC/PCTCP.C: a // comment ends in a backslash'

# ------------------------------------- extraction, transforms, the tree
prep "$work/orig.zip" "$work/empty.dif" "$work/out"
check "empty DIF: status" "$rc" 0
want=$(for m in .ok INCLUDE/COPYRIGH.H INCLUDE/TCP.H INCLUDE/WATTCP.H; do
    echo "./$m"; done; for m in $kernel; do echo "./SRC/$m"; done)
check "the members extracted" \
    "$(cd "$work/out" && find . -type f | sort)" "$(echo "$want" | sort)"
same "SRC/PCTCP.C transformed" "$work/xform.c" "$work/out/SRC/PCTCP.C"
same "INCLUDE/TCP.H transformed" "$work/xform.c" "$work/out/INCLUDE/TCP.H"
same "a stand-in unchanged" "$work/stub" "$work/out/SRC/PCARP.C"
check "the stamp" "$(cat "$work/out/.ok")" "archive $(sha "$work/orig.zip")
dif $(sha "$work/empty.dif")
script $(sha "$work/WATPREP.PY")"
check "the tree's mode" "$(stat -c %a "$work/out")" \
    "$(printf '%o' $((0777 & ~$(umask))))"
cp -R "$work/out" "$work/first"
prep "$work/orig.zip" "$work/empty.dif" "$work/out"
check "second run: status" "$rc" 0
same "second run: the same tree" "$work/first" "$work/out"
rm -rf "$work/out" "$work/first"
mkdir "$work/out"
prep "$work/orig.zip" "$work/empty.dif" "$work/out"
check "an empty directory: replaced" "$rc/$(ls -A "$work/out" | wc -l)" 0/3
rm -rf "$work/out"
mkdir "$work/out"
echo keep >"$work/out/KEEP"
prep "$work/orig.zip" "$work/empty.dif" "$work/out"
check "a directory without .ok: status" "$rc" 1
check "a directory without .ok: kept" "$(ls -A "$work/out")" KEEP
rm -rf "$work/out"
mkdir "$work/target"
echo keep >"$work/target/.ok"
ln -s target "$work/out"
prep "$work/orig.zip" "$work/empty.dif" "$work/out"
check "a link: status" "$rc" 1
check "a link: kept" "$(readlink "$work/out")" target
check "a link: its target kept" "$(cat "$work/target/.ok")" keep
rm -rf "$work/out" "$work/target"

# ------------------------------------------------------------- the DIFs
prep "$work/edit.zip" "$work/empty.dif" "$work/edited"
check "the edited archive: status" "$rc" 0
for d in good preamble; do
    rm -rf "$work/out"
    prep "$work/orig.zip" "$work/$d.dif" "$work/out"
    check "$d.dif: status" "$rc" 0
    same "$d.dif: the edited tree" -x .ok "$work/edited" "$work/out"
    check "$d.dif: the stamp names it" \
        "$(sed -n 2p "$work/out/.ok")" "dif $(sha "$work/$d.dif")"
done
refused shift.dif "$work/orig.zip" "$work/shift.dif" 'Hunk #1 succeeded at'
refused off.dif "$work/orig.zip" "$work/off.dif" 'FAILED at'
refused "already applied" "$work/edit.zip" "$work/good.dif" \
    'previously applied'
refused crlf.dif "$work/lf.zip" "$work/crlf.dif" 'patch failed'
refused cut.dif "$work/orig.zip" "$work/cut.dif" 'hunk cut short'
refused marker.dif "$work/orig.zip" "$work/marker.dif" 'hunk cut short'
refused foreign.dif "$work/orig.zip" "$work/foreign.dif" 'hunk cut short'
for d in other mixed create devnull; do
    refused $d.dif "$work/orig.zip" "$work/$d.dif" \
        'do not name one file of the tree'
done
refused order.dif "$work/orig.zip" "$work/order.dif" \
    'hunk before the end of the one before'
refused overlap.dif "$work/orig.zip" "$work/overlap.dif" \
    'hunk before the end of the one before'
refused empties.dif "$work/orig.zip" "$work/empties.dif" \
    'hunk empties SRC/PCARP.C'
refused nohunk1.dif "$work/orig.zip" "$work/nohunk1.dif" \
    'no hunk for SRC/PCARP.C'
refused nohunk2.dif "$work/orig.zip" "$work/nohunk2.dif" \
    'no hunk for SRC/PCARP.C'
refused headless.dif "$work/orig.zip" "$work/headless.dif" \
    ':2: not a line of a unified diff'
refused context.dif "$work/orig.zip" "$work/context.dif" \
    ':2: not a line of a unified diff'
refused git.dif "$work/orig.zip" "$work/git.dif" \
    ':1: not a line of a unified diff'
refused rename.dif "$work/orig.zip" "$work/rename.dif" \
    ':1: not a line of a unified diff'
refused text.dif "$work/orig.zip" "$work/text.dif" 'no file section'
refused "no DIF" "$work/orig.zip" "$work/none.dif" 'DIF not found'
prep "$work/orig.zip" "$work/good.dif" "$work/out" /nonexistent
check "no patch: status" "$rc/$(ls -A "$work" | grep -c '^out$')" 1/0
said "no patch" 'Debian package patch'

rc=0
"$py" -I "$script" "$work/orig.zip" "$work/empty.dif" 2>/dev/null || rc=$?
check "two arguments: status" "$rc" 2

if [ $fails -ne 0 ]; then
    echo "$prog: $fails of $checks checks failed" >&2
    exit 1
fi
echo "$prog: $checks checks passed"
