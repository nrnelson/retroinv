# winlib.sh - shared settings of the Windows test machines.  Sourced by
# mkwin.sh, runwin.sh, winnb.sh and winmbx.sh after they set $here to
# this directory and $repo to the repository; not run on its own.  It
# sources TEST/shlib.sh.
#
# Three machines, chosen by win_machine:
#   win30   MS-DOS 3.10 and Windows 3.0, in all three modes: the machine
#           of wintest
#   win30nb Windows 3.0 as win30, on MS-DOS 3.30, as MS Network Client
#           3.0 refuses an older DOS, with the client's NetBEUI on a PCnet
#           card (WIN_NIC), started before Windows, in standard and 386
#           enhanced mode, where the Windows programs run: the machine of
#           winnb
#   wfw311  MS-DOS 3.30, the oldest that Windows for Workgroups 3.11
#           starts on, and Windows for Workgroups 3.11, which runs in 386
#           enhanced mode only, with Microsoft TCP/IP-32 3.11b and its
#           Winsock bound to an NE2000 (WIN_NIC): the machine of wintcp
# Each has one disk, C:, a raw image of WIN_CYLS cylinders, WIN_HEADS
# heads and WIN_SECS sectors per track (16 MB): the boot code of MBR.ASM
# and one active primary partition, type 01, from cylinder 0 head 1 to
# the end, holding FAT12 in the layout MS-DOS 3.10's own FORMAT gives it
# (8 sectors per cluster, 512 root entries).  MS-DOS 3.10 addresses a
# fixed disk with 17 sectors per track whatever INT 13h function 08h
# reports, so both emulators are given this geometry explicitly.
#
# env: WINIMG_DIR  directory of win30's built image, BASE.IMG and
#                  INPUTS.SHA (default OBJ/WIN30 of the repository)
#      WINNBIMG_DIR  the same of win30nb (default OBJ/WIN30NB)
#      WFWIMG_DIR  the same of wfw311 (default OBJ/WFW311)

WIN_CYLS=120
WIN_HEADS=16
WIN_SECS=17
WIN_SECTORS=$((WIN_CYLS * WIN_HEADS * WIN_SECS))
WIN_PART_SECTORS=$((WIN_SECTORS - WIN_SECS))

# The directories of C: that hold a run's files (the program's) and its
# control files (the batch file AUTOEXEC.BAT ends in, its markers and
# QEXIT.COM).
WIN_APPDIR=RUN
WIN_CTLDIR=RUNWIN
# The SYSTEM.INI of win30's 8086 machine, beside SYSTEM.INI in
# C:\WINDOWS.
WIN_CGAINI=SYSTEM.CGA
# win30nb's computer name, which mkwin.sh makes its DOS machine name too.
WIN_NBNAME=RIVMW

. "$repo/TEST/shlib.sh"

export MTOOLS_SKIP_CHECK=1
export MTOOLS_NO_VFAT=1
# mtools reads and writes FAT times as local time: one zone for every
# call keeps them the same on every host.
export TZ=UTC0

# win_machine NAME - choose test machine NAME, setting WIN_MACHINE to it,
# WIN_IMGDIR to its image directory, WIN_TPL to the directory of its
# templates, WIN_FLOPPY to the boot floppy of its DOS, WIN_BOOTDRV to the
# offset in that floppy's boot sector of the BIOS drive it boots from
# (empty when the boot code takes the drive the BIOS passes), WIN_MODES
# to the modes runwin.sh may start it in, WIN_EMUS to the emulators it
# runs under and WIN_NIC to the QEMU device of its network card (empty
# for none); stops with die on another NAME.  The NE2000's port and
# interrupt are those of the card's section of WFW/PROTOCOL.INI; the
# PCnet sits in PCI slot 5, where the BIOS gives it the I/O port and
# interrupt of TEST/INTEG/PROTOCOL.INI, as the NetBIOS VMs' card.
win_machine() {
    case $1 in
    win30)
        WIN_IMGDIR=$(realpath -m "${WINIMG_DIR:-$repo/OBJ/WIN30}")
        WIN_TPL=$here
        WIN_FLOPPY=$DOS310_FLOPPY
        WIN_BOOTDRV=
        WIN_MODES='r s 3'
        WIN_EMUS='qemu dosbox'
        WIN_NIC= ;;
    win30nb)
        WIN_IMGDIR=$(realpath -m "${WINNBIMG_DIR:-$repo/OBJ/WIN30NB}")
        WIN_TPL=$here
        WIN_FLOPPY=$DOS330_FLOPPY
        WIN_BOOTDRV=0x1FD
        WIN_MODES='s 3'
        WIN_EMUS=qemu
        WIN_NIC=pcnet,addr=5,mac=52:54:00:52:49:0c ;;
    wfw311)
        WIN_IMGDIR=$(realpath -m "${WFWIMG_DIR:-$repo/OBJ/WFW311}")
        WIN_TPL=$here/WFW
        WIN_FLOPPY=$DOS330_FLOPPY
        WIN_BOOTDRV=0x1FD
        WIN_MODES=3
        WIN_EMUS=qemu
        WIN_NIC=ne2k_isa,iobase=0x300,irq=5,mac=52:54:00:52:49:1c ;;
    *) die "unknown machine: $1" ;;
    esac
    WIN_MACHINE=$1
}

# win_part IMAGE - the mtools image spec of the partition inside IMAGE.
win_part() {
    printf '%s@@%s\n' "$1" $((WIN_SECS * 512))
}

# win_device ID - the QEMU -device option attaching drive ID as the first
# IDE disk with the geometry above.
win_device() {
    printf 'ide-hd,drive=%s,cyls=%s,heads=%s,secs=%s,bios-chs-trans=none\n' \
        "$1" "$WIN_CYLS" "$WIN_HEADS" "$WIN_SECS"
}
