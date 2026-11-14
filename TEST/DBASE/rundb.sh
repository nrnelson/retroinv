#!/bin/sh
# rundb.sh - run one dBASE III Plus program headless in DOSBox-X.
#
# usage: rundb.sh [-t SECS] [-o FILE] WORKDIR PRG
#
#   WORKDIR  host directory mounted as C: and made the current directory;
#            the program reads and writes its tables there.
#   PRG      the program: a host path to a .PRG file, copied into WORKDIR
#            under its upper-cased 8.3 name with CR/LF line ends, or the
#            name of a .PRG already in WORKDIR (used as it is).
#   -t SECS  host timeout for the whole run (default 120), times
#            DOSTIME_SCALE (dos_secs of TEST/shlib.sh).
#   -o FILE  text file in WORKDIR that the program writes with SET
#            ALTERNATE TO; printed to standard output, CR and 0x1A
#            removed, after the run (default OUT.TXT; ignored if absent).
#
# The dBASE install (default $RETRO_DL/dbase11/DBASE, made by install.sh;
# override with DB_DIR) is mounted as D: and put on the PATH, so dBASE
# finds DBASE.OVL and reads D:\CONFIG.DB, which turns off the status
# line, TALK, SAFETY, CONFIRM and the help offer, and does not start the
# Assistant.  The DOS date is set from DB_DATE (YYYY-MM-DD, default
# 2026-10-08) because dBASE stamps it into every table header it writes.
#
# KSTUFF.COM (assembled from KSTUFF.ASM with nasm) is on the PATH as
# E:\KSTUFF.COM, so a program can queue keystrokes for its next
# full-screen command with RUN KSTUFF (see KSTUFF.ASM); that is how a
# program fills a memo field, which REPLACE cannot do in dBASE III Plus.
#
# rundb.sh copies two helper programs into WORKDIR for the run and
# removes them afterwards.  A program starts with "ON ERROR DO DBERR", so
# that its first error leaves the error number and message in DBERR.TXT
# and ends dBASE instead of waiting at the "Cancel, Ignore, or Suspend?"
# prompt, and ends with "DO DBEND" followed by QUIT; DBEND writes the
# marker file DBEND.TXT, which is the only proof that dBASE really ran
# the program to its end.  The install stays writable: USE opens a table
# read/write, and MKGOLD.PRG opens D:\SEED.DBF in place.  One run at
# a time per WORKDIR (lock directory RUNDB.LCK in WORKDIR).
#
# Exit status: 0 when DBEND.TXT was written and DBERR.TXT was not; 1 when
# the program reported an error (DBERR.TXT is printed to standard error);
# 2 on a usage or setup error, or WORKDIR locked by another run; 3 when
# the run timed out or ended without reaching DBEND; 129, 130 or 143 on
# SIGHUP, SIGINT or SIGTERM.  Only the DOSBox-X process started here is
# killed, on timeout or on a signal, and it is waited for before the
# scratch files go.
set -eu

here=$(cd "$(dirname "$0")" && pwd -P)
. "$here/../shlib.sh"

secs=120
out=OUT.TXT
while getopts t:o: opt; do
    case $opt in
    t) secs=$OPTARG ;;
    o) out=$OPTARG ;;
    *) die "usage: rundb.sh [-t SECS] [-o FILE] WORKDIR PRG" ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 2 ] || die "usage: rundb.sh [-t SECS] [-o FILE] WORKDIR PRG"
limit=$(dos_secs "$secs")
for t in dosbox-x nasm timeout; do
    command -v $t >/dev/null || die "$t not installed"
done

dbdir=${DB_DIR:-$RETRO_DL/dbase11/DBASE}
[ -f "$dbdir/DBASE.EXE" ] && [ -f "$dbdir/DBASE.OVL" ] ||
    die "no dBASE install in $dbdir (run install.sh, or set DB_DIR)"
dbdir=$(cd "$dbdir" && pwd -P)
[ -d "$1" ] || die "no such directory: $1"
work=$(cd "$1" && pwd -P)
for p in "$work" "$dbdir"; do
    case $p in *'|'*|*'&'*|*'"'*) die "path contains | & or \": $p" ;; esac
done

date=${DB_DATE:-2026-10-08}
echo "$date" |
    grep -Eq '^(19[89][0-9]|20[0-9][0-9])-(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])$' ||
    die "DB_DATE must be YYYY-MM-DD, 1980-2099, month 01-12, day 01-31: $date"
y=${date%%-*}; md=${date#*-}; m=${md%-*}; d=${md#*-}

name=$(basename "$2" | tr '[:lower:]' '[:upper:]')
case $name in *.PRG) ;; *) die "PRG must end in .PRG: $2" ;; esac
is_83 "$name" || die "not an 8.3 name: $name"
case $name in DBERR.PRG|DBEND.PRG) die "$name is reserved" ;; esac
out=$(echo "$out" | tr '[:lower:]' '[:upper:]')
is_83 "$out" || die "not an 8.3 name: $out"
[ -f "$2" ] || [ -f "$work/$name" ] || die "no such program: $2"

lock=$work/RUNDB.LCK
mkdir "$lock" 2>/dev/null || die "$work is in use by another run ($lock)"
scratch=
pid=
cleanup() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$work/DBERR.PRG" "$work/DBEND.PRG" "$work/DBEND.TXT"
    [ -n "$scratch" ] && rm -rf "$scratch"
    rmdir "$lock" 2>/dev/null || true
}
trap cleanup EXIT
on_signals

if [ -f "$2" ]; then
    if [ "$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")" != "$work/$name" ]; then
        dos_text "$2" > "$work/$name"
    fi
fi
for f in DBERR.PRG DBEND.PRG; do
    dos_text "$here/$f" > "$work/$f"
done

scratch=$(mktemp -d "${TMPDIR:-/tmp}/rundb.XXXXXX")
rm -f "$work/DBEND.TXT" "$work/DBERR.TXT" "$work/$out"
mkdir "$scratch/E"
nasm -f bin -o "$scratch/E/KSTUFF.COM" "$here/KSTUFF.ASM" || die "nasm failed"

{ cat "$here/DB.CONF"
  printf '\n[autoexec]\n'
  printf 'mount c "%s" -nocachedir\n' "$work"
  printf 'mount d "%s" -nocachedir\n' "$dbdir"
  printf 'mount e "%s" -nocachedir -ro\n' "$scratch/E"
  printf 'DATE %s/%s/%s\n' "$m" "$d" "$y"
  printf '%s\n' 'PATH D:\;E:\;Z:\'
  printf 'c:\n'
  printf 'DBASE %s\n' "${name%.PRG}"
  printf 'exit\n'
} > "$scratch/DB.CONF"

# DOSBox-X's own exit status is not used: it can crash while shutting
# down after a good run.  The marker files decide.
status=0
cd "$scratch"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout -k 10 "$limit" dosbox-x -conf "$scratch/DB.CONF" -nogui -nomenu \
        -exit > "$scratch/dosbox.out" 2>&1 &
pid=$!
wait "$pid" 2>/dev/null || status=$?
pid=
cd /

if [ -f "$work/$out" ]; then
    tr -d '\r\032' < "$work/$out"
    echo
fi
if [ -f "$work/DBERR.TXT" ]; then
    tr -d '\r\032' < "$work/DBERR.TXT" >&2
    echo "$prog: the program stopped on an error" >&2
    exit 1
fi
if [ ! -f "$work/DBEND.TXT" ]; then
    if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
        said=$(dos_secs_said "$secs")
        echo "$prog: timed out after $said" >&2
    else
        tail -5 "$scratch/dosbox.out" >&2
        echo "$prog: the program did not reach DO DBEND (DOSBox-X status $status)" >&2
    fi
    exit 3
fi
exit 0
