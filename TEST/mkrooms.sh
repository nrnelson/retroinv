#!/bin/sh
# mkrooms.sh - write the requests that add N rooms to a seeded data
# directory, for a run whose location tree must be longer than the rows
# a pick list's seek passes (LV_SEEKMAX of SRC/CLIENT/PAGER.H).
#
# usage: sh TEST/mkrooms.sh N
#
#   N   the rooms to add, 1 or more: top-level rooms "Room 1" to
#       "Room N", numbered in as many digits as N has so that they sort
#       in their order, after the locations of TEST/KEYS/SEED.REQ, whose
#       ADMIN adds them; the last has the code FAR and holds a bin,
#       Crate.
#
# The requests go to standard output, a request file as
# TEST/KEYS/SEED.REQ is (TEST/T_SCR.SH's "#@ ROOMS", wintest.sh -q).
#
# Exit status: 0; 2 on a usage error.
set -eu

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

[ $# = 1 ] || usage
case $1 in
''|0*|*[!0-9]*) usage ;;
esac

awk -v n="$1" 'BEGIN {
    print "HELLO MKROOMS"
    print "LOGIN ADMIN Retro1986"
    f = "LOC ADD LOC_NAME=\"Room %0" length(n) "d\" LTYPE=ROOM"
    for (i = 1; i < n; i++)
        printf f "\n", i
    printf f " LOC_CODE=FAR\n", n
    print "LOC ADD LOC_NAME=Crate LTYPE=BIN PARENT_ID=FAR"
    print "BYE"
}'
