#!/bin/sh
# rundos.sh - run DOS test programs headless in DOSBox-X and check their
# exit status.
#
# usage: sh TEST/rundos.sh [-m MACHINE] [-t SECS] EXE...
#
#   -m MACHINE  video adapter of the emulated machine: cga (default) or
#               hercules (monochrome, text mode 7 at B000h).
#   -t SECS     host timeout for each program, 1 or more (default 60),
#               times DOSTIME_SCALE (dos_secs of TEST/shlib.sh).
#
# Each EXE runs in its own DOSBox-X instance with the DOS version reported
# as 3.10, an 8086 CPU, the -m video adapter and no sound, all drivers
# dummy, so no window opens; the instance has its own configuration and
# scratch directories, made with mktemp, so that several runs of this
# script can go on at once.  Drives:
#   C:  the directory holding the EXE, read-only so that runs of the
#       programs beside it cannot disturb each other (the program is run
#       as C:\NAME.EXE)
#   D:  an empty scratch directory, the current drive, passed to the
#       program as its only argument "D:\"
#   E:  a control directory: the program's standard output goes to
#       E:\OUT.TXT, and an IF ERRORLEVEL ladder (errlevel_bat of
#       TEST/shlib.sh) writes its exit status (0..255) to E:\RC.TXT
# The EXE's base name must be an 8.3 name; it is upper-cased for DOS.
#
# DOS starts every program with errorlevel 0, so a program that DOS
# cannot load, or that ends without setting an exit code, would look like
# a pass.  A program therefore passes only when it exits with 0 and its
# output holds the summary line "<NAME> PASS <count>" that tf_done prints,
# NAME being the EXE's base name.
#
# The output of each program is printed, followed by a line with its
# status: the exit code, 200 on timeout, 201 when DOSBox-X ended without
# writing the exit code, 202 when the exit code was 0 but the summary
# line is missing.  Exit status: 0 when every program passed, 1 when any
# did not, 2 on a usage or setup error, and 129, 130 or 143 when this
# script got SIGHUP, SIGINT or SIGTERM (only the DOSBox-X instance it
# started is killed).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

timeout=60
machine=cga
while getopts m:t: opt; do
    case $opt in
    m) machine=$OPTARG ;;
    t) timeout=$OPTARG ;;
    *) usage ;;
    esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || usage
limit=$(dos_secs "$timeout")
case $machine in cga|hercules) ;; *) die "unknown machine: $machine" ;; esac
command -v dosbox-x >/dev/null || die "dosbox-x not installed"

for exe in "$@"; do
    [ -f "$exe" ] || die "no such file: $exe"
    name=$(basename "$exe" | tr '[:lower:]' '[:upper:]')
    case $name in *.EXE|*.COM) is_83 "$name" ;; *) false ;; esac ||
        die "not an 8.3 .EXE or .COM name: $name"
    dir=$(cd "$(dirname "$exe")" && pwd -P)
    # The directory is pasted into a quoted DOSBox-X mount command.
    case $dir in *'"'*) die "path contains a double quote: $dir" ;; esac
done

work=$(mktemp -d "${TMPDIR:-/tmp}/rundos.XXXXXX")
case $work in *'"'*) rmdir "$work"; die "path contains a double quote: $work" ;; esac
emupid=
cleanup() {
    if [ -n "$emupid" ]; then
        kill "$emupid" 2>/dev/null || true
        wait "$emupid" 2>/dev/null || true
        emupid=
    fi
    rm -rf "$work"
}
trap cleanup EXIT
on_signals

# Run one program; prints its output and returns its status as described
# at the top.
run_one() {
    exe=$1
    name=$(basename "$exe" | tr '[:lower:]' '[:upper:]')
    dir=$(cd "$(dirname "$exe")" && pwd -P)
    run=$work/$name
    rm -rf "$run"
    mkdir -p "$run/D" "$run/E"
    conf=$run/DOSBOX.CONF
    {
        cat <<CONF
[sdl]
output=surface
autolock=false
[log]
logfile=
[dosbox]
machine=$machine
memsize=1
quit warning=false
[cpu]
cputype=8086
core=normal
cycles=max
[dos]
ver=3.10
lfn=false
[mixer]
nosound=true
[sblaster]
sbtype=none
[gus]
gus=false
[speaker]
pcspeaker=false
tandy=off
disney=false
[midi]
mpu401=none
mididevice=none
[serial]
serial1=disabled
serial2=disabled
serial3=disabled
serial4=disabled
[parallel]
parallel1=disabled
[autoexec]
mount c "$dir" -nocachedir -ro
mount d "$run/D" -nocachedir
mount e "$run/E" -nocachedir
d:
E:\\RUN.BAT
CONF
    } >"$conf"

    # The program and the errorlevel probe run from a batch file, because
    # the DOSBox-X autoexec section holds only a limited number of lines.
    {
        printf 'C:\\%s D:\\ >E:\\OUT.TXT\r\n' "$name"
        errlevel_bat 255
        printf 'ECHO %%RC%%>E:\\RC.TXT\r\n'
        printf 'exit\r\n'
    } >"$run/E/RUN.BAT"

    # Run in the background and waited for, so that a signal reaches the
    # trap at once; timeout ends with 124 on its limit, or 137 when the
    # instance needed SIGKILL.
    (cd "$run" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        exec timeout -k 5 "$limit" dosbox-x -conf "$conf" -nogui -nomenu \
            -nopromptfolder >"$run/DOSBOX.LOG" 2>&1) &
    emupid=$!
    status=0
    wait "$emupid" || status=$?
    emupid=

    [ -f "$run/E/OUT.TXT" ] && tr -d '\r' <"$run/E/OUT.TXT"
    if [ $status = 124 ] || [ $status = 137 ]; then
        echo "$prog: $name timed out after $(dos_secs_said "$timeout")" >&2
        return 200
    fi
    rc=
    [ -f "$run/E/RC.TXT" ] && rc=$(tr -cd '0-9' <"$run/E/RC.TXT")
    if [ -z "$rc" ]; then
        echo "$prog: $name ended without an exit status" >&2
        tail -20 "$run/DOSBOX.LOG" >&2 || true
        return 201
    fi
    if [ "$rc" -eq 0 ]; then
        base=${name%.*}
        if ! tr -d '\r' 2>/dev/null <"$run/E/OUT.TXT" | awk -v b="$base" '
            $0 == b " PASS " $3 && $3 ~ /^[0-9]+$/ { found = 1 }
            END { exit !found }'; then
            echo "$prog: $name exited with 0 but printed no \"$base PASS\" line" >&2
            return 202
        fi
    fi
    return "$rc"
}

failed=0
for exe in "$@"; do
    name=$(basename "$exe" | tr '[:lower:]' '[:upper:]')
    echo "=== $name"
    rc=0
    run_one "$exe" || rc=$?
    echo "=== $name exit $rc"
    [ $rc -eq 0 ] || failed=$((failed + 1))
done
if [ $failed -ne 0 ]; then
    fail "$failed of $# programs failed"
fi
echo "$prog: $# programs passed"
