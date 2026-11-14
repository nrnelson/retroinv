#!/bin/sh
# wgagate.sh - the RETROINV server on a Workgroup Add-On for MS-DOS file
# server and its clients on an MS-DOS 3.30 VM with MS Client 3.0's basic
# redirector, the client an 8088 can run, over NetBEUI, a serial line and
# the mailbox on the server's share.
#
# usage: sh TEST/INTEG/wgagate.sh [-u] [EXEDIR]
#        sh TEST/INTEG/wgagate.sh -n
#
#   EXEDIR  directory of one build's INVSERV.EXE, INV.EXE and INVTEST.EXE
#           (default BIN, the Open Watcom build, made first); the message
#           file INVSERV.MSG is the host-built BIN/INVSERV.MSG
#   -u      rewrite TEST/GOLDEN/MBOX.TXT and WGANB.TXT from this run
#           instead of comparing them; review the differences before
#           keeping them
#   -n      check instead the helpers this gate relies on: that pw_check
#           of vmgate.sh finds a planted password (pw_selftest, on the
#           host), that LOSSY.PY's delay passes every frame late enough,
#           in order and unchanged, and that wganb_check judges crafted
#           event logs right (T_RELAY.PY, on the host), and that
#           run2vm.sh refuses an IMAGE_B other than net330 or wga500
#           (exit 2, no VM booted), a missing WGA image (exit 2, no VM
#           booted), and a NetBIOS VM whose NET START runs with the
#           Add-On's SYSTEM.INI (one VM booted, which must fail with
#           "NET START did not succeed", exit 1), and that run2vm.sh with
#           RELAY=delay fails a run whose relay never joined the VMs, as
#           a listener of this script holds the relay's port (two VMs
#           booted, which must fail with "the relay never joined VM B to
#           VM A", exit 1)
#
# The NetBIOS image (mkimg.sh) and the WGA image (mkwga.sh) are built
# first when they are missing or out of date.  One run of run2vm.sh with
# the VMs' COM1 ports joined (SERIAL=1) and VM B's NetBEUI link through
# the relay's delay (RELAY=delay), VM A on the WGA image and VM B on the
# NetBIOS image.  VM A, whose NET START has started the Add-On's
# server, shares D:\DATA as DATA and D:\MBOX as MBOX (NET SHARE), with
# UP.TXT put in MBOX between the two, and runs "INVSERV /CFG:SRV.CFG" from
# D:\ on the fresh data directory DATA, serving NetBIOS sessions, COM1 and
# the mailbox MBOX, its event log D:\SRV.LOG.  VM B meanwhile:
#   1. logs on (NET LOGON with an empty password) and connects E: to MBOX,
#      again until E:\UP.TXT shows that both shares are up, then F: to
#      DATA;
#   2. INVTEST - MBOX.SCR: mailbox sessions of two stations through E:
#      and a session on COM1;
#   3. INVTEST RISRV WGANB.SCR: NetBIOS sessions with the server, one of
#      them idle for longer than the Add-On's keepalive;
#   4. INV /CMD /LOCAL F:\, which must find the data files in use, as the
#      server holds them, and exit 2;
#   5. INVTEST - MBOXEND.SCR, whose mailbox session ends the server with
#      ADMIN SHUTDOWN, its transcript appended to MBOX.SCR's;
#   6. once VM A has put DOWN.TXT in MBOX after INVSERV ended, INV /CMD
#      /LOCAL F:\ again, which now opens the data and exits 0;
#   7. puts END.TXT in MBOX, which VM A waits for before it ends, so that
#      it serves the shares as long as VM B uses them.
# The markers are written under another name and renamed, so that the
# other VM never reads half of one.  The relay's delay keeps the NetBIOS
# sessions alive: under QEMU a NetBEUI station, on either VM, sometimes
# ignores the answer to its own poll when it comes back within a
# millisecond, polls again, ignores each fast answer again and after
# about half a minute ends the link with every session on it.  The
# Add-On can so end a session idle for half a minute at its first
# keepalive, when that polls, and a session request held up by the stall
# can leave a half-open session that the server holds.
#
# Checks: VM A runs MS-DOS 5.00 and VM B MS-DOS 3.30 (VER); both NET
# SHAREs, the logon and both NET USEs report success; INVSERV, the three
# INVTESTs and the second INV exit 0, the first INV 2; the transcripts, CR
# removed and the figures that vary masked (golden_check of vmgate.sh),
# equal TEST/GOLDEN/MBOX.TXT (MBOX.SCR and MBOXEND.SCR) and WGANB.TXT
# (WGANB.SCR); MBOX holds only the three markers at the end; the event log
# is well formed, with one START, STACK and STOP 0 event, the STACK at
# least SRV_STACK_MARGIN (log_check of vmgate.sh), and its NetBIOS
# sessions pass wganb_check of wganb.sh; no new password (pw_check of
# vmgate.sh) is in any file of VM B's D: but the scripts, in any file of
# VM A's D: or anywhere on that disk; and
# CHKDBF.PY --strict is content with every file of the data directory.
# The free conventional memory with the network loaded is printed for
# both VMs.
#
# On failure every program's output is printed, and the result directory
# is kept as OUT.fail.<timestamp>.
#
# env: TIMEOUT  per-VM limit in seconds (default 300), times DOSTIME_SCALE
#      OUT      result directory (default TEST/INTEG/vm/wgagate.run); it
#               must be new or an earlier OUT
#      RELAY, RELAY_MS  as run2vm.sh takes them (default RELAY=delay)
#      PORT_A, PCAP, NET330_DIR, WGA500_DIR  as run2vm.sh takes them
#      DOS500_DIR, WGA_DIR  as mkwga.sh takes them
#
# Exit status: 0 when every check passed; 1 on failure; 2 on a setup
# error, run2vm.sh's included; 129, 130 or 143 when stopped by SIGHUP,
# SIGINT or SIGTERM (run2vm.sh kills its VMs first).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
. "$here/wga500.sh"
. "$here/vmgate.sh"
. "$here/wganb.sh"

usage="usage: wgagate.sh [-u] [EXEDIR] | -n"

# images [wga] - build the NetBIOS image, and with "wga" the WGA image too,
# unless they are current.
images() {
    image_current || sh "$here/mkimg.sh" ||
        die "the NetBIOS image could not be built: run sh TEST/INTEG/mkimg.sh"
    [ "${1:-}" = wga ] || return 0
    wga_current || sh "$here/mkwga.sh" ||
        die "the WGA image could not be built: run" \
            "sh TEST/INTEG/mkwga.sh [DOS500_DIR [WGA_DIR]]"
}

# refused STATUS TEXTS VAR=VALUE... sh run2vm.sh DIR CMD - run run2vm.sh
# with the variables VAR set and check that it exits STATUS and prints
# every line of TEXTS, in their order; a refusal with status 2 must come
# before any boot, which leaves no OUT.
refused() {
    rf_want=$1
    printf '%s\n' "$2" >"$scratch/want"
    shift 2
    rf_rc=0
    rm -rf "$scratch/out"
    env OUT="$scratch/out" "$@" >"$scratch/run.out" 2>&1 || rf_rc=$?
    rf_ok=1
    [ "$rf_rc" -eq "$rf_want" ] || rf_ok=0
    [ "$rf_want" -ne 2 ] || [ ! -e "$scratch/out" ] || rf_ok=0
    awk 'NR == FNR { want[++n] = $0; next }
        k < n && index($0, want[k + 1]) { k++ }
        END { exit k < n }' "$scratch/want" "$scratch/run.out" || rf_ok=0
    [ "$rf_ok" -eq 0 ] || return 0
    cat "$scratch/run.out" >&2
    echo "$prog: run2vm.sh with $*: exit $rf_rc; wanted $rf_want and" \
         "every line of, in order:" >&2
    cat "$scratch/want" >&2
    status=1
}

# says FILE COUNT TEXT - check that COUNT lines of FILE hold TEXT.
says() {
    n=$(tr -d '\r' 2>/dev/null <"$1" | grep -cF -- "$3") || :
    [ "${n:-0}" -eq "$2" ] && return 0
    echo "$prog: ${1#"$OUT"/} holds \"$3\" ${n:-0} times, not $2" >&2
    status=1
}

# pw_case STATUS WHAT SETUP - plant a scratch tree $scratch/pw/p holding
# a copy of each script of pw_selftest, run the shell commands SETUP in
# it and check that pw_check over it exits STATUS.
pw_case() {
    rm -rf "$scratch/pw/p"
    mkdir -p "$scratch/pw/p/sub"
    cp "$scratch/pw/A.SCR" "$scratch/pw/B.SCR" "$scratch/pw/C.TXT" \
       "$scratch/pw/p/sub/"
    (cd "$scratch/pw/p" && eval "$3")
    pc_st=0
    (status=0
     pw_check "$scratch/pw/A.SCR" "$scratch/pw/B.SCR" "$scratch/pw/C.TXT" \
         -- "$scratch/pw/p" "$scratch/pw/p/IMG"
     exit "$status") >"$scratch/pw/out" 2>&1 || pc_st=$?
    chmod -R u+rwx "$scratch/pw/p"
    [ "$pc_st" -eq "$1" ] && return 0
    sed 's/^/  /' "$scratch/pw/out" >&2
    echo "$prog: pw_check with $2: exit $pc_st, not $1" >&2
    status=1
}

# pw_selftest - pw_check's cases: a leak of each PASSWD form, a copy of a
# script under its own name and under a near name, a missing place and
# a file grep cannot read.
pw_selftest() {
    mkdir "$scratch/pw"
    printf '@MBOX 1 X E:\\\r\n>& PASSWD ADMIN Pw1amp\r\n' >"$scratch/pw/A.SCR"
    printf '>  PASSWD\tPw1amp Pw2gt\r\n' >"$scratch/pw/B.SCR"
    printf 'PASSWD Pw2gt Pw3bare\n' >"$scratch/pw/C.TXT"
    pw_case 0 'nothing planted' ': >IMG'
    pw_case 1 'a ">&" leak' 'echo xPw1ampx >IMG'
    pw_case 1 'a ">" leak' ': >IMG; echo Pw2gt >sub/X.OUT'
    pw_case 1 'a bare leak' ': >IMG; echo Pw3bare >Y.OUT'
    pw_case 0 'a script copy' ': >IMG; cp sub/A.SCR A.SCR'
    pw_case 1 'a near name' ': >IMG; cp sub/A.SCR A.SC'
    pw_case 1 'a missing place' ':'
    pw_case 1 'an unreadable file' ': >IMG; : >U; chmod 000 U'
}

if [ "${1:-}" = -n ]; then
    [ $# -eq 1 ] || die "$usage"
    images
    mkdir -p "$here/vm"
    scratch=$(mktemp -d "$here/vm/wgagate.XXXXXX") ||
        die "cannot create a scratch directory"
    holder=
    trap 'rm -rf "$scratch"; [ -z "$holder" ] || kill "$holder" 2>/dev/null' \
        EXIT
    on_signals
    status=0
    pw_selftest
    python3 -I "$here/T_RELAY.PY" ||
        { echo "$prog: T_RELAY.PY failed" >&2; status=1; }
    mkdir "$scratch/empty" "$scratch/wganet"
    refused 2 'IMAGE_A and IMAGE_B must be net330 or wga500' \
        IMAGE_B=foo sh "$here/run2vm.sh" - VER
    refused 2 "no current image in $scratch/empty; run mkwga.sh" \
        WGA500_DIR="$scratch/empty" IMAGE_A=wga500 \
        sh "$here/run2vm.sh" - VER
    # The NetBIOS VM's own SYSTEM.INI is replaced just before NET START,
    # whose Add-On server Client 3.0 does not know.
    sed "s/@COMPUTERNAME@/RIVMA/g" "$here/WGA/SYSTEM.INI" | dos_text \
        >"$scratch/wganet/WGASYS.INI"
    printf '@ECHO OFF\r\n%s\r\n%s\r\n' \
        'COPY D:\WGASYS.INI C:\NET\SYSTEM.INI > NUL' 'C:\NET\NET START' \
        >"$scratch/wganet/WGANET.BAT"
    refused 1 'VM A: NET START: Error 2185: The service name is invalid.
VM A failed: NET START did not succeed' \
        NETSTART='C:\COMMAND /C D:\WGANET.BAT' \
        sh "$here/run2vm.sh" "$scratch/wganet" VER
    # A listener of this script's own holds the relay's port, so that
    # LOSSY.PY cannot bind it and VM B's link ends at the listener, never
    # reaching VM A.  It writes that port and a spare one for VM A, both
    # outside the port slots' bands, and keeps every link it accepts
    # until it is killed or an hour passes.
    python3 -I -c '
import os, socket, sys, time
hold = socket.socket()
hold.bind(("127.0.0.1", 0))
hold.listen(4)
spare = socket.socket()
spare.bind(("127.0.0.1", 0))
with open(sys.argv[1] + ".tmp", "w") as f:
    f.write("%d %d\n" % (hold.getsockname()[1], spare.getsockname()[1]))
spare.close()
os.replace(sys.argv[1] + ".tmp", sys.argv[1])
hold.settimeout(1)
links = []
end = time.monotonic() + float(sys.argv[2])
while time.monotonic() < end:
    try:
        links.append(hold.accept()[0])
    except socket.timeout:
        pass
' "$scratch/ports" 3600 &
    holder=$!
    i=0
    while [ ! -s "$scratch/ports" ] && [ "$i" -lt 100 ]; do
        sleep 0.1
        i=$((i + 1))
    done
    read -r port_b port_a <"$scratch/ports" ||
        die "the listener on the relay's port did not start"
    rj='run2vm.sh: relay: the relay never joined VM B to VM A'
    refused 1 "$rj (no RELAY.LOG)" \
        RELAY=delay PORT_A="$port_a" PORT_B="$port_b" NETSTART= \
        sh "$here/run2vm.sh" - VER - VER
    kill "$holder" 2>/dev/null || :
    wait "$holder" 2>/dev/null || :
    holder=
    [ "$status" -eq 0 ] || fail "FAIL"
    echo "$prog: pw_check, the relay's delay, wganb_check, run2vm.sh's" \
         "refusals and its relay check PASS"
    exit 0
fi

update=0
[ "${1:-}" != -u ] || { update=1; shift; }
[ $# -le 1 ] || die "$usage"
case ${1:-} in -*) die "$usage" ;; esac
exedir=${1:-BIN}
TIMEOUT=${TIMEOUT:-300}
OUT=${OUT:-"$here/vm/wgagate.run"}
RELAY=${RELAY:-delay}

OUT=$(out_check "$OUT" .run2vm) || exit 2
export TIMEOUT OUT RELAY
[ "$update" -eq 1 ] || [ -f "$repo/TEST/GOLDEN/WGANB.TXT" ] ||
    die "no golden TEST/GOLDEN/WGANB.TXT for WGANB.SCR: write it, or" \
        "run wgagate.sh -u and review it"

need_tools python3 mktemp
images wga
progs="BIN/QEXIT.COM BIN/INVSERV.MSG"
[ "$exedir" != BIN ] ||
    progs="$progs BIN/INVSERV.EXE BIN/INV.EXE BIN/INVTEST.EXE"
make -s -C "$repo" $progs || die "the programs could not be built"
case $exedir in /*) ;; *) exedir="$repo/$exedir" ;; esac
for f in INVSERV.EXE INV.EXE INVTEST.EXE; do
    [ -f "$exedir/$f" ] || die "not found: $exedir/$f"
done

gate_start wgagate

a="$payload/A"
b="$payload/B"
srv_files "$a" "$exedir"
mkdir -p "$b"
cp "$exedir/INV.EXE" "$exedir/INVTEST.EXE" "$here/MBOX.SCR" \
   "$here/WGANB.SCR" "$here/MBOXEND.SCR" "$b/"
printf '\r\n' >"$b/ENTER.TXT"
{
    printf '@ECHO OFF\r\nVER\r\n'
    printf 'C:\\NET\\NET SHARE DATA=D:\\DATA /FULL /SAVESHARE:NO /YES'
    printf ' > SHARE.OUT\r\n'
    printf 'ECHO UP> MBOX\\UP.TMP\r\nREN MBOX\\UP.TMP UP.TXT\r\n'
    printf 'C:\\NET\\NET SHARE MBOX=D:\\MBOX /FULL /SAVESHARE:NO /YES'
    printf ' >> SHARE.OUT\r\n'
    printf 'INVSERV.EXE /CFG:SRV.CFG\r\n'
    rc_to SRV.RC
    printf 'ECHO DOWN> MBOX\\DOWN.TMP\r\nREN MBOX\\DOWN.TMP DOWN.TXT\r\n'
    printf ':END\r\nIF NOT EXIST MBOX\\END.TXT GOTO END\r\n'
} >"$a/WGAA.BAT"
{
    printf '@ECHO OFF\r\nVER\r\n'
    printf 'C:\\NET\\NET LOGON RIVMB /YES /SAVEPW:NO < ENTER.TXT'
    printf ' > LOGON.OUT\r\n'
    printf ':USE\r\nC:\\NET\\NET USE E: \\\\RIVMA\\MBOX /YES > USEMBOX.OUT\r\n'
    printf 'IF NOT EXIST E:\\UP.TXT GOTO USE\r\n'
    printf 'C:\\NET\\NET USE F: \\\\RIVMA\\DATA /YES > USEDATA.OUT\r\n'
    printf 'INVTEST.EXE - MBOX.SCR > MBOX.OUT\r\n'
    rc_to MBOX.RC
    printf 'INVTEST.EXE RISRV WGANB.SCR > WGANB.OUT\r\n'
    rc_to WGANB.RC
    printf 'INV.EXE /CMD /LOCAL F:\\ < NUL > INUSE.OUT\r\n'
    rc_to INUSE.RC
    printf 'INVTEST.EXE - MBOXEND.SCR >> MBOX.OUT\r\n'
    rc_to MBOXEND.RC
    # Without the shutdown no DOWN.TXT comes: end the wait at once.
    printf 'IF NOT %%RC%%==0 GOTO END\r\n'
    printf ':DOWN\r\nIF NOT EXIST E:\\DOWN.TXT GOTO DOWN\r\n'
    printf 'INV.EXE /CMD /LOCAL F:\\ < NUL > LOCAL.OUT\r\n'
    rc_to LOCAL.RC
    printf ':END\r\nECHO END> E:\\END.TMP\r\nREN E:\\END.TMP END.TXT\r\n'
} >"$b/WGAB.BAT"

rc=0
IMAGE_A=wga500 SERIAL=1 \
    sh "$here/run2vm.sh" "$a" 'C:\COMMAND /C WGAA.BAT' \
                         "$b" 'C:\COMMAND /C WGAB.BAT' &
runpid=$!
wait "$runpid" || rc=$?
runpid=
# Any status but 0 and 1 (a setup error, a signal) ends the run here and
# leaves OUT as it is.
case $rc in 0|1) ;; *) exit "$rc" ;; esac
[ "$rc" -eq 0 ] || status=1

da="$OUT/A/D"
db="$OUT/B/D"
says "$da/OUT.TXT" 1 'MS-DOS Version 5.00'
says "$db/OUT.TXT" 1 'MS-DOS Version 3.30'
says "$da/SHARE.OUT" 2 "$net_ok"
for f in LOGON USEMBOX USEDATA; do
    says "$db/$f.OUT" 1 "$net_ok"
done

expect "$da/SRV.RC" 0
expect "$db/INUSE.RC" 2
expect "$db/MBOX.RC" 0
expect "$db/WGANB.RC" 0
expect "$db/MBOXEND.RC" 0
expect "$db/LOCAL.RC" 0
golden_check "$db" MBOX:MBOX WGANB:WGANB

left=$(cd "$da/MBOX" 2>/dev/null && find . -type f | sed 's|^\./||' | sort |
       tr '\n' ' ') || :
[ "$left" = 'DOWN.TXT END.TXT UP.TXT ' ] ||
    { echo "$prog: MBOX holds $left" >&2; status=1; }
log_check "$da/SRV.LOG" 1
wganb_check "$da/SRV.LOG" || status=1

pw_check "$here/MBOX.SCR" "$here/WGANB.SCR" "$here/MBOXEND.SCR" -- \
    "$da" "$OUT/A/D.IMG" "$db"

python3 -I "$repo/TEST/CHKDBF.PY" --strict --quiet "$da"/DATA/* || status=1

free_print A B

gate_end "$da/SRV.LOG" "$da/SHARE.OUT" "$db/LOGON.OUT" "$db/USEMBOX.OUT" \
    "$db/USEDATA.OUT" "$db/MBOX.OUT" "$db/WGANB.OUT"
