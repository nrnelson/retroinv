#!/bin/sh
# wintest.sh - one run of a Windows test program on the Windows 3.0 test
# machine, judged by the file it leaves: its report, checked line by line,
# or the snapshot of the test driver, compared with its golden.
#
# usage: sh TEST/WIN/wintest.sh [-e EMU] [-m MODE] [-M WORD] [-t SECS]
#            [-s SOCKET] [-p MONITOR] [-I INTL] [-f FILE]...
#            [-d DIR [-q REQUESTS... -i HOSTINV]] [-k KEYS] [-g GOLDEN]
#            [-c FILE]... [-o PRINTED] RUNDIR EXE [ARG...]
#
#   RUNDIR      the run's directory, made afresh: EXE and the files of -f
#               go into it, and TEST/WIN/runwin.sh runs the program there
#               as "NAME ARG...", NAME being EXE's name without .EXE.  It
#               is removed once the run has passed; a failed run keeps it,
#               and beside it, from a QEMU time-out, RUNDIR.PNG, its
#               screen, and RUNDIR.MON, its registers and interrupt
#               controllers, or, when QEMU ended early, RUNDIR.RST, its
#               log of the CPU's resets, quits and run states.
#   -e EMU, -m MODE, -t SECS, -s SOCKET, -p MONITOR  runwin.sh's emulator
#               (qemu, the default, or dosbox), mode (r, the default, s or
#               3), limit in seconds before DOSTIME_SCALE (default 120),
#               the socket of the machine's COM1 (default none) and the
#               path of QEMU's monitor (default runwin.sh's)
#   -I INTL     runwin.sh's International settings: the file whose lines
#               replace WIN.INI's [intl] section for this run
#   -M WORD     the mode the report's MODE line must name: REAL, STANDARD
#               or ENHANCED; needed without -k.  It is not taken from -m,
#               so that a run started in another mode than meant fails.
#   -f FILE     a file copied beside the program (repeatable)
#   -d DIR      an empty directory DIR made beside the program; DIR may be
#               a list of directories joined by commas, each made empty,
#               a '/' in it naming a directory inside another (MBX/IN)
#   -q REQUESTS fill DIR, the first of the list, as TEST/T_SCR.SH seeds
#               its runs: the host INV of -i runs "INV /CMD /LOCAL DIR
#               /NOW:..." in RUNDIR on the requests, none of which may be
#               refused (repeatable: one run for each file, in order)
#   -k KEYS     a key script of the test driver (SRC/WIN/TDRV.H), copied
#               beside the program, which is given "/KEYS:K /SNAP:S" after
#               ARG, K being the script's name and S that name with the
#               extension .SNP; that snapshot is the run's result, which
#               -g must name a golden for
#   -g GOLDEN   the golden the snapshot of -k must equal byte for byte,
#               but for its =STACK UNUSED= line, whose bytes never used
#               must be STACK_MARGIN of TEST/T_SCRH.C or more (snap_check
#               of TEST/shlib.sh); only with -k
#   -c FILE     a golden of a file the program writes beside it, under
#               FILE's name, which the run must leave equal to FILE byte
#               for byte (repeatable); only with -k
#   -o PRINTED  the golden of what the run printed: PRN_OUT, the file of
#               RUNDIR that the machine's printer writes (the port of
#               TEST/WIN/WIN.INI), must equal PRINTED byte for byte
#
# Without -k the result is the report NAME.OUT (TEST/WSMOKE.C,
# TEST/WSOCKT.C, TEST/WTEST.C), lines "WORD KEY VALUE", CR LF: its MODE
# line must name the mode of -M, an EXIT line must say 0, and a STACK
# line (TEST/WTEST.C's) at least STACK_MARGIN.  The result is printed,
# without its CRs, after runwin.sh's own lines, for the caller to look
# for the lines it wants.
#
# Exit status: 0 when the run ended and its result passed; 1 when it did
# not (the reason on standard error); runwin.sh's status when Windows did
# not end (200 to 202); 2 on a usage or setup error; 129, 130 or 143 on
# SIGHUP, SIGINT or SIGTERM, which runwin.sh, and through it the
# emulator, gets too before the script ends.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../shlib.sh"

# The stack bytes a run must leave unused, the screen tests' margin
# (TEST/T_SCRH.C).
STACK_MARGIN=$(cdefine "$here/../T_SCRH.C" STACK_MARGIN)

# halt SIGNAL - the signal handler (on_signals): end runwin.sh, which
# ends its emulator, and wait for it.
rwpid=
halt() {
    [ -z "$rwpid" ] || { kill -TERM "$rwpid" 2>/dev/null; wait "$rwpid"; } || :
}
on_signals halt

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

# The date and time the seeding runs at, TEST/T_SCR.SH's.
NOW=/NOW:$SEED_NOW
# The file the machine's printer writes in the run's directory.
PRN_OUT=PRINTER.OUT

emu=qemu
mode=r
want=
secs=120
sock=
mon=
intl=
files=
dir=
reqs=
inv=
keys=
golden=
cmps=
printed=
while getopts e:m:M:t:s:p:I:f:d:q:i:k:g:c:o: opt; do
    case $opt in
    e) emu=$OPTARG ;;
    m) mode=$OPTARG ;;
    M) want=$OPTARG ;;
    t) secs=$OPTARG ;;
    s) sock=$OPTARG ;;
    p) mon=$OPTARG ;;
    I) intl=$OPTARG ;;
    f) [ -f "$OPTARG" ] || die "no such file: $OPTARG"
       files="$files$OPTARG
" ;;
    d) dir=$OPTARG ;;
    q) [ -f "$OPTARG" ] || die "no such file: $OPTARG"
       reqs="$reqs $OPTARG" ;;
    i) inv=$OPTARG ;;
    k) keys=$OPTARG ;;
    g) golden=$OPTARG ;;
    c) [ -f "$OPTARG" ] || die "no such file: $OPTARG"
       cmps="$cmps $OPTARG" ;;
    o) printed=$OPTARG ;;
    *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 2 ] || usage
rundir=$1
exe=$2
shift 2
case $want in
REAL|STANDARD|ENHANCED) ;;
'') [ -n "$keys" ] || die "-M is needed without -k" ;;
*) die "unknown mode word: $want" ;;
esac
[ -f "$exe" ] || die "no such program: $exe"
[ -z "$reqs" ] || [ -n "$dir" ] || die "-q needs -d"
[ -z "$reqs" ] || [ -x "$inv" ] || die "-q needs the host INV of -i"
[ -z "$keys" ] || [ -n "$golden" ] || die "-k needs -g"
[ -z "$golden" ] || [ -n "$keys" ] || die "-g needs -k"
[ -z "$cmps" ] || [ -n "$keys" ] || die "-c needs -k"
for f in "$keys" "$golden" "$printed"; do
    [ -z "$f" ] || [ -f "$f" ] || die "no such file: $f"
done
name=$(basename "$exe" .EXE)

rm -rf "$rundir"
mkdir -p "$rundir"
cp "$exe" "$rundir/"
printf '%s' "$files" | while IFS= read -r f; do cp "$f" "$rundir/"; done
if [ -n "$dir" ]; then
    printf '%s\n' "$dir" | tr ',' '\n' | while IFS= read -r d; do
        mkdir -p "$rundir/$d"
    done
    dir=${dir%%,*}
    if [ -n "$reqs" ]; then
        inv=$(realpath "$inv")
        log=$rundir/seed.log
        for r in $reqs; do
            dos_text "$r" | (cd "$rundir" && "$inv" /CMD /LOCAL "$dir" $NOW) \
                >>"$log" 2>&1 || fail "the seeding failed: $(cat "$log")"
        done
        ! grep -q '^-ERR' "$log" ||
            fail "a seed request was refused: $(grep -m 1 '^-ERR' "$log")"
        rm -f "$log"
    fi
fi
out=$name.OUT
if [ -n "$keys" ]; then
    cp "$keys" "$rundir/"
    k=$(basename "$keys")
    out=${k%.*}.SNP
    set -- "$@" "/KEYS:$k" "/SNAP:$out"
fi

# runwin.sh runs in the background, so that a signal reaches halt at
# once.
rc=0
sh "$here/runwin.sh" -e "$emu" -m "$mode" -t "$secs" ${sock:+-s "$sock"} \
    ${mon:+-p "$mon"} ${intl:+-I "$intl"} "$rundir" "$name $*" &
rwpid=$!
wait "$rwpid" || rc=$?
rwpid=
res=$rundir/$out
if [ -f "$res" ]; then
    echo "=== $out"
    tr -d '\r' <"$res"
fi
[ $rc = 0 ] || exit $rc
[ -f "$res" ] || fail "the program wrote no $out"

# bad WHY - print WHY on standard error and mark the run failed.
ok=1
bad() { echo "$prog: $*" >&2; ok=0; }

if [ -z "$keys" ]; then
    rep=$(tr -d '\r' <"$res")
    modes=$(printf '%s\n' "$rep" | awk '$2 == "MODE"')
    [ -n "$modes" ] || bad "$out has no MODE line"
    [ -z "$(printf '%s\n' "$modes" | awk -v m="$want" '$3 != m')" ] ||
        bad "$out names another mode than $want"
    [ -z "$(printf '%s\n' "$rep" | awk '$2 == "EXIT" && $3 != "0"')" ] ||
        bad "$out has an EXIT line other than 0"
    [ -z "$(printf '%s\n' "$rep" |
            awk -v m="$STACK_MARGIN" '$2 == "STACK" && $3 < m')" ] ||
        bad "$out leaves fewer than $STACK_MARGIN stack bytes unused"
elif ! snap_check "$golden" "$res" "$STACK_MARGIN"; then
    ok=0
fi
for g in $cmps; do
    f=$rundir/$(basename "$g")
    if [ ! -f "$f" ]; then
        bad "the program wrote no $(basename "$g")"
    elif ! cmp -s "$f" "$g"; then
        bad "$f differs from $g"
    fi
done
if [ -z "$printed" ]; then
    :
elif [ ! -f "$rundir/$PRN_OUT" ]; then
    bad "the run printed nothing ($PRN_OUT)"
elif ! cmp -s "$rundir/$PRN_OUT" "$printed"; then
    bad "what it printed differs from $printed (< golden, > printed):"
    tr -d '\r' <"$printed" >"$rundir/printed.txt"
    tr -d '\r' <"$rundir/$PRN_OUT" | diff "$rundir/printed.txt" - >&2 || :
fi
[ $ok = 1 ] || exit 1
rm -rf "$rundir"
echo "$prog: PASS"
