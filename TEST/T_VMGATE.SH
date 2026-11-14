#!/bin/sh
# T_VMGATE.SH - host test of the failure reports of the two-VM gates:
# open_check, log_check and the golden helpers of TEST/INTEG/vmgate.sh,
# the report and exit status of TEST/INTEG/srvgate.sh, and the runs of
# TEST/INTEG/scrgate.sh, without an emulator.
#
# usage: sh TEST/T_VMGATE.SH
#
# open_check, log_check and the golden helpers each run in a fresh sh
# under "set -eu", as the gates call them.  It checks that:
#   - open_check prints "<prog>: RUN exited 2: it did not open its link"
#     on standard error alone for a .RC file holding level 2 (CR LF
#     ended); with OPTION KB FLOOR and KB given it adds " (with OPTION
#     KB: FLOOR_FREEKB_<build> may be too low)", with KB empty nothing;
#     it prints nothing for levels 9 and 12, an empty file and a missing
#     one, and returns 0 every time;
#   - log_check passes a CR LF log of COUNT server runs, each STACK of at
#     least SRV_STACK_MARGIN (TEST/shlib.sh) bytes right before its STOP
#     0, with sessions of slots 1 to 12 between, and prints each STACK
#     figure; it sets status to 1 and names the fault for a STACK after
#     its STOP, a line between STACK and STOP, a STACK before a STOP 0 of
#     slot 1, SRV_STACK_MARGIN - 1 or 0 bytes, no STACK, one STACK or one
#     START for two runs, two STOP 0 for one, an unknown event word and a
#     missing log;
#   - golden passes a file equal to its golden and fails one that
#     differs, naming it, leaving the golden as it was; with update 1 it
#     rewrites the golden; golden_done fails when fewer goldens were
#     compared or written than it is given, or one was written without
#     update 1; golden_check, with update 1, rewrites a golden from the
#     transcript of its own name and compares an alias with it, failing
#     one that differs from it and leaving it as rewritten, compares an
#     alias that differs, and fails, naming it, a missing transcript.
#
# Then it runs srvgate.sh in a scratch tree: copies of srvgate.sh and the
# scripts it sources, stand-ins for run2vm.sh, qmon.py, TEST/CHKDBF.PY,
# the programs, the request scripts and the goldens, and make on PATH.
# The stand-ins act out one scenario each (VT_SCEN): a passing run,
# whose output and qmon.py calls must stay as they are; a failed one,
# with and without VM B's DONE.TXT, whose standard error must give the
# INV runs that did not open their link, then DONE.TXT's verdict, then
# the lost-server case, then run2vm.sh's report, then the failed checks,
# and exit 1; a run in which only the lost-server case fails, which must
# quit both VMs and exit 1; a setup error of run2vm.sh (exit 2); SIGHUP,
# SIGINT and SIGTERM while the gate waits on qmon.py, which must stop
# qmon.py and wait for run2vm.sh (129, 130, 143); and SIGTERM after
# run2vm.sh's report was printed (143).  run2vm.sh's report must show
# exactly once, and no payload or monitor directory be left.  A signal is
# sent only once a stand-in has made the file READY, and the late one's
# stand-in goes on only once the signal was sent, so no step races
# another.
#
# Last it runs scrgate.sh in a scratch tree of its own, with four runs,
# THIN over COM1 and over NET each with a KB, INV over COM1 without and
# INV over NET with one, and stand-ins for run2vm.sh, the host INV, the
# programs, the key scripts and the goldens.  VM B's batch file must
# start each run's client lines and CHKDSK at its KB, over NET with the
# server's name, each run in its own directory with its own error level
# files.  Each run leaves its own least stack, which the gate must print
# under the run's name.  The gate must pass when every status line
# shows COM over COM1 and, over NET, NET and a NetBIOS name of RINV and
# four hexadecimal digits; and fail, naming only the runs at fault, when
# a run shows a NetBIOS name over COM1, COM over NET or the settings'
# station over NET; when every run, or R2 alone, exits 2 without a
# snapshot (each named with the floor of its program and link, none
# without a KB, and each run with a KB that started with less); when R3
# exits 0 without one; and when run2vm.sh fails with every file in
# place.  It must refuse a command line without -r, and each malformed
# -r, with status 2 and the reason.
#
# Exits 0 when every check passes, 1 when one fails, 2 on a setup error,
# 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"

command -v python3 >/dev/null || die "missing tool: python3"
env --default-signal=HUP,INT,TERM true 2>/dev/null ||
    die "env cannot reset a signal's handling (--default-signal)"

work=$(mktemp -d "${TMPDIR:-/tmp}/t_vmgate.XXXXXX")
gtmp=
gpid=
trap 'if [ -n "$gpid" ]; then kill "$gpid" 2>/dev/null || :
    wait "$gpid" 2>/dev/null || :; fi; rm -rf "$work" ${gtmp:+"$gtmp"}' EXIT
on_signals

fails=0
checks=0
failed=

# bad WHAT - report WHAT, a fault of the check counted last, and count
# that check failed once however many faults it has.
bad() {
    echo "$prog: FAIL: $1" >&2
    [ "$failed" = "$checks" ] || fails=$((fails + 1))
    failed=$checks
}

# same WHAT FILE [LINE...] - count a check, failed unless FILE holds
# exactly the lines LINE (nothing when none is given).
same() {
    checks=$((checks + 1))
    sm_what=$1
    sm_file=$2
    shift 2
    [ $# -eq 0 ] || printf '%s\n' "$@" >"$work/want"
    [ $# -ne 0 ] || : >"$work/want"
    cmp -s "$work/want" "$sm_file" && return 0
    bad "$sm_what:"
    diff -u "$work/want" "$sm_file" | sed 1,2d >&2 || :
}

# vg FUNCTION ARG... - run the vmgate.sh function in a fresh sh under
# "set -eu", prog G and status 0 first: its standard output and then
# "status <status>" to $work/o, its standard error to $work/e, and the
# shell's exit status to rc.
vg() {
    rc=0
    sh -eu -c '. "$0/shlib.sh"; . "$0/INTEG/vmgate.sh"; prog=G; status=0
        "$@"; echo "status $status"' "$here" "$@" >"$work/o" 2>"$work/e" ||
        rc=$?
}

# -------------------------------------------------------------- open_check
oc=$work/oc
mkdir "$oc"
printf '2\r\n' >"$oc/L2.RC"
printf '9\r\n' >"$oc/L9.RC"
printf '12\r\n' >"$oc/L12.RC"
: >"$oc/LE.RC"
lost='G: RUN exited 2: it did not open its link'

# ocx WHAT ERR ARG... - open_check ARG... must print ERR (nothing when
# empty) on standard error alone and return 0.
ocx() {
    ox_what=$1
    ox_err=$2
    shift 2
    vg open_check "$@"
    same "open_check $ox_what, standard output" "$work/o" 'status 0'
    if [ -n "$ox_err" ]; then
        same "open_check $ox_what, standard error" "$work/e" "$ox_err"
    else
        same "open_check $ox_what, standard error" "$work/e"
    fi
}

ocx 'level 2' "$lost" "$oc" L2 RUN
ocx 'level 2, KB' \
    "$lost (with -c 512: INVNET_FREEKB_<build> may be too low)" \
    "$oc" L2 RUN -c 512 INVNET
ocx 'level 2, KB empty' "$lost" "$oc" L2 RUN -c '' INVNET
for l in L9 L12 LE LM; do
    ocx "$l" '' "$oc" "$l" RUN
    ocx "$l, KB" '' "$oc" "$l" RUN -s 400 INVCOM
done

# --------------------------------------------------------------- log_check
log=$work/SRV.LOG
low=$((SRV_STACK_MARGIN - 1))
high=$((SRV_STACK_MARGIN + 1656))

# mklog LINE... - write LOG, one CR LF line "<date> <time> LINE" each.
mklog() {
    ml_s=0
    for ml_l in "$@"; do
        printf '2026-10-08 12:%02d:%02d %s\r\n' $((ml_s / 60)) \
            $((ml_s % 60)) "$ml_l"
        ml_s=$((ml_s + 1))
    done >"$log"
}

# The lines of one server run with sessions, ended by STACK N and STOP 0.
run_lines() {
    printf '%s\n' '0 START RISRV' '1 OPEN RIVMB' '10 OPEN RIVMC' \
        '1 REQ HELLO' '10 REQ HELLO' '10 REQ LOGIN' '10 LOGIN ADMIN' \
        '12 MBOX 5 OLD' '10 REQ ADMIN CLIENTS' '1 CLOSE' '10 CLOSE' \
        "0 STACK $1" '0 STOP 0'
}

# lpass WHAT COUNT N... - log_check LOG COUNT must pass, printing each
# STACK figure N.
lpass() {
    lp_what=$1
    lp_count=$2
    shift 2
    vg log_check "$log" "$lp_count"
    [ "$rc" -eq 0 ] || bad "log_check $lp_what: shell exit status $rc"
    for lp_n in "$@"; do
        echo "G: server stack never used: $lp_n bytes"
    done >"$work/lp"
    echo 'status 0' >>"$work/lp"
    same "log_check $lp_what, standard output" "$work/o" \
        "$(cat "$work/lp")"
    same "log_check $lp_what, standard error" "$work/e"
}

# lfail WHAT COUNT MSG - log_check LOG COUNT must set status 1 and print
# the line MSG on standard error.
lfail() {
    vg log_check "$log" "$2"
    checks=$((checks + 1))
    [ "$rc" -eq 0 ] || bad "log_check $1: shell exit status $rc"
    [ "$(tail -n 1 "$work/o")" = 'status 1' ] ||
        bad "log_check $1: status not set to 1"
    grep -qxF -- "$3" "$work/e" ||
        bad "log_check $1: no line '$3' in: $(cat "$work/e")"
}

set -f
IFS='
'
mklog $(run_lines $high) $(run_lines $SRV_STACK_MARGIN)
lpass 'two runs' 2 $high $SRV_STACK_MARGIN
mklog $(run_lines $SRV_STACK_MARGIN)
lpass 'SRV_STACK_MARGIN bytes' 1 $SRV_STACK_MARGIN
mklog $(run_lines $low)
lfail 'SRV_STACK_MARGIN - 1 bytes' 1 \
    "G: $low unused stack bytes, fewer than $SRV_STACK_MARGIN"
mklog $(run_lines 0)
lfail '0 bytes' 1 "G: 0 unused stack bytes, fewer than $SRV_STACK_MARGIN"
mklog '0 START RISRV' '0 STOP 0' "0 STACK $high" $(run_lines $high)
lfail 'STACK after STOP' 2 'G: 1 STACK events right before a STOP 0, not 2'
mklog '0 START RISRV' "0 STACK $high" '10 CLOSE' '0 STOP 0'
lfail 'a line between STACK and STOP' 1 \
    'G: 0 STACK events right before a STOP 0, not 1'
mklog '0 START RISRV' '1 OPEN RIVMB' '1 CLOSE' '0 STOP 0'
lfail 'no STACK' 1 'G: 0 STACK events right before a STOP 0, not 1'
mklog '0 START RISRV' '0 STOP 0' $(run_lines $high)
lfail 'one STACK of two runs' 2 \
    'G: 1 STACK events right before a STOP 0, not 2'
mklog '0 START RISRV' "0 STACK $high" '1 STOP 0' '0 STOP 0' \
    $(run_lines $high)
lfail 'a STACK before a STOP 0 of slot 1' 2 \
    'G: 1 STACK events right before a STOP 0, not 2'
mklog $(run_lines $high) "0 STACK $high" '0 STOP 0'
lfail 'one START of two runs' 2 "G: 1 'START RISRV' events, not 2"
mklog $(run_lines $high) '0 STOP 0'
lfail 'two STOP 0 of one run' 1 "G: 2 'STOP 0' events, not 1"
mklog '0 START RISRV' '1 OPEN RIVMB' '1 SEND HELLO' "0 STACK $high" \
    '0 STOP 0'
lfail 'an unknown event' 1 \
    'G: event log line 3:2026-10-08 12:00:02 1 SEND HELLO'
rm "$log"
lfail 'no log' 1 'G: no event log'
unset IFS
set +f

# ----------------------------------------------------------- golden helpers
gd=$work/gd
mkdir -p "$gd/TEST/GOLDEN" "$gd/m"
printf 'a\n' >"$gd/SAME"
printf 'b\n' >"$gd/DIFF"
printf 'b\n' >"$gd/G.OUT"
printf 'b\n' >"$gd/B.OUT"
printf 'z\n' >"$gd/Z.OUT"

# gdx WHAT UPDATE GOLDEN STATUS ERR CMDS - with TEST/GOLDEN/G.TXT
# holding "a", run CMDS, commands of the golden helpers, with repo, update
# UPDATE and mondir set: they must leave status STATUS, G.TXT holding
# GOLDEN, and print ERR (nothing when empty) as a line of standard error.
gdx() {
    printf 'a\n' >"$gd/TEST/GOLDEN/G.TXT"
    vg eval "repo=$gd update=$2 mondir=$gd/m; $6"
    checks=$((checks + 1))
    [ "$rc" -eq 0 ] || bad "golden $1: shell exit status $rc"
    [ "$(tail -n 1 "$work/o")" = "status $4" ] ||
        bad "golden $1: $(tail -n 1 "$work/o"), not status $4"
    [ "$(cat "$gd/TEST/GOLDEN/G.TXT")" = "$3" ] ||
        bad "golden $1: G.TXT holds $(cat "$gd/TEST/GOLDEN/G.TXT"), not $3"
    if [ -n "$5" ]; then
        grep -qxF -- "$5" "$work/e" ||
            bad "golden $1: no line '$5' in: $(cat "$work/e")"
    elif [ -s "$work/e" ]; then
        bad "golden $1: standard error: $(cat "$work/e")"
    fi
}

gdx 'the same' 0 a 0 '' "golden $gd/SAME G.TXT S; golden_done 1"
gdx 'a difference' 0 a 1 'G: D differs from TEST/GOLDEN/G.TXT' \
    "golden $gd/DIFF G.TXT D; golden_done 1"
gdx 'rewritten' 1 b 0 '' "golden $gd/DIFF G.TXT D; golden_done 1"
gdx 'one golden of two' 0 a 1 \
    'G: 1 goldens compared and 0 written with update 0, not 2' \
    "golden $gd/SAME G.TXT S; golden_done 2"
gdx 'written without update' 0 a 1 \
    'G: 0 goldens compared and 1 written with update 0, not 1' \
    "gd_wrote=1; golden_done 1"
gdx 'golden_check, an alias compared after its golden is rewritten' 1 b 0 \
    '' "golden_check $gd G:G B:G"
gdx 'golden_check, an alias that differs from its rewritten golden' 1 b 1 \
    'G: Z.OUT differs from TEST/GOLDEN/G.TXT' "golden_check $gd G:G Z:G"
gdx 'golden_check, an alias that differs' 0 a 1 \
    'G: B.OUT differs from TEST/GOLDEN/G.TXT' "golden_check $gd B:G"
gdx 'golden_check, no transcript' 0 a 1 'G: no C.OUT' \
    "golden_check $gd C:C"

# ------------------------------------------------- srvgate.sh in a stub tree
tree=$work/tree
sync=$work/sync
mkdir -p "$tree/TEST/INTEG" "$tree/TEST/GOLDEN" "$tree/BIN" "$work/bin"
# The gate's TMPDIR, which holds its monitor sockets: short, as a socket's
# path is limited to 108 bytes, whatever the caller's TMPDIR.
gtmp=$(mktemp -d /tmp/t_vmg.XXXXXX)
cp "$here/shlib.sh" "$tree/TEST/"
for f in srvgate.sh net330.sh free310.sh vmgate.sh; do
    cp "$here/INTEG/$f" "$tree/TEST/INTEG/"
done
for f in INVSERV.EXE INV.EXE INVTEST.EXE KSTUFF.COM EATRUN.COM QEXIT.COM \
    INVSERV.MSG; do
    echo "$f" >"$tree/BIN/$f"
done
printf 'PASSWD ADMIN NEWPW7\r\n' >"$tree/TEST/INTEG/NET2.SCR"
for f in SER.SCR DOWN.SCR NETCMD.TXT SRV.CFG; do
    printf '%s\r\n' "$f" >"$tree/TEST/INTEG/$f"
done
for g in NET2 SER NETCMD DOWN; do
    printf '+OK %s\n' "$g" >"$tree/TEST/GOLDEN/$g.TXT"
done
printf '#!/bin/sh\nexit 0\n' >"$work/bin/make"
chmod +x "$work/bin/make"

# run2vm.sh: the monitor sockets, then the scenario's result files and
# report.  In sigwait it waits from the start; on SIGTERM it reports the
# VMs killed only once GONE is made (the test saw the gate end) or after
# 1 s, so that a gate that did not wait for it misses the line.
cat >"$tree/TEST/INTEG/run2vm.sh" <<'EOF'
rm -rf "$OUT"
mkdir -p "$OUT/A/D" "$OUT/B/D"
: >"$OUT/.run2vm"
if [ "$VT_SCEN" = sigwait ]; then
    echo 'run2vm.sh: STARTED' >&2
    sleep 30 &
    sp=$!
    trap 'kill $sp
        i=0
        while [ ! -e "$VT_SYNC/GONE" ] && [ $i -lt 20 ]; do
            sleep 0.05
            i=$((i + 1))
        done
        echo "run2vm.sh: VMS KILLED" >&2
        exit 143' TERM
fi
for x in A B; do
    python3 -I -c 'import socket, sys
socket.socket(socket.AF_UNIX).bind(sys.argv[1])' "$MONDIR/$x.sock"
done
case $VT_SCEN in
setup) echo 'run2vm.sh: SETUP REPORT' >&2; exit 2 ;;
sigwait) wait $sp; exit 1 ;;
esac
put() { printf '%s\r\n' "$2" >"$OUT/$1"; }
g=${0%/INTEG/*}/GOLDEN
for f in NET2 SER COMCMD COMKEY NETCMD DOWN; do put B/D/$f.RC 0; done
put B/D/LOST.RC 9
put A/D/SRV1.RC 0
put A/D/SRV2.RC 0
put B/D/DONE.TXT ''
put B/D/COMBRK.OUT 'BREAK is on'
put B/D/COMKEY.OUT '+OK'
for f in NET2 SER NETCMD DOWN; do cp "$g/$f.TXT" "$OUT/B/D/$f.OUT"; done
cp "$g/NETCMD.TXT" "$OUT/B/D/COMCMD.OUT"
for n in 1 2; do
    printf '2026-10-08 12:0%s:00 0 %s\r\n' $n 'START RISRV' $n 'STACK 5000' \
        $n 'STOP 0'
done >"$OUT/A/D/SRV.LOG"
put A/D/CHKDSK.OUT '    600000 bytes free'
put B/D/CHKDSK.OUT '    590000 bytes free'
case $VT_SCEN in
late) echo 'run2vm.sh: HELD REPORT' >&2 ;;
lost) rm "$OUT/B/D/DONE.TXT" ;;
fail|faild)
    [ "$VT_SCEN" = fail ] || rm "$OUT/B/D/DONE.TXT"
    put B/D/COMCMD.RC 2
    put B/D/NETCMD.RC 2
    put B/D/LOST.RC 2
    : >"$OUT/A/D/SRV2.RC"
    echo 'run2vm.sh: HELD REPORT' >&2
    exit 1 ;;
esac
exit 0
EOF

# qmon.py: logs each call to QMON.LOG as "X quit" or "X drive" (X the
# VM); "quit" succeeds; "drive" fails when the VMs did not get through,
# blocks once READY is made in sigwait, making QSTOP when SIGTERM ends
# it, else succeeds.
cat >"$tree/TEST/INTEG/qmon.py" <<'EOF'
import os, signal, sys, time
scen = os.environ['VT_SCEN']
sync = os.environ['VT_SYNC']
def mark(name):
    open(os.path.join(sync, name), 'w').close()
def stop(signum, frame):
    mark('QSTOP')
    sys.exit(1)
step = 'quit' if sys.argv[2:] == ['quit'] else 'drive'
with open(os.path.join(sync, 'QMON.LOG'), 'a') as log:
    log.write(os.path.basename(sys.argv[1])[0] + ' ' + step + '\n')
if step == 'drive':
    if scen == 'sigwait':
        signal.signal(signal.SIGTERM, stop)
        mark('READY')
        time.sleep(30)
    if scen in ('setup', 'fail', 'faild', 'lost'):
        sys.exit(1)
EOF

# CHKDBF.PY: content; in late, it makes READY and goes on once GO is
# made.
cat >"$tree/TEST/CHKDBF.PY" <<'EOF'
import os, time
if os.environ['VT_SCEN'] == 'late':
    sync = os.environ['VT_SYNC']
    open(os.path.join(sync, 'READY'), 'w').close()
    for _ in range(200):
        if os.path.exists(os.path.join(sync, 'GO')):
            break
        time.sleep(0.05)
EOF

# await FILE - wait at most 10 s for FILE to exist; 1 when it does not.
await() {
    aw_i=0
    while [ ! -e "$1" ]; do
        [ "$aw_i" -lt 200 ] || return 1
        sleep 0.05
        aw_i=$((aw_i + 1))
    done
}

# gate SCEN STATUS [SIGNAL] - run the tree's srvgate.sh in scenario SCEN
# and check that it exits with STATUS and leaves no payload or monitor
# directory.  SIGHUP, SIGINT and SIGTERM are reset to their defaults for
# it, as a background job ignores SIGINT and a caller such as nohup may
# ignore the others, and an ignored signal cannot be trapped.  With
# SIGNAL, send it to the gate once READY is made, then make GO.  GONE is
# made once the gate has ended.  Its standard output and error go to
# $work/o and $work/e, the work directory shown as W and a kept run's
# time stamp as TS; qmon.py's calls are in $sync/QMON.LOG.
gate() {
    rm -rf "$sync"
    mkdir "$sync"
    VT_SCEN=$1 VT_SYNC=$sync OUT=$work/out.$1 TMPDIR=$gtmp \
        PATH=$work/bin:$PATH env --default-signal=HUP,INT,TERM \
        sh "$tree/TEST/INTEG/srvgate.sh" >"$work/go" 2>"$work/ge" &
    gpid=$!
    if [ $# -ge 3 ]; then
        checks=$((checks + 1))
        await "$sync/READY" || bad "srvgate.sh, $1 $3: no READY"
        kill -s "$3" "$gpid" 2>/dev/null ||
            bad "srvgate.sh, $1 $3: ended before the signal"
        : >"$sync/GO"
    fi
    rc=0
    wait "$gpid" || rc=$?
    gpid=
    : >"$sync/GONE"
    checks=$((checks + 1))
    [ "$rc" -eq "$2" ] || bad "srvgate.sh, $1 ${3:-}: exit status $rc, not $2"
    for f in go ge; do
        sed -e "s#$work#W#g" -e 's/\.fail\.[0-9]*-[0-9]*/.fail.TS/' \
            "$work/$f" >"$work/${f#g}"
    done
    left=$(ls -A "$gtmp" "$tree/TEST/INTEG/vm" 2>/dev/null | grep -v ':$' |
        grep . || :)
    checks=$((checks + 1))
    [ -z "$left" ] || bad "srvgate.sh, $1 ${3:-}: left $left"
}

stack='srvgate.sh: server stack never used: 5000 bytes'
free='srvgate.sh: VM A free memory with the network: 600000 bytes
srvgate.sh: VM B free memory with the network: 590000 bytes'

gate pass 0
same 'srvgate.sh passing, standard output' "$work/o" "$stack" "$stack" \
    "$free" 'srvgate.sh: PASS'
same 'srvgate.sh passing, standard error' "$work/e"
same 'srvgate.sh passing, qmon.py calls' "$sync/QMON.LOG" 'B drive' \
    'A drive' 'B drive'

# Only the lost-server case fails: the VMs are quit, and that alone fails
# the gate.
gate lost 1
same 'srvgate.sh lost, standard error' "$work/e" \
    'srvgate.sh: the lost-server case did not go as planned' \
    'srvgate.sh: FAIL; run kept in W/out.lost.fail.TS'
same 'srvgate.sh lost, qmon.py calls' "$sync/QMON.LOG" 'B drive' 'A quit' \
    'B quit'

nolink='exited 2: it did not open its link'
for s in fail faild; do
    gate $s 1
    {
        echo "srvgate.sh: INV /CMD /COM1 <NETCMD.TXT $nolink"
        echo "srvgate.sh: INV /CMD /NET <NETCMD.TXT $nolink"
        echo "srvgate.sh: INV /CMD /NET (lost-server case) $nolink"
        [ $s = faild ] ||
            echo 'srvgate.sh: VM B ran its batch to the end before the host' \
                'finished the lost-server case'
        echo 'srvgate.sh: the lost-server case did not go as planned'
        echo 'run2vm.sh: HELD REPORT'
        echo "srvgate.sh: SRV2.RC holds '', not 0"
        echo "srvgate.sh: COMCMD.RC holds '2', not 0"
        echo "srvgate.sh: NETCMD.RC holds '2', not 0"
        echo "srvgate.sh: LOST.RC holds '2', not 9"
        echo "srvgate.sh: FAIL; run kept in W/out.$s.fail.TS"
    } >"$work/fe"
    same "srvgate.sh $s, standard error" "$work/e" "$(cat "$work/fe")"
done

gate setup 2
same 'srvgate.sh setup error, standard output' "$work/o"
same 'srvgate.sh setup error, standard error' "$work/e" \
    'run2vm.sh: SETUP REPORT'

for sig in HUP INT TERM; do
    gate sigwait "$(signal_status $sig)" $sig
    same "srvgate.sh SIG$sig in a wait, standard output" "$work/o"
    same "srvgate.sh SIG$sig in a wait, standard error" "$work/e" \
        'run2vm.sh: STARTED' 'run2vm.sh: VMS KILLED'
    checks=$((checks + 1))
    await "$sync/QSTOP" ||
        bad "srvgate.sh SIG$sig in a wait: qmon.py not stopped"
done

gate late 143 TERM
same 'srvgate.sh late SIGTERM, standard output' "$work/o" "$stack" "$stack"
same 'srvgate.sh late SIGTERM, standard error' "$work/e" \
    'run2vm.sh: HELD REPORT'

# ------------------------------------------------ scrgate.sh in a stub tree
stree=$work/stree
mkdir -p "$stree/TEST/INTEG" "$stree/TEST/KEYS" "$stree/TEST/GOLDEN/SCR" \
    "$stree/BIN" "$stree/OBJ/HOST"
cp "$here/shlib.sh" "$stree/TEST/"
for f in scrgate.sh net330.sh free310.sh vmgate.sh SRV.CFG; do
    cp "$here/INTEG/$f" "$stree/TEST/INTEG/"
done
for f in INVSERV.EXE INVTEST.EXE INV.EXE INVTHIN.EXE EATRUN.COM QEXIT.COM \
    INVSERV.MSG INV.MSG INV.HLP; do
    echo "$f" >"$stree/BIN/$f"
done
printf '#define STACK_MARGIN 1024\n' >"$stree/TEST/T_SCRH.C"
printf '#@ DEEP\n' >"$stree/TEST/KEYS/DEEPA.KEY"
for f in RPTHELP.KEY SEED.REQ DEEP.REQ COLOR.CFG; do
    echo "$f" >"$stree/TEST/KEYS/$f"
done
for g in RPTHELP DEEPA; do
    printf '%s\r\n' '| TEST     LOCAL  ADMIN 3' '=ATTR 0' \
        >"$stree/TEST/GOLDEN/SCR/$g.SNC"
done
printf '#!/bin/sh\nwhile read -r l; do echo +OK; done\n' \
    >"$stree/OBJ/HOST/INV.bin"
chmod +x "$stree/OBJ/HOST/INV.bin"

# run2vm.sh: VM B's batch file, LF ended, as OUT/B.BAT; each server's
# results; FREE.OUT of each EATRUN line that runs CHKDSK, at its KB (a
# byte short in fail); and for each client line of run Rn error level 0
# and the golden as the link shows it, TEST over COM1 and a NetBIOS name
# over NET, RPTHELP leaving 3100 + n stack bytes unused and DEEPA 3000 +
# n; but by scenario level 2 and no snapshot for every run (fail) or R2
# (r2), no snapshot for R3 (nosnp), a NetBIOS name over COM1 in R1, COM
# over NET in R2 and TEST over NET in R4 (wrong), a station of lower-case
# hexadecimal digits over NET in R4 (lower); and exit status 1 in vmfail,
# else 0.
cat >"$stree/TEST/INTEG/run2vm.sh" <<'EOF'
rm -rf "$OUT"
mkdir -p "$OUT/A/D" "$OUT/B/D"
: >"$OUT/.run2vm"
put() { printf '%s\r\n' "$2" >"$OUT/$1"; }
for n in SEEDSRV RISRV; do
    put A/D/$n.RC 0
    put B/D/$n.WRC 0
    put B/D/$n.DRC 0
    printf '2026-10-08 12:00:00 0 %s\r\n' "START $n" 'STACK 5000' 'STOP 0' \
        >"$OUT/A/D/$n.LOG"
done
put A/D/CHKDSK.OUT '    600000 bytes free'
put B/D/CHKDSK.OUT '    590000 bytes free'
tr -d '\r' <"$3/SCRB.BAT" >"$OUT/B.BAT"
re='^EATRUN \([0-9]*\) C:\\DOS\\CHKDSK\.COM C: > \(R[0-9]*\)\\FREE\.OUT$'
sed -n "s/$re/\\1 \\2/p" "$OUT/B.BAT" | while read -r k d; do
    mkdir -p "$OUT/B/D/$d"
    less=0
    [ "$VT_SCEN" != fail ] || less=1
    put "B/D/$d/FREE.OUT" "    $((k * 1024 - less)) bytes free"
done
re='^.*\.EXE \(\/[A-Z0-9]*\) .*\/SNAP:\(R[0-9]*\)\\\([A-Z]*\)\.SNP$'
sed -n "s/$re/\\1 \\2 \\3/p" "$OUT/B.BAT" | while read -r sw d x; do
    mkdir -p "$OUT/B/D/$d"
    case $VT_SCEN:$d in
    fail:*|r2:R2) put "B/D/$d/$x.RC" 2; continue ;;
    nosnp:R3) put "B/D/$d/$x.RC" 0; continue ;;
    esac
    put "B/D/$d/$x.RC" 0
    sta='TEST    '
    con='COM  '
    if [ "$sw" != /COM1 ]; then
        sta=RINV0A1F
        con='NET  '
    fi
    case $VT_SCEN:$d in
    wrong:R1) sta=RINV0123 ;;
    wrong:R2) con='COM  ' ;;
    wrong:R4) sta='TEST    ' ;;
    lower:R4) sta=RINV0a1f ;;
    esac
    u=$((3000 + ${d#R}))
    [ "$x" = DEEPA ] || u=$((u + 100))
    printf '%s\r\n' "| $sta $con  ADMIN 3" '=ATTR 0' "=STACK UNUSED=$u" \
        >"$OUT/B/D/$d/$x.SNP"
done
[ "$VT_SCEN" != vmfail ]
EOF

# sgate SCEN STATUS - run the stub tree's scrgate.sh in scenario SCEN
# with the four runs; it must exit with STATUS.  Its outputs go where
# gate puts them, without the line naming a kept run; its batch's
# EATRUN, client and client error level lines to $work/bat.
sgate() {
    rc=0
    VT_SCEN=$1 OUT=$work/sout.$1 TMPDIR=$gtmp PATH=$work/bin:$PATH \
        sh "$stree/TEST/INTEG/scrgate.sh" -r INVTHIN,COM1,180 \
        -r INVTHIN,NET,188 -r INV,COM1 -r INV,NET,292 >"$work/go" \
        2>"$work/ge" || rc=$?
    checks=$((checks + 1))
    [ "$rc" -eq "$2" ] || bad "scrgate.sh, $1: exit status $rc, not $2"
    for f in go ge; do
        sed -e "s#$work#W#g" -e 's#/scrgate\.[A-Za-z0-9]*/#/P/#g' \
            -e '/: FAIL; run kept in /d' "$work/$f" >"$work/${f#g}"
    done
    b=$work/sout.$1/B.BAT
    [ "$2" -eq 0 ] || b=$(echo "$work/sout.$1.fail."*)/B.BAT
    grep -E '^EATRUN|EXE /|^ECHO %RC%> R[0-9]' "$b" >"$work/bat" \
        2>/dev/null || :
}

# cli_lines SERVER SCRIPT - the batch's client and error level lines of
# SCRIPT run against SERVER.
cli_lines() {
    k="/KEYS:$2.KEY"
    echo "EATRUN 180 INVTHIN.EXE /COM1 /CFG:R1\\$2.CFG $k /SNAP:R1\\$2.SNP"
    echo "ECHO %RC%> R1\\$2.RC"
    echo "EATRUN 188 INVTHIN.EXE /NET $1 /CFG:R2\\$2.CFG $k /SNAP:R2\\$2.SNP"
    echo "ECHO %RC%> R2\\$2.RC"
    echo "INV.EXE /COM1 /CFG:R3\\$2.CFG $k /SNAP:R3\\$2.SNP"
    echo "ECHO %RC%> R3\\$2.RC"
    echo "EATRUN 292 INV.EXE /NET $1 /CFG:R4\\$2.CFG $k /SNAP:R4\\$2.SNP"
    echo "ECHO %RC%> R4\\$2.RC"
}

# sout RUN... - the gate's lines of standard output before its verdict
# when the runs RUN (1 to 4) left their snapshots: the servers' stack,
# each run's name and, for RUN, its least stack, then the free memory.
sout() {
    echo 'scrgate.sh: server stack never used: 5000 bytes'
    echo 'scrgate.sh: server stack never used: 5000 bytes'
    n=0
    for r in 'INVTHIN /COM1 with 180 KB free' \
        'INVTHIN /NET with 188 KB free' 'INV /COM1' \
        'INV /NET with 292 KB free'; do
        n=$((n + 1))
        echo "scrgate.sh: run R$n: $r"
        case " $* " in
        *" $n "*) echo "scrgate.sh: stack never used: $((3000 + n))" \
            'bytes at least' ;;
        esac
    done
    echo 'scrgate.sh: VM A free memory with the network: 600000 bytes'
    echo 'scrgate.sh: VM B free memory with the network: 590000 bytes'
}

# sfail SCEN WHAT RUN... - scrgate.sh in scenario SCEN must exit 1 with
# the gate's lines of sout RUN... on standard output and the lines of
# $work/fe on standard error.
sfail() {
    sgate "$1" 1
    grep '^scrgate\.sh: ' "$work/o" >"$work/fs" || :
    sf_what=$2
    shift 2
    same "scrgate.sh $sf_what, standard output" "$work/fs" "$(sout "$@")"
    if [ -s "$work/fe" ]; then
        same "scrgate.sh $sf_what, standard error" "$work/e" \
            "$(cat "$work/fe")"
    else
        same "scrgate.sh $sf_what, standard error" "$work/e"
    fi
}

# lost N CLIENT FLOOR - the standard error lines of run RN, CLIENT the
# program and link, whose client runs exited 2 without a snapshot; with
# FLOOR, the -r word and floor its hint names.
lost() {
    lo_hint=
    [ $# -lt 3 ] || lo_hint=" (with -r $3_FREEKB_<build> may be too low)"
    for x in RPTHELP DEEPA; do
        echo "scrgate.sh: R$1/$x.RC holds '2', not 0"
        echo "scrgate.sh: $2 $x.KEY $nolink$lo_hint"
        echo "scrgate.sh: no R$1/$x.SNP"
    done
}

# short KB - the standard error line of a run started with a byte less
# than KB kilobytes.
short() {
    echo "scrgate.sh: VM B: CHKDSK reports $(($1 * 1024 - 1)) bytes free," \
        "not $1 KB (see TEST/INTEG/free310.sh)"
}

sgate pass 0
same 'scrgate.sh passing, batch' "$work/bat" \
    'EATRUN 180 C:\DOS\CHKDSK.COM C: > R1\FREE.OUT' \
    'EATRUN 188 C:\DOS\CHKDSK.COM C: > R2\FREE.OUT' \
    'EATRUN 292 C:\DOS\CHKDSK.COM C: > R4\FREE.OUT' \
    "$(cli_lines SEEDSRV RPTHELP)" "$(cli_lines RISRV DEEPA)"
same 'scrgate.sh passing, standard output' "$work/o" "$(sout 1 2 3 4)" \
    'scrgate.sh: PASS'
same 'scrgate.sh passing, standard error' "$work/e"

# Every client run fails to open: each is named with the floor of its
# program and link, the run without a KB with none; and each run with a
# KB started with a byte less.
{
    lost 1 'INVTHIN /COM1' 'INVTHIN,COM1,180: THINSCR'
    short 180
    lost 2 'INVTHIN /NET' 'INVTHIN,NET,188: THINSCRNET'
    short 188
    lost 3 'INV /COM1'
    lost 4 'INV /NET' 'INV,NET,292: INVSCRNET'
    short 292
} >"$work/fe"
sfail fail 'failing'

# Only R2 fails to open; only R3 leaves no snapshot; VM B's run fails
# with every file in place.
lost 2 'INVTHIN /NET' 'INVTHIN,NET,188: THINSCRNET' >"$work/fe"
sfail r2 'R2 failing' 1 3 4
printf 'scrgate.sh: no R3/%s.SNP\n' RPTHELP DEEPA >"$work/fe"
sfail nosnp 'R3 without snapshots' 1 2 4
: >"$work/fe"
sfail vmfail 'run2vm.sh failing' 1 2 3 4

# Each status line the link should not show fails: a NetBIOS name over
# COM1 (R1), COM over NET (R2) and the settings' station over NET (R4).
sgate wrong 1
grep 'differs from' "$work/e" | sed 's/ differs from .*//' >"$work/fs" || :
same 'scrgate.sh wrong status lines, differing snapshots' "$work/fs" \
    'scrgate.sh: W/stree/TEST/INTEG/vm/P/R1/RPTHELP.SNP' \
    'scrgate.sh: W/stree/TEST/INTEG/vm/P/R1/DEEPA.SNP' \
    'scrgate.sh: W/stree/TEST/INTEG/vm/P/R2/RPTHELP.SNP' \
    'scrgate.sh: W/stree/TEST/INTEG/vm/P/R2/DEEPA.SNP' \
    'scrgate.sh: W/stree/TEST/INTEG/vm/P/R4/RPTHELP.SNP' \
    'scrgate.sh: W/stree/TEST/INTEG/vm/P/R4/DEEPA.SNP'

# nb_local_name writes upper-case digits, so a station of lower-case
# ones over NET (R4) is no name of its making and differs.
sgate lower 1
grep 'differs from' "$work/e" | sed 's/ differs from .*//' >"$work/fs" || :
same 'scrgate.sh lower-case station, differing snapshots' "$work/fs" \
    'scrgate.sh: W/stree/TEST/INTEG/vm/P/R4/RPTHELP.SNP' \
    'scrgate.sh: W/stree/TEST/INTEG/vm/P/R4/DEEPA.SNP'

# Each malformed command line stops at once with status 2 and says why.
sbad() {
    sb_why=$1
    shift
    rc=0
    sh "$stree/TEST/INTEG/scrgate.sh" "$@" >"$work/o" 2>"$work/e" || rc=$?
    checks=$((checks + 1))
    [ "$rc" -eq 2 ] || bad "scrgate.sh $*: exit status $rc, not 2"
    same "scrgate.sh $*, standard error" "$work/e" "scrgate.sh: $sb_why"
}
use='usage: scrgate.sh -r NAME,LINK[,KB]... [EXEDIR]'
sbad "$use"
sbad "$use" BIN
sbad '-r needs NAME,LINK or NAME,LINK,KB' -r
sbad '-r needs NAME,LINK or NAME,LINK,KB' -r INV
sbad '-r needs an upper-case program name of 8.3' -r inv,COM1
sbad '-r needs the link COM1 or NET' -r INV,COM2
sbad '-r needs the link COM1 or NET' -r INV,NET,1,2
for k in 0 08 640 x; do
    sbad '-r needs a number of KB from 1 to 639' -r INV,NET,$k
done

[ $fails -eq 0 ] || fail "$fails of $checks checks failed"
echo "$prog: $checks checks passed"
