#!/bin/sh
# T_RCACHE.SH - host test of the run cache of the DOS emulator runs
# (TEST/runcache.sh and its use in MAKEFILE), without an emulator.
#
# usage: sh TEST/T_RCACHE.SH      (run from the repository root)
#
# First it asks MAKEFILE itself (make with a one-rule makefile added)
# what it gives five runs (two boots of T_PROTO on MS-DOS 3.10, T_SCR.SH
# on MS-DOS 3.10, T_CTEXT in DOSBox-X, T_INV.SH on MS-DOS 3.10) and
# checks that the script runs key TEST/KEYS/ and TEST/GOLDEN/, that a
# unit test's DOSBox-X run runs from, and keys, its own OBJ/RUN/<log>
# directory, that the runs of T_CLOCK, T_VIDEO and T_DOSX are never
# cached while T_CTEXT's are, and that fullgate and dostest1, the cache
# on, run with DOS_CACHE=write.
#
# Then it builds a scratch tree: MAKEFILE, SRCS.MK, SRC/ and the files
# of TEST/ and TEST/INTEG/ copied from here, and stand-ins for every
# other file the five runs' command lines and keyed paths name.  On PATH
# are stand-ins for qemu-system-i386, dosbox-x, dpkg-query, date and
# uname, and the few real tools the scripts need, nothing else.  There:
#   - every input that can change a result changes the key of exactly the
#     runs that read it: a program, a data file, one boot's script, a
#     golden, a key script, INV.EXE, INV.HLP gone, a file added to a run's
#     directory, every runner script, a file a test script reads
#     (SRC/CLIENT/LINK.H, SRCS.MK: only the runs of the scripts that read
#     it, or that name a script that does),
#     MAKEFILE, this cache's own script, BIN/QEXIT.COM, the boot floppy,
#     the emulators, a package version, the machine, DOSTIME_SCALE, TZ,
#     LANG, LC_ALL, the UTC offset, DOS_CACHE_SALT, the command line and
#     the pass pattern;
#   - modification times, the tree's place and DOS_JOBS change no key;
#   - a runner's reads are all found, however many a line holds, and a
#     file read by a runner other than a test script (SRC/COMPAT/COMPAT.H,
#     read by mapchk.sh, and STACK_MARGIN in TEST/T_SCRH.C, which wintcp.sh
#     reads as well as T_SCR.SH) changes every key;
#   - key fails for a command line without a runner, and global fails
#     for a runner that reads a file it cannot find or reads one in
#     another form (in backquotes, or without $( )), for a
#     missing emulator and for package versions it cannot read, and
#     removes expired entries and old temporary files but keeps the others;
#   - get misses before put and hits after it, giving back the log byte
#     for byte behind its "=== from the run cache" line; it misses for an
#     expired entry (unless DOS_CACHE_DAYS allows it), a cut-off entry,
#     another format, a missing "---" line and another manifest; drop
#     removes the entry once, printing its key;
#   - while large logs are stored under one key again and again, every get
#     gives back one whole log, and no temporary file is left.
#
# Exits 0 when every check passes, 1 when one fails, 2 on a setup error,
# 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

[ -f MAKEFILE ] && [ -f TEST/runcache.sh ] ||
    die "run from the repository root"

work=$(mktemp -d "${TMPDIR:-/tmp}/t_rcache.XXXXXX")
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
# status WHAT WANT COMMAND...: COMMAND's exit status must be WANT.
status() {
    sw=$1 st=$2
    shift 2
    sg=0
    "$@" >/dev/null 2>&1 || sg=$?
    check "$sw" $sg "$st"
}
# has WHAT WORD LIST: LIST must hold WORD.
has() { case " $3 " in *" $2 "*) check "$1" yes yes ;; *) check "$1" "$3" "a list with $2" ;; esac; }

# ------------------------------------------------------------- MAKEFILE
printf "rcq: ; @printf '%%s\\\\n' '\$(call dj,\$(W),\$(J))'\n" >"$work/q.mk"
# dj WHAT JOB: what MAKEFILE gives JOB, the run cache on.  In a fresh
# tree the first call also builds what MAKEFILE includes, OBJ/MSG.MK, and
# with it BIN/INV.MSG and BIN/INVSERV.MSG.
dj() { make -s --no-print-directory -f MAKEFILE -f "$work/q.mk" rcq W="$1" J="$2" DOS_RCG=x; }
jobs='OW/boot/T_PROTO/1 OW/boot/T_PROTO/2 OW/scr/T_SCR/- OW/cga/T_CTEXT/- OW/inv/T_INV/-'
i=0
for j in $jobs; do
    i=$((i + 1))
    dj cmd "$j" >"$work/cmd$i"
    dj keyx "$j" >"$work/keyx$i"
    dj pass "$j" >"$work/pass$i"
done
for j in OW/inv/T_INV/- OW/scr/T_SCR/- OW/prn/T_PRN/- OW/cgainv/T_INV/- OW/cgascr/T_SCR/- \
    OW/hgcscr/T_SCR/-; do
    k=$(dj keyx $j)
    has "$j keys TEST/KEYS" TEST/KEYS "$k"
    has "$j keys TEST/GOLDEN" TEST/GOLDEN "$k"
done
for j in OW/cga/T_CTEXT/- OW/hgc/T_VIDEO/- TC/cga/T_CTEXT/-; do
    d=OBJ/RUN/$(basename "$(dj log $j)")
    check "$j keys its directory" "$(dj keyx $j)" "$d"
    has "$j runs from its directory" "$d/$(echo "$j" | cut -d/ -f3).EXE" "$(dj cmd $j)"
done
for j in OW/boot/T_CLOCK/- OW/cga/T_CLOCK/- OW/boot/T_VIDEO/- OW/hgc/T_VIDEO/- OW/cga/T_DOSX/- \
    WC65/boot/T_DOSX/-; do
    check "$j is never cached" "$(dj cache $j)" ''
done
check "OW/boot/T_CTEXT/- is cached" "$(dj cache OW/boot/T_CTEXT/-)" 1
# runs TARGET: the make command TARGET runs with the cache on, whatever
# the caller's DOS_CACHE.
runs() { make -s --no-print-directory "$1" MAKE=echo DOS_CACHE=1; }
has "fullgate stores only" DOS_CACHE=write "$(runs fullgate)"
has "dostest1 stores only" DOS_CACHE=write "$(runs dostest1)"

# ------------------------------------------------------- the scratch tree
src=$work/src
mkdir -p "$src/TEST/INTEG" "$src/BIN" "$work/bin" "$work/min"
cp -R MAKEFILE SRCS.MK SRC "$src/"
for f in TEST/* TEST/INTEG/*; do [ ! -f "$f" ] || cp "$f" "$src/$f"; done
# Stand-ins: each path of the command lines, each keyed file, and a file
# in each keyed directory.
for i in 1 2 3 4 5; do
    for w in $(tr -d '"' <"$work/cmd$i"); do
        case $w in
        -*|*\\*|*:*) ;;
        */*) [ -f "$src/$w" ] || { mkdir -p "$src/${w%/*}"; echo "$w" >"$src/$w"; } ;;
        esac
    done
    for p in $(cat "$work/keyx$i"); do
        case $p in
        TEST/KEYS|TEST/GOLDEN|OBJ/RUN/*) mkdir -p "$src/$p"; echo "$p" >"$src/$p/STAND.IN" ;;
        *) mkdir -p "$src/${p%/*}"; echo "$p" >"$src/$p" ;;
        esac
    done
done
echo QEXIT >"$src/BIN/QEXIT.COM"
echo floppy >"$work/disk.img"
echo qemu >"$work/bin/qemu-system-i386"
echo dosbox >"$work/bin/dosbox-x"
echo 1.0 >"$work/pkgver"
printf '%s\n' '#!/bin/sh' \
    '[ -z "${FAKE_DPKG_FAIL:-}" ] || { echo "dpkg-query: no packages found" >&2; exit 1; }' \
    "for p; do case \$p in -*) ;; *) echo \"pkg \$p=\$(cat '$work/pkgver')\" ;; esac; done" \
    >"$work/bin/dpkg-query"
printf '%s\n' '#!/bin/sh' \
    "[ \"\$*\" = +%z ] && [ -n \"\${FAKE_UTC:-}\" ] && { echo \"\$FAKE_UTC\"; exit; }" \
    "exec $(command -v date) \"\$@\"" >"$work/bin/date"
printf '%s\n' '#!/bin/sh' \
    "[ \"\$*\" = -m ] && [ -n \"\${FAKE_ARCH:-}\" ] && { echo \"\$FAKE_ARCH\"; exit; }" \
    "exec $(command -v uname) \"\$@\"" >"$work/bin/uname"
chmod +x "$work/bin/"*
for t in sh cat cp rm mkdir mktemp mv chmod touch sed grep find sort sha256sum cksum head \
    tail cut wc cmp dirname basename realpath tr sleep; do
    ln -s "$(command -v $t)" "$work/min/$t"
done
PATH=$work/bin:$work/min
DOS310_IMG=$work/disk.img
DOS_CACHE_DIR=$work/cache
export PATH DOS310_IMG DOS_CACHE_DIR
unset DOSTIME_SCALE TZ LANG LC_ALL DOS_JOBS DOS_CACHE_DAYS DOS_CACHE_SALT

tree=$work/a
rc() { (cd "$tree" && sh TEST/runcache.sh "$@"); }

# keys: the keys of the five runs, each cut to 12 digits.  ARGS1 and PASS1
# change boot 1's command line and pass pattern.
key_of() {
    # shellcheck disable=SC2046
    rc key "$work/g" "$1" "$2" "$3" $(cat "$4") | sha256sum | cut -c1-12
}
keys() {
    rc global "$work/g"
    # shellcheck disable=SC2086
    set -- $jobs
    echo "$(key_of "$1" "${PASS1:-$(cat "$work/pass1")}" "$(cat "$work/cmd1")${ARGS1:-}" \
            "$work/keyx1")" \
        "$(key_of "$2" "$(cat "$work/pass2")" "$(cat "$work/cmd2")" "$work/keyx2")" \
        "$(key_of "$3" "$(cat "$work/pass3")" "$(cat "$work/cmd3")" "$work/keyx3")" \
        "$(key_of "$4" "$(cat "$work/pass4")" "$(cat "$work/cmd4")" "$work/keyx4")" \
        "$(key_of "$5" "$(cat "$work/pass5")" "$(cat "$work/cmd5")" "$work/keyx5")"
}
fresh() { rm -rf "$tree"; cp -R "$src" "$tree"; }
fresh
base=$(keys)

# changed WHAT MASK: the runs whose keys differ from base, as five 0/1
# digits in the order of jobs, must be MASK; the tree is then made afresh.
changed() {
    now=$(keys)
    mask=
    set -- "$1" "$2" $base
    what=$1 want=$2
    shift 2
    for k in $now; do
        if [ "$k" = "$1" ]; then mask=${mask}0; else mask=${mask}1; fi
        shift
    done
    check "$what" "$mask" "$want"
    fresh
}
bump() { echo '# x' >>"$tree/$1"; }
# word N PATTERN: the first word of run N's command line matching PATTERN.
word() { tr ' ' '\n' <"$work/cmd$1" | grep -m 1 "$2"; }

bump BIN/TEST/T_PROTO.EXE; changed "the program" 11000
bump "$(word 1 CONFIG.SYS)"; changed "a data file of both boots" 11000
bump "$(word 1 TEST/SCRIPTS/)"; changed "boot 1's script" 10000
bump TEST/GOLDEN/STAND.IN; changed "a golden" 00101
bump TEST/KEYS/STAND.IN; changed "a key script" 00101
bump BIN/INV.EXE; changed "INV.EXE" 00101
rm "$tree/BIN/INV.HLP"; changed "INV.HLP gone" 00101
bump "$(word 4 OBJ/RUN/)"; changed "the DOSBox-X run's program" 00010
echo x >"$tree/$(cat "$work/keyx4")/MORE.TXT"; changed "a file added to a run's directory" 00010
sed 's/^\(#define[ \t]*STACK_MARGIN[ \t]*\)[0-9]*/\160000/' "$src/TEST/T_SCRH.C" >"$tree/TEST/T_SCRH.C"
changed "STACK_MARGIN in TEST/T_SCRH.C, which wintcp.sh reads" 11111
bump SRC/CLIENT/LINK.H; changed SRC/CLIENT/LINK.H 00100
bump SRCS.MK; changed SRCS.MK 00001
# A test script that names another in its code reads what that one reads.
echo ': T_INV.SH' >>"$src/TEST/T_SCR.SH"
fresh
base=$(keys)
bump SRCS.MK; changed "SRCS.MK with T_SCR.SH naming T_INV.SH" 00101
sed -i '$d' "$src/TEST/T_SCR.SH"
fresh
base=$(keys)
for f in TEST/INTEG/boot310.sh TEST/rundos.sh TEST/T_SCR.SH TEST/T_INV.BAT \
    TEST/shlib.sh TEST/runcache.sh MAKEFILE BIN/QEXIT.COM; do
    bump "$f"; changed "$f" 11111
done
echo x >"$tree/TEST/INTEG/new.sh"; changed "a new runner script" 11111
echo x >>"$work/disk.img"; changed "the boot floppy" 11111
echo floppy >"$work/disk.img"
echo x >>"$work/bin/qemu-system-i386"; changed "qemu-system-i386" 11111
echo qemu >"$work/bin/qemu-system-i386"
echo x >>"$work/bin/dosbox-x"; changed "dosbox-x" 11111
echo dosbox >"$work/bin/dosbox-x"
echo 1.1 >"$work/pkgver"; changed "a package version" 11111
echo 1.0 >"$work/pkgver"
for v in FAKE_ARCH=i686 FAKE_UTC=+0900 DOSTIME_SCALE=2 TZ=Asia/Tokyo LANG=C LC_ALL=C \
    DOS_CACHE_SALT=box2; do
    export "${v%%=*}=${v#*=}"
    changed "${v%%=*}" 11111
    unset "${v%%=*}"
done
ARGS1=' 40'; changed "boot 1's command line" 10000
ARGS1=
PASS1='^T_PROTO PASS'; changed "boot 1's pass pattern" 10000
PASS1=
find "$tree" -exec touch -d '2001-01-01' {} +; changed "modification times" 00000
DOS_JOBS=3; export DOS_JOBS; changed DOS_JOBS 00000
unset DOS_JOBS
tree=$work/elsewhere/b; mkdir -p "$work/elsewhere"; fresh; changed "the tree's place" 00000
tree=$work/a; fresh

status "key without a runner" 1 rc key "$work/g" OW/x/T_X/- '' 'BIN/INV.EXE'
# Runners' reads.  The lines are put together here from pieces of the
# helper's name, so that this script, itself a runner, does not hold them.
c='cde''fine'
bump SRC/COMPAT/COMPAT.H; changed "SRC/COMPAT/COMPAT.H, which mapchk.sh reads" 11111
echo "a=\$($c SRCS.MK N) b=\$($c \"TEST/T_SCRH.C\" N)" >>"$src/TEST/INTEG/two.sh"
fresh
base=$(keys)
bump SRCS.MK; changed "the first of two reads on a line" 11111
bump TEST/T_SCRH.C; changed "the second of two reads on a line" 11111
rm "$src/TEST/INTEG/two.sh"
fresh
base=$(keys)
for line in "x=\$($c \"\$y/F.H\" N)" "x=\`$c SRCS.MK N\`" "$c SRCS.MK N >o"; do
    echo "$line" >"$tree/TEST/INTEG/bad.sh"
    status "global with the read $line" 1 rc global "$work/g"
done
fresh
mv "$work/bin/dosbox-x" "$work/dosbox-x"
status "global without dosbox-x" 1 rc global "$work/g"
mv "$work/dosbox-x" "$work/bin/dosbox-x"
FAKE_DPKG_FAIL=1; export FAKE_DPKG_FAIL
status "global without package versions" 1 rc global "$work/g"
unset FAKE_DPKG_FAIL
rc global "$work/g"

# get, put and drop.
man=$work/m
rc key "$work/g" OW/boot/T_X/1 '^T_X PASS [0-9]' "sh TEST/rundos.sh BIN/INV.EXE" >"$man"
k=$(sha256sum <"$man" | cut -c1-64)
entry=$DOS_CACHE_DIR/v1/$(echo "$k" | cut -c1-2)/$k
n=$(wc -l <"$man")
printf 'line 1\nT_X PASS 3' >"$work/log"
status "get before put" 1 rc get "$man" "$work/out"
rc put "$man" "$work/log" 42
when=$(rc get "$man" "$work/out") || when=
check "get after put" "$(head -n 1 "$work/out")" \
    "=== from the run cache: passed $when in 42s, key $k"
check "the stored log" "$(tail -n +2 "$work/out" | cksum)" "$(cksum <"$work/log")"
cp "$entry" "$work/whole"
touch -d '8 days ago' "$entry"
status "get of an expired entry" 1 rc get "$man" "$work/out"
DOS_CACHE_DAYS=9; export DOS_CACHE_DAYS
status "get within DOS_CACHE_DAYS" 0 rc get "$man" "$work/out"
unset DOS_CACHE_DAYS
head -n 3 "$work/whole" >"$entry"
status "get of a cut-off entry" 1 rc get "$man" "$work/out"
sed '1s/^RUNCACHE 1 /RUNCACHE 2 /' "$work/whole" >"$entry"
status "get of another format" 1 rc get "$man" "$work/out"
sed "$((n + 2))s/^---\$/--x/" "$work/whole" >"$entry"
status "get without the --- line" 1 rc get "$man" "$work/out"
sed '3s/^/x/' "$work/whole" >"$entry"
status "get of an entry with another manifest" 1 rc get "$man" "$work/out"
cp "$work/whole" "$entry"
check "drop of an entry: its key, status 0" "$(rc drop "$man"; echo $?)" \
    "$(sha256sum <"$man" | cut -c1-64)
0"
status "get after drop" 1 rc get "$man" "$work/out"
status "drop without an entry" 1 rc drop "$man"

# Readers while two large logs are stored under one key again and again.
for v in a b; do
    echo "log $v" >"$work/big$v"
    i=0
    while [ $i -lt 18 ]; do
        cat "$work/big$v" "$work/big$v" >"$work/twice"; mv "$work/twice" "$work/big$v"
        i=$((i + 1))
    done
done
sums=" $(cksum <"$work/biga") $(cksum <"$work/bigb") "
(i=0; while [ $i -lt 60 ]; do
    rc put "$man" "$work/biga" 1; rc put "$man" "$work/bigb" 2; i=$((i + 1))
done) &
writer=$!
torn=0
reads=0
while kill -0 $writer 2>/dev/null; do
    if rc get "$man" "$work/out" >/dev/null; then
        reads=$((reads + 1))
        case $sums in *" $(tail -n +2 "$work/out" | cksum) "*) ;; *) torn=$((torn + 1)) ;; esac
    fi
done
wait $writer
check "logs read whole while stored ($reads reads)" $torn 0
check "temporary files after the stores" "$(find "$DOS_CACHE_DIR" -name '.tmp.*' | wc -l)" 0

# global's pruning.
old=$DOS_CACHE_DIR/v1/00/old tmp=$DOS_CACHE_DIR/v1/00/.tmp.old
mkdir -p "${old%/*}"
: >"$old"; : >"$tmp"
touch -d '8 days ago' "$old"; touch -d '2 days ago' "$tmp"
rc global "$work/g"
status "global removes an expired entry" 1 test -f "$old"
status "global removes an old temporary file" 1 test -f "$tmp"
status "global keeps a valid entry" 0 test -f "$entry"

[ $fails = 0 ] || fail "$fails of $checks checks failed"
echo "$prog: $checks checks passed"
