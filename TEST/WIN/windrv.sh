#!/bin/sh
# windrv.sh - take a file from the Windows 3.0 setup disks, expanded: a
# printer driver that a test puts beside its program, to print on another
# printer than the test machine's.
#
# usage: sh TEST/WIN/windrv.sh NAME OUT
#
#   NAME  the file's name on its disk, such as EPSON9.DRV
#   OUT   the expanded file to write
#
# env: WIN30_DIR  directory of the seven Windows 3.0 720 KB disks
#                 DISK01.IMG to DISK07.IMG, as for mkwin.sh (default the
#                 "win30" package under RETRO_DL)
#      RETRO_DL   root of the period downloads (default $HOME/retro-dl)
#
# The disks hold the file compressed (SZDD) under its own name; OUT's
# length is checked against the one its header records.
#
# Exit status: 0; 2 on a usage or setup error, a file no disk holds among
# them; the status of any other command that fails.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../shlib.sh"

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

WIN30_DIR=${WIN30_DIR:-"$RETRO_DL/win30/Microsoft Windows 3.0 (3.5 720K)"}

[ $# = 2 ] || usage
need_tools mdir mcopy msexpand
disk=
n=1
while [ -z "$disk" ] && [ $n -le 7 ]; do
    d=$(find_ci "$WIN30_DIR" "DISK0$n.IMG")
    ! mdir -i "$d" "::$1" >/dev/null 2>&1 || disk=$d
    n=$((n + 1))
done
[ -n "$disk" ] || die "$1 is on no disk of $WIN30_DIR"
mcopy -n -i "$disk" "::$1" "$2.in"
img_expand "$2.in" "$2"
rm -f "$2.in"
