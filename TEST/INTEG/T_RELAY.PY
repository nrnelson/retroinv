"""T_RELAY.PY - check the delay mode of LOSSY.PY and the event log check
of wganb.sh on the host.

usage: python3 -I TEST/INTEG/T_RELAY.PY

Both are the files beside this script.  For each delay of DELAYS, in ms,
"LOSSY.PY --delay MS" runs between two stations this program plays: A
listens, B connects to the relay, and A and B then each send FRAMES
frames at once, as fast as the relay takes them, of the lengths that
LENGTHS cycles through (an empty frame included, every SPLIT-th frame
written in two parts).  Every frame must reach the other station
unchanged, in order and only once, none sooner than the delay after it
was sent, and all of them within FRAMES / 2 delays of the first send:
a relay that delays the frames of a direction one after another, or
lets one direction wait for the other, takes FRAMES delays or more.
The station named by CLOSERS for the run then closes; the relay must
then close the other station's connection and exit 0 within WAIT
seconds, having written nothing to its log.

wganb_check of wganb.sh, sourced as wgagate.sh sources it, must pass a
crafted event log of WGANB.SCR's sessions among those of other
stations, and fail, saying why on lines starting "wganb: ", each log of
CASES that breaks one of its rules.

Exit status: 0 when every check passed; 1 otherwise (the failures on
standard error); 2 on a usage error.
"""

import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DELAYS = (10, 30)           # ms the relay is run with
CLOSERS = ('B', 'A')        # the station that closes first, per run
FRAMES = 200                # frames each station sends
LENGTHS = (60, 1514, 0, 64, 1500, 1, 590, 14)
SPLIT = 5                   # every SPLIT-th frame goes out in two writes
EARLY = 0.0005              # clock rounding: seconds a frame may be early
WAIT = 20                   # seconds any step of a run may take


class Fail(Exception):
    """A check that did not hold."""


def frame(way, i):
    """Return frame i of direction way ("A>B" or "B>A"), of the length
    LENGTHS gives it in turn: it starts with i, low byte first, then the
    direction, so that a frame of one byte or more is unlike every
    other frame of its direction and length (FRAMES is under 256), and
    one of three bytes or more unlike those of the other direction
    too."""
    n = LENGTHS[i % len(LENGTHS)]
    seed = (i * 7 + (way == 'B>A') * 101) & 0xFF
    head = struct.pack('<Hc', i, way[0].encode('ascii'))
    body = bytes((seed + j) & 0xFF for j in range(n))
    return (head + body)[:n]


def connect(port, relay):
    """Connect to the relay's port, trying again while it starts."""
    deadline = time.monotonic() + WAIT
    while True:
        try:
            return socket.create_connection(('127.0.0.1', port), WAIT)
        except OSError:
            if relay.poll() is not None:
                raise Fail('the relay exited %d before B connected'
                           % relay.returncode)
            if time.monotonic() > deadline:
                raise Fail('the relay does not listen on port %d' % port)
            time.sleep(0.05)


def free_port():
    """Return a TCP port of 127.0.0.1 that is free just now."""
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class Station:
    """One end of the relay: sends FRAMES frames of its direction, and
    receives the other direction's, each stamped with the time it was
    sent or received in full."""

    def __init__(self, name, sock):
        self.name = name
        self.sock = sock
        self.way = name + '>' + ('B' if name == 'A' else 'A')
        self.sent = []
        self.got = []
        self.buf = b''
        self.error = None

    def send(self):
        """Send this station's frames, the last part of each stamped."""
        try:
            for i in range(FRAMES):
                f = frame(self.way, i)
                data = struct.pack('>I', len(f)) + f
                if i % SPLIT == SPLIT - 1:
                    cut = 2 + len(f) // 2
                    self.sock.sendall(data[:cut])
                    data = data[cut:]
                self.sent.append(time.monotonic())
                self.sock.sendall(data)
        except OSError as e:
            self.error = 'station %s send: %s' % (self.name, e)

    def frames(self):
        """Parse the frames in the buffer, stamping each now."""
        while len(self.buf) >= 4:
            n = struct.unpack('>I', self.buf[:4])[0]
            if len(self.buf) < 4 + n:
                return
            self.got.append((time.monotonic(), self.buf[4:4 + n]))
            self.buf = self.buf[4 + n:]

    def receive(self):
        """Receive the other direction's FRAMES frames."""
        try:
            while len(self.got) < FRAMES:
                chunk = self.sock.recv(65536)
                if not chunk:
                    self.error = ('station %s: the relay closed after %d '
                                  'frames' % (self.name, len(self.got)))
                    return
                self.buf += chunk
                self.frames()
        except OSError as e:
            self.error = 'station %s receive: %s' % (self.name, e)

    def drain(self):
        """Read until the relay closes; fail on a frame beyond FRAMES."""
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                self.buf += chunk
        except OSError as e:
            raise Fail('station %s: the relay did not close its connection '
                       '(%s)' % (self.name, e))
        if self.buf or len(self.got) != FRAMES:
            raise Fail('station %s got more than %d frames'
                       % (self.name, FRAMES))


def check_frames(dst, src, ms):
    """Check the frames dst received against those src sent."""
    if len(dst.got) != FRAMES:
        raise Fail('%s: %d frames of %d arrived'
                   % (src.way, len(dst.got), FRAMES))
    for i, (t, f) in enumerate(dst.got):
        if f != frame(src.way, i):
            raise Fail('%s: frame %d of %d is not the one sent'
                       % (src.way, i + 1, FRAMES))
        late = (t - src.sent[i]) * 1000
        if late < ms - EARLY * 1000:
            raise Fail('%s: frame %d came %.2f ms after it was sent, '
                       'under %d ms' % (src.way, i + 1, late, ms))


def run(ms, closer, work):
    """Relay FRAMES frames each way with a delay of ms; closer closes."""
    log = os.path.join(work, 'LOSSY%d.LOG' % ms)
    port_b = free_port()
    listen_a = socket.socket()
    listen_a.bind(('127.0.0.1', 0))
    listen_a.listen(1)
    listen_a.settimeout(WAIT)
    stations = []
    relay = subprocess.Popen(
        [sys.executable, '-I', os.path.join(HERE, 'LOSSY.PY'), '--delay',
         str(ms), str(port_b), str(listen_a.getsockname()[1]), log],
        stdin=subprocess.DEVNULL)
    try:
        b = Station('B', connect(port_b, relay))
        stations.append(b)
        try:
            a = Station('A', listen_a.accept()[0])
        except OSError as e:
            raise Fail('the relay did not connect to A (%s)' % e)
        stations.append(a)
        listen_a.close()
        for s in stations:
            s.sock.settimeout(WAIT)
        threads = [threading.Thread(target=f, daemon=True)
                   for s in stations for f in (s.receive, s.send)]
        start = time.monotonic()
        for t in threads:
            t.start()
        for t in threads:
            t.join(WAIT)
        end = time.monotonic()
        for s in stations:
            if s.error:
                raise Fail(s.error)
        if any(t.is_alive() for t in threads):
            raise Fail('the frames did not all pass within %d s' % WAIT)
        check_frames(a, b, ms)
        check_frames(b, a, ms)
        last = max(a.got[-1][0], b.got[-1][0])
        if last - start > FRAMES / 2 * ms / 1000:
            raise Fail('the frames took %.0f ms, over %d delays of %d ms: '
                       'they waited for one another'
                       % ((last - start) * 1000, FRAMES // 2, ms))
        shut, other = (b, a) if closer == 'B' else (a, b)
        shut.sock.close()
        other.drain()
        try:
            rc = relay.wait(WAIT)
        except subprocess.TimeoutExpired:
            raise Fail('the relay did not exit once %s closed' % closer)
        if rc != 0:
            raise Fail('the relay exited %d' % rc)
        if not os.path.isfile(log) or os.path.getsize(log) != 0:
            raise Fail('the relay log is missing or not empty')
        return end - start
    finally:
        if relay.poll() is None:
            relay.kill()
            relay.wait()
        listen_a.close()
        for s in stations:
            s.sock.close()


# A crafted event log of the shape wgagate.sh's server writes: MBOX.SCR's
# mailbox and serial sessions, WGANB.SCR's two NetBIOS sessions, whose
# stations each OPEN line gives, then MBOXEND.SCR's mailbox session,
# which reuses RINBA's slot and is closed by the shutdown it asks for,
# and the stack line the server writes before it stops.
LOG_GOOD = '''2026-10-21 10:00:00 0 START RISRV
2026-10-21 10:00:05 6 OPEN RIMBOX
2026-10-21 10:00:05 6 REQ HELLO
2026-10-21 10:00:06 6 REQ BYE
2026-10-21 10:00:06 6 CLOSE
2026-10-21 10:00:07 5 OPEN
2026-10-21 10:00:07 5 REQ HELLO
2026-10-21 10:00:08 5 REQ BYE
2026-10-21 10:00:08 5 CLOSE
2026-10-21 10:01:00 1 OPEN RINBA
2026-10-21 10:01:00 1 REQ HELLO
2026-10-21 10:01:01 2 OPEN RINBB
2026-10-21 10:01:01 2 REQ HELLO
2026-10-21 10:01:01 1 REQ LOGIN
2026-10-21 10:01:01 1 LOGIN ADMIN
2026-10-21 10:01:02 2 REQ LOGIN
2026-10-21 10:01:02 2 LOGIN ADMIN
2026-10-21 10:01:02 1 REQ ITEM LIST
2026-10-21 10:02:05 2 REQ BYE
2026-10-21 10:02:05 2 CLOSE
2026-10-21 10:02:06 1 REQ BYE
2026-10-21 10:02:06 1 CLOSE
2026-10-21 10:02:30 1 OPEN RIMBOX
2026-10-21 10:02:30 1 REQ HELLO
2026-10-21 10:02:31 1 REQ LOGIN
2026-10-21 10:02:31 1 LOGIN ADMIN
2026-10-21 10:02:31 1 REQ PASSWD
2026-10-21 10:02:32 1 REQ ADMIN SHUTDOWN
2026-10-21 10:02:34 1 CLOSE
2026-10-21 10:02:35 0 STACK 2
2026-10-21 10:02:35 0 STOP 0
'''

# (what, the good log's lines replaced (old, new), text of the report)
CASES = (
    ('a session closed without BYE',
     (('2 REQ BYE', '2 REQ PING'),), 'without BYE'),
    ('a BYE after the close',
     (('2 REQ BYE\n2026-10-21 10:02:05 2 CLOSE',
       '2 CLOSE\n2026-10-21 10:02:05 2 REQ BYE'),), 'without BYE'),
    ('the BYE of another slot only',
     (('1 REQ BYE', '1 REQ PING'),), 'without BYE'),
    ('a session never closed',
     (('2026-10-21 10:02:05 2 CLOSE\n', ''),), 'not closed'),
    ('a session never closed whose slot another station takes',
     (('2026-10-21 10:02:06 1 CLOSE\n', ''),), 'not closed'),
    ('a session never closed whose slot a station of ours takes',
     (('2026-10-21 10:01:01 2 OPEN RINBB\n2026-10-21 10:01:01 2 REQ HELLO\n',
       ''),
      ('2026-10-21 10:01:02 2 REQ LOGIN\n2026-10-21 10:01:02 2 LOGIN ADMIN\n',
       ''),
      ('2026-10-21 10:02:05 2 REQ BYE\n2026-10-21 10:02:05 2 CLOSE\n'
       '2026-10-21 10:02:06 1 REQ BYE\n2026-10-21 10:02:06 1 CLOSE\n',
       '2026-10-21 10:02:05 1 OPEN RINBB\n2026-10-21 10:02:05 1 REQ BYE\n'
       '2026-10-21 10:02:05 1 CLOSE\n')),
     'not closed'),
    ('a BYE of the session before on the slot',
     (('2026-10-21 10:01:01 2 OPEN RINBB\n2026-10-21 10:01:01 2 REQ HELLO\n',
       ''),
      ('2026-10-21 10:01:02 2 REQ LOGIN\n2026-10-21 10:01:02 2 LOGIN ADMIN\n',
       ''),
      ('2026-10-21 10:02:05 2 REQ BYE\n2026-10-21 10:02:05 2 CLOSE\n', ''),
      ('2026-10-21 10:02:06 1 CLOSE\n',
       '2026-10-21 10:02:06 1 CLOSE\n2026-10-21 10:02:07 1 OPEN RINBB\n'
       '2026-10-21 10:02:07 1 REQ PING\n2026-10-21 10:02:08 1 CLOSE\n')),
     'without BYE'),
    ('a retried call',
     (('2026-10-21 10:01:00 1 OPEN RINBA',
       '2026-10-21 10:00:59 3 OPEN RINBA\n2026-10-21 10:00:59 3 REQ BYE\n'
       '2026-10-21 10:00:59 3 CLOSE\n2026-10-21 10:01:00 1 OPEN RINBA'),),
     'OPEN'),
    ('a session missing',
     (('2 OPEN RINBB', '2 OPEN RIOTHER'),), 'OPEN'),
)


def wganb(log):
    """Run wganb_check over the log file; return (status, stdout, stderr)."""
    p = subprocess.run(
        ['sh', '-c', 'set -eu; here=$1; . "$here/wganb.sh"; '
         'wganb_check "$2"', 'sh', HERE, log],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, universal_newlines=True, timeout=WAIT)
    return p.returncode, p.stdout, p.stderr


def check_wganb(work):
    """Check wganb_check against LOG_GOOD and each log of CASES."""
    errors = []
    path = os.path.join(work, 'SRV.LOG')
    with open(path, 'w', newline='') as f:
        f.write(LOG_GOOD.replace('\n', '\r\n'))
    rc, out, err = wganb(path)
    if rc != 0 or out or err:
        errors.append('wganb_check fails the good log (%d): %s'
                      % (rc, (out + err).strip()))
    for what, edits, why in CASES + (('a missing log', (), 'no event log'),):
        text = LOG_GOOD
        for old, new in edits:
            if text.count(old) != 1:
                raise Fail('case "%s": "%s" is not one line of the log'
                           % (what, old))
            text = text.replace(old, new)
        if edits:
            with open(path, 'w', newline='') as f:
                f.write(text.replace('\n', '\r\n'))
        else:
            os.remove(path)
        rc, out, err = wganb(path)
        lines = err.splitlines()
        if (rc == 0 or out or not lines or why not in err or
                any(not l.startswith('wganb: ') for l in lines)):
            errors.append('wganb_check with %s: exit %d, said %r'
                          % (what, rc, (out + err).strip()))
    if errors:
        raise Fail('\n'.join(errors))


def main(argv):
    if argv:
        sys.stderr.write('t_relay: no arguments are taken\n')
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    status = 0
    with tempfile.TemporaryDirectory() as work:
        for ms, closer in zip(DELAYS, CLOSERS):
            try:
                secs = run(ms, closer, work)
                print('t_relay: --delay %d: %d frames each way in '
                      '%.0f ms' % (ms, FRAMES, secs * 1000))
            except (Fail, OSError) as e:
                sys.stderr.write('t_relay: --delay %d: %s\n' % (ms, e))
                status = 1
        try:
            check_wganb(work)
        except (Fail, OSError, subprocess.SubprocessError) as e:
            sys.stderr.write('t_relay: %s\n' % e)
            status = 1
    print('t_relay: %s' % ('PASS' if status == 0 else 'FAIL'))
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
