#!/bin/sh
# boot310.sh - boot a scratch copy of a DOS boot floppy (by default the
# Compaq MS-DOS 3.10 360 KB disk) in a headless emulator, optionally with a
# scratch hard disk as C:, run DOS command lines from a generated
# AUTOEXEC.BAT, and print what they wrote to standard output.
#
# usage: sh TEST/INTEG/boot310.sh [options] [COMMAND LINE]...
#
#   -i IMAGE   boot floppy image to copy (default: $DOS310_IMG, else the
#              Compaq 3.10 disk under $RETRO_DL/dos310/).  Never modified.
#   -f FILE    copy FILE to A:\ before booting (repeatable).  The base name,
#              upper-cased, must be 1-8 characters with an optional 1-3
#              character extension, each made of letters, digits and
#              _ $ ~ ! # & @ ^ -.  Device names (CON, NUL, COM1, CLOCK$, ...),
#              the files this script writes (AUTOEXEC.BAT, RCSAVE.BAT,
#              QEXIT.COM, MONO.COM, OUT.TXT, RC.TXT, COMMAND.COM) and
#              duplicates are rejected.
#   -h FILE    copy FILE to C:\ of a scratch hard disk (repeatable; the same
#              name rules as -f, so a file may go to both drives).  The hard
#              disk is attached only when -h is given; see below.
#   -t SECS    host timeout for the whole run, 1 or more (default 60),
#              times DOSTIME_SCALE (dos_secs of TEST/shlib.sh).
#   -e EMU     qemu (default) or dosbox (DOSBox-X, secondary check).
#   -m MACHINE video adapter under DOSBox-X: cga (default) or hercules
#              (monochrome, text mode 7 at B000h).  QEMU has only its VGA,
#              so -m needs -e dosbox.  DOSBox-X's boot command leaves the
#              BIOS in mode 3 even on a Hercules machine, so with hercules
#              AUTOEXEC.BAT first runs MONO.COM, which sets mode 7 through
#              INT 10h as a monochrome BIOS does at start-up.
#   -p FILE    attach QEMU's first parallel port (LPT1 at 378h, which the
#              BIOS lists in its data area and serves through INT 17h) to the
#              host file FILE, created or truncated, which then holds every
#              byte the guest prints.  Without -p the emulator's command line
#              is unchanged; -e dosbox refuses -p (DOSBox-X renders its
#              printer output as images instead of passing the bytes).
#   -b         attach a second floppy drive, B:, with no disk in it, so that
#              an access to B: raises a critical error; without -b B: is
#              the phantom of A:, for which DOS prompts for a disk change.
#              -e dosbox refuses -b.
#   -B IMAGE   attach a second floppy drive, B:, holding a scratch copy of
#              the floppy image IMAGE (never modified); with -k the copy is
#              kept as B.IMG.  -e dosbox refuses -B, and -b with -B is a
#              usage error.
#   -s         give QEMU's machine a second serial port, COM2 (2F8h, IRQ
#              3), whose line leads nowhere ("-serial null"); the BIOS lists
#              it in its data area.  Without -s or -S COM1, the console
#              below, is the only one.  -e dosbox refuses -s and -S.
#   -S SOCKET  as -s, but COM2's line is the unix socket SOCKET (no comma
#              in its name), on which a host program must already listen;
#              -s with -S is a usage error.
#   -D SCRIPT  play a user with TEST/INTEG/dialog.py and its script
#              SCRIPT, which waits for text in the console transcript,
#              types on the keyboard and changes the disk in B: (with
#              -B).  A COMMAND LINE starting with '!' then runs, without
#              the '!', with its standard output on the console instead
#              of OUT.TXT, so that the script sees it; a program reads
#              the keyboard through CON ("INSTALL <CON"), as the console
#              is COM1, whose input the BIOS of QEMU reports as failed
#              reads.  QEMU only.
#   -c         also print the console transcript (stderr, prompts, echo).
#   -k         keep the scratch directory and print its path ("boot310.sh:
#              kept DIR"); it holds the floppy as A.IMG, with -B the second
#              one as B.IMG and, with -h, the hard disk as C.IMG, whose
#              partition is then printed as an mtools image name
#              ("boot310.sh: hard disk partition DIR/C.IMG@@OFFSET", for
#              "mcopy -i").  Both lines go to standard error, also when the
#              run was interrupted.
#
# env: DOS310_IMG  the default boot floppy image of -i
#
# Each COMMAND LINE becomes one batch line with standard output appended to
# A:\OUT.TXT, so the command itself must not redirect standard output (pipes
# are fine).  A batch line is limited to 127 characters including the
# appended " >>A:\OUT.TXT", and a literal % must be written %%.  A command
# line may change the current drive (for example "C:"); the later lines and
# the final errorlevel probe still work.  Without any COMMAND LINE the run is
# "VER" and "CHKDSK", which shows the DOS version and free conventional
# memory.
#
# The 360 KB system floppy has little room left once the unneeded utilities
# are removed, so large programs and their output go on the hard disk.  It
# is a fresh image of HD_CYLS cylinders, HD_HEADS heads and HD_SECS sectors
# per track (16 MB) with one primary DOS partition, type 01, from cylinder
# 0 head 1 to the end, already formatted by mtools with FAT12 in the same
# layout as this DOS's own FORMAT gives it (8 sectors per cluster, 512 root
# entries, two 12-sector FATs, media byte F8h; mtools' boot sector carries
# its own OEM name and a DOS 4 extended BPB, which MS-DOS 3.10 ignores), so
# no FDISK or FORMAT runs in the guest.
# The geometry has 17 sectors per track because MS-DOS 3.10 addresses a
# fixed disk with 17 sectors per track (the MFM drives of its day) whatever
# INT 13h function 08h reports; QEMU and DOSBox-X are given the same
# geometry explicitly.
# MS-DOS 3.10 assigns C: to the hard disk before CONFIG.SYS runs, so a
# VDISK.SYS RAM disk becomes D: when the hard disk is present.  The
# partition starts HD_SECS * 512 bytes into C.IMG.
# A 1.2 MB floppy, in A: or in a second drive, is no alternative: MS-DOS
# 3.10 neither boots from nor reads one in QEMU, whose BIOS reports no
# change line for its floppy drives.
#
# The batch file starts with CTTY COM1 so that everything written to the DOS
# console (standard error, prompts, echoed batch lines) goes to the first
# serial port, which the emulator writes to a host file: the console
# transcript.  Programs that read the keyboard through DOS therefore wait on
# COM1 forever and end in the host timeout.  Programs that write to video
# memory or through INT 10h directly are not captured anywhere.
#
# After the last command a second batch file, A:\RCSAVE.BAT, stores the
# errorlevel in A:\RC.TXT (0..199, larger values read as 199; the ladder
# of errlevel_bat in TEST/shlib.sh) and runs A:\QEXIT 0 (BIN/QEXIT.COM,
# built with "make BIN/QEXIT.COM"), which ends QEMU through the
# isa-debug-exit device.  DOSBox-X has no such device, so in that mode
# the host polls the image for RC.TXT instead and then kills the
# emulator.
#
# A generated AUTOEXEC.BAT also stops DOS from prompting for date and time.
#
# COMMAND.COM keeps its transient part in the free memory at the top, which
# the program under test may use.  On the program's return the shell
# reloads that part from A:\COMMAND.COM only when a 16-bit sum of its words
# has changed, so a program that leaves bytes there with the same sum, by
# chance about once in 65,535 layouts, makes it jump into them, and the run
# hangs or crashes after the program itself has ended.  When the scratch
# floppy's COMMAND.COM is one that DOS_SHELLS of TEST/shlib.sh lists (the
# Compaq MS-DOS 3.10 one, or that of MS-DOS 3.30 or 5.00), the two bytes of
# the jump that skips the reload when the sum matches become NOPs
# (shell_patch), so the shell reloads its transient part after every
# program, under both emulators (about 10 ms each under QEMU); the shell is
# otherwise unchanged.  Any other COMMAND.COM is left as it is, and the
# script says so on standard error.  When QEMU times out, the script prints
# on standard error where its CPU is, read through a monitor socket in the
# scratch directory: "boot310.sh: CPU at cs:ip, SS:SP ss:sp, interrupts on"
# (or off).
#
# Exit status: when the run completed, the errorlevel left by the last
# external program (internal commands such as VER or ECHO do not change it),
# 0..199; 200 on host timeout; 201 when the emulator ended in any other way;
# 2 on a usage or setup error, or when the run's output cannot be written;
# 129, 130 or 143 when the script itself got SIGHUP, SIGINT or SIGTERM (the
# emulator is killed first).
set -eu

# mtools' FAT sanity check rejects a 360 KB image that is nearly full
# ("Cluster # at ... too big"); the image itself is sound.
export MTOOLS_SKIP_CHECK=1

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$repo/TEST/shlib.sh"
vmdir=$here/vm
qexit=$repo/BIN/QEXIT.COM

image=$DOS310_FLOPPY
timeout=60
emu=qemu
machine=
show_console=0
keep=0
lpt=
emptyb=0
bimage=
com2=0
com2sock=
files=
hdfiles=
names=
dialog=
dialogpid=

# Hard-disk geometry and layout (see above).
HD_CYLS=120
HD_HEADS=16
HD_SECS=17

while getopts i:f:h:t:e:m:p:bB:sS:D:ck opt; do
    case $opt in
    i) image=$OPTARG ;;
    f|h) [ -f "$OPTARG" ] || die "no such file: $OPTARG"
       name=$(basename "$OPTARG" | tr '[:lower:]' '[:upper:]')
       is_83 "$name" || die "not an 8.3 name: $name"
       case ${name%%.*} in
       CON|AUX|PRN|NUL|COM[1-4]|LPT[1-3]|CLOCK\$) die "device name: $name" ;;
       esac
       case $name in
       AUTOEXEC.BAT|RCSAVE.BAT|QEXIT.COM|MONO.COM|OUT.TXT|RC.TXT|COMMAND.COM)
           die "name used by this script: $name" ;;
       esac
       case " $names " in *" $opt:$name "*) die "duplicate file name: $name" ;; esac
       names="$names $opt:$name"
       if [ $opt = f ]; then
           files="$files$OPTARG
"
       else
           hdfiles="$hdfiles$OPTARG
"
       fi ;;
    t) timeout=$OPTARG ;;
    e) emu=$OPTARG ;;
    m) machine=$OPTARG ;;
    p) [ -n "$OPTARG" ] || die "-p needs a file name"
       lpt=$OPTARG ;;
    b) emptyb=1 ;;
    B) [ -f "$OPTARG" ] || die "no such image: $OPTARG"
       bimage=$OPTARG ;;
    s) [ -z "$com2sock" ] || die "-s and -S exclude each other"
       com2=1 ;;
    S) [ -z "$com2sock" ] || die "repeated -S"
       [ $com2 -eq 0 ] || die "-s and -S exclude each other"
       case $OPTARG in ''|*,*) die "bad COM2 socket name: $OPTARG" ;; esac
       com2=1; com2sock=$OPTARG ;;
    D) [ -f "$OPTARG" ] || die "no such dialogue script: $OPTARG"
       dialog=$OPTARG ;;
    c) show_console=1 ;;
    k) keep=1 ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))

case $emu in qemu|dosbox) ;; *) die "unknown emulator: $emu" ;; esac
case $machine in
'') machine=cga ;;
cga|hercules) [ $emu = dosbox ] || die "-m $machine needs -e dosbox" ;;
*) die "unknown machine: $machine" ;;
esac
case $timeout in ''|0*|*[!0-9]*) die "bad timeout: $timeout" ;; esac
limit=$(dos_secs "$timeout")
if [ -n "$lpt" ]; then
    [ $emu = qemu ] || die "-p needs -e qemu"
    # QEMU's chardev options take ',' as a separator.
    case $lpt in *,*) die "printer file name with a comma: $lpt" ;; esac
    ( : >"$lpt" ) 2>/dev/null || die "cannot create the printer file: $lpt"
fi
[ $emptyb -eq 0 ] || [ $emu = qemu ] || die "-b needs -e qemu"
[ $com2 -eq 0 ] || [ $emu = qemu ] || die "-s and -S need -e qemu"
[ -z "$bimage" ] || [ $emu = qemu ] || die "-B needs -e qemu"
[ $emptyb -eq 0 ] || [ -z "$bimage" ] || die "-b and -B exclude each other"
[ -z "$dialog" ] || [ $emu = qemu ] || die "-D needs -e qemu"
for c in "$@"; do
    [ ${#c} -le 114 ] || die "command line longer than 114 characters: $c"
done
[ -f "$image" ] || die "boot image not found: $image"
[ -f "$qexit" ] || die "missing $qexit (run: make BIN/QEXIT.COM)"
for t in mcopy mdel mtype mformat; do
    command -v $t >/dev/null || die "mtools not installed ($t)"
done
if [ $emu = qemu ]; then
    command -v qemu-system-i386 >/dev/null || die "qemu-system-i386 not installed"
else
    command -v dosbox-x >/dev/null || die "dosbox-x not installed"
fi

mkdir -p "$vmdir"
work=$(mktemp -d "$vmdir/boot310.XXXXXX")
img=$work/A.IMG
hd=
hdpart=
if [ -n "$hdfiles" ]; then
    hd=$work/C.IMG
    hdpart=$hd@@$((HD_SECS * 512))
fi
console=$work/CONSOLE.TXT
# QEMU's monitor, a unix socket whose path must fit sun_path (107 bytes)
# and hold no comma, QEMU's option separator; without it a time-out does
# not say where the CPU is.
mon=$work/MON
case $mon in *,*) mon= ;; esac
[ "$(printf %s "$mon" | wc -c)" -le 107 ] || mon=
monitor=${mon:+unix:$mon,server=on,wait=off}
[ -z "$dialog" ] || [ -n "$mon" ] || die "-D needs the monitor: $work is too long"
emupid=
cleanup() {
    [ -z "$emupid" ] || kill -KILL "$emupid" 2>/dev/null || true
    [ -z "$dialogpid" ] || kill -KILL "$dialogpid" 2>/dev/null || true
    if [ $keep = 1 ]; then
        [ -z "$mon" ] || rm -f "$mon"
        echo "$prog: kept $work" >&2
        [ -z "$hdpart" ] || echo "$prog: hard disk partition $hdpart" >&2
    else
        rm -rf "$work"
    fi
}
trap cleanup EXIT
on_signals

cp "$image" "$img"
chmod u+w "$img"
if [ -n "$bimage" ]; then
    cp "$bimage" "$work/B.IMG"
    chmod u+w "$work/B.IMG"
fi

# The 360 KB system disk has about 24 KB free.  Utilities that no test needs
# are removed from the scratch copy to make room for payload files.
for f in BASICA.EXE BASICA.COM BASIC.COM KEYBGR.COM KEYBFR.COM KEYBUK.COM \
         KEYBIT.COM KEYBSP.COM KEYBDA.COM KEYBNO.COM KEYBSV.COM KEYBSU.COM \
         GRAFTABL.COM GRAPHICS.COM DISKINIT.COM SELECT.COM; do
    mdel -i "$img" "::$f" 2>/dev/null || true
done
mdel -i "$img" ::AUTOEXEC.BAT ::RCSAVE.BAT ::OUT.TXT ::RC.TXT ::MONO.COM 2>/dev/null || true

shell=$work/COMMAND.COM
if mcopy -n -i "$img" ::COMMAND.COM "$shell" 2>/dev/null; then
    if shell_patch "$shell"; then
        mcopy -o -i "$img" "$shell" ::COMMAND.COM ||
            die "cannot patch COMMAND.COM"
    else
        echo "$prog: COMMAND.COM of $image is not a shell this script" \
            "patches (SHA-256 $shell_sum); it reloads its transient part" \
            "only when that part's sum has changed" >&2
    fi
    rm -f "$shell"
else
    echo "$prog: $image has no COMMAND.COM to patch" >&2
fi

copy_in() {
    name=$(basename "$1" | tr '[:lower:]' '[:upper:]')
    mcopy -o -i "${2:-$img}" "$1" "::$name" || die "cannot copy $1 (disk full?)"
}
copy_in "$qexit"
while IFS= read -r f; do
    [ -z "$f" ] || copy_in "$f"
done <<EOF
$files
EOF

if [ -n "$hd" ]; then
    hdsize=$((HD_CYLS * HD_HEADS * HD_SECS))
    psize=$((hdsize - HD_SECS))
    lastcyl=$((HD_CYLS - 1))
    dd if=/dev/zero of="$hd" bs=512 count=0 seek=$hdsize 2>/dev/null ||
        die "cannot create $hd"
    # Master boot record: no boot code (DOS boots from A:), one active
    # partition entry (start CHS 0/1/1, type 01, end CHS, first sector,
    # sector count) and the 55h AAh signature.
    { byte 128; byte 1; byte 1; byte 0; byte 1; byte $((HD_HEADS - 1))
      byte $((HD_SECS | ((lastcyl >> 8) << 6))); byte $lastcyl
      le32 $HD_SECS; le32 $psize; } |
        dd of="$hd" bs=1 seek=446 conv=notrunc 2>/dev/null
    printf '\125\252' | dd of="$hd" bs=1 seek=510 conv=notrunc 2>/dev/null
    mformat -i "$hdpart" -h $HD_HEADS -s $HD_SECS -H $HD_SECS -T $psize \
        -c 8 -r 32 -L 12 -m 0xf8 :: || die "cannot format $hd"
    while IFS= read -r f; do
        [ -z "$f" ] || copy_in "$f" "$hdpart"
    done <<EOF
$hdfiles
EOF
fi
# MONO.COM: MOV AX,0007h; INT 10h; RET (to the INT 20h at PSP:0000).
if [ $machine = hercules ]; then
    printf '\270\007\000\315\020\303' >"$work/MONO.COM"
    copy_in "$work/MONO.COM"
fi

# Generate AUTOEXEC.BAT and RCSAVE.BAT with DOS line endings.  AUTOEXEC.BAT
# ends by chaining to RCSAVE.BAT (DOS before 3.3 has no CALL), which keeps
# the errorlevel probe out of the console transcript.
bat=$work/AUTOEXEC.BAT
rcsave=$work/RCSAVE.BAT
{
    echo 'CTTY COM1'
    [ $machine != hercules ] || echo 'MONO'
    if [ $# -eq 0 ]; then
        echo 'VER >>OUT.TXT'
        echo 'CHKDSK >>OUT.TXT'
    else
        for c in "$@"; do
            case $c in
            !*) [ -n "$dialog" ] || die "a command line with '!' needs -D: $c"
                printf '%s\n' "${c#!}" ;;
            *) printf '%s >>A:\\OUT.TXT\n' "$c" ;;
            esac
        done
    fi
    echo 'A:\RCSAVE'
} | dos_text >"$bat"
{
    echo 'ECHO OFF'
    errlevel_bat 199
    echo 'ECHO %RC%>A:\RC.TXT'
    echo 'A:\QEXIT 0'
} | dos_text >"$rcsave"
mcopy -o -i "$img" "$bat" "$rcsave" :: || die "cannot write the batch files (disk full?)"

# Both emulators run in the background so that a signal to this script is
# handled at once: the trap exits and cleanup kills the emulator.  QEMU ends
# by itself through QEXIT; DOSBox-X is killed once RC.TXT has appeared.
if [ $emu = qemu ]; then
    # 486 is the oldest CPU model QEMU offers; -m 1 yields 640 KB conventional
    # memory as on the target machines.
    # -d nochain (logging to /dev/null) turns off QEMU's direct chaining of
    # translated blocks: its fast path for a near CALL/JMP within one page does
    # not wrap IP to 16 bits, so a call whose target lies across offset 0 of the
    # code segment (common in Open Watcom medium-model EXEs, whose linker turns
    # far calls within a segment into PUSH CS + near CALL) jumps to the wrong
    # address and the program hangs.  Seen with QEMU 10.0.13.
    set --
    if [ -n "$hd" ]; then
        set -- -drive "file=$hd,if=none,format=raw,id=hd0" \
            -device "ide-hd,drive=hd0,cyls=$HD_CYLS,heads=$HD_HEADS,secs=$HD_SECS"
    fi
    if [ -n "$lpt" ]; then
        set -- "$@" -parallel "file:$lpt"
    fi
    if [ $com2 -eq 1 ]; then
        set -- "$@" -serial "${com2sock:+unix:}${com2sock:-null}"
    fi
    if [ $emptyb -eq 1 ]; then
        set -- "$@" -drive if=floppy,index=1
    elif [ -n "$bimage" ]; then
        set -- "$@" -drive "file=$work/B.IMG,if=floppy,index=1,format=raw"
    fi
    qemu-system-i386 -cpu 486 -m 1 -accel "$QEMU_ACCEL" -d nochain -D /dev/null \
        -display none -monitor "${monitor:-none}" \
        -serial "file:$console" \
        -drive "file=$img,if=floppy,format=raw" -boot a "$@" \
        -device isa-debug-exit,iobase=0x501,iosize=0x01 &
else
    # CGA, the default, matches the text-mode video floor; COM1 goes to
    # the transcript.
    conf=$work/DOSBOX.CONF
    cat >"$conf" <<EOF
[dosbox]
machine=$machine
memsize=1
quit warning=false
[cpu]
cputype=8086
core=normal
cycles=max
[serial]
serial1=file file:$console
[autoexec]
EOF
    if [ -n "$hd" ]; then
        echo "imgmount 2 \"$hd\" -t hdd -fs none -size 512,$HD_SECS,$HD_HEADS,$HD_CYLS" >>"$conf"
    fi
    printf 'imgmount a "%s" -t floppy\nboot a:\n' "$img" >>"$conf"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        dosbox-x -conf "$conf" -nopromptfolder -silent >"$work/DOSBOX.LOG" 2>&1 &
fi
emupid=$!
if [ -n "$dialog" ]; then
    python3 -I "$here/dialog.py" "$console" "$mon" "$dialog" >&2 &
    dialogpid=$!
fi

# cpu_at: print on standard error where QEMU's CPU is, from "info
# registers" on the monitor; nothing when the monitor does not answer.
cpu_at() {
    python3 -I - "$prog" "$mon" <<'EOF' >&2 2>/dev/null || true
import re, socket, sys, time
s = socket.socket(socket.AF_UNIX)
s.settimeout(1)
s.connect(sys.argv[2])
s.sendall(b'info registers\n')
out = ''
end = time.time() + 3
while time.time() < end and not ('EFL=' in out and 'SS =' in out):
    try:
        data = s.recv(4096)
    except socket.timeout:
        continue
    if not data:
        break
    out += data.decode('latin-1')
reg = dict(re.findall(r'([A-Z]{2,3}) ?=([0-9a-f]+)', out))
print('%s: CPU at %s:%s, SS:SP %s:%s, interrupts %s' % (
    sys.argv[1], reg['CS'].upper(), reg['EIP'][-4:].upper(),
    reg['SS'].upper(), reg['ESP'][-4:].upper(),
    'on' if int(reg['EFL'], 16) & 0x200 else 'off'))
EOF
}

# Poll five times a second until the emulator exits (QEMU), RC.TXT appears
# (DOSBox-X) or the timeout expires.
ticks=$((limit * 5))
status=200
while [ $ticks -gt 0 ]; do
    sleep 0.2
    ticks=$((ticks - 1))
    if ! kill -0 "$emupid" 2>/dev/null; then
        status=201
        break
    fi
    if [ $emu = dosbox ] && mtype -i "$img" ::RC.TXT 2>/dev/null | grep -q '[0-9]'; then
        sleep 1
        status=0
        break
    fi
done
if [ $status = 201 ]; then
    # QEXIT 0 makes QEMU exit with host status 1.
    emustatus=0
    wait "$emupid" || emustatus=$?
    emupid=
    if [ $emu = qemu ] && [ $emustatus = 1 ]; then
        status=0
    else
        echo "$prog: $emu ended with status $emustatus before the run completed" >&2
    fi
else
    if [ $status != 0 ]; then
        echo "$prog: timed out after $(dos_secs_said "$timeout")" >&2
        [ $emu != qemu ] || [ -z "$mon" ] || cpu_at
    fi
    kill -KILL "$emupid" 2>/dev/null || true
    wait "$emupid" 2>/dev/null || true
    emupid=
fi

if [ -n "$dialogpid" ]; then
    if kill -0 "$dialogpid" 2>/dev/null; then
        kill -KILL "$dialogpid" 2>/dev/null || true
        wait "$dialogpid" 2>/dev/null || true
        echo "$prog: the dialogue script did not run to its end" >&2
    elif ! wait "$dialogpid"; then
        echo "$prog: the dialogue script failed" >&2
    fi
    dialogpid=
fi

# A run whose commands printed nothing has no OUT.TXT, which mtype reports
# on standard error; tr fails only when the output cannot be written (a
# full disk), and then the run must not pass for one that printed nothing.
mtype -i "$img" ::OUT.TXT 2>/dev/null | tr -d '\r' ||
    die "cannot write the run's output"
if [ $show_console = 1 ] || [ $status -ne 0 ]; then
    if [ -s "$console" ]; then
        echo '--- console ---'
        tr -d '\r' <"$console"
    fi
fi
if [ $status -eq 0 ]; then
    rc=$(mtype -i "$img" ::RC.TXT 2>/dev/null | tr -cd '0-9')
    [ -n "$rc" ] || { echo "$prog: run ended without RC.TXT" >&2; exit 201; }
    status=$rc
fi
exit $status
