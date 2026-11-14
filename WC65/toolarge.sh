#!/bin/sh
# toolarge.sh - hold the functions Watcom C 6.5 optimises less to a list.
#
# usage: sh WC65/toolarge.sh LIST WORK
#
# WCC 6.5 says "Procedure 'NAME' too large: may affect optimizations" of
# a function it then optimises less, and sets no error level for it.  This
# reads the compile logs of the build tree WORK, the CC<k>.LOG of the
# batch files that WORK/CC.LST names, and allows the message only for the
# functions of LIST, one OBJECT:NAME per line ("#" starts a comment).  It
# names every other function of a program module (a source under
# WORK/SRC/ other than a test T_*.C) the message is given for, and every
# function of LIST whose object a log compiles without the message for
# it (an object no log compiles, of a program left out of WC65_EXES, is
# not named).  A message belongs to the object whose COPY line
# (" NAME.OBJ") follows it in the log.
#
# Exit status: 0 when the message is given for no program function
# outside LIST, 1 when it is, 2 on a usage or read error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../TEST/shlib.sh"

[ $# -eq 2 ] || die "usage: sh WC65/toolarge.sh LIST WORK"
[ -f "$1" ] || die "list $1 not found"
[ -f "$2/CC.LST" ] || die "$2/CC.LST not found"
ok=$(sed 's/#.*//' "$1")
logs=
for b in $(cat "$2/CC.LST"); do
    [ -f "$2/$b.LOG" ] || die "$2/$b.LOG not found"
    logs="$logs $2/$b.LOG"
done
progs=$(cd "$2" && find SRC -name '*.C' ! -name 'T_*' |
    sed 's|.*/||; s|\.C$|.OBJ|')
# shellcheck disable=SC2086
cat $logs | tr -d '\r' | awk -v progs="$progs" -v ok="$ok" -v prog="$prog" '
    BEGIN { n = split(progs, w); for (i = 1; i <= n; i++) mine[w[i]] = 1
            n = split(ok, w); for (i = 1; i <= n; i++) allow[w[i]] = 1 }
    /^Procedure .* too large: may affect optimizations$/ {
        f = $2; gsub(/\047/, "", f); fns = fns " " f; next }
    /^ [^ ]+\.OBJ$/ {
        built[$1] = 1
        if ($1 in mine) {
            n = split(fns, w)
            for (i = 1; i <= n; i++) {
                k = $1 ":" w[i]; said[k] = 1
                if (!(k in allow)) { bad = 1
                    print prog ": " k " is too large for the optimiser" }
            }
        }
        fns = "" }
    END { for (k in allow) {
              split(k, o, ":")
              if ((o[1] in built) && !(k in said))
                  print prog ": " k " is no longer too large" }
          exit bad }' >&2
