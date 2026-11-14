#!/bin/sh
# scrgate.sh - the full-screen client's deepest screens over the network:
# INV's deepest golden-screen key scripts run against the RETROINV
# server between two MS-DOS 3.30 QEMU VMs, over a serial line or NetBIOS.
#
# usage: sh TEST/INTEG/scrgate.sh -r NAME,LINK[,KB]... [EXEDIR]
#
#   EXEDIR  directory of one build's INVSERV.EXE, INVTEST.EXE and the
#           clients the runs name (default BIN, the Open Watcom build,
#           made first); the message files are the host-built
#           BIN/INVSERV.MSG, BIN/INV.MSG and BIN/INV.HLP, and the host INV
#           seeds the data
#   -r NAME,LINK[,KB]  a run of every script by NAME.EXE of EXEDIR, INV
#           or the network-only client (THIN of SRCS.MK), over LINK, COM1
#           or NET, each started with KB kilobytes of free conventional
#           memory (less than one more, KB from 1 to 639), as CHKDSK
#           reports it in the same place, to find or check the memory the
#           deepest screens need there; without KB, with what the VM
#           leaves.  The runs, one or more, go in the order given.
#
# The scripts are those that set INV_FREEKB_<build> (DOC/BUILD.md,
# Runtime requirements): each of TEST/KEYS/ with "#@ DEEP", on the data
# TEST/T_SCR.SH gives it (TEST/KEYS/SEED.REQ, then DEEP.REQ), and
# RPTHELP, the deepest report path, on that of SEED.REQ alone.  The host
# INV seeds both data directories at the fixed time SEED_NOW.
#
# One run of run2vm.sh, with the VMs' COM1 ports joined (SERIAL=1) and
# both clocks starting at CLOCK, that same time, so that the dates the
# server and the client show are those of the goldens.  VM A runs, from
# D:\, INVSERV twice, with TEST/INTEG/SRV.CFG made over for each data
# directory: first called SEEDSRV on SEED.REQ's data, then RISRV on that
# of "#@ DEEP".  For each server in turn VM B waits until it answers a
# NetBIOS session (INVTEST); then, for each run in turn, Rn the run's
# directory (n its number), it runs each of the server's scripts x, from
# D:\ beside INV.MSG, INV.HLP and TEST/KEYS/COLOR.CFG, with a copy of
# the latter of its own (STATION=TEST), as
#   NAME /COM1 /CFG:Rn\x.CFG /KEYS:x.KEY /SNAP:Rn\x.SNP
# over COM1, with "/NET server" in place of /COM1 over NET; and then it
# ends the server with ADMIN SHUTDOWN over COM1 (INVTEST).  The scripts
# leave the data as they found it, so one server serves them all; the
# second server's own name keeps VM B from taking the first, still
# ending, for it.
#
# The host then requires exit status 0 of both servers, of every INVTEST
# run and of every client run; each snapshot file, its "=STACK UNUSED=n"
# line left out, equal to the script's golden TEST/GOLDEN/SCR/x.SNC of
# the local runs as the link must show it (link_view): the status line's
# connection field COM or NET where the golden has LOCAL, and over NET
# its station the NetBIOS name the client calls from; and n at least
# STACK_MARGIN of TEST/T_SCRH.C (snap_check of TEST/shlib.sh); and every
# line of each server's event log well formed, with one START, STACK
# and STOP 0 event (log_check of vmgate.sh).  Each run is named before
# its checks, with the least stack it left; the free conventional memory
# with the network loaded is printed last.
#
# On failure each client run that did not open its link or could not
# load INV.MSG is named (open_check of vmgate.sh), as at a memory floor:
# with KB given, the makefile's floor that may be too low is
# <P>SCR_FREEKB_<build> over COM1 and <P>SCRNET_FREEKB_<build> over NET,
# P being INV for INV and THIN for any other NAME.  The servers' event
# logs and INVTEST's transcripts are printed, and the result directory
# is kept as OUT.fail.<timestamp>.
#
# env: TIMEOUT  per-VM limit in seconds (default 300), times DOSTIME_SCALE
#      OUT      result directory (default TEST/INTEG/vm/scrgate.run); it
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

CLOCK=$SEED_CLOCK
KEYS=$repo/TEST/KEYS
GOLD=$repo/TEST/GOLDEN/SCR
usage="usage: scrgate.sh -r NAME,LINK[,KB]... [EXEDIR]"

# run_split RUN - set cli, link and kb to the NAME, LINK and KB (empty
# for no limit) of RUN, a word NAME,LINK,KB of runs.
run_split() {
    cli=${1%%,*}
    kb=${1##*,}
    link=${1#"$cli",}
    link=${link%,"$kb"}
}

# The runs, in order, each a word NAME,LINK,KB.
runs=
while [ $# -gt 0 ]; do
    case $1 in
        -r) case ${2-} in
                *,*,*) run_split "$2" ;;
                *,*) run_split "$2," ;;
                *) die "-r needs NAME,LINK or NAME,LINK,KB" ;;
            esac
            is_83 "$cli.EXE" ||
                die "-r needs an upper-case program name of 8.3"
            case $link in
                COM1|NET) ;;
                *) die "-r needs the link COM1 or NET" ;;
            esac
            case $kb in
                ''|[1-9]|[1-9][0-9]|[1-5][0-9][0-9]|6[0-3][0-9]) ;;
                *) die "-r needs a number of KB from 1 to 639" ;;
            esac
            runs="$runs $cli,$link,$kb"
            shift 2 ;;
        -*) die "$usage" ;;
        *) break ;;
    esac
done
[ $# -le 1 ] && [ -n "$runs" ] || die "$usage"
exedir=${1:-BIN}
TIMEOUT=${TIMEOUT:-300}
OUT=${OUT:-"$here/vm/scrgate.run"}

OUT=$(out_check "$OUT" .run2vm) || exit 2
export TIMEOUT OUT

# The distinct clients of the runs.
clients=$(for r in $runs; do echo "${r%%,*}.EXE"; done | sort -u)
need_tools python3 mktemp awk
progs="BIN/QEXIT.COM BIN/EATRUN.COM BIN/INVSERV.MSG BIN/INV.MSG BIN/INV.HLP"
progs="$progs OBJ/HOST/INV.bin"
[ "$exedir" != BIN ] ||
    progs="$progs BIN/INVSERV.EXE BIN/INVTEST.EXE $(echo "$clients" |
        sed 's|^|BIN/|')"
make -s -C "$repo" $progs || die "the programs could not be built"
case $exedir in /*) ;; *) exedir="$repo/$exedir" ;; esac
for f in INVSERV.EXE INVTEST.EXE $clients; do
    [ -f "$exedir/$f" ] || die "not found: $exedir/$f"
done
deep=$(grep -l '^#@ DEEP' "$KEYS"/*.KEY | sed 's|.*/||; s|\.KEY$||' |
    paste -s -d , -)
[ -n "$deep" ] || die "no key script in TEST/KEYS has #@ DEEP"
# The servers in the order they run, one line each: its name, its data
# directory, the requests that seed it and the key scripts run against
# it, the last two separated by commas.
servers="SEEDSRV SEED SEED RPTHELP
RISRV DATA SEED,DEEP $deep"
scripts=$(echo "$servers" | cut -d ' ' -f 4 | tr , ' ')
# The stack bytes a snapshot must leave unused, as in TEST/T_SCR.SH.
margin=$(cdefine "$here/../T_SCRH.C" STACK_MARGIN)

gate_start scrgate

a="$payload/A"
b="$payload/B"
srv_files "$a" "$exedir"
mkdir -p "$b"
for f in INVTEST.EXE $clients; do cp "$exedir/$f" "$b/"; done
cp "$repo/BIN/INV.MSG" "$repo/BIN/INV.HLP" "$KEYS/COLOR.CFG" "$b/"
for s in $scripts; do dos_text "$KEYS/$s.KEY" >"$b/$s.KEY"; done
printf '@CALL 1 RIWAIT\n> HELLO RIWAIT\n< +OK\n> BYE\n< +OK\n' >"$b/WAIT.SCR"
{
    printf '@COM 1 COM1\n> HELLO RIDOWN\n< +OK\n> LOGIN ADMIN Retro1986\n'
    printf '<* +OK LEVEL=3 *\n> ADMIN SHUTDOWN\n< +OK\n'
} >"$b/DOWN.SCR"
printf '@ECHO OFF\r\n' >"$a/SRVA.BAT"
{
    printf '@ECHO OFF\r\n'
    n=0
    for r in $runs; do
        n=$((n + 1))
        run_split "$r"
        mkdir "$b/R$n"
        [ -z "$kb" ] || eat "$b" "$kb" "R$n\\FREE.OUT"
    done
} >"$b/SCRB.BAT"
echo "$servers" | while read -r name dir reqs keys; do
    mkdir -p "$a/$dir"
    for r in $(echo "$reqs" | tr , ' '); do
        dos_text "$KEYS/$r.REQ" |
            (cd "$a" && "$repo/OBJ/HOST/INV.bin" /CMD /LOCAL "$dir" \
                /NOW:$SEED_NOW) >>"$payload/$name.SED" ||
            die "the host INV could not seed $dir"
    done
    bad=$(tr -d '\r' <"$payload/$name.SED" | grep -v '^+OK' | head -n 1) ||
        :
    [ -z "$bad" ] || die "the seed of $dir was answered: $bad"
    tr -d '\r' <"$here/SRV.CFG" | sed -e "s/^NAME=.*/NAME=$name/" \
        -e "s/^DATA=.*/DATA=$dir/" -e "s/^LOG=.*/LOG=$name.LOG/" |
        dos_text >"$a/$name.CFG"
    {
        printf 'INVSERV.EXE /CFG:%s.CFG\r\n' "$name"
        rc_to "$name.RC"
    } >>"$a/SRVA.BAT"
    {
        printf 'INVTEST.EXE %s WAIT.SCR > %s.WT\r\n' "$name" "$name"
        rc_to "$name.WRC"
        n=0
        for r in $runs; do
            n=$((n + 1))
            run_split "$r"
            sw=/COM1
            [ "$link" = COM1 ] || sw="/NET $name"
            for s in $(echo "$keys" | tr , ' '); do
                printf 'COPY COLOR.CFG R%s\\%s.CFG > NUL\r\n' "$n" "$s"
                printf '%s%s.EXE %s /CFG:R%s\\%s.CFG' "$(kb_run "$kb")" \
                    "$cli" "$sw" "$n" "$s"
                printf ' /KEYS:%s.KEY /SNAP:R%s\\%s.SNP\r\n' "$s" "$n" "$s"
                rc_to "R$n\\$s.RC"
            done
        done
        printf 'INVTEST.EXE - DOWN.SCR > %s.DN\r\n' "$name"
        rc_to "$name.DRC"
    } >>"$b/SCRB.BAT"
done

rc=0
QEMU_EXTRA="-rtc base=$CLOCK" SERIAL=1 \
    sh "$here/run2vm.sh" "$a" 'C:\COMMAND /C SRVA.BAT' \
                         "$b" 'C:\COMMAND /C SCRB.BAT' &
runpid=$!
wait "$runpid" || rc=$?
runpid=
case $rc in 0|1) ;; *) exit "$rc" ;; esac
[ "$rc" -eq 0 ] || status=1

da="$OUT/A/D"
db="$OUT/B/D"

# link_view FILE [LINK] - print the snapshot file FILE as it is compared,
# its status line being the text row right before a snapshot's =ATTR
# line.  With LINK, FILE is a golden of the local runs, shown as the
# client must show it over LINK: COM or NET in the connection field in
# place of LOCAL and, over NET, RINV**** as the station.  Without, FILE
# is a snapshot whose station, when it is RINV and four hexadecimal
# digits, the NetBIOS name nb_local_name (SRC/COMPAT/CNBNAME.C) makes up
# for each call, is shown as RINV****; any other, such as the settings'
# TEST, stays as it is.
link_view() {
    LC_ALL=C awk -v link="${2-}" '
        /^=ATTR / && link != "" && match(prev, /^\| [^ ]+ +LOCAL/) {
            sta = link == "NET" ? "RINV****" : substr(prev, 3, RLENGTH - 7)
            prev = sprintf("| %-" (RLENGTH - 7) "s%-5s", sta,
                link == "NET" ? "NET" : "COM") substr(prev, RLENGTH + 1)
        }
        /^=ATTR / && link == "" &&
            prev ~ /^\| RINV[0-9A-F][0-9A-F][0-9A-F][0-9A-F] / {
            prev = "| RINV****" substr(prev, 11)
        }
        NR > 1 { print prev }
        { prev = $0 }
        END { if (NR > 0) print prev }' "$1"
}

logs=
for name in $(echo "$servers" | cut -d ' ' -f 1); do
    expect "$da/$name.RC" 0
    expect "$db/$name.WRC" 0
    expect "$db/$name.DRC" 0
    log_check "$da/$name.LOG" 1 "$name"
    logs="$logs $da/$name.LOG $db/$name.WT $db/$name.DN"
done
n=0
for r in $runs; do
    n=$((n + 1))
    run_split "$r"
    floor=THINSCR
    [ "$cli" != INV ] || floor=INVSCR
    [ "$link" = COM1 ] || floor=${floor}NET
    mkdir "$payload/R$n"
    echo "$prog: run R$n: $cli /$link${kb:+ with $kb KB free}"
    least=
    for s in $scripts; do
        expect "$db/R$n/$s.RC" 0 "R$n/$s.RC"
        open_check "$db/R$n" "$s" "$cli /$link $s.KEY" -r "${kb:+$r}" \
            "$floor"
        if [ ! -f "$db/R$n/$s.SNP" ]; then
            echo "$prog: no R$n/$s.SNP" >&2
            status=1
            continue
        fi
        link_view "$GOLD/$s.SNC" "$link" >"$payload/R$n/$s.GLD"
        link_view "$db/R$n/$s.SNP" >"$payload/R$n/$s.SNP"
        snap_check "$payload/R$n/$s.GLD" "$payload/R$n/$s.SNP" "$margin" ||
            status=1
        u=$(tr -d '\r' <"$db/R$n/$s.SNP" | sed -n 's/^=STACK UNUSED=//p')
        is_uint "$u" && [ "${least:-$u}" -ge "$u" ] && least=$u
    done
    [ -z "$least" ] || echo "$prog: stack never used: $least bytes at least"
    free_check B "$kb" "R$n/FREE.OUT"
done

free_print A B

gate_end $logs
