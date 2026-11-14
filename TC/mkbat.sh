#!/bin/sh
# mkbat.sh - prepare the Turbo C 2.01 build tree and generate its batch files.
#
# usage: sh TC/mkbat.sh            (run from the repository root)
#
# env:
#   TC_SRCS     source-list file to read (default: SRCS.MK)
#   TC_SRCROOT  directory the paths in that file are relative to
#               (default: the repository root)
#   TC_GENINC   generated headers (paths) copied into INC with the others
#               (default: none)
#   TC_JOBS     number of compile batch files, 1 or more (default 1); fewer
#               are written when there are fewer objects to compile
#
# Exit status: 0 when the tree and batch files are written, 2 on any error
# (a bad setting, a missing or non-8.3 file, a source list it cannot read,
# a command line too long for DOS).
#
# Turbo C 2.01 needs CR/LF line endings: with bare LF it treats everything
# after a preprocessor directive as part of that directive and silently
# compiles an empty file.  The sources are therefore copied with CR/LF
# endings into a work tree under BIN/TC/ and compiled from there.
#
# The build is split in two steps.  The compile batch files CC1.BAT,
# CC2.BAT, ... each compile a disjoint share of the objects, so that
# TC/runtc.sh can run them in several DOSBox-X instances at once; the
# shares are balanced by source size, largest first.  BIN/TC/BUILD.BAT
# then links every executable from the objects, in one instance.
#
# Layout seen from DOS (repository = C:, Turbo C install = D:):
#   C:\BIN\TC\BUILD.BAT          generated link batch file
#   C:\BIN\TC\CC<k>.BAT          generated compile batch files
#   C:\BIN\TC\TURBOC.CFG         include path D:\INCLUDE;INC (read by TCC
#                                from the current directory)
#   C:\BIN\TC\SRC\..., TEST\...  CR/LF copies of every .C file, and of
#                                WATTCP's kernel modules under
#                                OBJ\WATTCP\SRC\ once make has prepared them
#   C:\BIN\TC\INC\               CR/LF copies of every .H file, WATTCP's
#                                included, flat
#   C:\BIN\TC\OBJ\<m>\<path>.OBJ objects, one tree per memory model, mirroring
#                                the source path so equal base names in
#                                different directories cannot collide
#   C:\BIN\TC\<NAME>.RSP         TLINK response file per executable
#   C:\BIN\TC\<NAME>.MAP         detailed segment map (TLINK /s)
#   C:\BIN\TC\<NAME>.EXE         linked executables
#   BIN/TC/EXES.LST              host-side list of the executables to expect
#   BIN/TC/CC.LST                host-side list of the compile batch files
#                                (base names CC1, CC2, ...)
#
# Turbo C 2.01 does not look in the including file's directory for
# #include "X.H"; it searches the current directory and then the -I list in
# order.  All headers therefore sit in INC, and two headers with the same
# base name are refused.  TCC writes no temporary files and is told each
# object's path, so the compile batch files can share the work tree.
#
# TCC 2.01 has no response-file support of its own, so linking calls TLINK
# directly with a response file, which keeps every command line within
# DOS_MAXCMD (TEST/shlib.sh).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

[ -f SRCS.MK ] && [ -d TC ] || die "run from the repository root"

SRCS=${TC_SRCS:-SRCS.MK}
ROOT=${TC_SRCROOT:-.}
[ -f "$SRCS" ] || die "source list $SRCS not found"
[ -d "$ROOT" ] || die "source root $ROOT not found"

jobs=${TC_JOBS:-1}
jobs_ok TC_JOBS "$jobs"

WORK=BIN/TC
BAT=$WORK/BUILD.BAT
# Fixed locations inside the Turbo C install mounted as D:.
TCC='D:\TCC'
TLINK='D:\TLINK'
TCINC='D:\INCLUDE'
TCLIB='D:\LIB'

vars=$(srcs_vars "$SRCS") || exit 2
exes=$(getvar TC_EXES)
portable=$(getvar COMPAT_PORTABLE)
kernel=$(getvar WATTCP_KERNEL)
wattcp=$(getvar WATTCP_USERS)
[ -n "$exes" ] || die "TC_EXES is empty in $SRCS"

rm -rf "$WORK"
mkdir -p "$WORK"
copy_tree "$ROOT" "$WORK" dos_text "${TC_GENINC:-}" "$(getvar WATTCP_DIR)"
printf -- '-I%s;INC\r\n' "$TCINC" > "$WORK/TURBOC.CFG"

# prologue FILE [K] - the first lines of batch file FILE.
prologue() {
    emit "$1" '@ECHO OFF'
    emit "$1" 'REM Written by TC/mkbat.sh from the source list; do not edit.'
    emit "$1" 'C:'
    emit "$1" 'CD \BIN\TC'
}

: > "$BAT"
prologue "$BAT"

# Every object to compile, once, in build order, for deal_out:
# "size<TAB>source<TAB>command" lines, size being that of the source copy.
comp=$WORK/COMP.LST
: > "$comp"
done_objs=' '
for exe in $exes; do
    model=$(getvar "${exe}_MODEL")
    srcs=$(getvar "${exe}_SRCS")
    case $model in
        s|m|c|l|h) ;;
        *) die "${exe}_MODEL must be one of s m c l h (Turbo C 2.01" \
            "ships no tiny-model library), got '$model'" ;;
    esac
    [ -n "$srcs" ] || die "${exe}_SRCS is empty"
    check83 "$exe.EXE" $srcs
    mu=$(printf '%s' "$model" | tr 'a-z' 'A-Z')
    rsp="$WORK/$exe.RSP"
    : > "$rsp"
    printf '%s\\C0%s.OBJ+\r\n' "$TCLIB" "$mu" >> "$rsp"

    n=0
    count=$(printf '%s\n' $srcs | wc -l)
    for src in $srcs; do
        n=$((n + 1))
        [ -f "$WORK/$src" ] || die "$src (listed for $exe) not found in $ROOT"
        [ -s "$WORK/$src" ] || die "$src (listed for $exe) is empty"
        obj="OBJ/$mu/${src%.C}.OBJ"
        dobj=$(dospath "$obj")
        if [ $n -lt "$count" ]; then
            printf '%s+\r\n' "$dobj" >> "$rsp"
        else
            printf '%s\r\n' "$dobj" >> "$rsp"
        fi
        case $done_objs in *" $obj "*) continue ;; esac
        done_objs="$done_objs$obj "
        mkdir -p "$WORK/${obj%/*}"
        case $src in
            SRC/COMPAT/*) ansi='' ;;
            *) ansi=' -A' ;;
        esac
        # The portable sources of SRC/COMPAT/ are strict C89 too.
        case " $portable " in *" $src "*) ansi=' -A' ;; esac
        # -w enables every warning; -w-stu then drops "Undefined
        # structure", which TCC 2.01 reports for every opaque handle type
        # (a struct declared in a header and defined only in its own
        # module).  WATTCP's kernel modules and our sources that include
        # its headers are not strict C89, and the kernel modules are built
        # without warnings (-w-): WATTCP's 1994 C is changed only as
        # WATTCP.DIF needs.  Turbo C's defaults, byte packing and signed
        # char, are WATTCP's own.
        warn=' -w -w-stu'
        case " $kernel " in *" $src "*) ansi=''; warn=' -w-' ;; esac
        case " $wattcp " in *" $src "*) ansi='' ;; esac
        # CSCHDAT.C defines the far data of SRC/COMPAT/CSCHEMA.C, which
        # Turbo C 2.01 cannot qualify far: its whole data segment is
        # renamed into a CODE-class segment outside DGROUP instead (class
        # FAR_DATA would be placed after _STACK, where the near heap and
        # the stack grow).
        case $src in
            SRC/COMPAT/CSCHDAT.C) seg=' -zRCSCH_FAR -zSCSCH_GRP -zTCODE' ;;
            *) seg='' ;;
        esac
        cc="$TCC -c -m$model -1- -O -Z -G-$warn$ansi$seg -o$dobj"
        printf '%s\t%s\t%s\n' "$(wc -c < "$WORK/$src" | tr -d ' ')" \
            "$src" "$cc $(dospath "$src")" >> "$comp"
    done
    # Remaining TLINK fields: executable, map file (empty: <NAME>.MAP,
    # with the detailed segment list of /s), libraries.
    printf '%s.EXE\r\n\r\n' "$exe" >> "$rsp"
    printf '%s\\EMU.LIB+\r\n%s\\MATH%s.LIB+\r\n%s\\C%s.LIB\r\n' \
        "$TCLIB" "$TCLIB" "$mu" "$TCLIB" "$mu" >> "$rsp"
    emit "$BAT" "ECHO === $exe"
    emit "$BAT" "$TLINK /c /s @$exe.RSP"
    emit "$BAT" "IF ERRORLEVEL 1 ECHO *** FAILED: link $exe.EXE"
done
emit "$BAT" 'ECHO === BUILD.BAT DONE'

# emit_cc FILE SRC CMD - the compile CMD of source SRC, and the line that
# reports its failure.
emit_cc() {
    emit "$1" "$3"
    emit "$1" "IF ERRORLEVEL 1 ECHO *** FAILED: compile $(dospath "$2")"
}

deal_out "$comp" "$jobs" prologue emit_cc
printf '%s\n' $exes > "$WORK/EXES.LST"

ncc=$(wc -l < "$WORK/CC.LST" | tr -d ' ')
echo "$prog: wrote $BAT and $ncc compile batch files for: $exes"
