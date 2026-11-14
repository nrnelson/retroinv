#!/bin/sh
# mapchk.sh - check a linker map file against the 16-bit segment budgets.
#
# usage: sh TEST/mapchk.sh [-v] [-w STACK] [-f | -n MAXNEAR] [-l BUILD]
#            FILE.MAP
#
#   -v          also print the near data total, its extent, the near heap,
#               DGROUP's free bytes and the load size, on standard output
#   -n MAXNEAR  the program's near data budget in bytes, 1 or more
#               (default MAX_NEAR)
#   -f          no near data budget: only check that DGROUP fits (for the
#               Turbo C unit tests, which store every repeated string
#               literal again)
#   -w STACK    the map of a Windows program, whose stack is STACK bytes
#               (win_stack of MAKEFILE) and which links CMEM.C without a
#               near-heap module, Windows growing its local heap in DGROUP
#   -l BUILD    the map of a program with free-memory floors, built by
#               BUILD (OW, TC or WC65): hold its load size to its line in
#               TEST/LOADBUD.TXT
#
# Every CODE-class segment must fit in MAX_CODE bytes (one 64 KB segment
# less headroom for growth) and the near data in DGROUP, not counting the
# stack, in MAXNEAR bytes.  No single far data segment may exceed MAX_FAR
# bytes, the largest buffer allowed.  MAKEFILE passes -n from a program's
# <NAME>_MAXNEAR of SRCS.MK when it sets one.
#
# The near heap is the size the near-heap module the map shows linked
# gives: CMEM_NEARHEAP of SRC/COMPAT/COMPAT.H for SRC/COMPAT/CMEMHN.C,
# CMEM_TCPHEAP for CMEMHT.C, for CMEMHI.C and CMEMHR.C their own
# CMEM_INVHEAP and CMEM_REMHEAP in a TLINK map and CMEM_NEARHEAP in the
# others, none without any.  A map that links CMEM.C needs exactly one of
# them; the Open Watcom link already fails without one, the Turbo C and
# Watcom C 6.5 links do not.  DGROUP's 64 KB must hold that heap, the
# stack (_stklen of SRC/COMPAT/CDOS.C, the Turbo C build's; the Watcom
# builds link with the same size, MAKEFILE and WC65/mkbat.sh) and MAXNEAR
# bytes of static near data, and also the extent of the near data in the
# map, from the lowest start of a piece to the highest end, which counts
# the padding the linker puts between modules: Turbo C's start-up code
# ends a program whose extent, stack and heap come to more before main.
# The near data total, the sum of the pieces, is what MAXNEAR bounds, as
# TEST/OMFNEAR.PY counts it.  Both files are found from this script's
# directory, so it runs from anywhere.
#
# Three map formats are read, told apart by their segment table heading:
#   Open Watcom wlink   "Segment  Class  Group  Address  Size", sizes in hex
#   WLINK 4.1           "Segment  Group  Class  Address  Size" (Watcom C
#                       6.5); the group column is empty for ungrouped
#                       segments
#   Turbo C TLINK       "Start  Stop  Length  Name  Class", one line per
#                       segment with the length as 0nnnnH; the group of each
#                       piece is only in the "Detailed map of segments"
#                       (lines "ssss:oooo llll C=class S=name G=group M=..."),
#                       which TLINK writes with /s, so a Turbo C map without
#                       it is refused
#
# The load size is the end of the highest segment of the map, rounded up
# to a paragraph: the memory the program's image and its uninitialised
# data take when DOS loads it, below what the near heap and the far
# allocations take afterwards.  It grows when
# code or static data move DGROUP up a paragraph, and the free memory a
# program's deepest screens are left at its floors (the *_FREEKB_* of
# MAKEFILE) shrinks by as much.  The budgets are in LOADBUD.TXT beside
# this script, which says how they are set.
#
# Exit status: 0 when the map fits; 1 when a budget is exceeded or the map
# has no segment table (or, from TLINK, no detailed map); 2 on a usage
# error or when the map, _stklen or a heap size cannot be read.  Its
# messages go to standard error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/shlib.sh"
verbose=
MAX_CODE=61440
MAX_NEAR=20480
MAX_FAR=32768
maxnear=$MAX_NEAR
usage="usage: mapchk.sh [-v] [-w STACK] [-f | -n MAXNEAR] [-l BUILD] FILE.MAP"
fit=
budget=
winstack=
build=
while getopts :vfn:w:l: opt; do
    case $opt in
    v) verbose=1 ;;
    f) fit=1 ;;
    n) budget=$OPTARG ;;
    w) winstack=$OPTARG ;;
    l) build=$OPTARG ;;
    *) die "$usage" ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || die "$usage"
if [ -n "$fit" ]; then
    [ -z "$budget" ] || die "$usage"
    maxnear=0
elif [ -n "$budget" ]; then
    case $budget in *[!0-9]*|0*) die "-n needs a number of bytes" ;; esac
    maxnear=$budget
fi
map=$1
[ -f "$map" ] || die "no such file: $map"
cdos=$here/../SRC/COMPAT/CDOS.C
[ -f "$cdos" ] || die "no such file: $cdos"
STACK=$(sed -n 's/^unsigned _stklen = \([0-9][0-9]*\);.*/\1/p' "$cdos")
[ -n "$STACK" ] || die "_stklen not found in $cdos"
if [ -n "$winstack" ]; then
    case $winstack in *[!0-9]*|0*) die "-w needs a number of bytes" ;; esac
    STACK=$winstack
fi
nearheap=$(cdefine "$here/../SRC/COMPAT/COMPAT.H" CMEM_NEARHEAP)
tcpheap=$(cdefine "$here/../SRC/COMPAT/COMPAT.H" CMEM_TCPHEAP)
invheap=$(cdefine "$here/../SRC/COMPAT/CMEMHI.C" CMEM_INVHEAP)
remheap=$(cdefine "$here/../SRC/COMPAT/CMEMHR.C" CMEM_REMHEAP)
loadbud=
if [ -n "$build" ]; then
    name=$(basename "$map" .MAP)
    loadbud=$(awk -v n="$name" -v b="$build" '$1 == n && $2 == b { print $3 }' \
        "$here/LOADBUD.TXT")
    case $loadbud in
    ''|*[!0-9]*) die "$name $build has no load budget in TEST/LOADBUD.TXT" ;;
    esac
fi

tr -d '\r' < "$map" |
awk -v map="$map" -v verbose="$verbose" -v maxcode="$MAX_CODE" \
    -v maxnear="$maxnear" -v maxfar="$MAX_FAR" -v stack="$STACK" \
    -v nearheap="$nearheap" -v tcpheap="$tcpheap" -v invheap="$invheap" \
    -v remheap="$remheap" -v win="$winstack" -v build="$build" \
    -v loadbud="$loadbud" '
function hex(s,   i, v, d) {
    v = 0
    s = toupper(s)
    sub(/H$/, "", s)
    for (i = 1; i <= length(s); i++) {
        d = index("0123456789ABCDEF", substr(s, i, 1)) - 1
        if (d < 0) return -1
        v = v * 16 + d
    }
    return v
}
# The linear address of seg:off.
function lin(addr,   a) {
    split(addr, a, ":")
    return hex(a[1]) * 16 + hex(a[2])
}
# Raise top, the end of the highest segment, to start + size.
function reach(start, size) {
    if (start + size > top) top = start + size
}
# Count a non-STACK DGROUP piece of size bytes at seg:off in the near
# data: its size in near, its span in lo..hi.
function dgroup(addr, size,   start) {
    if (size <= 0) return
    near += size
    start = lin(addr)
    if (!spans || start < lo) lo = start
    if (!spans || start + size > hi) hi = start + size
    spans = 1
}
# Check one segment of the summary table.
function seg(name, class, size) {
    rows++
    if (size < 0) return
    if (class == "CODE" && size > maxcode) {
        printf "%s: code segment %s is %d bytes (limit %d)\n", map, name,
            size, maxcode > "/dev/stderr"
        bad = 1
    }
    if (class ~ /^FAR_(DATA|BSS)$/ && size > maxfar) {
        printf "%s: far segment %s is %d bytes (limit %d)\n", map, name,
            size, maxfar > "/dev/stderr"
        bad = 1
    }
}
/^Segment[ \t]+Class[ \t]+Group/ { fmt = "ow"; inseg = 1; next }
/^Segment[ \t]+Group[ \t]+Class/ { fmt = "wl41"; inseg = 1; next }
/^[ \t]*Start[ \t]+Stop[ \t]+Length[ \t]+Name[ \t]+Class/ { fmt = "tlink"; inseg = 1; next }
inseg && /^[ \t]*$/ { if (rows) inseg = 0; next }
inseg && /^=+/ { next }
inseg && fmt == "ow" && NF >= 5 && $(NF-1) ~ /:/ {
    size = hex($NF)
    seg($1, $2, size)
    reach(lin($(NF-1)), size)
    if ($3 == "DGROUP" && $2 != "STACK") dgroup($(NF-1), size)
    next
}
inseg && fmt == "wl41" && NF >= 4 && $(NF-1) ~ /:/ {
    size = hex($NF)
    if (NF >= 5) { group = $2; class = $3 } else { group = ""; class = $2 }
    seg($1, class, size)
    reach(lin($(NF-1)), size)
    if (group == "DGROUP" && class != "STACK") dgroup($(NF-1), size)
    next
}
inseg && fmt == "tlink" && NF == 5 && $3 ~ /^[0-9A-Fa-f]+H$/ {
    seg($4, $5, hex($3))
    reach(hex($1), hex($3))
    next
}
fmt == "tlink" && /^[0-9A-Fa-f]+:[0-9A-Fa-f]+ [0-9A-Fa-f]+ C=/ {
    detail = 1
    class = ""; group = ""
    for (i = 3; i <= NF; i++) {
        if ($i ~ /^C=/) class = substr($i, 3)
        if ($i ~ /^G=/) group = substr($i, 3)
    }
    if (group == "DGROUP" && class != "STACK") dgroup($1, hex($2))
}
# The modules that set the near heap, named in each format: an object
# path in a wlink "Module:" line or a WLINK 4.1 symbol line, a source
# name in a TLINK "M=" field.
{
    u = toupper($0)
    if (u ~ /[\/\\]CMEM\.OBJ|M=CMEM\.C( |$)/) cmem = 1
    if (u ~ /[\/\\]CMEMHN\.OBJ|M=CMEMHN\.C( |$)/) hn = 1
    if (u ~ /[\/\\]CMEMHT\.OBJ|M=CMEMHT\.C( |$)/) ht = 1
    if (u ~ /[\/\\]CMEMHI\.OBJ|M=CMEMHI\.C( |$)/) hinv = 1
    if (u ~ /[\/\\]CMEMHR\.OBJ|M=CMEMHR\.C( |$)/) hr = 1
}
END {
    if (!rows) {
        printf "%s: no segment table found\n", map > "/dev/stderr"
        exit 1
    }
    if (fmt == "tlink" && !detail) {
        printf "%s: no detailed map of segments (link with TLINK /s)\n",
            map > "/dev/stderr"
        exit 1
    }
    tc = fmt == "tlink"
    heap = hn ? nearheap : ht ? tcpheap : \
        hinv ? (tc ? invheap : nearheap) : hr ? (tc ? remheap : nearheap) : 0
    if (cmem && !win && hn + ht + hinv + hr != 1) {
        printf "%s: CMEM.C needs one near-heap module, CMEMHN.C, " \
            "CMEMHT.C, CMEMHI.C or CMEMHR.C\n", map > "/dev/stderr"
        bad = 1
    }
    if (maxnear && maxnear + stack + heap > 65536) {
        printf "%s: near data budget %d + _stklen %d + near heap %d " \
            "exceed 64 KB\n", map, maxnear, stack, heap > "/dev/stderr"
        bad = 1
    }
    if (maxnear && near > maxnear) {
        printf "%s: near data is %d bytes (limit %d)\n", map, near,
            maxnear > "/dev/stderr"
        bad = 1
    }
    extent = hi - lo
    if (extent + stack + heap > 65536) {
        printf "%s: near data extent %d + _stklen %d + near heap %d " \
            "exceed 64 KB\n", map, extent, stack, heap > "/dev/stderr"
        bad = 1
    }
    load = int((top + 15) / 16) * 16
    if (loadbud && load > loadbud) {
        printf "%s: load size %d bytes, %d over the %s budget of " \
            "TEST/LOADBUD.TXT, which its free-memory floors in MAKEFILE " \
            "were measured at; trim it, or run their gates and raise " \
            "the budget (DOC/BUILD.md, Load size)\n", map, load,
            load - loadbud, build > "/dev/stderr"
        bad = 1
    }
    if (verbose)
        printf "%s: near data %d bytes (%s), extent %d, near heap %d, " \
            "DGROUP %d bytes free, load size %d\n", map, near,
            maxnear ? "limit " maxnear : "no budget", extent, heap,
            65536 - extent - stack - heap, load
    exit bad
}'
