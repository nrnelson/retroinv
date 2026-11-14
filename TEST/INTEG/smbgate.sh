#!/bin/sh
# smbgate.sh - the RETROINV server's mailbox on a host share: INVSERV
# without NetBIOS sessions in the TCP/IP VM serves request files that the
# host posts to a private smbd.
#
# usage: sh TEST/INTEG/smbgate.sh [-u] [EXEDIR]
#
#   EXEDIR  directory of one build's INVSERV.EXE (default BIN, the Open
#           Watcom build, made first); the message file INVSERV.MSG is
#           the host-built BIN/INVSERV.MSG
#   -u      rewrite TEST/GOLDEN/SMBMBOX.TXT, and in the Open Watcom
#           build SMBEXP.TXT, from this run instead of comparing them;
#           review the difference before keeping it
#
# The TCP/IP image is built first when it is missing or out of date
# (mktcpc.sh).  smbd.sh serves the run's directory share/ as the
# mailbox; the VM boots as smbprobe.sh's does (tcp_boot of tcp330.sh),
# connects E: to the share, records CHKDSK's free memory and runs, from
# D:\,
# "INVSERV /CFG:SMB.CFG": no NetBIOS session, the mailbox E:\, the data
# directory D:\DATA, fresh, and the event log D:\SRV.LOG.  MBOXCLI.PY
# meanwhile posts requests, each as a .TMP renamed to .REQ, and reads
# each response: HOST1's logs in, changes the password, adds an item and
# lists it; the login ticket flow follows; HOST2's logs in again and ends
# the server with ADMIN SHUTDOWN.
#
# The ticket flow, with IDLE=1 in SMB.CFG, of stations TKA and TKB (the
# expected answers are the protocol's Session text; comments in the
# request scripts below give the readings where it is not explicit):
# a ticket is issued and redeemed; PASSWD revokes it and issues a new
# one; the old one is refused; the new one is refused under another
# station and dropped for its holder; BYE TICKET=END ends a ticket; one
# idle for IDLE= minutes is refused.  The expiry runs in the Open Watcom
# build only, as it takes IDLE= minutes and its code is portable C, the
# same in every build; its answers are TEST/GOLDEN/SMBEXP.TXT, not
# SMBMBOX.TXT.  Each refusal is a 401 that closes its session at once.
#
# Checks: INVSERV exits 0; the responses, after "=STATION" lines, equal
# TEST/GOLDEN/SMBMBOX.TXT; the share holds nothing else at the end; the
# event log is well formed, with one START, STACK and STOP 0 event, the
# STACK at least SRV_STACK_MARGIN (log_check of vmgate.sh); no new
# password and no ticket (those MBOXCLI.PY saw) is in any file of the
# share or of D:; and CHKDBF.PY --strict is content with every file of
# the data directory.  The free
# conventional memory with the client and E: is printed.
#
# env: TIMEOUT    host-side limit in seconds for the VM run (default 300),
#                 times DOSTIME_SCALE (dos_secs of TEST/shlib.sh); each
#                 response has as long
#      TCP330_DIR, NET330_DIR, PORT_LOCKS  as mktcpc.sh and smbd.sh take
#                 them
#
# Exit status: 0 when every check passed; 1 on failure; 2 on a setup
# error; 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.  Each run works in
# its own directory TEST/INTEG/vm/smbgate.XXXXXX, removed on success and
# on interruption and kept (and named) on failure.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/tcp330.sh"
. "$here/vmgate.sh"

usage="usage: smbgate.sh [-u] [EXEDIR]"
update=0
[ "${1:-}" != -u ] || { update=1; shift; }
[ $# -le 1 ] || die "$usage"
exedir=${1:-BIN}
# The idle expiry of a ticket takes IDLE= minutes and its code is portable C,
# the same in every build, so only the Open Watcom build (BIN) runs it.
ow=0
[ "$exedir" != BIN ] || ow=1
TIMEOUT=${TIMEOUT:-300}

tcp_traps

is_uint "$TIMEOUT" && [ "$TIMEOUT" -gt 0 ] ||
    die "TIMEOUT must be a positive integer: $TIMEOUT"
limit=$(dos_secs "$TIMEOUT")

need_tools qemu-system-i386 mcopy nc timeout make python3
tcp_current || sh "$here/mktcpc.sh" ||
    die "the TCP/IP image could not be built"
progs="BIN/QEXIT.COM BIN/INVSERV.MSG"
[ "$exedir" != BIN ] || progs="$progs BIN/INVSERV.EXE"
make -s -C "$repo" $progs || die "the programs could not be built"
case $exedir in /*) ;; *) exedir="$repo/$exedir" ;; esac
[ -f "$exedir/INVSERV.EXE" ] || die "not found: $exedir/INVSERV.EXE"

mkdir -p "$here/vm"
vm=$(mktemp -d "$here/vm/smbgate.XXXXXX") ||
    die "cannot create a scratch directory"
share="$vm/share"
mkdir "$share"
sh "$here/smbd.sh" start "$vm/smbd" "$share" || die "smbd did not start"
port=$(cat "$vm/smbd/PORT")

# The requests, CR LF ended.
password=Smb2gate
password2=Tkt2gate
dos_text >"$vm/HOST1.TXT" <<EOF
HELLO HOST1
LOGIN ADMIN admin
PASSWD admin $password
ITEM ADD CATEGORY=DRV MFR=Tandon MODEL=TM100-2 QTY=2
ITEM LIST CAT=DRV
BYE
EOF
# The ticket scripts.  @TICKET...@ marks are filled in by MBOXCLI.PY.
# TKA1: a password login that asks for a ticket (t1).
dos_text >"$vm/TKA1.TXT" <<EOF
HELLO TKA
LOGIN ADMIN $password TICKET=NEW
WHOAMI
EOF
# TKA2: the ticket logs in, answering as a password login, with no new one.
dos_text >"$vm/TKA2.TXT" <<EOF
HELLO TKA
LOGIN ADMIN TICKET=@TICKET@
WHOAMI
EOF
# TKA3: a session that holds a ticket gets a fresh one (t2) from PASSWD.
dos_text >"$vm/TKA3.TXT" <<EOF
HELLO TKA
LOGIN ADMIN TICKET=@TICKET@
PASSWD $password $password2
EOF
# TKA4: PASSWD dropped the user's tickets: t1 is refused 401, which closes
# the session at once, so the PING is not answered.
dos_text >"$vm/TKA4.TXT" <<EOF
HELLO TKA
LOGIN ADMIN TICKET=@TICKET-1@
PING
EOF
# TKA5: t2 works.
dos_text >"$vm/TKA5.TXT" <<EOF
HELLO TKA
LOGIN ADMIN TICKET=@TICKET@
PING
EOF
# TKB1: t2 under station TKB is refused 401, which drops t2 for its holder
# too: whoever shows a ticket holds it.  A failure that shows no held
# ticket (a failed password LOGIN, an unknown ticket) drops none.
dos_text >"$vm/TKB1.TXT" <<EOF
HELLO TKB
LOGIN ADMIN TICKET=@TICKET:TKA@
PING
EOF
# TKA6: t2 is gone, refused 401 like any ticket the server does not hold.
dos_text >"$vm/TKA6.TXT" <<EOF
HELLO TKA
LOGIN ADMIN TICKET=@TICKET@
PING
EOF
# TKA7: a new ticket (t3), ended with the farewell.
dos_text >"$vm/TKA7.TXT" <<EOF
HELLO TKA
LOGIN ADMIN $password2 TICKET=NEW
BYE TICKET=END
EOF
# TKA8: t3, dropped by BYE TICKET=END, is refused.
dos_text >"$vm/TKA8.TXT" <<EOF
HELLO TKA
LOGIN ADMIN TICKET=@TICKET@
PING
EOF
# TKA9, Open Watcom only: a new ticket (t4), then idle (wait:) longer
# than IDLE=.
dos_text >"$vm/TKA9.TXT" <<EOF
HELLO TKA
LOGIN ADMIN $password2 TICKET=NEW
EOF
# TKA10, Open Watcom only: the idle ticket is refused.
dos_text >"$vm/TKA10.TXT" <<EOF
HELLO TKA
LOGIN ADMIN TICKET=@TICKET@
PING
EOF
dos_text >"$vm/HOST2.TXT" <<EOF
HELLO HOST2
LOGIN ADMIN $password2
ADMIN SHUTDOWN
EOF

# D: the server, its settings and the run's batch file.
d=$(tcp_payload "$vm")
{
    printf '@ECHO OFF\r\n'
    printf 'CALL C:\\NET\\NETUP D:\\NETSTART.OUT\r\n'
    printf 'NET USE E: \\\\%s\\%s /YES > D:\\NETUSE.OUT\r\n' \
        "$SMB_NAME" "$SMB_SHARE"
    printf 'CHKDSK C: > D:\\CHKDSK.OUT\r\n'
    printf 'D:\r\nCD \\\r\nINVSERV /CFG:SMB.CFG\r\n'
    errlevel_bat 127
    printf 'ECHO %%RC%%> D:\\DONE.TXT\r\nD:\\QEXIT %%RC%%\r\n'
} >"$vm/RUN.BAT"
dos_text "$here/SMB.CFG" >"$vm/SMB.CFG"
mmd -i "$d" ::DATA
for f in "$vm/RUN.BAT" "$vm/SMB.CFG" "$exedir/INVSERV.EXE" \
         "$repo/BIN/INVSERV.MSG" "$repo/BIN/QEXIT.COM"; do
    mcopy -i "$d" "$f" "::$(basename "$f")" || die "cannot copy $f to D:"
done

tcp_boot "$vm" "$port" "$limit"
status=0
# mboxcli N ITEM... - MBOXCLI.PY on the share, its tickets in TICKETSN.TXT.
mboxcli() {
    mc_n=$1
    shift
    python3 -I "$here/MBOXCLI.PY" -t "$limit" -T "$vm/TICKETS$mc_n.TXT" \
        "$share" "$@" || status=1
}
{
    mboxcli 1 HOST1="$vm/HOST1.TXT" TKA="$vm/TKA1.TXT" TKA="$vm/TKA2.TXT" \
        TKA="$vm/TKA3.TXT" TKA="$vm/TKA4.TXT" TKA="$vm/TKA5.TXT" \
        TKB="$vm/TKB1.TXT" TKA="$vm/TKA6.TXT" TKA="$vm/TKA7.TXT" \
        TKA="$vm/TKA8.TXT"
    if [ "$ow" -eq 1 ]; then
        # IDLE=1 is a minute; the wait is 10 s longer, as the VM's clock is
        # not exact.
        [ "$status" -ne 0 ] ||
            mboxcli 2 TKA="$vm/TKA9.TXT" "wait:$(dos_secs 70)" \
                TKA="$vm/TKA10.TXT" >"$vm/EXPIRY.OUT"
    fi
    [ "$status" -ne 0 ] || mboxcli 3 HOST2="$vm/HOST2.TXT"
} >"$vm/MBOX.OUT"
# The server ends the VM once it has stopped; stop it only on a failure.
[ "$status" -eq 0 ] || kill "$qpid" 2>/dev/null || :
rc=0
wait "$qpid" || rc=$?
qpid=
sh "$here/smbd.sh" stop "$vm/smbd" || status=1

mkdir "$vm/D"
mcopy -s -n -i "$d" "::*" "$vm/D/" 2>/dev/null || :
for f in NETSTART.OUT NETUSE.OUT CHKDSK.OUT QEMU.ERR MBOX.OUT; do
    echo "===== $f"
    if [ -f "$vm/D/$f" ]; then tr -d '\r' <"$vm/D/$f"
    elif [ -f "$vm/$f" ]; then cat "$vm/$f"
    else echo "(missing)"; fi
done

done_code=$(tr -dc '0-9' 2>/dev/null <"$vm/D/DONE.TXT" || :)
if [ "$rc" -eq 124 ]; then
    echo "$prog: VM timed out after $(dos_secs_said "$TIMEOUT")" >&2
    status=1
elif [ -z "$done_code" ] || [ $((rc % 2)) -ne 1 ] ||
        [ $(((rc - 1) / 2)) -ne "$done_code" ]; then
    echo "$prog: the VM did not complete its run (QEMU status $rc," \
         "marker '${done_code}')" >&2
    status=1
elif [ "$done_code" -ne 0 ]; then
    echo "$prog: INVSERV exit $done_code, not 0" >&2
    status=1
fi
golden "$vm/MBOX.OUT" SMBMBOX.TXT MBOX.OUT
[ "$ow" -eq 0 ] || golden "$vm/EXPIRY.OUT" SMBEXP.TXT EXPIRY.OUT
golden_done $((1 + ow))
left=$(cd "$share" && find . -type f | sed 's|^\./||')
[ -z "$left" ] || { echo "$prog: left in the share: $left" >&2; status=1; }
log_check "$vm/D/SRV.LOG" 1
pw_check "$vm/HOST1.TXT" "$vm/TKA3.TXT" -- "$share" "$vm/D" "$vm/D.IMG"
# The server writes no ticket to D: (its log, the data, or a file since
# removed: D.IMG) and leaves none in the share.  MBOXCLI.PY saw three
# tickets, four where the expiry ran.
cat "$vm"/TICKETS?.TXT >"$vm/TICKETS.TXT" 2>/dev/null || :
[ "$(wc -l <"$vm/TICKETS.TXT")" -ge $((3 + ow)) ] || {
    echo "$prog: MBOXCLI.PY saw too few tickets" >&2; status=1; }
if grep -rlF -f "$vm/TICKETS.TXT" "$share" "$vm/D" "$vm/D.IMG" >&2; then
    echo "$prog: a ticket is on a disk" >&2
    status=1
fi
python3 -I "$repo/TEST/CHKDBF.PY" --strict --quiet "$vm"/D/DATA/* ||
    status=1
free=$(tr -d '\r' 2>/dev/null <"$vm/D/CHKDSK.OUT" |
       sed -n 's/^ *\([0-9][0-9]*\) bytes free$/\1/p')
echo "$prog: free conventional memory with the client and E:" \
    "${free:-unknown} bytes"

if [ $status -eq 0 ]; then
    echo "$prog: PASS"
    rm -rf "$vm"
    exit 0
fi
fail "FAIL; scratch files kept in $vm"
