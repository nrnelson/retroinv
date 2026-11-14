#!/bin/sh
# smbprobe.sh - prove the TCP/IP VM's mailbox share: MS Network Client 3.0
# on MS-DOS 3.30 connects to a private smbd on the host and exchanges
# files with it through REQUEST and RESPONSE.
#
# usage: sh TEST/INTEG/smbprobe.sh
#
# env: TIMEOUT    host-side limit in seconds for the VM run (default 180),
#                 times DOSTIME_SCALE (dos_secs of TEST/shlib.sh)
#      KEEP_VM=1  keep the scratch directory even when the probe passes
#      TCP330_DIR, NET330_DIR, PORT_LOCKS  as mktcpc.sh and smbd.sh take
#                 them
#
# The TCP/IP image is built first when it is missing or out of date
# (mktcpc.sh).  smbd.sh serves the run's directory share/; the VM boots a
# copy-on-write C: from the image and a fresh D: with RUN.BAT, and QEMU's
# user network, restricted to that one forward, joins the guest's
# connections to SMB_ADDR:139 to smbd, each through its own nc (which
# must take -q, as the traditional and the OpenBSD netcat do).  Before
# the boot the host puts REQUEST\HOST1.REQ in the share, written as
# HOST1.TMP and renamed.  The VM starts the network client (NETUP.BAT),
# connects E: to the share, records CHKDSK's free memory, types
# HOST1.REQ and deletes it, writes RESPONSE\VM1.TMP and renames it to
# VM1.RSP, then waits for RESPONSE\HOST2.ACK, which the host writes, again
# by rename, as soon as it sees VM1.RSP; the VM types the answer and ends
# through QEXIT.
#
# Checks: the VM ran to its end; the connection was made with the LANMAN2
# dialect "DOS LM1.2X002" (from smbd's log, at the level SMB/SMB.CNF
# sets); the host's request read back unchanged and was deleted; the VM's
# file arrived under its final upper-case name with its content and no
# temporary name left; the host's answer read back unchanged; every name
# in the share is an upper-case 8.3 name.  The free conventional memory
# with the client loaded and E: connected is printed.
#
# Each run works in its own directory TEST/INTEG/vm/smbprobe.XXXXXX,
# removed on success and on interruption and kept (and named) on failure.
#
# Exit status: 0 when the probe passes, 1 when it fails, 2 on a setup
# error, 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/tcp330.sh"

[ $# -eq 0 ] || die "usage: smbprobe.sh"
TIMEOUT=${TIMEOUT:-180}
KEEP_VM=${KEEP_VM:-0}

qexit="$repo/BIN/QEXIT.COM"
tcp_traps

is_uint "$TIMEOUT" && [ "$TIMEOUT" -gt 0 ] ||
    die "TIMEOUT must be a positive integer: $TIMEOUT"
limit=$(dos_secs "$TIMEOUT")

need_tools qemu-system-i386 mcopy mtype nc timeout make
tcp_current || sh "$here/mktcpc.sh" ||
    die "the TCP/IP image could not be built"
make -s -C "$repo" BIN/QEXIT.COM >/dev/null ||
    die "BIN/QEXIT.COM could not be built"

vm=$(mktemp -d "$here/vm/smbprobe.XXXXXX") ||
    die "cannot create a scratch directory"
share="$vm/share"
mkdir "$share"
sh "$here/smbd.sh" start "$vm/smbd" "$share" || die "smbd did not start"
port=$(cat "$vm/smbd/PORT")

req="REQUEST FROM HOST $$"
rsp="RESPONSE FROM VM $$"
ack="ANSWER FROM HOST $$"
printf '%s\r\n' "$req" >"$share/REQUEST/HOST1.TMP"
mv "$share/REQUEST/HOST1.TMP" "$share/REQUEST/HOST1.REQ"

# D: the run's batch file, the VM's response and QEXIT.  QEXIT 0 ends QEMU
# with status 1.
d=$(tcp_payload "$vm")
printf '%s\r\n' "$rsp" >"$vm/VM1.TXT"
dos_text >"$vm/RUN.BAT" <<EOF
@ECHO OFF
CALL C:\\NET\\NETUP D:\\NETSTART.OUT
NET USE E: \\\\$SMB_NAME\\$SMB_SHARE /YES > D:\\NETUSE.OUT
CHKDSK C: > D:\\CHKDSK.OUT
TYPE E:\\REQUEST\\HOST1.REQ > D:\\HOST1.OUT
DEL E:\\REQUEST\\HOST1.REQ
COPY D:\\VM1.TXT E:\\RESPONSE\\VM1.TMP > NUL
REN E:\\RESPONSE\\VM1.TMP VM1.RSP
:WAIT
IF NOT EXIST E:\\RESPONSE\\HOST2.ACK GOTO WAIT
TYPE E:\\RESPONSE\\HOST2.ACK > D:\\HOST2.OUT
ECHO OK> D:\\DONE.TXT
D:\\QEXIT 0
EOF
for f in "$vm/RUN.BAT" "$vm/VM1.TXT" "$qexit"; do
    mcopy -i "$d" "$f" "::$(basename "$f")" || die "cannot copy $f to D:"
done

# QEMU runs in the background so that a signal reaches the trap at once.
tcp_boot "$vm" "$port" "$limit"

# Answer VM1.RSP as soon as it appears, and bound the time the host took
# to see the rename: from the last poll that still saw VM1.TMP, or from
# the poll before.
status=0
seen=
n=0
until [ -f "$share/RESPONSE/VM1.RSP" ]; do
    if ! kill -0 "$qpid" 2>/dev/null || [ $n -ge $((limit * 10)) ]; then
        echo "$prog: RESPONSE/VM1.RSP did not appear" >&2
        status=1
        break
    fi
    [ ! -f "$share/RESPONSE/VM1.TMP" ] || seen=$n
    sleep 0.1
    n=$((n + 1))
done
if [ $status -eq 0 ]; then
    polls=$((n - ${seen:-$((n - 1))}))
    echo "$prog: rename seen by the host within $((polls * 100)) ms"
    printf '%s\r\n' "$ack" >"$share/RESPONSE/HOST2.TMP"
    mv "$share/RESPONSE/HOST2.TMP" "$share/RESPONSE/HOST2.ACK"
fi
# QEMU's status tells nothing once the probe has stopped it.
killed=
if [ $status -ne 0 ] && kill "$qpid" 2>/dev/null; then
    killed=killed
fi
rc=0
wait "$qpid" || rc=$?
qpid=
sh "$here/smbd.sh" stop "$vm/smbd" || status=1

for f in NETSTART.OUT NETUSE.OUT CHKDSK.OUT HOST1.OUT HOST2.OUT DONE.TXT; do
    mtype -i "$d" "::$f" 2>/dev/null | tr -d '\r' >"$vm/$f" || :
done
for f in NETSTART.OUT NETUSE.OUT CHKDSK.OUT QEMU.ERR; do
    echo "===== $f"
    tr -d '\r' <"$vm/$f"
done

case ${killed:-$rc} in
    killed) ;;
    1) grep -qx OK "$vm/DONE.TXT" || {
           echo "$prog: the VM did not reach the end of RUN.BAT" >&2
           status=1; } ;;
    124) echo "$prog: VM timed out after $(dos_secs_said "$TIMEOUT")" >&2
         status=1 ;;
    *) echo "$prog: unexpected QEMU exit status $rc" >&2; status=1 ;;
esac
# check WHAT COMMAND... - run COMMAND; on failure report WHAT and fail.
check() {
    what=$1
    shift
    if "$@"; then
        echo "$prog: $what OK"
    else
        echo "$prog: $what FAILED" >&2
        status=1
    fi
}
check "dialect DOS LM1.2X002" \
    grep -q 'Selected protocol DOS LM1.2X002' "$vm/smbd/smbd.log"
check "host's request read by the VM" grep -qxF "$req" "$vm/HOST1.OUT"
check "host's request deleted by the VM" \
    test ! -e "$share/REQUEST/HOST1.REQ"
check "VM's temporary name gone" test ! -e "$share/RESPONSE/VM1.TMP"
check "VM's response content" \
    sh -c 'tr -d "\r" 2>/dev/null <"$1" | grep -qxF "$2"' - \
        "$share/RESPONSE/VM1.RSP" "$rsp"
check "host's answer read by the VM" grep -qxF "$ack" "$vm/HOST2.OUT"
check "upper-case 8.3 names in the share" \
    sh -c 'cd "$1" && find . -mindepth 1 | sed "s|^\./||" |
        awk -v set="$2" "$3"' - "$share" "$DOS83_SET" "$DOS83_AWK"
free=$(sed -n 's/^ *\([0-9][0-9]*\) bytes free$/\1/p' "$vm/CHKDSK.OUT")
echo "$prog: free conventional memory with the client and E:" \
    "${free:-unknown} bytes"

if [ $status -eq 0 ]; then
    echo "$prog: mailbox share of the TCP/IP VM OK"
    [ "$KEEP_VM" = 1 ] || rm -rf "$vm"
fi
[ ! -d "$vm" ] || echo "$prog: scratch files kept in $vm"
exit $status
