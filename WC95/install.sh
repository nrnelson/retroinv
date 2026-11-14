#!/bin/sh
# install.sh - install Watcom C 9.5 for 16-bit Windows from its disks.
#
# usage: sh WC95/install.sh IMG_DIR DEST_DIR
#
#   IMG_DIR   directory holding the 1.44 MB disk images W9532_01.img ..
#             W9532_08.img (WATCOM C/C++32 9.5) and W9516_01.img ..
#             W9516_04.img (the C/C++16 Delta Pack), and the binary
#             patches Patch32.zip and Patch16.zip
#   DEST_DIR  installation directory, which must not exist yet, mounted
#             as D: by WC95/runwc.sh (which reads it from WC95_DIR,
#             default $RETRO_DL/wc95/WATCOM)
#
# The 16-bit compiler comes as a delta pack without headers, resource
# compiler or linker definitions, which it takes from the 32-bit set, and
# both sets are packed in WATCOM's own archive format, which only their
# INSTALL.EXE unpacks.  So mtools copies the disks of each set into one
# directory per set, and each set's INSTALL.SCR has its questions ("ask
# NAME") replaced by the answers below ("set NAME y|n") and its disk
# changes dropped.  One headless DOSBox-X run (TC/TC.CONF filled in) then
# runs the 32-bit set's INSTALL.EXE, the 16-bit set's over it, and the
# patches' APPLYA.BAT, the level A patches of both sets, and APPLYB.BAT,
# the 32-bit set's level B (the 16-bit set has no level B patch here, so
# its compiler stays at 9.5a).  Nothing from the disks or the patches runs
# on the host; of the patch archives, extracted by 7z, only the regular
# files under Patch/A and Patch/B are used.
#
# The install is made in a scratch directory beside DEST_DIR and moved
# into place only when every file WC95/TOOLS.SHA pins matches.
#
# Exit status: 0 when the compiler is installed; 1 when the install or
# patch run fails or its result is not as WC95/TOOLS.SHA pins it; 2 on a
# usage error, a missing input or an existing DEST_DIR; 129, 130 or 143
# on SIGHUP, SIGINT or SIGTERM.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

[ $# -eq 2 ] || die "usage: sh WC95/install.sh IMG_DIR DEST_DIR"
IMG=$1
DEST=$2
need_tools mcopy 7z dosbox-x
for f in W9532_01 W9532_02 W9532_03 W9532_04 W9532_05 W9532_06 W9532_07 \
         W9532_08 W9516_01 W9516_02 W9516_03 W9516_04; do
    [ -f "$IMG/$f.img" ] || die "$IMG/$f.img not found"
done
for f in Patch32.zip Patch16.zip; do
    [ -f "$IMG/$f" ] || die "$IMG/$f not found"
done
[ ! -e "$DEST" ] || die "$DEST exists; remove it first"
mkdir -p "$(dirname "$DEST")"
T=$(mktemp -d "$(cd "$(dirname "$DEST")" && pwd -P)/.wc95inst.XXXXXX")
trap 'rm -rf "$T"' EXIT
on_signals
case $T in *'|'*|*'&'*|*'"'*) die "path contains | & or \": $T" ;; esac

# The answers to the sets' questions: the C compiler, its DOS host and
# its Windows 3.x target with the libraries, and nothing else.
ans32='wcc y
wpp n
doshost y
os2host n
nthost n
dosx n
pls n
ecs n
dos4g n
ads n
win3tgt y
wex y
doc n
penw n
os2tgt n
nttgt n
nlm n
help y
wprof n
pen n
clibexam n'
ans16='wcc y
wpp n
dos y
os2 n
doslib y
os2lib n
winlib y'

# answer SCR ANSWERS - rewrite the install script SCR in place: each
# "ask NAME ..." (behind any "if %x " and "ifnot %x " prefixes) becomes
# "set NAME" and its answer from ANSWERS, "NAME y|n" lines, and each
# "enter ..." disk change is dropped; stops with die on a question
# ANSWERS does not answer.
answer() {
    tr -d '\r' < "$1" | awk -v ans="$2" '
    BEGIN { n = split(ans, l, "\n")
            for (i = 1; i <= n; i++) { split(l[i], w, " "); a[w[1]] = w[2] } }
    { pre = ""; rest = $0
      while (match(rest, /^(if|ifnot) %[a-z0-9]+ /)) {
          pre = pre substr(rest, 1, RLENGTH); rest = substr(rest, RLENGTH + 1)
      }
      if (rest ~ /^ask /) {
          split(rest, w, " ")
          if (!(w[2] in a)) { print w[2] > "/dev/stderr"; exit 3 }
          print pre "set " w[2] " " a[w[2]]; next
      }
      if (rest !~ /^enter /) print }' > "$1.new" ||
        die "$1: a question without an answer"
    dos_text "$1.new" > "$1"
    rm -f "$1.new"
}

mkdir "$T/a" "$T/b" "$T/c" "$T/d" "$T/e" "$T/e/A" "$T/e/B" "$T/z16" "$T/z32"
for i in 01 02 03 04 05 06 07 08; do
    mcopy -n -i "$IMG/W9532_$i.img" '::*' "$T/a/"
done
for i in 01 02 03 04; do
    mcopy -n -i "$IMG/W9516_$i.img" '::*' "$T/b/"
done
answer "$T/a/INSTALL.SCR" "$ans32"
answer "$T/b/INSTALL.SCR" "$ans16"
7z x -y -bd -o"$T/z32" "$IMG/Patch32.zip" > /dev/null </dev/null
7z x -y -bd -o"$T/z16" "$IMG/Patch16.zip" > /dev/null </dev/null
# Both level A sets share APPLYA.BAT and README.A, and their other files
# do not overlap.
find "$T/z32/Patch/A" "$T/z16/Patch/A" -maxdepth 1 -type f \
    -exec cp {} "$T/e/A/" \;
find "$T/z32/Patch/B" -maxdepth 1 -type f -exec cp {} "$T/e/B/" \;
i=0
while [ $i -lt 100 ]; do printf 'y\r\n'; i=$((i + 1)); done > "$T/c/Y.TXT"
printf '%s\r\n' '@ECHO OFF' \
    'A:\INSTALL.EXE A: D:\WATCOM < C:\Y.TXT' \
    'B:\INSTALL.EXE B: D:\WATCOM < C:\Y.TXT' \
    'E:' 'CD \A' 'CALL APPLYA.BAT D:\WATCOM -p' \
    'CD \B' 'CALL APPLYB.BAT D:\WATCOM -p' 'C:' 'ECHO === INSTALL DONE' \
    > "$T/c/INST.BAT"
sed -e "s|@REPO@|$T/c|g" -e "s|@TOOLDIR@|$T/d|g" \
    -e "s|@MOUNTS@|mount a \"$T/a\" -nocachedir\\
mount b \"$T/b\" -nocachedir\\
mount e \"$T/e\" -nocachedir|" \
    -e 's|@BAT@|C:\\INST.BAT|g' -e 's|@LOG@|C:\\INST.LOG|g' \
    "$here/../TC/TC.CONF" > "$T/c/INST.CNF"
# The run takes some 6 minutes; like the other setup steps it has a fixed
# limit, which DOSTIME_SCALE does not change.
limit=1800
(cd "$T/c" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    exec timeout -k 10 $limit dosbox-x -conf "$T/c/INST.CNF" \
        -nogui -nomenu -exit > "$T/c/dosbox.out" 2>&1) ||
    fail "DOSBox-X failed or timed out after ${limit}s"
[ -f "$T/c/INST.LOG" ] || fail "the install run wrote no log"
log=$(tr -d '\r' < "$T/c/INST.LOG")
printf '%s\n' "$log" | grep -q '^=== INSTALL DONE$' ||
    fail "the install run did not end; its log:
$log"
! printf '%s\n' "$log" |
    grep -Eiq 'incorrect crc|not in archive|cannot|invalid' ||
    fail "the install run reported errors; its log:
$log"
(cd "$T/d/WATCOM" && sha256sum --quiet --strict -c "$here/TOOLS.SHA") ||
    fail "the install is not as WC95/TOOLS.SHA pins it"
mv "$T/d/WATCOM" "$DEST"
echo "$prog: installed Watcom C 9.5 into $DEST"
