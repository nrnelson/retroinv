"""MBOXCLI.PY - post request files to a RETROINV mailbox and print the
responses.

usage: python3 -I TEST/INTEG/MBOXCLI.PY [-t SECS] [-T FILE] DIR
       ITEM...

DIR is the mailbox directory, holding REQUEST and RESPONSE.  For each
ITEM STATION=FILE, in order: the bytes of FILE are written to
REQUEST/STATION.TMP, which is then renamed STATION.REQ, so that the
server never reads half a request; once RESPONSE/STATION.RSP appears,
within SECS seconds (default 120), it is read and removed, and printed
after a line "=STATION", its lines without CR.  The server removes a
request before it puts its response in place, so the request must be
gone by then.  STATION is the request's upper-case base name, 1 to 8
letters or digits.

Login tickets.  A ticket in a response ("TICKET=" and 32 upper-case hex
digits) is kept for its station and printed as TICKET=<t>, so the output
is the same on every run.  In a request FILE, @TICKET@ is replaced by
the last ticket of its own station, @TICKET:STA@ by the last ticket of
station STA, and a "-N" before the closing "@" takes the one N tickets
earlier (@TICKET-1@).  A response is zeroed, as a client must zero the
one that gave a ticket, before it is removed.  An ITEM "wait:SECS" waits
SECS seconds before the next one.  -T FILE writes every ticket seen,
one a line, for a search of the disks.

Exit status: 0 when every response came; 1 when one did not come in
time, its request was still there or a ticket was given twice (the
reason on standard error); 2 on a usage error or a file that cannot be
read or written.
"""

import getopt
import os
import re
import sys
import time

POLL = 0.1          # seconds between looks for a response
TICKET = re.compile(r'TICKET=([0-9A-F]{32})\b')
HOLDER = re.compile(r'@TICKET(?::([A-Z0-9]{1,8}))?(?:-([0-9]))?@')


def post(box, station, data, secs):
    """Post data as station's request and return its response; None when
    it did not come within secs seconds."""
    req = os.path.join(box, 'REQUEST', station)
    with open(req + '.TMP', 'wb') as f:
        f.write(data)
    os.rename(req + '.TMP', req + '.REQ')
    rsp = os.path.join(box, 'RESPONSE', station + '.RSP')
    end = time.monotonic() + secs
    while not os.path.exists(rsp):
        if time.monotonic() > end:
            return None
        time.sleep(POLL)
    with open(rsp, 'r+b') as f:
        got = f.read()
        f.seek(0)
        f.write(bytes(len(got)))
    os.remove(rsp)
    return got


def fill(data, station, tickets):
    """Return data with its @TICKET...@ marks replaced; a mark whose
    ticket does not exist is left, so the server refuses it."""
    def one(m):
        have = tickets.get(m.group(1) or station, [])
        back = int(m.group(2) or 0)
        return have[-1 - back] if back < len(have) else m.group(0)
    return HOLDER.sub(one, data.decode('latin-1')).encode('latin-1')


def main(argv):
    usage = __doc__.split('\n\n')[1]
    try:
        opts, args = getopt.getopt(argv, 't:T:')
        opts = dict(opts)
        secs = float(opts.get('-t', '120'))
    except (getopt.GetoptError, ValueError):
        args = []
    if not args[1:] or any(not re.match(r'^(wait:[0-9]+|[A-Z0-9]{1,8}=.+)$',
                                     a) for a in args[1:]):
        sys.stderr.write(usage + '\n')
        return 2
    box = args[0]
    tickets = {}
    for item in args[1:]:
        if item.startswith('wait:'):
            time.sleep(int(item[5:]))
            continue
        station, _, path = item.partition('=')
        try:
            with open(path, 'rb') as f:
                data = fill(f.read(), station, tickets)
            got = post(box, station, data, secs)
        except OSError as e:
            sys.stderr.write('mboxcli: %s\n' % e)
            return 2
        if got is None:
            sys.stderr.write('mboxcli: no response for %s within %g s\n'
                             % (station, secs))
            return 1
        if os.path.exists(os.path.join(box, 'REQUEST', station + '.REQ')):
            sys.stderr.write('mboxcli: the request of %s is still there\n'
                             % station)
            return 1
        text = got.decode('latin-1').replace('\r', '')
        for t in TICKET.findall(text):
            if any(t in h for h in tickets.values()):
                sys.stderr.write('mboxcli: %s was given a ticket seen '
                                 'before\n' % station)
                return 1
            tickets.setdefault(station, []).append(t)
        sys.stdout.write('=%s\n' % station)
        sys.stdout.write(TICKET.sub('TICKET=<t>', text))
        sys.stdout.flush()
    if '-T' in opts:
        with open(opts['-T'], 'w') as f:
            for have in tickets.values():
                f.write(''.join(t + '\n' for t in have))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
