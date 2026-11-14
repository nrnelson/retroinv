#!/bin/sh
# winnb.sh - the NET link of the Windows programs between two machines:
# INVSERV on an MS-DOS 3.30 VM over MS Client 3.0 NetBEUI, and WNET
# (TEST/WNET.C) and INVWIN on the win30nb test machine
# (TEST/WIN/winlib.sh) with the same network, in standard and 386
# enhanced mode, where the Windows programs run.
#
# usage: sh TEST/WIN/winnb.sh [-m MODES] WNET INVWIN INV.MSG HOSTINV
#
#   WNET     the Windows program BIN/WIN/WNET.EXE
#   INVWIN, INV.MSG  the client for Windows and its message file
#   HOSTINV  the host build of INV, which seeds the server's data
#   -m MODES the modes of Windows to run, runwin.sh's letters (default
#            "s 3": standard and 386 enhanced mode)
#
# The NetBIOS image (TEST/INTEG/mkimg.sh) and win30nb's disk
# (TEST/WIN/mkwin.sh -w win30nb) must be current.
#
# For each mode, one run: VM A of TEST/INTEG/run2vm.sh, alone, runs
# "INVSERV /CFG:WINNB.CFG" (the name RISRV, one session, the event log
# WINNB.LOG) on a data directory that HOSTINV has seeded as the screen
# tests seed theirs (TEST/KEYS/SEED.REQ, wintest.sh -q).  Once run2vm.sh
# has written its ports, the relay TEST/INTEG/LOSSY.PY --delay RELAY_MS
# listens on PORT_B and passes the frames on to VM A's PORT_A, as
# run2vm.sh's RELAY=delay does between its VMs: NetBEUI under QEMU now
# and then ignores the answer to its own poll that comes back within a
# millisecond, and then leaves a CALL half open (LOSSY.PY; DOC/BUILD.md,
# the stall).  Then TEST/WIN/runwin.sh starts win30nb, its card on the
# relay (-n), with NET.ON beside the programs, so that the network
# starts before Windows, which then starts four times, once for each run
# in turn: WNET calls RISRV until the server answers and plays a HELLO;
# INVWIN, beside INV.MSG, runs "/NET NOSUCH", a name no station has,
# with the key script TEST/WIN/INVCALL.KEY, and "/NET RISRV" with
# TEST/WIN/INVLINK.KEY; and WNET calls again and plays
# TEST/WIN/WNET.SCR, which ends with ADMIN SHUTDOWN.  A run passes when
# runwin.sh ends with 0, INVWIN's snapshots equal
# TEST/GOLDEN/WIN/INVCALL.SNP and INVNB.SNP, each but for its stack line,
# held to STACK_MARGIN (snap_check of TEST/shlib.sh), WNET's report
# WNET.OUT, the last run's, holds "WNET PASS", names the mode and says
# "WNET EXIT 0", INVSERV ended with errorlevel 0, and the server's log
# shows at least three sessions (WNET's two and INVWIN's), each opened
# from the machine name WIN_NBNAME of winlib.sh, which INVWIN and WNET
# call from as INV does under DOS (dos_machine_name).  When the Windows
# machine fails, VM A is stopped at once rather than at its limit.  Each
# mode's verdict and its seconds are printed; a failed run's directory
# is kept, with the relay's log, and the snapshots' differences from
# their goldens shown.
#
# env: TIMEOUT     VM A's limit in seconds (default 600)
#      RELAY_MS    the relay's delay in whole milliseconds (default 10);
#                  0 joins the Windows machine to VM A directly
#      OUT         result directory (default OBJ/WINNB), emptied first: it
#                  must be new or an earlier OUT
#      NET330_DIR  the NetBIOS VM image (see TEST/INTEG/net330.sh);
#      WINNBIMG_DIR  win30nb's image (see TEST/WIN/winlib.sh)
# Every limit in seconds, WAIT_SECS and WIN_SECS below included, is
# multiplied by DOSTIME_SCALE (dos_secs of TEST/shlib.sh).
#
# Exit status: 0 when every mode passed; 1 when one failed; 2 on a usage
# or setup error, run2vm.sh's included; 129, 130 or 143 on SIGHUP, SIGINT
# or SIGTERM (run2vm.sh kills VM A first).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$repo/TEST/shlib.sh"
STACK_MARGIN=$(cdefine "$repo/TEST/T_SCRH.C" STACK_MARGIN)

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

# win30nb's machine name, read in a subshell that keeps winlib.sh's
# settings for mtools, TZ among them, from the VMs' clocks.
STATION=$(. "$here/winlib.sh" && echo "$WIN_NBNAME")
# The sessions the server opens in a run: WNET's two and INVWIN's.
OPENS=3
# The date and time the seeding runs at, TEST/T_SCR.SH's.
NOW=/NOW:$SEED_NOW
# The seconds WNET calls the server for while its VM starts, and the
# Windows machine's limit.
WAIT_SECS=240
WIN_SECS=400
# INVWIN's key scripts, each with the golden of its snapshot.
KEYS=$repo/TEST/WIN/INVLINK.KEY
SNAP=$repo/TEST/GOLDEN/WIN/INVNB.SNP
CALLKEYS=$repo/TEST/WIN/INVCALL.KEY
CALLSNAP=$repo/TEST/GOLDEN/WIN/INVCALL.SNP
# modeword LETTER - the word of WNET's MODE line for runwin.sh's mode
# LETTER; nothing for another letter.
modeword() {
    case $1 in s) echo STANDARD ;; 3) echo ENHANCED ;; esac
}
# opened LOG - whether the server's event log LOG shows OPENS sessions or
# more, each opened from STATION (a CALL left half open may add one).
opened() {
    [ -f "$1" ] && tr -d '\r' <"$1" | awk -v st="$STATION" -v n=$OPENS '
        $4 == "OPEN" { k++; bad += $5 != st }
        END { exit bad || k < n }'
}

modes='s 3'
while getopts m: opt; do
    case $opt in
    m) modes=$OPTARG ;;
    *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 4 ] || usage
for f in "$1" "$2" "$3"; do
    [ -f "$f" ] || die "no such file: $f"
done
[ -x "$4" ] || die "no such program: $4"
wnet=$(realpath "$1")
invwin=$(realpath "$2")
invmsg=$(realpath "$3")
inv=$(realpath "$4")
for m in $modes; do
    [ -n "$(modeword "$m")" ] || die "unknown mode: $m"
done
TIMEOUT=${TIMEOUT:-600}
export TIMEOUT
RELAY_MS=${RELAY_MS:-10}
is_uint "$RELAY_MS" || die "RELAY_MS must be a whole number"
OUT=${OUT:-$repo/OBJ/WINNB}
marker=.winnb
case $(realpath -m "$OUT") in
/|"$HOME"|"$repo") die "refusing OUT=$OUT" ;;
esac
[ ! -e "$OUT" ] || [ -f "$OUT/$marker" ] ||
    die "OUT=$OUT exists and is not an earlier OUT"
need_tools python3 timeout
rm -rf "$OUT"
mkdir -p "$OUT/SRV/DATA"
: >"$OUT/$marker"
wait_secs=$(dos_secs $WAIT_SECS)
relay_secs=$(dos_secs $((TIMEOUT + WIN_SECS)))

# The server's payload: INVSERV, its messages and settings, the data.
cp "$repo/BIN/INVSERV.EXE" "$repo/BIN/INVSERV.MSG" "$OUT/SRV/"
printf 'NAME=RISRV\r\nDATA=DATA\r\nSESSIONS=1\r\nLOG=WINNB.LOG\r\n' \
    >"$OUT/SRV/WINNB.CFG"
dos_text "$repo/TEST/KEYS/SEED.REQ" |
    (cd "$OUT/SRV" && "$inv" /CMD /LOCAL DATA $NOW) >"$OUT/seed.log" 2>&1 ||
    fail "the seeding failed: $(cat "$OUT/seed.log")"
! grep -q '^-ERR' "$OUT/seed.log" ||
    fail "a seed request was refused: $(grep -m 1 '^-ERR' "$OUT/seed.log")"

srvpid=
relpid=
# halt [SIGNAL] - stop the relay and run2vm.sh, which kills VM A, and
# wait for them; also the signal handler (on_signals).
halt() {
    for h_p in $relpid $srvpid; do
        kill -s TERM "$h_p" 2>/dev/null || :
        wait "$h_p" 2>/dev/null || :
    done
    relpid=
    srvpid=
}
on_signals halt

# The run's port slot (port_slot of TEST/shlib.sh), its ports given to
# run2vm.sh: held by this script, and so by the relay and the Windows
# machine it starts, until all of them have ended, so that no other run
# takes those ports while the Windows machine, which reconnects to its
# relay, still lives; a slot of run2vm.sh's own ends with VM A.
port_slot
status=0
for m in $modes; do
    word=$(modeword "$m")
    dir=$OUT/$m
    start=$(date +%s)
    PORT_A=$((PORT_BASE + 2 * sl_slot)) OUT=$dir/A \
        sh "$repo/TEST/INTEG/run2vm.sh" "$OUT/SRV" \
        'INVSERV /CFG:WINNB.CFG' >"$dir.srv" 2>&1 &
    srvpid=$!
    # run2vm.sh writes its ports before it starts the VM; a run that
    # stopped before that leaves none.
    n=0
    while [ ! -f "$dir/A/PORTS" ] && kill -0 "$srvpid" 2>/dev/null &&
        [ $n -lt 600 ]; do
        sleep 0.1
        n=$((n + 1))
    done
    [ -f "$dir/A/PORTS" ] ||
        { halt; die "run2vm.sh wrote no ports: $(cat "$dir.srv")"; }
    port=$(sed -n 's/^PORT_A=//p' "$dir/A/PORTS")
    if [ "$RELAY_MS" -gt 0 ]; then
        relay=$(sed -n 's/^PORT_B=//p' "$dir/A/PORTS")
        timeout "$relay_secs" python3 -I "$repo/TEST/INTEG/LOSSY.PY" \
            --delay "$RELAY_MS" "$relay" "$port" "$dir.relay" \
            2>"$dir.relerr" &
        relpid=$!
        port=$relay
    fi
    mkdir -p "$dir/W"
    cp "$wnet" "$repo/TEST/WIN/WNET.SCR" "$invwin" "$invmsg" "$KEYS" \
        "$CALLKEYS" "$dir/W/"
    printf '> HELLO WINNB\r\n< +OK\r\n' >"$dir/W/HELLO.SCR"
    : >"$dir/W/NET.ON"
    k=$(basename "$KEYS")
    c=$(basename "$CALLKEYS")
    net=stream,addr.type=inet,addr.host=127.0.0.1,addr.port=$port
    wrc=0
    sh "$here/runwin.sh" -w win30nb -m "$m" -t $WIN_SECS \
        -n "$net,server=off,reconnect-ms=200" "$dir/W" \
        "WNET RISRV HELLO.SCR $wait_secs" \
        "INVWIN /NET NOSUCH /KEYS:$c /SNAP:${c%.*}.SNP" \
        "INVWIN /NET RISRV /KEYS:$k /SNAP:${k%.*}.SNP" \
        "WNET RISRV WNET.SCR $wait_secs" >"$dir.win" 2>&1 || wrc=$?
    rep=$(tr -d '\r' <"$dir/W/WNET.OUT" 2>/dev/null || :)
    ok=0
    [ $wrc = 0 ] &&
        snap_check "$CALLSNAP" "$dir/W/${c%.*}.SNP" "$STACK_MARGIN" &&
        snap_check "$SNAP" "$dir/W/${k%.*}.SNP" "$STACK_MARGIN" &&
        printf '%s\n' "$rep" | grep -q '^WNET PASS' &&
        printf '%s\n' "$rep" | grep -qx "WNET MODE $word" &&
        printf '%s\n' "$rep" | grep -qx 'WNET EXIT 0' && ok=1
    if [ $ok = 1 ]; then
        wait "$srvpid" || ok=0
        srvpid=
    fi
    halt
    [ $ok = 0 ] || opened "$dir/A/A/D/WINNB.LOG" || ok=0
    secs=$(($(date +%s) - start))
    if [ $ok = 1 ]; then
        echo "$prog: $word PASS in $secs s"
        rm -rf "$dir" "$dir.srv" "$dir.win" "$dir.relay" "$dir.relerr"
        continue
    fi
    status=1
    echo "$prog: $word FAIL in $secs s (Windows $wrc)" >&2
    for f in "$dir.win" "$dir/W/WNET.OUT" "$dir.srv" "$dir/A/A/D/WINNB.LOG" \
        "$dir/W/NETSTART.OUT" "$dir.relerr"; do
        [ ! -s "$f" ] || { echo "===== $f"; tr -d '\r' <"$f"; }
    done
    echo "===== $CALLSNAP (<) and the run's (>)"
    diff "$CALLSNAP" "$dir/W/${c%.*}.SNP" || :
    echo "===== $SNAP (<) and the run's (>)"
    diff "$SNAP" "$dir/W/${k%.*}.SNP" || :
done
[ $status = 0 ] || fail "FAIL; runs kept in $OUT"
rm -rf "$OUT"
echo "$prog: PASS"
