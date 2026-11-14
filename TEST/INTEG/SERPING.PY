"""SERPING.PY - time a RETROINV server's answers on a serial line.

usage: python3 -I TEST/INTEG/SERPING.PY SOCKET STOPFILE LIMIT USER PASSWORD

SOCKET is a unix socket that carries the server's serial line (QEMU's
chardev of the guest's port).  Over it the program says HELLO as the
station RIPING and logs in as USER with PASSWORD, then sends a PING every
PERIOD seconds, each once the answer to the last has come, until the
file STOPFILE exists; then it sends ADMIN SHUTDOWN, which USER must be
allowed, and reads its answer.  It prints one line per PING, "PING n
secs", n counting from 1 and secs the host's time from the request to
the end of its answer, and a last line "PINGS n max secs".

Every answer must come within WAIT seconds; HELLO, LOGIN and ADMIN
SHUTDOWN must be answered +OK, and each PING +OK within LIMIT seconds.

Exit status: 0 when every answer was as required; 1 otherwise (the
reason on standard error); 2 on a usage error or a socket that cannot
be reached.
"""

import os
import socket
import sys
import time

PERIOD = 0.5        # seconds from one PING to the next
WAIT = 60           # seconds an answer may take before the run fails


class Fail(Exception):
    """An answer that is not as required: exit status 1."""


class Line:
    """The serial line: request lines out, answer lines in."""

    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.buf = b''

    def ask(self, request):
        """Send request; return its one-line answer and the seconds it
        took."""
        t0 = time.monotonic()
        self.sock.sendall(request.encode('ascii') + b'\r\n')
        while b'\n' not in self.buf:
            left = t0 + WAIT - time.monotonic()
            if left <= 0:
                raise Fail('no answer to %s within %d s' % (request, WAIT))
            self.sock.settimeout(left)
            try:
                got = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not got:
                raise Fail('the line closed after %s' % request)
            self.buf += got
        line, _, self.buf = self.buf.partition(b'\n')
        return line.rstrip(b'\r').decode('latin-1'), time.monotonic() - t0


def expect_ok(line, request):
    """Send request and fail unless its answer starts +OK."""
    answer, _ = line.ask(request)
    if not answer.startswith('+OK'):
        raise Fail('%s answered %r' % (request.split()[0], answer))


def main(argv):
    if len(argv) != 5:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    path, stop, limit, user, password = argv
    try:
        limit = float(limit)
        line = Line(path)
    except (ValueError, OSError) as e:
        sys.stderr.write('serping: %s\n' % e)
        return 2
    n = 0
    worst = 0.0
    try:
        expect_ok(line, 'HELLO RIPING')
        expect_ok(line, 'LOGIN %s %s' % (user, password))
        due = time.monotonic()
        while not os.path.exists(stop):
            answer, secs = line.ask('PING')
            n += 1
            worst = max(worst, secs)
            print('PING %d %.3f' % (n, secs), flush=True)
            if answer != '+OK':
                raise Fail('PING %d answered %r' % (n, answer))
            if secs > limit:
                raise Fail('PING %d took %.3f s, over %g s' % (n, secs, limit))
            due += PERIOD
            time.sleep(max(0.0, due - time.monotonic()))
        expect_ok(line, 'ADMIN SHUTDOWN')
    except (Fail, OSError) as e:
        sys.stderr.write('serping: %s\n' % e)
        return 1
    finally:
        print('PINGS %d max %.3f' % (n, worst))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
