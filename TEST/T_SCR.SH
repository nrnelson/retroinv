#!/bin/sh
# T_SCR.SH - golden-screen test of INV's full-screen client: every key
# script in TEST/KEYS/ is run and its snapshots compared with the goldens
# in TEST/GOLDEN/SCR/.
#
# usage (from the repository root):
#   sh TEST/T_SCR.SH [-u] [HOSTBIN]
#   sh TEST/T_SCR.SH -d cga|hercules|qemu [-m KB] EXE
#
# The scripts are TEST/KEYS/*.KEY except SCRE*.KEY and SCRR*.KEY, which
# belong to T_SCRE and T_SCRR (the screens run without INV.C); a new
# screen's scripts are picked up by being put there.  Each run happens
# in a fresh scratch current directory holding the data directory TDATA,
# filled first by "INV /CMD /LOCAL TDATA" from TEST/KEYS/SEED.REQ, and a
# copy of TEST/KEYS/COLOR.CFG or MONO.CFG (STATION=TEST), so no snapshot
# shows a path, a machine name or a date of its own:
#   INV /LOCAL TDATA /NOW:20261008120000 /CFG:COLOR.CFG /KEYS:x.KEY /SNAP:x.SNP
# INV.MSG and INV.HLP sit beside the program.  A script's comment lines
# starting "#@" set its runs:
#   #@ NOSEED          the data directory starts empty (a first run)
#   #@ DEEP            after SEED.REQ the run is seeded from DEEP.REQ too,
#                      which makes every list on the deepest paths (the
#                      location tree, the office's contents, the item list,
#                      item 1's Loans and Service tabs, item 2's Parts tab,
#                      the location pick list, the item pick list of
#                      "Deep") longer than a page, and the location tree
#                      and the item list in maker order long enough that
#                      their cursors move their rings to the larger block
#                      (LV_RINGMAX of PAGER.H), and the tags in use more
#                      than the tag pick list holds, over two ITEM TAGS
#                      pages, the second's sorting first; such a script
#                      pages each list it passes to its end and back (End,
#                      Home), so that each holds its ring of page
#                      cursors, as INV's free memory figure counts them
#                      (not with NOSEED or PRN)
#   #@ ROOMS           after SEED.REQ the run is seeded from the requests
#                      of TEST/mkrooms.sh too, LV_SEEKMAX (PAGER.H) rooms
#                      after SEED.REQ's locations, the last, FAR, holding a
#                      bin, Crate, so that the location pick list opened on
#                      Crate's room gives up its seek first (not with
#                      NOSEED, DEEP or PRN)
#   #@ SAMPLE          the run is seeded from the release's sample data,
#                      REL/SAMPLE.REQ without its comment and blank lines,
#                      in place of SEED.REQ (not with NOSEED, DEEP or
#                      ROOMS)
#   #@ EXIT n          the run must end with status n (default 0)
#   #@ CFGHAS line     the settings file must hold this line afterwards
#   #@ MONOCGA         also run with MONO.CFG (MONO=1), the TP_MONOCGA
#                      palette, against x.SNB
#   #@ BIOS, #@ SNOW   under DOSBox-X with a CGA, also run with BIOS=1,
#                      or SNOW=1, against the same x.SNC
#   #@ PRN             every run also has /PRN:file, and the print jobs it
#                      captures there must equal TEST/GOLDEN/PRT/x.PRN
#   #@ FILE NAME       the file NAME (8.3 in capitals, no directory, not
#                      a DOS device name) that the run writes in its
#                      current directory must equal TEST/GOLDEN/PRT/x.EXT,
#                      EXT being NAME's extension; at most one per script
#
# Other "#@" words are refused.  A script's file names must not be DOS
# device names (PRN.TXT would print), and the files a run leaves in its
# current directory under DOS other than that of "#@ FILE" are not
# removed, so the scripts leave none: a DOS run whose scratch directory
# T_SCR.BAT cannot remove fails.  The snapshot file, without its
# "=STACK UNUSED=n" line, must equal the golden byte for byte, as must
# the files of "#@ PRN" and "#@ FILE" on every platform (with -u the host
# runs rewrite those goldens too).  DOS runs must write that line, with n
# of at least STACK_MARGIN bytes; the host writes none; under Turbo C a
# near heap grown past its reservation into the stack makes n 0.  Only
# runs with /KEYS: fill the stack, so the stack of INV /CMD is not
# measured.  The seeding must answer every request with +OK.  Host runs
# also report goldens that no run compares (a script removed, a MONOCGA,
# PRN or FILE directive dropped), those of TEST/GOLDEN/PRT/ included but
# for the ones of T_RPT (RPT*) and T_SCRR (SCRR*).  The goldens per
# platform:
#   host          x.SNC (TP_COLOR); x.SNM with the host-only switch /MDA
#                 (TP_MONO); x.SNB for the MONOCGA scripts
#   -d cga        DOSBox-X, machine=cga, 8086: x.SNC, the BIOS and SNOW
#                 runs, x.SNB
#   -d hercules   DOSBox-X, machine=hercules: x.SNM, the palette chosen by
#                 the adapter alone
#   -d qemu       Compaq MS-DOS 3.10 under QEMU (its VGA reports an EGA):
#                 x.SNC, several runs per boot from the scratch hard disk
#                 of TEST/INTEG/boot310.sh as C:, which holds the program,
#                 the scripts, the snapshots and the data, beside a VDISK
#                 RAM disk, which DOS then names D:; with -m KB the
#                 RAM disk is sized (TEST/INTEG/free310.sh) so that INV
#                 starts with KB kilobytes of free conventional memory
#                 (less than one more), and every boot first has CHKDSK
#                 report the free memory in the same place, which must
#                 be that, so the runs check the free memory INV is
#                 documented to need; without -m it is the least RAM
#                 disk, FREE310_MINKB KB, which leaves every build more
#                 than it needs
# Under DOSBox-X, -m KB has its batch file first take memory with
# LOADFIX (TEST/INTEG/free310.sh sizes the block) so that INV starts with
# KB kilobytes free, less than one more, and has the CHKDSK of the
# MS-DOS 3.10 floppy of TEST/INTEG/boot310.sh ($DOS310_IMG, else the
# Compaq disk under $RETRO_DL/dos310/), run on a blank floppy as A:,
# report the free memory in the same place, which must be that.
# The DOS runs use TEST/T_SCR.BAT for each run; INV.MSG and INV.HLP are
# taken from EXE's directory, else from BIN/.
#
# Host mode runs HOSTBIN (default OBJ/HOST/INV.bin) with BIN/INV.MSG and
# BIN/INV.HLP.  With -u the host runs rewrite the goldens instead of
# comparing; review the difference before keeping them.  A run that
# writes a golden without -u, or none with it, fails.
#
# The scratch directory is made under /tmp, whatever TMPDIR says: INV
# accepts at most LINK_MAXDIR (LINK.H) characters for its own directory,
# where it finds INV.MSG, and the script refuses longer paths.
#
# Exits 0 when every run matches, 1 on a mismatch, 2 on a usage or setup
# error, 129, 130 or 143 on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"
. "$here/INTEG/free310.sh"

[ -f SRCS.MK ] && [ -d TEST/KEYS ] || die "run from the repository root"

REPO=$(pwd -P)
freekb=
KEYS=$REPO/TEST/KEYS
GOLD=$REPO/TEST/GOLDEN/SCR
PGOLD=$REPO/TEST/GOLDEN/PRT
NOW=/NOW:$SEED_NOW
update=0
plat=host
dosuse='-d cga|hercules|qemu [-m KB] EXE'
case ${1:-} in
-u) update=1; shift ;;
-d) [ $# -ge 3 ] || die "usage: sh TEST/T_SCR.SH $dosuse"
    plat=$2; shift 2
    case $plat in
    cga|hercules|qemu) ;;
    *) die "unknown platform: $plat" ;;
    esac
    if [ "$1" = -m ]; then
        [ $# -eq 3 ] || die "-m KB goes with one EXE"
        freekb=$2; shift 2
    fi
    [ $# -eq 1 ] || die "usage: sh TEST/T_SCR.SH $dosuse" ;;
esac
[ $# -le 1 ] || die "usage: sh TEST/T_SCR.SH [-u] [HOSTBIN] | $dosuse"

# directive SCRIPT WORD: the rest of each "#@ WORD ..." line, one per
# line.
directive() {
    awk -v w="$2" '$1 == "#@" && $2 == w {
        sub(/^#@[ \t]+[^ \t]+[ \t]*/, ""); print }' "$KEYS/$1.KEY"
}
has() { [ -n "$(awk -v w="$2" '$1 == "#@" && $2 == w' "$KEYS/$1.KEY")" ]; }
# outfile SCRIPT: the file of its "#@ FILE", or nothing.
outfile() { directive "$1" FILE; }

scripts=
for k in "$KEYS"/*.KEY; do
    case $(basename "$k") in SCRE*|SCRR*) continue ;; esac
    s=$(basename "$k" .KEY)
    bad=$(awk '$1 == "#@" &&
        $2 !~ /^(NOSEED|DEEP|ROOMS|SAMPLE|EXIT|CFGHAS|MONOCGA|BIOS|SNOW|PRN|FILE)$/ {
        print $2 }' "$k")
    [ -z "$bad" ] || die "$s.KEY: unknown directive #@ $bad"
    if has "$s" DEEP && { has "$s" NOSEED || has "$s" PRN; }; then
        die "$s.KEY: #@ DEEP goes with neither NOSEED nor PRN"
    fi
    if has "$s" ROOMS &&
        { has "$s" NOSEED || has "$s" DEEP || has "$s" PRN; }; then
        die "$s.KEY: #@ ROOMS goes with neither NOSEED, DEEP nor PRN"
    fi
    if has "$s" SAMPLE &&
        { has "$s" NOSEED || has "$s" DEEP || has "$s" ROOMS; }; then
        die "$s.KEY: #@ SAMPLE goes with neither NOSEED, DEEP nor ROOMS"
    fi
    # "#@ FILE": once, one 8.3 name with an extension, not a DOS device,
    # which DOS finds with any extension.
    if has "$s" FILE; then
        of=$(outfile "$s")
        bad=x
        case $of in ?*.?*) is_83 "$of" && bad= ;; esac
        case ${of%%.*} in
        CON|AUX|PRN|NUL|COM[1-4]|LPT[1-3]|CLOCK\$) bad=x ;;
        esac
        [ -z "$bad" ] || die "$s.KEY: #@ FILE needs one 8.3 file name" \
            "in capitals, not a device, once"
    fi
    scripts="$scripts $s"
done
[ -n "$scripts" ] || die "no key scripts in TEST/KEYS"

maxdir=$(cdefine SRC/CLIENT/LINK.H LINK_MAXDIR)
# The stack bytes a DOS run must leave unused, the margin of T_SCRE and
# T_SCRR too.
STACK_MARGIN=$(cdefine TEST/T_SCRH.C STACK_MARGIN)
scratch=$(mktemp -d /tmp/t_scr.XXXXXX)
kept=
cleanup() {
    # boot310.sh -k names the directory it kept, also when it was
    # interrupted.
    if [ -f "$scratch/boot.log" ]; then
        kept=$(sed -n 's/^boot310.sh: kept //p' "$scratch/boot.log")
    fi
    case $kept in */boot310.*) rm -rf "$kept" ;; esac
    rm -rf "$scratch"
}
trap cleanup EXIT
on_signals
sed '/^#/d;/^$/d' REL/SAMPLE.REQ >"$scratch/SAMPLE.REQ"
sh "$here/mkrooms.sh" "$(cdefine SRC/CLIENT/PAGER.H LV_SEEKMAX)" \
    >"$scratch/ROOMS.REQ"
# The longest path a run hands to INV: its program directory, the key
# script and the snapshot file (host); the DOS paths are short.
for p in "$scratch/bin/" "$scratch/keys/SETTINGS.KEY" \
    "$scratch/out/R99.SNP"; do
    [ ${#p} -le "$maxdir" ] ||
        die "scratch path $p is longer than LINK_MAXDIR ($maxdir)"
done

fails=0
check_fail() { echo "$prog: FAIL: $*" >&2; fails=$((fails + 1)); }
# The goldens written: some with -u, none without.
wrote=0
wrote_check() {
    [ $((wrote > 0)) = $update ] ||
        check_fail "wrote $wrote goldens, -u $update"
}

# The runs of this platform, one line each: id script cfgsrc cfgname
# golden-suffix.  cfgsrc is COLOR.CFG, MONO.CFG, or the variants BIOS.CFG
# and SNOW.CFG made below; the run directory always calls it COLOR.CFG or
# MONO.CFG.
printf 'STATION=TEST\r\nBIOS=1\r\n' >"$scratch/BIOS.CFG"
printf 'STATION=TEST\r\nSNOW=1\r\n' >"$scratch/SNOW.CFG"
cp "$KEYS/COLOR.CFG" "$KEYS/MONO.CFG" "$scratch/"
n=0
: >"$scratch/runs"
addrun() {
    n=$((n + 1))
    printf 'R%02d %s %s %s %s\n' $n "$1" "$2" "$3" "$4" >>"$scratch/runs"
}
for s in $scripts; do
    case $plat in
    host)
        addrun "$s" COLOR.CFG COLOR.CFG SNC
        addrun "$s" COLOR.CFG COLOR.CFG SNM
        if has "$s" MONOCGA; then addrun "$s" MONO.CFG MONO.CFG SNB; fi ;;
    cga)
        addrun "$s" COLOR.CFG COLOR.CFG SNC
        if has "$s" BIOS; then addrun "$s" BIOS.CFG COLOR.CFG SNC; fi
        if has "$s" SNOW; then addrun "$s" SNOW.CFG COLOR.CFG SNC; fi
        if has "$s" MONOCGA; then addrun "$s" MONO.CFG MONO.CFG SNB; fi ;;
    hercules)
        addrun "$s" COLOR.CFG COLOR.CFG SNM ;;
    qemu)
        addrun "$s" COLOR.CFG COLOR.CFG SNC ;;
    esac
done

# check ID SCRIPT CFGNAME SUFFIX STATUS: compare the run's snapshot file
# out/ID.SNP with the golden, and its status and settings file with the
# script's directives.
check() {
    id=$1 s=$2 cfg=$3 suf=$4 rc=$5
    what="$s.$suf ($id, $plat, $cfg)"
    want=$(directive "$s" EXIT)
    [ "$rc" = "${want:-0}" ] ||
        check_fail "$what: exit status $rc, expected ${want:-0}"
    snp=$scratch/out/$id.SNP
    [ -f "$snp" ] || { check_fail "$what: no snapshot file"; return 0; }
    stack=$(tr -d '\r' <"$snp" | sed -n 's/^=STACK UNUSED=\([0-9]*\)$/\1/p')
    if [ $plat = host ]; then
        [ -z "$stack" ] || check_fail "$what: the host wrote a =STACK line"
    elif [ -z "$stack" ]; then
        check_fail "$what: no =STACK line"
    elif [ "$stack" -lt $STACK_MARGIN ]; then
        check_fail "$what: only $stack stack bytes never used" \
            "(margin $STACK_MARGIN)"
    else
        echo "$stack" >>"$scratch/stack"
    fi
    grep -av '^=STACK UNUSED=' "$snp" >"$snp.cmp" || true
    gold=$GOLD/$s.$suf
    if [ $update = 1 ]; then
        cp "$snp.cmp" "$gold"
        wrote=$((wrote + 1))
    elif [ ! -f "$gold" ]; then
        check_fail "$what: no golden $gold"
    elif ! cmp -s "$snp.cmp" "$gold"; then
        check_fail "$what: differs from TEST/GOLDEN/SCR/$s.$suf"
        diff "$gold" "$snp.cmp" | tr -d '\r' | head -20 >&2 || true
    fi
    if has "$s" PRN; then
        cmpout "$what" "$scratch/out/$id.PRN" "$s.PRN"
    fi
    of=$(outfile "$s")
    if [ -n "$of" ]; then
        cmpout "$what" "$scratch/out/$id.OUT" "$s.${of##*.}"
    fi
    if [ $plat != host ] &&
        tr -d '\r' 2>/dev/null <"$scratch/out/RESULT.TXT" |
        awk -v i="$id" '$1 == i && $2 == "LEFT" { f = 1 }
            END { exit !f }'; then
        check_fail "$what: T_SCR.BAT could not remove its scratch directory"
    fi
    if [ $plat != host ] && ! has "$s" NOSEED; then
        sed=$scratch/out/$id.SED
        if [ ! -f "$sed" ]; then
            check_fail "$what: no seeding output"
        elif tr -d '\r' <"$sed" | grep -qv '^+OK'; then
            check_fail "$what: seeding refused:" \
                "$(tr -d '\r' <"$sed" | grep -v '^+OK' | head -3)"
        fi
    fi
    directive "$s" CFGHAS | while IFS= read -r line; do
        [ -n "$line" ] || continue
        tr -d '\r' <"$scratch/out/$id.CFG" | grep -qxF "$line" || {
            echo "$prog: FAIL: $what: settings file lacks $line" >&2
            echo x
        }
    done >"$scratch/cfgfail"
    [ -s "$scratch/cfgfail" ] && fails=$((fails + 1))
    return 0
}

# result ID: the status that T_SCR.BAT recorded in out/RESULT.TXT for the
# DOS run ID, or "none".
result() {
    tr -d '\r' 2>/dev/null <"$scratch/out/RESULT.TXT" |
        awk -v i="$1" '$1 == i && $2 != "LEFT" { st = $2 }
            END { print (st == "" ? "none" : st) }'
}

# cmpout WHAT FILE GOLDEN: compare a run's printer capture or output file
# with TEST/GOLDEN/PRT/GOLDEN, or with -u make it the golden.
cmpout() {
    if [ ! -f "$2" ]; then
        check_fail "$1: no file for TEST/GOLDEN/PRT/$3"
    elif [ $update = 1 ]; then
        cp "$2" "$PGOLD/$3"
        wrote=$((wrote + 1))
    elif [ ! -f "$PGOLD/$3" ]; then
        check_fail "$1: no golden TEST/GOLDEN/PRT/$3"
    elif ! cmp -s "$2" "$PGOLD/$3"; then
        check_fail "$1: differs from TEST/GOLDEN/PRT/$3"
        cmp -l "$PGOLD/$3" "$2" | head -10 >&2 || true
    fi
    return 0
}

mkdir "$scratch/out"

# ----------------------------------------------------------------- host
if [ $plat = host ]; then
    inv=${1:-OBJ/HOST/INV.bin}
    [ -x "$inv" ] || die "not an executable: $inv"
    for f in BIN/INV.MSG BIN/INV.HLP; do
        [ -f "$f" ] || die "missing $f (make $f)"
    done
    mkdir "$scratch/bin" "$scratch/keys"
    cp "$inv" "$scratch/bin/INV"
    cp BIN/INV.MSG BIN/INV.HLP "$scratch/bin/"
    for s in $scripts; do cp "$KEYS/$s.KEY" "$scratch/keys/"; done
    while read -r id s src cfg suf; do
        run=$scratch/run/$id
        what="$s.$suf ($id, $plat, $cfg)"
        mkdir -p "$run/TDATA"
        if ! has "$s" NOSEED; then
            seeds=$KEYS/SEED.REQ
            has "$s" DEEP && seeds="$seeds $KEYS/DEEP.REQ"
            has "$s" ROOMS && seeds="$seeds $scratch/ROOMS.REQ"
            has "$s" SAMPLE && seeds=$scratch/SAMPLE.REQ
            seeded=1
            for f in $seeds; do
                dos_text "$f" | (cd "$run" &&
                    "$scratch/bin/INV" /CMD /LOCAL TDATA "$NOW") \
                    >>"$run/seed.out" 2>&1 || { seeded=0; break; }
            done
            if [ $seeded = 0 ]; then
                check_fail "$what: seeding failed:" "$(cat "$run/seed.out")"
                continue
            fi
            if grep -q '^-ERR' "$run/seed.out"; then
                check_fail "$what: seeding refused:" \
                    "$(grep '^-ERR' "$run/seed.out" | head -3)"
                continue
            fi
        fi
        cp "$scratch/$src" "$run/$cfg"
        mda=
        [ "$suf" = SNM ] && mda=/MDA
        prn=
        has "$s" PRN && prn=/PRN:$scratch/out/$id.PRN
        rc=0
        # shellcheck disable=SC2086
        (cd "$run" && "$scratch/bin/INV" /LOCAL TDATA "$NOW" "/CFG:$cfg" \
            "/KEYS:$scratch/keys/$s.KEY" "/SNAP:$scratch/out/$id.SNP" \
            $mda $prn </dev/null >"$run/out" 2>"$run/err") || rc=$?
        of=$(outfile "$s")
        if [ -n "$of" ] && [ -f "$run/$of" ]; then
            cp "$run/$of" "$scratch/out/$id.OUT"
        fi
        [ -s "$run/err" ] &&
            check_fail "$s.$suf: standard error: $(cat "$run/err")"
        [ -s "$run/out" ] &&
            check_fail "$s.$suf: standard output: $(cat "$run/out")"
        cp "$run/$cfg" "$scratch/out/$id.CFG"
        check "$id" "$s" "$cfg" "$suf" "$rc"
    done <"$scratch/runs"
    # Goldens no run compared.
    for g in "$GOLD"/*.SN?; do
        b=$(basename "$g")
        case $b in SCRE*|SCRR*) continue ;; esac
        grep -q " ${b%.*} [^ ]* [^ ]* ${b##*.}\$" "$scratch/runs" ||
            check_fail "TEST/GOLDEN/SCR/$b: no run compares it"
    done
    for g in "$PGOLD"/*; do
        b=$(basename "$g")
        case $b in RPT*|SCRR*) continue ;; esac
        found=
        for s in $scripts; do
            of=$(outfile "$s")
            if { has "$s" PRN && [ "$b" = "$s.PRN" ]; } ||
               { [ -n "$of" ] && [ "$b" = "$s.${of##*.}" ]; }; then
                found=1
            fi
        done
        [ -n "$found" ] || check_fail "TEST/GOLDEN/PRT/$b: no run compares it"
    done
    wrote_check
    [ $update = 1 ] && echo "$prog: rewrote the goldens in TEST/GOLDEN/SCR"
    [ $fails = 0 ] || fail "$fails failures"
    echo "$prog: host ok ($n runs)"
    exit 0
fi

# ------------------------------------------------------------------ DOS
[ $update = 0 ] || die "-u works on the host only"
exe=$1
[ -f "$exe" ] || die "no such file: $exe"
dosf=$scratch/dosf
mkdir "$dosf"
cp "$exe" "$dosf/INV.EXE"
for f in INV.MSG INV.HLP; do
    src=$(dirname "$exe")/$f
    [ -f "$src" ] || src=BIN/$f
    [ -f "$src" ] || die "no $f beside $exe or in BIN/"
    cp "$src" "$dosf/$f"
done
dos_text TEST/T_SCR.BAT >"$dosf/T_SCR.BAT"
dos_text "$KEYS/SEED.REQ" >"$dosf/SEED.REQ"
dos_text "$KEYS/DEEP.REQ" >"$dosf/DEEP.REQ"
dos_text "$scratch/SAMPLE.REQ" >"$dosf/SAMPLE.REQ"
dos_text "$scratch/ROOMS.REQ" >"$dosf/ROOMS.REQ"
for c in COLOR MONO BIOS SNOW; do cp "$scratch/$c.CFG" "$dosf/"; done
for s in $scripts; do dos_text "$KEYS/$s.KEY" >"$dosf/$s.KEY"; done

# The seed argument of T_SCR.BAT: S, D for "#@ DEEP", M for "#@ ROOMS",
# R for "#@ SAMPLE" or N, with P added for "#@ PRN".
seedflag() {
    if has "$1" NOSEED; then f=N; elif has "$1" DEEP; then f=D
    elif has "$1" ROOMS; then f=M
    elif has "$1" SAMPLE; then f=R; else f=S; fi
    if has "$1" PRN; then f=${f}P; fi
    echo $f
}

if [ $plat = qemu ]; then
    # Each boot runs scripts until about BOOT_SNAPS snapshots are due, which
    # keeps a boot well inside its timeout and limits what one hung run
    # costs.  The program, the scripts, the results and each run's
    # scratch directory are on C:, the hard disk of boot310.sh: the VDISK
    # RAM disk, D:, only takes memory, and the data of "#@ DEEP" would not
    # fit it when INV is left much.  The boot's command lines first make
    # C: the current drive, so COMMAND.COM is named by its path on A:.
    BOOT_SNAPS=16
    export MTOOLS_SKIP_CHECK=1
    vdisk=$FREE310_MINKB
    if [ -n "$freekb" ]; then
        vdisk=$(free310_vdisk "$freekb") ||
            die "no RAM disk leaves $freekb KB free"
        # FREE.BAT runs CHKDSK where T_SCR.BAT runs INV.
        printf 'ECHO OFF\r\nA:\\CHKDSK D: >C:\\FREE.TXT\r\n' >"$dosf/FREE.BAT"
    fi
    free310_config "$dosf/CONFIG.SYS" "$vdisk"
    # boot: one boot running the runs in $bootids.
    boot() {
        echo C: >"$scratch/boot.cmds"
        : >"$scratch/boot.files"
        if [ -n "$freekb" ]; then
            printf '%s\n' 'A:\COMMAND /C C:\FREE' >>"$scratch/boot.cmds"
            echo "$dosf/FREE.BAT" >>"$scratch/boot.files"
        fi
        for id in $bootids; do
            set -- $(awk -v i="$id" '$1 == i' "$scratch/runs")
            printf '%s\n%s\n' "$dosf/$3" "$dosf/$2.KEY" >>"$scratch/boot.files"
            printf 'A:\\COMMAND /C T_SCR %s C:\\%s.KEY C:\\%s %s %s' \
                "$1" "$2" "$3" "$4" "$(seedflag "$2")" >>"$scratch/boot.cmds"
            printf ' C: C: %s C: %s\n' "$NOW" "$(outfile "$2")" \
                >>"$scratch/boot.cmds"
        done
        set -- -f "$dosf/CONFIG.SYS"
        for f in INV.EXE INV.MSG INV.HLP T_SCR.BAT SEED.REQ DEEP.REQ \
            ROOMS.REQ SAMPLE.REQ; do
            set -- "$@" -h "$dosf/$f"
        done
        for f in $(sort -u "$scratch/boot.files"); do set -- "$@" -h "$f"; done
        while IFS= read -r l; do set -- "$@" "$l"; done <"$scratch/boot.cmds"
        rc=0
        sh TEST/INTEG/boot310.sh -t 900 -k "$@" \
            </dev/null >"$scratch/boot.log" 2>&1 || rc=$?
        kept=$(sed -n 's/^boot310.sh: kept //p' "$scratch/boot.log")
        hd=$(sed -n 's/^boot310.sh: hard disk partition //p' \
            "$scratch/boot.log")
        ids=$(printf ' %s' $bootids)
        if [ -n "$hd" ]; then
            # A missing RESULT.TXT fails the boot below; a run's missing
            # files fail that run's check.
            mcopy -n -i "$hd" ::RESULT.TXT "$scratch/out/RESULT.TXT" || true
            if [ -n "$freekb" ]; then
                rm -f "$scratch/FREE.TXT"
                mcopy -n -i "$hd" ::FREE.TXT "$scratch/FREE.TXT" \
                    2>/dev/null || true
                if why=$(free310_check "$scratch/FREE.TXT" "$freekb"); then
                    echo "$prog: MS-DOS 3.10 boot for$ids: free:" \
                        "$(free310_bytes "$scratch/FREE.TXT") bytes"
                else
                    check_fail "MS-DOS 3.10 boot for$ids: $why"
                fi
            fi
            for id in $bootids; do
                for f in "$id.SNP" "$id.CFG" "$id.SED" "$id.PRN" "$id.OUT"; do
                    mcopy -n -i "$hd" "::$f" "$scratch/out/$f" \
                        2>/dev/null || true
                done
            done
        fi
        case $kept in */boot310.*) rm -rf "$kept" ;; esac
        # boot310.sh passes on the status of the last program, which the
        # results file records anyway; only its own failures count here.
        if [ "$rc" -ge 200 ] || [ ! -f "$scratch/out/RESULT.TXT" ]; then
            check_fail "MS-DOS 3.10 boot for$ids: status $rc"
            cat "$scratch/boot.log" >&2
        fi
        for id in $bootids; do
            set -- $(awk -v i="$id" '$1 == i' "$scratch/runs")
            check "$1" "$2" "$4" "$5" "$(result "$1")"
        done
        rm -f "$scratch/out/RESULT.TXT"
    }
    bootids=
    snaps=0
    # check() sets id and s, so this loop reads into names of its own.
    while read -r rid rs _ _ _; do
        k=$(awk 'toupper($1) == "S" { n++ } END { print n + 0 }' \
            "$KEYS/$rs.KEY")
        if [ -n "$bootids" ] && [ $((snaps + k)) -gt $BOOT_SNAPS ]; then
            boot
            bootids=
            snaps=0
        fi
        bootids="$bootids $rid"
        snaps=$((snaps + k))
    done <"$scratch/runs"
    [ -z "$bootids" ] || boot
else
    # One DOSBox-X instance runs every run of this platform from a batch
    # file of COMMAND /C T_SCR lines: C: holds the program and scripts,
    # D: the scratch directories and results; with -m, A: is a blank
    # floppy for the CHKDSK of the MS-DOS 3.10 floppy, which FREE.BAT
    # runs where T_SCR.BAT runs INV.
    afloppy=
    : >"$dosf/RUNS.BAT"
    if [ -n "$freekb" ]; then
        lf=$(free310_vdisk "$freekb" "$FREEDBX_BASE" 1) ||
            die "no LOADFIX block leaves $freekb KB free"
        # A blank floppy: DOSBox-X's image of the boot floppy shows
        # CHKDSK allocation errors, and CHKDSK then waits for a key.
        dd if=/dev/zero of="$scratch/A.IMG" bs=1024 count=360 2>/dev/null &&
            MTOOLS_SKIP_CHECK=1 mformat -i "$scratch/A.IMG" -f 360 :: &&
            MTOOLS_SKIP_CHECK=1 mcopy -n -i "$DOS310_FLOPPY" \
                ::CHKDSK.COM "$dosf/CHKDSK.COM" ||
            die "no CHKDSK.COM from the MS-DOS 3.10 floppy"
        afloppy="imgmount a \"$scratch/A.IMG\" -t floppy"
        printf 'ECHO OFF\r\nC:\\CHKDSK A: >D:\\FREE.TXT\r\n' >"$dosf/FREE.BAT"
        printf 'LOADFIX -%s >NUL\r\nCOMMAND /C C:\\FREE\r\n' "$lf" \
            >"$dosf/RUNS.BAT"
    fi
    while read -r id s src cfg suf; do
        printf 'COMMAND /C C:\\T_SCR %s C:\\%s.KEY C:\\%s %s %s' \
            "$id" "$s" "$src" "$cfg" "$(seedflag "$s")"
        printf ' D: D: %s C: %s\r\n' "$NOW" "$(outfile "$s")"
    done <"$scratch/runs" >>"$dosf/RUNS.BAT"
    printf 'EXIT\r\n' >>"$dosf/RUNS.BAT"
    cat >"$scratch/DOSBOX.CONF" <<EOF
[sdl]
output=surface
[log]
logfile=
[dosbox]
machine=$plat
memsize=1
quit warning=false
[cpu]
cputype=8086
core=normal
cycles=max
[dos]
ver=3.10
lfn=false
[mixer]
nosound=true
[speaker]
pcspeaker=false
[autoexec]
$afloppy
mount c "$dosf" -nocachedir
mount d "$scratch/out" -nocachedir
c:
C:\\RUNS.BAT
EOF
    # 900 s for the instance, times DOSTIME_SCALE (dos_secs).
    lim=$(dos_secs 900)
    (cd "$scratch" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout -k 5 "$lim" dosbox-x -conf DOSBOX.CONF -nogui -nomenu \
        -nopromptfolder >DOSBOX.LOG 2>&1) || true
    if [ ! -f "$scratch/out/RESULT.TXT" ]; then
        tail -20 "$scratch/DOSBOX.LOG" >&2
        fail "DOSBox-X wrote no results"
    fi
    if [ -n "$freekb" ]; then
        if why=$(free310_check "$scratch/out/FREE.TXT" "$freekb"); then
            echo "$prog: DOSBox-X: free:" \
                "$(free310_bytes "$scratch/out/FREE.TXT") bytes"
        else
            check_fail "DOSBox-X: $why"
        fi
    fi
    while read -r id s src cfg suf; do
        check "$id" "$s" "$cfg" "$suf" "$(result "$id")"
    done <"$scratch/runs"
fi
wrote_check
if [ -f "$scratch/stack" ]; then
    echo "$prog: $plat: stack never used:" \
        "$(sort -n "$scratch/stack" | head -1) bytes at least"
fi
[ $fails = 0 ] || fail "$plat: $fails failures"
echo "$prog: $plat ok ($n runs)"
