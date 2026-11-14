#!/bin/sh
# mkwin.sh - build the system disk of a Windows test machine (winlib.sh),
# installed on the host without an emulator: MS-DOS 3.10 and Windows 3.0,
# MS-DOS 3.30 and Windows 3.0 with MS Network Client 3.0's NetBEUI, or
# MS-DOS 3.30 and Windows for Workgroups 3.11 with Microsoft TCP/IP-32.
#
# usage: sh TEST/WIN/mkwin.sh [-w MACHINE] [-x EXTDIR]...
#
#   -w MACHINE  win30 (the default), win30nb or wfw311 (win_machine of
#               winlib.sh)
#   -x EXTDIR   an extension of the image (repeatable, applied in order):
#               every file under EXTDIR but CONFIG.ADD and AUTOEXEC.ADD at
#               its top goes to the same path on C:, which must be upper
#               case 8.3; the lines of CONFIG.ADD are added to the end of
#               CONFIG.SYS, those of AUTOEXEC.ADD to AUTOEXEC.BAT before
#               the line that starts a run.  Device drivers, network
#               clients and their settings come in this way.
#
# env: WIN30_DIR     directory of the seven Windows 3.0 720 KB disks
#                    DISK01.IMG to DISK07.IMG (default the "win30"
#                    package under RETRO_DL)
#      WFW311_DIR    directory of the eight Windows for Workgroups 3.11
#                    1.44 MB disks Disk01.img to Disk08.img, in any letter
#                    case (default the "wfw311" package)
#      TCP32_DIR     directory of TCP32B.EXE, Microsoft TCP/IP-32 3.11b
#                    (default the "tcp32b" package)
#      DOS310_IMG    win30's MS-DOS boot floppy, whose system goes on C:
#                    (DOS310_FLOPPY of TEST/shlib.sh; win30nb and wfw311
#                    take DOS330_FLOPPY)
#      NET330_DIR    win30nb's source of the network client, the NetBIOS
#                    VM image (TEST/INTEG/net330.sh)
#      WINIMG_DIR, WINNBIMG_DIR, WFWIMG_DIR  the output directory of each
#                    machine (winlib.sh)
#      RETRO_DL      root of the period downloads (default
#                    $HOME/retro-dl)
#
# Output, under the machine's directory: BASE.IMG, the disk (read-only;
# each run boots a copy of it), and INPUTS.SHA, the SHA-256 of the
# build's inputs: the machine, the scripts of this directory and
# TEST/shlib.sh, the machine's templates, its disks, TCP32B.EXE for
# wfw311, the floppy, the extensions (paths and contents, in order) and
# the versions of nasm, which assembles the master boot code MBR.ASM, and
# of mtools, which writes the file system.  The image is rebuilt only
# when that hash has changed, and the same inputs give the same bytes:
# every file keeps the time stamp it has on its disk or in its archive,
# and the files and directories made here take that of WIN.CNF.  One
# build runs at a time per output directory, under the lock DIR.lock; a
# second waits for it.  The image is built and checked in DIR/build and
# then renamed over BASE.IMG, so a failed build leaves the previous image
# as it was and a run copying it never sees half an image.
#
# Install.  The files are those SETUP copies, after SETUP.INF, for an
# "MS-DOS System" with a 386, a VGA, an enhanced 101-key keyboard, no
# mouse, US settings and code page 437 (the tables below).  For win30
# that is SETUP's whole set for an "MS-DOS or PC-DOS System" without a
# network, the files of all three modes, and the CGA driver and fonts of
# the 8086 machine (runwin.sh -e dosbox), as Windows 3.0's VGA and EGA
# drivers need a 186 or later.  win30nb takes the same files and an
# extension of its own, applied first: the files of C:\NET of the
# NetBIOS VM image (TEST/INTEG/mkimg.sh, which must be current; read under
# its lock, shared) that NET START reads, and the settings of
# TEST/INTEG/SYSTEM.INI and PROTOCOL.INI, with the computer name
# WIN_NBNAME of winlib.sh and the response timer of VM B (NB_T1_B of
# TEST/INTEG/net330.sh), as this machine takes VM B's place beside VM A
# of TEST/INTEG/run2vm.sh; and
# PCNETBM.COM and SETMACH.COM (from PCNETBM.ASM and SETMACH.ASM of this
# directory).  AUTOEXEC.BAT runs PCNETBM for a run that asks for the
# network, by a file NET.ON in its directory C:\RUN, and when it has
# found the card the network starts with C:\NET\NET START, its output in
# C:\RUN\NETSTART.OUT; a run's C:\RUN\NETUSE.BAT, if it has one, then
# connects its drives, and SETMACH gives DOS that name as the machine
# name that function 5E00h reports, which the client itself always
# leaves unset.  For wfw311 it is the set of a custom
# setup without the optional accessories, games, screen savers,
# wallpapers, readme files and TrueType fonts, then NETWORK.INF's NDIS 3
# driver of the NE2000 and the files of TCP/IP-32's OEMSETUP.INF, which
# TCP32B.EXE, a ZIP archive behind a DOS extractor stub, holds and which
# are read from it on the host; not the Microsoft Windows Network, whose
# driver needs the security settings WFWSYS.CFG, which only SETUP's half
# in Windows makes.  SETUP's second half runs in Windows and adds
# printers and Program Manager groups; the tests need no group, and
# win30 and win30nb get one printer as Control Panel would install it,
# the Generic / Text Only driver of SETUP.INF's [io.device], whose port
# WIN.INI names.  Its first half writes SYSTEM.INI and WIN.INI (and for
# wfw311 PROTOCOL.INI), here the templates of the machine (*.INI of
# WIN_TPL), whose [intl] sets ISO short dates (yyyy-MM-dd), a '.' decimal
# separator, no thousands separator and a 0 before it (iLZero=1), so that
# INVWIN shows dates and numbers as the wire gives them unless a run
# sets others (runwin.sh -I), and WIN.COM, the start-up code WIN.CNF
# followed by the display's logo code and data.  SYSTEM.CGA is win30's
# SYSTEM.INI with the CGA's display lines (cga_sed).  The compressed
# files are expanded with msexpand (SZDD, Windows 3.0) or KWAJ.PY (KWAJ,
# Windows for Workgroups, whose disks keep such a file under its name
# with the extension's last character made "_", the whole name in its
# header, which must match); each is checked against the length its
# header records, and all of them together against the SHA-256 of the
# Windows set (expand_win30, expand_wfw311).  The system
# files of the floppy (its first two root entries) go first on C:, as its
# boot sector loads them from the start of the data area, and its other
# files to C:\DOS; the shell C:\COMMAND.COM reloads its transient part
# after every program (shell_patch of TEST/shlib.sh).  The partition's
# boot sector is the floppy's, its parameter block that of the new file
# system.
#
# Check.  A copy of the new image boots once under each emulator of the
# machine: under QEMU it starts Windows in the machine's last mode with
# the smoke test BIN/WIN/WSMOKE.EXE (TEST/WSMOKE.C), whose report must
# say PASS; under DOSBox-X, whose 8086 runs no protected-mode program, it
# runs VER under DOS and must bring its output back.  What runwin.sh
# leaves of a failed run, its screen and registers at a time-out or QEMU's
# log of an early end, stays in CHECK.PNG, CHECK.MON or CHECK.RST of the
# output directory until the next build or up-to-date check.
#
# Exit status: 0 when the image is built or already current; 1 when the
# boot check fails; 2 on a usage or setup error; the status of any other
# command that fails; 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/winlib.sh"

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

WIN30_DIR=${WIN30_DIR:-"$RETRO_DL/win30/Microsoft Windows 3.0 (3.5 720K)"}
WFW311_DIR=${WFW311_DIR:-\
"$RETRO_DL/wfw311/Microsoft Windows for Workgroups 3.11 (OEM) (3.5-1.44mb)"}
tcp32_pkg='Microsoft TCP-IP-32 for Windows for Workgroups 3.11 (3.11b) (1995)'
TCP32_DIR=${TCP32_DIR:-"$RETRO_DL/tcp32b/$tcp32_pkg/Setup"}
# The limit of the boot check, in seconds, before runwin.sh scales it by
# DOSTIME_SCALE: runwin.sh's default.
CHECK_SECS=120
# The SHA-256 of the "SHA-256  PATH" lines, in path order, of the files
# that the install expands, per Windows set: a decoder that writes the
# right length but wrong bytes is caught here, not by a run.
expand_win30=a3d6011d250624a80109d86b0181db2c61d2f83055fc961649c1214cf3a1d468
expand_wfw311=af25237f9a45a792abb826528d54f3e3430da2055b82af24065ed06f65a7813b
# The lines that make win30's SYSTEM.INI the 8086 machine's: its CGA
# driver, grabber and fonts.
cga_sed='s/^display.drv=vga.drv$/display.drv=cga.drv/
s/^oemfonts.fon=vgaoem.fon$/oemfonts.fon=cgaoem.fon/
s/^fonts.fon=vgasys.fon$/fonts.fon=cgasys.fon/
s/^fixedfon.fon=vgafix.fon$/fixedfon.fon=cgafix.fon/
s/^286grabber=vgacolor.gr2$/286grabber=cga.gr2/
s/^display.drv=VGA$/display.drv=CGA/'

# What goes where: a directory of C:, then the files SETUP copies there,
# each as DISK:NAME, or DISK:NAME=TARGET for a file installed under
# another name; DISK is the number of a disk, or t for TCP32B.EXE.  The
# section of SETUP.INF (or of NETWORK.INF and OEMSETUP.INF) of each group
# is in brackets.
install_win30='
WINDOWS 1:SETUP.EXE 1:SETUP.INF                                 [Main]
WINDOWS 1:SETUP.HLP 1:WINHELP.HLP 1:WINHELP.EXE                  [windows]
WINDOWS 2:CONTROL.INI 2:RAMDRIVE.SY$=RAMDRIVE.SYS 2:SMARTDRV.SY$=SMARTDRV.SYS
WINDOWS 2:EMM386.SY$=EMM386.SYS 2:WINVER=WINVER.EXE              [net]
WINDOWS 5:HIMEM.SY$=HIMEM.SYS                                    [copy.himem]
WINDOWS 3:PROGMAN.EXE 4:TASKMAN.EXE 3:WINFILE.EXE 3:CALC.EXE     [win.apps]
WINDOWS 3:CALENDAR.EXE 3:CARDFILE.EXE 4:CLIPBRD.EXE 4:CLOCK.EXE
WINDOWS 3:DIGITAL.FON 3:CONTROL.EXE 3:NOTEPAD.EXE 3:PBRUSH.EXE 3:PBRUSH.DLL
WINDOWS 3:PIFEDIT.EXE 3:RECORDER.EXE 3:RECORDER.DLL 4:REVERSI.EXE
WINDOWS 3:PRINTMAN.EXE 3:SOL.EXE 4:TERMINAL.EXE 3:WRITE.EXE 3:MSDOS.EXE
WINDOWS 4:CALC.HLP 4:CALENDAR.HLP 5:CARDFILE.HLP 4:CLIPBRD.HLP   [win.help]
WINDOWS 4:CONTROL.HLP 4:NOTEPAD.HLP 4:PBRUSH.HLP 4:PIFEDIT.HLP
WINDOWS 4:PRINTMAN.HLP 4:PROGMAN.HLP 4:RECORDER.HLP 5:REVERSI.HLP 4:SOL.HLP
WINDOWS 4:TERMINAL.HLP 4:WINFILE.HLP 4:WRITE.HLP
WINDOWS 5:PYRAMID.BMP 5:CHESS.BMP 5:WEAVE.BMP 5:BOXES.BMP        [win.bmps]
WINDOWS 5:PAPER.BMP 5:PARTY.BMP 5:RIBBONS.BMP
WINDOWS 5:README.TXT 5:NETWORKS.TXT 5:PRINTERS.TXT               [win.readme]
WINDOWS 5:WININI.TXT 5:WININI2.TXT 5:SYSINI.TXT 5:SYSINI2.TXT
WINDOWS 5:SYSINI3.TXT 5:3270.TXT
WINDOWS/SYSTEM 1:KERNEL.EXE 1:GDI.EXE 1:USER.EXE 1:WIN.CNF     [windows.system]
WINDOWS/SYSTEM 3:WINOLDAP.MOD 2:LZEXPAND.DLL 3:WIN87EM.DLL       [win.other]
WINDOWS/SYSTEM 3:SYSEDIT.EXE
WINDOWS/SYSTEM 4:KRNL386.EXE 4:WINOA386.MOD 4:SWAPFILE.EXE       [pwin386]
WINDOWS/SYSTEM 4:WIN386.PS2 4:WIN386.EXE
WINDOWS/SYSTEM 5:KRNL286.EXE 5:WINOA286.MOD 5:DOSX.EXE           [pwin286]
WINDOWS/SYSTEM 2:VGA.DRV 5:VGACOLOR.GR2 5:VGA.GR3                [display] vga
WINDOWS/SYSTEM 2:VGALOGO.LGO 2:VGALOGO.RLE
WINDOWS/SYSTEM 2:CGA.DRV 5:CGA.GR2 2:CGASYS.FON 2:CGAFIX.FON     [display] cga
WINDOWS/SYSTEM 2:CGAOEM.FON
WINDOWS/SYSTEM 5:CGA40WOA.FON 5:CGA80WOA.FON 5:EGA40WOA.FON      [VGA.GR3]
WINDOWS/SYSTEM 5:EGA80WOA.FON
WINDOWS/SYSTEM 2:KEYBOARD.DRV                                [keyboard.drivers]
WINDOWS/SYSTEM 2:NOMOUSE.DRV                                  [pointing.device]
WINDOWS/SYSTEM 2:SYSTEM.DRV 2:SOUND.DRV 2:COMM.DRV               [system]
WINDOWS/SYSTEM 2:VGASYS.FON 2:VGAFIX.FON 2:VGAOEM.FON [sys/fixed/oemfonts]
WINDOWS/SYSTEM 6:HELVE.FON 6:COURE.FON 6:TMSRE.FON 5:SYMBOLE.FON [fonts]
WINDOWS/SYSTEM 6:ROMAN.FON 6:SCRIPT.FON 6:MODERN.FON
WINDOWS/SYSTEM 7:TTY.DRV 7:TTY.HLP          [io.device] Generic / Text Only
'
install_wfw311='
WINDOWS 1:SETUP.HLP 1:SETUP.TXT 1:WINSETUP.EXE 1:WINHELP.EXE     [windows]
WINDOWS 1:CONTROL.HLP
WINDOWS 6:CONTROL.SRC=CONTROL.INI 6:WINVER=WINVER.EXE 6:WININIT.EXE [net]
WINDOWS 6:HIMEM.SYS 4:EMM386.EXE 6:SMARTDRV.EXE   [win.devices.win386]
WINDOWS 6:RAMDRIVE.SYS 6:IFSHLP.SYS
WINDOWS 4:PROGMAN.EXE 6:TASKMAN.EXE 3:WINFILE.EXE 4:CLIPBRD.EXE  [win.shell]
WINDOWS 6:CLIPSRV.EXE 6:CONTROL.EXE 5:PIFEDIT.EXE 5:REGEDIT.EXE
WINDOWS 4:PRINTMAN.EXE 3:MSD.EXE 6:MSD.INI 3:WRITE.EXE 5:WRITE.HLP
WINDOWS 4:MORICONS.DLL
WINDOWS t:TCP32UI.DLL t:ARP.EXE t:PING.EXE t:ROUTE.EXE        [tcpip_install]
WINDOWS t:TRACERT.EXE t:HOSTS.SAM t:LMHOSTS.SAM t:SERVICES t:PROTOCOL
WINDOWS t:NETWORKS t:IPCONFIG.EXE t:FTP.EXE t:TELNET.EXE t:NETSTAT.EXE
WINDOWS t:NBTSTAT.EXE t:README.TXT t:MTCPIP32.HLP t:WINSOCK.DLL
WINDOWS t:TELNET.HLP t:MSTCP32.DEF
WINDOWS/SYSTEM 1:GDI.EXE 1:USER.EXE 1:WIN.CNF 2:LZEXPAND.DLL [windows.system]
WINDOWS/SYSTEM 2:VER.DLL 2:SHELL.DLL 1:WFWSETUP.DLL 1:COMMDLG.DLL
WINDOWS/SYSTEM 2:COMMCTRL.DLL 1:NCDW.DLL 2:SCONFIG.DLL
WINDOWS/SYSTEM 1:KRNL386.EXE 1:SETUP.INF                 [winexec, keepers]
WINDOWS/SYSTEM 2:CPWIN386.CPL                          [windows.system.386]
WINDOWS/SYSTEM 5:WINOA386.MOD 6:WIN386.PS2 2:WIN386.EXE          [pwin386]
WINDOWS/SYSTEM 5:VTDAPI.386 5:VFAT.386 6:VCACHE.386 4:IFSMGR.386
WINDOWS/SYSTEM 6:VSHARE.386 6:VCOMM.386 6:SERIAL.386 6:LPT.386
WINDOWS/SYSTEM 6:VPMTD.386 6:IOS.386 6:VXDLDR.386 4:RMM.D32
WINDOWS/SYSTEM 4:AB.DLL 5:DEMILAYR.DLL 2:FRAMEWRK.DLL 5:MAILMGR.DLL [mapi]
WINDOWS/SYSTEM 4:MAILSPL.EXE 4:MAPI.DLL 4:CMC.DLL 3:MSSFS.DLL
WINDOWS/SYSTEM 5:PABNSP.DLL 6:SENDFILE.DLL 3:STORE.DLL 3:VFORMS.DLL
WINDOWS/SYSTEM 6:WIN87EM.DLL 6:SYSEDIT.EXE 6:NDDEAPI.DLL      [win.other]
WINDOWS/SYSTEM 4:OLECLI.DLL 6:OLESVR.DLL 6:WSWAP.EXE 4:MIDIMAP.DRV
WINDOWS/SYSTEM 6:MIDIMAP.CFG 6:SND.CPL 4:MMSYSTEM.DLL 6:MMTASK.TSK
WINDOWS/SYSTEM 5:TIMER.DRV 6:MCISEQ.DRV 6:MCIWAVE.DRV 6:TOOLHELP.DLL
WINDOWS/SYSTEM 3:MAIN.CPL 5:DRIVERS.CPL 6:SETUP.REG 5:CONTROL.INF
WINDOWS/SYSTEM 6:APPS.INF 5:DDEML.DLL 4:NETWORK.INF 6:WINNET.INF
WINDOWS/SYSTEM 6:LM21DRV.UPD 4:UNIDRV.DLL 5:UNIDRV.HLP
WINDOWS/SYSTEM 2:VGA.DRV 2:VGA.3GR 2:VGALOGO.LGO 2:VGALOGO.RLE [display] vga
WINDOWS/SYSTEM 2:CGA40WOA.FON 2:CGA80WOA.FON 2:EGA40WOA.FON     [VGA.3gr]
WINDOWS/SYSTEM 2:EGA80WOA.FON 1:DOSAPP.FON                [codepages] 437
WINDOWS/SYSTEM 2:KEYBOARD.DRV 2:NOMOUSE.DRV [keyboard.drivers, pointing.device]
WINDOWS/SYSTEM 2:SYSTEM.DRV 2:MMSOUND.DRV 2:COMM.DRV              [system]
WINDOWS/SYSTEM 2:VGASYS.FON 2:VGAFIX.FON 2:VGAOEM.FON [sys/fixed/oemfonts]
WINDOWS/SYSTEM 5:SSERIFE.FON 6:COURE.FON 5:SERIFE.FON 5:SYMBOLE.FON [fonts]
WINDOWS/SYSTEM 6:SMALLE.FON 6:ROMAN.FON 6:SCRIPT.FON 6:MODERN.FON
WINDOWS/SYSTEM 7:NDIS.386 7:NE2000.386                   [ndis3, ms$ne2000]
WINDOWS/SYSTEM t:WSASRV.EXE t:VIP.386 t:VDHCP.386 t:VTDI.386  [tcpip_install]
WINDOWS/SYSTEM t:VTCP.386 t:VNBT.386 t:WSOCK.386 t:WSTCP.386
'

# The files of C:\NET of the NetBIOS VM image that NET START reads.
NB_FILES='AM2100.DOS IFSHLP.SYS NDISHLP.SYS NET.EXE NET.MSG NETBIND.COM
NETH.MSG PROTMAN.DOS PROTMAN.EXE SHARES.PWL WFWSYS.CFG'

machine=win30
exts=
while getopts w:x: opt; do
    case $opt in
    w) machine=$OPTARG ;;
    x) [ -d "$OPTARG" ] || die "no such directory: $OPTARG"
       exts="$exts$(cd "$OPTARG" && pwd -P)
" ;;
    *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 0 ] || usage
win_machine "$machine"

need_tools mcopy mmd mdir mattrib mformat msexpand nasm qemu-system-i386 \
    dosbox-x flock sha256sum make python3
tcp32=
case $WIN_MACHINE in
win30|win30nb) install=$install_win30 diskdir=$WIN30_DIR ndisks=7
               expand=$expand_win30 ;;
wfw311) install=$install_wfw311 diskdir=$WFW311_DIR ndisks=8
        expand=$expand_wfw311
        tcp32=$(find_ci "$TCP32_DIR" TCP32B.EXE) ;;
esac
disks=$(n=1; while [ $n -le $ndisks ]; do
    find_ci "$diskdir" "DISK0$n.IMG"
    n=$((n + 1))
done)
[ -f "$WIN_FLOPPY" ] || die "boot floppy not found: $WIN_FLOPPY"

# ext_files DIR - the files of extension DIR, as sorted paths relative to
# it.
ext_files() {
    (cd "$1" && find . -type f | sed 's|^\./||' | LC_ALL=C sort)
}

# inputs - print the SHA-256 of the build's inputs (see above).
inputs() {
    {
        echo "$WIN_MACHINE"
        (cd "$here" && sha256sum mkwin.sh runwin.sh winlib.sh KWAJ.PY \
            ../shlib.sh MBR.ASM)
        (cd "$WIN_TPL" && sha256sum *.INI CONFIG.SYS AUTOEXEC.BAT)
        printf '%s\n' "$disks" | while IFS= read -r in_d; do
            sha256sum <"$in_d"
        done
        [ -z "$tcp32" ] || sha256sum <"$tcp32"
        sha256sum <"$WIN_FLOPPY"
        printf '%s' "$exts" | while IFS= read -r in_x; do
            echo "extension"
            ext_files "$in_x" | while IFS= read -r in_f; do
                printf '%s %s\n' "$(sha256sum <"$in_x/$in_f")" "$in_f"
            done
        done
        nasm -v
        mformat --version | head -n 1
    } | sha256sum | cut -d' ' -f1
}

# net_ext DIR - make DIR win30nb's extension (see above).
net_ext() {
    rm -rf "$1"
    mkdir -p "$1/NET"
    (
        here=$repo/TEST/INTEG
        . "$here/net330.sh"
        image_lock 8 s "$NET330_DIR"
        image_current ||
            die "no current NetBIOS VM image in $NET330_DIR; run mkimg.sh"
        for f in $NB_FILES; do
            mcopy -n -m -i "$(part_img "$BASE_IMG")" "::NET/$f" "$1/NET/$f"
        done
        sed "s/@COMPUTERNAME@/$WIN_NBNAME/g" "$here/SYSTEM.INI" |
            dos_text >"$1/NET/SYSTEM.INI"
        sed "s/@T1@/$NB_T1_B/g" "$here/PROTOCOL.INI" |
            dos_text >"$1/NET/PROTOCOL.INI"
    )
    for f in PCNETBM SETMACH; do
        nasm -f bin -o "$1/NET/$f.COM" "$here/$f.ASM"
    done
    printf 'LASTDRIVE=Z\nDEVICE=C:\\NET\\IFSHLP.SYS\n' >"$1/CONFIG.ADD"
    printf '%s\n' 'IF EXIST C:\RUN\NET.ON C:\NET\PCNETBM' \
        'IF ERRORLEVEL 1 C:\NET\NET START >C:\RUN\NETSTART.OUT' \
        'IF EXIST C:\RUN\NETUSE.BAT C:\COMMAND /C C:\RUN\NETUSE.BAT' \
        "IF EXIST C:\\RUN\\NET.ON C:\\NET\\SETMACH $WIN_NBNAME" \
        >"$1/AUTOEXEC.ADD"
}

mkdir -p "$WIN_IMGDIR"
exec 9>"$WIN_IMGDIR.lock"
flock 9
rm -f "$WIN_IMGDIR/CHECK.PNG" "$WIN_IMGDIR/CHECK.MON" "$WIN_IMGDIR/CHECK.RST"
if [ "$WIN_MACHINE" = win30nb ]; then
    net_ext "$WIN_IMGDIR/NET"
    exts="$WIN_IMGDIR/NET
$exts"
fi
key=$(inputs)
if [ -f "$WIN_IMGDIR/BASE.IMG" ] &&
    [ "$(cat "$WIN_IMGDIR/INPUTS.SHA" 2>/dev/null)" = "$key" ]; then
    echo "$prog: $WIN_IMGDIR/BASE.IMG is up to date"
    exit 0
fi
make -s -C "$repo" BIN/QEXIT.COM BIN/WIN/WSMOKE.EXE ||
    die "BIN/QEXIT.COM and BIN/WIN/WSMOKE.EXE could not be built"

work=$WIN_IMGDIR/build
rm -rf "$work"
mkdir -p "$work/disk/t" "$work/C/DOS" "$work/C/TEMP" "$work/C/WINDOWS/SYSTEM"
trap 'rm -rf "$work"' EXIT
on_signals

# --- the Windows files, as SETUP installs them ------------------------------
n=1
printf '%s\n' "$disks" | while IFS= read -r d; do
    mkdir "$work/disk/$n"
    mcopy -n -m -i "$d" '::*' "$work/disk/$n/"
    n=$((n + 1))
done
# TCP32B.EXE's members, 8.3 names each (DOS83_SET), with their times.
[ -z "$tcp32" ] ||
    python3 -I - "$DOS83_SET" "$tcp32" "$work/disk/t" <<'EOF'
import calendar, os, re, sys, zipfile
chars = '[' + sys.argv[1] + ']'
NAME83 = re.compile(r'^%s{1,8}(\.%s{1,3})?$' % (chars, chars))
with zipfile.ZipFile(sys.argv[2]) as z:
    for info in z.infolist():
        if not NAME83.match(info.filename):
            sys.exit('mkwin.sh: TCP32B.EXE holds %r' % info.filename)
        out = os.path.join(sys.argv[3], info.filename)
        with open(out, 'wb') as f:
            f.write(z.read(info))
        t = calendar.timegm(info.date_time + (0, 0, 0))
        os.utime(out, (t, t))
EOF
printf '%s\n' "$install" | sed 's/\[.*//' | while read -r dir files; do
    [ -n "$dir" ] || continue
    for f in $files; do
        name=${f#?:}
        name=${name%=*}
        dst=${f#*=}
        dst=${dst#?:}
        raw=$work/disk/${f%%:*}/$name
        if [ ! -f "$raw" ]; then
            case $name in
            *.???) raw=${raw%?}_ ;;
            *.*) raw=${raw}_ ;;
            *) raw=$raw._ ;;
            esac
        fi
        [ -f "$raw" ] || die "${f%%:*}:$name is not on its disk"
        case $(head -c 4 "$raw") in
        SZDD) img_expand "$raw" "$work/C/$dir/$dst"
              echo "$dir/$dst" >>"$work/EXPANDED" ;;
        KWAJ) [ "$(python3 -I "$here/KWAJ.PY" "$raw" "$work/C/$dir/$dst")" = \
                "$name" ] || die "$raw does not hold $name"
              echo "$dir/$dst" >>"$work/EXPANDED" ;;
        *) cp "$raw" "$work/C/$dir/$dst" ;;
        esac
        touch -r "$raw" "$work/C/$dir/$dst"
    done
done
got=$(cd "$work/C" && LC_ALL=C sort "$work/EXPANDED" | xargs sha256sum |
    sha256sum | cut -d' ' -f1)
[ "$got" = "$expand" ] ||
    die "the expanded files have SHA-256 $got, not the Windows set's $expand"
sys=$work/C/WINDOWS/SYSTEM
stamp=$(stat -c %Y "$sys/WIN.CNF")
cat "$sys/WIN.CNF" "$sys/VGALOGO.LGO" "$sys/VGALOGO.RLE" \
    >"$work/C/WINDOWS/WIN.COM"
inis=$(cd "$WIN_TPL" && ls *.INI)
for f in $inis; do
    dos_text "$WIN_TPL/$f" >"$work/C/WINDOWS/$f"
done
if [ "$WIN_MACHINE" = win30 ]; then
    sed "$cga_sed" "$here/SYSTEM.INI" | dos_text >"$work/C/WINDOWS/$WIN_CGAINI"
    [ "$(sed "$cga_sed" "$here/SYSTEM.INI" | diff "$here/SYSTEM.INI" - |
        grep -c '^>')" -eq "$(printf '%s\n' "$cga_sed" | wc -l)" ] ||
        die "SYSTEM.INI lacks a display line that $WIN_CGAINI changes"
    inis="$inis $WIN_CGAINI"
fi

# --- MS-DOS, the start-up files and the extensions ------------------------
mcopy -n -m -i "$WIN_FLOPPY" '::*' "$work/disk/"
dosfiles=$(mdir -a -b -i "$WIN_FLOPPY" :: | sed 's|^::/||')
check83 $dosfiles
bootfiles=$(printf '%s\n' "$dosfiles" | head -n 2)
dosfiles=$(printf '%s\n' "$dosfiles" | sed '1,2d' | grep -vx COMMAND.COM)
cp -p "$work/disk/COMMAND.COM" "$work/C/COMMAND.COM"
shell_patch "$work/C/COMMAND.COM" ||
    die "COMMAND.COM of $WIN_FLOPPY is not a shell this script patches"
touch -r "$work/disk/COMMAND.COM" "$work/C/COMMAND.COM"
for f in $dosfiles; do
    cp -p "$work/disk/$f" "$work/C/DOS/$f"
done
dos_text "$WIN_TPL/CONFIG.SYS" >"$work/C/CONFIG.SYS"
dos_text "$WIN_TPL/AUTOEXEC.BAT" >"$work/C/AUTOEXEC.BAT"
printf '%s' "$exts" | while IFS= read -r x; do
    ext_files "$x" | while IFS= read -r f; do
        check83 "$f"
        case $f in
        CONFIG.ADD) dos_text "$x/$f" >>"$work/C/CONFIG.SYS" ;;
        AUTOEXEC.ADD) dos_text "$x/$f" >>"$work/C/AUTOEXEC.BAT" ;;
        *) mkdir -p "$work/C/$(dirname "$f")"
           cp "$x/$f" "$work/C/$f"
           touch -d "@$stamp" "$work/C/$f" ;;
        esac
    done
done
printf 'IF EXIST C:\\%s\\RUN.BAT C:\\%s\\RUN\r\n' "$WIN_CTLDIR" "$WIN_CTLDIR" \
    >>"$work/C/AUTOEXEC.BAT"
for f in CONFIG.SYS AUTOEXEC.BAT WINDOWS/WIN.COM; do
    touch -d "@$stamp" "$work/C/$f"
done
for f in $inis; do
    touch -d "@$stamp" "$work/C/WINDOWS/$f"
done

# --- the disk ---------------------------------------------------------------
img=$work/BASE.IMG
part=$(win_part "$img")
truncate -s $((WIN_SECTORS * 512)) "$img"
# The MBR: the boot code, then one active partition entry (start CHS
# 0/1/1, type 01, end CHS, first sector, sector count) and 55h AAh.
nasm -f bin -o "$work/MBR.BIN" "$here/MBR.ASM"
dd if="$work/MBR.BIN" of="$img" conv=notrunc 2>/dev/null
lastcyl=$((WIN_CYLS - 1))
{ byte 128; byte 1; byte 1; byte 0; byte 1; byte $((WIN_HEADS - 1))
  byte $((WIN_SECS | ((lastcyl >> 8) << 6))); byte $lastcyl
  le32 $WIN_SECS; le32 $WIN_PART_SECTORS; } |
    dd of="$img" bs=1 seek=446 conv=notrunc 2>/dev/null
printf '\125\252' | dd of="$img" bs=1 seek=510 conv=notrunc 2>/dev/null
SOURCE_DATE_EPOCH=$stamp mformat -i "$part" -h $WIN_HEADS -s $WIN_SECS \
    -H $WIN_SECS -T $WIN_PART_SECTORS -c 8 -r 32 -L 12 -m 0xf8 -N 0 ::
# The floppy's boot sector with the new parameter block (bytes 0Bh to
# 1Dh, up to the 16-bit count of hidden sectors that MS-DOS 3.10 and 3.30
# read) and, where the machine's DOS keeps it (WIN_BOOTDRV), the BIOS
# drive it boots from, the first hard disk.
dd if="$WIN_FLOPPY" of="$work/BOOT.BIN" bs=512 count=1 2>/dev/null
dd if="$img" of="$work/BOOT.BIN" bs=1 skip=$((WIN_SECS * 512 + 11)) \
    seek=11 count=19 conv=notrunc 2>/dev/null
[ -z "$WIN_BOOTDRV" ] || byte 128 |
    dd of="$work/BOOT.BIN" bs=1 seek=$((WIN_BOOTDRV)) conv=notrunc 2>/dev/null
dd if="$work/BOOT.BIN" of="$img" bs=512 seek=$WIN_SECS conv=notrunc \
    2>/dev/null

# mcopy_sorted DIR - copy the files of $work/C/DIR (DIR empty for the
# root) to the same directory of C:, in name order.
mcopy_sorted() {
    ms_files=$(cd "$work/C/$1" && find . -maxdepth 1 -type f |
        sed 's|^\./||' | LC_ALL=C sort)
    check83 $ms_files
    [ -z "$ms_files" ] ||
        (cd "$work/C/$1" && mcopy -m -i "$part" $ms_files "::$1/")
}
for f in $bootfiles; do
    mcopy -m -i "$part" "$work/disk/$f" "::$f"
    mattrib -i "$part" +r +s +h "::$f"
done
mcopy_sorted ''
for d in $(cd "$work/C" && find . -mindepth 1 -type d | sed 's|^\./||' |
    LC_ALL=C sort); do
    check83 "$d"
    SOURCE_DATE_EPOCH=$stamp mmd -i "$part" "::$d"
    mcopy_sorted "$d"
done

# --- the boot check, then the new image replaces the old -------------------
for e in $WIN_EMUS; do
    mkdir "$work/$e"
    case $e in
    qemu) cp "$repo/BIN/WIN/WSMOKE.EXE" "$work/$e/"
          mode=${WIN_MODES##* } cmd=WSMOKE out=WSMOKE.OUT want='WSMOKE PASS' ;;
    *) mode=dos cmd='VER >VER.TXT' out=VER.TXT want=Version ;;
    esac
    sh "$here/runwin.sh" -w "$WIN_MACHINE" -e $e -i "$img" -m $mode \
        -t $CHECK_SECS -d "$WIN_IMGDIR/CHECK.PNG" "$work/$e" "$cmd" ||
        fail "the new image did not boot under $e"
    grep -q "$want" "$work/$e/$out" 2>/dev/null ||
        fail "the boot under $e brought no \"$want\" back in $out"
done
chmod a-w "$img"
mv -f "$img" "$WIN_IMGDIR/BASE.IMG"
printf '%s\n' "$key" >"$WIN_IMGDIR/INPUTS.SHA"
echo "$prog: built $WIN_IMGDIR/BASE.IMG"
