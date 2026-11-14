#!/bin/sh
# mkdist.sh - make the release: the sample data directory, the floppy
# images that REL/DISKS.TXT describes and the zip of the 1.44 MB disk.
#
# usage: sh REL/mkdist.sh WORK OUT HOSTINV [NAME=VALUE]...
#
#   WORK        scratch directory, emptied first.  The sample data goes to
#               WORK/DATA, the responses that made it to WORK/SAMPLE.OUT,
#               the files of disk L as they are to WORK/PLAIN/L/, and as
#               they go on the disk, those marked Z compressed, to WORK/L/.
#   OUT         the directory of the images and the zip, emptied first; it
#               also gets SHA256.TXT, the SHA-256 sum of each.
#   HOSTINV     INV built for the host (OBJ/HOST/INV.bin).
#   NAME=VALUE  the text that replaces the mark @NAME@ in the text files
#               (LICENSE and those of REL/); @VERSION@ is PROTO_VERSION of
#               SRC/PROTO/PROTO.H and @THIN@, which REL/DISKS.TXT may hold
#               too, the network-only client's name, THIN of SRCS.MK.  No
#               line of a text file may then be wider than TEXT_COLS.
#
# Run from the repository root.  The sample data is made by HOSTINV /CMD
# /LOCAL from the requests of REL/SAMPLE.REQ on a new directory, with the
# engine's clock fixed at SAMPLE_NOW; every request must succeed and every
# file must pass TEST/CHKDBF.PY --strict.  Each image is formatted and
# filled by mtools with every time stamp at SAMPLE_NOW (SOURCE_DATE_EPOCH,
# and the files' own times, read in UTC), so that a build of the same
# programs gives the same images byte for byte.  A file marked Z in
# DISKS.TXT goes on the disks compressed by REL/szdd.py, as deterministic,
# under the name COMPRESS -r gives it (lzname), the sample data in DATA._
# with each file under its own name; each is compressed once.  The zip,
# for a hard disk or an emulator, holds the files of the 1.44 MB disk as
# they are in a folder named after its volume label, and takes that name
# too (RETROINV.ZIP); REL/diskzip.py writes it, as byte-stable as the
# images, each entry dated by its staged file; DISKS.TXT must have exactly
# one 1440 KB disk.  Every disk is staged before any image is written, so
# that INSTALL's list of each set, the source LIST of DISKS.TXT, can give
# the bytes of the set's files as installed.  A summary line per disk
# gives its files, the bytes they take and the bytes left free, and a
# last line the zip's size.
#
# Exit status: 0 done; 1 a sample request failed, a table failed its
# check, a disk is full or the zip cannot be written; 2 a usage or setup
# error.
set -eu

. TEST/shlib.sh

[ $# -ge 3 ] ||
    die "usage: sh REL/mkdist.sh WORK OUT HOSTINV [NAME=VALUE]..."
[ -f SRCS.MK ] && [ -f REL/DISKS.TXT ] ||
    die "run from the repository root"
work=$1
out=$2
[ -x "$3" ] || die "no host INV: $3"
hostinv=$(cd "$(dirname "$3")" && pwd)/$(basename "$3")
shift 3
for t in mformat mcopy mmd mdir sha256sum python3; do
    command -v $t >/dev/null || die "$t not installed"
done

# The widest line of a text file on the disks: one column short of the
# 80-column screen, whose last column wraps a line that fills it.
TEXT_COLS=79
# The sample data's clock and every time stamp on the disks.
SAMPLE_NOW=20261001090000
export TZ=UTC
SOURCE_DATE_EPOCH=$(date -u -d "$(echo $SAMPLE_NOW |
    sed 's/\(....\)\(..\)\(..\)\(..\)\(..\)\(..\)/\1-\2-\3 \4:\5:\6/')" +%s)
export SOURCE_DATE_EPOCH

# The sed program that fills in the marks of the text files.
version=$(sed -n 's/^#define[ \t]*PROTO_VERSION[ \t]*"\(.*\)".*/\1/p' \
    SRC/PROTO/PROTO.H)
[ -n "$version" ] || die "PROTO_VERSION not found in SRC/PROTO/PROTO.H"
vars=$(srcs_vars SRCS.MK)
thin=$(getvar THIN)
[ -n "$thin" ] || die "THIN not found in SRCS.MK"
marks="s/@VERSION@/$version/g;s/@THIN@/$thin/g"
for a in "$@"; do
    case $a in
    [A-Z]*=*) marks="$marks;s/@${a%%=*}@/${a#*=}/g" ;;
    *) die "not NAME=VALUE: $a" ;;
    esac
done

rm -rf "$work" "$out"
mkdir -p "$work/DATA" "$out"

# The sample data.
sed '/^#/d;/^$/d' REL/SAMPLE.REQ >"$work/SAMPLE.IN"
rc=0
(cd "$work" && "$hostinv" /CMD /LOCAL DATA /NOW:$SAMPLE_NOW \
    <SAMPLE.IN >SAMPLE.OUT 2>SAMPLE.ERR) || rc=$?
if [ $rc -ne 0 ] || [ -s "$work/SAMPLE.ERR" ] ||
        grep -q '^-ERR' "$work/SAMPLE.OUT"; then
    cat "$work/SAMPLE.ERR" >&2
    grep -n '^-ERR' "$work/SAMPLE.OUT" >&2 || :
    fail "the sample requests failed (exit status $rc); see $work/SAMPLE.OUT"
fi
python3 -I TEST/CHKDBF.PY --strict "$work"/DATA/* >"$work/CHKDBF.OUT" ||
    fail "the sample data failed its check; see $work/CHKDBF.OUT"

# The disks, each "L KB IMAGE LABEL SERIAL", and the files, each "DISKS
# ROLES PATH SOURCE" with @THIN@ filled in, in the order of DISKS.TXT.
disks=$(sed -n 's/^=//p' REL/DISKS.TXT)
files=$(sed '/^#/d;/^=/d;/^$/d;s/@THIN@/'"$thin"'/g' REL/DISKS.TXT)

# stage L PATH SOURCE: copy SOURCE to WORK/PLAIN/L/PATH, as text when it
# is one.
stage() {
    st_to=$work/PLAIN/$1/$2
    mkdir -p "$(dirname "$st_to")"
    case $3 in
    REL/*|LICENSE)
        sed "$marks" "$3" | dos_text >"$st_to"
        st_left=$(grep -o '@[A-Z][A-Z_0-9]*@' "$st_to" | head -1)
        [ -z "$st_left" ] || die "$3: no value for the mark $st_left"
        st_wide=$(tr -d '\r' <"$st_to" |
            LC_ALL=C awk -v w=$TEXT_COLS 'length > w { print NR; exit }')
        [ -z "$st_wide" ] ||
            die "$3: line $st_wide is wider than $TEXT_COLS columns" ;;
    *)  cp "$3" "$st_to" ;;
    esac
    touch -d "@$SOURCE_DATE_EPOCH" "$st_to"
}

# lzname PATH: PATH with the name COMPRESS -r gives its last part: the
# last character of a three-character extension '_', '_' added to a
# shorter one, "._" to a name without one (lz_name of SRC/TOOLS/LZEXP.C).
lzname() {
    case ${1##*/} in
    *.???) echo "${1%?}_" ;;
    *.*) echo "${1}_" ;;
    *) echo "$1._" ;;
    esac
}

# Every disk's files but INSTALL's list, as they are, in WORK/PLAIN/L/.
printf '%s\n' "$disks" | while read -r l kb image label serial; do
    mkdir -p "$work/PLAIN/$l"
    printf '%s\n' "$files" | while read -r on roles path src; do
        case $on in *$l*) ;; *) continue ;; esac
        check83 "$path"
        case $src in
        LIST) ;;
        SAMPLE)
            for f in $(cd "$work/DATA" && LC_ALL=C ls); do
                check83 "$path/$f"
                stage "$l" "$path/$f" "$work/DATA/$f"
            done ;;
        *)  [ -f "$src" ] || die "missing $src (make dist builds it)"
            stage "$l" "$path" "$src" ;;
        esac
    done
done

# INSTALL's list (SRC/TOOLS/INSTPLAN.H) of each set, the disks of one
# size, on each of its disks: the release's name; the size and the
# labels; the disk's number in the set; then a line "ROLES DISKS SIZE
# PATH" for each file of the set, ROLES with its Z, DISKS the numbers of
# its disks and SIZE its bytes as installed, for the sample data
# "BYTES,FILES", the sum of its files' bytes and their number, and for
# the list itself the list's own.
for kb in $(printf '%s\n' "$disks" | awk '!seen[$2]++ { print $2 }'); do
    set_l=$(printf '%s\n' "$disks" | awk -v k="$kb" '$2 == k { printf "%s", $1 }')
    labels=$(printf '%s\n' "$disks" | awk -v k="$kb" '$2 == k { printf " %s", $4 }')
    body=$(printf '%s\n' "$files" | while read -r on roles path src; do
        nums=$(awk -v on="$on" -v set="$set_l" 'BEGIN {
            for (i = 1; i <= length(set); i++)
                if (index(on, substr(set, i, 1))) printf "%d", i }')
        [ -n "$nums" ] || continue
        d=$(printf '%s' "$set_l" | cut -c"$(printf '%s' "$nums" | cut -c1)")
        case $src in
        LIST) size=@SELF@ ;;
        SAMPLE) size=$(cat "$work/PLAIN/$d/$path"/* | wc -c),$(ls \
            "$work/PLAIN/$d/$path" | wc -l) ;;
        *) size=$(wc -c <"$work/PLAIN/$d/$path") ;;
        esac
        echo "$roles $nums $size $path"
    done)
    head=$(printf '%s\n%s%s\n1' "$version" "$kb" "$labels")
    self=0
    while :; do
        n=$(printf '%s\n%s\n' "$head" "$body" | sed "s/@SELF@/$self/" |
            dos_text | wc -c)
        [ "$n" -ne "$self" ] || break
        self=$n
    done
    i=1
    for l in $(printf '%s' "$set_l" | sed 's/./& /g'); do
        printf '%s\n%s%s\n%d\n%s\n' "$version" "$kb" "$labels" $i "$body" |
            sed "s/@SELF@/$self/" | dos_text >"$work/PLAIN/$l/INSTALL.LST"
        touch -d "@$SOURCE_DATE_EPOCH" "$work/PLAIN/$l/INSTALL.LST"
        i=$((i + 1))
    done
done

# pack IN OUT KEY: compress IN to OUT, through WORK/LZ/KEY, which keeps
# the first compression of a file for the disks that hold it too.
pack() {
    pk_c=$work/LZ/$3
    if [ ! -f "$pk_c" ]; then
        mkdir -p "$(dirname "$pk_c")"
        python3 -I REL/szdd.py "$1" "$pk_c" || die "cannot compress $1"
    fi
    cp "$pk_c" "$2"
    touch -d "@$SOURCE_DATE_EPOCH" "$2"
}

# Every disk's files as they go on it, in WORK/L/: those marked Z
# compressed under lzname's name, those of line N of the files
# compressed once (pack's key N/NAME), the others as they are.
printf '%s\n' "$disks" | while read -r l kb image label serial; do
    n=0
    printf '%s\n' "$files" | while read -r on roles path src; do
        n=$((n + 1))
        case $on in *$l*) ;; *) continue ;; esac
        mkdir -p "$(dirname "$work/$l/$path")"
        case $roles in
        *Z*) z=$(lzname "$path")
             check83 "$z"
             if [ "$src" = SAMPLE ]; then
                 mkdir "$work/$l/$z"
                 for f in $(cd "$work/PLAIN/$l/$path" && LC_ALL=C ls); do
                     pack "$work/PLAIN/$l/$path/$f" "$work/$l/$z/$f" "$n/$f"
                 done
             else
                 pack "$work/PLAIN/$l/$path" "$work/$l/$z" "$n/${z##*/}"
             fi ;;
        *)   cp -pr "$work/PLAIN/$l/$path" "$work/$l/$path" ;;
        esac
    done
done

# The images, each file put on its disk in the order of DISKS.TXT.
printf '%s\n' "$disks" | while read -r l kb image label serial; do
    img=$out/$image
    mformat -C -i "$img" -f "$kb" -v "$label" -N "$serial" :: ||
        die "cannot format $image"
    printf '%s\n' "$files" | while read -r on roles path src; do
        case $on in *$l*) ;; *) continue ;; esac
        case $roles in *Z*) path=$(lzname "$path") ;; esac
        if [ "$src" = SAMPLE ]; then
            mmd -i "$img" "::$path"
            for f in $(cd "$work/$l/$path" && LC_ALL=C ls); do
                mcopy -m -i "$img" "$work/$l/$path/$f" "::$path/$f" ||
                    fail "$image: no room for $path/$f"
            done
            continue
        fi
        d=$(dirname "$path")
        if [ "$d" != . ] && ! mdir -i "$img" "::$d" >/dev/null 2>&1; then
            mmd -i "$img" "::$d"
        fi
        mcopy -m -i "$img" "$work/$l/$path" "::$path" ||
            fail "$image: no room for $path"
    done
    n=$(find "$work/$l" -type f | wc -l)
    used=$(find "$work/$l" -type f -exec cat {} + | wc -c)
    free=$(mdir -i "$img" :: |
        sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
    echo "$image: $label, $kb KB, $n files of $used bytes, $free bytes free"
done

read -r zl zimage zname <<EOF
$(printf '%s\n' "$disks" |
    awk '$2 == 1440 { n++; d = $1 " " $3 " " $4 } END { if (n == 1) print d }')
EOF
[ -n "$zname" ] || die "REL/DISKS.TXT needs exactly one 1440 KB disk"
check83 "$zname.ZIP"
python3 -I REL/diskzip.py "$work/PLAIN/$zl" "$zname" "$out/$zname.ZIP" ||
    fail "cannot write $zname.ZIP"
echo "$zname.ZIP: the files of $zimage as they are in $zname/," \
    "$(wc -c <"$out/$zname.ZIP") bytes"

(cd "$out" && sha256sum $(printf '%s\n' "$disks" | awk '{ print $3 }') \
    "$zname.ZIP") >"$out/SHA256.TXT"
cat "$out/SHA256.TXT"
