"""qmon.py - drive a headless QEMU guest through its monitor socket.

usage: python3 -I TEST/INTEG/qmon.py SOCKET STEP...

Steps, run in order:
  wait:TEXT[:SECS]  poll the 80x25 text screen until TEXT appears
                    (default limit WAIT_SECS; exit status 1 on timeout).
                    TEXT may contain ':'; only a numeric part after the
                    last ':' is taken as SECS.
  key:NAME          press one key (QEMU sendkey name, e.g. ret, f3, up)
  press:NAME:TEXT[:SECS]
                    press key NAME once the screen has settled, then poll
                    the screen as wait does until TEXT appears; NAME is
                    pressed a second time only when it was lost (see
                    below).
  leave:NAME:TEXT[:SECS]
                    the same, but done when TEXT has left the screen or
                    QEMU has ended (the monitor closed): the key that
                    ends the program.
  type:TEXT         type TEXT (letters, digits, space and . : \\ - _ only)
  sleep:SECS        pause
  screen            print the text screen to standard output
  quit              stop QEMU

The screen is read from colour text memory at B8000h with the monitor's
"xp" command, so no display is needed.

A program may draw its screen before it reads the keyboard, and one that
empties the keyboard buffer when it starts reading loses a key pressed
too early; a press or leave step therefore guards its key in three ways,
timed by the module constants STABLE and RESEND:

- it waits until the screen has not changed for STABLE seconds before it
  presses, so that a screen still being drawn (with no pause as long as
  that) is neither answered early nor taken as the screen to compare
  with;
- it presses again only when the screen is still exactly that settled
  screen RESEND seconds after the press: a program that read the key may
  be busy for a while before its screen changes, and a second key would
  then answer the next screen and put every later step one screen out of
  step, so the gap is long and the key is pressed again at most once;
- it presses again only when the BIOS keyboard buffer is empty (its head
  and tail pointers at 0040:001Ah and 0040:001Ch are equal): a key still
  waiting there has not been lost, only not read yet.

Every step is checked before QEMU is reached: an unknown step, a key,
press or leave step without a key name, a wait, press or leave step
without TEXT, a type step with a character outside that set, a sleep
step without a finite number of seconds or a screen or quit step with
an argument stops the run before any key is sent.

Exit status: 0 when every step succeeded; 1 when a wait, press or leave
step timed out, the monitor stopped answering or closed during another
step, or it could not be reached; 2 on a usage error, a step refused as
above, or a key name QEMU refuses.
"""
import math
import socket
import string
import sys
import time

# Seconds the screen must stay unchanged before a press or leave step
# presses its key; seconds after the press before the key is pressed a
# second time, if it was lost; seconds between two reads of the screen,
# and between two tries to reach the monitor.
STABLE = 1.5
RESEND = 25
POLL = 0.5
# Seconds a wait, press or leave step gives its TEXT when the step names
# none; seconds the monitor has to answer a command, and to be reached.
WAIT_SECS = 120
REPLY_SECS = 30
CONNECT_SECS = 10

# The sendkey name of each character a type step can type.
KEYS = {' ': 'spc', '.': 'dot', ':': 'shift-semicolon', '\\': 'backslash',
        '-': 'minus', '_': 'shift-minus'}
KEYS.update((c, c) for c in string.ascii_lowercase + string.digits)
KEYS.update((c, 'shift-' + c.lower()) for c in string.ascii_uppercase)

VERBS = ('wait', 'press', 'leave', 'key', 'type', 'sleep', 'screen', 'quit')


class Closed(Exception):
    """The monitor connection closed: QEMU has ended."""


class BadKey(Exception):
    """QEMU refused a sendkey name; the arguments are the name and QEMU's
    answer."""


class Monitor:
    """A connection to the QEMU monitor, at its prompt between commands."""

    def __init__(self, path):
        """Connect to the monitor socket path, trying for CONNECT_SECS
        while QEMU makes it, and read the first prompt."""
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        limit = time.monotonic() + CONNECT_SECS
        while True:
            try:
                self.sock.connect(path)
                break
            except OSError:
                if time.monotonic() > limit:
                    raise SystemExit('qmon: cannot connect to %s' % path)
                time.sleep(POLL)
        self.sock.settimeout(REPLY_SECS)
        self.buf = b''
        self.read_prompt()

    def read_prompt(self):
        """The monitor's output up to and with its next prompt."""
        while not self.buf.endswith(b'(qemu) '):
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                raise SystemExit('qmon: no reply from the QEMU monitor '
                                 'within %d s (VM hung or gone)'
                                 % REPLY_SECS)
            except OSError:
                raise Closed()
            if not data:
                raise Closed()
            self.buf += data
        out, self.buf = self.buf, b''
        return out.decode('latin-1')

    def cmd(self, line):
        """Send the command line; return the monitor's answer."""
        try:
            self.sock.sendall(line.encode('ascii') + b'\n')
        except OSError:
            raise Closed()
        return self.read_prompt()

    def sendkey(self, name):
        """Press key NAME; BadKey when QEMU answers anything but the echo
        of the command and the prompt."""
        reply = self.cmd('sendkey ' + name)
        answer = reply.partition('\r\n')[2][:-len('(qemu) ')].strip()
        if answer:
            raise BadKey(name, answer.splitlines()[0])

    def keys_pending(self):
        """True when the BIOS keyboard buffer holds a key not yet read."""
        out = self.cmd('xp /2hx 0x41a')
        for row in out.splitlines():
            if ':' in row and row.strip().startswith('00'):
                words = row.split(':', 1)[1].split()
                return len(words) >= 2 and words[0] != words[1]
        return False

    def screen(self):
        """The text screen, 25 lines of 80 characters, blanks trimmed."""
        out = self.cmd('xp /2000hx 0xb8000')
        cells = []
        for row in out.splitlines():
            if ':' not in row or not row.strip().startswith('00'):
                continue
            for word in row.split(':', 1)[1].split():
                if word.startswith('0x'):
                    ch = int(word, 16) & 0xFF
                    cells.append(chr(ch) if 32 <= ch < 127 else ' ')
        text = ''.join(cells[:2000])
        return '\n'.join(text[i:i + 80].rstrip()
                         for i in range(0, len(text), 80))


def text_secs(arg):
    """Split TEXT[:SECS] into TEXT and its seconds."""
    text, sep, secs = arg.rpartition(':')
    if not sep or not secs.replace('.', '', 1).isdigit():
        return arg, WAIT_SECS
    return text, float(secs)


def timed_out(shown, text, leave=False):
    """Print the screen shown and why the step failed; return 1."""
    sys.stdout.write(shown + '\n')
    sys.stderr.write('qmon: timed out waiting for %r%s\n' %
                     (text, ' to go' if leave else ''))
    return 1


def wait_for(mon, text, limit):
    """Poll the screen until TEXT appears; 0, or 1 after LIMIT."""
    while True:
        shown = mon.screen()
        if text in shown.replace('\n', ' '):
            return 0
        if time.monotonic() > limit:
            return timed_out(shown, text)
        time.sleep(POLL)


def press(mon, key, text, limit, leave):
    """Press KEY, guarded as the module docstring says, and poll until
    TEXT appears (or, with LEAVE, has gone or QEMU has ended); 0, or 1
    after LIMIT."""
    before = mon.screen()
    since = time.monotonic()
    while time.monotonic() - since < STABLE:
        if time.monotonic() > limit:
            return timed_out(before, text, leave)
        time.sleep(POLL)
        shown = mon.screen()
        if shown != before:
            before, since = shown, time.monotonic()
    mon.sendkey(key)
    pressed = time.monotonic()
    again = True
    while True:
        try:
            shown = mon.screen()
            if (text in shown.replace('\n', ' ')) != leave:
                return 0
            if time.monotonic() > limit:
                return timed_out(shown, text, leave)
            if shown != before:
                again = False
            elif again and time.monotonic() - pressed >= RESEND and \
                    not mon.keys_pending():
                mon.sendkey(key)
                again = False
        except Closed:
            if leave:
                return 0
            raise
        time.sleep(POLL)


def refused(step):
    """Why STEP cannot run, or None when it can."""
    verb, sep, arg = step.partition(':')
    if verb not in VERBS:
        return 'unknown step %r' % step
    if verb in ('screen', 'quit') and sep:
        return 'no argument taken in %r' % step
    if verb in ('key', 'press', 'leave') and not arg.partition(':')[0]:
        return 'no key name in %r' % step
    if verb in ('press', 'leave'):
        arg = arg.partition(':')[2]
    if verb in ('wait', 'press', 'leave') and not text_secs(arg)[0]:
        return 'no text in %r' % step
    if verb == 'type':
        bad = sorted(set(ch for ch in arg if ch not in KEYS))
        if bad:
            return 'no key types %r in %r' % (''.join(bad), step)
    if verb == 'sleep':
        try:
            secs = float(arg)
        except ValueError:
            secs = -1
        if not (math.isfinite(secs) and secs >= 0):
            return 'no number of seconds in %r' % step
    return None


def usage(reason):
    """Write reason and the usage line on standard error; return 2."""
    sys.stderr.write('qmon: %s\n%s\n' % (reason, __doc__.split('\n\n')[1]))
    return 2


def main(argv):
    if len(argv) < 2:
        return usage('no SOCKET and STEP')
    for step in argv[1:]:
        why = refused(step)
        if why:
            sys.stderr.write('qmon: %s\n' % why)
            return 2
    try:
        return run(Monitor(argv[0]), argv[1:])
    except Closed:
        raise SystemExit('qmon: monitor closed')
    except BadKey as e:
        sys.stderr.write('qmon: QEMU refused key %r: %s\n' % e.args)
        return 2


def run(mon, steps):
    for step in steps:
        verb, _, arg = step.partition(':')
        if verb == 'wait':
            text, secs = text_secs(arg)
            if wait_for(mon, text, time.monotonic() + secs):
                return 1
        elif verb in ('press', 'leave'):
            key, _, rest = arg.partition(':')
            text, secs = text_secs(rest)
            if press(mon, key, text, time.monotonic() + secs, verb == 'leave'):
                return 1
        elif verb == 'key':
            mon.sendkey(arg)
            time.sleep(0.2)
        elif verb == 'type':
            for ch in arg:
                mon.sendkey(KEYS[ch])
                time.sleep(0.1)
        elif verb == 'sleep':
            time.sleep(float(arg))
        elif verb == 'screen':
            sys.stdout.write(mon.screen() + '\n')
        elif verb == 'quit':
            mon.sock.sendall(b'quit\n')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
