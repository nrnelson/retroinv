#!/bin/sh
# distchk.sh - check the release images and zip that REL/mkdist.sh made:
# that each image holds exactly the files staged for it, compressed as
# its set's list says, and the zip those of the 1.44 MB disk as they
# are, and that INSTALL installs from each set's images under DOS and
# the installed programs run.
#
# usage: sh REL/distchk.sh WORK OUT HOSTINV
#
#   WORK     the scratch directory mkdist.sh was given; its staged disks
#            are compared and its sample data gives the expected answers.
#            The check's own files go to WORK/CHK/.
#   OUT      the directory of the images and the zip.
#   HOSTINV  INV built for the host (OBJ/HOST/INV.bin).
#
# Run from the repository root.  First every image is read back with
# mtools, in UTC so that its files keep the times mkdist.sh gave them,
# and must equal its staged directory WORK/L, file for file and byte for
# byte.  Then REL/diskzip.py --check holds the zip to each promise of its
# format against the 1.44 MB disk's files as they are, WORK/PLAIN/L
# (order, folder, compression and its bytes, system, attributes, dates,
# no extra field or comment), and its entries, extracted with their CRCs
# checked, must be one folder named as that disk's label, equal to those
# files.  Every disk must hold INSTALL.EXE, INSTALL.LST, README.TXT,
# LICENSE.TXT and WATTCP.TXT in its root as they are, its other files
# one for one with those it holds as they are, each that its list marks
# Z compressed under its COMPRESS -r name, the sample data in DATA._,
# and msexpand of the mscompress package, a reader of the format apart
# from ours, must give each back; the disk with SETUP.EXE every file
# SETUP copies, INVWIN.EXE, INV.MSG, INVWIN.HLP and INVWIN.HTX.  The
# disks of each size that hold INV.EXE must hold the sample data, and
# those of each size but 360 KB INVWIN.EXE, the Windows client.  Every
# file a disk takes from the build, as DISKS.TXT gives them, must be the
# build's bytes, and every disk's INSTALL.LST within the limits of
# SRC/TOOLS/INSTPLAN.H.
#
# The expected answers come from the host build.  HOSTINV /CMD answers
# the requests of REL/CHECK.REQ on a copy of the sample data; they must
# hold no error and the password change ADMIN owes.  HOSTINV runs the key
# script REL/HELP.KEY on the login screen from a directory holding an
# INV.MSG and an INV.HLP of the disks; its snapshot must show the help,
# the title of the @LOGIN section of SRC/CLIENT/INV.HLP.
# Every DOS run must give the same, the line ends aside (boot310.sh drops
# the CRs).
#
# Then INSTALL installs from each set, in one boot of
# TEST/INTEG/boot310.sh (FILES=45) per set, all at once, the set's first
# disk as the second floppy, B:, a hard disk as C:, which the boot keeps
# (-k) for its files to be compared with what the installations must
# leave there; a boot whose set has two disks has INSTALL's questions
# answered on the keyboard by TEST/INTEG/dialog.py, which changes B: to
# each disk INSTALL asks for, the others from files:
#   INST360, MS-DOS 3.10:  the server with the sample data in C:\RETROINV
#                          from the 360 KB disk 1, the question for disk
#                          2 answered first with disk 1 still in B:,
#                          which INSTALL must refuse by its label; then
#                          the network-only client in C:\THIN from disk
#                          2, which asks for disk 1
#   INST720, MS-DOS 3.30:  the same from the 720 KB disks, the server
#                          with the Windows client
#   INST120, MS-DOS 3.30:  the same from the 1.2 MB disk
#   INST144, MS-DOS 3.30:  from the 1.44 MB disk, the network-only client
#                          with the Windows client in C:\THIN, a server
#                          with the sample data in C:\SRV, then again
#                          over an edited INVSERV.CFG, which must stay,
#                          one PC alone with an empty inventory in
#                          C:\ONE, installed by C:\SRV\INSTALL from that
#                          directory as from the zip's folder, then
#                          INSTALL /U of C:\ONE, its data deleted, and of
#                          C:\SRV, its data kept
# Each run of INSTALL must exit with 0.  After the installations the
# installed programs run from C:\RUN, which holds a copy of the server's
# DATA\ and INVSERV.CFG: INV /KEYS:HELP.KEY, its snapshot then typed into
# the output, where the line of unused stack, which only a DOS build
# writes, is not compared; INV /CMD /LOCAL DATA <A:\CHECK.REQ; INVTCP /?,
# COMTEST /?, DBPACK, INVDUMP and the network-only client's /?, each of
# which must print its usage; and but in INST144 INVSERV last, which must
# exit with INVSERV_NONBRC of SRCS.MK, as no NetBIOS provider is loaded
# (in INST144 the last INSTALL /U's 0).  Each boot's output is kept in
# WORK/CHK/<boot>.OUT; the hard disk, C:\RUN and CHECK.REQ aside, must
# hold the files of the installations' roles, each as its disk's
# INSTALL.LST names it, from the first disk of the set holding it (MS-DOS
# 3.10 reads no 720 KB or 1.44 MB disk, nor a 1.2 MB one in QEMU).  One
# line per boot, and one for the zip, says passed or FAILED.
#
# Last, the images and the zip must be byte-stable, as mkdist.sh
# promises: it runs once before the boots and once after them, a minute
# or more later, into WORK/CHK/MK1 and MK2, each mark of the staged REL/
# files set to 0 (the marks only fill in text), and the two must write
# the same SHA256.TXT.
#
# Exit status: 0 every check passed; 1 a check failed; 2 a usage or setup
# error.
set -eu

. TEST/shlib.sh
. TEST/INTEG/free310.sh

[ $# -eq 3 ] || die "usage: sh REL/distchk.sh WORK OUT HOSTINV"
[ -f SRCS.MK ] && [ -f REL/DISKS.TXT ] ||
    die "run from the repository root"
work=$1
out=$2
[ -x "$3" ] || die "no host INV: $3"
hostinv=$(cd "$(dirname "$3")" && pwd)/$(basename "$3")
[ -d "$work/DATA" ] || die "no sample data in $work (run mkdist.sh first)"
command -v msexpand >/dev/null || die "msexpand not installed (mscompress)"
vars=$(srcs_vars SRCS.MK)
nonbrc=$(getvar INVSERV_NONBRC)
[ -n "$nonbrc" ] || die "INVSERV_NONBRC not found in SRCS.MK"
thin=$(getvar THIN)
[ -n "$thin" ] || die "THIN not found in SRCS.MK"

chk=$work/CHK
rm -rf "$chk"
mkdir -p "$chk"
bad=0

# The disks of DISKS.TXT, "L KB IMAGE LABEL SERIAL", and image_of L, the
# image of disk L; the files, "DISKS ROLES PATH SOURCE".
disks=$(sed -n 's/^=//p' REL/DISKS.TXT)
image_of() {
    printf '%s\n' "$disks" | awk -v l="$1" '$1 == l { print $3 }'
}
files=$(sed '/^#/d;/^=/d;/^$/d;s/@THIN@/'"$thin"'/g' REL/DISKS.TXT)

# Each image read back against its staged directory.
for l in $(printf '%s\n' "$disks" | awk '{ print $1 }'); do
    image=$(image_of "$l")
    mkdir "$chk/$l"
    TZ=UTC MTOOLS_SKIP_CHECK=1 mcopy -s -n -m -i "$out/$image" '::*' \
        "$chk/$l/" ||
        die "cannot read $image"
    if ! diff -r "$work/$l" "$chk/$l" >"$chk/$l.DIF"; then
        echo "FAILED: $image: not the files staged for it ($chk/$l.DIF)"
        bad=1
    fi
done

# The zip read back against the 1.44 MB disk's files as they are.
read -r zl zname <<EOF2
$(printf '%s\n' "$disks" |
    awk '$2 == 1440 { n++; d = $1 " " $4 } END { if (n == 1) print d }')
EOF2
[ -n "$zname" ] || die "REL/DISKS.TXT needs exactly one 1440 KB disk"
zip=$zname.ZIP
if ! python3 -I REL/diskzip.py --check "$out/$zip" "$chk/ZIP" \
        "$work/PLAIN/$zl" "$zname" >"$chk/ZIP.DIF" 2>&1; then
    echo "FAILED: $zip: $(head -1 "$chk/ZIP.DIF") ($chk/ZIP.DIF)"
    bad=1
elif ! diff -r "$work/PLAIN/$zl" "$chk/ZIP/$zname" >"$chk/ZIP.DIF" 2>&1
then
    echo "FAILED: $zip: not the files of $(image_of "$zl") ($chk/ZIP.DIF)"
    bad=1
else
    echo "passed: $zip"
fi

# no L WHAT: report that disk L lacks WHAT.
no() {
    echo "FAILED: $(image_of "$1"): $2"
    bad=1
}

# lzname PATH: as in REL/mkdist.sh.
lzname() {
    case ${1##*/} in
    *.???) echo "${1%?}_" ;;
    *.*) echo "${1}_" ;;
    *) echo "$1._" ;;
    esac
}

# expands L Z PATH: the compressed file Z of disk L expands to PATH of
# its files as they are.
expands() {
    msexpand <"$work/$1/$2" 2>/dev/null | cmp -s - "$work/PLAIN/$1/$3" ||
        no "$1" "$2 does not expand to $3"
}

# Each disk's files: the plain ones in its root, the others compressed,
# one for one with the files as they are, and SETUP's beside it.
for l in $(printf '%s\n' "$disks" | awk '{ print $1 }'); do
    for f in INSTALL.EXE INSTALL.LST README.TXT LICENSE.TXT WATTCP.TXT; do
        cmp -s "$work/$l/$f" "$work/PLAIN/$l/$f" || no "$l" "no plain $f"
    done
    [ "$(find "$work/$l" -type f | wc -l)" = \
      "$(find "$work/PLAIN/$l" -type f | wc -l)" ] ||
        no "$l" "not one file for each of its files as they are"
    while read -r on roles path src; do
        case $on in *$l*) ;; *) continue ;; esac
        case $roles in
        *Z*) z=$(lzname "$path")
             if [ "$src" = SAMPLE ]; then
                 for f in $(cd "$work/PLAIN/$l/$path" && ls); do
                     expands "$l" "$z/$f" "$path/$f"
                 done
             else
                 expands "$l" "$z" "$path"
             fi ;;
        *)   cmp -s "$work/$l/$path" "$work/PLAIN/$l/$path" ||
                 no "$l" "no plain $path" ;;
        esac
    done <<EOF2
$files
EOF2
    if [ -e "$work/PLAIN/$l/SETUP.EXE" ]; then
        for f in INVWIN.EXE INV.MSG INVWIN.HLP INVWIN.HTX; do
            [ -e "$work/PLAIN/$l/$f" ] || no "$l" "no $f beside SETUP.EXE"
        done
    fi
done

# The sample data on a disk of each size whose disks hold INV.EXE, and
# the Windows client on a disk of each size but 360 KB.
for kb in $(printf '%s\n' "$disks" | awk '{ print $2 }' | sort -u); do
    inv=''
    data=''
    win=''
    for l in $(printf '%s\n' "$disks" |
            awk -v k="$kb" '$2 == k { print $1 }'); do
        [ ! -e "$work/PLAIN/$l/INV.EXE" ] || inv=1
        [ ! -e "$work/PLAIN/$l/DATA/ITEMS.DBF" ] || data=1
        [ ! -e "$work/PLAIN/$l/INVWIN.EXE" ] || win=1
    done
    [ -z "$inv" ] || [ -n "$data" ] ||
        { echo "FAILED: no $kb KB disk holds the sample data"; bad=1; }
    [ "$kb" = 360 ] || [ -n "$win" ] ||
        { echo "FAILED: no $kb KB disk holds INVWIN.EXE"; bad=1; }
done

# The build's files, as DISKS.TXT puts each on its disks.
while read -r on roles path src; do
    case $src in SAMPLE|LIST|LICENSE|REL/*) continue ;; esac
    for l in $(echo "$on" | sed 's/./& /g'); do
        cmp -s "$src" "$work/PLAIN/$l/$path" || no "$l" "$path is not $src"
    done
done <<EOF2
$files
EOF2

# The expected answers and snapshot.
sed '/^#/d;/^$/d' REL/CHECK.REQ >"$chk/CHECK.REQ"
cp -r "$work/DATA" "$chk/DATA"
(cd "$chk" && "$hostinv" /CMD /LOCAL DATA <CHECK.REQ >EXPECT.CR) ||
    fail "the host INV failed on the sample data"
tr -d '\r' <"$chk/EXPECT.CR" >"$chk/EXPECT.OUT"
err=$(grep '^-ERR' "$chk/EXPECT.OUT" | head -1)
[ -z "$err" ] || fail "the sample data answers an error: $err"
grep -q '^+OK LEVEL=3 MUSTCHG=1$' "$chk/EXPECT.OUT" ||
    fail "ADMIN of the sample data owes no password change"
mkdir "$chk/HOST"
cp "$hostinv" "$chk/HOST/INV.bin"
for f in INV.MSG INV.HLP; do
    src=$(ls "$work"/PLAIN/*/$f 2>/dev/null | head -1)
    [ -n "$src" ] || fail "no disk holds $f"
    cp "$src" "$chk/HOST/"
done
cp REL/HELP.KEY "$chk/HOST/"
cp -r "$work/DATA" "$chk/HOST/DATA"
(cd "$chk/HOST" && ./INV.bin /LOCAL DATA /KEYS:HELP.KEY /SNAP:HELP.SNP) ||
    fail "the host INV failed on the key script"
tr -d '\r' <"$chk/HOST/HELP.SNP" >"$chk/HELP.OUT"
title=$(sed -n '/^@LOGIN$/{n;p;q}' SRC/CLIENT/INV.HLP)
[ -n "$title" ] || die "no @LOGIN section in SRC/CLIENT/INV.HLP"
grep -qF "$title" "$chk/HELP.OUT" ||
    fail "the disks' INV.HLP shows no login help on the host"
cat "$chk/HELP.OUT" "$chk/EXPECT.OUT" >"$chk/WANT.OUT"
free310_config "$chk/CONFIG.SYS"

# mkdist N: run mkdist.sh into WORK/CHK/MKN, the images into
# WORK/CHK/MKN/OUT, its output into WORK/CHK/MKN.LOG.
# shellcheck disable=SC2046
marks=$(grep -oh '@[A-Z][A-Z_0-9]*@' $(sed '/^#/d;/^=/d' REL/DISKS.TXT |
    awk '$4 ~ /^REL\// { print $4 }' | sort -u) | sort -u |
    sed -n '/^@VERSION@$/d;/^@THIN@$/d;s/^@\(.*\)@$/\1=0/p')
mkdist() {
    # shellcheck disable=SC2086
    sh REL/mkdist.sh "$chk/MK$1" "$chk/MK$1/OUT" "$hostinv" $marks \
        >"$chk/MK$1.LOG" 2>&1 ||
        fail "mkdist.sh failed (see $chk/MK$1.LOG)"
}
mkdist 1

# ---------------------------------------------------------------------
# INSTALL.
# ---------------------------------------------------------------------

# Every disk's list within the limits of SRC/TOOLS/INSTPLAN.H.
ins_const() {
    sed -n "s/^#define $1  *\([0-9][0-9]*\).*/\1/p" SRC/TOOLS/INSTPLAN.H
}
maxfiles=$(ins_const INS_MAXFILES)
maxdisks=$(ins_const INS_MAXDISKS)
listmax=$(ins_const INS_LISTMAX)
[ -n "$maxfiles" ] && [ -n "$maxdisks" ] && [ -n "$listmax" ] ||
    die "the limits of INSTALL's list not found in SRC/TOOLS/INSTPLAN.H"
for l in $(printf '%s\n' "$disks" | awk '{ print $1 }'); do
    lst=$work/PLAIN/$l/INSTALL.LST
    if [ ! -f "$lst" ] || [ $(($(wc -l <"$lst") - 3)) -gt "$maxfiles" ] ||
            [ $(($(sed -n 2p "$lst" | wc -w) - 1)) -gt "$maxdisks" ] ||
            [ "$(wc -c <"$lst")" -ge "$listmax" ]; then
        no "$l" "INSTALL.LST missing or too large"
    fi
done

# want L DIR ROLES: put into DIR what an installation of the roles
# ROLES (letters) from disk L copies: each file of disk L's list whose
# roles hold one of them, as it is, from the first disk of the set that
# holds it, and the list itself from disk L.
want() {
    w_kb=$(printf '%s\n' "$disks" | awk -v l="$1" '$1 == l { print $2 }')
    w_set=$(printf '%s\n' "$disks" |
        awk -v k="$w_kb" '$2 == k { printf "%s", $1 }')
    mkdir -p "$2"
    sed -n '4,$p' "$work/PLAIN/$1/INSTALL.LST" | tr -d '\r' |
    while read -r w_roles w_nums w_size w_path; do
        case $w_roles in *["$3"]*) ;; *) continue ;; esac
        w_d=$(printf '%s' "$w_set" |
            cut -c"$(printf '%s' "$w_nums" | cut -c1)")
        [ "$w_path" != INSTALL.LST ] || w_d=$1
        cp -r "$work/PLAIN/$w_d/$w_path" "$2/"
    done
}

# label_of L: the volume label of disk L.
label_of() {
    printf '%s\n' "$disks" | awk -v l="$1" '$1 == l { print $4 }'
}

# runs DIR [INVSERV]: the command lines, one a line, that run the
# programs installed in C:\DIR and C:\THIN from C:\RUN, which gets a
# copy of DIR's DATA\ and, with INVSERV, its INVSERV.CFG for INVSERV,
# run last; the output of the runs follows a line RUNS.
runs() {
    echo 'MD C:\RUN'
    echo 'MD C:\RUN\DATA'
    echo "COPY C:\\$1\\DATA\\*.* C:\\RUN\\DATA"
    [ -z "${2:-}" ] || echo "COPY C:\\$1\\INVSERV.CFG C:\\RUN"
    echo 'C:'
    echo 'CD \RUN'
    echo 'ECHO RUNS'
    echo "C:\\$1\\INV /LOCAL DATA /KEYS:A:\\HELP.KEY /SNAP:HELP.SNP"
    echo 'TYPE HELP.SNP'
    echo "C:\\$1\\INV /CMD /LOCAL DATA <A:\\CHECK.REQ"
    for p in INVTCP COMTEST; do echo "C:\\$1\\$p /?"; done
    for p in DBPACK INVDUMP; do echo "C:\\$1\\$p"; done
    echo "C:\\THIN\\$thin /?"
    [ -z "${2:-}" ] || echo "C:\\$1\\INVSERV"
}
usage="INVTCP COMTEST DBPACK INVDUMP $thin"

# dialog FIRST SECOND: the dialogue of a two-disk set whose disks are
# FIRST and SECOND: from FIRST in B:, the server with the sample data,
# and with the Windows client where the set has it, its second disk
# asked for and FIRST refused first; then from SECOND, the network-only
# client in C:\THIN, which asks for FIRST.
dialog() {
    d_win=$(printf '%s\n' "$files" |
        awk -v s="$1$2" '$2 ~ /W/ && $3 ~ /\.EXE$/ && $1 ~ "[" s "]"')
    echo 'expect Your choice [1]:'
    echo 'send 2'
    if [ -n "$d_win" ]; then
        echo 'expect INVWIN? (Y/N) [N]:'
        echo 'send Y'
    fi
    echo 'expect [C:\RETROINV]:'
    echo 'send'
    echo 'expect an empty inventory)?'
    echo 'send'
    echo "expect Insert the disk labelled $(label_of "$2")"
    echo 'send'
    echo "expect It is labelled $(label_of "$1")."
    echo "expect Insert the disk labelled $(label_of "$2")"
    echo "change $out/$(image_of "$2")"
    echo 'send'
    echo 'expect RETROINV is installed'
    echo 'expect Your choice [1]:'
    echo 'send 4'
    if [ -n "$d_win" ]; then
        echo 'expect INVWIN? (Y/N) [N]:'
        echo 'send'
    fi
    echo 'expect [C:\RETROINV]:'
    echo 'send C:\THIN'
    echo "expect Insert the disk labelled $(label_of "$1")"
    echo "change $out/$(image_of "$1")"
    echo 'send'
    echo 'expect RETROINV is installed'
}

# boot NAME DOS L OPTS LINES: in the background, boot MS-DOS DOS (310 or
# 330) with disk L as B:, a hard disk as C: that the boot keeps, and the
# further options OPTS of boot310.sh (a word list), running the command
# lines of the text LINES, one a line; the output goes to
# WORK/CHK/NAME.OUT, the exit status to WORK/CHK/NAME.RC.
boot() {
    b_name=$1
    if [ "$2" = 310 ]; then b_dos=$DOS310_FLOPPY; else b_dos=$DOS330_FLOPPY; fi
    b_image=$out/$(image_of "$3")
    b_opts=$4
    b_lines=$5
    set --
    while IFS= read -r b_c; do
        set -- "$@" "$b_c"
    done <<EOF2
$b_lines
EOF2
    # shellcheck disable=SC2086
    (rc=0
     sh TEST/INTEG/boot310.sh -t 600 -k -i "$b_dos" -B "$b_image" $b_opts \
         -h "$chk/CHECK.REQ" -f "$chk/CONFIG.SYS" -f "$chk/CHECK.REQ" \
         -f REL/HELP.KEY B: "$@" >"$chk/$b_name.OUT" 2>&1 || rc=$?
     echo $rc >"$chk/$b_name.RC") &
}

# The installations, each set's in one boot, all at once.
inst=$(printf '%s\n' '!INSTALL <CON' 'IF ERRORLEVEL 1 ECHO FAILED' \
    '!INSTALL <CON' 'IF ERRORLEVEL 1 ECHO FAILED')
dialog A B >"$chk/I360.DLG"
boot INST360 310 A "-D $chk/I360.DLG" "$inst
$(runs RETROINV 1)"
dialog M N >"$chk/I720.DLG"
boot INST720 330 M "-D $chk/I720.DLG" "$inst
$(runs RETROINV 1)"
mkdir -p "$chk/ANS"
printf '2\nY\n\n\n' | dos_text >"$chk/ANS/ANSV.TXT"
printf '4\nN\nC:\\THIN\n' | dos_text >"$chk/ANS/ANST.TXT"
boot INST120 330 K "-f $chk/ANS/ANSV.TXT -f $chk/ANS/ANST.TXT" \
    "INSTALL <A:\\ANSV.TXT
IF ERRORLEVEL 1 ECHO FAILED
INSTALL <A:\\ANST.TXT
IF ERRORLEVEL 1 ECHO FAILED
$(runs RETROINV 1)"
printf '4\nY\nC:\\THIN\n' | dos_text >"$chk/ANS/ANS1.TXT"
printf '2\nN\nC:\\SRV\n\n' | dos_text >"$chk/ANS/ANS2.TXT"
printf '2\nN\nC:\\SRV\nN\nN\n' | dos_text >"$chk/ANS/ANS3.TXT"
printf '1\nN\nC:\\ONE\nN\n' | dos_text >"$chk/ANS/ANS4.TXT"
printf 'Y\nY\n' | dos_text >"$chk/ANS/ANS5.TXT"
printf '\nN\n' | dos_text >"$chk/ANS/ANS6.TXT"
echo EDITED | dos_text >"$chk/ANS/INVSERV.CFG"
boot INST144 330 H "$(for n in 1 2 3 4 5 6; do
        printf ' -f %s' "$chk/ANS/ANS$n.TXT"; done) -f $chk/ANS/INVSERV.CFG" \
    "INSTALL <A:\\ANS1.TXT
IF ERRORLEVEL 1 ECHO FAILED: 1
INSTALL <A:\\ANS2.TXT
IF ERRORLEVEL 1 ECHO FAILED: 2
$(runs SRV)
B:
COPY A:\\INVSERV.CFG C:\\SRV
INSTALL <A:\\ANS3.TXT
IF ERRORLEVEL 1 ECHO FAILED: 3
TYPE C:\\SRV\\INVSERV.CFG
C:\\SRV\\INSTALL <A:\\ANS4.TXT
IF ERRORLEVEL 1 ECHO FAILED: 4
C:\\ONE\\INSTALL /U <A:\\ANS5.TXT
IF ERRORLEVEL 1 ECHO FAILED: 5
C:\\SRV\\INSTALL /U <A:\\ANS6.TXT
IF ERRORLEVEL 1 ECHO FAILED: 6"
wait

# installed NAME: copy the hard disk the boot NAME kept into
# WORK/CHK/NAME/ and remove the boot's directory; 1 when there is none.
installed() {
    i_part=$(sed -n 's/^boot310\.sh: hard disk partition //p' \
        "$chk/$1.OUT")
    i_dir=$(sed -n 's/^boot310\.sh: kept //p' "$chk/$1.OUT")
    mkdir "$chk/$1"
    i_rc=0
    [ -n "$i_part" ] &&
        MTOOLS_SKIP_CHECK=1 mcopy -s -n -m -i "$i_part" '::*' "$chk/$1/" ||
        i_rc=1
    [ -z "$i_dir" ] || rm -rf "$i_dir"
    return $i_rc
}

# verdict NAME RC WANT: the boot NAME must have exited with RC, no run of
# it said FAILED, the output after its line RUNS must start with the
# expected snapshot and answers and hold the usage line of each program
# of $usage, and its hard disk, CHECK.REQ and RUN\ aside, must hold what
# the directory WANT does.
verdict() {
    v_out=$chk/$1.OUT
    v_rc=$(cat "$chk/$1.RC")
    v_why=
    if ! installed "$1"; then
        v_why="no hard disk kept"
    elif [ "$v_rc" != "$2" ]; then
        v_why="exit status $v_rc, expected $2"
    elif grep -aq FAILED "$v_out"; then
        v_why="an INSTALL run failed"
    elif ! sed '1,/^RUNS *$/d' "$v_out" | grep -av '^=STACK UNUSED=' |
            head -n "$(wc -l <"$chk/WANT.OUT")" |
            cmp -s - "$chk/WANT.OUT"; then
        v_why="INV did not show or answer as the host INV did"
    elif ! rm -r "$chk/$1/CHECK.REQ" "$chk/$1/RUN" ||
            ! diff -r "$3" "$chk/$1" >"$chk/$1.DIF" 2>&1; then
        v_why="not the files the installations leave ($chk/$1.DIF)"
    else
        for p in $usage; do
            grep -aq "^usage: $p " "$v_out" ||
                { v_why="no usage line of $p"; break; }
        done
    fi
    if [ -n "$v_why" ]; then
        echo "FAILED: $1: $v_why (see $v_out)"
        bad=1
    else
        echo "passed: $1"
    fi
}
want A "$chk/W360/RETROINV" VD
want B "$chk/W360/THIN" T
verdict INST360 "$nonbrc" "$chk/W360"
want M "$chk/W720/RETROINV" VDW
want N "$chk/W720/THIN" T
verdict INST720 "$nonbrc" "$chk/W720"
want K "$chk/W120/RETROINV" VDW
want K "$chk/W120/THIN" T
verdict INST120 "$nonbrc" "$chk/W120"
want H "$chk/W144/THIN" TW
mkdir -p "$chk/W144/SRV"
cp -r "$work/DATA" "$chk/W144/SRV/"
if grep -aq '^EDITED$' "$chk/INST144.OUT"; then
    verdict INST144 0 "$chk/W144"
else
    echo "FAILED: INST144: the edited INVSERV.CFG was not kept" \
        "(see $chk/INST144.OUT)"
    bad=1
fi

mkdist 2
if cmp -s "$chk/MK1/OUT/SHA256.TXT" "$chk/MK2/OUT/SHA256.TXT"; then
    echo "passed: two runs of mkdist.sh made the same images and zip"
else
    echo "FAILED: two runs of mkdist.sh did not make the same images and" \
        "zip ($chk/MK1.LOG, $chk/MK2.LOG)"
    bad=1
fi
[ $bad -eq 0 ] || exit 1
