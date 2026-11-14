#!/bin/sh
# T_TOOLS.SH - host test of the command lines, output and exit statuses of
# the test tools NBTEST and COMTEST.
#
# usage: sh TEST/T_TOOLS.SH NBTEST COMTEST     (run from the repository root)
#
# NBTEST and COMTEST are the two programs built for the host
# (OBJ/HOST/NBTEST.bin, OBJ/HOST/COMTEST.bin), which never finds a
# NetBIOS provider or a serial port.  It checks that:
#   - every bad command line exits 1 with nothing on standard output and
#     the usage text, one "usage:" line per form, on standard error: for
#     NBTEST no operand, an unknown mode, a missing, extra or empty name,
#     a count given as an operand, /WAIT with its value as a word of its
#     own, a /WAIT: of 0, above MAX_WAIT_SECS (NBTEST.C), not a number or
#     empty, a /COUNT: of 0 or above MAX_COUNT, /COUNT with SERVER, /WAIT
#     with CLIENT, a repeated /EXIT, /ASYNC, /WAIT: or /COUNT:, an unknown
#     switch and /? before or after a mode; for COMTEST a speed that is
#     no rate of UART_BAUDOK, /BAUD without its colon, two ports, a
#     repeated or unknown switch, a word that is no switch and /? with a
#     switch;
#   - every other command line, its switches in any case and begun by
#     "/" or "-", gets past the command line: NBTEST in either mode and
#     COMTEST on COM1 by default, on COM2 with /COM2 and with /BAUD: and
#     /RTSCTS each end with 2, the provider missing, every line of
#     standard output starting with the tool's name, the last the verdict
#     "<TOOL> FAIL" and its reason (tool_out of TEST/shlib.sh), COMTEST's
#     ending "no such port" and its output naming the port it tried;
#   - /? or -? alone prints the usage text on standard output, nothing on
#     standard error, and exits 0.
# The verdict PASS and the other statuses need the providers: the DOS
# runs of TEST/T_INV.SH, TEST/INTEG/nbgate.sh, TEST/INTEG/tcpprobe.sh and
# COMTEST's run of kind com in MAKEFILE check them.
#
# Exits 0 when every check passes, 1 when one fails, 2 on a setup error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

[ $# -eq 2 ] || die "usage: sh TEST/T_TOOLS.SH NBTEST COMTEST"
nb=$1
ct=$2
for b in "$nb" "$ct"; do
    [ -x "$b" ] || die "no such program: $b"
done
max_wait=$(cdefine SRC/TOOLS/NBTEST.C MAX_WAIT_SECS)
max_count=$(cdefine SRC/TOOLS/NBTEST.C MAX_COUNT)

mkdir -p OBJ || die "cannot create OBJ"
work=$(mktemp -d OBJ/t_tools.XXXXXX) || die "cannot create a scratch directory"
trap 'rm -rf "$work"' EXIT
on_signals

printf '%s\n' \
    'usage: NBTEST SERVER name [/WAIT:seconds] [/ASYNC] [/EXIT]' \
    'usage: NBTEST CLIENT localname remotename [/COUNT:n] [/ASYNC] [/EXIT]' \
    >"$work/NBTEST.use"
printf '%s\n' 'usage: COMTEST [/COM1 | /COM2] [/BAUD:n] [/RTSCTS]' \
    >"$work/COMTEST.use"

fails=0
checks=0
lastbad=0
# bad WHY...: report WHY and count the current check as failed, once.
bad() {
    echo "$prog: FAIL: $tool $what: $*" >&2
    [ "$lastbad" = "$checks" ] || fails=$((fails + 1))
    lastbad=$checks
}

# run TOOL STATUS WHAT [ARG...]: run TOOL (NBTEST or COMTEST) with ARG,
# its standard output in $work/out and its standard error in $work/err;
# count a failure unless it exits with STATUS and tool_out passes its
# standard output.
run() {
    tool=$1
    want=$2
    what=$3
    shift 3
    checks=$((checks + 1))
    bin=$nb
    [ "$tool" = NBTEST ] || bin=$ct
    rc=0
    "$bin" "$@" </dev/null >"$work/out" 2>"$work/err" || rc=$?
    [ "$rc" = "$want" ] || bad "status $rc, expected $want"
    why=$(tool_out "$tool" "$work/out" "$rc") || bad "$why"
}

# usage TOOL WHAT [ARG...]: a bad command line, which must exit 1 with
# exactly the tool's usage text on standard error.
usage() {
    t=$1
    w=$2
    shift 2
    run "$t" 1 "$w" "$@"
    cmp -s "$work/err" "$work/$t.use" ||
        bad "standard error: $(cat "$work/err")"
}

# noport WHAT PORT [ARG...]: COMTEST with ARG, which must try to open
# PORT, COM1 or COM2, and fail for want of it.
noport() {
    w=$1
    port=$2
    shift 2
    run COMTEST 2 "$w" "$@"
    tail -n 1 "$work/out" | grep -q 'no such port$' ||
        bad "verdict: $(tail -n 1 "$work/out")"
    cat "$work/out" "$work/err" | grep -qi "$port" ||
        bad "no mention of $port: $(cat "$work/out" "$work/err")"
}

# help TOOL SWITCH: TOOL with SWITCH alone, /? or -?, which must exit 0
# with exactly its usage text on standard output and nothing on standard
# error.
help() {
    tool=$1
    what=$2
    checks=$((checks + 1))
    bin=$nb
    [ "$tool" = NBTEST ] || bin=$ct
    rc=0
    "$bin" "$what" </dev/null >"$work/out" 2>"$work/err" || rc=$?
    [ "$rc" = 0 ] && cmp -s "$work/out" "$work/$tool.use" &&
        [ ! -s "$work/err" ] ||
        bad "status $rc, standard output: $(cat "$work/out")"
}

usage NBTEST "no operand"
usage NBTEST "unknown mode" PROBE X
usage NBTEST "server without a name" SERVER
usage NBTEST "server with two names" SERVER X Y
usage NBTEST "client with one name" CLIENT A
usage NBTEST "count as an operand" CLIENT A B 5
usage NBTEST "empty name" SERVER ''
usage NBTEST "empty remote name" CLIENT A ''
usage NBTEST "/WAIT and a word" SERVER X /WAIT 10
usage NBTEST "/WAIT:0" SERVER X /WAIT:0
usage NBTEST "/WAIT: above MAX_WAIT_SECS" SERVER X "/WAIT:$((max_wait + 1))"
usage NBTEST "/WAIT: no number" SERVER X /WAIT:ten
usage NBTEST "/WAIT: empty" SERVER X /WAIT:
usage NBTEST "/COUNT:0" CLIENT A B /COUNT:0
usage NBTEST "/COUNT: above MAX_COUNT" CLIENT A B "/COUNT:$((max_count + 1))"
usage NBTEST "/COUNT with SERVER" SERVER X /COUNT:5
usage NBTEST "/WAIT with CLIENT" CLIENT A B /WAIT:5
usage NBTEST "unknown switch" SERVER X /BOGUS
usage NBTEST "/? after a mode" SERVER X /?
usage NBTEST "/? before a mode" /? SERVER X
usage NBTEST "/EXIT twice" SERVER X /EXIT -exit
usage NBTEST "/ASYNC twice" SERVER X /ASYNC /async
usage NBTEST "/WAIT: twice" SERVER X /WAIT:5 -wait:6
usage NBTEST "/COUNT: twice" CLIENT A B /COUNT:1 /count:2
help NBTEST /?
help NBTEST -?
run NBTEST 2 "server" SERVER X
run NBTEST 2 "server, '-' and lower case" SERVER X -wait:1 -async -exit
run NBTEST 2 "server, mixed case" SERVER X "/Wait:$max_wait" /Async /Exit
run NBTEST 2 "client" CLIENT A B
run NBTEST 2 "client, '-' and lower case" CLIENT A B -count:1 -async -exit
run NBTEST 2 "client, mixed case" CLIENT A B "/Count:$max_count" /ASYNC

usage COMTEST "no such rate" /BAUD:1000
usage COMTEST "rate 0" /BAUD:0
usage COMTEST "/BAUD without its colon" /BAUD
usage COMTEST "two ports" /COM1 /COM2
usage COMTEST "repeated switch" /RTSCTS -rtscts
usage COMTEST "/BAUD: twice" /BAUD:9600 -baud:9600
usage COMTEST "unknown switch" /COM3
usage COMTEST "no switch" COM1
usage COMTEST "/? with a switch" /? /COM1
help COMTEST /?
help COMTEST -?
noport "default port" COM1
noport "/COM2" COM2 /COM2
noport "speed and flow control" COM1 /BAUD:38400 /RTSCTS
noport "'-' and lower case" COM2 -com2 -baud:9600 -rtscts
noport "mixed case" COM1 /Com1 /Baud:19200 /RtsCts

if [ $fails -ne 0 ]; then
    echo "$prog: $fails of $checks checks failed" >&2
    exit 1
fi
echo "$prog: $checks checks passed"
