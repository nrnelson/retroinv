#!/bin/sh
# winser.sh - the SERIAL link of the Windows programs between two QEMU
# machines: WSERT and INVWIN on COM1 of the Windows 3.0 test machine,
# each the client of INVSERV on COM2 of MS-DOS 3.10.
#
# usage: sh TEST/WIN/winser.sh [-u] [-m MODES] [-t SECS] WSERT.EXE
#            INVWIN.EXE INV.MSG INVSERV.EXE INVSERV.MSG CONFIG.SYS HOSTINV
#
#   WSERT.EXE    the test program of TEST/WSERT.C
#   INVWIN.EXE, INV.MSG  the client for Windows and its message file
#   INVSERV.EXE, INVSERV.MSG  the server and its message file
#   CONFIG.SYS   the MS-DOS 3.10 boot's, with the FILES= the engine needs
#                (DOS310_CFG of MAKEFILE)
#   HOSTINV      the host INV, which seeds the server's data
#   -m MODES     the Windows modes, runwin.sh's r, s and 3 (default
#                "s 3")
#   -t SECS      the limit of each machine and of each step of the relay,
#                before DOSTIME_SCALE (default 300)
#   -u           rewrite TEST/GOLDEN/WIN/WSERT.TXT from the first mode's
#                run instead of comparing it; review the difference before
#                keeping it
#
# Each program has a run in each mode, all side by side, each in a
# directory of its own in WORK, PROGRAM.MODE.  The host INV seeds a data
# directory from TEST/KEYS/SEED.REQ, as the -q of TEST/WIN/wintest.sh
# does; TEST/WIN/WSRELAY.PY makes a socket for each machine's line and
# listens; TEST/INTEG/boot310.sh boots MS-DOS 3.10 with COM2 on the first
# socket, the server, WINSER.CFG and the data on its hard disk, and runs
# "INVSERV /CFG:WINSER.CFG"; and wintest.sh runs, in the mode with COM1
# on the second socket and QEMU's monitor on a third, "WSERT 1", or
# "INVWIN /COM1" with INV.MSG and the key script TEST/WIN/INVLINK.KEY,
# its settings TEST/WIN/PANE1.CFG, whose WINPANE.LOC=1 shows a location's
# contents beside the tree on this slow link too, or, as the run
# INVUSER, TEST/WIN/INVUSER.KEY, which sets two users' passwords, or, as
# the run INVBACK, TEST/WIN/INVBACK.KEY, on a data directory that
# TEST/KEYS/RLLONG.REQ has given sixty items more, a list longer than a
# window.  WSRELAY.PY joins the two lines once the server answers,
# searches the Windows machine's memory for the ADMIN password at each
# PING WSERT sends, at each ITEM GET INVWIN sends after its LOGIN, its
# item view's, that of a scoped screen's title and its preview's two, the
# second stopped after it, and, as
# for the passwords INVUSER sets (USERPWS), at each USER LIST it sends,
# answers the line FLOOD of WSERT, which it keeps from the server, with
# more bytes than the Windows driver's receive queue holds, and ends the
# server once Windows has ended.
#
# A run passes when wintest.sh passes, which for INVWIN compares its
# snapshot with TEST/GOLDEN/WIN/INVSER.SNP, for INVUSER with INVUSR.SNP,
# for INVBACK with INVSERB.SNP,
# WSRELAY.PY exits 0 and
# boot310.sh exits 0, INVSERV's status; for WSERT the session of
# WSERT.OUT (its lines starting "> " or "< ") must also equal
# TEST/GOLDEN/WIN/WSERT.TXT.  A passed run's directory is removed and its
# seconds printed; a failed one's is kept, with the output of each
# program: SEED.LOG, RELAY.LOG, SRV.LOG (the boot's) and WIN.LOG
# (wintest.sh's), WIN.BIN, the bytes Windows sent, and WIN.MEM, the
# memory last searched; and beside a WSERT run's, WSERT.MODE.TXT, its
# session.  WORK goes once every run passed.
#
# env: WORK  the runs' directory, emptied first (default OBJ/WINSER of
#            the repository)
#
# Exit status: 0 when every run passed; 1 when one failed; 2 on a usage or
# setup error; 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM, once the
# runs' programs and their emulators have ended.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$repo/TEST/shlib.sh"

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

# The date and time the seeding runs at, wintest.sh's.
NOW=/NOW:$SEED_NOW
SEED=$repo/TEST/KEYS/SEED.REQ
GOLDEN=$repo/TEST/GOLDEN/WIN/WSERT.TXT
KEYS=$repo/TEST/WIN/INVLINK.KEY
SNAP=$repo/TEST/GOLDEN/WIN/INVSER.SNP
UKEYS=$repo/TEST/WIN/INVUSER.KEY
USNAP=$repo/TEST/GOLDEN/WIN/INVUSR.SNP
# INVBACK's script and golden, the request file that makes its item list
# longer than a window, and the ITEM GET requests it sends: one for the
# title of each of its two scoped screens.
BKEYS=$repo/TEST/WIN/INVBACK.KEY
BSNAP=$repo/TEST/GOLDEN/WIN/INVSERB.SNP
BSEED=$repo/TEST/KEYS/RLLONG.REQ
BGETS=2
# The passwords INVUSER.KEY sets, a new user's and a reset one, key by
# key, and the USER LIST requests it sends: the list opened, and fetched
# again after each save.
USERPWS='Qz7Wx2Kp Vb9Rt4Ny'
USERLISTS=3
# The seconds WSRELAY.PY gets to make its sockets, and the seconds the
# programs of the runs get to end after a signal.
SOCKWAIT=10
HALTWAIT=30

update=0
modes='s 3'
secs=300
while getopts um:t: opt; do
    case $opt in
    u) update=1 ;;
    m) modes=$OPTARG ;;
    t) secs=$OPTARG ;;
    *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 7 ] || usage
for f in "$1" "$2" "$3" "$4" "$5" "$6"; do
    [ -f "$f" ] || die "no such file: $f"
done
[ -x "$7" ] || die "no such program: $7"
is_uint "$secs" && [ "$secs" -gt 0 ] || die "SECS must be 1 or more"
[ -n "$modes" ] || die "no mode given"
for m in $modes; do
    case $m in r|s|3) ;; *) die "unknown mode: $m" ;; esac
done
need_tools python3 mktemp realpath
wsert=$(realpath "$1")
invwin=$(realpath "$2")
invmsg=$(realpath "$3")
srv=$(realpath "$4")
msg=$(realpath "$5")
cfg=$(realpath "$6")
inv=$(realpath "$7")
pw=$(sed -n 's/^LOGIN ADMIN //p' "$SEED" | tail -n 1)
[ -n "$pw" ] || die "no LOGIN ADMIN in $SEED"
WORK=${WORK:-$repo/OBJ/WINSER}
rm -rf "$WORK"
mkdir -p "$WORK"
WORK=$(realpath "$WORK")

# halt SIGNAL - the signal handler (on_signals): end the runs, so that
# none starts another program, then every program they started, each of
# which ends its own emulator, and wait at most HALTWAIT seconds for all
# of them to be gone.
halt() {
    for p in $(cat "$WORK"/*.PID 2>/dev/null); do
        kill -TERM "$p" 2>/dev/null || :
    done
    pids=$(cat "$WORK"/*/PIDS 2>/dev/null) || :
    for p in $pids; do
        kill -TERM "$p" 2>/dev/null || :
    done
    n=0
    while [ $n -lt $((HALTWAIT * 10)) ] && alive $pids; do
        sleep 0.1
        n=$((n + 1))
    done
}

# alive PID... - succeed while one of the processes PID lives.
alive() {
    for p; do
        ! kill -0 "$p" 2>/dev/null || return 0
    done
    return 1
}
on_signals halt

# run PROGRAM MODE - one run, in WORK/PROGRAM.MODE; prints its verdict,
# returns 0 when it passed.
run() {
    exe=$1
    mode=$2
    d=$WORK/$exe.$mode
    mkdir -p "$d/SEED/DATA"
    start=$(date +%s)
    seeds=$SEED
    [ $exe != INVBACK ] || seeds="$seeds $BSEED"
    for s in $seeds; do
        if ! dos_text "$s" | (cd "$d/SEED" && "$inv" /CMD /LOCAL DATA $NOW) \
                >>"$d/SEED.LOG" 2>&1 || grep -q '^-ERR' "$d/SEED.LOG"; then
            echo "$prog: $exe.$mode: the seeding failed" >&2
            return 1
        fi
    done
    socks=$(mktemp -d "${TMPDIR:-/tmp}/winser.XXXXXX")
    case $exe in
    WSERT) set -- PING 2 ;;
    INVWIN) set -- 'ITEM GET' 4 ;;
    INVBACK) set -- 'ITEM GET' $BGETS ;;
    *) set -- 'USER LIST' $USERLISTS $USERPWS ;;
    esac
    python3 -I "$here/WSRELAY.PY" "$socks/SRV" "$socks/WIN" "$socks/MON" \
        "$(dos_secs "$secs")" "$pw" "$d/WIN.BIN" "$@" >"$d/RELAY.LOG" 2>&1 &
    echo $! >"$d/PIDS"
    n=0
    while { [ ! -S "$socks/SRV" ] || [ ! -S "$socks/WIN" ]; } &&
          [ $n -lt $((SOCKWAIT * 10)) ]; do
        sleep 0.1
        n=$((n + 1))
    done
    if [ ! -S "$socks/SRV" ] || [ ! -S "$socks/WIN" ]; then
        kill -TERM "$(cat "$d/PIDS")" 2>/dev/null || :
        rm -rf "$socks"
        echo "$prog: $exe.$mode: WSRELAY.PY made no sockets" >&2
        return 1
    fi
    set --
    for f in "$d"/SEED/DATA/*; do
        set -- "$@" -h "$f"
    done
    sh "$repo/TEST/INTEG/boot310.sh" -t "$secs" -S "$socks/SRV" -f "$cfg" \
        -h "$srv" -h "$msg" -h "$here/WINSER.CFG" "$@" C: "MD DATA" \
        "COPY *.DBF DATA" "COPY *.NDX DATA" "COPY *.DBT DATA" \
        "INVSERV /CFG:WINSER.CFG" >"$d/SRV.LOG" 2>&1 &
    echo $! >>"$d/PIDS"
    case $exe in
    WSERT)
        case $mode in
        r) word=REAL ;; s) word=STANDARD ;; *) word=ENHANCED ;;
        esac
        set -- -M $word "$d/WIN" "$wsert" 1 ;;
    INVWIN) set -- -f "$here/PANE1.CFG" -k "$KEYS" -g "$SNAP" ;;
    INVBACK) set -- -k "$BKEYS" -g "$BSNAP" ;;
    *) set -- -k "$UKEYS" -g "$USNAP" ;;
    esac
    [ $exe = WSERT ] || set -- -f "$invmsg" "$@" "$d/WIN" "$invwin" /COM1
    [ $exe != INVWIN ] || set -- "$@" /CFG:PANE1.CFG
    wrc=0
    sh "$here/wintest.sh" -m "$mode" -t "$secs" -s "$socks/WIN" \
        -p "$socks/MON" "$@" >"$d/WIN.LOG" 2>&1 &
    echo $! >>"$d/PIDS"
    wait $! || wrc=$?
    rrc=0
    wait "$(sed -n 1p "$d/PIDS")" || rrc=$?
    # Without the relay the server never ends: end its machine.
    [ $rrc = 0 ] || kill -TERM "$(sed -n 2p "$d/PIDS")" 2>/dev/null || :
    brc=0
    wait "$(sed -n 2p "$d/PIDS")" || brc=$?
    rm -rf "$socks"
    r=$exe.$mode
    ok=1
    [ $wrc = 0 ] || { echo "$prog: $r: wintest.sh exit $wrc" >&2; ok=0; }
    [ $rrc = 0 ] || { echo "$prog: $r: WSRELAY.PY exit $rrc" >&2; ok=0; }
    [ $brc = 0 ] || { echo "$prog: $r: boot310.sh exit $brc" >&2; ok=0; }
    if [ $exe = WSERT ]; then
        grep '^[<>] ' "$d/WIN.LOG" >"$WORK/$r.TXT" || :
        if [ $update = 0 ] && ! cmp -s "$GOLDEN" "$WORK/$r.TXT"; then
            echo "$prog: $r: the session differs from $GOLDEN" \
                "(< golden, > run):" >&2
            diff "$GOLDEN" "$WORK/$r.TXT" >&2 || :
            ok=0
        fi
    fi
    if [ $ok = 1 ]; then
        echo "$prog: $r: passed in $(($(date +%s) - start)) s"
        rm -rf "$d"
    else
        echo "$prog: $r: failed; kept $d" >&2
    fi
    [ $ok = 1 ]
}

runs=
for m in $modes; do
    runs="$runs WSERT.$m INVWIN.$m INVUSER.$m INVBACK.$m"
done
for r in $runs; do
    run "${r%.*}" "${r#*.}" >"$WORK/$r.OUT" 2>&1 &
    echo $! >"$WORK/$r.PID"
done
status=0
for r in $runs; do
    wait "$(cat "$WORK/$r.PID")" || status=1
    cat "$WORK/$r.OUT"
done
if [ $update = 1 ] && [ $status = 0 ]; then
    first=${modes%% *}
    cp "$WORK/WSERT.$first.TXT" "$GOLDEN"
    echo "$prog: rewrote TEST/GOLDEN/WIN/WSERT.TXT from mode $first"
fi
[ $status = 1 ] || { rm -rf "$WORK"; echo "$prog: PASS"; }
exit $status
