#!/bin/sh
# stvgate.sh - the RETROINV server's serial session kept served while
# NetBIOS sessions load it, between two MS-DOS 3.30 QEMU VMs.
#
# usage: sh TEST/INTEG/stvgate.sh -r ROUNDS [EXEDIR]
#
#   EXEDIR  directory of one build's INVSERV.EXE and INVTEST.EXE (default
#           BIN, the Open Watcom build, made first); the message file
#           INVSERV.MSG is the host-built BIN/INVSERV.MSG
#   -r N    LOAD_ROUNDS, 1 or more: STVGATE_ROUNDS_<build> of MAKEFILE
#
# The host INV (OBJ/HOST/INV.bin) seeds a data directory with SEED_ITEMS
# items and ADMIN's password changed to PASSWORD.  One run of run2vm.sh
# with the VMs' monitors open, VM A's COM2 on a host socket (SERIAL=1,
# COM2_A) and the network as UDP datagrams (NETMODE=dgram): VM A runs,
# from D:\, "INVSERV /CFG:STV.CFG" on that data, serving NetBIOS
# sessions and COM1 and COM2, its event log D:\SRV.LOG.
# Once the server's status screen is up, SERPING.PY logs in on COM2 and
# PINGs every half second.  VM B meanwhile runs INVTEST RISRV LOAD.SCR, a
# script this gate writes: as many NetBIOS sessions as STV.CFG's
# SESSIONS= (LOAD_SESSIONS) log in, then, LOAD_ROUNDS times, each sends a
# paged list of over 2 KB and only then are the answers read, so that
# requests of every session are in flight at once.  When VM B shows that
# INVTEST has ended, SERPING.PY ends the server with ADMIN SHUTDOWN.
# Over datagrams only the guests' speed sets the pace of the load, which
# a loaded host can only slow: LOAD_ROUNDS is sized, per build, so that
# the build's fastest run measured still lasts half as long again as
# LOAD_SECS (about 75 requests a second for the Watcom builds, 9 for
# the Turbo C build, whose server lists far more slowly).  The stream
# network stalls some requests for a while, but not every run, so a
# load sized by its stalls could end early.
#
# The host then requires exit status 0 of INVSERV, INVTEST and SERPING.PY,
# which needs every PING answered +OK within PING_LIMIT seconds times
# DOSTIME_SCALE; the event log well formed, with one START, STACK and
# STOP 0 event, the STACK at least SRV_STACK_MARGIN (log_check of
# vmgate.sh); and, in its times, a load window, from the login of the
# last of the NetBIOS sessions to their last request, of at least
# LOAD_SECS seconds with no gap between two of their requests longer than
# LOAD_GAP seconds times DOSTIME_SCALE (the guests run slower on a loaded
# host), in which the serial session made at least PING_COUNT requests.
# So the serial line was served promptly all the while the NetBIOS
# sessions were, and they were not held up by it.  The free conventional
# memory with the network loaded is printed for both VMs.
#
# On failure every program's output is printed, and the result directory
# is kept as OUT.fail.<timestamp>.
#
# env: TIMEOUT  per-VM limit in seconds (default 600), times DOSTIME_SCALE
#      OUT      result directory (default TEST/INTEG/vm/stvgate.run); it
#               must be new or an earlier OUT
#      PORT_A, PCAP, NET330_DIR  as run2vm.sh takes them
#
# Exit status: 0 when every check passed; 1 on failure; 2 on a setup
# error, run2vm.sh's included; 129, 130 or 143 when stopped by SIGHUP,
# SIGINT or SIGTERM (run2vm.sh kills its VMs first).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/net330.sh"
. "$here/free310.sh"
. "$here/vmgate.sh"

CLOCK=$SEED_CLOCK
PASSWORD=Stv2gate
SEED_ITEMS=50
LOAD_SECS=60
LOAD_GAP=5
PING_LIMIT=1
PING_COUNT=60

usage="usage: stvgate.sh -r ROUNDS [EXEDIR]"
[ "${1:-}" = -r ] && [ $# -ge 2 ] && is_uint "$2" && [ "$2" -ge 1 ] &&
    [ $# -le 3 ] || die "$usage"
LOAD_ROUNDS=$2
shift 2
exedir=${1:-BIN}
TIMEOUT=${TIMEOUT:-600}
OUT=${OUT:-"$here/vm/stvgate.run"}

OUT=$(out_check "$OUT" .run2vm) || exit 2
# The sessions of LOAD.SCR, as many as the server serves.
LOAD_SESSIONS=$(tr -d '\r' <"$here/STV.CFG" | sed -n 's/^SESSIONS=//p')
is_uint "$LOAD_SESSIONS" || die "STV.CFG has no SESSIONS="
gap=$(dos_secs "$LOAD_GAP")
export TIMEOUT OUT

need_tools python3 mktemp awk
progs="BIN/QEXIT.COM BIN/INVSERV.MSG OBJ/HOST/INV.bin"
[ "$exedir" != BIN ] || progs="$progs BIN/INVSERV.EXE BIN/INVTEST.EXE"
make -s -C "$repo" $progs || die "the programs could not be built"
case $exedir in /*) ;; *) exedir="$repo/$exedir" ;; esac
for f in INVSERV.EXE INVTEST.EXE; do
    [ -f "$exedir/$f" ] || die "not found: $exedir/$f"
done

gate_start stvgate
ppid=

# stv_halt SIGNAL - the signal handler (on_signals): stop SERPING.PY,
# then what halt stops.
stv_halt() {
    [ -z "$ppid" ] || kill "$ppid" 2>/dev/null || :
    halt "$1"
}
on_signals stv_halt

a="$payload/A"
b="$payload/B"
mkdir -p "$a/DATA" "$b"
cp "$exedir/INVSERV.EXE" "$repo/BIN/INVSERV.MSG" "$here/STV.CFG" "$a/"
cp "$exedir/INVTEST.EXE" "$b/"

# The seed: requests that must each be answered +OK.
{
    printf 'HELLO SEED\r\nLOGIN ADMIN admin\r\nPASSWD admin %s\r\n' \
        "$PASSWORD"
    printf 'ITEM ADD CATEGORY=SYS MFR="Load Test Corporation" MODEL=LT-1 '
    printf 'DESCR="One of the items whose lists load the server" '
    printf 'TAGS="load,list,page,netbios,serial,starvation"\r\n'
    i=1
    while [ "$i" -lt "$SEED_ITEMS" ]; do
        printf 'ITEM COPY 1\r\n'
        i=$((i + 1))
    done
} >"$payload/SEED.REQ"
(cd "$a" && "$repo/OBJ/HOST/INV.bin" /CMD /LOCAL DATA /NOW:$SEED_NOW \
    <"$payload/SEED.REQ" >"$payload/SEED.OUT") ||
    die "the host INV could not seed the data"
bad=$(tr -d '\r' <"$payload/SEED.OUT" | grep -v '^+OK' | head -n 1) || :
[ -z "$bad" ] || die "the seed was answered: $bad"

# LOAD.SCR: the sessions log in, then the rounds, then BYE.
{
    s=1
    while [ "$s" -le "$LOAD_SESSIONS" ]; do
        printf '@CALL %s RILOAD%s\n> HELLO RILOAD%s\n< +OK\n' "$s" "$s" "$s"
        printf '> LOGIN ADMIN %s\n' "$PASSWORD"
        printf '< +OK LEVEL=3 NAME="System Administrator"\n'
        s=$((s + 1))
    done
    r=0
    while [ "$r" -lt "$LOAD_ROUNDS" ]; do
        s=1
        while [ "$s" -le "$LOAD_SESSIONS" ]; do
            printf '@S %s\n>& ITEM LIST FIELDS=ITEM_ID,MFR,DESCR,TAGS ' "$s"
            printf 'PAGE=25\n'
            s=$((s + 1))
        done
        s=1
        while [ "$s" -le "$LOAD_SESSIONS" ]; do
            printf '@WAIT %s\n< +DATA\n' "$s"
            s=$((s + 1))
        done
        r=$((r + 1))
    done
    s=1
    while [ "$s" -le "$LOAD_SESSIONS" ]; do
        printf '@S %s\n> BYE\n< +OK\n' "$s"
        s=$((s + 1))
    done
} >"$b/LOAD.SCR"

# The line VM B shows once INVTEST has ended.
donemark='LOAD DONE'
{
    printf '@ECHO OFF\r\nINVSERV /CFG:STV.CFG\r\n'
    rc_to SRV.RC
} >"$a/STVA.BAT"
{
    printf '@ECHO OFF\r\nINVTEST RISRV LOAD.SCR > LOAD.OUT\r\n'
    rc_to LOAD.RC
    printf 'ECHO %s > CON\r\n' "$donemark"
} >"$b/STVB.BAT"

stop="$payload/STOP"
pings="$payload/PING.OUT"
rc=0
prc=0
MONDIR=$mondir SERIAL=1 COM2_A="$mondir/com2" NETMODE=dgram \
    QEMU_EXTRA="-rtc base=$CLOCK" \
    sh "$here/run2vm.sh" "$a" 'C:\COMMAND /C STVA.BAT' \
                         "$b" 'C:\COMMAND /C STVB.BAT' &
runpid=$!
if qmon A "wait:RETROINV server RISRV:$(dos_secs 240)"; then
    python3 -I "$here/SERPING.PY" "$mondir/com2" "$stop" \
        "$(dos_secs "$PING_LIMIT")" ADMIN "$PASSWORD" >"$pings" &
    ppid=$!
    qmon B "wait:$donemark:$(dos_secs "$TIMEOUT")" || :
    : >"$stop"
    wait "$ppid" || prc=$?
    ppid=
else
    prc=1
fi
[ "$prc" -eq 0 ] || quit_vms
wait "$runpid" || rc=$?
runpid=
case $rc in 0|1) ;; *) exit "$rc" ;; esac
[ "$rc" -eq 0 ] || status=1
if [ "$prc" -ne 0 ]; then
    echo "$prog: SERPING.PY exit $prc" >&2
    status=1
fi

da="$OUT/A/D"
db="$OUT/B/D"
expect "$da/SRV.RC" 0
expect "$db/LOAD.RC" 0
log_check "$da/SRV.LOG" 1

# The load window: the NetBIOS sessions are the slots whose OPEN names a
# RILOAD station, the serial session the slot that sent PING.  LOAD.SCR
# starts its rounds only when every session has logged in, so the window
# opens at the last of their LOGIN events: a slow NetBIOS call of the
# last session is not load, a slow first request is.
if [ -f "$da/SRV.LOG" ]; then
    window=$(tr -d '\r' <"$da/SRV.LOG" | awk -v sessions="$LOAD_SESSIONS" '
        { split($2, t, ":"); s = t[1] * 3600 + t[2] * 60 + t[3] }
        $4 == "OPEN" && $5 ~ /^RILOAD/ { load[$3] = 1 }
        $4 == "LOGIN" && ($3 in load) && ++on == sessions { first = last = s }
        $4 == "REQ" && $5 == "PING" { ping[s]++ }
        $4 == "REQ" && ($3 in load) && on >= sessions {
            if (s - last > gap) gap = s - last
            last = s
        }
        END {
            for (p in ping) if (p >= first && p <= last) k += ping[p]
            printf "%d %d %d\n", last - first, gap, k
        }')
    set -- $window
    echo "$prog: load window ${1}s, longest gap ${2}s, $3 serial requests"
    if [ "$1" -lt "$LOAD_SECS" ] || [ "$2" -gt "$gap" ] ||
       [ "$3" -lt "$PING_COUNT" ]; then
        echo "$prog: the load window must last ${LOAD_SECS}s or more," \
             "with no gap over ${gap}s and $PING_COUNT or more" \
             "serial requests" >&2
        status=1
    fi
fi
tail -n 1 "$pings" 2>/dev/null || :

free_print A B

gate_end "$da/SRV.LOG" "$db/LOAD.OUT" "$pings"
