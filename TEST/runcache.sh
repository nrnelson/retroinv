#!/bin/sh
# runcache.sh - the cache of passed DOS emulator runs: a run whose key, a
# digest of everything that can change its result, matches that of a run
# which passed before is not run again, and the stored log stands for it.
#
# usage: sh TEST/runcache.sh global FILE
#        sh TEST/runcache.sh key GLOBAL JOB PASS CMD [PATH]...
#        sh TEST/runcache.sh get MANIFEST LOG
#        sh TEST/runcache.sh put MANIFEST LOG SECS
#        sh TEST/runcache.sh drop MANIFEST
#
#   global  write to FILE the lines that every key of one emulator stage
#           shares (see below), after removing the entries that have
#           expired.
#   key     print the manifest of one run: GLOBAL's lines; the run's name
#           JOB, its pass pattern PASS and its command line CMD, which must
#           start "sh RUNNER"; then a "<sha256> <path>" line for each file
#           that RUNNER, or a test script TEST/T_*.SH it names, reads (see
#           below), for each word of CMD that names a regular file and for
#           each regular file under each PATH (a file or a directory), or
#           "absent PATH" for a PATH that does not exist.  The key is the
#           SHA-256 of the manifest.
#   get     when the cache holds a pass stored under MANIFEST's key with
#           that same manifest, no more than DOS_CACHE_DAYS days old, write
#           its log to LOG behind a line "=== from the run cache: ...",
#           print the time it was stored and exit 0; otherwise exit 1.
#   put     store LOG as a pass under MANIFEST's key, SECS being how long
#           the run took.
#   drop    remove the entry of MANIFEST's key and print the key: exit 0
#           when there was one, 1 (printing nothing) when there was none.
#
# The shared lines name the cache format; DOS_CACHE_SALT; the machine;
# the installed versions of the emulator, BIOS, SDL, mtools and shell
# packages (dpkg-query); the SHA-256 of the qemu-system-i386 and dosbox-x
# programs found on PATH and of the MS-DOS 3.10 boot floppy;
# DOSTIME_SCALE, TZ, LANG, LC_ALL and the host's UTC offset; and the
# SHA-256 of MAKEFILE, of every file matching TEST/*.sh, TEST/*.SH,
# TEST/*.BAT and TEST/INTEG/* (the runners, this script among them, so
# that a change of the keying makes new keys), of the files that the
# runners other than the test scripts TEST/T_*.SH read, and of
# BIN/QEXIT.COM.  The files a runner reads are those it names in "cdefine
# FILE" or "srcs_vars FILE" (TEST/shlib.sh), found by reading it; a test
# script's go only into the keys of the runs it takes part in, so that a
# list such as SRCS.MK changes only the keys of the runs that read it.
# global fails when a runner reads a file it cannot follow.  Paths are
# relative to the repository root, the current directory, so trees at
# different places share keys.
#
# An entry is the file DOS_CACHE_DIR/v1/<first two hex digits>/<key>: a
# line "RUNCACHE 1 <secs> <UTC time>", the manifest, a line "---" and the
# run's log.  Only passes are stored.  An entry is written under a
# temporary name in its own directory and renamed into place, so runs
# that store the same key at once leave one whole entry.  An entry
# expires DOS_CACHE_DAYS days after the run that stored it, so a result
# that depends on the date is checked again.
#
# env: DOS_CACHE_DIR   the cache directory (required)
#      DOS_CACHE_DAYS  the days an entry stays valid, 1 or more (default 7)
#      DOS_CACHE_SALT  text added to every key as it is
#      DOS310_IMG      the boot floppy (DOS310_FLOPPY of TEST/shlib.sh)
#
# Exit status: 0 on success (a hit for get, an entry removed for drop);
# 1 on a miss, when drop found no entry, or when global cannot identify
# a tool or a file the runners read (the reason on standard error); 2 on
# a usage error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

[ $# -ge 2 ] || usage
[ -n "${DOS_CACHE_DIR:-}" ] || die "DOS_CACHE_DIR is not set"
days=${DOS_CACHE_DAYS:-7}
case $days in ''|0*|*[!0-9]*) die "DOS_CACHE_DAYS must be a whole number, 1 or more: $days" ;; esac
store=$DOS_CACHE_DIR/v1

# The packages whose versions are keyed.
PKGS='qemu-system-x86 qemu-system-common qemu-system-data seabios ipxe-qemu
dosbox-x dosbox-x-data libsdl2-2.0-0 mtools dash coreutils mawk'

# digest LABEL FILE: print "<sha256 of FILE> LABEL".
digest() {
    h=$(sha256sum <"$2") || die "cannot read $2"
    printf '%s %s\n' "${h%% *}" "$1"
}

# The helpers of TEST/shlib.sh whose FILE argument names a file a runner
# reads, written in pieces so that this script does not use them itself.
READERS='cde''fine srcs''_vars'

# reads_of RUNNER: print the files RUNNER reads, one per line, each named
# from the repository root ("$here/" taken from RUNNER's directory).  Each
# use of a helper of READERS outside comment lines must read
# "$(HELPER FILE" or "$(HELPER "FILE"" (TEST/shlib.sh's definitions aside);
# fail on any other use and when a FILE is not a file here.
reads_of() {
    code=$(sed '/^[[:space:]]*#/d' "$1")
    for w in $READERS; do
        all=$(printf '%s\n' "$code" | grep -o "\\<$w\\>" | wc -l)
        uses=$(printf '%s\n' "$code" | grep -o "\$($w \"\\{0,1\\}[^ )\"]*" || true)
        n=$(printf '%s' "$uses" | grep -c . || true)
        [ "$1" != TEST/shlib.sh ] || n=$((n + $(printf '%s\n' "$code" | grep -c "^$w() {" || true)))
        [ "$all" = "$n" ] || fail "$1 uses $w in a form that cannot be keyed"
        for f in $(printf '%s\n' "$uses" | sed "s/^\$($w \"\\{0,1\\}//"); do
            case $f in '$here/'*) f=${1%/*}/${f#'$here/'} ;; esac
            f=$(realpath -m --relative-to=. "$f")
            [ -f "$f" ] || fail "$1 reads $f, which is not a file here"
            echo "$f"
        done
    done
}

# keyfile MANIFEST: set key and entry for MANIFEST.
keyfile() {
    [ -f "$1" ] || die "no such manifest: $1"
    key=$(sha256sum <"$1")
    key=${key%% *}
    entry=$store/${key%"${key#??}"}/$key
}

cmd=$1
shift
case $cmd in
global)
    [ $# = 1 ] || usage
    if [ -d "$store" ]; then
        find "$store" -type f -name '.tmp.*' -mmin +1440 -exec rm -f {} + 2>/dev/null || true
        find "$store" -type f ! -name '.tmp.*' -mmin +$((days * 1440)) \
            -exec rm -f {} + 2>/dev/null || true
    fi
    for t in dpkg-query qemu-system-i386 dosbox-x; do
        command -v $t >/dev/null || fail "$t not found"
    done
    # shellcheck disable=SC2086
    pkgs=$(dpkg-query -W -f='pkg ${Package}=${Version}\n' $PKGS) ||
        fail "the versions of $PKGS are not all known"
    [ -f "$DOS310_FLOPPY" ] || fail "boot floppy not found: $DOS310_FLOPPY"
    runners=
    for f in TEST/*.sh TEST/*.SH TEST/*.BAT TEST/INTEG/*; do
        [ ! -f "$f" ] || runners="$runners $f"
    done
    reads=
    for r in $runners; do
        f=$(reads_of "$r") || exit 1
        case $r in TEST/T_*.SH) ;; *) reads="$reads$f
" ;; esac
    done
    reads=$(printf '%s' "$reads" | LC_ALL=C sort -u)
    {
        echo 'fmt 1'
        echo "salt ${DOS_CACHE_SALT:-}"
        echo "arch $(uname -m)"
        echo "$pkgs"
        echo "env DOSTIME_SCALE=${DOSTIME_SCALE:-} TZ=${TZ:-} LANG=${LANG:-}" \
            "LC_ALL=${LC_ALL:-} utc$(date +%z)"
        digest qemu-system-i386 "$(command -v qemu-system-i386)"
        digest dosbox-x "$(command -v dosbox-x)"
        digest DOS310_IMG "$DOS310_FLOPPY"
        for f in MAKEFILE $runners $reads BIN/QEXIT.COM; do
            [ ! -f "$f" ] || digest "$f" "$f"
        done
    } >"$1"
    ;;
key)
    [ $# -ge 4 ] || usage
    [ -f "$1" ] || die "no such file: $1"
    # The runner, then each test script named outside comments by one
    # already taken, and the files each of them reads.
    todo=${4#sh }
    todo=${todo%% *}
    case $4 in "sh $todo"*) [ -f "$todo" ] ;; *) false ;; esac || fail "no runner in: $4"
    seen= reads=
    while [ -n "$todo" ]; do
        r=${todo%% *}
        todo=${todo#"$r"}
        todo=${todo# }
        case " $seen " in *" $r "*) continue ;; esac
        seen="$seen $r"
        f=$(reads_of "$r") || exit 1
        reads="$reads$f
"
        for n in $(sed '/^[[:space:]]*#/d' "$r" | grep -o 'T_[A-Z0-9_]*\.SH' | sort -u); do
            [ ! -f "TEST/$n" ] || todo="$todo TEST/$n"
        done
        todo=${todo# }
    done
    cat "$1"
    printf 'job %s\npass %s\ncmd %s\n' "$2" "$3" "$4"
    for f in $(printf '%s' "$reads" | LC_ALL=C sort -u); do
        digest "$f" "$f"
    done
    words=$4
    shift 4
    set -f
    # shellcheck disable=SC2086
    for w in $words; do
        [ ! -f "$w" ] || digest "$w" "$w"
    done
    set +f
    for p in "$@"; do
        if [ -d "$p" ]; then
            find -L "$p" -type f | LC_ALL=C sort |
                while IFS= read -r f; do digest "$f" "$f"; done
        elif [ -f "$p" ]; then
            digest "$p" "$p"
        else
            echo "absent $p"
        fi
    done
    ;;
get)
    [ $# = 2 ] || usage
    keyfile "$1"
    [ -n "$(find "$entry" -mmin -$((days * 1440)) 2>/dev/null)" ] || exit 1
    # One read of the entry, so that a rename or removal meanwhile is seen
    # either whole or not at all.
    copy=$2.ENT
    trap 'rm -f "$copy"' EXIT
    cat "$entry" >"$copy" 2>/dev/null || exit 1
    read -r tag ver secs when <"$copy" || exit 1
    [ "$tag $ver" = 'RUNCACHE 1' ] || exit 1
    n=$(wc -l <"$1")
    sed -n "2,$((n + 1))p" "$copy" | cmp -s - "$1" || exit 1
    [ "$(sed -n "$((n + 2))p" "$copy")" = --- ] || exit 1
    { echo "=== from the run cache: passed $when in ${secs}s, key $key"
      tail -n +$((n + 3)) "$copy"; } >"$2"
    echo "$when"
    ;;
put)
    [ $# = 3 ] || usage
    keyfile "$1"
    [ -f "$2" ] || die "no such log: $2"
    mkdir -p "${entry%/*}"
    tmp=$(mktemp "${entry%/*}/.tmp.XXXXXX")
    trap 'rm -f "$tmp"' EXIT
    { printf 'RUNCACHE 1 %s %s\n' "$3" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
      cat "$1"; echo ---; cat "$2"; } >"$tmp"
    chmod 644 "$tmp"
    mv -f "$tmp" "$entry"
    ;;
drop)
    [ $# = 1 ] || usage
    keyfile "$1"
    [ -f "$entry" ] || exit 1
    rm -f "$entry"
    printf '%s\n' "$key"
    ;;
*)
    usage
    ;;
esac
