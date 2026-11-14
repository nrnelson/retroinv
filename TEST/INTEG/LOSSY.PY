"""LOSSY.PY - relay a QEMU stream netdev link, and either lose the frames
that deadlock NetBEUI when both stations' response timers are equal or
delay every frame.

usage: python3 -I TEST/INTEG/LOSSY.PY [--delay MS] LISTEN_PORT TARGET_PORT LOG

VM B connects to 127.0.0.1:LISTEN_PORT; the relay then connects to VM A,
which listens on 127.0.0.1:TARGET_PORT, and passes the frames both ways
(QEMU's stream framing: a 4-byte big-endian length, then the Ethernet
frame) until either side closes.

With --delay, every frame is passed unchanged, each no sooner than MS
milliseconds after it was read, in order in each direction and nothing
lost; each direction waits on its own, and a frame's wait does not add to
the next one's.  Under QEMU a NetBEUI station sometimes ignores the answer
to its own poll when it comes back within a millisecond, while the station
is still dealing with the poll it sent; an answer that comes 10 ms late is
taken.  LOG is created and stays empty.

Without --delay, two kinds of frame are lost, the loss behind the deadlock
that the unequal T1 timers of net330.sh prevent:
- the first resent bulk data from B (a NetBIOS DATA_FIRST_MIDDLE or
  DATA_ONLY_LAST I-frame over BULK_MIN bytes with its re-synch indicator
  set), and the next frame from B if it is a DATA I-frame too.  A has
  then acknowledged B's earlier frames and B its, so both stations' T1
  timers start within a millisecond.
- after that, polls that cross, as a station can ignore a frame that
  arrives while it is still dealing with one it has sent: an LLC command
  with the poll bit set (an RR, RNR or REJ poll, or a DISC) is held
  WINDOW seconds, and when the other station sends one in that time,
  both are dropped.  WINDOW spans the one 55 ms clock tick by which equal
  timers can differ, with room for a loaded host; the first polls of the
  unequal timers come NB_T1_B - NB_T1_A (net330.sh) apart, give or take a
  tick, well outside WINDOW.
LOG then gets one line per dropped frame: the seconds since the start,
the direction ("A>B" or "B>A") and "bulk" or "poll".

The relay never ends on its own while both sides stay connected;
run2vm.sh kills it.
"""

import queue
import socket
import struct
import sys
import threading
import time

WINDOW = 0.150      # seconds a poll is held
BULK_MIN = 1000     # bytes: a bulk data frame is longer
CONNECT_SECS = 60   # how long to try connecting to VM A

NB_DELIM = b"\xff\xef"
NB_DATA = (0x15, 0x16)  # DATA_FIRST_MIDDLE, DATA_ONLY_LAST


def classify(frame):
    """Return (data, resynch, poll) for an Ethernet 802.2 NetBEUI frame:
    data is True for a NetBIOS DATA I-frame, resynch its re-synch
    indicator, poll True for a command S- or U-frame with the poll bit."""
    llc = frame[14:]
    if len(llc) < 3 or llc[0] != 0xF0:
        return False, False, False
    command = (llc[1] & 1) == 0
    ctl = llc[2]
    if ctl & 1 == 0:
        nb = llc[4:]
        data = (len(nb) >= 8 and nb[2:4] == NB_DELIM and nb[4] in NB_DATA)
        resynch = data and struct.unpack("<H", nb[6:8])[0] == 1
        return data, resynch, False
    if ctl & 3 == 1:
        poll = len(llc) >= 4 and llc[3] & 1
    else:
        poll = ctl & 0x10
    return False, False, bool(command and poll)


def frames(src):
    """Yield each frame read from src, with its length in front, until
    src closes."""
    buf = b""
    while True:
        chunk = src.recv(65536)
        if not chunk:
            return
        buf += chunk
        while len(buf) >= 4:
            n = 4 + struct.unpack(">I", buf[:4])[0]
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


def delay(src, dst, secs):
    """Pass the frames from src to dst, each no sooner than secs after it
    was read, until either side closes, then close both.  A thread of its
    own sends them, so that a frame's wait does not hold up the reading
    of the next."""
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
    except OSError:
        pass
    due.put(None)
    sender.join()


class Relay:
    """The relay's state, shared by its two pump threads under lock: the
    progress of the bulk drop and each direction's held poll."""

    def __init__(self, log):
        self.log = log
        self.start = time.time()
        self.lock = threading.Lock()
        self.bulk = 0           # 0 armed, 1 dropped (next data too), 2 done
        self.held = {"A>B": None, "B>A": None}

    def note(self, way, what):
        """Log a dropped frame; called with lock held."""
        self.log.write("%.3f %s %s\n" % (time.time() - self.start, way, what))
        self.log.flush()

    def decide(self, way, frame):
        """Return (drop, hold): drop is "bulk" or "poll" for a frame to
        drop now, else None; hold is the record of a poll to forward
        after WINDOW unless the other side's poll crosses it."""
        data, resynch, poll = classify(frame)
        with self.lock:
            if way == "B>A" and self.bulk < 2:
                if self.bulk == 1:
                    self.bulk = 2
                    if data:
                        return "bulk", None
                elif data and resynch and len(frame) > BULK_MIN:
                    self.bulk = 1
                    return "bulk", None
            if self.bulk and poll:
                other = self.held["B>A" if way == "A>B" else "A>B"]
                if other is not None:
                    other["drop"] = True
                    return "poll", None
                hold = {"drop": False}
                self.held[way] = hold
                return None, hold
        return None, None

    def pump(self, src, dst, way):
        """Pass the frames from src to dst (direction way) until either
        side closes, dropping those decide picks, then close both."""
        try:
            for rec in frames(src):
                drop, hold = self.decide(way, rec[4:])
                if hold is not None:
                    time.sleep(WINDOW)
                    with self.lock:
                        self.held[way] = None
                        if hold["drop"]:
                            drop = "poll"
                if drop:
                    with self.lock:
                        self.note(way, drop)
                else:
                    dst.sendall(rec)
        except OSError:
            pass
        close(src, dst)


def main():
    usage = "usage: LOSSY.PY [--delay MS] LISTEN_PORT TARGET_PORT LOG"
    args = sys.argv[1:]
    ms = None
    if args[:1] == ["--delay"]:
        if len(args) < 2 or not (args[1].isascii() and args[1].isdigit()):
            sys.exit(usage)
        ms = int(args[1])
        args = args[2:]
    if len(args) != 3:
        sys.exit(usage)
    listen_port, target_port = int(args[0]), int(args[1])
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", listen_port))
    server.listen(1)
    b, _ = server.accept()
    server.close()
    deadline = time.time() + CONNECT_SECS
    while True:
        try:
            a = socket.create_connection(("127.0.0.1", target_port))
            break
        except OSError:
            if time.time() > deadline:
                sys.exit("LOSSY.PY: VM A does not listen on port %d"
                         % target_port)
            time.sleep(0.1)
    for s in (a, b):
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    with open(args[2], "w") as log:
        if ms is None:
            relay = Relay(log)
            ways = [(relay.pump, (a, b, "A>B")), (relay.pump, (b, a, "B>A"))]
        else:
            ways = [(delay, (a, b, ms / 1000.0)), (delay, (b, a, ms / 1000.0))]
        threads = [threading.Thread(target=f, args=x, daemon=True)
                   for f, x in ways]
        for t in threads:
            t.start()
        for t in threads:
            t.join()


if __name__ == "__main__":
    main()
