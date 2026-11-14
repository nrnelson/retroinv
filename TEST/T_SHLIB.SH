#!/bin/sh
# T_SHLIB.SH - host test of the time limits, the errorlevel batch lines,
# the test tools' output check and the source-list reader of
# TEST/shlib.sh.
#
# usage: sh TEST/T_SHLIB.SH      (run from the repository root)
#
# It checks that:
#   - dos_secs multiplies a limit by DOSTIME_SCALE, unset or empty meaning
#     1, and stops with status 2 on a limit that is empty, 0, written with
#     a leading zero or not a number, and on a scale outside 1 to 9;
#   - dos_secs_said names the scaled limit, and the limit and the scale
#     when the scale is not 1;
#   - errlevel_bat MAX, for MAX 1, 255 and the largest that fits one line
#     and one more, writes only lines "FOR %%E IN (...) DO IF ERRORLEVEL
#     %%E SET RC=%%E" ended by CR LF, each within DOS_MAXCMD characters,
#     whose lists together run from 0 to MAX without a gap or a repeat,
#     in one line for 1 and for the largest, in two for the one after it;
#     and stops with status 2 on a MAX of 0, 256 or not a number;
#   - tool_out passes, with LF or CR LF line ends, prefixed lines ending
#     in PASS for status 0, in FAIL and a reason for 3, lower-case words
#     after an optional device name in capitals and a colon, and no
#     output for 1, and refuses output for status 1, PASS for 2 or FAIL
#     for 0, a reason that is empty, starts with a digit, punctuation or
#     a capital word, or names a device without its colon, an unprefixed
#     line, a line after the verdict and no output for 0;
#   - srcs_vars expands a "$(NAME)" reference in a variable's name as in
#     its value, continuation lines and "+=" included, and stops with
#     status 2 on a name that refers to a variable not yet defined.
#
# Exits 0 when every check passes, 1 when one fails, 2 on a setup error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

[ -f TEST/shlib.sh ] || die "run from the repository root"
mkdir -p OBJ || die "cannot create OBJ"
work=$(mktemp -d OBJ/t_shlib.XXXXXX) || die "cannot create a scratch directory"
trap 'rm -rf "$work"' EXIT
on_signals

fails=0
checks=0

# bad WHAT - count a failed check named WHAT.
bad() {
    echo "$prog: FAIL: $1" >&2
    fails=$((fails + 1))
}

# expect WHAT WANT CMD... - run CMD in a subshell, so that a die in it
# ends only the subshell, and count a failure unless it prints WANT and
# exits 0, or, for WANT "status 2", exits 2.
expect() {
    what=$1
    want=$2
    shift 2
    checks=$((checks + 1))
    rc=0
    got=$( ("$@") 2>/dev/null) || rc=$?
    case $want in
    'status 2') [ "$rc" -eq 2 ] || bad "$what: status $rc, expected 2" ;;
    *) [ "$rc" -eq 0 ] && [ "$got" = "$want" ] ||
        bad "$what: status $rc, '$got', expected '$want'" ;;
    esac
}

# scaled SCALE FUNC SECS - FUNC SECS with DOSTIME_SCALE set to SCALE.
scaled() {
    DOSTIME_SCALE=$1
    "$2" "$3"
}

# unscaled FUNC SECS - FUNC SECS with DOSTIME_SCALE unset.
unscaled() {
    unset DOSTIME_SCALE
    "$1" "$2"
}

expect "dos_secs, scale unset" 300 unscaled dos_secs 300
expect "dos_secs, scale empty" 300 scaled '' dos_secs 300
expect "dos_secs, scale 1" 1 scaled 1 dos_secs 1
expect "dos_secs, scale 2" 600 scaled 2 dos_secs 300
expect "dos_secs, scale 9" 2700 scaled 9 dos_secs 300
for secs in '' 0 0300 30s; do
    expect "dos_secs '$secs'" 'status 2' scaled 1 dos_secs "$secs"
done
for scale in 0 10 x ' 2'; do
    expect "dos_secs, scale '$scale'" 'status 2' scaled "$scale" dos_secs 300
done
expect "dos_secs_said, scale unset" 300s unscaled dos_secs_said 300
expect "dos_secs_said, scale 1" 300s scaled 1 dos_secs_said 300
expect "dos_secs_said, scale 2" '600s (300s x 2)' scaled 2 dos_secs_said 300

# wide, the least MAX that takes a second line: errlevel_bat adds levels
# to a list while its text is at most ERRLEVEL_LIST characters long.
wide=0
list=0
while [ ${#list} -le $ERRLEVEL_LIST ]; do
    wide=$((wide + 1))
    list="$list $wide"
done
wide=$((wide + 1))

# levels MAX [LINES] - check the lines errlevel_bat MAX writes, and that
# there are LINES of them when given.
levels() {
    checks=$((checks + 1))
    if ! (errlevel_bat "$1") >"$work/rc.bat" 2>"$work/err"; then
        bad "errlevel_bat $1: failed: $(cat "$work/err")"
        return
    fi
    long=$(tr -d '\r' <"$work/rc.bat" | awk -v max=$DOS_MAXCMD \
        'length > max { n++ } END { print n + 0 }')
    [ "$long" -eq 0 ] || bad "errlevel_bat $1: $long lines over $DOS_MAXCMD"
    [ "$(grep -c "$(printf '\r')\$" "$work/rc.bat")" -eq \
        "$(wc -l <"$work/rc.bat")" ] ||
        bad "errlevel_bat $1: a line without CR LF"
    form='^FOR %%E IN \([0-9 ]*\) DO IF ERRORLEVEL %%E SET RC=%%E$'
    if tr -d '\r' <"$work/rc.bat" | grep -Ev "$form" >/dev/null; then
        bad "errlevel_bat $1: a line of another form"
    fi
    got=$(sed 's/^FOR %%E IN (\([0-9 ]*\)).*/\1/' "$work/rc.bat" | tr '\n' ' ')
    want=$(seq 0 "$1" | tr '\n' ' ')
    [ "$got" = "$want" ] ||
        bad "errlevel_bat $1: the levels are not 0 to $1 in order"
    [ -z "${2:-}" ] || [ "$2" -eq "$(wc -l <"$work/rc.bat")" ] ||
        bad "errlevel_bat $1: not $2 lines"
}

levels 1 1
levels $((wide - 1)) 1
levels $wide 2
levels 255
for max in 0 256 x; do
    expect "errlevel_bat $max" 'status 2' errlevel_bat "$max"
done

# verdict OK|BAD STATUS TEXT - check that tool_out T passes (OK) or
# refuses (BAD) the output TEXT, its escapes as printf takes them, of a
# run with exit status STATUS.
verdict() {
    checks=$((checks + 1))
    printf "$3" >"$work/out"
    rc=0
    tool_out T "$work/out" "$2" >/dev/null || rc=$?
    case $1$rc in OK0|BAD1) ;; *) bad "tool_out, $1, status $2: '$3'" ;; esac
}

verdict OK 0 'T LISTEN\r\nT PASS\r\n'
verdict OK 3 'T sent 256\nT FAIL lost bytes\n'
verdict OK 2 'T FAIL COM1: no such port\n'
verdict OK 1 ''
verdict BAD 1 'T FAIL usage\n'
verdict BAD 2 'T PASS\n'
verdict BAD 0 'T one\nT FAIL lost\n'
verdict BAD 2 'T FAIL 2\n'
verdict BAD 2 'T FAIL \n'
verdict BAD 2 'T FAIL\n'
verdict BAD 2 'T FAIL : no such port\n'
verdict BAD 2 'T FAIL NetBIOS not present\n'
verdict BAD 3 'T FAIL Byte lost\n'
verdict BAD 2 'T FAIL COM1 no such port\n'
verdict BAD 2 'T FAIL COM1: No such port\n'
verdict BAD 0 'one\nT PASS\n'
verdict BAD 0 'T PASS\nT after\n'
verdict BAD 0 ''

# srclist TEXT NAME - the value srcs_vars reads for NAME from a source
# list holding TEXT, a printf format.  The reader is named in pieces, as
# TEST/runcache.sh keys a script by the files it hands the reader.
reader='srcs''_vars'
srclist() {
    # shellcheck disable=SC2059
    printf "$1" >"$work/LIST.MK"
    vars=$("$reader" "$work/LIST.MK") || exit 2
    getvar "$2"
}
expect 'list name' 'A.C B.C' \
    srclist 'P := X\n$(P)_SRCS := A.C \\\n\tB.C\n' X_SRCS
expect 'list name +=' 'A.C B.C' \
    srclist 'P := X\n$(P)_SRCS := A.C\n$(P)_SRCS += B.C\n' X_SRCS
expect 'list name and value' 'X.C' \
    srclist 'P := X\n$(P)_SRCS := $(P).C\n' X_SRCS
expect 'list name undefined' 'status 2' \
    srclist '$(P)_SRCS := A.C\nP := X\n' X_SRCS

if [ $fails -ne 0 ]; then
    echo "$prog: $fails of $checks checks failed" >&2
    exit 1
fi
echo "$prog: $checks checks passed"
