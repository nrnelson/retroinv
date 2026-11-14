#!/bin/sh
# mkimg.sh - build the MS-DOS 3.30 + MS Network Client 3.0 QEMU system disk.
#
# usage: sh TEST/INTEG/mkimg.sh [DOS330_DIR [MSCLIENT_DIR]]
#
#   DOS330_DIR    directory with the MS-DOS 3.30 360 KB disks DISK01.IMG and
#                 DISK02.IMG (env DOS330_DIR; default that of TEST/shlib.sh,
#                 the "dos330" package under RETRO_DL)
#   MSCLIENT_DIR  directory with the Network Client 3.0 self-extracting disks
#                 DSK3-1.EXE and DSK3-2.EXE (env MSCLIENT_DIR; default the
#                 "msclient" package under RETRO_DL)
#
# env: TIMEOUT       host-side limit in seconds for each install VM (default
#                    300)
#      NET330_DIR    output directory (default TEST/INTEG/vm/net330)
#      RETRO_DL      root of the period downloads, as unpack.sh lays them
#                    out (default $HOME/retro-dl)
#      SYSLINUX_MBR  the SYSLINUX MBR boot code written to both disks
#                    (default /usr/lib/syslinux/mbr/mbr.bin, from the
#                    syslinux-common package)
#
# Output, all under NET330_DIR:
#   BASE.IMG     raw system disk C: (read-only backing file for the VMs)
#   PAYLOAD.IMG  blank formatted disk, copied per VM run as drive D:
#   msc/         the Network Client files, unpacked and expanded
#   INPUTS.SHA   the hash of the files the image is built from
#                (image_inputs of net330.sh)
#
# Everything is built in NET330_DIR.new and moved into place only after the
# final checks, so a failed rebuild leaves the previous image untouched.
# The build holds the image's lock exclusive (image_lock of net330.sh), so
# that it waits for the runs booting the image and for another build, and
# it ends there when the image is then current.
# SIGHUP, SIGINT and SIGTERM kill the install VM and leave NET330_DIR.new
# behind for inspection.
#
# Exit status: 0 when the images are built; 2 on a setup error and when an
# install VM or a check of its result fails; 1 when the client archives
# cannot be unpacked; the status of any other command that fails; 129, 130
# or 143 on SIGHUP, SIGINT or SIGTERM.
#
# Steps:
#   1. The two client archives are ZIP files behind a DOS extractor stub; the
#      ZIP members are read on the host (the stub is never run), each must
#      have an 8.3 name and be at most IMG_MAXFILE bytes (imglib.sh); a
#      name on both disks must have identical contents.  The
#      SZDD-compressed members are expanded with msexpand.  SZDD keeps the last character of
#      the real name in its header, which restores names such as
#      NET.EX_ -> NET.EXE; a member whose header holds no valid 8.3
#      character (some have 00h) is still expanded but keeps its "_" name.
#   2. Two empty disks get a partition table (sfdisk) and the SYSLINUX MBR
#      boot code, which boots the active partition.
#   3. QEMU boots a scratch copy of DOS 3.30 disk 1 with disk 2 as B:.  Its
#      AUTOEXEC.BAT runs DOS's own FORMAT C: /S and FORMAT D: (answering the
#      confirmation from a file), copies both floppies to C:\DOS, writes the
#      marker C:\DONE.TXT and ends the VM with QEXIT.  QEMU exits with status
#      1 both for QEXIT 0 and when it cannot start, so the marker (and, for
#      the SETUP VM below, D:\DONE.TXT) is what proves the VM ran.
#   4. The client files, the CONFIG.SYS, AUTOEXEC.BAT, SYSTEM.INI and
#      PROTOCOL.INI templates from this directory and the PCI setup script
#      PCNET.SCR (see img_config of imglib.sh) are copied in with mtools;
#      SYSTEM.INI gets the default computer name RIVMA and PROTOCOL.INI VM
#      A's NetBEUI timer NB_T1_A (net330.sh).
#   5. The client's own SETUP.EXE is run once over that installation, driven
#      through the QEMU monitor by qmon.py (it reads the screen from text
#      memory and presses ENTER on each confirmation screen, and F3 on the
#      last, each once more if SETUP lost it).  SETUP accepts the existing
#      settings and creates the binary security-settings file WFWSYS.CFG
#      and SHARES.PWL, without which NET.EXE refuses every command.  The
#      templates are then copied in again, replacing the versions SETUP
#      rewrote.
#   6. The shells C:\COMMAND.COM and C:\DOS\COMMAND.COM are made to reload
#      their transient part after every program, and a scratch copy of the
#      disk boots once to prove it (img_shell of imglib.sh).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/net330.sh"
. "$here/imglib.sh"

DOS330_DIR=${1:-$DOS330_DIR}
MSCLIENT_DIR=${2:-${MSCLIENT_DIR:-"$RETRO_DL/msclient/MSNetwork_Client_3.0"}}
TIMEOUT=${TIMEOUT:-300}
SYSLINUX_MBR=${SYSLINUX_MBR:-/usr/lib/syslinux/mbr/mbr.bin}

qexit="$repo/BIN/QEXIT.COM"
shellchk="$repo/BIN/SHELLCHK.COM"
# The client files installed in C:\NET.
client_files='NET.EXE NET.MSG NETH.MSG PROTMAN.DOS PROTMAN.EXE NETBIND.COM
    AM2100.DOS IFSHLP.SYS NDISHLP.SYS'

is_uint "$TIMEOUT" && [ "$TIMEOUT" -gt 0 ] || die "TIMEOUT must be a positive integer"
need_tools qemu-system-i386 sfdisk mcopy mmd mdir mdel mtype msexpand \
    python3 timeout truncate realpath flock make sha256sum
inputs=$(image_inputs)
for f in DISK01.IMG DISK02.IMG; do
    [ -f "$DOS330_DIR/$f" ] || die "not found: $DOS330_DIR/$f"
done
for f in DSK3-1.EXE DSK3-2.EXE; do
    [ -f "$MSCLIENT_DIR/$f" ] || die "not found: $MSCLIENT_DIR/$f"
done
[ -f "$SYSLINUX_MBR" ] || die "MBR boot code not found: $SYSLINUX_MBR (apt install syslinux-common)"
image_lock 8 x "$NET330_DIR"
if [ "$il_waited" -eq 1 ] && image_current; then
    echo "$prog: $NET330_DIR is current"
    exit 0
fi
make -s -C "$repo" BIN/QEXIT.COM BIN/SHELLCHK.COM ||
    die "BIN/QEXIT.COM or BIN/SHELLCHK.COM could not be built"

img_begin "$NET330_DIR" .net330
msc="$new/msc"
base="$new/BASE.IMG"
payload="$new/PAYLOAD.IMG"
mkdir -p "$msc/raw"

# --- 1. unpack and expand the Network Client -------------------------------
# The 8.3 rule is that of is_83: the character set DOS83_SET of
# TEST/shlib.sh, passed in and wrapped as a bracket expression.
python3 -I - "$DOS83_SET" "$IMG_MAXFILE" "$msc/raw" "$work/szdd.map" \
    "$MSCLIENT_DIR/DSK3-1.EXE" "$MSCLIENT_DIR/DSK3-2.EXE" <<'PY'
import os, re, sys, zipfile
chars, limit = "[" + sys.argv[1] + "]", int(sys.argv[2])
dest, mapfile = sys.argv[3], sys.argv[4]
NAME83 = re.compile(r"^%s{1,8}(\.%s{1,3})?$" % (chars, chars))
CHAR83 = re.compile(r"^%s$" % chars)
seen = {}
with open(mapfile, "w") as out:
    for arc in sys.argv[5:]:
        with zipfile.ZipFile(arc) as z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                name = re.split(r"[/\\]", info.filename)[-1].upper()
                if not NAME83.match(name):
                    sys.exit("unpack: member is not an 8.3 name: %r" % info.filename)
                if info.file_size > limit:
                    sys.exit("unpack: member over %d bytes: %s"
                             % (limit, name))
                data = z.read(info)
                if name in seen:
                    if seen[name] != data:
                        sys.exit("unpack: two different members named %s" % name)
                    continue
                seen[name] = data
                with open(os.path.join(dest, name), "wb") as f:
                    f.write(data)
                final = name
                if name.endswith("_") and data[:4] == b"SZDD":
                    ch = chr(data[9]).upper()
                    cand = name[:-1] + ch
                    if CHAR83.match(ch) and NAME83.match(cand):
                        final = cand
                    out.write("%s\t%s\tSZDD\n" % (name, final))
                else:
                    out.write("%s\t%s\n" % (name, final))
PY
while IFS='	' read -r raw final szdd; do
    is_83 "$final" || die "unpack: bad name $final"
    if [ -n "$szdd" ]; then
        img_expand "$msc/raw/$raw" "$msc/$final"
    else
        cp "$msc/raw/$raw" "$msc/$final"
    fi
done <"$work/szdd.map"
for f in $client_files; do
    [ -f "$msc/$f" ] || die "client file missing after unpack: $f"
done

# --- 2. partitioned empty disks ---------------------------------------------
img_disk "$base"
img_disk "$payload"
c=$(part_img "$base")
d=$(part_img "$payload")

# --- 3. DOS formats both disks and copies itself to C: ----------------------
cp "$DOS330_DIR/DISK01.IMG" "$work/A.IMG"
cp "$DOS330_DIR/DISK02.IMG" "$work/B.IMG"
chmod u+w "$work/A.IMG" "$work/B.IMG"
printf 'Y\r\n' >"$work/YES.TXT"
cat >"$work/AUTOEXEC.BAT" <<'BAT'
FORMAT C: /S < A:\YES.TXT > NUL
FORMAT D: < A:\YES.TXT > NUL
MD C:\DOS
COPY A:\*.* C:\DOS > NUL
COPY B:\*.* C:\DOS > NUL
ECHO OK> C:\DONE.TXT
QEXIT 0
BAT
dos_text "$work/AUTOEXEC.BAT" >"$work/AUTOEXEC.DOS"
mcopy -o -i "$work/A.IMG" "$work/AUTOEXEC.DOS" ::AUTOEXEC.BAT
mcopy -o -i "$work/A.IMG" "$work/YES.TXT" ::YES.TXT
mcopy -o -i "$work/A.IMG" "$qexit" ::QEXIT.COM

img_run_vm install \
    -drive file="$work/A.IMG",format=raw,if=floppy,index=0 \
    -drive file="$work/B.IMG",format=raw,if=floppy,index=1 \
    -drive file="$base",if=none,id=hd0,format=raw \
    -device "$(hd_device hd0 0)" \
    -drive file="$payload",if=none,id=hd1,format=raw \
    -device "$(hd_device hd1 1)" \
    -boot a -net none -monitor none
img_end_vm install "$c" ::DONE.TXT

mdir -a -i "$c" ::IO.SYS >/dev/null 2>&1 || die "FORMAT C: /S did not make C: bootable"
mdir -i "$c" ::DOS/SHARE.EXE >/dev/null 2>&1 || die "DOS files missing on C:"
mdir -i "$d" :: >/dev/null 2>&1 || die "FORMAT D: did not produce a FAT volume"
mdel -i "$c" ::DONE.TXT ::DOS/AUTOEXEC.BAT ::DOS/YES.TXT ::DOS/QEXIT.COM

# --- 4. Network Client and configuration ------------------------------------
mmd -i "$c" ::NET
for f in $client_files; do
    mcopy -i "$c" "$msc/$f" "::NET/$f"
done
img_config "$c" "$here/SYSTEM.INI" RIVMA

# --- 5. SETUP.EXE creates the security-settings files -----------------------
cp --sparse=always "$payload" "$work/SETUP.IMG"
s=$(part_img "$work/SETUP.IMG")
for f in "$msc/raw"/*; do
    mcopy -o -i "$s" "$f" "::$(basename "$f")"
done
img_setup "$base" "$work/SETUP.IMG" \
    "wait:Welcome to Setup" \
    "press:ret:Setup will place your Network Client files" \
    "press:ret:The listed options are correct." \
    "press:ret:Network Client is now installed" \
    "leave:f3:Network Client is now installed"
mdir -i "$c" ::NET/WFWSYS.CFG >/dev/null 2>&1 || die "SETUP.EXE did not create WFWSYS.CFG"
img_config "$c" "$here/SYSTEM.INI" RIVMA

# --- done: replace the previous image ---------------------------------------
img_finish "$inputs"
