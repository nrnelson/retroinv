#!/bin/sh
# runwin.sh - boot a copy of a Windows test machine (winlib.sh) headless,
# start Windows with a program, wait for Windows to end and bring the
# program's files back.
#
# usage: sh TEST/WIN/runwin.sh [-w MACHINE] [-e EMU] [-m MODE] [-t SECS]
#            [-i IMAGE] [-I INTL] [-n NETDEV] [-d DUMP] [-s SOCKET]
#            [-p MONITOR] [-k] RUNDIR 'COMMAND LINE'...
#
#   RUNDIR     a directory whose files and subdirectories, every path
#              upper-case 8.3, are copied to C:\RUN before the boot; the
#              files of C:\RUN are copied back into it afterwards, new and
#              changed ones replacing the old (a file the run deletes
#              stays).
#   COMMAND    the program and its arguments, WIN's command line: the run
#              starts "WIN /R COMMAND" (-m r; /S for s, /3 for 3) with
#              C:\RUN the current directory, and with several, each in
#              turn once Windows has ended.  The program writes its
#              results into its own directory and ends Windows with
#              ExitWindows.  WIN leaves errorlevel 0 whatever code
#              ExitWindows is given, so the program's files tell how it
#              went.
#   -w MACHINE win30 (the default), win30nb or wfw311
#   -e EMU     an emulator of the machine (WIN_EMUS): qemu (default), a
#              486 under TCG with QEMU_MB of RAM and a VGA, its clock
#              starting at CLOCK, the time wintest.sh seeds its data at, so
#              that a date the program shows of its own (a user's last
#              login) is the same on every day; or, for win30,
#              dosbox: DOSBox-X as an 8086 with 640 KB and a CGA, modes r
#              and dos only.  Windows 3.0's VGA and EGA drivers need a 186
#              or later, so on the 8086 the batch file below first copies
#              C:\WINDOWS\SYSTEM.CGA, the same settings with the CGA's
#              driver and fonts, over SYSTEM.INI.
#   -m MODE    a mode of the machine (WIN_MODES): r (real mode), s
#              (standard mode) or 3 (386 enhanced mode), the default its
#              first; or dos: COMMAND runs under DOS, without Windows.
#   -t SECS    host limit of the run, 1 or more (default 120), times
#              DOSTIME_SCALE (dos_secs of TEST/shlib.sh).
#   -i IMAGE   the machine's disk (default BASE.IMG of its directory, as
#              TEST/WIN/mkwin.sh builds it); never written.  Each run
#              boots its own copy, so runs may go on side by side.
#   -I INTL    a file whose lines, its own [intl] line first, replace the
#              [intl] section of WIN.INI in the run's copy of the disk:
#              the International settings of Control Panel, which
#              are otherwise those of the machine's WIN.INI template
#              (mkwin.sh).
#   -n NETDEV  the network of the machine's card (WIN_NIC; win30nb and
#              wfw311 only): a QEMU -netdev value without its id, such as
#              "user" (QEMU's user-mode network: the guest's gateway
#              10.0.2.2 is the host, a connection to 10.0.2.2:PORT one to
#              the host's 127.0.0.1:PORT) or a "stream" netdev shared with
#              another machine; without it the card has no network.
#   -d DUMP    where QEMU saves the screen as a PNG file when the run is
#              still going at the limit (default RUNDIR.PNG); beside it,
#              named DUMP with .MON for its .PNG (or after its name, if
#              it has no .PNG), go the monitor's "info registers" and
#              "info pic" (the CPU and the interrupt controllers), all
#              three taken with the machine stopped.  When QEMU ends before
#              the run did (status 201 below), beside it with .RST goes
#              QEMU's log of the run: each reset of the CPU with its state
#              (the machine's own at power-on), a guest's triple fault, a
#              shutdown asked for by the monitor or a signal (none is
#              logged when the guest reset the machine otherwise) and the
#              run states.
#              Files there are removed first.  DOSBox-X has no monitor,
#              so no dump.
#   -s SOCKET  give the QEMU machine a COM1 (3F8h, IRQ 4) whose line is
#              the unix socket SOCKET (no comma in its name), which QEMU
#              connects to, and again every 200 ms while no program
#              listens there.  Without -s the machine has no serial
#              port under either emulator; -e dosbox refuses -s.
#   -p MONITOR QEMU's monitor (unix socket, no comma in its name, at
#              most 107 bytes) at MONITOR, for another program to drive
#              the machine through it while it runs; by default it lies
#              in the scratch directory.  -e dosbox refuses -p.
#   -k         keep the scratch directory and print its path ("runwin.sh:
#              kept DIR"); it holds the disk as C.IMG.
#
# AUTOEXEC.BAT of the image ends in C:\RUNWIN\RUN.BAT when that exists,
# and this script writes it: it records that the boot reached it
# (C:\RUNWIN\START.TXT), runs each COMMAND, records the errorlevel the
# last left (0 to 199, larger values reading as 199: errlevel_bat of
# TEST/shlib.sh) in C:\RUNWIN\END.TXT and ends the machine with
# C:\RUNWIN\QEXIT 0 (BIN/QEXIT.COM), which QEMU's isa-debug-exit device
# turns into its exit.  DOSBox-X has no such device, so the host polls
# the disk for END.TXT instead and then kills it; DOSBox-X also stops
# itself a second after the limit (-time-limit), writing out what DOS
# wrote, and is killed DOSBOX_GRACE seconds later if it has not.  Nothing
# the machine prints is captured: Windows owns the screen.
#
# Exit status: the errorlevel of END.TXT when the run ended through the
# batch file; 200 when the limit expired after the batch had started
# (Windows or the program never ended); 201 when the emulator stopped
# after the batch had started but before its end (a crash, a QEMU error,
# a reset, which ends QEMU; DOSBox-X boots again instead and so runs on
# to the limit, 200); 202 when the machine never reached the batch, at
# the limit or not (a boot failure); 2 on a usage or setup error; 129,
# 130 or 143 when the script got SIGHUP, SIGINT or SIGTERM (the emulator
# is killed first).  The files of C:\RUN come back on every end but a
# signal; one that cannot be copied is reported on standard error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/winlib.sh"

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

# The RAM of the QEMU machine in MB: 640 KB and 3 MB of extended memory,
# enough for 386 enhanced mode, TCP/IP-32 included.
QEMU_MB=4
# The date and time the QEMU machine's clock starts at.
CLOCK=$SEED_CLOCK
# The seconds DOSBox-X gets past the limit to stop by itself.
DOSBOX_GRACE=10

machine=win30
emu=qemu
mode=
timeout=120
image=
netdev=
intl=
dump=
serial=none
mon=
keep=0
while getopts w:e:m:t:i:I:n:d:s:p:k opt; do
    case $opt in
    w) machine=$OPTARG ;;
    e) emu=$OPTARG ;;
    m) mode=$OPTARG ;;
    t) timeout=$OPTARG ;;
    i) image=$OPTARG ;;
    I) intl=$OPTARG ;;
    n) netdev=$OPTARG ;;
    d) dump=$OPTARG ;;
    s) case $OPTARG in ''|*,*) die "bad serial socket name: $OPTARG" ;; esac
       serial=unix:$OPTARG,reconnect-ms=200 ;;
    p) case $OPTARG in ''|*,*) die "bad monitor socket name: $OPTARG" ;; esac
       mon=$OPTARG ;;
    k) keep=1 ;;
    *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 2 ] || usage
rundir=$1
shift
win_machine "$machine"
mode=${mode:-${WIN_MODES%% *}}
image=${image:-$WIN_IMGDIR/BASE.IMG}
limit=$(dos_secs "$timeout")
case " $WIN_EMUS " in
*" $emu "*) ;;
*) die "unknown emulator of $WIN_MACHINE: $emu" ;;
esac
need_tools mcopy mmd mdir mtype
case $emu in
qemu) need_tools qemu-system-i386 python3 ;;
*) need_tools dosbox-x
   [ "$serial" = none ] || die "-s needs -e qemu"
   [ -z "$mon" ] || die "-p needs -e qemu" ;;
esac
case $mode in
dos) win= ;;
*) case " $WIN_MODES " in
   *" $mode "*) ;;
   *) die "unknown mode of $WIN_MACHINE: $mode" ;;
   esac
   [ $mode = r ] || [ $emu = qemu ] ||
       die "-m $mode needs -e qemu: the 8086 has no such mode"
   win="C:\\WINDOWS\\WIN /$(echo $mode | tr rs RS) " ;;
esac
[ -z "$netdev" ] || [ -n "$WIN_NIC" ] || die "$WIN_MACHINE has no network card"
[ -d "$rundir" ] || die "no such directory: $rundir"
[ -f "$image" ] || die "no image: $image (run TEST/WIN/mkwin.sh)"
[ -z "$intl" ] || [ -f "$intl" ] || die "no such file: $intl"
qexit=$repo/BIN/QEXIT.COM
[ -f "$qexit" ] || die "missing $qexit (run: make BIN/QEXIT.COM)"
(cd "$rundir" && find . -mindepth 1) | sed 's|^\./||' |
    while IFS= read -r f; do check83 "$f"; done
dump=${dump:-$(cd "$rundir" && pwd).PNG}
case $dump in *[[:space:]]*) die "dump file name with a blank: $dump" ;; esac
state=${dump%.PNG}.MON
resets=${dump%.PNG}.RST

work=$(mktemp -d "${TMPDIR:-/tmp}/runwin.XXXXXX")
# The disk's path is pasted into a quoted DOSBox-X imgmount command.
case $work in
*'"'*) rmdir "$work"; die "path contains a double quote: $work" ;;
esac
c=$work/C.IMG
part=$(win_part "$c")
# QEMU's monitor, a unix socket, whose path must fit sun_path.
mon=${mon:-$work/MON}
emupid=
cleanup() {
    [ -z "$emupid" ] || kill -KILL "$emupid" 2>/dev/null || true
    rm -f "$mon"
    if [ $keep = 1 ]; then
        echo "$prog: kept $work" >&2
    else
        rm -rf "$work"
    fi
}
trap cleanup EXIT
on_signals
[ ${#mon} -le 107 ] || die "monitor path too long for a socket: $mon"

cp --sparse=always "$image" "$c"
chmod u+w "$c"
rm -f "$dump" "$state" "$resets"
mmd -i "$part" "::$WIN_APPDIR" "::$WIN_CTLDIR" 2>/dev/null ||
    die "$image is not a disk this script runs"
if [ -n "$intl" ]; then
    mcopy -n -i "$part" ::WINDOWS/WIN.INI "$work/WIN.OLD"
    awk -v intl="$intl" '
        { sub(/\r$/, "") }
        /^\[/ { skip = 0 }
        tolower($0) == "[intl]" {
            while ((getline l < intl) > 0) { sub(/\r$/, "", l); print l }
            print ""
            skip = found = 1
            next
        }
        !skip { print }
        END { exit !found }' "$work/WIN.OLD" >"$work/WIN.NEW" ||
        die "WIN.INI of $image has no [intl] section"
    dos_text <"$work/WIN.NEW" >"$work/WIN.INI"
    mcopy -o -i "$part" "$work/WIN.INI" ::WINDOWS/WIN.INI
fi
for f in "$rundir"/*; do
    [ ! -e "$f" ] || mcopy -s -m -i "$part" "$f" "::$WIN_APPDIR/"
done
ctl="C:\\$WIN_CTLDIR"
bat=$work/RUN.BAT
: >"$bat"
emit "$bat" 'ECHO OFF'
emit "$bat" "ECHO OK>$ctl\\START.TXT"
[ $emu = qemu ] ||
    emit "$bat" "COPY C:\\WINDOWS\\$WIN_CGAINI C:\\WINDOWS\\SYSTEM.INI >NUL"
# Windows may leave another current directory behind as it ends.
for cmd; do
    emit "$bat" "CD \\$WIN_APPDIR"
    emit "$bat" "$win$cmd"
done
errlevel_bat 199 >>"$bat"
emit "$bat" "ECHO %RC%>$ctl\\END.TXT"
emit "$bat" "$ctl\\QEXIT 0"
mcopy -i "$part" "$bat" "$qexit" "::$WIN_CTLDIR/"

# Both emulators run in the background so that a signal to this script is
# handled at once: the trap exits and cleanup kills the emulator.
if [ $emu = qemu ]; then
    # -d nochain works around QEMU's 16-bit near-call wrapping (see
    # TEST/INTEG/boot310.sh); -no-reboot makes a reset end QEMU, with
    # status 0 and no word, so the log tells a reset from a quit.
    if [ -z "$WIN_NIC" ]; then
        set -- -nic none
    elif [ -z "$netdev" ]; then
        set -- -device "$WIN_NIC"
    else
        set -- -netdev "$netdev,id=n0" -device "$WIN_NIC,netdev=n0"
    fi
    qemu-system-i386 -cpu 486 -m $QEMU_MB -accel "$QEMU_ACCEL" \
        -d nochain,cpu_reset,trace:qemu_system_shutdown_request,trace:runstate_set \
        -D "$work/QEMU.LOG" -display none -vga std -no-reboot \
        -rtc base=$CLOCK \
        -monitor "unix:$mon,server=on,wait=off" -serial "$serial" -parallel none \
        "$@" -device isa-debug-exit,iobase=0x501,iosize=0x01 \
        -drive "file=$c,if=none,id=hd0,format=raw" \
        -device "$(win_device hd0)" \
        -boot c >"$work/EMU.LOG" 2>&1 &
    ticks=$((limit * 5))
else
    conf=$work/DOSBOX.CONF
    cat >"$conf" <<EOF
[dosbox]
machine=cga
memsize=1
[cpu]
cputype=8086
core=normal
cycles=max
[mixer]
nosound=true
[serial]
serial1=disabled
serial2=disabled
[autoexec]
imgmount 2 "$c" -t hdd -fs none -size 512,$WIN_SECS,$WIN_HEADS,$WIN_CYLS
boot -l c
EOF
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        dosbox-x -conf "$conf" -nogui -nomenu -nopromptfolder \
        -time-limit $((limit + 1)) >"$work/EMU.LOG" 2>&1 &
    ticks=$(((limit + 1 + DOSBOX_GRACE) * 5))
fi
emupid=$!

# marker NAME - succeed when C:\RUNWIN\NAME holds text.
marker() {
    mtype -i "$part" "::$WIN_CTLDIR/$1" 2>/dev/null | grep -q '[0-9A-Z]'
}

# Poll five times a second until the emulator exits, END.TXT appears
# (DOSBox-X) or the ticks run out; late is set once the limit has passed.
late=0
ended=0
left=$((ticks - limit * 5))
while [ $ticks -gt 0 ]; do
    sleep 0.2
    ticks=$((ticks - 1))
    [ $ticks -gt $left ] || late=1
    if ! kill -0 "$emupid" 2>/dev/null; then
        ended=1
        break
    fi
    if [ $emu = dosbox ] && marker END.TXT; then
        break
    fi
done
if [ $late = 1 ] && [ $ended = 0 ] && [ $emu = qemu ]; then
    python3 -I - "$mon" "$dump" "$state" <<'EOF' 2>/dev/null || true
import socket, sys, time
cmds = [b'stop', b'screendump %s -f png' % sys.argv[2].encode(),
        b'info registers', b'info pic']
s = socket.socket(socket.AF_UNIX)
s.settimeout(5)
s.connect(sys.argv[1])
end = time.time() + 10
out = b''
# Each command goes after the prompt that the one before it ends with.
for n in range(len(cmds) + 1):
    while time.time() < end and out.count(b'(qemu) ') <= n:
        out += s.recv(4096)
    if n < len(cmds):
        s.sendall(cmds[n] + b'\n')
# After each prompt, the monitor's echo of the command, one line, then
# its answer.
answers = out.split(b'(qemu) ')[1:]
with open(sys.argv[3], 'wb') as f:
    for cmd, ans in zip(cmds[2:], answers[2:]):
        f.write(b'== %s\n%s' % (cmd, ans.partition(b'\r\n')[2]
                                .replace(b'\r', b'')))
EOF
fi
emustatus=0
if [ $ended = 1 ]; then
    wait "$emupid" || emustatus=$?
else
    kill -KILL "$emupid" 2>/dev/null || true
    wait "$emupid" 2>/dev/null || true
fi
emupid=

# An empty C:\RUN has nothing to bring back, and mcopy refuses its "*".
if [ -n "$(mdir -b -i "$part" "::$WIN_APPDIR")" ]; then
    mcopy -s -n -m -i "$part" "::$WIN_APPDIR/*" "$rundir/" ||
        echo "$prog: C:\\$WIN_APPDIR did not all come back" >&2
fi
if marker END.TXT && { [ $emu = dosbox ] || [ "$emustatus" = 1 ]; }; then
    rc=$(mtype -i "$part" "::$WIN_CTLDIR/END.TXT" | tr -cd '0-9')
    echo "$prog: ended with errorlevel $rc"
    exit "$rc"
fi
[ ! -s "$work/EMU.LOG" ] || tail -n 20 "$work/EMU.LOG" | sed "s/^/$emu: /" >&2
if ! marker START.TXT; then
    status=202
    echo "$prog: the machine did not reach the run batch" >&2
elif [ $late = 1 ]; then
    status=200
    echo "$prog: timed out after $(dos_secs_said "$timeout")" >&2
else
    status=201
    echo "$prog: $emu ended with status $emustatus before the run ended" >&2
    [ ! -s "$work/QEMU.LOG" ] || cp "$work/QEMU.LOG" "$resets"
fi
[ ! -f "$dump" ] || echo "$prog: screen in $dump" >&2
[ ! -f "$state" ] || echo "$prog: CPU and interrupt controllers in $state" >&2
[ ! -f "$resets" ] || echo "$prog: resets, quits and run states in $resets" >&2
exit $status
