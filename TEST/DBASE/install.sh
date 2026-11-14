#!/bin/sh
# install.sh - install dBASE III Plus 1.1 for headless use in DOSBox-X.
#
# usage: install.sh SRC DEST
#
#   SRC   directory holding the 720 KB "System Disk #1/#2" image
#         sys_1-2.img, or the image itself, from the package that
#         TEST/INTEG/unpack.sh extracts into $RETRO_DL/dbase11/.  Read only.
#   DEST  install directory to create (must not exist yet), normally
#         $RETRO_DL/dbase11/DBASE, where rundb.sh looks for it.  Never
#         inside the repository.
#
# The shipped disk holds DBASE.LOD, an encrypted executable that the
# disk's own ID.EXE turns into DBASE.EXE after asking for user name,
# company and serial number (the vendor's INSTALL.BAT runs ID the same
# way).  This script copies the files that INSTALL.BAT copies plus ID.EXE
# and its inputs, runs ID inside DOSBox-X with the answers typed through
# DOSBox-X's AUTOTYPE, and keeps only what INSTALL.BAT would have left on
# a hard disk.  It then writes the rig's CONFIG.DB (from this directory)
# and has dBASE itself create SEED.DBF, a one-field table (NAME C 10)
# typed into the interactive CREATE screen.  dBASE has no command that
# makes a table from nothing, and COPY STRUCTURE EXTENDED needs an open
# table, so the golden-file program starts from this seed.
#
# Environment:
#   DB_USER     user name typed into ID (default RETROINV)
#   DB_COMPANY  company name typed into ID (default RETROINV TEST RIG)
#   DB_SERIAL   serial number; default: the "Serial:" line of a *.txt
#               file next to the image (the archive's notes)
#   DB_TIMEOUT  seconds per DOSBox-X run (default 120)
#
# Only letters, digits, space, '-' and '.' can be typed.  Exit status 0 on
# success, 1 on failure (DEST is not created; the work is done in
# DEST.new, which is left for inspection), 2 on a usage or setup error,
# 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM (DOSBox-X is stopped and
# DEST.new removed).
set -eu

here=$(cd "$(dirname "$0")" && pwd -P)
. "$here/../shlib.sh"

[ $# -eq 2 ] || die "usage: install.sh SRC DEST"
src=$1 dest=$2
for t in dosbox-x mcopy timeout sha256sum; do
    command -v $t >/dev/null || die "$t not installed"
done

if [ -d "$src" ]; then
    img=$src/sys_1-2.img
    srcdir=$src
else
    img=$src
    srcdir=$(dirname "$src")
fi
[ -f "$img" ] || die "no disk image: $img"
# The only image this script was checked against.
want=c5bf081d19feed5283ca8a92b20a06bc4c0f4ffa6d5372bc8f37627c0766390a
got=$(sha256sum "$img" | cut -d' ' -f1)
[ "$got" = "$want" ] || die "unexpected image (sha256 $got)"

[ -e "$dest" ] && die "DEST already exists: $dest"
mkdir -p "$(dirname "$dest")"
dest=$(cd "$(dirname "$dest")" && pwd -P)/$(basename "$dest")
case $dest in
"$(cd "$here/../.." && pwd -P)"/*) die "DEST is inside the repository" ;;
*'|'*|*'&'*|*'"'*) die "DEST contains | & or \"" ;;
esac

user=${DB_USER:-RETROINV}
company=${DB_COMPANY:-RETROINV TEST RIG}
serial=${DB_SERIAL:-}
if [ -z "$serial" ]; then
    serial=$(cat "$srcdir"/*.txt 2>/dev/null | tr -d '\r' |
        sed -n 's/^Serial: *\([0-9-]*\).*/\1/p' | head -1)
fi
[ -n "$serial" ] || die "no serial number found; set DB_SERIAL"
timeout=${DB_TIMEOUT:-120}
case $user$company$serial in
*[!A-Za-z0-9\ .-]*) die "DB_USER, DB_COMPANY and DB_SERIAL may hold only A-Z a-z 0-9 space - ." ;;
esac

# keys TEXT: the DOSBox-X AUTOTYPE key names that type TEXT.
keys() {
    printf '%s\n' "$1" | fold -w1 | while IFS= read -r c; do
        case $c in
        [A-Za-z0-9]) printf ' %s' "$c" ;;
        ' '|'') printf ' space' ;;
        -) printf ' minus' ;;
        .) printf ' period' ;;
        esac
    done
}

work=$dest.new
rm -rf "$work"
mkdir -p "$work"
scratch=$(mktemp -d "${TMPDIR:-/tmp}/dbinst.XXXXXX")
pid=
# On a signal the running DOSBox-X is stopped and waited for before the
# scratch directory and DEST.new are removed.
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        pid=
    fi
    rm -rf "$scratch"
}
# drop SIGNAL - the signal handler (on_signals): stop DOSBox-X and remove
# the scratch directory (cleanup), then DEST.new.
drop() { cleanup; rm -rf "$work"; }
trap cleanup EXIT
on_signals drop

for f in DBASE.LOD DBASE.SER ID.EXE SIGNON.COM DBASE.MSG DBASEINL.OVL \
         DBASE.OVL HELP.DBS ASSIST.HLP; do
    mcopy -n -i "$img" "::/$f" "$work/$f" || fail "cannot copy $f from the image"
done
chmod u+w "$work"/*

# run NAME AUTOEXEC-LINES: one DOSBox-X run with DEST.new as C:.
run() {
    { cat "$here/DB.CONF"
      printf '\n[autoexec]\nmount c "%s" -nocachedir\nc:\n' "$work"
      printf '%s\n' "$2"
      printf 'exit\n'
    } > "$scratch/$1.conf"
    # DOSBox-X may crash on shutdown while AUTOTYPE is still armed; the
    # files each step must produce are checked instead of its status.
    cd "$scratch"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout -k 10 "$timeout" dosbox-x -conf "$scratch/$1.conf" \
        -nogui -nomenu -exit > "$scratch/$1.out" 2>&1 &
    pid=$!
    wait "$pid" 2>/dev/null || true
    pid=
    cd /
}

# ID: user name, company, serial, "re-edit? N", "press any key".
idkeys="$(keys "$user") enter$(keys "$company") enter$(keys "$serial") enter n enter"
run id "AUTOTYPE -w 3 -p 0.15$idkeys
ID"
[ -f "$work/DBASE.EXE" ] && [ ! -f "$work/DBASE.LOD" ] ||
    fail "ID did not produce DBASE.EXE (see $work)"
rm -f "$work/DBASE.SER" "$work/ID.EXE" "$work/SIGNON.COM"

dos_text "$here/CONFIG.DB" > "$work/CONFIG.DB"

# The sign-on screen times out by itself after a few seconds; keys typed
# before it ends are lost, so typing starts later.  CREATE SEED: field
# NAME, type Character (default), width 10; an empty field name and Enter
# end the definition, Enter confirms, N declines data entry.
seedkeys="$(keys 'CREATE SEED') enter$(keys NAME) enter enter$(keys 10) enter enter enter n$(keys QUIT) enter"
# The DOS date is fixed so SEED.DBF is the same on every install.
run seed "DATE 10/08/2026
AUTOTYPE -w 15 -p 0.3$seedkeys
DBASE"
[ -f "$work/SEED.DBF" ] || fail "dBASE did not create SEED.DBF (see $work)"

mv "$work" "$dest"
echo "$prog: dBASE III Plus 1.1 installed in $dest"
ls -l "$dest"
