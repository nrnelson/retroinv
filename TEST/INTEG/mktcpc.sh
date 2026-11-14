#!/bin/sh
# mktcpc.sh - build the MS-DOS 3.30 + MS Network Client 3.0 TCP/IP QEMU
# system disk from the NetBEUI one.
#
# usage: sh TEST/INTEG/mktcpc.sh [DOS330_DIR [MSCLIENT_DIR]]
#
#   The arguments are those of mkimg.sh, which is run first when the
#   NetBEUI image is missing or out of date.
#
# env: TCP330_DIR  output directory (default TEST/INTEG/vm/tcp330)
#      NET330_DIR  the NetBEUI image it starts from (net330.sh)
#
# Output, all under TCP330_DIR:
#   BASE.IMG     raw system disk C: (read-only backing file for the VMs)
#   PAYLOAD.IMG  blank formatted disk, copied per VM run as drive D:
#   INPUTS.SHA   the hash of the files the image is built from
#                (tcp_inputs of tcp330.sh)
#
# The NetBEUI image already holds DOS, the client and the security
# settings that its SETUP.EXE run created; this script copies it and adds,
# with mtools, the TCP/IP files of the unpacked client (NET330_DIR/msc)
# and the templates of TEST/INTEG/SMB/: SYSTEM.INI (the computer name
# TCP_COMPUTER) and PROTOCOL.INI (the PCnet card with TCPDRV.DOS, a fixed
# address on QEMU's user network, no NetBEUI), LMHOSTS (SMB_NAME at
# SMB_ADDR), NETUP.BAT, which starts the client, with ENTER.TXT, the empty
# password it logs on with, and an AUTOEXEC.BAT that runs NETUP.BAT.
# CONFIG.SYS and the PCI setup of the network card are the NetBEUI
# image's (see mkimg.sh).  No VM runs: the security settings do not
# depend on the transport, and smbprobe.sh proves the image.  Of the
# client's TCP/IP programs, NMTSR.EXE (name defence), EMSBFR.EXE and
# UMB.COM (buffers in EMS and upper memory) are left out: a client that
# only connects to a server named in LMHOSTS needs none of them, and
# together they take 15 KB of conventional memory.
#
# Everything is built in TCP330_DIR.new and moved into place only at the
# end, so a failed rebuild leaves the previous image untouched.  One build
# runs at a time per tree: a second one waits up to lock_wait seconds for
# the first and ends there when the image is then current.
#
# Exit status: 0 when the images are built; 2 on a setup error; that of
# mkimg.sh when it fails; the status of any other command that fails.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/tcp330.sh"

marker=.tcp330
# The longest wait for another build, which may run mkimg.sh too.
lock_wait=1800

need_tools mcopy sha256sum flock realpath
mkdir -p "$here/vm"
exec 9>"$here/vm/.tcp330.lock"
if ! flock -n 9; then
    echo "$prog: waiting for another mktcpc.sh" >&2
    flock -w "$lock_wait" 9 ||
        die "another mktcpc.sh still runs after $lock_wait s"
    if tcp_current; then
        echo "$prog: $TCP330_DIR is current"
        exit 0
    fi
fi

image_current || sh "$here/mkimg.sh" "$@" 9>&-
# The image is built from the NetBIOS image's files.
image_lock 8 s "$NET330_DIR"
inputs=$(tcp_inputs)

dest=$(realpath -m "$TCP330_DIR")
case $dest in
    /|"$HOME"|"$(realpath -m "$HOME")"|"$repo")
        die "refusing TCP330_DIR=$TCP330_DIR" ;;
esac
for d in "$dest" "$dest.new"; do
    if [ -e "$d" ] && [ ! -f "$d/$marker" ]; then
        die "$d exists and was not made by mktcpc.sh"
    fi
done
new="$dest.new"
work="$new/build"
base="$new/BASE.IMG"

rm -rf "$new"
mkdir -p "$work"
: >"$new/$marker"
cp --sparse=always "$BASE_IMG" "$base"
cp --sparse=always "$PAYLOAD_TEMPLATE" "$new/PAYLOAD.IMG"
chmod u+w "$base"
c=$(part_img "$base")

# mkimg.sh keeps the "_" name of an expanded file whose SZDD header lacks
# the last character of its real name, as the TCP/IP programs' do.
for f in TCPDRV.DOS NEMM.DOS TCPTSR.EXE TINYRFC.EXE TCPUTILS.INI; do
    src="$NET330_DIR/msc/$f"
    [ -f "$src" ] || src="$NET330_DIR/msc/${f%?}_"
    [ -f "$src" ] || die "client file missing: $f"
    mcopy -o -i "$c" "$src" "::NET/$f"
done
sed -e "s/@NAME@/$SMB_NAME/g" -e "s/@ADDR@/$SMB_ADDR/g" "$here/SMB/LMHOSTS" |
    dos_text >"$work/LMHOSTS"
printf '\r\n' >"$work/ENTER.TXT"
for f in SYSTEM.INI PROTOCOL.INI NETUP.BAT AUTOEXEC.BAT; do
    sed "s/@COMPUTERNAME@/$TCP_COMPUTER/g" "$here/SMB/$f" |
        dos_text >"$work/$f"
done
for f in SYSTEM.INI PROTOCOL.INI NETUP.BAT LMHOSTS ENTER.TXT; do
    mcopy -o -i "$c" "$work/$f" "::NET/$f"
done
mcopy -o -i "$c" "$work/AUTOEXEC.BAT" ::AUTOEXEC.BAT

chmod a-w "$base"
rm -rf "$work"
printf '%s\n' "$inputs" >"$new/${TCP_INPUTS_SHA##*/}"
rm -rf "$dest"
mv "$new" "$dest"
echo "$prog: built $dest/BASE.IMG and $dest/PAYLOAD.IMG"
