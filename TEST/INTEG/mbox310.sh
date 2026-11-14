#!/bin/sh
# mbox310.sh - INVSERV without NetBIOS on MS-DOS 3.10, serving the one
# request file its mailbox holds.
#
# usage: sh TEST/INTEG/mbox310.sh [-u] INVSERV.EXE INVSERV.MSG CONFIG.SYS
#
#   INVSERV.EXE  one build's server
#   INVSERV.MSG  its message file
#   CONFIG.SYS   the boot's CONFIG.SYS, with the FILES= the engine needs
#   -u           rewrite TEST/GOLDEN/SRV310.RSP from this run instead of
#                comparing it; review the difference before keeping it
#
# Boots the Compaq MS-DOS 3.10 floppy (boot310.sh) with the server and
# SRV310.CFG on the scratch hard disk C:, which also gets the empty data
# directory DATA and the mailbox MBOX, and the request
# TEST/INTEG/ADMIN.REQ, given CR LF line ends, copied from the floppy into
# MBOX\REQUEST.  The server, started with SRV310.CFG, has no NetBIOS
# session to serve and none is loaded: it finds the request (DOS's find
# first and next), answers it in MBOX\RESPONSE\ADMIN.TMP and renames that
# to ADMIN.RSP, overwrites the request with zeros and removes it, and ends
# at the request's ADMIN SHUTDOWN, the PING after it refused with 501.
# The request ABC.REQ, served first as it sorts first, logs in with
# TICKET=NEW and ends with BYE TICKET=END: a login ticket's issue and end
# on the floor DOS (a fixed request cannot reuse one, whose digits are
# random).  ADMIN's CAT LIST takes the server's stack deepest of all the
# requests (the listing path), its LOC LIST and LOC CONTENTS 0 ALL=1 (the
# walks of the location tree) nearly as deep; its items, one in a location
# and one deleted, give the walks, the export, reindex and pack before the
# shutdown work to do on the floor DOS.  The run passes when INVSERV exits
# 0, the request is gone, the new password of its PASSWD is nowhere on the
# hard disk, ADMIN.RSP equals TEST/GOLDEN/SRV310.RSP with its CR removed
# and the STACK event of the event log SRV.LOG shows at least
# SRV_STACK_MARGIN (TEST/shlib.sh) stack bytes never used.  It also needs
# ABC.RSP to equal TEST/GOLDEN/SRV310T.RSP, its CR removed and the
# ticket's 32 digits shown as <t>, the ABC request gone, and the ticket
# not in SRV.LOG.
#
# Exit status: 0 when the run passes; 1 when it fails (INVSERV's exit,
# boot310.sh's time-out or emulator end); 2 on a usage or setup error,
# boot310.sh's included; 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$repo/TEST/shlib.sh"
. "$here/vmgate.sh"

usage="usage: mbox310.sh [-u] INVSERV.EXE INVSERV.MSG CONFIG.SYS"
update=0
[ "${1:-}" != -u ] || { update=1; shift; }
[ $# -eq 3 ] || die "$usage"

tmp=$(mktemp -d)
kept=

# cleanup - remove the scratch files and the hard disk boot310.sh kept,
# also when a signal ended the boot (boot310.sh names it then too).
cleanup() {
    [ -n "$kept" ] ||
        kept=$(sed -n 's/^boot310.sh: kept //p' "$tmp/ERR" 2>/dev/null) || :
    rm -rf "$tmp" ${kept:+"$kept"}
}
trap cleanup EXIT
on_signals
dos_text "$here/ADMIN.REQ" >"$tmp/ADMIN.REQ"
# The ticket request logs in with ADMIN's first password, before
# ADMIN.REQ changes it; MUSTCHG is still pending then, as it is for a
# ticket.
dos_text >"$tmp/ABC.REQ" <<EOF
HELLO ABC
LOGIN ADMIN admin TICKET=NEW
BYE TICKET=END
EOF
# Marks in the output before the response, between it and the event log,
# and after the log (ECHO keeps the blank before the boot's redirection).
rsp='=RSP'
tkt='=TKT'
log='=LOG'
end='=END'
rc=0
sh "$here/boot310.sh" -k -t 120 -h "$1" -h "$2" -h "$here/SRV310.CFG" \
    -f "$tmp/ADMIN.REQ" -f "$tmp/ABC.REQ" -f "$3" C: "MD DATA" "MD MBOX" \
    "MD MBOX\\REQUEST" "MD MBOX\\RESPONSE" "COPY A:ADMIN.REQ MBOX\\REQUEST" \
    "COPY A:ABC.REQ MBOX\\REQUEST" \
    "INVSERV /CFG:SRV310.CFG" "ECHO $rsp" "TYPE MBOX\\RESPONSE\\ADMIN.RSP" \
    "ECHO $tkt" "TYPE MBOX\\RESPONSE\\ABC.RSP" \
    "ECHO $log" "TYPE SRV.LOG" "ECHO $end" \
    "IF EXIST MBOX\\REQUEST\\ADMIN.REQ ECHO REQUEST LEFT" \
    "IF EXIST MBOX\\REQUEST\\ABC.REQ ECHO REQUEST LEFT" \
    >"$tmp/OUT" 2>"$tmp/ERR" || rc=$?
grep -v '^boot310.sh: \(kept\|hard disk partition\) ' "$tmp/ERR" >&2 || :
tr -d '\r' <"$tmp/OUT"
status=0
case $rc in
    0) ;;
    200) echo "$prog: the boot timed out" >&2; status=1 ;;
    201) echo "$prog: the emulator ended before the boot did" >&2; status=1 ;;
    *) # 2 is also boot310.sh's own failure, when the boot never ran.
       grep -q "^$end" "$tmp/OUT" || die "boot310.sh exit $rc"
       echo "$prog: INVSERV exit $rc, not 0" >&2
       status=1 ;;
esac
kept=$(sed -n 's/^boot310.sh: kept //p' "$tmp/ERR")
[ -n "$kept" ] && [ -f "$kept/C.IMG" ] || die "boot310.sh kept no hard disk"
if grep -q '^REQUEST LEFT' "$tmp/OUT"; then
    echo "$prog: the request is still in MBOX\\REQUEST" >&2
    status=1
fi
pw=$(sed -n 's/^PASSWD [^ ]* //p' "$here/ADMIN.REQ")
if grep -qF -- "$pw" "$kept/C.IMG"; then
    echo "$prog: the new password is on the hard disk" >&2
    status=1
fi
# The figure of the STACK event right before the log's STOP.
unused=$(tr -d '\r' <"$tmp/OUT" | sed -n "/^$log *\$/,/^$end *\$/p" |
    awk 's != "" && / 0 STOP [0-9]+$/ { print s }
         { s = "" }
         / 0 STACK [0-9]+$/ { s = $NF }')
if [ -z "$unused" ]; then
    echo "$prog: no STACK event right before the STOP in SRV.LOG" >&2
    status=1
else
    echo "$prog: stack never used: $unused bytes"
    if [ "$unused" -lt "$SRV_STACK_MARGIN" ]; then
        echo "$prog: $unused unused stack bytes," \
            "fewer than $SRV_STACK_MARGIN" >&2
        status=1
    fi
fi
tr -d '\r' <"$tmp/OUT" | sed -n "/^$rsp *\$/,/^$tkt *\$/p" | sed '1d;$d' \
    >"$tmp/RSP"
tr -d '\r' <"$tmp/OUT" | sed -n "/^$tkt *\$/,/^$log *\$/p" | sed '1d;$d' \
    >"$tmp/TKT"
# The ticket, then ABC.RSP with it shown as <t>: the digits are random.
ticket=$(sed -n 's/.* TICKET=\([0-9A-F]\{32\}\)$/\1/p' "$tmp/TKT")
if [ -z "$ticket" ]; then
    echo "$prog: ABC.RSP holds no ticket" >&2
    status=1
elif tr -d '\r' <"$tmp/OUT" | sed -n "/^$log *\$/,/^$end *\$/p" |
        grep -qF -- "$ticket"; then
    echo "$prog: the ticket is in the event log" >&2
    status=1
fi
sed -i 's/ TICKET=[0-9A-F]\{32\}$/ TICKET=<t>/' "$tmp/TKT"
golden "$tmp/RSP" SRV310.RSP ADMIN.RSP
golden "$tmp/TKT" SRV310T.RSP ABC.RSP
golden_done 2
[ "$status" -ne 0 ] || echo "$prog: PASS"
exit $status
