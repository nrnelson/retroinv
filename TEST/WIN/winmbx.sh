#!/bin/sh
# winmbx.sh - the MBOX link of the Windows client between two machines:
# INVSERV serving its mailbox on the share of a Workgroup Add-On for
# MS-DOS server, and INVWIN on the win30nb test machine
# (TEST/WIN/winlib.sh), which reaches the share over NetBEUI, in standard
# and 386 enhanced mode.
#
# usage: sh TEST/WIN/winmbx.sh [-m MODES] INVWIN INV.MSG HOSTINV
#
#   INVWIN, INV.MSG  the client for Windows and its message file
#   HOSTINV  the host build of INV, which seeds the server's data
#   -m MODES the modes of Windows to run, runwin.sh's letters (default
#            "s 3": standard and 386 enhanced mode)
#
# The NetBIOS image (TEST/INTEG/mkimg.sh), the Add-On's image
# (TEST/INTEG/mkwga.sh) and win30nb's disk (TEST/WIN/mkwin.sh -w win30nb)
# must be current; the server is BIN/INVSERV.EXE.
#
# For each mode, one run.  VM A of TEST/INTEG/run2vm.sh, alone, on the
# Add-On's image, with its COM1 on a socket of the run, puts UP.TXT in
# D:\MBOX, shares that as MBOX and runs "INVSERV /CFG:WINMBX.CFG": no
# NetBIOS session, the mailbox MBOX, COM1 and the event log SRV.LOG, on
# a data directory that HOSTINV has seeded as the screen tests seed
# theirs (TEST/KEYS/SEED.REQ, wintest.sh -q).  Once run2vm.sh has written
# its ports, TEST/WIN/MBXRELAY.PY listens on PORT_B and passes the frames
# on to VM A's PORT_A, RELAY_MS late, as winnb.sh's relay does.  Then
# TEST/WIN/runwin.sh starts win30nb, its card on the relay and its
# monitor on a socket of the run, with NET.ON and NETUSE.BAT beside the
# program, so that before Windows the network starts, logs on and
# connects E: to MBOX, again until E:\UP.TXT is there; and Windows runs
# "INVWIN /MBOX:E:\" with the key script TEST/WIN/INVLINK.KEY, its
# station the machine name WIN_NBNAME of winlib.sh, which mkwin.sh's
# SETMACH gives DOS (dos_machine_name).  The relay requires the ADMIN
# password to cross the network only in the LOGIN that asks for a login
# ticket, before the ITEM GET of the script's item view, and to be
# nowhere in the Windows machine's memory at that ITEM GET; it writes
# down every ticket it saw cross.  Once Windows has ended,
# TEST/INTEG/SERPING.PY logs in on VM A's COM1 and ends the server with
# ADMIN SHUTDOWN.
#
# A run passes when runwin.sh ends with 0, INVWIN's snapshot equals
# TEST/GOLDEN/WIN/INVMBX.SNP but for its stack line, held to STACK_MARGIN
# (snap_check of TEST/shlib.sh), the logon and the connection of E: report
# success, the relay and SERPING.PY end with 0, run2vm.sh with 0 and
# INVSERV with errorlevel 0, the server's log SRV.LOG shows mailbox
# sessions, each opened from WIN_NBNAME, the mailbox holds nothing but
# UP.TXT, and neither the password nor any ticket the relay saw is on VM
# A's D:, whose request files the server zeroes and whose response files
# with a ticket the client zeroes, through the basic redirector, before
# it removes them.  When the Windows machine fails, VM A is stopped at
# once rather than at its limit.  Each mode's verdict and its seconds are
# printed; a failed run's directory is kept, with every program's output
# and the snapshot's difference from its golden shown.
#
# env: TIMEOUT     VM A's limit in seconds (default 600)
#      RELAY_MS    the relay's delay in whole milliseconds (default 10)
#      OUT         result directory (default OBJ/WINMBX), emptied first: it
#                  must be new or an earlier OUT
#      NET330_DIR, WGA500_DIR  the VM images (see TEST/INTEG/net330.sh and
#                  wga500.sh); WINNBIMG_DIR  win30nb's (see winlib.sh)
# Every limit in seconds, WIN_SECS below included, is multiplied by
# DOSTIME_SCALE (dos_secs of TEST/shlib.sh).
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

# win30nb's machine name, INVWIN's station and the user it logs on to the
# network as, read in a subshell that keeps winlib.sh's settings for
# mtools, TZ among them, from the VMs' clocks.
STATION=$(. "$here/winlib.sh" && echo "$WIN_NBNAME")
# The date and time the seeding runs at, TEST/T_SCR.SH's.
NOW=/NOW:$SEED_NOW
# The Windows machine's limit, and the seconds SERPING.PY gets for each
# answer of the server it ends.
WIN_SECS=400
SER_SECS=60
# The key script, the golden of its snapshot, and the request line at
# which the relay searches the Windows machine's memory.
KEYS=$here/INVLINK.KEY
SNAP=$repo/TEST/GOLDEN/WIN/INVMBX.SNP
MARK='ITEM GET'
# What NET LOGON, NET USE and NET SHARE say when they succeed.
NET_OK='The command completed successfully.'

modes='s 3'
while getopts m: opt; do
    case $opt in
    m) modes=$OPTARG ;;
    *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 3 ] || usage
for f in "$1" "$2"; do
    [ -f "$f" ] || die "no such file: $f"
done
[ -x "$3" ] || die "no such program: $3"
invwin=$(realpath "$1")
invmsg=$(realpath "$2")
inv=$(realpath "$3")
for m in $modes; do
    case $m in s|3) ;; *) die "unknown mode: $m" ;; esac
done
TIMEOUT=${TIMEOUT:-600}
export TIMEOUT
RELAY_MS=${RELAY_MS:-10}
is_uint "$RELAY_MS" && [ "$RELAY_MS" -gt 0 ] ||
    die "RELAY_MS must be a whole number above 0"
OUT=${OUT:-$repo/OBJ/WINMBX}
marker=.winmbx
case $(realpath -m "$OUT") in
/|"$HOME"|"$repo") die "refusing OUT=$OUT" ;;
esac
[ ! -e "$OUT" ] || [ -f "$OUT/$marker" ] ||
    die "OUT=$OUT exists and is not an earlier OUT"
need_tools python3 timeout mktemp
pw=$(sed -n 's/^LOGIN ADMIN //p' "$repo/TEST/KEYS/SEED.REQ" | tail -n 1 |
     tr -d '\r')
[ -n "$pw" ] || die "no LOGIN ADMIN in TEST/KEYS/SEED.REQ"
rm -rf "$OUT"
mkdir -p "$OUT/SRV/DATA" "$OUT/SRV/MBOX/REQUEST" "$OUT/SRV/MBOX/RESPONSE"
: >"$OUT/$marker"
relay_secs=$(dos_secs $((TIMEOUT + WIN_SECS)))
ser_secs=$(dos_secs $SER_SECS)

# The server's payload: INVSERV, its messages, settings and batch file,
# the data and the mailbox.
cp "$repo/BIN/INVSERV.EXE" "$repo/BIN/INVSERV.MSG" "$OUT/SRV/"
printf 'DATA=DATA\r\nSESSIONS=0\r\nMAILBOX=MBOX\r\nSERIAL=COM1\r\nLOG=SRV.LOG\r\n' \
    >"$OUT/SRV/WINMBX.CFG"
{
    printf '@ECHO OFF\r\nECHO UP> MBOX\\UP.TMP\r\nREN MBOX\\UP.TMP UP.TXT\r\n'
    printf 'C:\\NET\\NET SHARE MBOX=D:\\MBOX /FULL /SAVESHARE:NO /YES'
    printf ' > SHARE.OUT\r\n'
    printf 'INVSERV.EXE /CFG:WINMBX.CFG\r\n'
    errlevel_bat 127
    printf 'ECHO %%RC%%> SRV.RC\r\n'
} >"$OUT/SRV/MBXA.BAT"
dos_text "$repo/TEST/KEYS/SEED.REQ" |
    (cd "$OUT/SRV" && "$inv" /CMD /LOCAL DATA $NOW) >"$OUT/seed.log" 2>&1 ||
    fail "the seeding failed: $(cat "$OUT/seed.log")"
! grep -q '^-ERR' "$OUT/seed.log" ||
    fail "a seed request was refused: $(grep -m 1 '^-ERR' "$OUT/seed.log")"

srvpid=
relpid=
socks=
# halt [SIGNAL] - stop the relay and run2vm.sh, which kills VM A, and
# wait for them; also the signal handler (on_signals).
halt() {
    for h_p in $relpid $srvpid; do
        kill -s TERM "$h_p" 2>/dev/null || :
        wait "$h_p" 2>/dev/null || :
    done
    relpid=
    srvpid=
    [ -z "$socks" ] || rm -rf "$socks"
    socks=
}
on_signals halt

# says FILE TEXT - succeed when FILE holds the line TEXT, CR removed, or
# a line that ends with it after a prompt's colon: NET LOGON's for the
# password, which reading ENTER.TXT leaves unended.
says() {
    [ -f "$1" ] && tr -d '\r' <"$1" | awk -v t="$2" '
        $0 == t || substr($0, length($0) - length(t)) == ":" t { f = 1 }
        END { exit !f }'
}

# opened LOG - whether the server's event log LOG shows mailbox sessions,
# each opened from STATION; SERPING.PY's serial session has no station.
opened() {
    [ -f "$1" ] && tr -d '\r' <"$1" | awk -v st="$STATION" '
        $4 == "OPEN" && $5 != "" { k++; bad += $5 != st }
        END { exit bad || !k }'
}

# The run's port slot (port_slot of TEST/shlib.sh), its ports given to
# run2vm.sh: held by this script, and so by the relay and the Windows
# machine it starts, until all of them have ended, so that no other run
# takes those ports while the Windows machine, which reconnects to its
# relay, still lives; a slot of run2vm.sh's own ends with VM A.
port_slot
status=0
for m in $modes; do
    dir=$OUT/$m
    start=$(date +%s)
    # A socket's path is limited to 108 bytes; mktemp's is short.
    socks=$(mktemp -d)
    # run2vm.sh gives a VM run alone no serial port, so VM A's COM1
    # (3F8h, IRQ 4) is a device of QEMU_EXTRA, on a socket QEMU listens on.
    com1="-chardev socket,id=com1,path=$socks/COM1,server=on,wait=off"
    com1="$com1 -device isa-serial,chardev=com1,iobase=0x3f8,irq=4"
    IMAGE_A=wga500 QEMU_EXTRA=$com1 OUT=$dir/A \
        PORT_A=$((PORT_BASE + 2 * sl_slot)) \
        sh "$repo/TEST/INTEG/run2vm.sh" "$OUT/SRV" 'C:\COMMAND /C MBXA.BAT' \
        >"$dir.srv" 2>&1 &
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
    relay=$(sed -n 's/^PORT_B=//p' "$dir/A/PORTS")
    timeout "$relay_secs" python3 -I "$here/MBXRELAY.PY" "$RELAY_MS" \
        "$relay" "$port" "$socks/MON" "$pw" "$MARK" "$dir.tkt" \
        >"$dir.relay" 2>&1 &
    relpid=$!
    mkdir -p "$dir/W"
    cp "$invwin" "$invmsg" "$KEYS" "$dir/W/"
    : >"$dir/W/NET.ON"
    printf '\r\n' >"$dir/W/ENTER.TXT"
    {
        printf 'C:\\NET\\NET LOGON %s /YES /SAVEPW:NO' "$STATION"
        printf ' < C:\\RUN\\ENTER.TXT > C:\\RUN\\LOGON.OUT\r\n'
        printf ':USE\r\nC:\\NET\\NET USE E: \\\\RIVMA\\MBOX /YES'
        printf ' > C:\\RUN\\USE.OUT\r\nIF NOT EXIST E:\\UP.TXT GOTO USE\r\n'
    } >"$dir/W/NETUSE.BAT"
    k=$(basename "$KEYS")
    net=stream,addr.type=inet,addr.host=127.0.0.1,addr.port=$relay
    wrc=0
    sh "$here/runwin.sh" -w win30nb -m "$m" -t $WIN_SECS -p "$socks/MON" \
        -n "$net,server=off,reconnect-ms=200" "$dir/W" \
        "INVWIN /MBOX:E:\\ /KEYS:$k /SNAP:${k%.*}.SNP" >"$dir.win" 2>&1 ||
        wrc=$?
    rrc=0
    wait "$relpid" || rrc=$?
    relpid=
    ok=0
    src=0
    if [ $wrc = 0 ]; then
        : >"$dir.stop"
        python3 -I "$repo/TEST/INTEG/SERPING.PY" "$socks/COM1" "$dir.stop" \
            "$ser_secs" ADMIN "$pw" >"$dir.ser" 2>&1 || src=$?
        [ $src = 0 ] && wait "$srvpid" && ok=1
        srvpid=
    fi
    halt
    da=$dir/A/A/D
    if [ $ok = 1 ]; then
        [ $rrc = 0 ] &&
            snap_check "$SNAP" "$dir/W/${k%.*}.SNP" "$STACK_MARGIN" &&
            says "$dir/W/LOGON.OUT" "$NET_OK" &&
            says "$dir/W/USE.OUT" "$NET_OK" &&
            says "$da/SHARE.OUT" "$NET_OK" &&
            says "$da/SRV.RC" 0 &&
            opened "$da/SRV.LOG" &&
            [ "$(cd "$da/MBOX" && find . -type f)" = ./UP.TXT ] || ok=0
    fi
    if [ $ok = 1 ]; then
        for s in "$pw" $(cat "$dir.tkt"); do
            if grep -qaF -- "$s" "$dir/A/A/D.IMG"; then
                echo "$prog: a secret is on VM A's D: ($s)" >&2
                ok=0
            fi
        done
        [ -s "$dir.tkt" ] || { echo "$prog: no ticket crossed" >&2; ok=0; }
    fi
    secs=$(($(date +%s) - start))
    if [ $ok = 1 ]; then
        echo "$prog: $m PASS in $secs s"
        rm -rf "$dir" "$dir.srv" "$dir.win" "$dir.relay" "$dir.ser" \
            "$dir.stop" "$dir.tkt" "$dir.MEM"
        continue
    fi
    status=1
    echo "$prog: $m FAIL in $secs s (Windows $wrc, relay $rrc," \
        "SERPING $src)" >&2
    for f in "$dir.win" "$dir.relay" "$dir.ser" "$dir.srv" \
        "$dir/W/NETSTART.OUT" "$dir/W/LOGON.OUT" "$dir/W/USE.OUT" \
        "$da/SHARE.OUT" "$da/SRV.RC" "$da/SRV.LOG"; do
        [ ! -s "$f" ] || { echo "===== $f"; tr -d '\r' <"$f"; }
    done
    echo "===== $SNAP (<) and the run's (>)"
    diff "$SNAP" "$dir/W/${k%.*}.SNP" || :
done
[ $status = 0 ] || fail "FAIL; runs kept in $OUT"
rm -rf "$OUT"
echo "$prog: PASS"
