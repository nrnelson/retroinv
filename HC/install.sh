#!/bin/sh
# install.sh - install the help compiler HC.EXE of the Windows 3.0 SDK.
#
# usage: sh HC/install.sh IMG DEST_DIR
#
#   IMG       the SDK's disk 1, "Development Tools", a 1.2 MB disk image;
#             one known dump of it has the SHA-256
#             9028aa38020fdbbfbe8beb6af52238f11223ba75620b9db9a579ae3aee90785c,
#             but only the file is checked, so another dump does as well
#   DEST_DIR  installation directory, which must not exist yet, read by
#             HC/runhc.sh from HC_DIR (default $RETRO_DL/win30hc)
#
# mtools copies RESTOOLS\HC.EXE off the image into a scratch directory
# beside DEST_DIR, which becomes DEST_DIR only when the file matches
# HC/TOOLS.SHA.  Nothing from the disk runs on the host.
#
# Exit status: 0 when HC.EXE is installed; 1 when the image does not hold
# it as HC/TOOLS.SHA pins it; 2 on a usage error, a missing input or an
# existing DEST_DIR; 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

[ $# -eq 2 ] || die "usage: sh HC/install.sh IMG DEST_DIR"
IMG=$1
DEST=$2
need_tools mcopy sha256sum
[ -f "$IMG" ] || die "$IMG not found"
[ ! -e "$DEST" ] || die "$DEST exists; remove it first"
mkdir -p "$(dirname "$DEST")"
T=$(mktemp -d "$(cd "$(dirname "$DEST")" && pwd -P)/.hcinst.XXXXXX")
trap 'rm -rf "$T"' EXIT
on_signals
mcopy -m -n -i "$IMG" ::/RESTOOLS/HC.EXE "$T/" ||
    fail "$IMG holds no RESTOOLS\\HC.EXE"
(cd "$T" && sha256sum --quiet --strict -c "$here/TOOLS.SHA") ||
    fail "HC.EXE is not as HC/TOOLS.SHA pins it"
chmod 755 "$T"
mv "$T" "$DEST"
trap - EXIT
echo "$prog: installed HC.EXE into $DEST"
