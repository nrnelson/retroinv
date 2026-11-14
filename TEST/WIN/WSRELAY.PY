"""WSRELAY.PY - join the serial line of a Windows machine to that of a
RETROINV server for one session.

usage: python3 -I TEST/WIN/WSRELAY.PY SRVSOCK WINSOCK WINMON SECS PASSWORD
       WINLOG MARK COUNT [SECRET...]

SRVSOCK and WINSOCK are unix sockets the program makes and listens on,
for QEMU's chardevs of the server machine's port and of the Windows
machine's, which connect to them; WINMON is the Windows machine's QEMU
monitor.  The program then:

1. waits for the server's machine and sends it "HELLO RIRELAY" every
   PROBE seconds, each after a CR LF that ends what a port opened late
   kept of the one before, until a line comes back: the server serves.
   After SETTLE seconds for the answers still due it sends BYE, waits
   for its +OK and SETTLE seconds more, so that the server has ended
   the session and opened its port again;
2. waits for the Windows machine and passes on each line's bytes to the
   other until the Windows machine closes its line, writing the bytes it
   sent into WINLOG.  At each line starting with MARK that the Windows
   machine sends, it waits DUMPWAIT seconds and then, before it passes
   on another byte, saves the machine's memory through WINMON, into
   WINLOG with the extension .MEM, and requires that PASSWORD and each
   SECRET (the passwords a key script sets) be nowhere in it.  The
   machine is stopped while its memory is saved, so that the copy is of
   one moment and the guest sees no time pass while QEMU writes it;
3. after SETTLE seconds says HELLO, logs in as ADMIN with PASSWORD and
   sends ADMIN SHUTDOWN, each answered +OK, and waits for the server's
   machine to close its line.

A line starting with FLOOD that the Windows machine sends is not passed
on: the program answers it with FLOODLEN bytes, more than the receive
queue of CCOMM.C (CC_RXQ) holds, at the line's rate.

The bytes the Windows machine sent must hold no NUL, as CCOMM.C sends
none of the zeros it fills its queue with, hold COUNT lines starting with
MARK (TEST/WSERT.C sends a PING after its LOGIN and one after its first
link's close; INVWIN each ITEM GET after its LOGIN, or with
TEST/WIN/INVUSER.KEY each USER LIST it fetches) and end with the BYE
of its session's close.  Each step must end within SECS
seconds.  The program prints, as each step and each search ends, its
seconds from the start.

Exit status: 0 when everything went as required; 1 otherwise (the reason
on standard error); 2 on a usage error or a socket that cannot be made.
"""

import os
import re
import select
import socket
import sys
import time

PROBE = 2.0         # seconds from one probing HELLO to the next
SETTLE = 2.0        # seconds the server gets to answer or reopen its port
LINE_RATE = 960     # bytes a second of a 9600-baud line, ten bits a byte
FIFO = 16           # the most bytes either line passes on at once
FLOOD = b'FLOOD'    # what a line the Windows machine sends starts with
                    # to be answered with a flood
FLOODLEN = 5120     # bytes of the flood
PACE = 0.005        # seconds between two looks at the bytes due
DUMPWAIT = 0.5      # seconds from a MARK line's end to the memory
                    # search, by which Windows waits for the answer, its
                    # queue wiped


class Fail(Exception):
    """A step that did not go as required: exit status 1."""


class Line:
    """One machine's line, accepted on a listening socket."""

    def __init__(self, path):
        self.lsock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.lsock.bind(path)
        self.lsock.listen(1)
        self.sock = None
        self.buf = b''

    def accept(self, end, what):
        """Take the machine's connection, before the time end."""
        self.lsock.settimeout(max(0.0, end - time.monotonic()))
        try:
            self.sock, _ = self.lsock.accept()
        except socket.timeout:
            raise Fail('the %s machine did not connect' % what) from None

    def recv(self, end):
        """The bytes that come before the time end: b'' when none came,
        None once the machine has closed its line."""
        self.sock.settimeout(max(0.001, end - time.monotonic()))
        try:
            return self.sock.recv(4096) or None
        except socket.timeout:
            return b''

    def line(self, end):
        """The next line, without its CR LF, or None when none came before
        the time end."""
        while b'\n' not in self.buf:
            if time.monotonic() >= end:
                return None
            got = self.recv(end)
            if got is None:
                raise Fail('the server machine closed its line')
            self.buf += got
        line, _, self.buf = self.buf.partition(b'\n')
        return line.rstrip(b'\r').decode('latin-1')

    def drain(self, secs):
        """Drop what comes in the next secs seconds."""
        end = time.monotonic() + secs
        while self.line(end) is not None:
            pass
        self.buf = b''

    def ask(self, request, end):
        """Send request and fail unless its answer, before the time end,
        starts +OK."""
        self.sock.sendall(request.encode('ascii') + b'\r\n')
        answer = self.line(end)
        if answer is None or not answer.startswith('+OK'):
            raise Fail('%s answered %r' % (request.split()[0], answer))


def probe(srv, end):
    """Step 1."""
    while True:
        srv.sock.sendall(b'\r\nHELLO RIRELAY\r\n')
        if srv.line(min(end, time.monotonic() + PROBE)) is not None:
            break
        if time.monotonic() >= end:
            raise Fail('the server never answered')
    srv.drain(SETTLE)
    srv.ask('BYE', end)
    srv.drain(SETTLE)


class Memory:
    """The Windows machine's memory, read through its QEMU monitor."""

    def __init__(self, path, dump, secrets, t0):
        self.path = path
        self.dump = dump
        self.secrets = [s.encode('latin-1') for s in secrets]
        self.t0 = t0
        self.searches = 0

    def command(self, mon, cmd):
        """Send cmd and return the monitor's answer, up to its prompt."""
        if cmd:
            mon.sendall(cmd.encode('ascii') + b'\n')
        got = b''
        while not got.endswith(b'(qemu) '):
            d = mon.recv(4096)
            if not d:
                raise Fail('the Windows machine\'s monitor closed')
            got += d
        return got.decode('latin-1')

    def search(self, secs):
        """Fail when a secret is in memory."""
        time.sleep(DUMPWAIT)
        mon = socket.socket(socket.AF_UNIX)
        mon.settimeout(secs)
        mon.connect(self.path)
        with mon:
            self.command(mon, '')
            size = re.search(r'base memory: (\d+)',
                             self.command(mon, 'info memory_size_summary'))
            if size is None:
                raise Fail('the monitor gave no memory size')
            self.command(mon, 'stop')
            try:
                self.command(mon, 'pmemsave 0 %s "%s"' % (size.group(1),
                                                         self.dump))
            finally:
                self.command(mon, 'cont')
        with open(self.dump, 'rb') as f:
            mem = f.read()
        self.searches += 1
        for i, secret in enumerate(self.secrets):
            at = mem.find(secret)
            if at >= 0:
                raise Fail('password %d is in memory at %05Xh at search %d' %
                           (i + 1, at, self.searches))
        print('wsrelay: no password in %d bytes at search %d, %.1f s' %
              (len(mem), self.searches, time.monotonic() - self.t0),
              flush=True)


def relay(srv, win, end, log, memory, mark):
    """Step 2: the bytes the Windows machine sent.  Each line carries at
    most LINE_RATE bytes a second each way, as the test's 9600-baud line
    would, where QEMU's ports would pass a whole response at once."""
    sent = bytearray()
    scan = 0
    due = {srv.sock: bytearray(), win.sock: bytearray()}
    credit = dict.fromkeys(due, 0.0)
    last = time.monotonic()
    while True:
        now = time.monotonic()
        left = end - now
        if left <= 0:
            raise Fail('the Windows machine did not close its line')
        # The credit of a line grows with time to a FIFO's worth.
        for s, q in due.items():
            credit[s] = min(credit[s] + (now - last) * LINE_RATE, FIFO)
            n = min(len(q), int(credit[s]))
            if n:
                s.sendall(q[:n])
                del q[:n]
                credit[s] -= n
        last = now
        wait = PACE if due[srv.sock] or due[win.sock] else left
        ready, _, _ = select.select(list(due), [], [], min(wait, left))
        for s in ready:
            got = s.recv(4096)
            if s is win.sock:
                if not got:
                    srv.sock.sendall(due[srv.sock] + sent[scan:])
                    log.write(sent)
                    return bytes(sent)
                sent += got
                eol = sent.find(b'\n', scan)
                while eol >= 0:
                    if sent.startswith(FLOOD, scan):
                        due[win.sock] += b'x' * FLOODLEN
                    else:
                        due[srv.sock] += sent[scan:eol + 1]
                    if sent[scan:eol].startswith(mark):
                        memory.search(left)
                        last = time.monotonic()
                    scan = eol + 1
                    eol = sent.find(b'\n', scan)
            elif not got:
                raise Fail('the server machine closed its line')
            else:
                due[win.sock] += got


def main(argv):
    if len(argv) < 8:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    srvpath, winpath, monpath, secs, password, logpath, mark, count = \
        argv[:8]
    try:
        secs = float(secs)
        count = int(count)
        mark = mark.encode('ascii')
        srv = Line(srvpath)
        win = Line(winpath)
        log = open(logpath, 'wb')
    except (ValueError, OSError) as e:
        sys.stderr.write('wsrelay: %s\n' % e)
        return 2
    t0 = time.monotonic()
    memory = Memory(monpath, os.path.splitext(logpath)[0] + '.MEM',
                    [password] + argv[8:], t0)
    try:
        with log:
            srv.accept(t0 + secs, 'server')
            probe(srv, time.monotonic() + secs)
            print('wsrelay: the server serves at %.1f s' %
                  (time.monotonic() - t0), flush=True)
            win.accept(time.monotonic() + secs, 'Windows')
            sent = relay(srv, win, time.monotonic() + secs, log, memory,
                         mark)
            print('wsrelay: the Windows machine ended at %.1f s' %
                  (time.monotonic() - t0), flush=True)
        if b'\0' in sent:
            raise Fail('the Windows machine sent a NUL')
        if memory.searches != count:
            raise Fail('the Windows machine sent %d lines %s, not %d' %
                       (memory.searches, mark.decode('ascii'), count))
        if not sent.endswith(b'\nBYE\r\n'):
            raise Fail('the Windows machine did not end with BYE: %r' %
                       sent[-16:])
        srv.drain(SETTLE)
        end = time.monotonic() + secs
        srv.ask('HELLO RIRELAY', end)
        srv.ask('LOGIN ADMIN %s' % password, end)
        srv.ask('ADMIN SHUTDOWN', end)
        while srv.recv(end) is not None:
            if time.monotonic() >= end:
                raise Fail('the server machine did not end')
        print('wsrelay: the server ended at %.1f s' %
              (time.monotonic() - t0), flush=True)
    except (Fail, OSError) as e:
        sys.stderr.write('wsrelay: %s\n' % e)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
