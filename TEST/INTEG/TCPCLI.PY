"""TCPCLI.PY - drive TCP sessions to a server by a send/expect script.

usage: python3 -I TEST/INTEG/TCPCLI.PY [-t SECS] [-o FILE] HOST PORT [SCRIPT]

Runs the steps of SCRIPT (standard input without it), one per line, in
order; blank lines and lines starting with '#' are skipped.  S names a
session, any word, open from its "open" step until its "close" or "eof"
step.  TEXT is the rest of the line after S and one space, with the
escapes \\r, \\n, \\t, \\\\ and \\xHH; "*N" after it repeats it N times.

  open S          connect S to HOST:PORT
  send S TEXT     send TEXT on S
  expect S TEXT   receive TEXT on S, byte for byte, next
  until S TEXT    receive on S up to and including the first TEXT, and
                  add what came to the transcript
  refused S [TEXT]
                  connect S, send TEXT if given, then receive no bytes
                  and EOF or a reset: a server with no free socket
  eof S           receive no more bytes on S, then EOF or a reset
  close S         close S
  run CMD...      run a host command, its words split as by the shell
                  and only $NAME and ${NAME} of the environment expanded;
                  it must exit 0
  wait FILE N TEXT
                  wait until FILE, its $NAME and ${NAME} expanded as by
                  "run", holds N lines that contain TEXT

Every receive and every "wait" must end within SECS seconds (default
10).  The transcript, FILE (standard output without -o), holds
each line an "until" step received, its CR LF or LF removed, after
"S< ".  A connection that QEMU's user network forwards is accepted at
once, whatever the guest does, and a guest's refusal shows only as an
EOF or a reset later, which is why "refused" connects first.

Exit status: 0 when every step passed, 1 at the first that failed (its
line, and the bytes received, on standard error), 2 on a usage error,
a bad step or a connection that cannot be made.
"""

import getopt
import os
import shlex
import socket
import subprocess
import sys
import time

POLL = 0.2          # seconds between the reads of a "wait"


class Fail(Exception):
    """A step that did not pass: exit status 1."""


class Bad(Exception):
    """A usage, script or connection error: exit status 2."""


def unescape(text):
    """TEXT with its escapes resolved and its "*N" repeat applied."""
    times = 1
    head, star, count = text.rpartition('*')
    if star and count.isdigit():
        text, times = head, int(count)
    out = bytearray()
    i = 0
    while i < len(text):
        c = text[i]
        if c != '\\':
            out += c.encode('latin-1')
            i += 1
            continue
        e = text[i + 1:i + 2]
        if e in ('r', 'n', 't', '\\'):
            out += {'r': b'\r', 'n': b'\n', 't': b'\t', '\\': b'\\'}[e]
            i += 2
        elif e == 'x':
            out.append(int(text[i + 2:i + 4], 16))
            i += 4
        else:
            raise Bad('unknown escape \\' + e)
    return bytes(out) * times


class Client:
    """The sessions of one script run."""

    def __init__(self, host, port, secs, out):
        self.addr = (host, port)
        self.secs = secs
        self.out = out
        self.socks = {}
        self.held = {}

    def connect(self, s):
        if s in self.socks:
            raise Bad('session %s is open' % s)
        try:
            k = socket.create_connection(self.addr, self.secs)
        except OSError as e:
            raise Bad('cannot connect %s: %s' % (s, e))
        self.socks[s] = k
        self.held[s] = b''

    def sock(self, s):
        if s not in self.socks:
            raise Bad('session %s is not open' % s)
        return self.socks[s]

    def send(self, s, text):
        """Send TEXT on S; a peer that has ended S is left to the next
        receive to find."""
        try:
            self.sock(s).sendall(text)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def recv(self, s, want):
        """Receive on S until WANT bytes are held: True, or False at EOF
        or a reset."""
        k = self.sock(s)
        end = time.monotonic() + self.secs
        while len(self.held[s]) < want:
            left = end - time.monotonic()
            if left <= 0:
                raise Fail('%s: timed out after %r' % (s, self.held[s]))
            k.settimeout(left)
            try:
                d = k.recv(4096)
            except socket.timeout:
                continue
            except ConnectionResetError:
                d = b''
            if not d:
                return False
            self.held[s] += d
        return True

    def ended(self, s):
        """S gets no bytes, then EOF or a reset; S is closed."""
        if self.recv(s, len(self.held[s]) + 1):
            raise Fail('%s: bytes before the end: %r' % (s, self.held[s]))
        self.close(s)

    def close(self, s):
        self.sock(s).close()
        del self.socks[s]
        del self.held[s]

    def wait(self, name, n, text):
        """Wait until file NAME holds N lines that contain TEXT."""
        end = time.monotonic() + self.secs
        while True:
            with open(name, 'rb') as f:
                if sum(text in t for t in f) >= n:
                    return
            if time.monotonic() >= end:
                raise Fail('%s: not %d lines with %r' % (name, n, text))
            time.sleep(POLL)

    def until(self, s, text):
        """Receive on S through TEXT, into the transcript."""
        self.sock(s)
        while text not in self.held[s]:
            if not self.recv(s, len(self.held[s]) + 1):
                raise Fail('%s: ended after %r' % (s, self.held[s]))
        n = self.held[s].index(text) + len(text)
        for t in self.held[s][:n].splitlines():
            self.out.write('%s< %s\n' % (s, t.decode('latin-1')))
        self.held[s] = self.held[s][n:]

    def step(self, line):
        words = line.split()
        parts = line.split(' ', 2)
        rest = parts[2] if len(parts) == 3 else ''
        op = words[0]
        s = words[1] if len(words) > 1 else ''
        if op == 'run':
            cmd = [os.path.expandvars(w) for w in shlex.split(line)[1:]]
            if not cmd:
                raise Bad('no command')
            if subprocess.run(cmd, check=False).returncode:
                raise Fail('the command failed')
        elif op == 'wait':
            parts = line.split(' ', 3)
            if len(parts) < 4 or not parts[2].isdigit():
                raise Bad('bad step')
            self.wait(os.path.expandvars(parts[1]), int(parts[2]),
                      unescape(parts[3]))
        elif not s:
            raise Bad('no session')
        elif op == 'open' and len(words) == 2:
            self.connect(s)
        elif op == 'send' and rest:
            self.sock(s).sendall(unescape(rest))
        elif op == 'expect' and rest:
            text = unescape(rest)
            ok = self.recv(s, len(text))
            if not ok or self.held[s][:len(text)] != text:
                raise Fail('%s: got %r' % (s, self.held[s]))
            self.held[s] = self.held[s][len(text):]
        elif op == 'until' and rest:
            self.until(s, unescape(rest))
        elif op == 'refused':
            self.connect(s)
            self.send(s, unescape(rest))
            self.ended(s)
        elif op == 'eof' and len(words) == 2:
            self.ended(s)
        elif op == 'close' and len(words) == 2:
            self.close(s)
        else:
            raise Bad('bad step')


def main(argv):
    try:
        opts, argv = getopt.getopt(argv, 't:o:')
        opts = dict(opts)
        secs = float(opts.get('-t', 10))
    except (getopt.GetoptError, ValueError):
        argv = []
    if len(argv) not in (2, 3) or not argv[1].isdigit():
        print(__doc__.split('\n\n')[1], file=sys.stderr)
        return 2
    try:
        script = (open(argv[2], encoding='latin-1') if len(argv) == 3
                  else sys.stdin)
        out = open(opts['-o'], 'w') if '-o' in opts else sys.stdout
    except OSError as e:
        print('tcpcli: %s' % e, file=sys.stderr)
        return 2
    client = Client(argv[0], int(argv[1]), secs, out)
    for n, line in enumerate(script, 1):
        line = line.rstrip('\r\n')
        if not line.strip() or line.startswith('#'):
            continue
        try:
            client.step(line)
        except (Fail, OSError) as e:
            print('tcpcli: line %d: %s: %s' % (n, line, e), file=sys.stderr)
            return 1
        except (Bad, ValueError) as e:
            print('tcpcli: line %d: %s: %s' % (n, line, e), file=sys.stderr)
            return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
