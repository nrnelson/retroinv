# shlib.sh - shell helpers shared by the build and test scripts.
#
# Helpers for the build, test and release scripts (TC/, WC65/, WC95/,
# TEST/, REL/) and MAKEFILE recipes, which source it (". TEST/shlib.sh").  Not
# run on its own.  POSIX sh: every function works under dash and under
# "set -eu".  The functions keep their working values in variables named
# sl_*, and the runner core its state in rb_*; a script that sources this
# file uses neither prefix for its own variables.
#
# The helpers stop the script in one of two ways: die, with status 2, on
# a usage or setup error (a bad argument, a missing tool or file, a lock
# not had in time), and fail, with status 1, when the work itself failed
# (a build that went wrong).  A script may assign prog after sourcing to
# change the name its messages start with, and may define its own die or
# fail to change how it stops; the functions here call them by name, so
# the script's definitions are used.  Such a definition must still end the
# script with exit, never return: the helpers go on as if it had not been
# called.  fail is called by runbat_finish (through runbat_build) and by
# runbat_check, which would otherwise carry on after a failed build.  One
# path bypasses fail: when the log check finds errors, a missing end
# marker or a missing executable, runbat_check prints a message for each
# and then exits 1 itself, so an override does not change that path.

# The base name of the running script, the prefix of every message.
prog=${0##*/}

# The characters of a DOS 8.3 name as this repository writes them: upper
# case, digits and the punctuation DOS accepts, "-" last so that the set
# reads the same as a shell pattern and as an awk bracket expression.
DOS83_SET='A-Z0-9_$~!#&-'

# The 8.3 rule, as an awk program run with set=DOS83_SET: each input line
# is a "/"-separated list of names, each one to eight characters of the
# set, optionally followed by a dot and one to three more.  It prints the
# first line that breaks the rule and exits 1, or exits 0.
DOS83_AWK='
    BEGIN { name = "^[" set "]+$" }
    { n = split($0, part, "/")
      ok = (n > 0)
      for (i = 1; ok && i <= n; i++) {
          d = index(part[i], ".")
          base = d ? substr(part[i], 1, d - 1) : part[i]
          ext = d ? substr(part[i], d + 1) : ""
          ok = base ~ name && length(base) <= 8 &&
              (!d || (ext ~ name && length(ext) <= 3))
      }
      if (!ok) { print; exit 1 } }'

# The instant the test data are seeded at: the /NOW: value of the host INV
# runs and of the DOS runs that compare with them, and the same instant as
# the start time of a QEMU machine's clock (-rtc base=).
SEED_NOW=20261008120000
SEED_CLOCK=2026-10-08T12:00:00

# The longest command line COMMAND.COM accepts, excluding the terminating
# CR.
DOS_MAXCMD=126

# The characters of numbers an errlevel_bat line lists before its last,
# which keeps the line well inside DOS_MAXCMD.
ERRLEVEL_LIST=60

# The stack bytes a DOS run of INVSERV must leave untouched, as the STACK
# event of its event log counts them (TEST/INTEG/mbox310.sh and log_check
# of TEST/INTEG/vmgate.sh): half the stack of every build (_stklen of
# SRC/COMPAT/CDOS.C), kept for interrupt handlers and paths no run takes.
SRV_STACK_MARGIN=4096

# The -accel value of every QEMU run.  tb-size caps TCG's translation cache
# in MB; left at its default it grows each QEMU process to about 1.2 GB
# resident whatever the guest's memory.
QEMU_ACCEL=tcg,tb-size=32

# The root of the period downloads, outside the repository.
RETRO_DL=${RETRO_DL:-$HOME/retro-dl}

# The MS-DOS 3.10 boot floppy of the emulator runs: DOS310_IMG, else the
# Compaq disk under $RETRO_DL/dos310/.
DOS310_FLOPPY=${DOS310_IMG:-$RETRO_DL/dos310/Microsoft MS-DOS 3.10 [Compaq OEM Rev C] (1986-03-27) (5.25-360k)/disk01.img}

# The directory of the MS-DOS 3.30 360 KB disks DISK01.IMG and DISK02.IMG,
# and its boot floppy, disk 1, whose DOS also reads 720 KB and 1.44 MB
# disks.
DOS330_DIR=${DOS330_DIR:-"$RETRO_DL/dos330/Microsoft MS-DOS 3.30 (5.25)"}
DOS330_FLOPPY=$DOS330_DIR/DISK01.IMG

# The directory of the MS-DOS 5.00 720 KB disks DISK01.IMG to DISK03.IMG
# (any letter case), whose disk 1 also boots.
DOS500_DIR=${DOS500_DIR:-"$RETRO_DL/dos500/Microsoft MS-DOS 5.00 (3.5-720k)"}

# The shells that TEST/INTEG/boot310.sh and the image builders' img_shell
# make reload their transient part after every program, one per line: the
# SHA-256 of a COMMAND.COM and the file offset of the JZ in its resident
# part that skips the reload when the transient part's word sum is
# unchanged (Compaq MS-DOS 3.10, MS-DOS 3.30, MS-DOS 5.00).
DOS_SHELLS='55dd70c1658cd9d1f9bf3ebc0334901e182f2f5943ca99397743ac4603a62beb 508
cd76da9fa330349af658e9568b0b0b0d6789cc0ad73d0c10d3465cded69e9f05 589
74ad25067e7d9f5c3a0ac2f2967347defb0ffef9e6ed60b60e6499f0b1a8cc89 3754'

# shell_patch FILE - set shell_sum to the SHA-256 of FILE, a COMMAND.COM;
# when DOS_SHELLS lists it, put two NOPs over its JZ and succeed (die when
# FILE cannot be written), else leave FILE as it is and fail.
shell_patch() {
    shell_sum=$(sha256sum <"$1")
    shell_sum=${shell_sum%% *}
    sp_jz=$(printf '%s\n' "$DOS_SHELLS" |
        awk -v s="$shell_sum" '$1 == s { print $2 }')
    [ -n "$sp_jz" ] || return 1
    printf '\220\220' |
        dd of="$1" bs=1 seek="$sp_jz" conv=notrunc 2>/dev/null ||
        die "cannot patch $1"
}

# die MSG... - print "prog: MSG" to standard error and exit with status 2,
# the status of a usage or setup error.
die() {
    printf '%s: %s\n' "$prog" "$*" >&2
    exit 2
}

# fail MSG... - print "prog: MSG" to standard error and exit with status
# 1, the status of a failed build.
fail() {
    printf '%s: %s\n' "$prog" "$*" >&2
    exit 1
}

# is_uint VALUE - true when VALUE is a non-empty string of decimal digits.
is_uint() {
    case $1 in ''|*[!0-9]*) return 1 ;; *) return 0 ;; esac
}

# need_tools TOOL... - stop with die unless every TOOL is on the PATH.
need_tools() {
    for sl_t in "$@"; do
        command -v "$sl_t" >/dev/null 2>&1 || die "missing tool: $sl_t"
    done
}

# find_ci DIR NAME - print the path of the file NAME in DIR, matching
# case-insensitively; stops with die when DIR or the file is missing.
# Called as $(find_ci ...), whose status 2 then stops a "set -e" script.
find_ci() {
    [ -d "$1" ] || die "directory not found: $1"
    sl_f=$(find "$1" -maxdepth 1 -type f -iname "$2" | head -n 1)
    [ -n "$sl_f" ] || die "$2 not found in $1"
    printf '%s\n' "$sl_f"
}

# jobs_ok NAME VALUE - stop with die unless VALUE, the value of NAME, is
# a whole number of 1 or more written without a leading zero (so "04" is
# refused as well as "0").
jobs_ok() {
    case $2 in
    ''|0*|*[!0-9]*)
        die "$1 must be a whole number, 1 or more, without a leading" \
            "zero: '$2'" ;;
    esac
}

# dos_secs SECS - print SECS, a host-side time limit of an emulator run
# in whole seconds, 1 or more, multiplied by DOSTIME_SCALE (a whole number
# from 1 to 9; unset or empty means 1), so that a slower or more heavily
# shared host can run more emulators at once without false timeouts.
# Each script applies it once, where it starts the emulator, to the limit
# it was given; the limits passed around keep their unscaled values.
# Stops with die on any other SECS or DOSTIME_SCALE.
dos_secs() {
    case $1 in
    ''|0*|*[!0-9]*) die "a time limit must be a whole number of seconds," \
        "1 or more: '$1'" ;;
    esac
    case ${DOSTIME_SCALE:-1} in
    [1-9]) echo $(($1 * ${DOSTIME_SCALE:-1})) ;;
    *) die "DOSTIME_SCALE must be a whole number from 1 to 9:" \
        "'$DOSTIME_SCALE'" ;;
    esac
}

# dos_secs_said SECS - the limit dos_secs SECS as a timeout message names
# it: "<N>s", followed by " (<SECS>s x <DOSTIME_SCALE>)" when the scale
# is not 1.
dos_secs_said() {
    sl_n=$(dos_secs "$1")
    if [ "${DOSTIME_SCALE:-1}" = 1 ]; then
        echo "${sl_n}s"
    else
        echo "${sl_n}s (${1}s x $DOSTIME_SCALE)"
    fi
}

# is_83 NAME - true when NAME is one upper-case 8.3 name (DOS83_AWK).
is_83() {
    case $1 in */*|*'
'*) return 1 ;;
    esac
    printf '%s\n' "$1" | awk -v set="$DOS83_SET" "$DOS83_AWK" >/dev/null
}

# check83 PATH... - stop with die unless every PATH is a "/"-separated
# list of upper-case 8.3 names (DOS83_AWK), so that it exists under DOS.
# All the paths are checked in one awk process.
check83() {
    [ $# -gt 0 ] || return 0
    sl_bad=$(printf '%s\n' "$@" | awk -v set="$DOS83_SET" "$DOS83_AWK") ||
        die "'$sl_bad' is not an upper-case 8.3 path"
}

# dos_text [FILE] - copy FILE, or standard input, to standard output with
# CR LF line ends; lines that already end in CR LF are not given a second
# CR.
dos_text() {
    sed 's/\r*$/\r/' ${1+"$1"}
}

# byte N - write N, taken modulo 256, to standard output as one byte.
byte() {
    printf "\\$(printf %03o $(($1 & 255)))"
}

# le32 N - write N to standard output as four little-endian bytes.
le32() {
    byte "$1"; byte $(($1 >> 8)); byte $(($1 >> 16)); byte $(($1 >> 24))
}

# img_expand FILE OUT - expand the SZDD file FILE into OUT with msexpand
# and check OUT's length against the one FILE's header records (bytes 10
# to 13, little-endian); stops with die when they differ.
img_expand() {
    sl_len=$(od -An -tu1 -j10 -N4 "$1" |
        awk '{ print $1 + 256 * ($2 + 256 * ($3 + 256 * $4)) }')
    msexpand <"$1" >"$2"
    [ "$(wc -c <"$2")" -eq "$sl_len" ] || die "expand failed: $1"
}

# dospath PATH - print PATH with every "/" turned into "\".
dospath() {
    sl_in=$1
    sl_out=
    while :; do
        case $sl_in in
        */*) sl_out=$sl_out${sl_in%%/*}'\'; sl_in=${sl_in#*/} ;;
        *) break ;;
        esac
    done
    printf '%s\n' "$sl_out$sl_in"
}

# emit FILE LINE - append LINE to batch file FILE with a CR LF end; stops
# with die when LINE is longer than DOS_MAXCMD.
emit() {
    [ ${#2} -le $DOS_MAXCMD ] ||
        die "command longer than $DOS_MAXCMD chars: $2"
    printf '%s\r\n' "$2" >>"$1"
}

# signal_status NAME - the exit status of a script ended by signal NAME
# (HUP, INT or TERM): 128 plus the signal number.
signal_status() {
    case $1 in HUP) echo 129 ;; INT) echo 130 ;; *) echo 143 ;; esac
}

# on_signals [HANDLER] - make SIGHUP, SIGINT and SIGTERM end the script
# with the status signal_status gives them; an EXIT trap still runs.
# With HANDLER, a shell function, a signal first resets the three traps,
# so that a second signal ends the script at once, and runs "HANDLER
# NAME" (NAME being HUP, INT or TERM) to stop what the script started.
on_signals() {
    for sl_sig in HUP INT TERM; do
        trap "${1:+trap - HUP INT TERM; $1 $sl_sig; }exit $(signal_status $sl_sig)" $sl_sig
    done
}

# errlevel_bat MAX - write to standard output, with CR LF line ends, the
# batch lines that leave the error level of the last program, 0 to MAX
# (at most 255), in the environment variable RC: lines of the form
# "FOR %%E IN (0 1 2 ...) DO IF ERRORLEVEL %%E SET RC=%%E", whose lists
# together run from 0 to MAX in order, so that the last true test sets RC.
# Higher levels give MAX.  Each list stops once it passes ERRLEVEL_LIST
# characters.  The caller's next line
# writes the value, for example "ECHO %RC%>E:\RC.TXT"; a redirection on
# the FOR lines themselves would truncate the file at every test.
errlevel_bat() {
    is_uint "$1" && [ "$1" -ge 1 ] && [ "$1" -le 255 ] ||
        die "errlevel_bat: MAX must be 1 to 255, got '$1'"
    sl_n=0
    while [ $sl_n -le "$1" ]; do
        sl_list=$sl_n
        sl_n=$((sl_n + 1))
        while [ $sl_n -le "$1" ] && [ ${#sl_list} -le $ERRLEVEL_LIST ]; do
            sl_list="$sl_list $sl_n"
            sl_n=$((sl_n + 1))
        done
        printf 'FOR %%%%E IN (%s) DO IF ERRORLEVEL %%%%E SET RC=%%%%E\r\n' \
            "$sl_list"
    done
}

# tool_out TOOL FILE STATUS - check FILE, the standard output (LF or CR LF
# line ends) of a run of the test tool TOOL (NBTEST, TCPTEST, COMTEST)
# that ended with exit status STATUS.  A bad command line (status 1)
# writes nothing there; any other run writes lines that each start with
# TOOL and a blank, the last its verdict, "TOOL PASS" for status 0 and
# "TOOL FAIL" and a reason otherwise: lower-case words, which may follow
# the name of the device concerned, in capitals with a colon ("COM1: no
# such port").  Returns 0, or prints what is wrong and returns 1.
tool_out() {
    tr -d '\r' <"$2" | awk -v t="$1" -v st="$3" '
        st == 1 { print "standard output not empty: " $0; bad = 1; exit }
        index($0, t " ") != 1 {
            print "line " NR " not prefixed: " $0
            bad = 1
        }
        { last = $0 }
        END {
            if (bad || st == 1)
                exit bad
            if (st == 0 && last != t " PASS") {
                print "last line not the verdict " t " PASS: " last
                exit 1
            }
            if (st != 0 && last !~ ("^" t " FAIL ([A-Z]+[0-9]*: )?[a-z]")) {
                print "last line not a verdict " t " FAIL: " last
                exit 1
            }
        }'
}

# dos_lock_take s|x WAIT - take the emulator lock of the DOS test runs,
# named by DOS_LOCK (files $DOS_LOCK.Q and $DOS_LOCK.LCK), shared (s) or
# exclusive (x), waiting at most WAIT seconds for each of its two steps.
# The queue lock $DOS_LOCK.Q is taken first, on descriptor 7, and then
# the lock itself, on descriptor 8.  An exclusive taker keeps the queue
# lock while it waits for the readers to leave and while it runs, and a
# shared taker lets go of it as soon as it has the lock, so that no new
# shared taker can slip past a waiting exclusive one.  The calling shell
# holds what it took until it closes the descriptors or exits; commands
# it starts that must not hold the locks are run with 7>&- 8>&-.  Returns
# non-zero, holding neither lock, when a step times out or the lock
# directory or its files cannot be made or opened.
dos_lock_take() {
    case $1 in
    s|x) ;;
    *) die "dos_lock_take: mode must be s or x, got '$1'" ;;
    esac
    is_uint "$2" ||
        die "dos_lock_take: WAIT must be a whole number of seconds, got '$2'"
    [ -n "${DOS_LOCK:-}" ] || die "dos_lock_take: DOS_LOCK is not set"
    mkdir -p "$(dirname "$DOS_LOCK")" || return 1
    command exec 7>>"$DOS_LOCK.Q" 8>>"$DOS_LOCK.LCK" || return 1
    if ! { flock -w "$2" 7 && flock -w "$2" "-$1" 8; }; then
        flock -u 7 2>/dev/null || true
        return 1
    fi
    [ "$1" = x ] || flock -u 7
}

# The host ports of the QEMU runs: run n of those running at once holds
# port slot n, 0 to PORT_SLOTS - 1, and with it the ports PORT_BASE + 2n
# and PORT_BASE + 2n + 1 and the port PORT_BASE + 2 * PORT_SLOTS + n (the
# band up to PORT_BASE + 3 * PORT_SLOTS - 1, above every slot's pair).
PORT_BASE=12340
PORT_SLOTS=32

# port_slot - take the first port slot that no other run holds, its lock
# file in PORT_LOCKS (default ${TMPDIR:-/tmp}/retroinv-ports-<uid>), on
# descriptor 6, which the QEMUs the script starts inherit, so that the
# slot is held while the script or one of them lives; sets sl_slot to its
# number.  The directory must be private: the user's own, not a symbolic
# link, and writable by no one else.  A slot file is opened for
# appending, so that it never truncates anything.  Dies when every slot
# is taken.
port_slot() {
    PORT_LOCKS=${PORT_LOCKS:-${TMPDIR:-/tmp}/retroinv-ports-$(id -u)}
    mkdir -m 700 -p "$PORT_LOCKS" 2>/dev/null || :
    [ -d "$PORT_LOCKS" ] && [ ! -h "$PORT_LOCKS" ] && [ -O "$PORT_LOCKS" ] &&
        [ -z "$(find "$PORT_LOCKS" -maxdepth 0 -perm /022)" ] ||
        die "PORT_LOCKS=$PORT_LOCKS is not a private directory of this user's"
    sl_slot=0
    while :; do
        [ "$sl_slot" -lt "$PORT_SLOTS" ] ||
            die "every port slot in $PORT_LOCKS is taken"
        { command exec 6>>"$PORT_LOCKS/slot$sl_slot"; } 2>/dev/null ||
            die "cannot open $PORT_LOCKS/slot$sl_slot"
        if flock -n 6; then return 0; fi
        sl_slot=$((sl_slot + 1))
    done
}

# srcs_vars FILE - print the make-style variable list FILE (SRCS.MK) as
# "NAME<TAB>words" lines, one per variable in the order of first
# definition.  Backslash continuations are joined, "#" comments dropped,
# and ":=", "=" and "+=" accepted; runs of blanks become one space.  A
# "$(NAME)" reference in a value, or in a variable's name, is replaced by
# the value NAME has at that point, as make does for ":="; a reference to
# a variable not yet defined, or an unterminated "$(", prints a message
# and returns 2.
srcs_vars() {
    awk -v prog="$prog" '
    function expand(s,   out, i, j, ref) {
        out = ""
        while ((i = index(s, "$(")) > 0) {
            j = index(substr(s, i + 2), ")")
            if (j == 0) { bad = "unterminated $( in " name; return "" }
            ref = substr(s, i + 2, j - 1)
            if (!(ref in v)) {
                bad = "undefined variable " ref " used in " name; return ""
            }
            out = out substr(s, 1, i - 1) v[ref]
            s = substr(s, i + 2 + j)
        }
        return out s
    }
    { sub(/\r$/, "") }
    cont { line = line " " $0 } !cont { line = $0 }
    /\\$/ { sub(/\\$/, "", line); cont = 1; next }
    { cont = 0
      sub(/#.*/, "", line)
      if (line !~ /=/) next
      op = index(line, "=")
      name = substr(line, 1, op - 1); val = substr(line, op + 1)
      app = (name ~ /\+$/)
      sub(/[:+]$/, "", name); gsub(/[ \t]+/, " ", name)
      gsub(/^ +| +$/, "", name)
      name = expand(name)
      val = expand(val)
      if (bad != "") { print prog ": " bad | "cat 1>&2"; exit 2 }
      gsub(/[ \t]+/, " ", val); gsub(/^ +| +$/, "", val)
      if (app && (name in v)) v[name] = v[name] " " val
      else v[name] = val
      if (!(name in seen)) { seen[name] = 1; order[++n] = name } }
    END { if (bad != "") exit 2
          for (i = 1; i <= n; i++) printf "%s\t%s\n", order[i], v[order[i]] }
    ' "$1"
}

# getvar NAME - print the value of NAME in $vars, which holds the output
# of srcs_vars; prints nothing for a name not defined there.
getvar() {
    printf '%s\n' "$vars" | awk -F '\t' -v k="$1" '$1 == k { print $2 }'
}

# cdefine FILE NAME - print the number that the C source FILE gives NAME
# in a "#define NAME number" line; stops with die when FILE has no such
# line.
cdefine() {
    sl_v=$(sed -n "s/^#define[ \t]*$2[ \t]*\([0-9][0-9]*\).*/\1/p" "$1")
    [ -n "$sl_v" ] || die "$2 not found in $1"
    printf '%s\n' "$sl_v"
}

# copy_tree ROOT WORK CONVERT GENINC [DIR...] - copy every .C and .H file
# under ROOT/SRC, ROOT/TEST and each ROOT/DIR that exists (WATTCP's
# prepared tree) into the build tree WORK through the filter CONVERT
# (cat, or dos_text for CR LF copies): each .C file to the same path below
# WORK, each .H file into the one directory WORK/INC, so that an include
# path of one directory finds every header.  GENINC is a blank-separated
# list of further headers (generated ones) copied into WORK/INC the same
# way.  Stops with die when a copied path, or a GENINC base name, is not
# 8.3 (check83), when two of the headers share a base name, or when a
# GENINC file is missing.
copy_tree() {
    sl_files=$(cd "$1" && shift 4 && for sl_top in SRC TEST "$@"; do
        if [ -d "$sl_top" ]; then
            find "$sl_top" -type f \( -name '*.C' -o -name '*.H' \)
        fi
    done | sort)
    sl_dup=$(printf '%s\n' $sl_files $4 | awk -F/ '/\.H$/ { b = $NF
            if (b in seen) { print b " in " seen[b] " and " $0; exit }
            seen[b] = $0 }')
    [ -z "$sl_dup" ] || die "duplicate header name $sl_dup"
    check83 $sl_files
    mkdir -p "$2/INC"
    for sl_f in $sl_files; do
        case $sl_f in
        *.H) "$3" "$1/$sl_f" >"$2/INC/${sl_f##*/}" ;;
        *) mkdir -p "$2/${sl_f%/*}"
           "$3" "$1/$sl_f" >"$2/$sl_f" ;;
        esac
    done
    sl_gen=
    for sl_f in $4; do
        [ -f "$sl_f" ] || die "generated header $sl_f not found"
        sl_gen="$sl_gen ${sl_f##*/}"
    done
    check83 $sl_gen
    for sl_f in $4; do
        "$3" "$sl_f" >"$2/INC/${sl_f##*/}"
    done
}

# deal_out COMP JOBS OPEN EMIT - deal the compiles listed in file COMP out
# to the batch files CC1.BAT .. CC<JOBS>.BAT in COMP's directory, and list
# their base names (CC1, CC2, ...) in CC.LST there.  COMP holds one compile
# per line in build order, as tab-separated fields: the source size in
# bytes, then the fields EMIT takes, none of them empty.  Fewer batch files
# are written when COMP has fewer lines than JOBS, so the number written is
# the line count of CC.LST.  Each batch file is created empty and passed to
# "OPEN FILE K" (K its number) for its first lines.  The compiles go
# largest first, each to the batch with the fewest bytes so far (the lowest
# number on a tie), and a batch keeps its compiles in build order: for each
# line of COMP, "EMIT FILE FIELD..." appends the commands to its batch
# file, the fields being those after the size.  Every batch file then ends
# with "ECHO === CC<k>.BAT DONE".  COMP is deleted.  Stops with die when
# JOBS is refused by jobs_ok, or when COMP is empty.
deal_out() {
    jobs_ok "the number of compile batch files" "$2"
    sl_comp=$1
    sl_jobs=$2
    sl_open=$3
    sl_emit=$4
    case $sl_comp in */*) sl_dir=${sl_comp%/*} ;; *) sl_dir=. ;; esac
    sl_total=$(wc -l <"$sl_comp")
    [ "$sl_total" -gt 0 ] || die "nothing to compile"
    [ "$sl_jobs" -le "$sl_total" ] || sl_jobs=$sl_total
    : >"$sl_dir/CC.LST"
    sl_k=1
    while [ $sl_k -le "$sl_jobs" ]; do
        : >"$sl_dir/CC$sl_k.BAT"
        "$sl_open" "$sl_dir/CC$sl_k.BAT" $sl_k
        printf 'CC%s\n' $sl_k >>"$sl_dir/CC.LST"
        sl_k=$((sl_k + 1))
    done
    sl_tab=$(printf '\t')
    awk -F "$sl_tab" '{ print $1 "\t" NR }' "$sl_comp" |
        sort -t "$sl_tab" -k1,1nr -k2,2n |
        awk -F "$sl_tab" -v n="$sl_jobs" '{ best = 1
            for (b = 2; b <= n; b++) if (load[b] < load[best]) best = b
            load[best] += $1; print $2 "\t" best }' |
        sort -n | cut -f2 | paste "$sl_comp" - | {
        # This subshell splits each line into the EMIT fields: on tabs
        # only, and without expanding a "*" or "?" in them.
        set -f
        while IFS= read -r sl_line; do
            sl_k=${sl_line##*"$sl_tab"}
            sl_line=${sl_line%"$sl_tab"*}
            sl_line=${sl_line#*"$sl_tab"}
            sl_ifs=$IFS
            IFS=$sl_tab
            set -- $sl_line
            IFS=$sl_ifs
            "$sl_emit" "$sl_dir/CC$sl_k.BAT" "$@"
        done
    } || exit $?
    sl_k=1
    while [ $sl_k -le "$sl_jobs" ]; do
        emit "$sl_dir/CC$sl_k.BAT" "ECHO === CC$sl_k.BAT DONE"
        sl_k=$((sl_k + 1))
    done
    rm -f "$sl_comp"
}

# Runner core of TC/runtc.sh, WC65/runwc.sh and WC95/runwc.sh, which
# build a compiler's executables in headless DOSBox-X from the batch files
# its generator writes.  The runner sets, before calling runbat_prepare:
#   rb_dir      the compiler's directory, which holds the build lock
#               rb_dir/.LOCK
#   rb_work     the work tree the generator writes: EXES.LST, CC.LST, the
#               link batch file BUILD.BAT, the logs CC<k>.LOG and LINK.LOG,
#               the assembled log BUILD.LOG, and the executables
#   rb_timeout  seconds before a DOSBox-X instance is killed, and the
#               longest wait for the build lock (and for DOS_LOCK when
#               DOS_LOCK_WAIT is unset), each times DOSTIME_SCALE
#               (dos_secs, applied by runbat_prepare as rb_limit)
#   rb_conf     the DOSBox-X configuration template; runbat_build fills
#               in @BAT@ and @LOG@ for each instance
#   rb_sed      further sed commands applied to the template (its other
#               @NAME@ fields), or empty
# and installs "trap runbat_cleanup EXIT" before it.  The core sets
# rb_limit (rb_timeout scaled), rb_exes (the executables of the run) and
# rb_bats (the compile batch files' base names).  A runner then calls
# runbat_prepare, runbat_build and runbat_check, in that order.
rb_limit=
rb_exes=
rb_bats=
rb_scratch=
rb_pids=
rb_names=
rb_sed=

# runbat_cleanup - the EXIT trap of a runner: stop the DOSBox-X instances
# still running, delete the scratch directory and, when the script is
# failing, the executables of this run, so that a linker that reports a
# problem but still writes its output leaves nothing behind.
runbat_cleanup() {
    sl_rc=$?
    for sl_p in $rb_pids; do kill "$sl_p" 2>/dev/null || true; done
    for sl_p in $rb_pids; do wait "$sl_p" 2>/dev/null || true; done
    [ -z "$rb_scratch" ] || rm -rf "$rb_scratch"
    if [ "$sl_rc" -ne 0 ]; then
        for sl_e in $rb_exes; do rm -f "$rb_work/$sl_e.EXE"; done
    fi
}

# runbat_prepare CMD... - set rb_limit; take the build lock rb_dir/.LOCK
# exclusively on descriptor 9 (waiting at most rb_limit seconds), so that
# one build at a time owns rb_dir and rb_work; run the generator command
# CMD under it with descriptor 9 closed; read rb_exes and rb_bats from the
# lists it wrote; take the emulator lock shared when DOS_LOCK is set
# (dos_lock_take, waiting DOS_LOCK_WAIT seconds, default rb_limit), so
# that a DOS test that must run alone does not run beside the build; and
# delete the logs of an earlier run (CC<k>.LOG, LINK.LOG, BUILD.LOG), so
# that none of them can pass for this run's.  The build lock is held until
# the script exits, the emulator lock until runbat_check.  Called as a
# plain command, so that "set -e" stops the script when CMD fails.
runbat_prepare() {
    command -v flock >/dev/null || die "flock not installed"
    rb_limit=$(dos_secs "$rb_timeout")
    exec 9>"$rb_dir/.LOCK"
    if ! flock -n 9; then
        echo "$prog: waiting for another build to release $rb_dir/.LOCK" >&2
        flock -w "$rb_limit" 9 ||
            die "another build still holds $rb_dir/.LOCK after" \
                "$rb_limit s; giving up"
    fi
    "$@" 9>&-
    rb_exes=$(cat "$rb_work/EXES.LST")
    [ -n "$rb_exes" ] || die "no executables listed in $rb_work/EXES.LST"
    rb_bats=$(cat "$rb_work/CC.LST")
    [ -n "$rb_bats" ] || die "no compile batch files listed in $rb_work/CC.LST"
    for sl_b in $rb_bats; do rm -f "$rb_work/$sl_b.LOG"; done
    if [ -n "${DOS_LOCK:-}" ]; then
        sl_w=${DOS_LOCK_WAIT:-$rb_limit}
        dos_lock_take s "$sl_w" ||
            die "the DOS test runs still hold $DOS_LOCK after $sl_w s;" \
                "giving up"
    fi
    rm -f "$rb_work/BUILD.LOG" "$rb_work/LINK.LOG"
}

# runbat_build CCDIR LINKBAT LINKLOG - run the build: every compile batch
# file of rb_bats at once, each in its own DOSBox-X instance, as
# CCDIR<bat>.BAT writing CCDIR<bat>.LOG (CCDIR being the DOS directory
# that holds them, ending in a backslash, such as C:\BIN\TC\ or E:\);
# then, once all have ended, the link batch file LINKBAT writing LINKLOG
# (DOS paths) in one more instance.  Stops with fail when an instance
# fails or times out.
runbat_build() {
    for sl_b in $rb_bats; do
        runbat_start "$sl_b" "$1$sl_b.BAT" "$1$sl_b.LOG"
    done
    runbat_finish
    runbat_start LINK "$2" "$3"
    runbat_finish
}

# runbat_start NAME BAT LOG - used by runbat_build: start DOSBox-X in the
# background on batch file BAT (a DOS path) with its output going to LOG
# (a DOS path), under a timeout of rb_limit seconds; its configuration
# (rb_conf filled in) and scratch files go in the directory NAME of a
# scratch directory made on the first call.  The instance does not
# inherit descriptors 7 to 9, so it holds no lock.
runbat_start() {
    [ -n "$rb_scratch" ] ||
        rb_scratch=$(mktemp -d "${TMPDIR:-/tmp}/${prog%.sh}.XXXXXX")
    mkdir "$rb_scratch/$1"
    sl_conf=$rb_scratch/$1/${rb_conf##*/}
    # sed reads "\" and "&" in the replacement, and "|" ends it: escape
    # them.
    sl_bat=$(printf '%s\n' "$2" | sed 's/[\\&|]/\\&/g')
    sl_log=$(printf '%s\n' "$3" | sed 's/[\\&|]/\\&/g')
    sed -e "$rb_sed" -e "s|@BAT@|$sl_bat|g" -e "s|@LOG@|$sl_log|g" \
        "$rb_conf" >"$sl_conf"
    (cd "$rb_scratch/$1" &&
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        exec timeout -k 10 "$rb_limit" dosbox-x -conf "$sl_conf" \
            -nogui -nomenu -exit >"$rb_scratch/$1/dosbox.out" 2>&1 \
            7>&- 8>&- 9>&-) &
    rb_pids="$rb_pids $!"
    rb_names="$rb_names $1"
}

# runbat_finish - used by runbat_build: wait for every instance
# runbat_start started since the last call; stops with fail naming the
# first that failed or timed out, after printing the end of its DOSBox-X
# output.
runbat_finish() {
    sl_bad=
    set -- $rb_pids
    for sl_name in $rb_names; do
        sl_st=0
        wait "$1" || sl_st=$?
        shift
        if [ "$sl_st" -ne 0 ] && [ -z "$sl_bad" ]; then
            tail -20 "$rb_scratch/$sl_name/dosbox.out" >&2
            if [ "$sl_st" -eq 124 ]; then
                sl_bad="DOSBox-X ($sl_name) timed out after"
                sl_bad="$sl_bad $(dos_secs_said "$rb_timeout")"
            else
                sl_bad="DOSBox-X ($sl_name) exited with status $sl_st"
            fi
        fi
    done
    rb_pids=
    rb_names=
    [ -z "$sl_bad" ] || fail "$sl_bad"
}

# runbat_check PATTERN [CHECK [SKIP]] - finish a run after runbat_build:
# let go of the emulator lock; assemble rb_work/BUILD.LOG from the logs
# CC<k>.LOG in batch order and LINK.LOG, and print it; then fail when a
# line of the log matches PATTERN and not SKIP (extended regular
# expressions, matched ignoring case: PATTERN for the compiler's error and
# warning messages, SKIP for the warnings it gives that no switch turns
# off), when the log lacks the end marker "=== <name>.BAT DONE" of a
# compile batch file or of BUILD.BAT, or when an executable of rb_exes is
# missing.  Last, CHECK, a shell command, runs when given, still under
# the build lock.  Every failure, CHECK's included, stops the script with
# status 1 after a message naming it.
runbat_check() {
    [ -z "${DOS_LOCK:-}" ] || flock -u 8
    for sl_b in $rb_bats LINK; do
        [ -f "$rb_work/$sl_b.LOG" ] ||
            fail "$rb_work/$sl_b.LOG was not written"
        cat "$rb_work/$sl_b.LOG"
    done >"$rb_work/BUILD.LOG"
    tr -d '\r' <"$rb_work/BUILD.LOG"
    echo "----"
    sl_fail=0
    if tr -d '\r' <"$rb_work/BUILD.LOG" | grep -Eiv "${3:-^\$}" |
        grep -Eiq "$1"; then
        echo "$prog: errors or warnings in $rb_work/BUILD.LOG" >&2
        sl_fail=1
    fi
    for sl_b in $rb_bats BUILD; do
        tr -d '\r' <"$rb_work/BUILD.LOG" | grep -q "^=== $sl_b.BAT DONE\$" || {
            echo "$prog: $rb_work/BUILD.LOG has no end marker for" \
                "$sl_b.BAT" >&2
            sl_fail=1
        }
    done
    for sl_e in $rb_exes; do
        [ -f "$rb_work/$sl_e.EXE" ] ||
            { echo "$prog: $rb_work/$sl_e.EXE missing" >&2; sl_fail=1; }
    done
    [ $sl_fail -eq 0 ] || exit 1
    if [ -n "${2:-}" ]; then
        sh -c "$2" 7>&- 8>&- 9>&- ||
            fail "the checks after the build failed"
    fi
    sl_built=
    for sl_e in $rb_exes; do sl_built="$sl_built$rb_work/$sl_e.EXE "; done
    echo "$prog: built $sl_built"
}

# snap_check GOLDEN SNAP MARGIN - check a snapshot file SNAP holding a
# "=STACK UNUSED=n" line (INV's /SNAP:, the Windows programs' test driver
# of SRC/WIN/TDRV.H) against GOLDEN: 0 when SNAP, that line left out,
# equals GOLDEN byte for byte and n is at least MARGIN; else 1, the
# shortfall and the difference (< golden, > snapshot) on standard error.
# SNAP.CMP keeps SNAP without that line, for an update to copy.
snap_check() {
    sl_n=$(tr -d '\r' <"$2" | sed -n 's/^=STACK UNUSED=\([0-9]*\)$/\1/p')
    grep -av '^=STACK UNUSED=' "$2" >"$2.CMP" || :
    sl_ok=0
    if [ -z "$sl_n" ]; then
        echo "$prog: $2 has no =STACK UNUSED= line" >&2
    elif [ "$sl_n" -lt "$3" ]; then
        echo "$prog: $2 left only $sl_n stack bytes unused (margin $3)" >&2
    else
        sl_ok=1
    fi
    if ! cmp -s "$1" "$2.CMP"; then
        echo "$prog: $2 differs from $1 (< golden, > snapshot):" >&2
        tr -d '\r' <"$1" >"$2.GLD"
        tr -d '\r' <"$2.CMP" | diff "$2.GLD" - >&2 || :
        rm -f "$2.GLD"
        sl_ok=0
    fi
    [ $sl_ok = 1 ]
}
