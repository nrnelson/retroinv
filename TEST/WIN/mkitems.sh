#!/bin/sh
# mkitems.sh - write the requests that add N items to a seeded data
# directory, for a Windows run whose list must be long.
#
# usage: sh TEST/WIN/mkitems.sh N
#
#   N   the items to add, 1 or more: parts of maker Many, models M1 to MN,
#       at Box B2, after the three items of TEST/KEYS/SEED.REQ, whose
#       ADMIN adds them, so that item n + 3 is model Mn.
#
# The requests go to standard output, a request file as
# TEST/KEYS/RLLONG.REQ is (wintest.sh -q).
#
# Exit status: 0; 2 on a usage error.
set -eu

usage() { sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

[ $# = 1 ] || usage
case $1 in
''|0*|*[!0-9]*) usage ;;
esac

awk -v n="$1" 'BEGIN {
    print "HELLO MKITEMS"
    print "LOGIN ADMIN Retro1986"
    for (i = 1; i <= n; i++)
        print "ITEM ADD CATEGORY=PART MFR=Many MODEL=M" i " LOC_ID=B2"
    print "BYE"
}'
