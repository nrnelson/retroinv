#!/bin/sh
# mkbat.sh - prepare the Watcom C 9.5 build tree and generate its batch files.
#
# usage: sh WC95/mkbat.sh          (run from the repository root)
#
# env:
#   WC95_SRCS     source-list file to read (default: SRCS.MK)
#   WC95_SRCROOT  directory the paths in that file are relative to
#                 (default: the repository root)
#   WC95_GENINC   generated headers (paths) copied into E:\INC with the
#                 others (default: none)
#   WC95_CFLAGS   the switches of every compile (required): those of the
#                 Open Watcom Windows build, as the MAKEFILE gives them
#   WC95_ANSI     the switch added for strict ANSI C (required)
#   WC95_NOANSI   the sources compiled without it (default: none): those
#                 that include windows.h or are not portable, as the
#                 MAKEFILE's NOANSI patterns give them
#   WC95_LINK     the WLINK directives every program takes (required): the
#                 stack, heap, code packing and segment attributes of the
#                 Open Watcom build, as the MAKEFILE gives them
#   WC95_JOBS     number of compile batch files, 1 or more (default 1);
#                 fewer are written when there are fewer objects to compile
#
# Exit status: 0 when the tree and batch files are written, 2 on any error
# (a bad setting, a missing or non-8.3 file, a source list it cannot read,
# a command line too long for DOS).
#
# The executables are WC95_EXES, Windows programs; each has <NAME>_SRCS,
# its memory model <NAME>_MODEL and its resource script <NAME>_RC.
#
# The build is split in two steps, as for Watcom C 6.5 (WC65/mkbat.sh):
# the compile batch files CC1.BAT, CC2.BAT, ... each compile a disjoint
# share of the objects, balanced by source size, so that WC95/runwc.sh
# can run them in several DOSBox-X instances at once; BUILD.BAT then, in
# one instance, compiles each program's resource script, links it and
# binds the resources into it with WRC /30 /t, which marks it for
# Windows 3.0 in protected mode only, as the Open Watcom build does.
#
# Layout seen from DOS (repository = C:, Watcom C 9.5 install = D:, the
# work tree BIN/WC95 = E:):
#   E:\BUILD.BAT, E:\CC<k>.BAT   generated batch files
#   E:\CC<k>\                    the directory CC<k>.BAT compiles in, for
#                                the compiler's scratch files
#   E:\SRC\..., E:\TEST\...      copies of every .C file
#   E:\INC\                      copies of every .H file, the generated
#                                headers and the programs' resource
#                                scripts, flat
#   E:\OBJ\<m>\<path>.OBJ        objects, one tree per memory model
#   E:\OBJ\<NAME>.RES            compiled resources
#   E:\<NAME>.LNK                WLINK directive file per executable
#   E:\<NAME>.EXE, <NAME>.MAP    linked executable and its map
#   BIN/WC95/EXES.LST            host-side list of the executables to expect
#   BIN/WC95/CC.LST              host-side list of the compile batch files
#
# WCC 9.5 names its object (-fo=) and reads the switches every compile
# shares from the environment variable WCC, so a compile line holds only
# the source, the object, the model and WC95_ANSI; every line is kept within
# DOS_MAXCMD (TEST/shlib.sh).  The compiler and WRC find the headers
# through INCLUDE, E:\INC;D:\H;D:\H\WIN, so two headers or resource
# scripts with the same base name are refused, and ours come before the
# compiler's: SRC/CLIENT/PRINT.H before the PRINT.H of D:\H\WIN, which
# no header of windows.h includes.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

[ -f SRCS.MK ] && [ -d WC95 ] || die "run from the repository root"

SRCS=${WC95_SRCS:-SRCS.MK}
ROOT=${WC95_SRCROOT:-.}
[ -f "$SRCS" ] || die "source list $SRCS not found"
[ -d "$ROOT" ] || die "source root $ROOT not found"

jobs=${WC95_JOBS:-1}
jobs_ok WC95_JOBS "$jobs"
[ -n "${WC95_CFLAGS:-}" ] && [ -n "${WC95_ANSI:-}" ] &&
    [ -n "${WC95_LINK:-}" ] ||
    die "WC95_CFLAGS, WC95_ANSI and WC95_LINK must be set"
noansi=" ${WC95_NOANSI:-} "

WORK=BIN/WC95
BAT=$WORK/BUILD.BAT

vars=$(srcs_vars "$SRCS") || exit 2
exes=$(getvar WC95_EXES)
[ -n "$exes" ] || die "WC95_EXES is empty in $SRCS"

rm -rf "$WORK"
mkdir -p "$WORK"
copy_tree "$ROOT" "$WORK" cat "${WC95_GENINC:-}"

# prologue FILE [K] - the first lines of batch file FILE: the tool paths
# and switches, then drive E:; with K, those of compile batch file
# CC<K>.BAT, which then changes to its directory E:\CC<K>, made here.
prologue() {
    emit "$1" '@ECHO OFF'
    emit "$1" 'REM Written by WC95/mkbat.sh from the source list; do not edit.'
    emit "$1" 'SET WATCOM=D:'
    emit "$1" 'SET PATH=D:\BIN;D:\BINB'
    emit "$1" 'SET INCLUDE=E:\INC;D:\H;D:\H\WIN'
    emit "$1" "SET WCC=$WC95_CFLAGS"
    emit "$1" 'E:'
    if [ $# -gt 1 ]; then
        mkdir "$WORK/CC$2"
        emit "$1" "CD \\CC$2"
    fi
}

: > "$BAT"
prologue "$BAT"

comp=$WORK/COMP.LST
: > "$comp"
done_objs=' '
for exe in $exes; do
    model=$(getvar "${exe}_MODEL")
    srcs=$(getvar "${exe}_SRCS")
    rc=$(getvar "${exe}_RC")
    case $model in
        s|m) ;;
        *) die "${exe}: memory model must be s or m, got '$model'" ;;
    esac
    [ -n "$srcs" ] || die "${exe}_SRCS is empty"
    [ -n "$rc" ] || die "${exe}_RC is empty"
    check83 "$exe.EXE" $srcs "$rc"
    # The resource script and the scripts beside it, which it may
    # include, go into E:\INC with the headers.
    for f in "$ROOT/${rc%/*}"/*.RC; do
        b=${f##*/}
        if [ -f "$WORK/INC/$b" ]; then
            cmp -s "$f" "$WORK/INC/$b" ||
                die "duplicate resource script name $b"
        else
            cp "$f" "$WORK/INC/$b"
        fi
    done
    mu=$(printf '%s' "$model" | tr 'a-z' 'A-Z')
    lnk="$WORK/$exe.LNK"
    { printf 'SYSTEM windows\r\nNAME %s.EXE\r\nOPTION MAP=%s.MAP\r\n' \
          "$exe" "$exe"
      printf '%s\n' "$WC95_LINK" | dos_text; } > "$lnk"

    for src in $srcs; do
        [ -f "$WORK/$src" ] || die "$src (listed for $exe) not found in $ROOT"
        obj="OBJ/$mu/${src%.C}.OBJ"
        printf 'FILE %s\r\n' "$(dospath "$obj")" >> "$lnk"
        case $done_objs in *" $obj "*) continue ;; esac
        done_objs="$done_objs$obj "
        mkdir -p "$WORK/${obj%/*}"
        printf '%s\t%s\t%s\t%s\n' "$(wc -c < "$WORK/$src" | tr -d ' ')" \
            "$src" "$model" "$obj" >> "$comp"
    done
    res="OBJ\\$exe.RES"
    emit "$BAT" "ECHO === $exe"
    emit "$BAT" "WRC /q /r /fo=$res INC\\${rc##*/}"
    emit "$BAT" "IF ERRORLEVEL 1 ECHO *** FAILED: resources $exe"
    emit "$BAT" "WLINK @$exe.LNK"
    emit "$BAT" "IF ERRORLEVEL 1 ECHO *** FAILED: link $exe.EXE"
    emit "$BAT" "WRC /q /30 /t $res $exe.EXE"
    emit "$BAT" "IF ERRORLEVEL 1 ECHO *** FAILED: bind $exe.EXE"
    emit "$BAT" "IF NOT EXIST $exe.EXE ECHO *** FAILED: no $exe.EXE"
done
emit "$BAT" 'ECHO === BUILD.BAT DONE'

# emit_cc FILE SRC MODEL OBJ - compile source SRC in memory model MODEL
# into OBJ, with WC95_ANSI unless WC95_NOANSI names it, with the lines
# that report a failed compile or a missing object.
emit_cc() {
    dsrc=$(dospath "$2")
    dobj=$(dospath "$4")
    za=" $WC95_ANSI"
    case $noansi in *" $2 "*) za= ;; esac
    emit "$1" "WCC E:\\$dsrc -fo=E:\\$dobj -m$3$za"
    emit "$1" "IF ERRORLEVEL 1 ECHO *** FAILED: compile $dsrc"
    emit "$1" "IF NOT EXIST E:\\$dobj ECHO *** FAILED: no object for $dsrc"
}

deal_out "$comp" "$jobs" prologue emit_cc
printf '%s\n' $exes > "$WORK/EXES.LST"

ncc=$(wc -l < "$WORK/CC.LST" | tr -d ' ')
echo "$prog: wrote $BAT and $ncc compile batch files for: $exes"
