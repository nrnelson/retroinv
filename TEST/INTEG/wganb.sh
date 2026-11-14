# wganb.sh - the event log check of the NetBIOS sessions of WGANB.SCR.
# Sourced by wgagate.sh, and by T_RELAY.PY to test it, after they set
# $here to this directory; not run on its own.

# wganb_check SRVLOG - check that the server's event log SRVLOG (CR LF or
# LF lines) holds the sessions of WGANB.SCR's @CALL lines, known by their
# stations, the names called from: one OPEN per @CALL, so that no CALL
# was made again, and in each of those sessions a REQ BYE and then its
# CLOSE, so that none was lost or left half open.  Other lines and the
# sessions of other stations are passed over.  Prints each failure as
# "wganb: ..." on standard error; returns 0 when there is none, else 1.
wganb_check() {
    if [ ! -f "$1" ]; then
        echo "wganb: no event log $1" >&2
        return 1
    fi
    wn_calls=$(tr -d '\r' <"$here/WGANB.SCR" |
               sed -n 's/^@CALL [0-9][0-9]* \([^ ]*\)$/\1/p' |
               paste -s -d ' ' -)
    tr -d '\r' <"$1" | awk -v calls="$wn_calls" '
        BEGIN {
            n = split(calls, name, " ")
            for (i = 1; i <= n; i++)
                ours[name[i]] = 1
        }
        function fail(why) {
            print "wganb: " why
            bad = 1
        }
        $4 == "OPEN" && NF == 5 && ($5 in ours) {
            if ($3 in sta)
                fail("slot " $3 " opened for " $5 " while the session of " \
                     sta[$3] " was not closed")
            opens++
            sta[$3] = $5
            bye[$3] = 0
            next
        }
        !($3 in sta) { next }
        $4 == "OPEN" {
            fail("slot " $3 " opened while the session of " sta[$3] \
                 " was not closed")
            delete sta[$3]
            next
        }
        $4 == "REQ" && NF == 5 && $5 == "BYE" { bye[$3] = 1 }
        $4 == "CLOSE" {
            if (!bye[$3])
                fail("the session of " sta[$3] " on slot " $3 \
                     " closed without BYE")
            delete sta[$3]
        }
        END {
            for (s in sta)
                fail("the session of " sta[s] " on slot " s " was not closed")
            if (opens + 0 != n)
                fail(opens + 0 " OPEN events of " calls ", not " n)
            exit bad
        }' >&2
}
