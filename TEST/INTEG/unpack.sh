#!/bin/sh
# unpack.sh - extract the period software archives you supply.
#
# usage: sh TEST/INTEG/unpack.sh SRC_DIR DEST_DIR
#
#   SRC_DIR   directory holding the archives, under the file names listed
#             below
#   DEST_DIR  directory that receives one directory per package,
#             normally $RETRO_DL, the root the build and test scripts
#             read (default $HOME/retro-dl)
#
# Each package of the table is either needed, by the make targets its row
# names (and by the parts of those that read it, such as the gates integ
# runs), or optional ("-"), read by no build or test; a missing optional
# archive is reported as skipped and leaves the exit status alone.
#
# The archives are untrusted data: they are only unpacked, never executed
# on the host, and an archive holding a symbolic link, an absolute path or
# a ".." component is refused.  Each package is extracted into a
# temporary directory and renamed into place only on success, so an
# interrupted run never leaves a half-extracted package behind; a package
# whose directory already exists is left as it is.
#
# Exit status: 0 when every needed archive was extracted or already was;
# 1 when a needed archive was missing, or any archive supplied was refused
# or failed to extract; 2 on a usage error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../shlib.sh"
[ $# -eq 2 ] && [ -n "$1" ] && [ -n "$2" ] ||
    die "usage: unpack.sh SRC_DIR DEST_DIR"
src=$1
dest=$2

# package-directory|the make targets that need it, or - when none does|archive
map='
tc201|gate integ tcp dist|Borland Turbo C 2.01 (1989) (3.5-720k).7z
tc20man|-|Borland Turbo C 2.0 Manual.7z
tc10|-|Borland Turbo C 1.0 (5.25).7z
tc15|-|Borland Turbo C 1.5 (5.25).7z
tcpp101|-|Borland Turbo C++ 1.01 (3.5).7z
tcpp30|-|Borland Turbo CPP 3.0 (5.25-1.2mb).7z
dbase10|-|dBase III Plus 1.0 (Unprotected) (5.25-360k).7z
dbase11|gate|dBase III Plus 1.1 (1987) (3.5-720k).7z
dbase11man|-|dBase III Plus 1.1 (1986) Manuals.7z
fox10|-|Fox Software FoxPro 1.00 (1989) (5.25-360k).7z
fox102|-|Fox Software FoxPro 1.02 (1990) (5.25-360k).7z
fox20|-|Microsoft Foxpro 2.0 for DOS (3.5).7z
fox25a|-|Microsoft Foxpro 2.5a for DOS (3.5).7z
fox26a|-|Microsoft FoxPro 2.6a Professional (3.5).7z
dos310|gate tcp distcheck|Microsoft MS-DOS 3.10 [Compaq OEM Rev C] (1986-03-27) (5.25-360k).7z
dos330|integ tcp wintcp distcheck|Microsoft MS-DOS 3.30 (5.25).7z
dos331|-|Microsoft DOS 3.31 (3.5).7z
dos500|integ|Microsoft MS-DOS 5.00 (3.5-720k).7z
msnet10|-|Microsoft MS-Net 1.00 (Beta 3.0 7-3-1984) (5.25-360k).7z
wgaddon|integ|Microsoft Workgroup Add-On for MS-DOS (3.5).7z
wgconn|-|Microsoft Workgroup Connection (3.5).7z
msclient|integ tcp wintcp|msnetwork_client_3-0.zip
wc65|gate integ tcp dist|Watcom C.ver.6.5.English.zip
win30|gate|Microsoft Windows 3.0 (3.5 720K).7z
win30a|-|Microsoft Windows 3.0a (3.5 720K).7z
win30sdk|winhelp winhelpchk dist|Microsoft Windows 3.0 SDK (1990) (5.25-1.2mb).7z
wfw311|wintcp|Microsoft Windows for Workgroups 3.11 (OEM) (3.5-1.44mb).7z
tcp32b|wintcp|Microsoft TCP-IP-32 for Windows for Workgroups 3.11 (3.11b) (1995).7z
'

# Entries that could write outside the destination directory.
unsafe='^Path = (/|\.\.(/|$)|.*/\.\.(/|$))|^Attributes = .*l|^Symbolic Link = .'

status=0
# The loop reads a here-document, not a pipe, so it runs in this shell and
# can set status; 7z gets /dev/null as stdin so it cannot consume the list
# (for example as a password prompt).
while IFS='|' read -r pkg need file; do
    [ -n "$pkg" ] || continue
    if [ -d "$dest/$pkg" ]; then
        echo "exists:  $pkg"
        continue
    fi
    if [ ! -f "$src/$file" ]; then
        if [ "$need" = - ]; then
            echo "skipped: $file (optional)"
        else
            echo "missing: $file (needed by make $need)" >&2
            status=1
        fi
        continue
    fi
    # The listing starts with a header describing the archive itself; the
    # entries follow the first line of dashes.
    if 7z l -slt -- "$src/$file" </dev/null | sed '1,/^----------/d' | grep -Eq "$unsafe"; then
        echo "refused: $file (link, absolute path or ..)" >&2
        status=1
        continue
    fi
    tmp="$dest/.$pkg.tmp"
    rm -rf -- "$tmp"
    mkdir -p -- "$tmp"
    if 7z x -y -bso0 -bsp0 -o"$tmp" -- "$src/$file" </dev/null; then
        mv -- "$tmp" "$dest/$pkg"
        echo "ok:      $pkg"
    else
        rm -rf -- "$tmp"
        echo "failed:  $file" >&2
        status=1
    fi
done <<END_OF_MAP
$map
END_OF_MAP
exit $status
