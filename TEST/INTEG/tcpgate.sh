#!/bin/sh
# tcpgate.sh - TCP clients on the host through INVTCP, the TCP bridge, into
# the RETROINV server, between two MS-DOS 3.30 QEMU VMs.
#
# usage: sh TEST/INTEG/tcpgate.sh [-u] [-m KB] [EXEDIR]
#
#   EXEDIR  directory of one build's INVSERV.EXE and INVTCP.EXE (default
#           BIN, the Open Watcom build, made first); the message file
#           INVSERV.MSG is the host-built BIN/INVSERV.MSG
#   -m KB   start INVTCP with KB kilobytes of free conventional memory
#           (less than one more, KB below 640), as CHKDSK reports it in
#           the same place, to find or check the memory INVTCP needs
#   -u      rewrite the golden files from this run instead of comparing
#           them; review the difference before keeping it
#
# One run of run2vm.sh with VM B's second card, the NE2000 on QEMU's user
# network (NIC2_B), and the VMs' monitors open.  VM A runs, from D:\,
# "INVSERV /CFG:SRV.CFG" on the fresh data directory D:\DATA, its event
# log D:\SRV.LOG; its COM1, which SRV.CFG serves, is joined to VM B's
# (SERIAL=1 of run2vm.sh) and carries nothing.  Once INVSERV has ended,
# VM A writes SRV_ENDED on its screen and waits for a key, its NetBIOS
# provider still running.
# VM B loads the packet driver, runs INVTCP with a server name of
# NB_NAMELEN characters and with "*", each a usage error, then "INVTCP
# RISRV" with its event lines on COM2, which QEMU writes to a host file
# as they come, then WATTCP's PING of the host's gateway, which proves
# INVTCP released the packet driver.  Once the server's status screen is
# up and INVTCP has said START, TCPCLI.PY plays TCP.SCR through the
# host's PORT_T (run2vm.sh's OUT/PORTS): HELLO, a first LOGIN and PASSWD,
# and requests written together on TS_SLOTS sessions at once, among them
# a write of over 2 KB and three lists of over 2 KB each; one more
# connection gets no bytes and is closed; nc's session takes a freed
# slot; ADMIN SHUTDOWN ends the server and every session; once VM A's
# screen shows SRV_ENDED, a connection finds no server, its name deleted,
# and is closed without a byte, and two more send bytes the bridge never
# reads, one closed by the bridge and one reset by the host.  INVTCP's
# memory is searched once the new password's requests are answered, and
# for each of those two connections' bytes once it has ended (MEM.PY
# below).  The host then presses Enter on VM A and, on VM B, F3 and, once
# INVTCP's question is on its screen, Y, or, when anything failed, ends
# both VMs.  nc must be the traditional or the OpenBSD netcat, which keep
# their connection open for the answers at the end of their input.
#
# The host requires exit status 1 of the two refused INVTCP runs, 0 of
# INVSERV and of "INVTCP RISRV", and PING's 4 replies; the transcript of
# TCPCLI.PY's "until" steps, nc's lines after it as "nc< ", equal to
# TEST/GOLDEN/TCP.TXT; INVTCP's event lines, the time, a connection's
# slot and the peer's port masked and the lines sorted, equal to
# TEST/GOLDEN/TCPEV.TXT; every line of the server's event log well
# formed, with one START, STACK and STOP 0 event, the STACK at least
# SRV_STACK_MARGIN (log_check of vmgate.sh); none of those searches
# finding what it looks for; and the new password of TCP.SCR in no file
# the VMs left.  The VMs' clocks start at the fixed time CLOCK.
#
# On failure every program's output is printed, and the result directory
# is kept as OUT.fail.<timestamp>.
#
# env: TIMEOUT  per-VM limit in seconds (default 300), times DOSTIME_SCALE
#               (dos_secs of TEST/shlib.sh); each receive of the host's
#               sessions has a tenth of it
#      OUT      result directory (default TEST/INTEG/vm/tcpgate.run); it
#               must be new or an earlier OUT
#      WATTCP_APPS  directory holding WATTCP's PING.EXE (default under
#               RETRO_DL)
#      PORT_A, PCAP, PKTDRV, NET330_DIR  as run2vm.sh takes them
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
SERVER=RISRV                    # the server's NetBIOS name
SRV_ENDED='TCPGATE SERVER ENDED'    # VM A's screen once INVSERV has ended
usage="usage: tcpgate.sh [-u] [-m KB] [EXEDIR]"
update=0
freekb=
while [ $# -gt 0 ]; do
    case $1 in
        -u) update=1; shift ;;
        -m) [ $# -ge 2 ] && is_uint "$2" && [ "$2" -lt 640 ] ||
                die "-m needs a number of KB below 640"
            freekb=$2
            shift 2 ;;
        -*) die "$usage" ;;
        *) break ;;
    esac
done
[ $# -le 1 ] || die "$usage"
exedir=${1:-BIN}
TIMEOUT=${TIMEOUT:-300}
OUT=${OUT:-"$here/vm/tcpgate.run"}
WATTCP_APPS=${WATTCP_APPS:-"$RETRO_DL/wattcp/x/apps"}

OUT=$(out_check "$OUT" .run2vm) || exit 2
export TIMEOUT OUT

need_tools python3 mktemp nc timeout
nc -h 2>&1 | grep -q -e '^\[v1\.10' -e '^OpenBSD netcat' ||
    die "nc must be the traditional or the OpenBSD netcat"
progs="BIN/QEXIT.COM BIN/EATRUN.COM BIN/INVSERV.MSG"
[ "$exedir" != BIN ] || progs="$progs BIN/INVSERV.EXE BIN/INVTCP.EXE"
make -s -C "$repo" $progs || die "the programs could not be built"
case $exedir in /*) ;; *) exedir="$repo/$exedir" ;; esac
for f in INVSERV.EXE INVTCP.EXE; do
    [ -f "$exedir/$f" ] || die "not found: $exedir/$f"
done
ping=$(find_ci "$WATTCP_APPS" PING.EXE)

gate_start tcpgate

a="$payload/A"
b="$payload/B"
srv_files "$a" "$exedir"
mkdir -p "$b"
cp "$exedir/INVTCP.EXE" "$ping" "$b/"
dos_text <"$here/WATTCP.CFG" >"$b/WATTCP.CFG"
{
    printf '@ECHO OFF\r\nINVSERV /CFG:SRV.CFG\r\n'
    rc_to SRV.RC
    printf 'ECHO %s> CON\r\nPAUSE\r\n' "$SRV_ENDED"
} >"$a/SRVA.BAT"
{
    printf '@ECHO OFF\r\nNE2000 0x60 11 0x320 > PKTDRV.OUT\r\n'
    printf 'INVTCP RISRV6789ABCDEF0\r\n'
    rc_to LONG.RC
    printf 'INVTCP *\r\n'
    rc_to STAR.RC
    [ -z "$freekb" ] || eat "$b" "$freekb" FREE.OUT
    printf '%sINVTCP.EXE %s > COM2\r\n' "$(kb_run "$freekb")" "$SERVER"
    rc_to TCP.RC
    printf 'PING 10.0.2.2 4 > PING.OUT\r\n'
} >"$b/TCPB.BAT"

# The searches of INVTCP's memory that TCP.SCR runs through MEM, each in
# a dump of VM B's memory through its monitor from INVTCP's PSP up: the
# NetBIOS provider, loaded below, keeps copies of the frames it sent.
# "absent TEXT" fails when TEXT is there.  "drop PORT" and "reset PORT"
# open a session through PORT once the server has gone, while the
# bridge's CALL waits for the name query to fail and so reads nothing,
# and send a marker until it is there.  drop waits for the bridge to
# close the session and, the connection kept open so that its socket is
# not let go, wants the marker gone; reset resets the session and, once
# INVTCP reports it closed, wants the marker gone before the slot
# listens again, which waits for the CALL.
cat >"$payload/MEM.PY" <<'EOF'
import os, re, socket, struct, sys, time
mon, ev, dump = os.getenv('MON'), os.getenv('EV'), os.getenv('DUMP')
secs = float(os.getenv('STEP'))
cmd, arg = sys.argv[1], sys.argv[2]
# INVTCP's PSP (INT 20h, its command tail at 80h: the server's name, a
# blank before it and one after it left by the redirection) at the
# paragraph after its memory block's header ('M' or 'Z', then the
# owner's PSP).
tail = (' %s ' % os.getenv('SERVER')).encode('ascii')
PSP = re.compile(rb'[MZ](..).{13}\xcd\x20.{126}' +
                 re.escape(bytes((len(tail),)) + tail + b'\r'), re.S)


def at(mark):
    """Where MARK is in INVTCP's memory, -1 when nowhere."""
    m = socket.socket(socket.AF_UNIX)
    m.settimeout(secs)
    m.connect(mon)

    def prompt():
        b = b''
        while not b.endswith(b'(qemu) '):
            d = m.recv(4096)
            if not d:
                sys.exit('tcpgate: the monitor closed')
            b += d
    prompt()
    m.sendall(('pmemsave 0 655360 "%s"\n' % dump).encode('ascii'))
    prompt()
    m.close()
    with open(dump, 'rb') as f:
        mem = f.read()
    for p in PSP.finditer(mem):
        if p.start() % 16 == 0 and \
                struct.unpack('<H', p.group(1))[0] == p.start() // 16 + 1:
            return mem.find(mark, p.start())
    sys.exit("tcpgate: INVTCP's PSP is not in memory")


def gone(mark, when):
    i = at(mark)
    if i >= 0:
        sys.exit('tcpgate: %r is at %05Xh %s' % (mark, i, when))


def closes():
    with open(ev, 'rb') as f:
        return f.read().count(b' CLOSE TCP')


if cmd == 'absent':
    gone(arg.encode('latin-1'), 'in memory')
    sys.exit(0)
mark = ('RETROINV TCPGATE ' + cmd.upper()).encode('ascii')
c = socket.create_connection(('127.0.0.1', int(arg)), secs)
end = time.monotonic() + secs
# Bytes sent before the guest is ESTABLISHED may be lost (TCP.SCR), so
# the marker goes again until it is in.
while at(mark) < 0:
    if time.monotonic() > end:
        sys.exit('tcpgate: the %s marker never reached memory' % cmd)
    try:
        c.sendall(mark)
    except OSError as e:
        sys.exit('tcpgate: the %s session ended early: %s' % (cmd, e))
    time.sleep(0.2)
if cmd == 'drop':
    try:
        got = c.recv(1)
    except OSError as e:
        sys.exit('tcpgate: the drop session was not closed cleanly: %s' % e)
    if got:
        sys.exit('tcpgate: the drop session got bytes')
    gone(mark, 'once the bridge closed its session')
    sys.exit(0)
n = closes()
c.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
c.close()
while closes() == n:
    if time.monotonic() > end:
        sys.exit('tcpgate: INVTCP did not report the reset session closed')
    time.sleep(0.2)
gone(mark, 'once the session was reset')
EOF

# INVTCP's event lines, on VM B's COM2 (the port after run2vm.sh's COM1),
# and the variables TCP.SCR and MEM.PY read.
EV="$payload/COM2.OUT"
STEP=$(($(dos_secs "$TIMEOUT") / 10))
NCOUT="$payload/NC.OUT"
MEM="$payload/MEM.PY"
MON="$mondir/B.sock"
SRVMON="$mondir/A.sock"
QMON="$here/qmon.py"
DUMP="$payload/MEM.BIN"
export STEP NCOUT EV MEM MON DUMP SERVER SRVMON QMON SRV_ENDED
rc=0
rm -f "$OUT/PORTS"
MONDIR=$mondir NIC2_B=1 SERIAL=1 QEMU_EXTRA="-rtc base=$CLOCK" \
    QEMU_EXTRA_B="-serial file:$EV" \
    sh "$here/run2vm.sh" "$a" 'C:\COMMAND /C SRVA.BAT' \
                         "$b" 'C:\COMMAND /C TCPB.BAT' &
runpid=$!
if wait_for PORT_T= "$OUT/PORTS" 60 &&
   qmon A "wait:Sessions open:$(dos_secs 240)" &&
   wait_for ' 0 START ' "$EV" "$(dos_secs 60)"; then
    PORT=$(sed -n 's/^PORT_T=//p' "$OUT/PORTS")
    export PORT
    python3 -I "$here/TCPCLI.PY" -t "$STEP" -o "$payload/TCP.OUT" \
        127.0.0.1 "$PORT" "$here/TCP.SCR" &
    qpid=$!
    wait "$qpid" || status=1
    qpid=
else
    echo "$prog: the server or INVTCP did not start" >&2
    status=1
fi
if [ "$status" -eq 0 ]; then
    qmon A key:ret || :
    qmon B key:f3 "wait:Stop the bridge?:$(dos_secs 30)" key:y || :
else
    quit_vms
fi
wait "$runpid" || rc=$?
runpid=
# Any status but 0 and 1 (a setup error, a signal) ends the run here and
# leaves OUT as it is.
case $rc in 0|1) ;; *) exit "$rc" ;; esac
[ "$rc" -eq 0 ] || status=1

da="$OUT/A/D"
db="$OUT/B/D"
expect "$da/SRV.RC" 0
expect "$db/LONG.RC" 1
expect "$db/STAR.RC" 1
expect "$db/TCP.RC" 0
if ! tr -d '\r' 2>/dev/null <"$db/PING.OUT" |
        grep -Eq '^Received *: *4 *$'; then
    echo "$prog: PING after INVTCP did not get 4 replies" >&2
    status=1
fi

{
    cat "$payload/TCP.OUT" 2>/dev/null || :
    tr -d '\r' 2>/dev/null <"$NCOUT" | sed 's/^/nc< /' || :
} >"$payload/TCP.TXT"
golden "$payload/TCP.TXT" TCP.TXT TCP.TXT
# The time, a connection's slot (1 to TS_SLOTS) and the peer's port
# masked, the lines sorted.
stamp='[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}'
tr -d '\r' <"$EV" |
    sed -E -e "s/^$stamp [1-4] /T n /" -e "s/^$stamp 0 /T 0 /" \
        -e 's/:[0-9]+$//' | LC_ALL=C sort >"$payload/TCPEV.TXT"
golden "$payload/TCPEV.TXT" TCPEV.TXT TCPEV.TXT
golden_done 2

log_check "$da/SRV.LOG" 1

# No new password anywhere the VMs wrote.
pw=$(sed -n 's/^send [0-9]* .*PASSWD admin \([^\\]*\)\\r\\n.*/\1/p' \
     "$here/TCP.SCR")
[ -n "$pw" ] || die "TCP.SCR changes no password to look for"
hits=$(grep -rlF -- "$pw" "$da" "$db" "$EV") || :
[ -z "$hits" ] || { echo "$prog: the password is in $hits" >&2; status=1; }

free_print B
free_check B "$freekb" FREE.OUT

[ "$status" -eq 0 ] ||
    cp "$EV" "$payload/TCP.OUT" "$payload/TCP.TXT" "$payload/TCPEV.TXT" \
        "$OUT/" 2>/dev/null || :
gate_end "$da/SRV.LOG" "$EV" "$payload/TCP.OUT" "$NCOUT" \
    "$db/PKTDRV.OUT" "$db/PING.OUT"
