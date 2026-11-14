#!/bin/sh
# nbgate.sh - NetBIOS session test between two MS-DOS 3.30 QEMU VMs.
#
# usage: sh TEST/INTEG/nbgate.sh [-x] [COUNT]
#
# Runs three times through run2vm.sh over MS Client 3.0 NetBEUI, first
# once per server mode: VM A runs "NBTEST SERVER RISRV" (the wait-form
# commands), then "NBTEST SERVER RISRV /ASYNC" (the no-wait commands),
# and VM B each time "NBTEST CLIENT RICLI RISRV /COUNT:COUNT" (default
# 10 PING/PONG round trips and a bulk message the server receives in
# pieces), with /ASYNC too in the second run, where it places its CALL
# as a no-wait command.
# The base image must have been built with mkimg.sh.  Every program's
# output is printed.  Each must exit 0 with standard output as tool_out
# (TEST/shlib.sh) wants for a pass, its last line "NBTEST PASS".  The
# second run also requires each program's no-wait line, VM A's "NBTEST
# LISTEN RISRV from * ..., no-wait ..." and VM B's "NBTEST CALL RISRV
# from RICLI, no-wait ...", and a CALL of VM B that completed with NB_OK
# while still in progress once posted ("rc=00 ... pending=1").  A third
# run, VM A alone, has "NBTEST SERVER RISRV /WAIT:1" wait in vain for a
# call: it must exit 3, the test failed, with the verdict "NBTEST FAIL"
# and a reason.
#
# -x runs instead the loss mode: the wait-form server alone, with VM
# B's link through LOSSY.PY (run2vm.sh RELAY=1), which drops the resent
# bulk data and then every pair of crossing polls.  The session survives
# only while the two VMs' NetBEUI timers differ (net330.sh), so the mode
# also requires the relay's log to show the bulk drop.
#
# Every mode runs even when an earlier one fails.  A failed mode prints
# its VMs' qemu.log and NETSTART.OUT, and the result directory is then
# kept as OUT.fail.<timestamp>.
#
# env: TIMEOUT  per-VM limit in seconds (default 180)
#      PCAP     1 to record each VM's frames (see run2vm.sh)
#      OUT      result directory (default TEST/INTEG/vm/nbgate.run), one
#               run2vm.sh directory per mode in OUT/wait, OUT/async and
#               OUT/fail (OUT/loss with -x);
#               it must be new or an earlier OUT
#      NET330_DIR  image directory (see net330.sh)
#
# Exit status: 0 only when every mode run ended as described above; 1 on
# failure; 2 on a setup error, run2vm.sh's included; 129, 130 or 143 when
# stopped by SIGHUP, SIGINT or SIGTERM (run2vm.sh kills its VMs first).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/net330.sh"

modes="wait async fail"
if [ "${1:-}" = -x ]; then
    modes=loss
    shift
fi
count=${1:-10}
is_uint "$count" && [ "$count" -gt 0 ] || die "COUNT must be a positive integer"
TIMEOUT=${TIMEOUT:-180}
OUT=${OUT:-"$here/vm/nbgate.run"}
marker=.nbgate

# OUT is moved away on failure, so it must be new or an earlier OUT of
# this script or of run2vm.sh; the VM directories run2vm.sh left there
# are removed.
OUT=$(out_check "$OUT" "$marker" .run2vm) || exit 2
[ -f "$OUT/$marker" ] || rm -rf "$OUT/A" "$OUT/B" "$OUT/.run2vm"
export TIMEOUT OUT

make -s -C "$repo" BIN/NBTEST.EXE BIN/QEXIT.COM || die "BIN/NBTEST.EXE could not be built"

mkdir -p "$here/vm" "$OUT"
: >"$OUT/$marker"
payload=$(mktemp -d "$here/vm/nbgate.XXXXXX")
runpid=
# halt SIGNAL - the signal handler (on_signals): send SIGTERM to
# run2vm.sh (it kills its VMs; a background job cannot catch SIGINT) and
# wait for it.
halt() {
    if [ -n "$runpid" ]; then
        kill -s TERM "$runpid" 2>/dev/null || :
        wait "$runpid" 2>/dev/null || :
    fi
}
trap 'rm -rf "$payload"' EXIT
on_signals halt
cp "$repo/BIN/NBTEST.EXE" "$payload/NBTEST.EXE"

status=0
for mode in $modes; do
    relay=0
    async=
    vms="A B"
    wantst=0
    case $mode in
        wait) ;;
        loss) relay=1 ;;
        fail) vms=A; wantst=3 ;;
        *) async=" /ASYNC" ;;
    esac
    echo "$prog: server mode $mode"
    rc=0
    if [ "$mode" = fail ]; then
        OUT="$OUT/$mode" sh "$here/run2vm.sh" "$payload" \
            "NBTEST SERVER RISRV /WAIT:1" &
    else
        RELAY=$relay OUT="$OUT/$mode" sh "$here/run2vm.sh" \
            "$payload" "NBTEST SERVER RISRV$async" \
            "$payload" "NBTEST CLIENT RICLI RISRV /COUNT:$count$async" &
    fi
    runpid=$!
    wait "$runpid" || rc=$?
    runpid=
    case $rc in 0|1) ;; *) exit "$rc" ;; esac

    # run2vm.sh's 1 for the failed run's own status is not a failure.
    [ "$wantst" = 0 ] || rc=0
    for x in $vms; do
        d=$OUT/$mode/$x/D
        st=$(tr -dc '0-9' 2>/dev/null <"$d/DONE.TXT" || :)
        if [ "$st" != "$wantst" ]; then
            echo "$prog: $mode: VM $x NBTEST exit '$st', expected $wantst" >&2
            rc=1
        elif ! why=$(tool_out NBTEST "$d/OUT.TXT" "$st"); then
            echo "$prog: $mode: VM $x: $why" >&2
            rc=1
        fi
    done
    # Each wanted line: the VM, a space, the pattern.
    if [ -n "$async" ]; then
        for want in \
            'A ^NBTEST LISTEN RISRV from \* for [0-9]* s, no-wait \.\.\.$' \
            'B ^NBTEST CALL RISRV from RICLI, no-wait \.\.\.$' \
            'B ^NBTEST CALL RISRV attempt [0-9]* rc=00 lsn=[0-9A-F]*'\
' pending=1$'
        do
            x=${want%% *}
            if ! tr -d '\r' 2>/dev/null <"$OUT/$mode/$x/D/OUT.TXT" |
                    grep -q "${want#* }"; then
                echo "$prog: $mode: VM $x printed no line ${want#* }" >&2
                rc=1
            fi
        done
    fi
    if [ "$relay" = 1 ] && ! grep -q ' B>A bulk$' "$OUT/$mode/RELAY.LOG" 2>/dev/null; then
        echo "$prog: $mode: the relay dropped no resent bulk data" >&2
        rc=1
    fi
    if [ "$rc" -eq 0 ]; then
        echo "$prog: $mode PASS"
        continue
    fi
    status=1
    # Show what the network side did.
    for x in $vms; do
        for f in qemu.log D/NETSTART.OUT; do
            echo "===== $mode VM $x: $f"
            if [ -f "$OUT/$mode/$x/$f" ]; then
                tr -d '\r' <"$OUT/$mode/$x/$f"
            else
                echo "(missing)"
            fi
        done
    done
    if [ "$relay" = 1 ]; then
        echo "===== $mode relay: RELAY.LOG"
        cat "$OUT/$mode/RELAY.LOG" 2>/dev/null || echo "(missing)"
    fi
done
if [ "$status" -eq 0 ]; then
    echo "$prog: PASS"
    exit 0
fi

keep="$OUT.fail.$(date +%Y%m%d-%H%M%S)"
mv "$OUT" "$keep"
fail "FAIL; run kept in $keep"
