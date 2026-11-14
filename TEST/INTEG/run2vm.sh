#!/bin/sh
# run2vm.sh - run a command in one or two networked MS-DOS QEMU VMs.
#
# usage: sh TEST/INTEG/run2vm.sh DIR_A CMD_A [DIR_B CMD_B]
#
#   DIR_x  host directory whose files are copied to the root of the VM's
#          drive D: ("-" for none); names must be 8.3 DOS names, in either
#          case (they are upper-cased)
#   CMD_x  DOS command line run from D:\ with standard output redirected to
#          D:\OUT.TXT.  A batch file needs "C:\COMMAND /C X.BAT" (COMMAND.COM
#          then always reports errorlevel 0).  The command must return to
#          DOS; a program that ends the VM itself skips the completion marker
#          below and the run is reported as incomplete.
#
# Each VM boots its own copy-on-write overlay of its image's BASE.IMG as C:
# plus a fresh copy of that image's blank payload disk as D:.  IMAGE_A and
# IMAGE_B choose the image of each VM:
#   net330  MS-DOS 3.30 + MS Network Client 3.0, a NetBEUI workstation
#           (mkimg.sh, net330.sh; SYSTEM.INI of this directory)
#   wga500  MS-DOS 5.00 + Workgroup Add-On for MS-DOS, a NetBEUI peer
#           server whose NET START also starts the server (mkwga.sh,
#           wga500.sh; WGA/SYSTEM.INI)
# The harness writes D:\RUN.BAT, which C:\AUTOEXEC.BAT starts after
# loading SHARE.  RUN.BAT installs the VM's SYSTEM.INI (its image's template
# with its own computer name) and PROTOCOL.INI (its own NetBEUI timer T1),
# runs NET START with output in D:\NETSTART.OUT, records CHKDSK C: (free
# conventional memory with the network loaded) in D:\CHKDSK.OUT and runs
# CMD_x.  It then writes the command's errorlevel (0..127, larger values
# read as 127; the ladder of errlevel_bat in TEST/shlib.sh) to D:\DONE.TXT
# and passes it to QEXIT, which ends QEMU with host status errorlevel*2+1.
# QEMU also exits with status 1 when it fails to start, so a run only counts
# when D:\DONE.TXT exists and agrees with the QEMU status.  Unless NETSTART
# is empty, a VM whose NET START does not report success fails too, with
# NET START's Error line printed.
#
# Afterwards the whole of D: is extracted to OUT/A/D and OUT/B/D, and
# NETSTART.OUT and OUT.TXT of each VM are printed.
#
# Each VM has an AMD PCnet card (QEMU "pcnet") in PCI slot 5, where
# SeaBIOS gives it I/O C000h and IRQ 10, the values PROTOCOL.INI passes to
# Microsoft's AM2100.DOS.  AUTOEXEC.BAT turns on its bus mastering first
# and records its PCI state in C:\NET\PCNET.OUT; RUN.BAT copies that to D:,
# and a VM whose card is missing or set up otherwise fails with the cause.
# The two cards are joined into a private Ethernet segment that MS Client
# NetBEUI runs over.  By default VM A's QEMU listens on 127.0.0.1:PORT_A
# and VM B's connects to it (QEMU "stream" netdev, TCP, reconnecting until
# A is up), which needs no host configuration.  With RELAY=1 or RELAY=delay,
# VM B connects instead to the relay LOSSY.PY on 127.0.0.1:PORT_B, which
# passes the frames on to VM A: with RELAY=1 it loses those that deadlock
# NetBEUI under equal response timers and logs each loss in OUT/RELAY.LOG;
# with RELAY=delay it loses none and passes each RELAY_MS milliseconds
# late, which keeps a station's answer to a poll from coming back before
# the station has dealt with its own poll: under QEMU, NetBEUI sometimes
# ignores an answer that comes back that soon.  LOSSY.PY creates
# OUT/RELAY.LOG once it has joined VM B to VM A, so a run with a relay
# whose RELAY.LOG is missing fails, after each VM's own cause: the relay
# never joined VM B to VM A.  (QEMU's NE2000 cannot carry NetBEUI:
# Microsoft's NE2000.DOS sends an idle frame with a byte count of 0,
# after which QEMU 10.0's netdev takes no further frame from that card.)
# The datagram transports are options:
#   NETMODE=dgram  each VM sends UDP datagrams to the other's port
#   NETMODE=mcast  a UDP multicast group (QEMU "socket,mcast"); needs a
#                  multicast-capable interface for 127.0.0.1, which a stock
#                  Linux loopback is not ("ip link set lo multicast on")
#
# With NIC2_B=1, VM B also has an NE2000 (QEMU "ne2k_isa", I/O 320h, IRQ
# 11) on QEMU's user-mode network, where the host is 10.0.2.2, and the
# host's 127.0.0.1:PORT_T is forwarded to the guest's TCP port 7000.  The
# Crynwr packet driver NE2000.COM is copied from PKTDRV to VM B's D:, and
# CMD_B must load it after NET START with "NE2000 0x60 11 0x320" (software
# interrupt 60h).  QEMU accepts every host connection to PORT_T at once and
# closes it without data when nothing in the guest listens on port 7000:
# within seconds when a WATTCP program answers with a reset, after about
# 75 s (QEMU's SYN retries) when no TCP program runs in the guest.
#
# With SERIAL=1 the VMs' COM1 ports (3F8h, IRQ 4) are joined: VM A's QEMU
# listens on a unix socket in a private directory of its own, which VM B's
# connects to (reconnecting until A is up), so that what one guest sends
# on COM1 the other receives.  COM2_A then also gives VM A a COM2 (2F8h,
# IRQ 3) on a unix socket at that path, on which QEMU listens for a host
# program; bytes VM A sends while no program is connected are lost.
# Without SERIAL=1 neither VM has a COM1, and the BIOS lists a port that
# QEMU_EXTRA_B adds, at 2F8h with IRQ 3, as COM1; with it, as COM2.
#
# Runs share the images: a run holds the lock of each image it boots
# shared (image_lock of TEST/INTEG/net330.sh), so it waits for a build of
# that image and a build waits for it.  Only one run at a time uses an
# OUT; a second one exits with status 2 while OUT.lock is held.  Runs
# take different ports: unless PORT_A is given, a run holds the first of
# the PORT_SLOTS lock files slot<n> in this user's PORT_LOCKS that no
# other run holds, for as long as it or a QEMU it started lives, and uses
# the ports PORT_BASE + 2n and PORT_BASE + 2n + 1, and for PORT_T the port
# PORT_BASE + 2 * PORT_SLOTS + n (the band up to PORT_BASE + 3 *
# PORT_SLOTS - 1, above every slot's pair).  OUT/PORTS holds the run's
# ports as lines PORT_A=, PORT_B= and, with NIC2_B=1, PORT_T=; it exists
# before the VMs start, and a caller that waits for it removes an earlier
# run's OUT/PORTS first.
#
# env: TIMEOUT   seconds before a VM is killed (default 300), times
#                DOSTIME_SCALE (dos_secs of TEST/shlib.sh)
#      DELAY_B   seconds between starting VM A and VM B (default 0)
#      NETMODE   stream (default), dgram or mcast
#      PORT_A    the TCP port VM A listens on in stream mode, and VM A's
#                UDP port in dgram mode (default the slot's first port);
#                a PORT_A that is given takes no slot, so keep PORT_A to
#                PORT_A + 2 out of the slots' bands, or give the ports of
#                a slot the caller holds (winnb.sh, winmbx.sh)
#      PORT_B    VM B's UDP port in dgram mode, the relay's port with RELAY
#                (default PORT_A + 1)
#      PORT_T    the host port forwarded to VM B's port 7000 with NIC2_B=1
#                (default the slot's, or PORT_A + 2 when PORT_A is given)
#      PORT_LOCKS  this user's directory of the port slots' lock files
#                (default ${TMPDIR:-/tmp}/retroinv-ports-<uid>); it must
#                be the user's own directory, not a symbolic link, and
#                writable by no one else
#      RELAY     1 or delay to pass VM B's stream link through LOSSY.PY,
#                losing frames or delaying them (default 0, no relay)
#      RELAY_MS  the delay of RELAY=delay, in whole milliseconds (default 10)
#      NIC2_B    1 to give VM B the NE2000 above (default 0)
#      SERIAL    1 to join the VMs' COM1 ports as above (default 0)
#      COM2_A    the unix socket of VM A's COM2 with SERIAL=1 (default
#                none): a path of at most 107 bytes, without commas
#      PKTDRV    directory holding NE2000.COM (default $RETRO_DL/pktd)
#      RETRO_DL  root of the period downloads (default $HOME/retro-dl)
#      MCAST     multicast group:port in mcast mode (230.0.0.1:PORT_A)
#      NAME_A, NAME_B  NetBIOS computer names, 1-15 of A-Z 0-9 (RIVMA, RIVMB)
#      T1_A, T1_B      NetBEUI response timers in milliseconds (NB_T1_A and
#                      NB_T1_B of net330.sh), unequal so that after a loss
#                      the two stations' polls do not cross
#      MAC_A, MAC_B    PCnet MAC addresses (52:54:00:52:49:0a / :0b)
#      NETSTART  command that starts the network (default "C:\NET\NET START";
#                empty to leave the network unloaded)
#      QEMU_EXTRA  extra QEMU options for every VM (split on spaces)
#      QEMU_EXTRA_B  extra QEMU options for VM B only (split on spaces)
#      PCAP      1 to record each VM's Ethernet traffic in OUT/X/net.pcap
#                (QEMU filter-dump)
#      MONDIR    directory for QEMU monitor sockets MONDIR/A.sock, B.sock
#                (unset: no monitor); keep it short, a socket path is
#                limited to 108 bytes
#      OUT       work and result directory (default TEST/INTEG/vm/run2vm);
#                emptied first, so it must be new or an earlier OUT
#      IMAGE_A, IMAGE_B  each VM's image, net330 or wga500 (default net330)
#      NET330_DIR, WGA500_DIR  the images' directories (see net330.sh and
#                      wga500.sh)
#
# Exit status: 0 when every VM ran CMD_x and it returned errorlevel 0;
# 1 when a VM failed, timed out or a command returned non-zero, or the
# relay never joined the VMs; 2 on usage or setup errors; 129/130/143 when
# stopped by SIGHUP/SIGINT/SIGTERM (the VMs are killed first).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/wga500.sh"

[ $# -eq 2 ] || [ $# -eq 4 ] || die "usage: run2vm.sh DIR_A CMD_A [DIR_B CMD_B]"

TIMEOUT=${TIMEOUT:-300}
DELAY_B=${DELAY_B:-0}
NETMODE=${NETMODE:-stream}
PORT_A=${PORT_A:-}
PORT_B=${PORT_B:-}
PORT_T=${PORT_T:-}
RELAY=${RELAY:-0}
RELAY_MS=${RELAY_MS:-10}
NIC2_B=${NIC2_B:-0}
SERIAL=${SERIAL:-0}
COM2_A=${COM2_A:-}
PKTDRV=${PKTDRV:-$RETRO_DL/pktd}
MCAST=${MCAST:-}
NAME_A=${NAME_A:-RIVMA}
NAME_B=${NAME_B:-RIVMB}
T1_A=${T1_A:-$NB_T1_A}
T1_B=${T1_B:-$NB_T1_B}
MAC_A=${MAC_A:-52:54:00:52:49:0a}
MAC_B=${MAC_B:-52:54:00:52:49:0b}
NETSTART=${NETSTART-'C:\NET\NET START'}
IMAGE_A=${IMAGE_A:-net330}
IMAGE_B=${IMAGE_B:-net330}
OUT=${OUT:-"$here/vm/run2vm"}

qexit="$repo/BIN/QEXIT.COM"
marker=.run2vm
# VM B's NE2000 with NIC2_B=1.
nic2_dev=ne2k_isa,iobase=0x320,irq=11,netdev=n1,mac=52:54:00:52:49:1b

# ini_value KEY - print the hexadecimal or decimal digits of KEY's value in
# PROTOCOL.INI (any case, blanks around "="; a leading 0x is dropped).
ini_value() {
    tr -d '\r' <"$here/PROTOCOL.INI" | tr '[:upper:]' '[:lower:]' |
        sed -n "s/^[[:blank:]]*$1[[:blank:]]*=[[:blank:]]*\(0x\)\{0,1\}\([0-9a-f]\{1,\}\).*/\2/p" |
        head -n 1
}
# The I/O base and IRQ PROTOCOL.INI gives AM2100.DOS, which pcnet_check
# expects of the card.
pcnet_io=$(ini_value ioaddress)
pcnet_irq=$(ini_value interrupt)
[ -n "$pcnet_io" ] && is_uint "$pcnet_irq" ||
    die "PROTOCOL.INI lacks IOADDRESS=0x<hex> or INTERRUPT=<n>"

is_uint "$TIMEOUT" && [ "$TIMEOUT" -gt 0 ] || die "TIMEOUT must be a positive integer"
limit=$(dos_secs "$TIMEOUT")
is_uint "$DELAY_B" || die "DELAY_B must be a non-negative integer"
for p in "$PORT_A" "$PORT_B" "$PORT_T"; do
    [ -z "$p" ] || is_uint "$p" ||
        die "PORT_A, PORT_B and PORT_T must be integers"
done
for t in "$T1_A" "$T1_B"; do
    is_uint "$t" && [ "$t" -ge 1 ] && [ "$t" -le 60000 ] ||
        die "T1_A and T1_B must be milliseconds from 1 to 60000"
done
for n in "$NAME_A" "$NAME_B"; do
    printf '%s\n' "$n" | grep -Eq '^[A-Z0-9]{1,15}$' || die "bad computer name: $n"
done
for i in "$IMAGE_A" "$IMAGE_B"; do
    case $i in net330|wga500) ;; *) die "IMAGE_A and IMAGE_B must be net330 or wga500" ;; esac
done
case $NETMODE in stream|dgram|mcast) ;; *) die "NETMODE must be stream, dgram or mcast" ;; esac
# The relay's options: none for RELAY=1, the delay for RELAY=delay.
relay_opts=
case $RELAY in
    0) ;;
    1|delay)
       [ "$NETMODE" = stream ] || die "RELAY=$RELAY needs NETMODE=stream"
       [ $# -eq 4 ] || die "RELAY=$RELAY needs two VMs"
       need_tools python3 ;;
    *) die "RELAY must be 0, 1 or delay" ;;
esac
if [ "$RELAY" = delay ]; then
    is_uint "$RELAY_MS" || die "RELAY_MS must be a non-negative integer"
    relay_opts="--delay $RELAY_MS"
fi
case $NIC2_B in
    0) ;;
    1) [ $# -eq 4 ] || die "NIC2_B=1 needs two VMs"
       ne2000=$(find_ci "$PKTDRV" NE2000.COM) ;;
    *) die "NIC2_B must be 0 or 1" ;;
esac

case $SERIAL in
    0) [ -z "$COM2_A" ] || die "COM2_A needs SERIAL=1" ;;
    1) [ $# -eq 4 ] || die "SERIAL=1 needs two VMs"
       case $COM2_A in *,*) die "COM2_A must not hold a comma" ;; esac
       [ ${#COM2_A} -le 107 ] || die "COM2_A is longer than 107 bytes" ;;
    *) die "SERIAL must be 0 or 1" ;;
esac

need_tools qemu-system-i386 qemu-img mcopy mdir mtype timeout flock make sha256sum
images=$IMAGE_A
[ $# -eq 2 ] || [ "$IMAGE_B" = "$IMAGE_A" ] || images="$images $IMAGE_B"
make -s -C "$repo" BIN/QEXIT.COM || die "BIN/QEXIT.COM could not be built"
# The lock of each image the VMs boot, shared (image_lock of net330.sh).
case " $images " in *" wga500 "*)
    image_lock 8 s "$WGA500_DIR"
    wga_current || die "no current image in $WGA500_DIR; run mkwga.sh" ;;
esac
case " $images " in *" net330 "*)
    image_lock 9 s "$NET330_DIR"
    image_current || die "no current image in $NET330_DIR; run mkimg.sh" ;;
esac

# The port slot (port_slot of TEST/shlib.sh).
if [ -z "$PORT_A" ]; then
    port_slot
    PORT_A=$((PORT_BASE + 2 * sl_slot))
    PORT_T=${PORT_T:-$((PORT_BASE + 2 * PORT_SLOTS + sl_slot))}
fi
PORT_B=${PORT_B:-$((PORT_A + 1))}
PORT_T=${PORT_T:-$((PORT_A + 2))}
# VM B's user-mode network with NIC2_B=1: the host's PORT_T leads to the
# guest's TCP port 7000.
nic2_net=user,id=n1,hostfwd=tcp:127.0.0.1:$PORT_T-:7000
MCAST=${MCAST:-230.0.0.1:$PORT_A}

# OUT is removed and recreated; refuse anything that is not an earlier OUT.
OUT=$(out_check "$OUT" "$marker") || exit 2
mkdir -p "$(dirname "$OUT")"
exec 7>>"$OUT.lock"
flock -n 7 || die "another run2vm.sh is running in $OUT"

# bad_83 DIR - print the first path below DIR whose name, upper-cased, is
# not an 8.3 name (is_83); print nothing when every name is.
bad_83() {
    find "$1" -mindepth 1 | while IFS= read -r p; do
        is_83 "$(basename "$p" | tr '[:lower:]' '[:upper:]')" ||
            { printf '%s\n' "$p"; break; }
    done
}

# prepare X DIR CMD NAME T1 IMAGE - build OUT/X with the overlay and payload
# disk of IMAGE.
prepare() {
    v="$OUT/$1"
    case $6 in
        net330) base=$BASE_IMG payload=$PAYLOAD_TEMPLATE sysini="$here/SYSTEM.INI" ;;
        wga500) base=$WGA_BASE_IMG payload=$WGA_PAYLOAD sysini="$here/WGA/SYSTEM.INI" ;;
    esac
    mkdir -p "$v"
    qemu-img create -q -f qcow2 -b "$base" -F raw "$v/C.QCOW2"
    cp --sparse=always "$payload" "$v/D.IMG"
    chmod u+w "$v/D.IMG"
    d=$(part_img "$v/D.IMG")
    if [ "$2" != "-" ]; then
        [ -d "$2" ] || die "not a directory: $2"
        bad=$(bad_83 "$2")
        [ -z "$bad" ] || die "payload names must be 8.3: $bad"
        for f in "$2"/*; do
            [ -e "$f" ] || continue
            mcopy -s -o -i "$d" "$f" "::$(basename "$f" | tr '[:lower:]' '[:upper:]')"
        done
    fi
    sed "s/@COMPUTERNAME@/$4/g" "$sysini" >"$v/SYSTEM.TPL"
    dos_text "$v/SYSTEM.TPL" >"$v/SYSTEM.INI"
    sed "s/@T1@/$5/g" "$here/PROTOCOL.INI" >"$v/PROTOCOL.TPL"
    dos_text "$v/PROTOCOL.TPL" >"$v/PROTOCOL.INI"
    {
        echo '@ECHO OFF'
        echo 'COPY D:\SYSTEM.INI C:\NET\SYSTEM.INI > NUL'
        echo 'COPY D:\PROTOCOL.INI C:\NET\PROTOCOL.INI > NUL'
        echo 'COPY C:\NET\PCNET.OUT D:\ > NUL'
        [ -z "$NETSTART" ] || printf '%s > D:\\NETSTART.OUT\n' "$NETSTART"
        echo 'C:\DOS\CHKDSK C: > D:\CHKDSK.OUT'
        echo 'D:'
        echo 'CD \'
        printf '%s > D:\\OUT.TXT\n' "$3"
        errlevel_bat 127
        echo 'ECHO %RC%> D:\DONE.TXT'
        echo 'D:\QEXIT %RC%'
    } | dos_text >"$v/RUN.BAT"
    mcopy -o -i "$d" "$v/RUN.BAT" ::RUN.BAT
    mcopy -o -i "$d" "$v/SYSTEM.INI" ::SYSTEM.INI
    mcopy -o -i "$d" "$v/PROTOCOL.INI" ::PROTOCOL.INI
    mcopy -o -i "$d" "$qexit" ::QEXIT.COM
    if [ "$1" = B ]; then
        [ "$NIC2_B" = 0 ] || mcopy -o -i "$d" "$ne2000" ::NE2000.COM
    fi
}

# start X MAC LOCAL_PORT REMOTE_PORT - boot VM X in the background under
# timeout; sets tpid_X to the timeout process.
start() {
    v="$OUT/$1"
    case $NETMODE in
        stream)
            netdev="stream,id=n0,addr.type=inet,addr.host=127.0.0.1"
            if [ "$1" = A ]; then
                netdev="$netdev,addr.port=$PORT_A,server=on"
            elif [ "$RELAY" != 0 ]; then
                netdev="$netdev,addr.port=$PORT_B,server=off,reconnect-ms=200"
            else
                netdev="$netdev,addr.port=$PORT_A,server=off,reconnect-ms=200"
            fi ;;
        dgram)
            netdev="dgram,id=n0,local.type=inet,local.host=127.0.0.1,local.port=$3"
            netdev="$netdev,remote.type=inet,remote.host=127.0.0.1,remote.port=$4" ;;
        mcast) netdev="socket,id=n0,mcast=$MCAST,localaddr=127.0.0.1" ;;
    esac
    pcap=
    [ "${PCAP:-0}" != 1 ] || pcap="-object filter-dump,id=f0,netdev=n0,file=$v/net.pcap"
    monitor=none
    [ -z "${MONDIR:-}" ] || monitor="unix:$MONDIR/$1.sock,server,nowait"
    # The first -serial is COM1 (3F8h, IRQ 4); "none" leaves it out, and
    # the BIOS then lists a later port (2F8h, IRQ 3) as COM1.
    serial="none"
    extra=
    if [ "$1" = A ]; then
        if [ "$SERIAL" = 1 ]; then
            serial="unix:$sdir/com1,server=on,wait=off"
            [ -z "$COM2_A" ] ||
                extra="-serial unix:$COM2_A,server=on,wait=off"
        fi
    else
        [ "$SERIAL" = 0 ] || serial="unix:$sdir/com1,reconnect-ms=200"
        [ "$NIC2_B" = 0 ] || extra="-netdev $nic2_net -device $nic2_dev"
        extra="$extra ${QEMU_EXTRA_B:-}"
    fi
    date +%s.%N >"$v/START"
    # -d nochain works around a QEMU bug in 16-bit near-call wrapping;
    # see boot310.sh.
    timeout "$limit" qemu-system-i386 -display none -no-reboot -m 4 \
        -accel "$QEMU_ACCEL" -d nochain -D /dev/null \
        -rtc base=localtime \
        -drive file="$v/C.QCOW2",if=none,id=hd0,format=qcow2 \
        -device "$(hd_device hd0 0)" \
        -drive file="$v/D.IMG",if=none,id=hd1,format=raw \
        -device "$(hd_device hd1 1)" \
        -boot c \
        -netdev "$netdev" \
        -device pcnet,addr=5,netdev=n0,mac="$2" \
        -device isa-debug-exit,iobase=0x501,iosize=0x01 \
        -serial "$serial" -parallel none -monitor "$monitor" \
        -pidfile "$v/qemu.pid" $pcap ${QEMU_EXTRA:-} $extra \
        >"$v/qemu.log" 2>&1 &
    eval "tpid_$1=\$!"
}

# collect X - wait for VM X and record its QEMU status in OUT/X/RC.
collect() {
    eval "p=\${tpid_$1:-}"
    [ -n "$p" ] || return 0
    set +e
    wait "$p"
    echo $? >"$OUT/$1/RC"
    set -e
    eval "tpid_$1="
}

# pcnet_check FILE - check the register dump that C:\NET\PCNET.SCR (the
# template PCNET.SCR of this directory, see img_config of imglib.sh) leaves
# in FILE: the card was found, has bus mastering on, and has the I/O base
# and interrupt line PROTOCOL.INI gives AM2100.DOS.  Print each mismatch
# and return 1 if there is one.  A carry set at the end comes from the
# search (B102h) or, with DI=3Ch, from the interrupt line read.
pcnet_check() {
    [ -f "$1" ] || { echo "no PCnet report (PCNET.OUT)"; return 1; }
    pc_regs=$(tr -d '\r' <"$1" | grep '^AX=' | tail -n 1)
    pc_flags=$(tr -d '\r' <"$1" | grep '^DS=' | tail -n 1)
    pc_cmd=$(printf '%s\n' "$pc_regs" | sed -n 's/.*DX=\([0-9A-F]\{4\}\).*/\1/p')
    pc_cx=$(printf '%s\n' "$pc_regs" | sed -n 's/.*CX=\([0-9A-F]\{4\}\).*/\1/p')
    pc_bar=$(printf '%s\n' "$pc_regs" | sed -n 's/.*SI=\([0-9A-F]\{4\}\).*/\1/p')
    case "$pc_flags " in
        *" CY "*)
            case $pc_regs in
                *DI=003C*) echo "PCI BIOS read of the PCnet interrupt line failed" ;;
                *) echo "PCnet not found on the PCI bus" ;;
            esac
            return 1 ;;
        *" NC "*) ;;
        *) echo "PCnet report unreadable"; return 1 ;;
    esac
    [ -n "$pc_cmd" ] && [ -n "$pc_cx" ] && [ -n "$pc_bar" ] ||
        { echo "PCnet report unreadable"; return 1; }
    pc_ok=0
    [ $((0x$pc_cmd & 4)) -ne 0 ] ||
        { echo "PCnet bus mastering off (command register $pc_cmd)"; pc_ok=1; }
    [ $((0x$pc_bar & 0xFFFC)) -eq $((0x$pcnet_io)) ] ||
        { echo "PCnet I/O base register $pc_bar, not $pcnet_io (PROTOCOL.INI)"; pc_ok=1; }
    [ $((0x${pc_cx#??})) -eq "$pcnet_irq" ] ||
        { echo "PCnet interrupt line $((0x${pc_cx#??})), not $pcnet_irq (PROTOCOL.INI)"; pc_ok=1; }
    return $pc_ok
}

# finish X - extract drive D:, print the results; return 0 on success.
finish() {
    v="$OUT/$1"
    d=$(part_img "$v/D.IMG")
    rc=$(cat "$v/RC" 2>/dev/null || echo 255)
    mkdir -p "$v/D"
    mcopy -s -n -i "$d" "::*" "$v/D/" 2>/dev/null || :
    for f in NETSTART.OUT OUT.TXT; do
        echo "===== VM $1: $f"
        if [ -f "$v/D/$f" ]; then tr -d '\r' <"$v/D/$f"; else echo "(missing)"; fi
    done
    card_bad=0
    if ! why=$(pcnet_check "$v/D/PCNET.OUT"); then
        printf '%s\n' "$why" | sed "s/^/$prog: VM $1: /" >&2
        card_bad=1
    fi
    net_bad=0
    if [ -n "$NETSTART" ] &&
            ! tr -d '\r' 2>/dev/null <"$v/D/NETSTART.OUT" | grep -qxF "$net_ok"; then
        tr -d '\r' 2>/dev/null <"$v/D/NETSTART.OUT" | grep '^Error' |
            sed "s/^/$prog: VM $1: NET START: /" >&2 || :
        net_bad=1
    fi
    done_code=$(tr -dc '0-9' 2>/dev/null <"$v/D/DONE.TXT" || :)
    if [ "$rc" -eq 124 ]; then
        echo "$prog: VM $1 timed out after $(dos_secs_said "$TIMEOUT")" >&2
        return 1
    fi
    if [ -z "$done_code" ] || [ $((rc % 2)) -ne 1 ] || [ "$rc" -ge 256 ] ||
            [ $(((rc - 1) / 2)) -ne "$done_code" ]; then
        echo "$prog: VM $1 did not complete its run (QEMU status $rc, marker '${done_code}')" >&2
        [ ! -s "$v/qemu.log" ] || sed 's/^/qemu: /' "$v/qemu.log" >&2
        return 1
    fi
    if [ "$done_code" -eq 0 ]; then
        echo "$prog: VM $1 command exit 0"
        [ "$card_bad" = 0 ] ||
            { echo "$prog: VM $1 failed: network card" >&2; return 1; }
        [ "$net_bad" = 0 ] ||
            { echo "$prog: VM $1 failed: NET START did not succeed" >&2; return 1; }
        return 0
    fi
    echo "$prog: VM $1 command exit $done_code" >&2
    return 1
}

# stop SIGNAL - the signal handler (on_signals): kill both VMs, their
# timeout wrappers and the relay.
stop() {
    for x in A B R; do
        eval "p=\${tpid_$x:-}"
        [ -z "$p" ] || kill "$p" 2>/dev/null || :
    done
    kill_pidfiles "$OUT/A/qemu.pid" "$OUT/B/qemu.pid"
    [ -z "$sdir" ] || rm -rf "$sdir"
    echo "$prog: stopped by SIG$1; VMs killed" >&2
}

tpid_A=
tpid_B=
tpid_R=
sdir=
rm -rf "$OUT"
mkdir -p "$OUT"
: >"$OUT/$marker"
{
    echo "PORT_A=$PORT_A"
    echo "PORT_B=$PORT_B"
    [ "$NIC2_B" = 0 ] || echo "PORT_T=$PORT_T"
} >"$OUT/PORTS.NEW"
mv "$OUT/PORTS.NEW" "$OUT/PORTS"
vms=A
prepare A "$1" "$2" "$NAME_A" "$T1_A" "$IMAGE_A"
if [ $# -eq 4 ]; then
    vms="A B"
    prepare B "$3" "$4" "$NAME_B" "$T1_B" "$IMAGE_B"
fi

on_signals stop
# A socket's path is limited to 108 bytes; mktemp's is short.
[ "$SERIAL" = 0 ] || sdir=$(mktemp -d)
start A "$MAC_A" "$PORT_A" "$PORT_B"
if [ $# -eq 4 ]; then
    if [ "$DELAY_B" -gt 0 ]; then
        sleep "$DELAY_B" &
        wait $! || :
    fi
    if [ "$RELAY" != 0 ]; then
        timeout "$limit" python3 -I "$here/LOSSY.PY" $relay_opts "$PORT_B" \
            "$PORT_A" "$OUT/RELAY.LOG" 2>"$OUT/relay.err" &
        tpid_R=$!
    fi
    start B "$MAC_B" "$PORT_B" "$PORT_A"
fi
for x in $vms; do
    collect "$x"
done
if [ -n "$tpid_R" ]; then
    kill "$tpid_R" 2>/dev/null || :
    wait "$tpid_R" 2>/dev/null || :
    tpid_R=
    [ ! -s "$OUT/relay.err" ] || sed 's/^/relay: /' "$OUT/relay.err" >&2
fi
trap - HUP INT TERM
[ -z "$sdir" ] || rm -rf "$sdir"

status=0
for x in $vms; do
    finish "$x" || status=1
done
# After each VM's own cause: a relay that never joined the VMs.
if [ "$RELAY" != 0 ] && [ ! -f "$OUT/RELAY.LOG" ]; then
    echo "$prog: relay: the relay never joined VM B to VM A (no RELAY.LOG)" \
        >&2
    status=1
fi
exit $status
