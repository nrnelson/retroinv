"""MBXRELAY.PY - relay the network of a Windows machine to a RETROINV
server's machine, every frame late, and watch the mailbox's files cross.

usage: python3 -I TEST/WIN/MBXRELAY.PY MS LISTEN_PORT TARGET_PORT WINMON
       PASSWORD MARK TICKETS

The Windows machine connects to 127.0.0.1:LISTEN_PORT, a QEMU stream
netdev (a 4-byte big-endian length, then the Ethernet frame); the relay
then connects to the server's machine, which listens on TARGET_PORT, and
passes the frames both ways, each no sooner than MS milliseconds after
it was read, in order and none lost, as TEST/INTEG/LOSSY.PY --delay does
and for the same reason, until either side closes.

The frames the Windows machine sends carry the request files it writes
on the server's share.  Every one that holds PASSWORD must also hold
TICKET=NEW, the LOGIN that asks for a login ticket, and come before the
first frame holding MARK, a request line the client sends after that
LOGIN.  At that first MARK the relay waits DUMPWAIT seconds and then
saves the Windows machine's memory through its QEMU monitor WINMON, the
machine stopped meanwhile, into TICKETS with the extension .MEM, which
must not hold PASSWORD.  Every ticket the frames carry either way
(TICKET= and PROTO_TKTLEN hex digits) goes into TICKETS, one a line, for
the caller to look for on the server's disk.  The program prints, as the
password is seen, the search ends and the relay ends, its seconds from
the start.

Exit status: 0 when the Windows machine sent PASSWORD in a LOGIN with
TICKET=NEW only, before MARK, which came, and the memory searched then
did not hold it; 1 otherwise (the reason on standard error); 2 on a
usage error or a port that cannot be reached.
"""

import os
import queue
import re
import socket
import struct
import sys
import threading
import time

CONNECT_SECS = 60   # how long to try connecting to the server's machine
DUMPWAIT = 0.5      # seconds from MARK to the memory search
TICKET = re.compile(rb'TICKET=([0-9A-F]{32})')
NEW = b'TICKET=NEW'


class Fail(Exception):
    """A frame or a search that is not as required: exit status 1."""


def frames(src):
    """Yield each frame read from src, with its length in front, until
    src closes."""
    buf = b''
    while True:
        chunk = src.recv(65536)
        if not chunk:
            return
        buf += chunk
        while len(buf) >= 4:
            n = 4 + struct.unpack('>I', buf[:4])[0]
            if len(buf) < n:
                break
            yield buf[:n]
            buf = buf[n:]


def close(*socks):
    """Shut down each of socks, so that a pump blocked on one ends."""
    for s in socks:
        try:
            s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass


class Watch:
    """What the frames carried, shared by the two pumps under lock."""

    def __init__(self, monitor, password, mark, tickets, t0):
        self.monitor = monitor
        self.password = password.encode('latin-1')
        self.mark = mark.encode('latin-1')
        self.dump = os.path.splitext(tickets)[0] + '.MEM'
        self.t0 = t0
        self.lock = threading.Lock()
        self.tickets = []
        self.seen = 0           # frames that held the password
        self.marked = False     # MARK came
        self.error = None       # the first Fail

    def secs(self):
        return time.monotonic() - self.t0

    def fail(self, why):
        if self.error is None:
            self.error = why

    def look(self, frame, from_win):
        """Note what frame holds; True when it is the first MARK."""
        with self.lock:
            for t in TICKET.findall(frame):
                if t not in self.tickets:
                    self.tickets.append(t)
            if not from_win:
                return False
            if self.password in frame:
                self.seen += 1
                print('mbxrelay: the password crossed at %.1f s' %
                      self.secs(), flush=True)
                if self.marked:
                    self.fail('the password crossed after %s' %
                              self.mark.decode('latin-1'))
                elif NEW not in frame:
                    self.fail('the password crossed without TICKET=NEW')
            if self.mark in frame and not self.marked:
                self.marked = True
                return True
        return False

    def command(self, mon, cmd):
        """Send cmd and return the monitor's answer, up to its prompt."""
        if cmd:
            mon.sendall(cmd.encode('ascii') + b'\n')
        got = b''
        while not got.endswith(b'(qemu) '):
            d = mon.recv(4096)
            if not d:
                raise Fail("the Windows machine's monitor closed")
            got += d
        return got.decode('latin-1')

    def search(self):
        """Fail when the password is in the Windows machine's memory."""
        time.sleep(DUMPWAIT)
        mon = socket.socket(socket.AF_UNIX)
        mon.settimeout(CONNECT_SECS)
        mon.connect(self.monitor)
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
        at = mem.find(self.password)
        if at >= 0:
            raise Fail('the password is in memory at %05Xh' % at)
        print('mbxrelay: no password in %d bytes at %.1f s' %
              (len(mem), self.secs()), flush=True)


def pump(src, dst, secs, watch, from_win):
    """Pass the frames from src to dst, each no sooner than secs after it
    was read, until either side closes, then close both; search the
    memory at the first MARK.  A thread of its own sends them, so that a
    frame's wait does not hold up the reading of the next."""
    due = queue.Queue()

    def send():
        try:
            while True:
                item = due.get()
                if item is None:
                    break
                wait = item[0] - time.monotonic()
                if wait > 0:
                    time.sleep(wait)
                dst.sendall(item[1])
        except OSError:
            pass
        close(src, dst)

    sender = threading.Thread(target=send, daemon=True)
    sender.start()
    try:
        for rec in frames(src):
            due.put((time.monotonic() + secs, rec))
            if watch.look(rec[4:], from_win):
                try:
                    watch.search()
                except (Fail, OSError) as e:
                    with watch.lock:
                        watch.fail(str(e))
    except OSError:
        pass
    due.put(None)
    sender.join()


def connect(port):
    """A connection to the server's machine on port, made within
    CONNECT_SECS."""
    deadline = time.monotonic() + CONNECT_SECS
    while True:
        try:
            return socket.create_connection(('127.0.0.1', port))
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.1)


def main(argv):
    if len(argv) != 7:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    ms, listen, target, monitor, password, mark, tickets = argv
    try:
        secs = int(ms) / 1000.0
        server = socket.socket()
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('127.0.0.1', int(listen)))
        server.listen(1)
    except (ValueError, OSError) as e:
        sys.stderr.write('mbxrelay: %s\n' % e)
        return 2
    t0 = time.monotonic()
    watch = Watch(monitor, password, mark, tickets, t0)
    win, _ = server.accept()
    server.close()
    try:
        srv = connect(int(target))
    except OSError as e:
        sys.stderr.write('mbxrelay: the server machine: %s\n' % e)
        return 2
    for s in (win, srv):
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    ways = [(win, srv, True), (srv, win, False)]
    threads = [threading.Thread(target=pump, args=(a, b, secs, watch, w),
                                daemon=True) for a, b, w in ways]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print('mbxrelay: the relay ended at %.1f s' % watch.secs(), flush=True)
    with open(tickets, 'wb') as f:
        for t in watch.tickets:
            f.write(t + b'\n')
    if watch.error is None and not watch.seen:
        watch.fail('the password never crossed')
    if watch.error is None and not watch.marked:
        watch.fail('no %s crossed' % mark)
    if watch.error is not None:
        sys.stderr.write('mbxrelay: %s\n' % watch.error)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
