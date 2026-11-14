#!/bin/sh
# mkwga.sh - build the MS-DOS 5.00 + Workgroup Add-On for MS-DOS QEMU
# system disk, a NetBEUI peer server.
#
# usage: sh TEST/INTEG/mkwga.sh [DOS500_DIR [WGA_DIR]]
#
#   DOS500_DIR  directory with the MS-DOS 5.00 720 KB disks DISK01.IMG,
#               DISK02.IMG and DISK03.IMG, in any letter case (env
#               DOS500_DIR; default that of TEST/shlib.sh, the "dos500"
#               package under RETRO_DL)
#   WGA_DIR     directory with the Workgroup Add-On 1.44 MB disk
#               DISK01.IMG, in any letter case (env WGA_DIR; default the
#               "wgaddon" package under RETRO_DL)
#
# env: TIMEOUT       host-side limit in seconds for each install VM (default
#                    300)
#      WGA500_DIR    output directory (wga500.sh)
#      NET330_DIR    the NetBIOS image it borrows from (net330.sh); built
#                    first with mkimg.sh's settings when missing or out of
#                    date
#      RETRO_DL      root of the period downloads (default $HOME/retro-dl)
#      SYSLINUX_MBR  the SYSLINUX MBR boot code written to the disk
#                    (default /usr/lib/syslinux/mbr/mbr.bin, from the
#                    syslinux-common package)
#
# Output, all under WGA500_DIR:
#   BASE.IMG     raw system disk C: (read-only backing file for the VMs)
#   PAYLOAD.IMG  blank formatted disk, copied per VM run as drive D: (the
#                NetBIOS image's)
#   INPUTS.SHA   the hash of the files the image is built from
#                (wga_inputs of wga500.sh)
#
# Everything is built in WGA500_DIR.new and moved into place only after the
# final checks, so a failed rebuild leaves the previous image untouched.
# The build holds the image's lock exclusive (image_lock of net330.sh), so
# that it waits for the runs booting the image and for another build, and
# it ends there when the image is then current; it holds the NetBIOS
# image's lock shared while it reads that image.
# SIGHUP, SIGINT and SIGTERM kill the install VM and leave WGA500_DIR.new
# behind for inspection.
#
# Exit status: 0 when the images are built; 2 on a setup error and when an
# install VM or a check of its result fails; that of mkimg.sh when it
# fails; the status of any other command that fails; 129, 130 or 143 on
# SIGHUP, SIGINT or SIGTERM.
#
# Steps:
#   1. The files of the three DOS disks and the Add-On disk are copied
#      out with mtools; each must have an 8.3 name and be at most
#      IMG_MAXFILE bytes (imglib.sh), and a name on two DOS disks must
#      have one content.  DOS 5.00
#      compresses most of its files (SZDD) under a name whose last
#      character is "_" and, unlike the Network Client, keeps no trace of
#      the real one in the header; the real name is the one name in disk
#      1's SETUP.INI that differs from it only in that last character.
#      msexpand expands them, each checked against the length its header
#      records.
#   2. An empty disk gets a partition table (sfdisk) and the SYSLINUX MBR
#      boot code, which boots the active partition.
#   3. QEMU boots a scratch copy of DOS disk 1, whose AUTOEXEC.BAT runs
#      DOS's own FORMAT C: /S (answering the confirmation from a file),
#      writes the marker C:\DONE.TXT and ends the VM with QEXIT.  QEMU
#      exits with status 1 both for QEXIT 0 and when it cannot start, so
#      the marker proves the VM ran.
#   4. mtools copies the DOS files to C:\DOS and the Add-On programs
#      wga_files, expanded the same way, to C:\NET, with CONFIG.SYS,
#      AUTOEXEC.BAT and PROTOCOL.INI of this directory (SHARE, NetBEUI on
#      the PCnet card, as on the NetBIOS VMs), WGA/SYSTEM.INI (file
#      sharing and the peer server on, the computer name WGA_COMPUTER)
#      and the PCI setup script PCNET.SCR (see img_config of imglib.sh).
#   5. The Add-On's own SETUP.EXE is run from a scratch D: holding its
#      disk's files, driven through the QEMU monitor by qmon.py
#      (img_setup of imglib.sh) as mkimg.sh drives the Network Client's:
#      it accepts the existing installation, copies the rest of its files
#      into C:\NET and creates the security-settings file WFWSYS.CFG and
#      SHARES.PWL, without which NET.EXE refuses every command.  The
#      templates are then copied in again, replacing the versions SETUP
#      rewrote.
#   6. The shells C:\COMMAND.COM and C:\DOS\COMMAND.COM are made to reload
#      their transient part after every program, and a scratch copy of the
#      disk boots once to prove it (img_shell of imglib.sh).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/wga500.sh"
. "$here/imglib.sh"

DOS500_DIR=${1:-$DOS500_DIR}
WGA_DIR=${2:-${WGA_DIR:-\
"$RETRO_DL/wgaddon/Microsoft Workgroup Add-On for MS-DOS (3.5)"}}
TIMEOUT=${TIMEOUT:-300}
SYSLINUX_MBR=${SYSLINUX_MBR:-/usr/lib/syslinux/mbr/mbr.bin}

qexit="$repo/BIN/QEXIT.COM"
shellchk="$repo/BIN/SHELLCHK.COM"
# The Add-On files installed in C:\NET before its SETUP.EXE runs, which
# otherwise takes the installation for a new one and asks for the card.
wga_files='NET.EXE NET.MSG NETH.MSG PROTMAN.DOS PROTMAN.EXE AM2100.DOS
    IFSHLP.SYS NDISHLP.SYS'

is_uint "$TIMEOUT" && [ "$TIMEOUT" -gt 0 ] ||
    die "TIMEOUT must be a positive integer"
need_tools qemu-system-i386 sfdisk mcopy mmd mdir mdel mtype msexpand \
    python3 timeout truncate realpath flock make sha256sum
dos1=$(find_ci "$DOS500_DIR" DISK01.IMG)
dos2=$(find_ci "$DOS500_DIR" DISK02.IMG)
dos3=$(find_ci "$DOS500_DIR" DISK03.IMG)
wga_disk=$(find_ci "$WGA_DIR" DISK01.IMG)
[ -f "$SYSLINUX_MBR" ] ||
    die "MBR boot code not found: $SYSLINUX_MBR (apt install syslinux-common)"
image_lock 9 x "$WGA500_DIR"
if [ "$il_waited" -eq 1 ] && wga_current; then
    echo "$prog: $WGA500_DIR is current"
    exit 0
fi
make -s -C "$repo" BIN/QEXIT.COM BIN/SHELLCHK.COM ||
    die "BIN/QEXIT.COM or BIN/SHELLCHK.COM could not be built"

out_check "$WGA500_DIR" .wga500 BASE.IMG >/dev/null || exit 2
image_current || sh "$here/mkimg.sh" 9>&-
# The NetBIOS image lends its payload disk to the build.
image_lock 8 s "$NET330_DIR"
inputs=$(wga_inputs)

img_begin "$WGA500_DIR" .wga500
base="$new/BASE.IMG"
mkdir -p "$work/dos" "$work/raw" "$work/wga"

# take_disk IMAGE DIR - copy the files of the floppy IMAGE into DIR, each
# with an 8.3 name and at most IMG_MAXFILE bytes; a name DIR already holds
# must have the same contents.
take_disk() {
    rm -rf "$work/disk"
    mkdir "$work/disk"
    [ -n "$(mdir -b -i "$1" ::)" ] || die "no files on $1"
    mcopy -n -i "$1" '::*' "$work/disk/"
    for f in "$work/disk"/*; do
        n=${f##*/}
        is_83 "$n" || die "not an 8.3 name on $1: $n"
        [ "$(wc -c <"$f")" -le "$IMG_MAXFILE" ] ||
            die "file over $IMG_MAXFILE bytes on $1: $n"
        if [ -e "$2/$n" ]; then
            cmp -s "$f" "$2/$n" || die "two different files named $n"
        else
            mv "$f" "$2/$n"
        fi
    done
}

# --- 1. copy out the disks and expand MS-DOS ---------------------------------
for disk in "$dos1" "$dos2" "$dos3"; do
    take_disk "$disk" "$work/raw"
done
take_disk "$wga_disk" "$work/wga"
[ -f "$work/raw/SETUP.INI" ] || die "SETUP.INI missing from DOS disk 1"
python3 -I - "$work/raw" "$work/szdd.map" <<'PY'
import os, re, sys
raw, mapfile = sys.argv[1], sys.argv[2]
NAME = re.compile(rb"[a-z0-9]{1,8}\.[a-z0-9]{1,3}")
with open(os.path.join(raw, "SETUP.INI"), "rb") as f:
    names = set(n.decode().upper() for n in NAME.findall(f.read()))
with open(mapfile, "w") as out:
    for name in sorted(os.listdir(raw)):
        with open(os.path.join(raw, name), "rb") as f:
            head = f.read(14)
        if not (name.endswith("_") and head[:4] == b"SZDD"):
            out.write("%s\t%s\n" % (name, name))
            continue
        real = [n for n in names
                if len(n) == len(name) and n[:-1] == name[:-1]]
        if len(real) != 1:
            sys.exit("expand: no single name in SETUP.INI for %s" % name)
        out.write("%s\t%s\tSZDD\n" % (name, real[0]))
PY
while IFS='	' read -r raw final szdd; do
    is_83 "$final" || die "expand: bad name $final"
    case $final in IO.SYS|MSDOS.SYS|CONFIG.SYS|AUTOEXEC.BAT) continue ;; esac
    if [ -n "$szdd" ]; then
        img_expand "$work/raw/$raw" "$work/dos/$final"
    else
        cp "$work/raw/$raw" "$work/dos/$final"
    fi
done <"$work/szdd.map"
for f in FORMAT.COM SHARE.EXE DEBUG.EXE; do
    [ -f "$work/dos/$f" ] || die "DOS file missing after expanding: $f"
done
[ -f "$work/wga/SETUP.EXE" ] || die "SETUP.EXE missing from the Add-On disk"

# --- 2. a partitioned empty disk ---------------------------------------------
img_disk "$base"
c=$(part_img "$base")

# --- 3. DOS formats C: -------------------------------------------------------
cp "$dos1" "$work/A.IMG"
chmod u+w "$work/A.IMG"
printf 'Y\r\n' >"$work/YES.TXT"
dos_text >"$work/AUTOEXEC.BAT" <<'BAT'
FORMAT C: /S /V:WGA500 < A:\YES.TXT > NUL
ECHO OK> C:\DONE.TXT
QEXIT 0
BAT
mcopy -o -i "$work/A.IMG" "$work/AUTOEXEC.BAT" ::AUTOEXEC.BAT
mcopy -o -i "$work/A.IMG" "$work/YES.TXT" ::YES.TXT
mcopy -o -i "$work/A.IMG" "$qexit" ::QEXIT.COM

img_run_vm install \
    -drive file="$work/A.IMG",format=raw,if=floppy,index=0 \
    -drive file="$base",if=none,id=hd0,format=raw \
    -device "$(hd_device hd0 0)" \
    -boot a -net none -monitor none
img_end_vm install "$c" ::DONE.TXT
mdir -a -i "$c" ::IO.SYS >/dev/null 2>&1 ||
    die "FORMAT C: /S did not make C: bootable"
mdel -i "$c" ::DONE.TXT

# --- 4. DOS files and configuration ------------------------------------------
mmd -i "$c" ::DOS ::NET
mcopy -i "$c" "$work/dos"/* ::DOS/
for f in $wga_files; do
    [ -f "$work/wga/${f%?}_" ] || die "missing from the Add-On disk: ${f%?}_"
    img_expand "$work/wga/${f%?}_" "$work/$f"
    mcopy -i "$c" "$work/$f" "::NET/$f"
done
img_config "$c" "$here/WGA/SYSTEM.INI" "$WGA_COMPUTER"

# --- 5. SETUP.EXE installs the Add-On ----------------------------------------
cp --sparse=always "$PAYLOAD_TEMPLATE" "$work/SETUP.IMG"
chmod u+w "$work/SETUP.IMG"
s=$(part_img "$work/SETUP.IMG")
mcopy -i "$s" "$work/wga"/* ::
img_setup "$base" "$work/SETUP.IMG" \
    "wait:Welcome to Setup for Microsoft Workgroup Add-On" \
    "press:ret:Setup will place your Workgroup Add-On files" \
    "press:ret:The listed options are correct." \
    "press:ret:Workgroup Add-On is now installed" \
    "leave:f3:Workgroup Add-On is now installed"
for f in NET.EXE WFWSYS.CFG SHARES.PWL; do
    mdir -i "$c" "::NET/$f" >/dev/null 2>&1 ||
        die "SETUP.EXE did not install C:\\NET\\$f"
done
img_config "$c" "$here/WGA/SYSTEM.INI" "$WGA_COMPUTER"

# --- done: replace the previous image ---------------------------------------
cp --sparse=always "$PAYLOAD_TEMPLATE" "$new/PAYLOAD.IMG"
img_finish "$inputs"
