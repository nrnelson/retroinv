"""dialog.py - play a user at a QEMU guest's keyboard: wait for text
in its console transcript, type answers and change the floppy in its
second drive, as a script says.

usage: python3 -I TEST/INTEG/dialog.py CONSOLE MONITOR SCRIPT

CONSOLE is the file QEMU writes the guest's first serial port to, its
console under CTTY COM1; MONITOR is the unix socket of QEMU's monitor,
through which the keys are typed and the disk is changed.  SCRIPT holds
one step a line ('#' starts a comment line):

  expect TEXT    wait until the transcript holds TEXT after the end of
                 the text the last expect matched
  send [TEXT]    type TEXT on the keyboard, then Enter: letters, digits,
                 space and : \\ . only
  change IMAGE   put the image file IMAGE, read-only, in the second
                 floppy drive (B:), then wait CHANGE_WAIT seconds

A program reads the keyboard under CTTY COM1 only through the CON
device, as in "INSTALL <CON".  The script ends at its last step.  Exit
status: 0 the script ran to its end; 1 the monitor closed or refused a
step; 2 a usage error or an unknown step.
"""

import os
import socket
import sys
import time

# Seconds after a change before the next step: a DOS that cannot see
# the drive's change line reads the disk again only when it was last
# read two seconds or more before.
CHANGE_WAIT = 3
POLL = 0.2              # seconds between two reads of the transcript
CONNECT_TRIES = 300     # tenths of a second to wait for the monitor

# QEMU's key names of the characters send types, shifted ones with
# "shift-".
KEYS = {' ': 'spc', ':': 'shift-semicolon', '\\': 'backslash', '.': 'dot'}


def connect(path):
    """A socket connected to the unix socket path, once QEMU makes it."""
    for _ in range(CONNECT_TRIES):
        s = socket.socket(socket.AF_UNIX)
        try:
            s.connect(path)
            return s
        except OSError:
            s.close()
            time.sleep(0.1)
    raise SystemExit('dialog.py: no monitor at %s' % path)


def monitor(mon, command):
    """Send command to the monitor; the text it answers up to its next
    prompt, or None when it has closed."""
    mon.sendall(command.encode('latin-1') + b'\n')
    out = b''
    while not out.endswith(b'(qemu) '):
        data = mon.recv(4096)
        if not data:
            return None
        out += data
    return out.decode('latin-1')


def key(c):
    """QEMU's name of the key that types c."""
    if c in KEYS:
        return KEYS[c]
    if c.isupper():
        return 'shift-' + c.lower()
    return c


def main(argv):
    if len(argv) != 3:
        sys.stderr.write('dialog.py: usage error\n\n%s\n'
                         % __doc__.split('\n\n')[1])
        return 2
    with open(argv[2], encoding='latin-1') as f:
        steps = [line.rstrip('\n') for line in f
                 if line.strip() and not line.startswith('#')]
    mon = connect(argv[1])
    if monitor(mon, '') is None:
        return 1
    at = 0
    for step in steps:
        verb, _, arg = step.partition(' ')
        if verb == 'expect':
            while True:
                try:
                    with open(argv[0], 'rb') as f:
                        text = f.read().decode('latin-1')
                except OSError:
                    text = ''
                found = text.find(arg, at)
                if found >= 0:
                    at = found + len(arg)
                    break
                time.sleep(POLL)
        elif verb == 'send':
            for c in arg + '\r':
                if monitor(mon, 'sendkey ' +
                           ('ret' if c == '\r' else key(c))) is None:
                    return 1
        elif verb == 'change':
            answer = monitor(mon, 'change floppy1 %s raw read-only'
                             % os.path.abspath(arg))
            if answer is None or 'rror' in answer:
                sys.stderr.write('dialog.py: %s\n' % answer)
                return 1
            time.sleep(CHANGE_WAIT)
        else:
            sys.stderr.write('dialog.py: unknown step: %s\n' % step)
            return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
