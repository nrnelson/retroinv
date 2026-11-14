#!/bin/sh
# smbd.sh - start or stop a private Samba server for the TCP/IP test VM.
#
# usage: sh TEST/INTEG/smbd.sh start RUNDIR SHAREDIR
#        sh TEST/INTEG/smbd.sh stop RUNDIR
#
#   RUNDIR    the server's own directory: its smb.conf, its log
#             smbd.log, the files PID and PORT and its state directory
#             state; start makes it anew (an existing one must have
#             been made by smbd.sh and hold no running server)
#   SHAREDIR  an existing directory, served as share SMB_SHARE
#             (tcp330.sh); start makes its mailbox subdirectories
#             REQUEST and RESPONSE
#
# env: SMBD_LIFE  seconds after which a server not yet stopped is stopped
#                 (default 3600)
#
# start runs smbd as the calling user, from the template SMB/SMB.CNF (the
# dialect and logon settings are explained there), listening on 127.0.0.1
# only, on the first port of the port slot it takes (port_slot of
# TEST/shlib.sh): PORT_BASE + 2n for slot n.  No system file and no root
# right is used.  It writes the port to RUNDIR/PORT and smbd's process ID
# to RUNDIR/PID, returns once smbd listens, and leaves smbd running with
# its log in RUNDIR/smbd.log.  smbd cannot hold the slot (it closes the
# descriptors it inherits), so a watcher process holds it instead, as
# long as smbd lives and at most SMBD_LIFE seconds, after which it stops
# smbd itself; the watcher holds none of the caller's locks (descriptors
# 7 to 9).
#
# stop ends the process RUNDIR/PID names, if it is still that server (its
# command line names RUNDIR's smb.conf); its connection processes end
# with it.  A client reaching the server through a QEMU guestfwd of
# "cmd:nc -q 0 127.0.0.1 PORT" has one nc per connection, which ends
# when the guest or the server closes it.
#
# Exit status: 0 on success; 1 when smbd does not start or does not
# stop; 2 on a usage or setup error.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
. "$here/tcp330.sh"

usage="usage: smbd.sh start RUNDIR SHAREDIR | stop RUNDIR"
marker=.smbd
SMBD_LIFE=${SMBD_LIFE:-3600}
# Seconds smbd has to start listening, and to end once told to.
wait_secs=20

# alive PID CONF - succeed when PID is a running process whose command
# line names CONF.
alive() {
    tr '\0' ' ' 2>/dev/null <"/proc/$1/cmdline" | grep -qF -- "-s $2 "
}

# stop_server RUN - end the server of RUN, if any; fails when it outlives
# wait_secs.
stop_server() {
    [ -f "$1/PID" ] || return 0
    pid=$(cat "$1/PID")
    if alive "$pid" "$1/smb.conf"; then
        kill "$pid" 2>/dev/null || :
        n=0
        while alive "$pid" "$1/smb.conf"; do
            [ $n -lt $((wait_secs * 10)) ] ||
                fail "smbd $pid did not end within ${wait_secs}s"
            sleep 0.1
            n=$((n + 1))
        done
    fi
    rm -f "$1/PID"
}

[ $# -ge 2 ] || die "$usage"
run=$(realpath -m "$2")
case $1 in
    start) [ $# -eq 3 ] || die "$usage" ;;
    stop)
        [ $# -eq 2 ] || die "$usage"
        [ -f "$run/$marker" ] || die "$run was not made by smbd.sh"
        stop_server "$run"
        exit 0 ;;
    *) die "$usage" ;;
esac

need_tools smbd ss flock realpath
is_uint "$SMBD_LIFE" && [ "$SMBD_LIFE" -gt 0 ] ||
    die "SMBD_LIFE must be a positive integer"
share=$(realpath -e "$3" 2>/dev/null) && [ -d "$share" ] ||
    die "not a directory: $3"
for d in "$run" "$share"; do
    case $d in
        /|"$HOME"|"$(realpath -m "$HOME")") die "refusing $d" ;;
    esac
done
case $run/ in
    "$share"/*) die "RUNDIR $run lies inside SHAREDIR" ;;
esac
case $share/ in
    "$run"/*) die "SHAREDIR $share lies inside RUNDIR" ;;
esac
if [ -e "$run" ]; then
    [ -f "$run/$marker" ] || die "$run exists and was not made by smbd.sh"
    if [ -f "$run/PID" ] && alive "$(cat "$run/PID")" "$run/smb.conf"; then
        die "$run holds a running smbd"
    fi
    rm -rf "$run"
fi
mkdir -p "$run/state" "$share/REQUEST" "$share/RESPONSE"
: >"$run/$marker"

port_slot
port=$((PORT_BASE + 2 * sl_slot))
# sed reads "\" and "&" in the replacement, and "|" ends it: escape them.
esc() { printf '%s\n' "$1" | sed 's/[\\&|]/\\&/g'; }
sed -e "s|@PORT@|$port|g" -e "s|@RUN@|$(esc "$run/state")|g" \
    -e "s|@USER@|$(esc "$(id -un)")|g" -e "s|@NAME@|$SMB_NAME|g" \
    -e "s|@SHARE@|$SMB_SHARE|g" -e "s|@PATH@|$(esc "$share")|g" \
    "$here/SMB/SMB.CNF" >"$run/smb.conf"

# smbd makes itself a process group leader: when it ends it signals its
# whole group, which must therefore not be the caller's.  _NO_WINBINDD
# keeps its user lookups off a winbindd the host may run.
_NO_WINBINDD=1 smbd --foreground --debug-stdout -s "$run/smb.conf" \
    </dev/null >"$run/smbd.log" 2>&1 6>&- &
pid=$!
echo "$pid" >"$run/PID"
(
    n=0
    while alive "$pid" "$run/smb.conf"; do
        if [ $n -ge "$SMBD_LIFE" ]; then
            kill "$pid" 2>/dev/null || :
            break
        fi
        sleep 1
        n=$((n + 1))
    done
) </dev/null >/dev/null 2>&1 7>&- 8>&- 9>&- &
exec 6>&-

n=0
until ss -Hltnp "sport = :$port" | grep -qF "pid=$pid,"; do
    if ! alive "$pid" "$run/smb.conf" || [ $n -ge $((wait_secs * 10)) ]; then
        stop_server "$run"
        sed 's/^/smbd: /' "$run/smbd.log" >&2
        fail "smbd did not listen on 127.0.0.1:$port within ${wait_secs}s"
    fi
    sleep 0.1
    n=$((n + 1))
done
echo "$port" >"$run/PORT"
printf '%s: smbd %s serves %s as \\\\%s\\%s on 127.0.0.1:%s\n' \
    "$prog" "$pid" "$share" "$SMB_NAME" "$SMB_SHARE" "$port"
