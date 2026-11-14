#!/bin/sh
# T_INV.SH - test of INV.EXE's start-up, its exit statuses and INV /CMD, the
# scripted client, on the host and under DOS, and on the host of the
# start-up of THIN, the network-only client of SRCS.MK.
#
# usage (from the repository root):
#   sh TEST/T_INV.SH [-u] [HOSTBIN THINBIN]
#   sh TEST/T_INV.SH -d qemu|dosbox [-s] EXE
#   sh TEST/T_INV.SH -d qemu|dosbox -m KB EXE
#
# Host mode runs HOSTBIN and THINBIN with BIN/INV.MSG beside them in the
# current directory (default: INV and THIN built here from INV_SRCS and
# THIN's _SRCS of SRCS.MK with the gcc flags in CFLAGS, by default the
# strict host flags, and INV.MSG built from INV_MSGS by TEST/msgchk.py):
#   - INV /CMD /LOCAL on a fresh scratch data directory with the clock fixed
#     by the test switch /NOW:20261008120000, reading the request script
#     below, whose standard output must equal TEST/GOLDEN/INVCMD.TXT byte for
#     byte, with exit status 0 and nothing on standard error;
#   - the same directory opened again with lower-case '-' switches, input
#     whose last line has no LF, input cut short by a Ctrl-Z byte, and a
#     /NOW: month and day below 10 in a stored date;
#   - the exit status of a usage error, a stray argument (one that looks
#     like /? without its '/' too, with its message), a repeated /CMD,
#     /LOCAL, /NOW, /BIOS, /MDA, /KEYS:, /SNAP: or /PRN:, a /KEYS:, /SNAP:
#     or /PRN: with nothing after its colon, an unknown switch, /CFG
#     without its colon (1 each, with the usage text), a bad /NOW value
#     or one outside the years 1980..2079 (1, message first; both years
#     pass), a full-screen switch
#     (/BIOS, /CFG: without /COM1 or /COM2, /KEYS:, /SNAP:, /MDA)
#     together with /CMD, /SNAP: without /KEYS: and a bad or missing key
#     script (1), /NET without a name (with its own message), with an
#     empty name, a name of NB_NAMELEN characters or "*", or together
#     with /LOCAL or /NOW (1), /COM1 together with /COM2, /LOCAL, /NET
#     or /NOW (1), /TCP:, the Windows client's, in both modes (1), a
#     data directory that does not exist or whose name is too long (2),
#     /NET with no NetBIOS provider, the host's case, in both modes and
#     with a name of NB_NAMELEN - 1 characters (2), /COM1 and /COM2 with
#     no serial port, the host's case, in both modes and with /CFG: (2;
#     8 for a /CFG: file that cannot be read, which shows the settings
#     are read), standard output on a full device (4, Linux /dev/full),
#     a data file whose close fails, in /CMD and in the full-screen
#     client (4, the failure injected by an LD_PRELOAD shim built with
#     gcc) and /? (0, the usage text whole);
#   - the full-screen client: without /KEYS: it ends at once with 0 (the
#     host keyboard has no keys); a key script that logs in, saves the
#     settings to a /CFG: file, serial keys included and the default data
#     directory shown but left out, and quits (0, with snapshots that must
#     not show the password, also with /MDA), and again over a DATA= of
#     another directory with the default typed in its place, which is
#     saved; three refused logins (5); INV.MSG missing or of another
#     version (7); a settings file that cannot be read, a .$$$ name and an
#     over-long name (8), an empty /CFG: (1), snapshots written to a full
#     device (4, Linux /dev/full).
#     Status 6 (no memory for the screen) cannot be forced;
#   - THIN: no connection named, in both modes, /LOCAL, in both modes,
#     and /NOW:, alone or with /NET, are usage errors (1), a bad /NOW:
#     value too, with its message, which names THIN as SRCS.MK does, as
#     do those of /NET and /COM1, which fail to open as INV's do (2); its
#     usage text, on standard error after a usage error and on standard
#     output for /? (0), is INV's with THIN's name and a line saying that
#     it has no /LOCAL in place of the line of /LOCAL.
# With -u the golden file is rewritten from the first run instead; review
# the difference before keeping it.  A run that writes the golden without
# -u, or not with it, fails.
#
# DOS mode boots the Compaq MS-DOS 3.10 floppy with TEST/INTEG/boot310.sh
# in QEMU or DOSBox-X with its scratch hard disk as C: (FILES=45, a
# VDISK.SYS RAM disk, which DOS then names D:, for the data, and with -s
# SHARE loaded).  EXE (an INV.EXE from any of the DOS compilers), the
# COMTEST.EXE beside it, INV.MSG (from EXE's directory, else BIN/), the
# request script, the key scripts and settings file below and
# TEST/T_INV.BAT go to C:\, which has room for a far larger INV.EXE than
# the floppy, and the run is
#   C:
#   A:\COMMAND /C T_INV /NOW:20261008120000 D:
# The batch file's scripted run writes C:\CMD.OUT, which must equal the
# golden file byte for byte; everything the batch file writes to standard
# output (A:\OUT.TXT, CR removed) must equal the expected case lines below
# exactly (the batch file first has CHKDSK write its report of the free
# memory to C:\FREE.TXT instead), the last cases showing that INV /CMD
# /COM2 and COMTEST /COM2 (a port QEMU's machine lacks) and INV /CMD
# /LOCAL, run with BREAK on, leave it on; the full-screen login run writes
# C:\LOGIN.SNP, whose snapshots must show the adapter's memory equal to
# the shadow (VRAM=OK) and the home screen of the logged-in user, never
# the password, and must leave at least STACK_MARGIN (TEST/T_SCRH.C)
# bytes of stack untouched.  With qemu a second boot (boot310.sh -S)
# gives the machine a COM2 whose line is a socket served by a peer here
# that answers every request line +OK; INV /COM2 with SERIAL.KEY must
# exit 0, its snapshot of the login screen in C:\SERIAL.SNP showing COM2
# as the data directory.  With dosbox, DOSBox-X's own DOS, reporting the
# versions listed below, also runs the DOS version check of INV and of
# the test tools COMTEST, NBTEST and TCPTEST beside EXE, and the tools'
# command lines, verdicts and exit statuses without their providers.
# With -m KB (without SHARE) the RAM disk of both boots is sized
# (TEST/INTEG/free310.sh) so that INV starts with KB kilobytes of free
# conventional memory (less than one more), which C:\FREE.TXT and the
# COM2 boot's CHKDSK must show, so the boots check the free memory INV
# /LOCAL and INV /COM2 are documented to need; without -m both boots get
# the least RAM disk, FREE310_MINKB KB, which leaves every build more than
# it needs.
#
# Exits 0 when everything matches, 1 on a mismatch, 2 on a usage or setup
# error, 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"
. "$here/INTEG/free310.sh"

[ -f SRCS.MK ] && [ -f SRC/CLIENT/INV.C ] || die "run from the repository root"

GOLDEN=TEST/GOLDEN/INVCMD.TXT
NOW=/NOW:$SEED_NOW
# The stack bytes a DOS login run must leave untouched, as in T_SCR.SH.
STACK_MARGIN=$(cdefine TEST/T_SCRH.C STACK_MARGIN)
update=0
emu=
share=
freekb=
dosuse='-d qemu|dosbox [-s | -m KB] EXE'
case ${1:-} in
-u) update=1; shift ;;
-d) [ $# -ge 3 ] || die "usage: sh TEST/T_INV.SH $dosuse"
    emu=$2; shift 2
    case $emu in qemu|dosbox) ;; *) die "unknown emulator: $emu" ;; esac
    if [ "$1" = -s ]; then share=SHARE; shift
    elif [ "$1" = -m ]; then
        [ $# -eq 3 ] || die "-m KB goes with one EXE"
        freekb=$2; shift 2
    fi ;;
esac
if [ -n "$emu" ]; then
    [ $# -eq 1 ] || die "usage: sh TEST/T_INV.SH $dosuse"
else
    [ $# -eq 0 ] || [ $# -eq 2 ] ||
        die "usage: sh TEST/T_INV.SH [-u] [HOSTBIN THINBIN] | $dosuse"
fi

scratch=$(mktemp -d "${TMPDIR:-/tmp}/t_inv.XXXXXX")
peer=
trap '[ -z "$peer" ] || kill "$peer" 2>/dev/null; rm -rf "$scratch"' EXIT
on_signals

# The request script: CR LF line ends, except one line ended by a bare LF;
# an empty line (no response), a line of 300 bytes (error 103), a
# lower-case command, and a request after BYE that must not be answered.
# TEST/TSCRIPT.C holds a C copy of it for the end-to-end tests, which
# compare with the same golden file: the two change together.
long=$(printf '%300s' '' | tr ' ' 'X')
{
    printf '\r\n'
    printf 'HELLO INVTEST\r\n'
    printf 'LOGIN ADMIN admin\r\n'
    printf 'ITEM LIST\r\n'
    printf 'PASSWD admin Retro1986\r\n'
    printf 'LOGIN ADMIN Retro1986\n'
    printf '%s %s\r\n' 'ITEM ADD CATEGORY=SYS MFR=IBM MODEL=5150' \
        'DESCR="IBM Personal Computer" SERIALNO=0012345'
    printf 'ITEM ADD CATEGORY=DRV MFR=Tandon MODEL=TM100-2 QTY=2\r\n'
    printf '%s\r\n' "$long"
    printf 'ITEM GET 1\r\n'
    printf 'ITEM LIST\r\n'
    printf 'ping\r\n'
    printf 'BYE\r\n'
    printf 'PING\r\n'
} >"$scratch/INVCMD.IN"

# Key scripts and a settings file for the full-screen client.
# SAVECFG.KEY logs in with the password the request script set, opens the
# settings from the Options menu, saves them, and quits; BAD3.KEY, the
# script of TEST/T_SCR.SH, is refused three times and closes the message
# box that ends the program.
: >"$scratch/EMPTY.KEY"
cat >"$scratch/SAVECFG.KEY" <<'EOF2'
T ADMIN
K ENTER
T Retro1986
K ENTER
S HOME
K ALT-O
T S
S SETTINGS
K ENTER
K ESC
K F3
T Y
EOF2
cp TEST/KEYS/BAD3.KEY "$scratch/BAD3.KEY"
# SERIAL.KEY snapshots the login screen and quits.
printf 'S LOGIN\nK ESC\n' >"$scratch/SERIAL.KEY"
printf 'STATION=TEST\r\n' >"$scratch/TEST.CFG"

# snapcheck FILE NAME: the snapshots of SAVECFG.KEY in FILE, all VRAM=OK,
# the home screen showing the user, level and item count.
snapcheck() {
    [ "$(grep -c '^=SNAP ' "$1")" = 2 ] ||
        check_fail "$2: snapshots: $(grep '^=SNAP' "$1" | tr -d '\r')"
    [ "$(grep '^=SNAP ' "$1" | grep -vc 'VRAM=OK')" = 0 ] ||
        check_fail "$2: adapter memory differs:" \
            "$(grep '^=SNAP' "$1" | tr -d '\r')"
    grep -q '^| TEST *LOCAL  ADMIN 3 ' "$1" ||
        check_fail "$2: no status line of ADMIN 3"
    grep -q 'Item records      2 ' "$1" ||
        check_fail "$2: no item count on the home screen"
    grep -q 'Settings file  TEST.CFG' "$1" ||
        check_fail "$2: no settings screen"
    grep -aq 'Retro1986' "$1" &&
        check_fail "$2: the password shows on the screen"
    return 0
}

fails=0
check_fail() { echo "$prog: FAIL: $*" >&2; fails=$((fails + 1)); }

# ------------------------------------------------------------------ DOS
if [ -n "$emu" ]; then
    exe=$1
    [ -f "$exe" ] || die "no such file: $exe"
    [ -f "$GOLDEN" ] || die "missing $GOLDEN"
    cp "$exe" "$scratch/INV.EXE"
    comt=$(dirname "$exe")/COMTEST.EXE
    [ -f "$comt" ] || die "no COMTEST.EXE beside $exe"
    cp "$comt" "$scratch/COMTEST.EXE"
    msgf=$(dirname "$exe")/INV.MSG
    [ -f "$msgf" ] || msgf=BIN/INV.MSG
    [ -f "$msgf" ] || die "no INV.MSG beside $exe or in BIN/"
    cp "$msgf" "$scratch/INV.MSG"
    export MTOOLS_SKIP_CHECK=1
    dos_text TEST/T_INV.BAT >"$scratch/T_INV.BAT"
    vdisk=$FREE310_MINKB
    if [ -n "$freekb" ]; then
        vdisk=$(free310_vdisk "$freekb") ||
            die "no RAM disk leaves $freekb KB free"
    fi
    free310_config "$scratch/CONFIG.SYS" "$vdisk"
    rc=0
    # shellcheck disable=SC2086
    sh TEST/INTEG/boot310.sh -e "$emu" -t 300 -k -f "$scratch/CONFIG.SYS" \
        -h "$scratch/INV.EXE" -h "$scratch/COMTEST.EXE" \
        -h "$scratch/INV.MSG" -h "$scratch/INVCMD.IN" -h "$scratch/T_INV.BAT" \
        -h "$scratch/EMPTY.KEY" -h "$scratch/SAVECFG.KEY" \
        -h "$scratch/BAD3.KEY" -h "$scratch/TEST.CFG" \
        $share C: "A:\\COMMAND /C T_INV $NOW D:" \
        >"$scratch/boot.log" 2>&1 || rc=$?
    kept=$(sed -n 's/^boot310.sh: kept //p' "$scratch/boot.log")
    hd=$(sed -n 's/^boot310.sh: hard disk partition //p' "$scratch/boot.log")
    # A file the run did not write is reported by mcopy and by the checks
    # below, which also print the boot log.
    if [ -n "$kept" ] && [ -f "$kept/A.IMG" ]; then
        mcopy -n -i "$kept/A.IMG" ::OUT.TXT "$scratch/OUT.TXT" ||
            check_fail "$emu: cannot read A:\\OUT.TXT"
    fi
    if [ -n "$hd" ]; then
        mcopy -n -i "$hd" ::CMD.OUT "$scratch/CMD.OUT" ||
            check_fail "$emu: cannot read C:\\CMD.OUT"
        mcopy -n -i "$hd" ::LOGIN.SNP "$scratch/LOGIN.SNP" ||
            check_fail "$emu: cannot read C:\\LOGIN.SNP"
        if [ -n "$freekb" ]; then
            mcopy -n -i "$hd" ::FREE.TXT "$scratch/FREE.TXT" 2>/dev/null ||
                true
            why=$(free310_check "$scratch/FREE.TXT" "$freekb") ||
                check_fail "$emu: $why"
        fi
    fi
    case $kept in */boot310.*) rm -rf "$kept" ;; esac
    [ -f "$scratch/OUT.TXT" ] && [ -f "$scratch/CMD.OUT" ] ||
        {
            cat "$scratch/boot.log"
            fail "no OUT.TXT or CMD.OUT from the $emu run"
        }
    cmp -s "$scratch/CMD.OUT" "$GOLDEN" || {
        check_fail "$emu: INV /CMD output differs from $GOLDEN"
        diff "$GOLDEN" "$scratch/CMD.OUT" | cat -A | head -40 >&2 || true
    }
    # COMMAND.COM echoes the batch file's first line before ECHO OFF, and
    # ends the batch file with an empty line.
    tr -d '\r' <"$scratch/OUT.TXT" >"$scratch/cases"
    cat >"$scratch/cases.exp" <<'EOF'

C>ECHO OFF
== cmd exit 0
== usage exit 1
== badtime exit 1
== net exit 2
== tui exit 0
== login exit 0
== badlogin exit 5
== snapnokeys exit 1
== mda exit 1
== cfgscr exit 8
== nomsg exit 7
== nodir exit 2
== reopen exit 0
== cmdcom
BREAK is on
== comtest
BREAK is on
== cmdlocal
BREAK is on

EOF
    cmp -s "$scratch/cases" "$scratch/cases.exp" || {
        check_fail "$emu: batch output differs"
        diff "$scratch/cases.exp" "$scratch/cases" | cat -A >&2 || true
        cat "$scratch/boot.log" >&2
    }
    if [ -f "$scratch/LOGIN.SNP" ]; then
        snapcheck "$scratch/LOGIN.SNP" "$emu: login"
        unused=$(sed -n 's/^=STACK UNUSED=\([0-9]*\).*/\1/p' \
            "$scratch/LOGIN.SNP")
        echo "$prog: $emu: stack never used: ${unused:-?} bytes"
        if [ "${unused:-0}" -lt "$STACK_MARGIN" ]; then
            check_fail "$emu: login: ${unused:-no} unused stack bytes," \
                "fewer than $STACK_MARGIN"
        fi
    else
        check_fail "$emu: no LOGIN.SNP"
    fi
    [ "$rc" = 0 ] || check_fail "$emu: boot310.sh status $rc"

    if [ "$emu" = qemu ]; then
        # The COM2 boot runs INV straight from its command line, so that
        # its exit status is seen; with -m its RAM disk leaves KB free
        # there, which CHKDSK run before it must report (without -m it is
        # the first boot's).  The peer's +OK gets INV past HELLO to the
        # login screen.  /COM1's name goes unchecked, as COM1 is
        # boot310.sh's console.
        sock=$scratch/com2.sock
        mkdir "$scratch/com2"
        if [ -n "$freekb" ]; then
            vdisk=$(free310_vdisk "$freekb" "$FREE310_TOP") ||
                die "no RAM disk leaves $freekb KB free"
        fi
        free310_config "$scratch/com2/CONFIG.SYS" "$vdisk"
        cat >"$scratch/peer.py" <<'EOF2'
import socket
import sys

srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sys.argv[1])
srv.listen(1)
srv.settimeout(float(sys.argv[2]))
con, _ = srv.accept()
con.settimeout(float(sys.argv[2]))
buf = b''
while True:
    got = con.recv(256)
    if not got:
        break
    buf += got
    while b'\n' in buf:
        line, _, buf = buf.partition(b'\n')
        if line.strip():
            con.sendall(b'+OK\r\n')
EOF2
        lim=$(dos_secs 120)
        python3 -I "$scratch/peer.py" "$sock" "$lim" 2>"$scratch/peer.log" &
        peer=$!
        n=0
        while [ ! -S "$sock" ] && [ $n -lt 100 ]; do
            sleep 0.1; n=$((n + 1))
        done
        rc=0
        sh TEST/INTEG/boot310.sh -c -t 120 -k -S "$sock" \
            -f "$scratch/com2/CONFIG.SYS" -h "$scratch/INV.EXE" \
            -h "$scratch/INV.MSG" -h "$scratch/SERIAL.KEY" \
            C: "A:\\CHKDSK D:" "INV /COM2 /KEYS:SERIAL.KEY /SNAP:SERIAL.SNP" \
            >"$scratch/com2.log" 2>&1 || rc=$?
        kill "$peer" 2>/dev/null || true
        wait "$peer" 2>/dev/null || true
        peer=
        kept=$(sed -n 's/^boot310.sh: kept //p' "$scratch/com2.log")
        hd=$(sed -n 's/^boot310.sh: hard disk partition //p' \
            "$scratch/com2.log")
        [ -z "$hd" ] ||
            mcopy -n -i "$hd" ::SERIAL.SNP "$scratch/SERIAL.SNP" || true
        case $kept in */boot310.*) rm -rf "$kept" ;; esac
        if [ -n "$freekb" ] &&
            ! why=$(free310_check "$scratch/com2.log" "$freekb"); then
            check_fail "$emu: INV /COM2: $why"
            cat "$scratch/com2.log" >&2
        elif [ "$rc" != 0 ] ||
            ! grep -q 'Data: COM2 ' "$scratch/SERIAL.SNP" 2>/dev/null; then
            check_fail "$emu: INV /COM2: status $rc, no login screen" \
                "naming COM2"
            cat "$scratch/com2.log" "$scratch/peer.log" >&2
        fi
    fi

    if [ "$emu" = dosbox ]; then
        # DOSBox-X's own DOS can report any version; INV and the test tools
        # beside it must refuse 3.00 and accept 3.10 and the later
        # versions.  Each boot runs INV, /CMD at 3.00 (3) and later a usage
        # error, which shows its check was passed (1), then the tool cases
        # below, each a name, the statuses it may end with from 3.10 on
        # and a command; at 3.00 every one must end with 4, whatever its
        # arguments.  The cases: each tool with /? alone, TCPTEST with -?
        # too (0, every line of standard output a "usage:" line of the
        # tool; standard error goes unchecked, as COMMAND.COM cannot
        # redirect it); its switches in any case and begun by "/" or "-",
        # finding its provider missing (2: no NetBIOS, no packet driver,
        # COM2 disabled), but for COMTEST on COM1, DOSBox-X's dummy port,
        # whose loopback either passes (0) or fails (3); and bad command
        # lines (1).  Every other tool run must leave standard output as
        # tool_out (TEST/shlib.sh) wants.  Each boot gets 60 s, times
        # DOSTIME_SCALE (dos_secs).
        lim=$(dos_secs 60)
        tdir=$(dirname "$exe")
        for t in NBTEST TCPTEST; do
            [ -f "$tdir/$t.EXE" ] || die "no $t.EXE beside $exe"
        done
        cat >"$scratch/tools" <<'EOF'
CHELP 0 COMTEST /?
NHELP 0 NBTEST /?
THELP 0 TCPTEST /?
TDHELP 0 TCPTEST -?
CPORT 03 COMTEST
CNONE 2 COMTEST -com2 /Baud:38400 -rtscts
NSRV 2 NBTEST SERVER RISRV -wait:1 /Async
NCLI 2 NBTEST CLIENT RICLI RISRV /Count:1 -ASYNC
TDEF 2 TCPTEST
TPORT 2 TCPTEST -port:65535
CRATE 1 COMTEST /BAUD:1000
NCOUNT 1 NBTEST CLIENT RICLI RISRV 5
NWAIT 1 NBTEST SERVER RISRV /WAIT 10
TOLD 1 TCPTEST 7000
TZERO 1 TCPTEST /PORT:0
TBIG 1 TCPTEST /PORT:65536
TBARE 1 TCPTEST /PORT
TBOGUS 1 TCPTEST /BOGUS
TQPORT 1 TCPTEST /PORT:7000 /?
TQFIRST 1 TCPTEST /? /PORT:7000
TTWICE 1 TCPTEST /PORT:7000 -port:7001
EOF
        for v in 3.00:3:/CMD 3.10:1:/BOGUS 4.00:1:/BOGUS 5.00:1:/BOGUS \
                 7.10:1:/BOGUS; do
            ver=${v%%:*}; rest=${v#*:}; want=${rest%%:*}; arg=${rest#*:}
            run=$scratch/ver$ver
            mkdir -p "$run/C" "$run/E"
            cp "$exe" "$run/C/INV.EXE"
            cp "$comt" "$tdir/NBTEST.EXE" "$tdir/TCPTEST.EXE" "$run/C/"
            {
                echo "INV $want INV $arg"
                if [ "$ver" = 3.00 ]; then
                    sed 's/^\([A-Z]*\) [0-9]* /\1 4 /' "$scratch/tools"
                else
                    cat "$scratch/tools"
                fi
            } >"$run/cases"
            {
                while read -r name st cmd; do
                    printf 'C:\\%s <NUL >E:\\%s.OUT\r\n' "$cmd" "$name"
                    errlevel_bat 9
                    printf 'ECHO %s %%RC%%>>E:\\RC.TXT\r\n' "$name"
                done <"$run/cases"
                printf 'exit\r\n'
            } >"$run/E/RUN.BAT"
            cat >"$run/DOSBOX.CONF" <<EOF
[sdl]
output=surface
[log]
logfile=
[dosbox]
machine=cga
memsize=1
quit warning=false
[cpu]
cputype=8086
core=normal
cycles=max
[dos]
ver=$ver
lfn=false
[serial]
serial1=dummy
serial2=disabled
[mixer]
nosound=true
[autoexec]
mount c "$run/C" -nocachedir
mount e "$run/E" -nocachedir
c:
E:\\RUN.BAT
EOF
            (cd "$run" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout -k 5 "$lim" dosbox-x -conf DOSBOX.CONF -nogui -nomenu \
                -nopromptfolder >DOSBOX.LOG 2>&1) || true
            while read -r name st cmd; do
                got=$(sed -n "s/^$name \([0-9]*\).*/\1/p" "$run/E/RC.TXT" \
                    2>/dev/null || true)
                case $got in
                [$st]) ;;
                *) check_fail "DOS $ver: $cmd exit '$got', expected $st"
                   continue ;;
                esac
                tool=${cmd%% *}
                case $name:$got in
                INV:*|*:4) ;;
                *HELP:0)
                    [ -s "$run/E/$name.OUT" ] &&
                        ! tr -d '\r' <"$run/E/$name.OUT" |
                            grep -qv "^usage: $tool " ||
                        check_fail "DOS $ver: $cmd:" \
                            "$(cat "$run/E/$name.OUT")" ;;
                *)
                    why=$(tool_out "$tool" "$run/E/$name.OUT" "$got") ||
                        check_fail "DOS $ver: $cmd: $why" ;;
                esac
            done <"$run/cases"
        done
    fi
    [ $fails = 0 ] || exit 1
    echo "$prog: $emu ok"
    exit 0
fi

# ----------------------------------------------------------------- host
if [ $# -eq 2 ]; then
    inv=$1
    thin=$2
    for f in "$inv" "$thin"; do
        [ -x "$f" ] || die "not an executable: $f"
    done
    case $inv in /*) ;; *) inv=$(pwd)/$inv ;; esac
    case $thin in /*) ;; *) thin=$(pwd)/$thin ;; esac
    msgf=BIN/INV.MSG
    [ -f "$msgf" ] || die "missing $msgf (make BIN/INV.MSG)"
else
    CFLAGS=${CFLAGS:-"-std=c89 -pedantic -Wall -Wextra -Wconversion -Werror \
-DHOST_LINUX -ISRC/COMPAT -ISRC/DBF -ISRC/ENGINE -ISRC/PROTO -ISRC/CLIENT \
-ITEST"}
    # INV.MSG is kept away from the program, whose directory is searched
    # first, so the runs without one can be tested.
    mkdir "$scratch/bin" "$scratch/msg"
    vars=$(srcs_vars SRCS.MK) || exit 2
    # shellcheck disable=SC2046
    python3 -I TEST/msgchk.py --out "$scratch/msg/INV.MSG" \
        --header "$scratch/msg/MSGSTMP.H" $(getvar INV_MSGS) ||
        fail "INV.MSG does not build"
    msgf=$scratch/msg/INV.MSG
    inv=$scratch/bin/inv
    # Upper-case .C means C++ to gcc, hence -x c ... -x none.
    # shellcheck disable=SC2086,SC2046
    gcc $CFLAGS -I"$scratch/msg" -x c $(getvar INV_SRCS) -x none -o "$inv" ||
        fail "INV does not build"
    thin=$scratch/bin/$(getvar THIN)
    # shellcheck disable=SC2086,SC2046
    gcc $CFLAGS -I"$scratch/msg" -x c $(getvar "$(getvar THIN)_SRCS") \
        -x none -o "$thin" || fail "THIN does not build"
fi
# THIN's name, which its usage text must give: its file name without the
# extension, THIN of SRCS.MK.
thinname=${thin##*/}
thinname=${thinname%%.*}

# A shim for LD_PRELOAD whose close() closes the file, then reports EIO
# for a file named ITEMS.DBF.  It is host C, not C89, so it lives here,
# out of lint's reach.
cat >"$scratch/closef.c" <<'EOF2'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int close(int fd)
{
    static int (*real)(int);
    static const char name[] = "/ITEMS.DBF";
    char link[32], path[4096];
    ssize_t n;
    int rc;

    if (real == NULL)
        *(void **)&real = dlsym(RTLD_NEXT, "close");
    snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    n = readlink(link, path, sizeof path);
    rc = real(fd);
    if (n >= (ssize_t)(sizeof name - 1) &&
        memcmp(path + n - (sizeof name - 1), name, sizeof name - 1) == 0) {
        errno = EIO;
        return -1;
    }
    return rc;
}
EOF2
gcc -shared -fPIC -Wall -Werror -o "$scratch/closef.so" "$scratch/closef.c" ||
    fail "the close shim does not build"
printf 'HELLO CLOSE\r\nBYE\r\n' >"$scratch/close.in"

mkdir "$scratch/data" "$scratch/nomsg" "$scratch/badmsg"
cp "$msgf" "$scratch/INV.MSG"
printf '=STAMP 0000\r\n' >"$scratch/badmsg/INV.MSG"
cd "$scratch"

# run NAME EXPECTED-STATUS INPUT ARGS...: stdout to NAME.out, stderr to
# NAME.err, status checked.
run() {
    name=$1 want=$2 input=$3
    shift 3
    rc=0
    "$inv" "$@" <"$input" >"$name.out" 2>"$name.err" || rc=$?
    [ "$rc" = "$want" ] || {
        check_fail "$name: exit status $rc, expected $want"
        cat "$name.err" >&2
    }
}

run cmd 0 INVCMD.IN /CMD /LOCAL data/ "$NOW"
cd - >/dev/null
wrote=0
if [ $update = 1 ]; then
    cp "$scratch/cmd.out" "$GOLDEN"
    echo "$prog: rewrote $GOLDEN"
    wrote=1
elif ! cmp -s "$scratch/cmd.out" "$GOLDEN"; then
    check_fail "INV /CMD output differs from $GOLDEN"
    diff "$GOLDEN" "$scratch/cmd.out" | cat -A | head -40 >&2 || true
fi
cd "$scratch"
[ -s cmd.err ] && check_fail "INV /CMD wrote to standard error: $(cat cmd.err)"

# The data persist; lower-case '-' switches; a last line without LF.
printf 'HELLO AGAIN\r\nLOGIN admin Retro1986\r\nITEM GET 2 \r\nBYE' >again.in
run again 0 again.in -cmd -Local data/ "$NOW"
printf '+OK\r\n+OK LEVEL=3 NAME="System Administrator"\r\n+DATA\r\n' >again.exp
grep -q 'K MFR=Tandon' again.out ||
    check_fail "again: item 2 not found after reopening"
head -c "$(wc -c <again.exp)" again.out | cmp -s - again.exp ||
    check_fail "again: unexpected output: $(cat -A again.out | head -5)"
[ "$(tail -n 1 again.out)" = "$(printf '+OK\r')" ] ||
    check_fail "again: BYE without LF not answered"

# A /NOW: month and day below 10 keep their leading zeros in the dates
# stored.
printf 'HELLO MD\r\nLOGIN ADMIN Retro1986\r\nITEM SET 2 QTY=2\r\n' >md.in
printf 'ITEM GET 2\r\nBYE\r\n' >>md.in
run md 0 md.in /CMD /LOCAL data/ /NOW:20260305090807
grep -q '^K UPDATED=20260305' md.out ||
    check_fail "md: $(grep UPDATED md.out || cat md.out)"

# A data directory named without a separator gets the host's own, so the
# files land inside it.
mkdir sepdir
printf 'BYE\r\n' >nosep.in
run nosep 0 nosep.in /CMD /LOCAL sepdir "$NOW"
[ -f sepdir/ITEMS.DBF ] || check_fail "nosep: no sepdir/ITEMS.DBF"
ls | grep -q '\\' &&
    check_fail "nosep: files named with a backslash: $(ls | grep '\\')"

# Ctrl-Z ends the input.
printf 'HELLO Z\r\n\032PING\r\n' >ctrlz.in
run ctrlz 0 ctrlz.in /CMD /LOCAL data/
printf '+OK\r\n' | cmp -s - ctrlz.out ||
    check_fail "ctrlz: output $(cat -A ctrlz.out)"

: >empty.in
run usage    1 empty.in /CMD /BOGUS
run stray    1 empty.in /CMD /LOCAL data/ extra
run strayq   1 empty.in /CMD /LOCAL data/ 'x?'
run twice    1 empty.in /CMD /CMD /LOCAL data/
run badtime  1 empty.in /CMD /LOCAL data/ /NOW:20260230120000
run now1979  1 empty.in /CMD /LOCAL data/ /NOW:19791231235959
run now1980  0 empty.in /CMD /LOCAL data/ /NOW:19800101000000
run now2079  0 empty.in /CMD /LOCAL data/ /NOW:20791231235959
run now2080  1 empty.in /CMD /LOCAL data/ /NOW:20800101000000
run net      2 empty.in /CMD /NET SERVER
run nettui   2 empty.in /NET SERVER /KEYS:EMPTY.KEY
run netname  1 empty.in /CMD /NET
run netempty 1 empty.in /CMD /NET ''
run netlong  1 empty.in /CMD /NET SERVER6789ABCDEF
run netmax   2 empty.in /CMD /NET SERVER6789ABCDE
run netstar  1 empty.in /CMD /NET '*'
run netsw    1 empty.in /CMD /NET /LOCAL data/
run netlocal 1 empty.in /CMD /NET SERVER /LOCAL data/
run netnow   1 empty.in /CMD /NET SERVER "$NOW"
run com1     2 empty.in /CMD /COM1
run com2     2 empty.in /cmd /com2
run comcfg   2 empty.in /CMD /COM1 /CFG:TEST.CFG
run comtui   2 empty.in /COM2 /KEYS:EMPTY.KEY
run comboth  1 empty.in /CMD /COM1 /COM2
run comlocal 1 empty.in /CMD /COM1 /LOCAL data/
run comnet   1 empty.in /CMD /COM1 /NET SERVER
run comnow   1 empty.in /CMD /COM1 "$NOW"
run tcp      1 empty.in /CMD /TCP:10.0.2.15
run tcptui   1 empty.in /TCP:10.0.2.15:7000 /KEYS:EMPTY.KEY
run bios     1 empty.in /CMD /BIOS
run cfgcmd   1 empty.in /CMD /CFG:TEST.CFG
run keyscmd  1 empty.in /CMD /KEYS:EMPTY.KEY
run snapcmd  1 empty.in /CMD /KEYS:EMPTY.KEY /SNAP:x.snp
run mdacmd   1 empty.in /CMD /MDA
run snaponly 1 empty.in /LOCAL data/ /SNAP:x.snp
run nokeys   1 empty.in /LOCAL data/ /KEYS:NOSUCH.KEY
printf 'Q what\n' >bad.key
run badkeys  1 empty.in /LOCAL data/ /KEYS:bad.key
run tui      0 empty.in /LOCAL data/
run tuikeys  0 empty.in /LOCAL data/ /KEYS:EMPTY.KEY /SNAP:empty.snp
run login    0 empty.in /LOCAL data/ /CFG:TEST.CFG /KEYS:SAVECFG.KEY \
    /SNAP:login.snp
snapcheck login.snp login
grep -q '^ITEMORD=ID' TEST.CFG ||
    check_fail "login: settings not saved: $(cat -A TEST.CFG)"
grep -q '^STATION=TEST' TEST.CFG ||
    check_fail "login: STATION= lost: $(cat -A TEST.CFG)"
grep -aq 'Data directory   DATA\\ ' login.snp ||
    check_fail "login: the default data directory not shown"
grep -q '^DATA=[^[:cntrl:]]' TEST.CFG &&
    check_fail "login: the default data directory saved: $(cat -A TEST.CFG)"
grep -q '^BAUD=9600' TEST.CFG && grep -q '^RTSCTS=0' TEST.CFG ||
    check_fail "login: serial keys not saved: $(cat -A TEST.CFG)"
run mda      0 empty.in /LOCAL data/ /CFG:TEST.CFG /KEYS:SAVECFG.KEY \
    /SNAP:mda.snp /MDA
grep -q '^=SNAP HOME PALETTE=MONO ' mda.snp ||
    check_fail "mda: $(grep '^=SNAP' mda.snp)"
# A data directory set elsewhere and typed back as the default is saved
# as typed: only a DATA= left out stays out.
printf 'DATA=ELSE\r\n' >ELSE.CFG
sed 's/^T S$/T S\nK CTRL-Y\nT DATA\\/' SAVECFG.KEY >SETDIR.KEY
run setdir   0 empty.in /LOCAL data/ /CFG:ELSE.CFG /KEYS:SETDIR.KEY
tr -d '\r' <ELSE.CFG | grep -qx 'DATA=DATA\\' ||
    check_fail "setdir: DATA= not saved as typed: $(cat -A ELSE.CFG)"
# A snapshot that cannot be written ends the script, and the run with 4.
run snapfull 4 empty.in /LOCAL data/ /KEYS:SAVECFG.KEY /SNAP:/dev/full
grep -q 'INV: key script failed (error ' snapfull.err ||
    check_fail "snapfull: message: $(cat snapfull.err)"
run badlogin 5 empty.in /LOCAL data/ /KEYS:BAD3.KEY /SNAP:bad3.snp
grep -q 'Too many failed logins' bad3.snp || check_fail "badlogin: no message"
grep -aq 'wrong[123]' bad3.snp &&
    check_fail "badlogin: a password shows on the screen"
# The status stays 5 when the key script ends while the box is shown.
sed '$d' BAD3.KEY >BAD3E.KEY
run badend   5 empty.in /LOCAL data/ /KEYS:BAD3E.KEY
# A click line out of range, with a missing or an extra word, or with a
# sign is refused before anything runs: status 1 and the script's line.
n=0
for l in 'C 25 0' 'C 0 80' 'D 25 0' 'D 0 80' 'C 0' 'C 1 2 3' 'D 0 0 1' \
    'C -1 0' 'C 0 -1' 'C a b'; do
    n=$((n + 1))
    printf 'K ESC\n%s\n' "$l" >"BADC$n.KEY"
    run badclick$n 1 empty.in /LOCAL data/ /KEYS:BADC$n.KEY
    grep -q "bad key script: BADC$n.KEY:2" badclick$n.err ||
        check_fail "badclick$n ($l): message: $(cat badclick$n.err)"
done
# A settings file that exists but cannot be read, a scratch name, and a
# name over the settings path limit stop INV with 8; an empty /CFG: is a
# usage error.
mkdir cfgdir.cfg
run cfgdir   8 empty.in /LOCAL data/ /CFG:cfgdir.cfg /KEYS:EMPTY.KEY
run cfgscr   8 empty.in /LOCAL data/ '/CFG:TEST.$$$' /KEYS:EMPTY.KEY
run cfglong  8 empty.in /LOCAL data/ "/CFG:$(printf '%0129d' 0)" \
    /KEYS:EMPTY.KEY
run cfgempty 1 empty.in /LOCAL data/ /CFG: /KEYS:EMPTY.KEY
run cfgbare  1 empty.in /LOCAL data/ /CFG /KEYS:EMPTY.KEY
run cfgnocol 1 empty.in /LOCAL data/ /CFGTEST.CFG /KEYS:EMPTY.KEY
run comcfgd  8 empty.in /CMD /COM1 /CFG:cfgdir.cfg
for n in cfgdir cfgscr cfglong comcfgd; do
    grep -q 'cannot read the settings file' $n.err ||
        check_fail "$n: message: $(cat $n.err)"
done
# INV.MSG neither beside the program nor in the current directory, then
# one made for another set of messages.
cd nomsg
run nomsg    7 ../empty.in /LOCAL ../data/
cd ../badmsg
run badmsg   7 ../empty.in /LOCAL ../data/
cd ..
run dirtwice 1 empty.in /CMD /LOCAL data/ /LOCAL data/
run nowtwice 1 empty.in /CMD "$NOW" "$NOW"
# The full-screen switches: each once, and each with a name after its
# colon.
k=/KEYS:EMPTY.KEY
run biostwc  1 empty.in /LOCAL data/ $k /BIOS /BIOS
run mdatwc   1 empty.in /LOCAL data/ $k /MDA /MDA
run keystwc  1 empty.in /LOCAL data/ $k $k
run snaptwc  1 empty.in /LOCAL data/ $k /SNAP:x.snp /SNAP:y.snp
run prntwc   1 empty.in /LOCAL data/ $k /PRN:x.prn /PRN:y.prn
run keysnone 1 empty.in /LOCAL data/ /KEYS:
run snapnone 1 empty.in /LOCAL data/ $k /SNAP:
run prnnone  1 empty.in /LOCAL data/ $k /PRN:
run nodir    2 empty.in /CMD /LOCAL nodir/sub/
longdir=$(printf '%065d' 0)
run longdir  2 empty.in /CMD /LOCAL "$longdir"
run help     0 empty.in /?
rc=0
"$inv" /CMD /LOCAL data/ <INVCMD.IN >/dev/full 2>full.err || rc=$?
[ "$rc" = 4 ] || check_fail "full: exit status $rc, expected 4"
grep -q 'cannot write standard output' full.err ||
    check_fail "full: message: $(cat full.err)"
# A data file that cannot be closed ends either mode with 4: the shim
# fails every close of ITEMS.DBF.
for m in /CMD /KEYS:EMPTY.KEY; do
    rc=0
    LD_PRELOAD=$scratch/closef.so "$inv" $m /LOCAL data/ <close.in \
        >close.out 2>close.err || rc=$?
    [ "$rc" = 4 ] || check_fail "close $m: exit status $rc, expected 4"
    grep -q '^INV: cannot close the data files (error ' close.err ||
        check_fail "close $m: message: $(cat close.err)"
done
for n in net nettui; do
    grep -q 'cannot call SERVER: no NetBIOS$' $n.err ||
        check_fail "$n: message: $(cat $n.err)"
done
for n in com1 comcfg; do
    grep -q '^INV: cannot open COM1: no such port$' $n.err ||
        check_fail "$n: message: $(cat $n.err)"
done
for n in com2 comtui; do
    grep -q '^INV: cannot open COM2: no such port$' $n.err ||
        check_fail "$n: message: $(cat $n.err)"
done
grep -q '^INV: bad or repeated argument: /COM2$' comboth.err ||
    check_fail "comboth: message: $(cat comboth.err)"
for n in netname netsw; do
    grep -q "^INV: /NET needs the server's name$" $n.err ||
        check_fail "$n: message: $(cat $n.err)"
done
grep -q '^INV: bad or repeated argument: extra$' stray.err ||
    check_fail "stray: message: $(cat stray.err)"
grep -q '^INV: bad or repeated argument: x?$' strayq.err ||
    check_fail "strayq: message: $(cat strayq.err)"
grep -q 'nodir/sub/: no such directory: make it (MD) or name another' \
    nodir.err || check_fail "nodir: message: $(cat nodir.err)"
grep -q 'too long' longdir.err ||
    check_fail "longdir: message: $(cat longdir.err)"
for n in usage stray strayq twice dirtwice nowtwice netname netsw bios cfgcmd \
         keyscmd snapcmd mdacmd snaponly netlocal netnow comboth comlocal \
         comnet comnow tcp tcptui cfgbare cfgnocol biostwc mdatwc keystwc \
         snaptwc prntwc keysnone snapnone prnnone; do
    grep -q '^usage: INV' $n.err ||
        check_fail "$n: no usage text: $(cat $n.err)"
done
# The usage texts, whole: the lines of the connections, then those every
# program shares.
cat >usage.tail <<'EOF'
  /NET    use the server called name
  /COMn   use the server on serial port n (1 or 2)
  /BIOS   draw the screen through the BIOS
  /CFG:   the settings file (default INV.CFG)
  /CMD    read protocol requests from standard input and write the
          responses to standard output
EOF
cat - usage.tail >help.exp <<'EOF'
usage: INV [/LOCAL [dir] | /NET name | /COMn] [/BIOS] [/CFG:file]
       INV /CMD [/LOCAL [dir] | /NET name | /COMn [/CFG:file]]
  /LOCAL  use the data directory dir (default DATA/) without a server
EOF
cat - usage.tail >thinhelp.exp <<EOF
usage: $thinname {/NET name | /COMn} [/BIOS] [/CFG:file]
       $thinname /CMD {/NET name | /COMn [/CFG:file]}
  This program has no /LOCAL: name the server with /NET or /COMn.
EOF
cmp -s help.out help.exp ||
    check_fail "help: not the usage text: $(cat help.out)"
for n in badtime now1979 now2080; do
    grep -q '^INV: bad time: /NOW:' $n.err ||
        check_fail "$n: message: $(cat $n.err)"
    sed 1d $n.err | cmp -s - help.exp ||
        check_fail "$n: not the usage text after the message: $(cat $n.err)"
done
grep -q 'bad key script: NOSUCH.KEY' nokeys.err ||
    check_fail "nokeys: message: $(cat nokeys.err)"
grep -q 'bad key script: bad.key:1' badkeys.err ||
    check_fail "badkeys: message: $(cat badkeys.err)"
grep -q 'cannot read INV.MSG' nomsg/nomsg.err ||
    check_fail "nomsg: message: $(cat nomsg/nomsg.err)"
grep -q 'cannot read INV.MSG: .*not made for this program' badmsg/badmsg.err ||
    check_fail "badmsg: message: $(cat badmsg/badmsg.err)"
for n in md now1980 now2079 tui tuikeys login mda badlogin badend; do
    [ -s $n.err ] && check_fail "$n: wrote to standard error: $(cat $n.err)"
done
for n in usage stray twice dirtwice nowtwice badtime now1979 now1980 \
         now2079 now2080 net nettui netname netsw \
         netlocal netnow com1 com2 comcfg comtui comboth comlocal comnet \
         comnow comcfgd bios \
         cfgcmd keyscmd snapcmd mdacmd snaponly nokeys badkeys tui tuikeys \
         login mda snapfull badlogin badend cfgdir cfgscr cfglong cfgempty \
         cfgbare cfgnocol nodir longdir; do
    [ -s $n.out ] && check_fail "$n: wrote to standard output"
done

# THIN, the network-only client.
invsave=$inv
inv=$thin
run thinnone 1 empty.in
run thincmd  1 empty.in /CMD
run thinloc  1 empty.in /CMD /LOCAL data/
run thintui  1 empty.in /LOCAL data/
run thinnow  1 empty.in /CMD "$NOW"
run thinnetn 1 empty.in /CMD /NET SERVER "$NOW"
run thinnet  2 empty.in /CMD /NET SERVER
run thincom  2 empty.in /CMD /COM1
run thinbadt 1 empty.in /CMD /NET SERVER /NOW:bad
run thinhelp 0 empty.in /?
inv=$invsave
for n in thinnone thincmd thinloc thintui thinnow thinnetn; do
    cmp -s $n.err thinhelp.out ||
        check_fail "$n: not the usage text: $(cat $n.err)"
done
{ echo "$thinname: bad time: /NOW:bad"; cat thinhelp.exp; } |
    cmp -s - thinbadt.err ||
    check_fail "thinbadt: message and usage text: $(cat thinbadt.err)"
grep -q "^$thinname: cannot call SERVER: no NetBIOS\$" thinnet.err ||
    check_fail "thinnet: message: $(cat thinnet.err)"
grep -q "^$thinname: cannot open COM1: no such port\$" thincom.err ||
    check_fail "thincom: message: $(cat thincom.err)"
cmp -s thinhelp.out thinhelp.exp ||
    check_fail "thinhelp: not the usage text: $(cat thinhelp.out)"
for n in thinnone thincmd thinloc thintui thinnow thinnetn thinnet thincom \
         thinbadt; do
    [ -s $n.out ] && check_fail "$n: wrote to standard output"
done
[ $wrote = $update ] || check_fail "wrote $wrote goldens, -u $update"

[ $fails = 0 ] || exit 1
echo "$prog: ok"
