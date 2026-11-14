# vmgate.sh - shell helpers of the two-VM gates, srvgate.sh, scrgate.sh,
# tcpgate.sh, stvgate.sh, wgagate.sh and wintcp.sh, whose log_check,
# pw_check and golden helpers smbgate.sh also uses, and mbox310.sh the
# golden helpers.
#
# Sourced after net330.sh, and after free310.sh where free_check is
# used; not run on its own.  gate_start makes the run's directories and
# sets the traps; the other helpers use its variables: payload (the files
# for the VMs' D: drives, removed at exit), mondir (the VMs' monitor
# sockets and the gate's short-lived files), runpid (run2vm.sh, started
# by the gate in the background), qpid (the helper program in the
# foreground), status (1 once a check failed) and OUT (run2vm.sh's
# result directory).  The golden helpers read the gate's repo, update (1
# to rewrite the goldens) and status; golden_check also uses mondir.

# gate_start NAME - make payload under TEST/INTEG/vm and mondir, and set
# the exit trap and the signal handler (halt).
gate_start() {
    mkdir -p "$here/vm"
    payload=$(mktemp -d "$here/vm/$1.XXXXXX")
    # A monitor socket's path is limited to 108 bytes; mktemp's is short.
    mondir=$(mktemp -d)
    runpid=
    qpid=
    status=0
    trap 'rm -rf "$payload" "$mondir"' EXIT
    on_signals halt
}

# halt SIGNAL - the signal handler (on_signals): stop the program in the
# foreground, send SIGTERM to run2vm.sh (it kills its VMs; a background
# job cannot catch SIGINT) and wait for it.
halt() {
    [ -z "$qpid" ] || kill "$qpid" 2>/dev/null || :
    if [ -n "$runpid" ]; then
        kill -s TERM "$runpid" 2>/dev/null || :
        wait "$runpid" 2>/dev/null || :
    fi
}

# srv_files DIR EXEDIR - put into DIR what VM A's server runs from:
# EXEDIR's INVSERV.EXE, the host-built message file, SRV.CFG, the empty
# data directory DATA and the empty mailbox MBOX that SRV.CFG names.
srv_files() {
    mkdir -p "$1/DATA" "$1/MBOX/REQUEST" "$1/MBOX/RESPONSE"
    cp "$2/INVSERV.EXE" "$repo/BIN/INVSERV.MSG" "$here/SRV.CFG" "$1/"
}

# rc_to FILE - the batch lines that write the error level of the program
# before them to FILE.
rc_to() {
    errlevel_bat 127
    printf 'ECHO %%RC%%> %s\r\n' "$1"
}

# eat DIR KB FILE - copy BIN/EATRUN.COM (TEST/INTEG/EATRUN.ASM) into DIR
# and print the batch line that records in FILE what CHKDSK reports when
# EATRUN leaves it KB kilobytes free, as kb_run leaves the program it
# starts.
eat() {
    cp "$repo/BIN/EATRUN.COM" "$1/"
    printf 'EATRUN %s C:\\DOS\\CHKDSK.COM C: > %s\r\n' "$2" "$3"
}

# kb_run KB - print the start of a batch line whose program, named with
# its extension, EATRUN runs with KB kilobytes free; nothing when KB is
# empty.
kb_run() {
    [ -z "$1" ] || printf 'EATRUN %s ' "$1"
}

# qmon X STEP... - drive VM X through its monitor (qmon.py) once QEMU
# has made the socket; 1 when it has not within a minute or run2vm.sh has
# ended.  qmon.py runs in the background, so that a signal is handled at
# once (halt).
qmon() {
    x=$1
    shift
    i=0
    while [ ! -S "$mondir/$x.sock" ]; do
        [ "$i" -lt "$(dos_secs 60)" ] && kill -0 "$runpid" 2>/dev/null ||
            return 1
        sleep 1 &
        wait $!
        i=$((i + 1))
    done
    python3 -I "$here/qmon.py" "$mondir/$x.sock" "$@" &
    qpid=$!
    r=0
    wait "$qpid" || r=$?
    qpid=
    return "$r"
}

# wait_for TEXT FILE SECS - 0 once FILE holds TEXT, 1 when SECS have gone
# or run2vm.sh has ended first.
wait_for() {
    i=0
    until grep -qs -- "$1" "$2"; do
        [ "$i" -lt "$(($3 * 5))" ] && kill -0 "$runpid" 2>/dev/null ||
            return 1
        sleep 0.2
        i=$((i + 1))
    done
}

# quit_vms - end this run's VMs; run2vm.sh still extracts what their
# disks hold.
quit_vms() {
    for x in A B; do
        [ ! -S "$mondir/$x.sock" ] ||
            python3 -I "$here/qmon.py" "$mondir/$x.sock" quit >/dev/null || :
    done
}

# expect FILE STATUS [NAME] - check the error level a batch file wrote to
# FILE, which a failure names NAME (default its base name).
expect() {
    got=$(tr -dc '0-9' 2>/dev/null <"$1" || :)
    [ "$got" = "$2" ] && return 0
    echo "$prog: ${3:-${1##*/}} holds '$got', not $2" >&2
    status=1
}

# open_check DIR NAME RUN [OPTION KB FLOOR] - when DIR/NAME.RC, the error
# level a batch file wrote for the INV run RUN, holds EXIT_OPEN (2,
# INV.C) or EXIT_MSG (7), print on standard error that RUN did not open
# its link or could not load INV.MSG, as at a memory floor; with KB
# given, add that RUN started with KB kilobytes free (the gate's OPTION)
# and that the makefile's floor FLOOR_FREEKB_<build> may be too low.
# Prints nothing for any other level or a missing file; returns 0.
open_check() {
    oc_rc=$(tr -dc '0-9' 2>/dev/null <"$1/$2.RC" || :)
    case $oc_rc in
        2) oc_why='did not open its link' ;;
        7) oc_why='could not load INV.MSG' ;;
        *) return 0 ;;
    esac
    printf '%s: %s exited %s: it %s%s\n' "$prog" "$3" "$oc_rc" "$oc_why" \
        "${5:+ (with $4 $5: ${6}_FREEKB_<build> may be too low)}" >&2
}

# mask FILE - print the transcript FILE with CR removed and the figures
# that vary from run to run masked as "*": the K UPTIME= value and the
# IDLE column of ADMIN CLIENTS.
mask() {
    tr -d '\r' <"$1" | sed \
        -e 's/^\([0-9]< K UPTIME=\)[0-9][0-9]*$/\1*/' \
        -e 's/^\([0-9]< R [0-9][0-9]* [A-Z0-9]* [^ ]* [^ ]* [0-9] [0-9] \)'\
'[0-9][0-9]* /\1* /'
}

# The goldens compared and written by golden_cmp and golden.
gd_cmp=0
gd_wrote=0

# golden_cmp FILE NAME WHAT - compare FILE, the gate's WHAT, with
# TEST/GOLDEN/NAME.
golden_cmp() {
    gd_cmp=$((gd_cmp + 1))
    diff -u "$repo/TEST/GOLDEN/$2" "$1" >&2 || {
        echo "$prog: $3 differs from TEST/GOLDEN/$2" >&2
        status=1
    }
}

# golden FILE NAME WHAT - golden_cmp FILE NAME WHAT, or with update 1
# rewrite TEST/GOLDEN/NAME from FILE instead.
golden() {
    if [ "$update" -eq 1 ]; then
        cp "$1" "$repo/TEST/GOLDEN/$2"
        echo "$prog: rewrote TEST/GOLDEN/$2"
        gd_wrote=$((gd_wrote + 1))
    else
        golden_cmp "$@"
    fi
}

# golden_done N - fail the gate unless N goldens in all were compared or
# written, none written without update 1.
golden_done() {
    [ $((gd_cmp + gd_wrote)) -eq "$1" ] &&
        { [ "$gd_wrote" -eq 0 ] || [ "$update" -eq 1 ]; } || {
        echo "$prog: $gd_cmp goldens compared and $gd_wrote written" \
            "with update $update, not $1" >&2
        status=1
    }
}

# golden_check DIR NAME:GOLDEN... - check each transcript DIR/NAME.OUT,
# masked, against TEST/GOLDEN/GOLDEN.TXT; with update 1, rewrite instead
# each golden from the transcript of its own name (golden).  A missing
# transcript fails the gate; then golden_done for the others.
golden_check() {
    gc_dir=$1
    gc_left=0
    shift
    for gc_t in "$@"; do
        gc_n=${gc_t%:*}
        gc_g=${gc_t#*:}
        if [ ! -f "$gc_dir/$gc_n.OUT" ]; then
            echo "$prog: no $gc_n.OUT" >&2
            status=1
            continue
        fi
        gc_left=$((gc_left + 1))
        mask "$gc_dir/$gc_n.OUT" >"$mondir/$gc_n.MSK"
        if [ "$gc_n" = "$gc_g" ]; then
            golden "$mondir/$gc_n.MSK" "$gc_g.TXT" "$gc_n.OUT"
        else
            golden_cmp "$mondir/$gc_n.MSK" "$gc_g.TXT" "$gc_n.OUT"
        fi
    done
    golden_done "$gc_left"
}

# pw_check SCRIPT... -- PLACE... - check that no new password of the
# request scripts SCRIPT (the last argument of a PASSWD request, sent
# bare or after a "> " or ">& " sigil, blanks or tabs between the words)
# is in a file at or below any PLACE, the copies of the scripts under
# their own names aside.  A PLACE that is missing or that grep cannot
# read fails the check too; a SCRIPT that cannot be read stops with die.
pw_check() {
    pc_sed='s/^\(>&\{0,1\}[[:blank:]]\{1,\}\)\{0,1\}PASSWD[[:blank:]]\{1,\}'
    pc_sed=$pc_sed'[^[:blank:]]\{1,\}[[:blank:]]\{1,\}\([^[:blank:]]\{1,\}\)'
    pc_sed=$pc_sed'[[:blank:]]*$/\2/p'
    pc_pws=
    pc_skip=
    while [ $# -gt 0 ] && [ "$1" != -- ]; do
        [ -r "$1" ] || die "pw_check: cannot read $1"
        pc_pws="$pc_pws $(tr -d '\r' <"$1" | sed -n "$pc_sed")"
        pc_skip="$pc_skip
${1##*/}"
        shift
    done
    [ $# -ge 2 ] || die "pw_check: no place to search"
    shift
    [ -n "$(printf '%s' "$pc_pws" | tr -d ' \n')" ] ||
        die "the scripts change no password to look for"
    pc_gone=0
    for pc_p in "$@"; do
        if [ ! -e "$pc_p" ]; then
            echo "$prog: no $pc_p to search for passwords" >&2
            pc_gone=1
            status=1
        fi
    done
    for pc_pw in $(printf '%s\n' $pc_pws | sort -u); do
        pc_rc=0
        pc_hits=$(grep -rlF -- "$pc_pw" "$@" 2>/dev/null) || pc_rc=$?
        if [ "$pc_rc" -ge 2 ] && [ "$pc_gone" -eq 0 ]; then
            echo "$prog: grep could not search every place for $pc_pw" >&2
            status=1
        fi
        pc_hits=$(printf '%s\n' "$pc_hits" | while IFS= read -r pc_f; do
            [ -z "$pc_f" ] || printf '%s\n' "$pc_skip" |
                grep -qxF -- "${pc_f##*/}" || printf ' %s' "$pc_f"
        done)
        [ -z "$pc_hits" ] ||
            { echo "$prog: password $pc_pw is in$pc_hits" >&2; status=1; }
    done
}

# log_check LOG COUNT [NAME] - check the server's event log LOG:
# "YYYY-MM-DD HH:MM:SS slot EVENT text" lines, the server called NAME
# (default RISRV) started and stopped COUNT times, each STOP 0 right after
# a STACK event showing at least SRV_STACK_MARGIN (TEST/shlib.sh) stack
# bytes never used.
log_check() {
    if [ ! -f "$1" ]; then
        echo "$prog: no event log" >&2
        status=1
        return
    fi
    line='^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} [0-9]+ '
    line="$line(START|STACK|STOP|OPEN|LOGIN|REQ|CLOSE|MBOX)( |\$)"
    bad=$(tr -d '\r' <"$1" | grep -Evn "$line" | head -n 1) || :
    [ -z "$bad" ] || { echo "$prog: event log line $bad" >&2; status=1; }
    for e in "START ${3:-RISRV}" 'STOP 0'; do
        n=$(tr -d '\r' <"$1" | grep -c " 0 $e\$") || :
        [ "$n" -eq "$2" ] ||
            { echo "$prog: $n '$e' events, not $2" >&2; status=1; }
    done
    stk=$(tr -d '\r' <"$1" | awk '
        s != "" && / 0 STOP 0$/ { print s }
        { s = "" }
        / 0 STACK [0-9]+$/ { s = $NF }')
    n=$(printf '%s' "$stk" | grep -c .) || :
    [ "$n" -eq "$2" ] || {
        echo "$prog: $n STACK events right before a STOP 0, not $2" >&2
        status=1
    }
    for n in $stk; do
        echo "$prog: server stack never used: $n bytes"
        [ "$n" -ge "$SRV_STACK_MARGIN" ] || {
            echo "$prog: $n unused stack bytes," \
                "fewer than $SRV_STACK_MARGIN" >&2
            status=1
        }
    done
}

# free_print X... - print each VM X's free memory with the network.
free_print() {
    for x in "$@"; do
        printf '%s: VM %s free memory with the network: %s bytes\n' \
            "$prog" "$x" \
            "$(tr -d '\r' 2>/dev/null <"$OUT/$x/D/CHKDSK.OUT" |
               sed -n 's/^ *\([0-9][0-9]*\) bytes free$/\1/p' | head -n 1)"
    done
}

# free_check X KB FILE - with KB given, check that VM X's program started
# with KB kilobytes free (free310_check of the FILE eat wrote).
free_check() {
    [ -n "$2" ] || return 0
    why=$(free310_check "$OUT/$1/D/$3" "$2") && return 0
    echo "$prog: VM $1: $why" >&2
    status=1
}

# gate_end FILE... - exit 0 with PASS when every check passed; else print
# each FILE, keep the result directory as OUT.fail.<timestamp> and fail.
gate_end() {
    if [ "$status" -eq 0 ]; then
        echo "$prog: PASS"
        exit 0
    fi
    for f in "$@"; do
        echo "===== $f"
        if [ -f "$f" ]; then tr -d '\r' <"$f"; else echo "(missing)"; fi
    done
    keep="$OUT.fail.$(date +%Y%m%d-%H%M%S)"
    mv "$OUT" "$keep"
    fail "FAIL; run kept in $keep"
}
