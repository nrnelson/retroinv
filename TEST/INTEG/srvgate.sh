#!/bin/sh
# srvgate.sh - the RETROINV server and its clients between two MS-DOS 3.30
# QEMU VMs over MS Client 3.0 NetBEUI and a serial line.
#
# usage: sh TEST/INTEG/srvgate.sh [-u] [-m KB] [-c KB] [-s KB] [-t NAME]
#                                  [EXEDIR]
#
#   EXEDIR  directory of one build's INVSERV.EXE, INV.EXE and INVTEST.EXE
#           (default BIN, the Open Watcom build, made first); the message
#           file INVSERV.MSG is the host-built BIN/INVSERV.MSG
#   -m KB   start INVSERV with KB kilobytes of free conventional memory
#           (less than one more, KB below 640), as CHKDSK reports it in
#           the same place, to find or check the memory the server needs
#   -c KB   likewise start VM B's INVTEST runs and INV /CMD /NET with KB
#           kilobytes free, to find or check the memory INV /NET needs
#   -s KB   likewise start INV /CMD /COM1 with KB kilobytes free, to find
#           or check the memory INV /COM1 needs
#   -t NAME run NAME.EXE of EXEDIR, the network-only client (THIN of
#           SRCS.MK), wherever INV.EXE runs below; -c and -s then find or
#           check the memory it needs on NET and on COM1
#   -u      rewrite the golden transcripts from this run instead of
#           comparing them; review the difference before keeping it
#
# One run of run2vm.sh, with the VMs' COM1 ports joined (SERIAL=1) and
# their monitors open.  VM A runs, from D:\, "INVSERV /CFG:SRV.CFG" twice
# in a row on the data directory D:\DATA, fresh at the start, its event
# log D:\SRV.LOG; SRV.CFG also serves COM1 and the empty mailbox D:\MBOX.
# VM B, meanwhile:
#   1. INVTEST RISRV NET2.SCR: two NetBIOS sessions with requests in
#      flight at once (its INVTEST @CALL waits for the server to come up);
#   2. INVTEST - SER.SCR: a session on COM1, with requests in flight;
#   3. INV /CMD /COM1 <NETCMD.TXT, with BREAK on, which it must leave
#      on; INV /CMD /COM1 reading the keyboard, into whose BIOS buffer
#      KSTUFF.COM (TEST/DBASE/KSTUFF.ASM) has put Ctrl-C, BYE and Enter
#      (BREAK off, so that COMMAND.COM does not take the Ctrl-C first):
#      the INT 23h that DOS's line input raises must not end it, so that
#      it sends the BYE and writes the +OK; then INV /CMD /NET RISRV
#      <NETCMD.TXT, a request script in the manner of TEST/T_INV.SH's: CR
#      LF line ends but one bare LF, an empty line, a line of 300 bytes, a
#      lower-case command and a request after BYE; the data it changes it
#      sets to the same values each time, so both runs give one
#      transcript;
#   4. INV /CMD /NET RISRV reading the keyboard: the host types HELLO,
#      shuts the first server down from its status screen once the +OK is
#      on VM B's screen (F3, then Y), waits for VM A's screen to count
#      no open session, which the restarted server's screen does once the
#      first has hung up INV's session and ended, and then types PING,
#      which finds the connection gone: INV must say so and exit 9;
#   5. INVTEST RISRV DOWN.SCR against the second server, which ends it
#      with ADMIN SHUTDOWN.
# Each INV run must return: an INV that left a NetBIOS command with the
# provider would hold its exit until a key that no one presses (net_drain
# of INV.C), and the VM would time out.  The NetBIOS client that follows
# each INV run shows that the provider still serves.
# The host then requires exit status 0 of both servers, of every INVTEST
# run and of the INV runs of step 3, 9 of step 4; the transcripts of steps
# 1, 2, 3 and 5 equal to TEST/GOLDEN/NET2.TXT, SER.TXT, NETCMD.TXT (both
# runs) and DOWN.TXT, CR removed and the figures that vary from run to
# run (the K UPTIME= value and the IDLE column of ADMIN CLIENTS) masked
# as "*"; every line of the event log well formed, with two START, STACK
# and STOP 0 events, each STACK at least SRV_STACK_MARGIN (log_check of
# vmgate.sh); no new password (a PASSWD argument of the scripts) in
# any file the VMs left; and CHKDBF.PY --strict content with every file
# of the data directory.  The free conventional memory with the network
# loaded is printed for both VMs.  The VMs' clocks start at the fixed
# time CLOCK, so the dates the server writes are those of the goldens.
#
# On failure, after what qmon.py printed while driving the VMs, the
# gate's verdicts begin with each INV run of VM B that could not open its
# link, as at a memory floor that is the cause; then whether VM B ran its
# batch to the end before the host finished the lost-server case; then
# run2vm.sh's error report of the VMs, whose server failures may follow
# from those; then every check that failed and every program's output.
# The result directory is kept as OUT.fail.<timestamp>.
#
# env: TIMEOUT  per-VM limit in seconds (default 300)
#      OUT      result directory (default TEST/INTEG/vm/srvgate.run); it
#               must be new or an earlier OUT
#      PORT_A, PCAP, NET330_DIR  as run2vm.sh takes them
#
# Exit status: 0 when every check passed; 1 on failure; 2 on a setup
# error, run2vm.sh's included; 129, 130 or 143 when stopped by SIGHUP,
# SIGINT or SIGTERM (run2vm.sh kills its VMs first).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/net330.sh"
. "$here/free310.sh"
. "$here/vmgate.sh"

usage="usage: srvgate.sh [-u] [-m KB] [-c KB] [-s KB] [-t NAME] [EXEDIR]"
CLOCK=$SEED_CLOCK
update=0
srvkb=
clikb=
comkb=
# The client run as INV, and the prefixes of its memory floors in
# MAKEFILE on NET and on COM1 (open_check of vmgate.sh).
client=INV
netfloor=INVNET
comfloor=INVCOM
while [ $# -gt 0 ]; do
    case $1 in
        -u) update=1; shift ;;
        -t) [ $# -ge 2 ] && is_83 "$2.EXE" ||
                die "-t needs an upper-case program name of 8.3"
            client=$2
            netfloor=THINNET
            comfloor=THINCOM
            shift 2 ;;
        -m|-c|-s) [ $# -ge 2 ] && is_uint "$2" && [ "$2" -lt 640 ] ||
                die "$1 needs a number of KB below 640"
            case $1 in
                -m) srvkb=$2 ;;
                -c) clikb=$2 ;;
                *) comkb=$2 ;;
            esac
            shift 2 ;;
        -*) die "$usage" ;;
        *) break ;;
    esac
done
[ $# -le 1 ] || die "$usage"
exedir=${1:-BIN}
TIMEOUT=${TIMEOUT:-300}
OUT=${OUT:-"$here/vm/srvgate.run"}

OUT=$(out_check "$OUT" .run2vm) || exit 2
export TIMEOUT OUT

need_tools python3 mktemp
progs="BIN/QEXIT.COM BIN/EATRUN.COM BIN/KSTUFF.COM BIN/INVSERV.MSG"
[ "$exedir" != BIN ] ||
    progs="$progs BIN/INVSERV.EXE BIN/$client.EXE BIN/INVTEST.EXE"
make -s -C "$repo" $progs || die "the programs could not be built"
case $exedir in /*) ;; *) exedir="$repo/$exedir" ;; esac
for f in INVSERV.EXE "$client.EXE" INVTEST.EXE; do
    [ -f "$exedir/$f" ] || die "not found: $exedir/$f"
done

gate_start srvgate
# run2vm.sh's report, held back until the clients' failures are named.
runerr=$mondir/RUN.ERR
: >"$runerr"

# run_report - print run2vm.sh's report, once; nothing once the exit
# trap has removed it.
run_report() {
    [ -z "$runerr" ] || cat "$runerr" >&2 2>/dev/null || :
    runerr=
}

# srv_halt SIGNAL - the signal handler (on_signals): what halt stops,
# then run_report.
srv_halt() {
    halt "$1"
    run_report
}
on_signals srv_halt

# The line VM B shows when it starts the INV run of the lost-server case.
lostmark='LOST SERVER CASE'
a="$payload/A"
b="$payload/B"
srv_files "$a" "$exedir"
mkdir -p "$b"
cp "$exedir/$client.EXE" "$exedir/INVTEST.EXE" "$repo/BIN/KSTUFF.COM" \
   "$here/NET2.SCR" "$here/SER.SCR" "$here/NETCMD.TXT" "$here/DOWN.SCR" "$b/"
{
    printf '@ECHO OFF\r\n'
    [ -z "$srvkb" ] || eat "$a" "$srvkb" FREE.OUT
    for k in 1 2; do
        printf '%sINVSERV.EXE /CFG:SRV.CFG\r\n' "$(kb_run "$srvkb")"
        rc_to "SRV$k.RC"
    done
} >"$a/SRVA.BAT"
cli=$(kb_run "$clikb")
{
    printf '@ECHO OFF\r\n'
    [ -z "$clikb" ] || eat "$b" "$clikb" FREE.OUT
    [ -z "$comkb" ] || eat "$b" "$comkb" FREECOM.OUT
    printf '%sINVTEST.EXE RISRV NET2.SCR > NET2.OUT\r\n' "$cli"
    rc_to NET2.RC
    printf '%sINVTEST.EXE - SER.SCR > SER.OUT\r\n' "$cli"
    rc_to SER.RC
    printf 'BREAK ON\r\n'
    printf '%s%s.EXE /CMD /COM1 < NETCMD.TXT > COMCMD.OUT\r\n' \
        "$(kb_run "$comkb")" "$client"
    rc_to COMCMD.RC
    printf 'BREAK > COMBRK.OUT\r\n'
    printf 'BREAK OFF\r\n'
    printf "KSTUFF 2E03 'BYE 1C0D\r\n"
    printf '%s.EXE /CMD /COM1 > COMKEY.OUT\r\n' "$client"
    rc_to COMKEY.RC
    printf '%s%s.EXE /CMD /NET RISRV < NETCMD.TXT > NETCMD.OUT\r\n' "$cli" \
        "$client"
    rc_to NETCMD.RC
    printf 'ECHO %s > CON\r\n' "$lostmark"
    printf '%s%s.EXE /CMD /NET RISRV > CON\r\n' "$cli" "$client"
    rc_to LOST.RC
    printf '%sINVTEST.EXE RISRV DOWN.SCR > DOWN.OUT\r\n' "$cli"
    rc_to DOWN.RC
} >"$b/SRVB.BAT"

# The status screen's count of open sessions, at column SS_VAL of
# SRVSCR.C, ends its row; the next row follows it after one space.
opened='Sessions open       '
rc=0
lost=0
MONDIR=$mondir SERIAL=1 QEMU_EXTRA="-rtc base=$CLOCK" \
    sh "$here/run2vm.sh" "$a" 'C:\COMMAND /C SRVA.BAT' \
                         "$b" 'C:\COMMAND /C SRVB.BAT' 2>"$runerr" &
runpid=$!
# B's request is typed as soon as the mark shows, perhaps before INV has
# loaded: its 11 keys wait in the BIOS keyboard buffer, which holds 15.
if ! { qmon B "wait:$lostmark:$(dos_secs 300)" "type:HELLO LOST" key:ret \
               "wait:HELLO LOST +OK:$(dos_secs 60)" &&
       qmon A "wait:${opened}1 :$(dos_secs 30)" key:f3 \
               "wait:Sessions open: 1.:$(dos_secs 30)" key:y \
               "wait:${opened}0 :$(dos_secs 60)" &&
       qmon B type:PING key:ret \
               "wait:connection to the server lost:$(dos_secs 60)"; }
then
    lost=1
    quit_vms
fi
wait "$runpid" || rc=$?
runpid=
# Any status but 0 and 1 (a setup error, a signal) ends the run here and
# leaves OUT as it is.
case $rc in 0|1) ;; *) run_report; exit "$rc" ;; esac
[ "$rc" -eq 0 ] || status=1

da="$OUT/A/D"
db="$OUT/B/D"
open_check "$db" COMCMD "$client /CMD /COM1 <NETCMD.TXT" -s "$comkb" \
    "$comfloor"
open_check "$db" COMKEY "$client /CMD /COM1 (keyboard)"
open_check "$db" NETCMD "$client /CMD /NET <NETCMD.TXT" -c "$clikb" \
    "$netfloor"
open_check "$db" LOST "$client /CMD /NET (lost-server case)" -c "$clikb" \
    "$netfloor"
if [ "$lost" -eq 1 ]; then
    # VM B writes DONE.TXT only when its batch runs to the end, not when
    # quit_vms ends it.
    [ ! -f "$db/DONE.TXT" ] ||
        echo "$prog: VM B ran its batch to the end before the host" \
            "finished the lost-server case" >&2
    echo "$prog: the lost-server case did not go as planned" >&2
    status=1
fi
run_report
expect "$da/SRV1.RC" 0
expect "$da/SRV2.RC" 0
for t in NET2 SER COMCMD COMKEY NETCMD DOWN; do
    expect "$db/$t.RC" 0
done
expect "$db/LOST.RC" 9
if [ "$(tr -d '\r' 2>/dev/null <"$db/COMBRK.OUT")" != 'BREAK is on' ]; then
    echo "$prog: $client /CMD /COM1 did not leave BREAK on" >&2
    status=1
fi
if [ "$(tr -d '\r' 2>/dev/null <"$db/COMKEY.OUT")" != '+OK' ]; then
    echo "$prog: $client /CMD /COM1 did not read BYE after a Ctrl-C" >&2
    status=1
fi

golden_check "$db" NET2:NET2 SER:SER NETCMD:NETCMD COMCMD:NETCMD DOWN:DOWN

log_check "$da/SRV.LOG" 2

pw_check "$here/NET2.SCR" "$here/SER.SCR" "$here/DOWN.SCR" \
    "$here/NETCMD.TXT" -- "$da" "$db"

python3 -I "$repo/TEST/CHKDBF.PY" --strict --quiet "$da"/DATA/* || status=1

free_print A B
free_check A "$srvkb" FREE.OUT
free_check B "$clikb" FREE.OUT
free_check B "$comkb" FREECOM.OUT

gate_end "$da/SRV.LOG" "$db/NET2.OUT" "$db/SER.OUT" "$db/COMCMD.OUT" \
    "$db/NETCMD.OUT" "$db/DOWN.OUT"
