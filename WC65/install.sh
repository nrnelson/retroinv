#!/bin/sh
# install.sh - install Watcom C 6.5 from its unpacked distribution disks.
#
# usage: sh WC65/install.sh SRC_DIR DEST_DIR [RECON_DIR]
#
#   SRC_DIR    directory holding the disk contents as DISK1 ... DISK8,
#              normally $RETRO_DL/wc65 as TEST/INTEG/unpack.sh extracts it
#   DEST_DIR   installation directory, mounted as D: by WC65/runwc.sh
#              (WC65/runwc.sh reads it from WC65_DIR, default
#              $RETRO_DL/wc65/WATCOM)
#   RECON_DIR  directory holding the reconstructed CLIBS.LIB and CLIBC.LIB
#              (default: SRC_DIR/RECON)
#
# The distribution's own INSTALL.EXE is driven by the text file
# DISK1/INSTALL.SCR: "mkdir %2dir" creates a directory, "enter disk N"
# switches to disk N, and "copy %1src %2dst" copies a file from the current
# disk to the installation.  This script reads those three directives (and
# nothing else from the script) and performs them on the host, so the
# layout matches what INSTALL.EXE produces: BIN\ (WCC, WCG, WCL, WLINK,
# ...), H\ and H\SYS\ (headers), LIB\ (CLIB<m>.LIB, MATH<m>.LIB, ...) and
# SRC\.  Names are stored in upper case so they look the same from DOS.
# No program from the disks is run.
#
# On the only surviving copy of the disks the small- and compact-model
# run-time libraries, CLIBS.LIB and CLIBC.LIB, are about half overwritten
# by other files' data.  The script then copies over them a reconstruction
# of both, rebuilt from the intact medium-, large- and huge-model libraries
# with every surviving byte kept
# (https://github.com/nrnelson/watcom-c-6.5-restoration); each file must
# match its SHA-256 in WC65/CLIBREC.SHA, before anything is copied, so a
# bad one leaves DEST without the damaged libraries; WC65/runwc.sh checks
# them again before each build.
#
# Exit status: 0 when the compiler is installed; 1 when a file cannot be
# copied; 2 on a usage error, a missing or bad directive, a reconstructed
# library missing or not as WC65/CLIBREC.SHA pins it, or a key file
# missing after the copy; 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

[ $# -eq 2 ] || [ $# -eq 3 ] ||
    die "usage: sh WC65/install.sh SRC_DIR DEST_DIR [RECON_DIR]"
SRC=$1
DEST=$2
RECON=${3:-$SRC/RECON}
[ -f "$SRC/DISK1/INSTALL.SCR" ] || die "$SRC/DISK1/INSTALL.SCR not found"
while read -r want lib; do
    [ -f "$RECON/$lib" ] || die "$RECON/$lib not found"
    got=$(sha256sum < "$RECON/$lib" | cut -d' ' -f1)
    [ "$got" = "$want" ] ||
        die "$RECON/$lib has sha256 $got, not the reconstruction's $want"
done < "$here/CLIBREC.SHA"
mkdir -p "$DEST"
LIST=$DEST/.install.lst
trap 'rm -f "$LIST"' EXIT
on_signals

# Turn the directives into "D<TAB>dir", "C<TAB>disk<TAB>src<TAB>dst" lines;
# paths become upper case with '/' separators and no leading separator.
tr -d '\r' < "$SRC/DISK1/INSTALL.SCR" | awk '
    function norm(p) { gsub(/\\/, "/", p); sub(/^\/+/, "", p); return toupper(p) }
    BEGIN { disk = 1 }
    /^enter disk [0-9]+/ { disk = $3 + 0; next }
    /^mkdir %2/ { p = substr($2, 3); if (p != "") print "D\t" norm(p); next }
    /^copy %1[^ ]+ %2[^ ]+$/ {
        print "C\t" disk "\t" norm(substr($2, 3)) "\t" norm(substr($3, 3)) }
' > "$LIST"

count=0
while IFS="$(printf '\t')" read -r kind a b c; do
    case $kind in
        D)
            [ -n "$a" ] || die "empty directory name"
            case $a in *..*) die "bad directory $a" ;; esac
            mkdir -p "$DEST/$a" ;;
        C)
            [ -n "$a" ] && [ -n "$b" ] && [ -n "$c" ] ||
                die "copy directive with an empty field: disk '$a' '$b' -> '$c'"
            case $b$c in *..*) die "bad path $b -> $c" ;; esac
            [ -f "$SRC/DISK$a/$b" ] || die "missing DISK$a/$b"
            mkdir -p "$DEST/$(dirname "$c")"
            cp "$SRC/DISK$a/$b" "$DEST/$c"
            count=$((count + 1)) ;;
    esac
done < "$LIST"

while read -r _ lib; do
    cp "$RECON/$lib" "$DEST/LIB/$lib"
done < "$here/CLIBREC.SHA"

for f in BIN/WCC.EXE BIN/WCG.EXE BIN/WLINK.EXE H/STDIO.H LIB/CLIBS.LIB LIB/CLIBM.LIB; do
    [ -f "$DEST/$f" ] || die "$f was not installed"
done
echo "$prog: copied $count files and the reconstructed libraries into $DEST"
