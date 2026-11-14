#!/bin/sh
# tcpprobe.sh - run TCPTEST, the TCP probe of the bridge's sockets, in a
# QEMU DOS VM, drive its echo sessions from the host, and prove WATTCP's
# own PING and FINGER after it.
#
# usage: sh TEST/INTEG/tcpprobe.sh [-v 310|330] [EXEDIR]
#
#   -v 330   boot the MS-DOS 3.30 disk 1 under RETRO_DL (the default)
#   -v 310   boot the Compaq MS-DOS 3.10 disk 1 (DOS310_FLOPPY of
#            TEST/shlib.sh)
#   EXEDIR   directory holding the build's TCPTEST.EXE (default BIN)
#
# env: TIMEOUT    host-side limit in seconds for the VM run (default 180),
#                 times DOSTIME_SCALE (dos_secs of TEST/shlib.sh); each
#                 receive of the host's sessions has a tenth of it
#      KEEP_VM=1  keep the scratch directory even when the test passes
#      WATTCP_APPS  directory holding WATTCP's PING.EXE and FINGER.EXE
#                 (default under RETRO_DL)
#      PKTDRV     directory holding the Crynwr NE2000.COM (default under
#                 RETRO_DL)
#      RETRO_DL   root of the period downloads (default $HOME/retro-dl)
#      PORT_LOCKS  as run2vm.sh takes it
#
# The boot floppy's AUTOEXEC.BAT hands over to a 360 KB B: (MS-DOS 3.10
# under QEMU hangs on a larger one), which loads the NE2000 packet driver
# and runs TCPTEST with its standard output on COM1, which QEMU writes to
# a host file as it comes.  The host port of the run's port slot
# (port_slot of TEST/shlib.sh) is forwarded to the guest's port BR_PORT.
# Once TCPTEST is listening, TCPCLI.PY plays the script below: TS_SLOTS
# concurrent sessions echo lines, one longer than TCPTEST's line buffer;
# one more connection gets no bytes and is closed; a burst of more frames
# than the probe has receive buffers arrives at once through a second
# QEMU network port on the card's hub, its first frame is answered, and
# every session still echoes; no copy of the burst's marker is left in
# the guest's memory; a closed session's slot takes a new connection;
# QUIT ends every session and TCPTEST.  The sockets' wipes of received
# bytes (TSOCK.H) are proved by two markers no copy of which may be left
# in the guest's memory: one that comes with QUIT past the bytes TCPTEST
# reads, which the read moves and QUIT's close drops, and one that a
# session reset while TCPTEST waits for the closes leaves unread.
# WATTCP's PING of the host's gateway then proves that TCPTEST released
# the packet driver, and WATTCP's FINGER of probe@10.0.2.100 that a guest
# reaches the host over TCP: QEMU's guestfwd joins 10.0.2.100:79 to a
# one-shot listener on the host, which records the request and answers
# with a probe line.  QEXIT.COM ends the VM.
#
# CPKT's drop of a frame longer than any Ethernet frame is not covered:
# this packet driver drops such a frame before the probe's buffers see
# it.
#
# Checks: every TCPCLI.PY step, TCPTEST's event lines (slots and peer
# ports masked), QUIT and then the verdict PASS last, its exit status 0,
# PING's 4 replies, and the finger request and answer.
#
# Exit status: 0 when the probe passes, 1 when it fails, 2 on a setup
# error, 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.  Each run works in
# its own directory TEST/INTEG/vm/tcpprobe.XXXXXX, removed on success and
# on interruption and kept (and named) on failure.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$repo/TEST/shlib.sh"

usage="usage: tcpprobe.sh [-v 310|330] [EXEDIR]"
dos=330
while getopts v: o; do
    case $o in
        v) dos=$OPTARG ;;
        *) die "$usage" ;;
    esac
done
shift $((OPTIND - 1))
[ $# -le 1 ] || die "$usage"
exedir=${1:-"$repo/BIN"}
case $dos in
    330) img=$DOS330_FLOPPY ;;
    310) img=$DOS310_FLOPPY ;;
    *) die "$usage" ;;
esac
WATTCP_APPS=${WATTCP_APPS:-"$RETRO_DL/wattcp/x/apps"}
PKTDRV=${PKTDRV:-"$RETRO_DL/pktd"}
TIMEOUT=${TIMEOUT:-180}
KEEP_VM=${KEEP_VM:-0}

qexit="$repo/BIN/QEXIT.COM"
vm=
socks=
lsn=
qpid=

# stop_children - stop QEMU and the finger listener.
stop_children() {
    [ -z "$qpid" ] || kill "$qpid" 2>/dev/null || :
    [ -z "$lsn" ] || kill "$lsn" 2>/dev/null || :
}

# die MESSAGE - as the shared die (a setup error, status 2), but first
# stops the children and removes the scratch directory if one was made.
die() {
    echo "$prog: $*" >&2
    stop_children
    if [ -n "$vm" ]; then
        rm -rf "$vm"
        echo "$prog: scratch directory $vm removed" >&2
    fi
    exit 2
}

# Keeps the script's own exit status; the cleanup must not replace it.
on_exit() {
    st=$?
    set +e
    stop_children
    wait 2>/dev/null
    [ -z "$socks" ] || rm -rf "$socks"
    exit $st
}

# stop SIGNAL - the signal handler (on_signals): stop the children and
# remove the scratch directory.
stop() {
    set +e
    echo "$prog: stopped by SIG$1" >&2
    stop_children
    wait 2>/dev/null || :
    [ -z "$vm" ] || rm -rf "$vm"
}

trap on_exit EXIT
on_signals stop

is_uint "$TIMEOUT" && [ "$TIMEOUT" -gt 0 ] ||
    die "TIMEOUT must be a positive integer: $TIMEOUT"
limit=$(dos_secs "$TIMEOUT")
step=$(((limit + 9) / 10))

for t in qemu-system-i386 mcopy mformat mtype python3 timeout make flock; do
    command -v "$t" >/dev/null 2>&1 || die "missing tool: $t"
done
[ -f "$img" ] || die "DOS boot disk not found: $img"
tcptest=$(find_ci "$exedir" TCPTEST.EXE)
ne2000=$(find_ci "$PKTDRV" NE2000.COM)
ping=$(find_ci "$WATTCP_APPS" PING.EXE)
finger=$(find_ci "$WATTCP_APPS" FINGER.EXE)
make -s -C "$repo" BIN/QEXIT.COM >/dev/null ||
    die "BIN/QEXIT.COM could not be built"

mkdir -p "$here/vm" || die "cannot create $here/vm"
vm=$(mktemp -d "$here/vm/tcpprobe.XXXXXX") ||
    die "cannot create a scratch directory"
# A unix socket's path is limited to 108 bytes; mktemp's is short.
socks=$(mktemp -d) || die "cannot create a socket directory"
port_slot
port=$((PORT_BASE + 2 * PORT_SLOTS + sl_slot))

# The host's finger listener on a port the kernel picks, printed on its
# first line.  It must be listening before QEMU starts, because the
# guestfwd chardev connects to it then.  It serves that one connection:
# it records the request line in finger.req and answers with the probe
# line.
probe="RETROINV WATTCP PROBE $$"
cat >"$vm/FINGER.PY" <<'EOF'
import socket, sys
req, answer, limit = sys.argv[1], sys.argv[2], float(sys.argv[3])
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen(1)
print(s.getsockname()[1])
s.settimeout(limit)
c, _ = s.accept()
c.settimeout(limit)
q = b''
try:
    while b'\n' not in q:
        d = c.recv(200)
        if not d:
            break
        q += d
except socket.timeout:
    pass
with open(req, 'wb') as f:
    f.write(q)
c.sendall(answer.encode('ascii') + b'\r\n')
c.close()
EOF
# listen.log is made first, as COM1.OUT is below: a program started in
# the background opens its output in its own time, under load maybe
# after the file is first read, and here a sed that cannot read it would
# end the script (set -e).
: >"$vm/listen.log"
python3 -I -u "$vm/FINGER.PY" "$vm/finger.req" "$probe" "$limit" \
    >"$vm/listen.log" 2>&1 &
lsn=$!
fport=
n=0
while [ $n -lt 50 ]; do
    kill -0 "$lsn" 2>/dev/null || break
    fport=$(sed -n '1{/^[0-9][0-9]*$/p}' "$vm/listen.log")
    [ -n "$fport" ] && break
    sleep 0.1
    n=$((n + 1))
done
if [ -z "$fport" ] || ! kill -0 "$lsn" 2>/dev/null; then
    echo "$prog: finger listener output:" >&2
    cat "$vm/listen.log" >&2
    die "finger listener failed to start"
fi

# A: boot disk with an AUTOEXEC.BAT that hands over to B:\RUN.BAT and keeps
# DOS from prompting for date and time.
cp "$img" "$vm/A.IMG" || die "cannot copy $img"
chmod u+w "$vm/A.IMG" || die "cannot make A.IMG writable"
printf '@ECHO OFF\r\nB:\r\nRUN.BAT\r\n' >"$vm/AUTOEXEC.BAT" ||
    die "cannot write AUTOEXEC.BAT"
mcopy -o -i "$vm/A.IMG" "$vm/AUTOEXEC.BAT" ::AUTOEXEC.BAT ||
    die "cannot copy AUTOEXEC.BAT to the boot disk (disk full?)"

# B: the programs and WATTCP.CFG beside them.  QEXIT 0 ends QEMU with
# status 1, QEXIT 1 with status 3.
dos_text >"$vm/RUN.BAT" <<'EOF' || die "cannot write RUN.BAT"
@ECHO OFF
NE2000 0x60 11 0x320 > B:\PKTDRV.OUT
TCPTEST > COM1
IF ERRORLEVEL 1 QEXIT 1
PING 10.0.2.2 4 > B:\PING.OUT
FINGER probe@10.0.2.100 > B:\FINGER.OUT
QEXIT 0
EOF
mformat -i "$vm/B.IMG" -C -f 360 -v TCPPROBE :: || die "cannot format B.IMG"
for f in "$ne2000" "$tcptest" "$ping" "$finger" "$qexit" "$vm/RUN.BAT"; do
    mcopy -i "$vm/B.IMG" "$f" "::$(basename "$f" | tr a-z A-Z)" ||
        die "cannot copy $f to B: (disk full?)"
done
mcopy -t -i "$vm/B.IMG" "$here/WATTCP.CFG" ::WATTCP.CFG ||
    die "cannot copy WATTCP.CFG to B:"

# The burst, the searches of the guest's memory and the reset of a session
# that holds bytes, through QEMU's frame socket, its QMP socket and the
# host's port.  The burst opens with an ARP request for the probe's address
# from a station 10.0.2.99 of its own, which must be answered: the probe
# keeps the frames that found a free buffer and drops the later ones.
# Every other frame is broadcast, of the IP type, which the probe takes,
# and not a valid datagram, which it drops; its marker ends it, past what
# any frame the sessions bring in overwrites.  "absent MARK" fails when
# MARK is in the guest's memory.  "reset PORT" runs while TCPTEST waits
# for QUIT's closes, and so reads nothing: it opens two sessions through
# PORT, sends a marker on the first until the marker is in the guest's
# memory, resets that session and waits for the marker to leave, while
# the second keeps TCPTEST waiting.  TCPTEST must not have ended by then,
# or the wipe may be its end's rather than the reset's.
cat >"$vm/PROBE.PY" <<'EOF'
import json, socket, struct, sys, time
socks, vm, cmd = sys.argv[1], sys.argv[2], sys.argv[3]
secs = float(sys.argv[4])
if cmd == 'burst':
    mark = b'RETROINV TCPPROBE BURST'
    me = b'\x02\x00\x00\x00\x00\x01'
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    s.bind(socks + '/HOST')
    ask = (b'\xff' * 6 + me + b'\x08\x06\x00\x01\x08\x00\x06\x04\x00\x01'
           + me + bytes((10, 0, 2, 99)) + bytes(6) + bytes((10, 0, 2, 15)))
    head = b'\xff' * 6 + me + b'\x08\x00'
    frames = [ask + bytes(60 - len(ask))]
    frames += [head + bytes(1514 - len(head) - len(mark)) + mark] * 16
    for f in frames:
        s.sendto(f, socks + '/QEMU')
    s.settimeout(secs)
    try:
        while True:
            f = s.recv(2048)
            if (f[12:14] == b'\x08\x06' and f[20:22] == b'\x00\x02'
                    and f[38:42] == bytes((10, 0, 2, 99))):
                sys.exit(0)
    except socket.timeout:
        sys.exit("tcpprobe: no answer to the burst's first frame")
m = socket.socket(socket.AF_UNIX)
m.settimeout(secs)
m.connect(socks + '/QMP')
q = m.makefile('rw')
q.readline()


def qmp(c):
    q.write(json.dumps(c) + '\n')
    q.flush()
    r = json.loads(q.readline())
    if 'return' not in r:
        sys.exit('tcpprobe: QMP refused %s: %s' % (c, r))


def at(mark):
    """Where MARK is in the guest's memory, -1 when nowhere."""
    qmp({'execute': 'pmemsave', 'arguments': {
        'val': 0, 'size': 655360, 'filename': vm + '/MEM.BIN'}})
    with open(vm + '/MEM.BIN', 'rb') as f:
        return f.read().find(mark)


def ended():
    """True once TCPTEST has written its verdict, its last line."""
    with open(vm + '/COM1.OUT', 'rb') as f:
        out = f.read()
    return b'TCPTEST PASS' in out or b'TCPTEST FAIL' in out


qmp({'execute': 'qmp_capabilities'})
if cmd == 'absent':
    i = at(sys.argv[5].encode('ascii'))
    if i >= 0:
        sys.exit('tcpprobe: "%s" is at %05Xh' % (sys.argv[5], i))
    sys.exit(0)
if cmd != 'reset':
    sys.exit('tcpprobe: unknown PROBE.PY step: %s' % cmd)
mark = b'RETROINV TCPPROBE RESET'
c = [socket.create_connection(('127.0.0.1', int(sys.argv[5])), secs)
     for _ in range(2)]
end = time.monotonic() + secs
# A session's bytes sent before the guest is ESTABLISHED may be lost
# (see the sessions below), so the marker goes again until it is in.
while at(mark) < 0:
    if ended() or time.monotonic() > end:
        sys.exit('tcpprobe: the reset marker never reached the guest')
    try:
        c[0].sendall(mark)
    except OSError as e:
        sys.exit('tcpprobe: the reset session ended before its marker'
                 ' reached the guest: %s' % e)
    time.sleep(0.2)
c[0].setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
c[0].close()
while at(mark) >= 0 and time.monotonic() < end:
    time.sleep(0.2)
if ended() or time.monotonic() >= end:
    sys.exit("tcpprobe: the reset session's marker did not leave the guest's"
             " memory while TCPTEST waited for its closes")
EOF

# The host's sessions, each receive limited to step seconds.  Sessions 1
# to 4 take every slot; 5 is refused, then takes the slot that 4 leaves
# once TCPTEST reports it closed, as it listens again at once.  No
# session sends before TCPTEST's event line reports it OPEN, which
# WATTCP's ESTABLISHED state brings: when the guest's SYN-ACK is resent,
# libslirp (QEMU's user network) handles its MSS option again, resets its
# socket buffer and drops, never to send them, the bytes of the client
# not yet acknowledged.  Sessions 1 and 2 then leave two slots listening
# for PROBE.PY's reset.  QUIT comes in one segment with a marker past the
# TT_LINE bytes (of TCPTEST.C) that TCPTEST reads with it: the read moves
# the marker down in the socket's buffer and QUIT's close drops it there.
# Once session 5's end shows that TCPTEST has closed every session, and
# while session 3 still holds its slot, no copy of the marker may be left.
# The script is written here, not kept beside TCP.SCR, as QUIT's filler
# follows TT_LINE; TCPCLI.PY expands VM and SOCKS itself, after splitting
# each line, so that a path with a blank in it stays one word.
VM=$vm
SOCKS=$socks
export VM SOCKS
helper='run python3 -I "$VM/PROBE.PY" "$SOCKS" "$VM"'
ttline=$(sed -n 's/^#define TT_LINE  *\([0-9][0-9]*\) .*/\1/p' \
    "$repo/SRC/TOOLS/TCPTEST.C")
is_uint "$ttline" || die "TT_LINE not found in SRC/TOOLS/TCPTEST.C"
# QUIT, its CR LF, filler up to TT_LINE bytes, then the marker.
pad=$(printf "%$((ttline - 6))s" '' | tr ' ' x)
quit="QUIT\\r\\n${pad}RETROINV TCPPROBE QUIT"
cat >"$vm/PROBE.SCR" <<EOF
open 1
open 2
open 3
open 4
wait \$VM/COM1.OUT 4 OPEN
send 1 line 1\r\n
send 2 line 2\r\n
send 3 line 3\r\n
send 4 line 4\r\n
expect 4 line 4\r\n
expect 3 line 3\r\n
expect 2 line 2\r\n
expect 1 line 1\r\n
send 2 0123456789abcdef*20
send 2 \n
expect 2 0123456789abcdef*20
expect 2 \n
refused 5 line 5\n
$helper burst $step
send 1 again 1\n
send 2 again 2\n
send 3 again 3\n
send 4 again 4\n
expect 1 again 1\n
expect 2 again 2\n
expect 3 again 3\n
expect 4 again 4\n
$helper absent $step 'RETROINV TCPPROBE BURST'
close 4
wait \$VM/COM1.OUT 1 CLOSE
open 5
wait \$VM/COM1.OUT 5 OPEN
send 5 line 5\n
expect 5 line 5\n
close 1
close 2
wait \$VM/COM1.OUT 3 CLOSE
send 3 $quit
eof 5
$helper absent $step 'RETROINV TCPPROBE QUIT'
$helper reset $step $port
eof 3
EOF

# The card is on a hub with QEMU's user network, where the host's port
# leads to TCPTEST and 10.0.2.100:79 to the finger listener, and with the
# frame socket of the burst.
user="user,id=n1,hostfwd=tcp:127.0.0.1:$port-:7000"
user="$user,guestfwd=tcp:10.0.2.100:79-tcp:127.0.0.1:$fport"
dgram="dgram,id=n2,local.type=unix,local.path=$socks/QEMU"
dgram="$dgram,remote.type=unix,remote.path=$socks/HOST"
# QEMU runs in the background so that a signal reaches the trap at once.
# -d nochain works around a QEMU bug in 16-bit near-call wrapping;
# see boot310.sh.  COM1.OUT is made first, as listen.log is, so that a
# read of it before QEMU opens it finds the file.
: >"$vm/COM1.OUT"
timeout "$limit" qemu-system-i386 -display none -no-reboot -m 4 \
    -accel "$QEMU_ACCEL" -d nochain -D /dev/null \
    -drive file="$vm/A.IMG",format=raw,if=floppy,index=0 \
    -drive file="$vm/B.IMG",format=raw,if=floppy,index=1 \
    -boot a \
    -device ne2k_isa,iobase=0x320,irq=11,netdev=h0 \
    -netdev hubport,id=h0,hubid=0 \
    -netdev "$user" -netdev hubport,id=h1,hubid=0,netdev=n1 \
    -netdev "$dgram" -netdev hubport,id=h2,hubid=0,netdev=n2 \
    -device isa-debug-exit,iobase=0x501,iosize=0x01 \
    -serial file:"$vm/COM1.OUT" -parallel none -monitor none \
    -qmp unix:"$socks/QMP",server,nowait \
    2>"$vm/QEMU.ERR" &
qpid=$!

status=0
n=0
until grep -q 'TCPTEST LISTEN' "$vm/COM1.OUT"; do
    if ! kill -0 "$qpid" 2>/dev/null || [ $n -ge $((limit * 5)) ]; then
        echo "$prog: TCPTEST did not start listening" >&2
        status=1
        break
    fi
    sleep 0.2
    n=$((n + 1))
done
if [ $status -eq 0 ]; then
    python3 -I "$here/TCPCLI.PY" -t "$step" 127.0.0.1 "$port" \
        "$vm/PROBE.SCR" || status=1
fi
[ $status -eq 0 ] || kill "$qpid" 2>/dev/null || :
rc=0
wait "$qpid" || rc=$?
qpid=
# QEMU's status tells nothing once the host has stopped it.
[ $status -eq 0 ] || rc=1
kill "$lsn" 2>/dev/null || :
wait "$lsn" 2>/dev/null || :
lsn=

for f in PKTDRV.OUT PING.OUT FINGER.OUT; do
    mtype -i "$vm/B.IMG" "::$f" >"$vm/$f" 2>/dev/null || :
done
for f in PKTDRV.OUT COM1.OUT PING.OUT FINGER.OUT QEMU.ERR; do
    echo "===== $f"
    tr -d '\r' <"$vm/$f"
done

case $rc in
    1) ;;
    3) echo "$prog: TCPTEST failed" >&2; status=1 ;;
    124) echo "$prog: VM timed out after $(dos_secs_said "$TIMEOUT")" >&2
         status=1 ;;
    *) echo "$prog: unexpected QEMU exit status $rc" >&2; status=1 ;;
esac
# The events, a slot (0 to TS_SLOTS - 1) and the peer's port masked.
tr -d '\r' <"$vm/COM1.OUT" |
    sed -E 's/^TCPTEST [0-3] /TCPTEST n /; s/:[0-9]+$//' >"$vm/EVENTS"
printf 'TCPTEST %s\n' 'LISTEN 7000' 'n OPEN 10.0.2.2' 'n OPEN 10.0.2.2' \
    'n OPEN 10.0.2.2' 'n OPEN 10.0.2.2' 'n CLOSE' 'n OPEN 10.0.2.2' \
    'n CLOSE' 'n CLOSE' QUIT PASS \
    >"$vm/EVENTS.EXP"
if ! cmp -s "$vm/EVENTS.EXP" "$vm/EVENTS"; then
    echo "$prog: TCPTEST's events differ from those expected:" >&2
    diff "$vm/EVENTS.EXP" "$vm/EVENTS" >&2 || :
    status=1
fi
if tr -d '\r' <"$vm/PING.OUT" | grep -Eq '^Received *: *4 *$'; then
    echo "$prog: PING after TCPTEST OK (4 of 4)"
else
    echo "$prog: PING after TCPTEST did not get 4 replies" >&2; status=1
fi
if [ -f "$vm/finger.req" ] && tr -d '\r' <"$vm/finger.req" | grep -qxF probe &&
    tr -d '\r' <"$vm/FINGER.OUT" | grep -qxF "$probe"; then
    echo "$prog: TCP finger of 10.0.2.100 via host port $fport OK"
else
    echo "$prog: FINGER exchange with the host listener failed" >&2
    [ ! -f "$vm/finger.req" ] || od -c "$vm/finger.req" | head -n 3 >&2
    status=1
fi

if [ $status -eq 0 ]; then
    echo "$prog: TCPTEST on MS-DOS $dos from $exedir OK"
    [ "$KEEP_VM" = 1 ] || rm -rf "$vm"
fi
[ ! -d "$vm" ] || echo "$prog: scratch files kept in $vm"
exit $status
