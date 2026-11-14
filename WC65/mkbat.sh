#!/bin/sh
# mkbat.sh - prepare the Watcom C 6.5 build tree and generate its batch files.
#
# usage: sh WC65/mkbat.sh          (run from the repository root)
#
# env:
#   WC65_SRCS     source-list file to read (default: SRCS.MK)
#   WC65_SRCROOT  directory the paths in that file are relative to
#                 (default: the repository root)
#   WC65_GENINC   generated headers (paths) copied into E:\INC with the
#                 others (default: none)
#   WC65_JOBS     number of compile batch files, 1 or more (default 1);
#                 fewer are written when there are fewer objects to compile
#
# Exit status: 0 when the tree and batch files are written, 2 on any error
# (a bad setting, a missing or non-8.3 file, a source list it cannot read,
# a command line too long for DOS).
#
# The executables are WC65_EXES; each has <NAME>_SRCS and the memory
# model <NAME>_MODEL.  Each links with the stack size of the Turbo C
# build, _stklen of the repository's SRC/COMPAT/CDOS.C, so that every DOS
# build has the stack TEST/mapchk.sh budgets for.
#
# The build is split in two steps.  The compile batch files CC1.BAT,
# CC2.BAT, ... each compile a disjoint share of the objects, so that
# WC65/runwc.sh can run them in several DOSBox-X instances at once; the
# shares are balanced by source size, largest first.  BIN/WC65/BUILD.BAT
# then links every executable from the objects, in one instance.  WCC
# 6.5 reads memory it has not written, so each compile is run after
# ZEROMEM.COM (WC65/ZEROMEM.ASM), which zeroes the memory the compiler
# is given: an object is then the same whatever was compiled before it
# in its batch file, and the build's output the same whatever WC65_JOBS.
#
# Layout seen from DOS (repository = C:, Watcom C 6.5 install = D:, the
# work tree BIN/WC65 = E:, so that paths stay short):
#   E:\BUILD.BAT                 generated link batch file
#   E:\CC<k>.BAT                 generated compile batch files
#   E:\ZEROMEM.COM               WC65/ZEROMEM.ASM, assembled by nasm
#   E:\CC<k>\                    the directory CC<k>.BAT compiles in
#   E:\SRC\..., E:\TEST\...      copies of every .C file, and of WATTCP's
#                                kernel modules under E:\OBJ\WATTCP\SRC\
#                                once make has prepared them
#   E:\INC\                      copies of every .H file, WATTCP's
#                                included, flat
#   E:\OBJ\<m>\<path>.OBJ        objects, one tree per memory model,
#                                mirroring the source path so equal base
#                                names in different directories cannot
#                                collide
#   E:\<NAME>.LNK                WLINK directive file per executable
#   E:\<NAME>.EXE, <NAME>.MAP    linked executable and its map
#   BIN/WC65/EXES.LST            host-side list of the executables to expect
#   BIN/WC65/MODELS.LST          host-side list of the memory models used
#   BIN/WC65/CC.LST              host-side list of the compile batch files
#                                (base names CC1, CC2, ...)
#
# Watcom C 6.5 reads LF-only sources correctly, so the copies keep the
# repository's line endings; they are taken so that the build sees one
# consistent snapshot of the tree.
#
# WCC 6.5 has no switch naming the object file: it always writes
# <base>.OBJ into the current directory.  Each compile therefore names
# the source by its full path, and the object is then copied into place.
# It also names its temporary files (__LEAF__.TMP, __QUAD__.TMP,
# _TEMPA.TMP) without a directory, that is in the current directory, and
# under fixed names, so batch file CC<k>.BAT compiles in its own
# directory E:\CC<k>.
#
# WCC 6.5 does not look in the including file's directory for
# #include "X.H"; it searches the current directory and then the INCLUDE
# environment variable, here D:\H;E:\INC.  All headers therefore sit in
# E:\INC, and two headers with the same base name are refused.
#
# WCC has no response files, so every command is kept within DOS_MAXCMD
# (TEST/shlib.sh); linking reads its file list from a WLINK directive
# file.  WCC sets no error level for some failures (a missing source,
# for example) and none for warnings, so WC65/runwc.sh also checks the
# messages in the log.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

[ -f SRCS.MK ] && [ -d WC65 ] || die "run from the repository root"

SRCS=${WC65_SRCS:-SRCS.MK}
ROOT=${WC65_SRCROOT:-.}
[ -f "$SRCS" ] || die "source list $SRCS not found"
[ -d "$ROOT" ] || die "source root $ROOT not found"

jobs=${WC65_JOBS:-1}
jobs_ok WC65_JOBS "$jobs"

[ -f SRC/COMPAT/CDOS.C ] || die "SRC/COMPAT/CDOS.C not found"
STACK=$(sed -n 's/^unsigned _stklen = \([0-9][0-9]*\);.*/\1/p' \
    SRC/COMPAT/CDOS.C)
[ -n "$STACK" ] || die "_stklen not found in SRC/COMPAT/CDOS.C"

WORK=BIN/WC65
BAT=$WORK/BUILD.BAT
# Fixed locations inside the Watcom C 6.5 install mounted as D:.
WCC='D:\BIN\WCC'
WLINK='D:\BIN\WLINK'
WCINC='D:\H'
WCLIB='D:\LIB'
WCBIN='D:\BIN'
# Compiler switches: 8086 instructions, optimise for size, no stack
# overflow checks, quiet; TF_TERSE keeps the test programs' check macros
# small enough for this compiler (TEST/TESTFW.H).  wc_flags adds the
# warning level and WATTCP's switches.
WCFLAGS='/0 /os /s /zq /dTF_TERSE'

vars=$(srcs_vars "$SRCS") || exit 2
exes=$(getvar WC65_EXES)
[ -n "$exes" ] || die "WC65_EXES is empty in $SRCS"
kernel=$(getvar WATTCP_KERNEL)
wattcp=$(getvar WATTCP_USERS)

rm -rf "$WORK"
mkdir -p "$WORK"
nasm -f bin -o "$WORK/ZEROMEM.COM" WC65/ZEROMEM.ASM ||
    die "WC65/ZEROMEM.ASM could not be assembled"
copy_tree "$ROOT" "$WORK" cat "${WC65_GENINC:-}" "$(getvar WATTCP_DIR)"

# prologue FILE [K] - the first lines of batch file FILE: the tool paths,
# then drive E:; with K, those of compile batch file CC<K>.BAT, which then
# changes to its directory E:\CC<K>, made here.
prologue() {
    emit "$1" '@ECHO OFF'
    emit "$1" 'REM Written by WC65/mkbat.sh from the source list; do not edit.'
    emit "$1" "SET PATH=$WCBIN"
    emit "$1" "SET INCLUDE=$WCINC;E:\\INC"
    emit "$1" "SET LIB=$WCLIB"
    emit "$1" 'E:'
    if [ $# -gt 1 ]; then
        mkdir "$WORK/CC$2"
        emit "$1" "CD \\CC$2"
    fi
}

: > "$BAT"
prologue "$BAT"

# Every object to compile, once, in build order, for deal_out:
# "size<TAB>source<TAB>model<TAB>object" lines, size being that of the
# source copy.
comp=$WORK/COMP.LST
: > "$comp"
done_objs=' '
models=' '
for exe in $exes; do
    model=$(getvar "${exe}_MODEL")
    srcs=$(getvar "${exe}_SRCS")
    case $model in
        s|m|c|l|h) ;;
        *) die "${exe}: memory model must be one of s m c l h, got '$model'" ;;
    esac
    [ -n "$srcs" ] || die "${exe}_SRCS is empty"
    check83 "$exe.EXE" $srcs
    case $models in *" $model "*) ;; *) models="$models$model " ;; esac
    mu=$(printf '%s' "$model" | tr 'a-z' 'A-Z')
    lnk="$WORK/$exe.LNK"
    printf 'NAME %s.EXE\r\nOPTION MAP=%s.MAP\r\nOPTION STACK=%s\r\n' \
        "$exe" "$exe" "$STACK" > "$lnk"

    for src in $srcs; do
        [ -f "$WORK/$src" ] || die "$src (listed for $exe) not found in $ROOT"
        [ -s "$WORK/$src" ] || die "$src (listed for $exe) is empty"
        obj="OBJ/$mu/${src%.C}.OBJ"
        printf 'FILE %s\r\n' "$(dospath "$obj")" >> "$lnk"
        case $done_objs in *" $obj "*) continue ;; esac
        done_objs="$done_objs$obj "
        mkdir -p "$WORK/${obj%/*}"
        printf '%s\t%s\t%s\t%s\n' "$(wc -c < "$WORK/$src" | tr -d ' ')" \
            "$src" "$model" "$obj" >> "$comp"
    done
    emit "$BAT" "ECHO === $exe"
    emit "$BAT" "$WLINK @$exe.LNK"
    emit "$BAT" "IF ERRORLEVEL 1 ECHO *** FAILED: link $exe.EXE"
    emit "$BAT" "IF NOT EXIST $exe.EXE ECHO *** FAILED: no $exe.EXE"
done
emit "$BAT" 'ECHO === BUILD.BAT DONE'

# wc_flags SRC - the warning level of source SRC, and WATTCP's switches:
# /j (signed char) for WATTCP's kernel modules and our sources that
# include its headers (this compiler always packs structs to one byte, as
# WATTCP needs), the kernel modules at warning level 0, as WATTCP's 1994
# C is changed only as WATTCP.DIF needs; level 3 for every other source.
wc_flags() {
    case " $kernel " in *" $1 "*) echo '/j /w0'; return ;; esac
    case " $wattcp " in *" $1 "*) echo '/j /w3'; return ;; esac
    echo '/w3'
}

# emit_cc FILE SRC MODEL OBJ - zero the free memory, then compile source
# SRC in memory model MODEL in the batch file's own directory, copy the
# object it writes there to OBJ in the object tree and delete it, with
# the lines that report a failed compile or a missing object.
emit_cc() {
    dsrc=$(dospath "$2")
    dobj=$(dospath "$4")
    emit "$1" 'E:\ZEROMEM'
    emit "$1" "$WCC E:\\$dsrc /m$3 $WCFLAGS $(wc_flags "$2")"
    emit "$1" "IF ERRORLEVEL 1 ECHO *** FAILED: compile $dsrc"
    emit "$1" "COPY ${4##*/} E:\\$dobj"
    emit "$1" "DEL ${4##*/}"
    emit "$1" "IF NOT EXIST E:\\$dobj ECHO *** FAILED: no object for $dsrc"
}

deal_out "$comp" "$jobs" prologue emit_cc
printf '%s\n' $exes > "$WORK/EXES.LST"
printf '%s\n' $models > "$WORK/MODELS.LST"

ncc=$(wc -l < "$WORK/CC.LST" | tr -d ' ')
echo "$prog: wrote $BAT and $ncc compile batch files for: $exes"
