#!/bin/sh
# wintcp.sh - the Winsock tests TEST/WSOCKT.C and TEST/WSKT.C and
# INVWIN's TCP link on the Windows for Workgroups 3.11 test machine,
# through INVTCP, the TCP bridge, into the RETROINV server, between two
# MS-DOS 3.30 QEMU VMs.
#
# usage: sh TEST/INTEG/wintcp.sh [-u] [EXEDIR]
#
#   EXEDIR  directory of one build's INVSERV.EXE and INVTCP.EXE (default
#           BIN, the Open Watcom build, made first); the message files
#           are the host-built BIN/INVSERV.MSG and BIN/INV.MSG, and the
#           Windows programs Open Watcom's BIN/WIN/WSOCKT.EXE,
#           BIN/WIN/WSKT.EXE and BIN/WIN/INVWIN.EXE, all made first, as
#           is the host INV that seeds the data
#   -u      rewrite the golden files from this run instead of comparing
#           them; review the difference before keeping them
#
# The NetBIOS image (mkimg.sh) and the Windows for Workgroups machine's
# disk (TEST/WIN/mkwin.sh -w wfw311) must be current.
#
# One run of run2vm.sh with the VMs' monitors open.  VM A runs, from D:\,
# "INVSERV /CFG:SRV.CFG" on the data directory D:\DATA, which the host INV
# seeds from TEST/KEYS/SEED.REQ, its event log D:\SRV.LOG; its COM1, which
# SRV.CFG serves, is joined to VM B's (SERIAL=1 of run2vm.sh) and carries
# nothing.  VM B has a second card, an NE2000 at the port and interrupt of
# run2vm.sh's NIC2_B, but on a private Ethernet segment rather than QEMU's
# user network: a QEMU "stream" netdev that VM B's QEMU serves on
# 127.0.0.1, at the port of a port slot of this script's own (port_slot of
# TEST/shlib.sh), and the Windows machine's QEMU joins.  VM B loads the
# packet driver on it and runs "INVTCP RISRV", its event lines on COM2,
# which QEMU writes to a host file as they come; its address is 10.0.2.15
# (WATTCP.CFG), the Windows machine's 10.0.2.16 (TEST/WIN/WFW/SYSTEM.INI).
# VM B's QEMU also writes every frame of the segment into a capture
# (filter-dump).  Once the server's status screen is up and INVTCP has
# said START, the Windows machine boots (TEST/WIN/runwin.sh -w wfw311,
# its monitor open) and starts Windows in 386 enhanced mode for each
# program in turn:
#   - WSOCKT with VM B's address and BR_PORT (SRC/TCP/BRIDGE.H), which
#     sends HELLO, VER and BYE and reads the answers;
#   - INVWIN, beside INV.MSG, with "/TCP:" VM B's address, its port the
#     default, and the key script TEST/WIN/INVLINK.KEY, then
#     TEST/WIN/INVENDQ.KEY, which ends Windows' session while the link
#     is open;
#   - INVWIN with the key script TEST/WIN/INVFAIL.KEY, "/TCP:" the port
#     after BR_PORT, where nothing listens, then the address CANCEL,
#     where no station answers: as soon as the capture holds its ARP
#     request for CANCEL (TEST/INTEG/ARPWAIT.PY), so that INVWIN waits
#     for that connection, the host presses Esc through the monitor;
#   - WSKT with VM B's address and SILENT (TEST/WSKT.C).
# The host then presses F3 and, once the question is on the screen, Y on
# VM B and on VM A, or, when anything failed, ends both VMs.
#
# The host requires exit status 0 of INVSERV and INVTCP and of the Windows
# run; WSOCKT.OUT, the version in the answer to VER masked, followed by
# INVTCP's event lines, the time, a connection's slot and the peer's port
# masked and the lines sorted, equal to TEST/GOLDEN/WINTCP.TXT, each
# connection closed within CLOSE_SECS of its OPEN; INVWIN's snapshots,
# their =STACK UNUSED= line left out and held to STACK_MARGIN of
# TEST/T_SCRH.C (snap_check of TEST/shlib.sh), equal to
# TEST/GOLDEN/WIN/INVTCP.SNP (the session), INVTCPQ.SNP (the session's
# end), INVTCPR.SNP (the refusal) and INVTCPC.SNP (the wait Esc ended); WSKT's
# report WSKT.OUT saying "WSKT PASS" and "WSKT EXIT 0"; and every line of the
# server's event log well formed, with one START, STACK and STOP 0 event
# (log_check of vmgate.sh).  The VMs' clocks start at the fixed time CLOCK.
#
# On failure every program's output is printed, and the result directory
# is kept as OUT.fail.<timestamp>.
#
# env: TIMEOUT  the Windows run's limit in seconds (default 300), times
#               DOSTIME_SCALE (dos_secs of TEST/shlib.sh); each VM's is
#               that plus the waits around the run (VM_EXTRA)
#      OUT      result directory (default TEST/INTEG/vm/wintcp.run); it
#               must be new or an earlier OUT
#      PKTDRV   directory holding NE2000.COM (default $RETRO_DL/pktd)
#      PORT_A, PCAP, NET330_DIR  as run2vm.sh takes them; WFWIMG_DIR as
#               TEST/WIN/winlib.sh takes it
#
# Exit status: 0 when every check passed; 1 on failure; 2 on a setup
# error, run2vm.sh's included; 129, 130 or 143 when stopped by SIGHUP,
# SIGINT or SIGTERM (run2vm.sh kills its VMs first).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/net330.sh"
. "$here/vmgate.sh"

CLOCK=$SEED_CLOCK
# The same time for the seeding, as TEST/T_SCR.SH seeds.
NOW=/NOW:$SEED_NOW
SERVER=RISRV                    # the server's NetBIOS name
KEYS=$repo/TEST/WIN/INVLINK.KEY
FAILKEYS=$repo/TEST/WIN/INVFAIL.KEY
ENDKEYS=$repo/TEST/WIN/INVENDQ.KEY
# Addresses of the segment no station has: INVWIN's wait for the first,
# which the host ends with Esc, and WSKT's for the second, which runs out.
CANCEL=10.0.2.99
SILENT=10.0.2.98
# The longest a connection of the run may last, seconds.
CLOSE_SECS=40
# The waits around the Windows run, seconds before DOSTIME_SCALE: for the
# server's status screen, for INVTCP's START event and, at the end, for
# the server to count its sessions down to none.  A VM's limit exceeds
# the Windows run's by all three, so that the VMs outlast Windows' own.
SRV_UP=240
TCP_UP=60
SRV_DOWN=30
VM_EXTRA=$((SRV_UP + TCP_UP + SRV_DOWN))
usage="usage: wintcp.sh [-u] [EXEDIR]"
update=0
while [ $# -gt 0 ]; do
    case $1 in
        -u) update=1; shift ;;
        -*) die "$usage" ;;
        *) break ;;
    esac
done
[ $# -le 1 ] || die "$usage"
exedir=${1:-BIN}
TIMEOUT=${TIMEOUT:-300}
is_uint "$TIMEOUT" && [ "$TIMEOUT" -gt 0 ] ||
    die "TIMEOUT must be a positive integer"
OUT=${OUT:-"$here/vm/wintcp.run"}
PKTDRV=${PKTDRV:-$RETRO_DL/pktd}
gwin="$repo/TEST/GOLDEN/WIN"
# The stack bytes a snapshot of INVWIN must leave unused (snap_check).
margin=$(cdefine "$here/../T_SCRH.C" STACK_MARGIN)
# VM B's NE2000 on the private segment, as run2vm.sh's NIC2_B places it.
nic=ne2k_isa,iobase=0x320,irq=11,mac=52:54:00:52:49:1b
# The label of the count of sessions on the server's status screen.
opened='Sessions open       '

OUT=$(out_check "$OUT" .run2vm) || exit 2
export OUT

need_tools python3 mktemp
progs="BIN/QEXIT.COM BIN/INVSERV.MSG BIN/INV.MSG BIN/WIN/WSOCKT.EXE"
progs="$progs BIN/WIN/INVWIN.EXE BIN/WIN/WSKT.EXE OBJ/HOST/INV.bin"
[ "$exedir" != BIN ] || progs="$progs BIN/INVSERV.EXE BIN/INVTCP.EXE"
make -s -C "$repo" $progs || die "the programs could not be built"
case $exedir in /*) ;; *) exedir="$repo/$exedir" ;; esac
for f in INVSERV.EXE INVTCP.EXE; do
    [ -f "$exedir/$f" ] || die "not found: $exedir/$f"
done
ne2000=$(find_ci "$PKTDRV" NE2000.COM)
addr=$(tr -d '\r' <"$here/WATTCP.CFG" | sed -n 's/^my_ip=//p')
port=$(cdefine "$here/../../SRC/TCP/BRIDGE.H" BR_PORT)
[ -n "$addr" ] || die "WATTCP.CFG gives no my_ip"
port_slot
link=$((PORT_BASE + 2 * PORT_SLOTS + sl_slot))

gate_start wintcp

a="$payload/A"
b="$payload/B"
w="$payload/W"
srv_files "$a" "$exedir"
seed=$payload/seed.log
dos_text "$repo/TEST/KEYS/SEED.REQ" |
    (cd "$a" && "$repo/OBJ/HOST/INV.bin" /CMD /LOCAL DATA $NOW) \
        >"$seed" 2>&1 || die "the seeding failed: $(cat "$seed")"
! grep -q '^-ERR' "$seed" ||
    die "a seed request was refused: $(grep -m 1 '^-ERR' "$seed")"
mkdir -p "$b" "$w"
cp "$exedir/INVTCP.EXE" "$ne2000" "$b/"
cp "$repo/BIN/WIN/WSOCKT.EXE" "$repo/BIN/WIN/INVWIN.EXE" "$repo/BIN/INV.MSG" \
    "$repo/BIN/WIN/WSKT.EXE" "$KEYS" "$FAILKEYS" "$ENDKEYS" "$w/"
k=$(basename "$KEYS")
fk=$(basename "$FAILKEYS")
ek=$(basename "$ENDKEYS")
# INVWIN's snapshots by their goldens: TEST/GOLDEN/WIN/<golden> holds
# what the run writes as <snapshot>.
snaps="INVTCP.SNP:${k%.*}.SNP INVTCPQ.SNP:${ek%.*}.SNP"
snaps="$snaps INVTCPR.SNP:REFUSED.SNP INVTCPC.SNP:CANCEL.SNP"
dos_text <"$here/WATTCP.CFG" >"$b/WATTCP.CFG"
{
    printf '@ECHO OFF\r\nINVSERV /CFG:SRV.CFG\r\n'
    rc_to SRV.RC
} >"$a/SRVA.BAT"
{
    printf '@ECHO OFF\r\nNE2000 0x60 11 0x320 > PKTDRV.OUT\r\n'
    printf 'INVTCP %s > COM2\r\n' "$SERVER"
    rc_to TCP.RC
} >"$b/TCPB.BAT"

# INVTCP's event lines, on VM B's COM2 (the port after run2vm.sh's COM1).
EV="$payload/COM2.OUT"
# The segment: VM B's QEMU serves it, the Windows machine's joins it.
seg=stream,addr.type=inet,addr.host=127.0.0.1,addr.port=$link
extra_b="-serial file:$EV -netdev $seg,id=nw,server=on"
extra_b="$extra_b -device $nic,netdev=nw"
# Every frame of the segment, for the host to see INVWIN's ARP request.
pcap="$payload/SEG.PCAP"
extra_b="$extra_b -object filter-dump,id=fd,netdev=nw,file=$pcap"
winmon="$mondir/W.mon"
escpid=
rc=0
TIMEOUT=$((TIMEOUT + VM_EXTRA)) MONDIR=$mondir SERIAL=1 \
    QEMU_EXTRA="-rtc base=$CLOCK" QEMU_EXTRA_B=$extra_b \
    sh "$here/run2vm.sh" "$a" 'C:\COMMAND /C SRVA.BAT' \
                         "$b" 'C:\COMMAND /C TCPB.BAT' &
runpid=$!
if qmon A "wait:$opened:$(dos_secs $SRV_UP)" &&
   wait_for ' 0 START ' "$EV" "$(dos_secs $TCP_UP)"; then
    # Esc once INVWIN asks the segment for CANCEL, so that it is waiting
    # for that connection; timeout's process group goes with its kill.
    secs=$(dos_secs "$TIMEOUT")
    timeout "$secs" sh -c 'python3 -I "$1" "$2" "$3" "$4" &&
        python3 -I "$5" "$6" key:esc' - "$here/ARPWAIT.PY" "$pcap" \
        "$CANCEL" "$secs" "$here/qmon.py" "$winmon" >/dev/null 2>&1 &
    escpid=$!
    sh "$repo/TEST/WIN/runwin.sh" -w wfw311 -m 3 -t "$TIMEOUT" \
        -n "$seg,server=off,reconnect-ms=200" -p "$winmon" "$w" \
        "WSOCKT $addr $port" \
        "INVWIN /TCP:$addr /KEYS:$k /SNAP:${k%.*}.SNP" \
        "INVWIN /TCP:$addr /KEYS:$ek /SNAP:${ek%.*}.SNP" \
        "INVWIN /TCP:$addr:$((port + 1)) /KEYS:$fk /SNAP:REFUSED.SNP" \
        "INVWIN /TCP:$CANCEL /KEYS:$fk /SNAP:CANCEL.SNP" \
        "WSKT /TCP:$addr /TCP:$SILENT" &
    qpid=$!
    wait "$qpid" || status=1
    qpid=
    kill "$escpid" 2>/dev/null || :
    wait "$escpid" 2>/dev/null || :
else
    echo "$prog: the server or INVTCP did not start" >&2
    status=1
fi
if [ "$status" -eq 0 ]; then
    qmon B key:f3 "wait:Stop the bridge?:$(dos_secs 30)" key:y &&
        qmon A "wait:${opened}0 :$(dos_secs $SRV_DOWN)" key:f3 \
               "wait:Sessions open: 0.:$(dos_secs 30)" key:y || quit_vms
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
expect "$db/TCP.RC" 0

# The report, the server's version masked, then the events, the time, a
# connection's slot (1 to TS_SLOTS) and the peer's port masked, sorted.
stamp='[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}'
{
    tr -d '\r' 2>/dev/null <"$w/WSOCKT.OUT" |
        sed 's/^\(WSOCKT RECV +OK RETROINV\) .*/\1 */' || :
    tr -d '\r' <"$EV" |
        sed -E -e "s/^$stamp [1-4] /T n /" -e "s/^$stamp 0 /T 0 /" \
            -e 's/:[0-9]+$//' | LC_ALL=C sort
} >"$payload/WINTCP.TXT"
golden "$payload/WINTCP.TXT" WINTCP.TXT "the result"
golden_done 1
if [ "$update" -eq 1 ]; then
    for s in $snaps; do
        snap_check "$gwin/${s%%:*}" "$w/${s#*:}" "$margin" || :
        cp "$w/${s#*:}.CMP" "$gwin/${s%%:*}"
    done
    echo "$prog: rewrote INVWIN's snapshots"
else
    for s in $snaps; do
        snap_check "$gwin/${s%%:*}" "$w/${s#*:}" "$margin" || status=1
    done
fi
wk=$(tr -d '\r' <"$w/WSKT.OUT" 2>/dev/null || :)
printf '%s\n' "$wk" | grep -q '^WSKT PASS ' &&
    printf '%s\n' "$wk" | grep -qx 'WSKT EXIT 0' || {
    echo "$prog: WSKT failed" >&2
    status=1
}

log_check "$da/SRV.LOG" 1

# Each connection ends within CLOSE_SECS of its OPEN, so no close of
# INVWIN's waited (INVENDQ.KEY's at WM_ENDSESSION among them).
late=$(tr -d '\r' <"$EV" | awk -v max="$(dos_secs "$CLOSE_SECS")" '
    { split($2, t, ":"); s = t[1] * 3600 + t[2] * 60 + t[3] }
    $4 == "OPEN" { open[$3] = s }
    $4 == "CLOSE" && ($3 in open) && s - open[$3] > max {
        print $3 " " s - open[$3]
    }')
if [ -n "$late" ]; then
    echo "$prog: a connection lasted over $CLOSE_SECS s (slot, s): $late" >&2
    status=1
fi

[ "$status" -eq 0 ] || cp "$EV" "$payload/WINTCP.TXT" "$OUT/" 2>/dev/null || :
gate_end "$da/SRV.LOG" "$EV" "$w/WSOCKT.OUT" "$w/WSKT.OUT" \
    "$w/${k%.*}.SNP" "$w/${ek%.*}.SNP" "$w/REFUSED.SNP" "$w/CANCEL.SNP" \
    "$db/PKTDRV.OUT"
