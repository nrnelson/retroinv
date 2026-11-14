# RETROINV — The Server and the TCP Bridge

`INVSERV.EXE`, the server, is the one program that opens the data files while it runs. From one
cooperative loop it serves the line protocol of `DOC/PROTOCOL.md`, one session per connection
slot, over three transports: NetBIOS calls to its name, its serial ports and the request files
of its mailbox. A status screen shows its state until the operator or a level-3 user shuts it
down. `INVTCP.EXE`, the TCP bridge, carries the protocol from TCP/IP clients to it (The TCP
bridge, below).

| File | Responsibility | Uses |
|---|---|---|
| `SERVER/INVSERV.C` | The command line | SRVRUN SRVCFG |
| `SERVER/SRVRUN.C` | Start-up, the loop, the event log, the stop | CONN TNETB TSERIAL TMBOX TICKET THROTL SRVSCR SRVCFG MSG ENGINE |
| `SERVER/CONN.C` | The connection slots: requests to the protocol layer, responses held until sent | PROTO ENGINE |
| `SERVER/TNETB.C` | The NetBIOS transport | CONN COMPAT |
| `SERVER/TSERIAL.C` | The serial transport, on the port driver `COMPAT/CUART.C` and `CUARTSV.C` (the driver's calls a client never makes) | CONN COMPAT |
| `SERVER/TMBOX.C` | The mailbox transport | CONN COMPAT |
| `SERVER/TICKET.C` | The store of the mailbox's login tickets, and the login table every session's LOGIN uses: the store's calls, the throttle's and the protocol's login commands (`PROTO/DPTKT.C`) | THROTL PROTO ENGINE COMPAT (`CFINE.C`) |
| `SERVER/THROTL.C` | The failed-login throttle | ENGINE COMPAT |
| `SERVER/SRVSCR.C` | The status screen (video and keyboard), its texts in `INVSERV.MSG` | CONN TSERIAL SRVCFG MSG COMPAT |
| `SERVER/SRVCFG.C` | The settings file | CONN COMPAT |

## Running it

```
INVSERV [/CFG:file]
```

`/CFG:file` names the settings file, by default `SRVCFG_NAME` (`INVSERV.CFG`) in the current
directory. The switch is matched without regard to case and may start with `-`; `/?` writes
the usage text to standard output and exits with 0. Any other argument, an empty `/CFG:` or a
second one is a usage error. The server needs:

- MS-DOS 3.1 or later and `FILES=45` in `CONFIG.SYS`, as the client (`DOC/ENGINE.md`,
  Handles);
- unless `SESSIONS=0`, a NetBIOS provider sized as in NetBIOS resources (MS Network Client 3.0
  NetBEUI is the tested one);
- `SHARE.EXE`, recommended: DOS then refuses a second program on the same data directory
  (`ENG_EINUSE`);
- its message file `MSG_SRVFILE` (`INVSERV.MSG`, built with `INV.MSG` and carrying the same
  stamp), in the program's directory or else the current one;
- an existing data directory; an empty one gets new tables and the default `ADMIN` user, whose
  password must be changed at its first login (`DOC/ENGINE.md`).

Clients reach it with `INV /NET name` (`DOC/CLIENT.md`), `name` being its `NAME=`, with
`INV /COM1` or `/COM2`, with the Windows client's `INVWIN /MBOX:dir` on its mailbox, or, for
scripts, with `INVTEST`'s `@MBOX` sessions. Each transport carries requests, answers and
passwords in plain text, and the server takes a call from any station that reaches it
(`DOC/SECURITY.md`).

## Settings: `INVSERV.CFG`

Text lines `KEY=value`, ended by CR LF or LF. Keys are matched without regard to case, blanks
around `=` and at the ends of a line are ignored, a line that is empty or starts with `;` is a
comment, and a Ctrl-Z ends the file. The limits are constants of `SRC/SERVER/SRVCFG.H` and
`CONN.H`, `SRC/COMPAT/COMPAT.H` and `SRC/ENGINE/ENGINE.H`.

| Key | Value | Default |
|---|---|---|
| `NAME=` | the server's NetBIOS name: 1 to `NB_NAMELEN` − 1 characters, no space or control byte, not starting with `*`; letters are upper-cased | `SRVCFG_DEFNAME` (`RETROINV`) |
| `DATA=` | the data directory: 1 to `ENG_MAXDIR` characters, no control byte, counting the separator that is added when the value does not end in `\`, `/` or `:` | `DATA\` |
| `READONLY=` | stations capped at level 1 whatever the user: 1 to `SRVCFG_ROMAX` names of 1 to `ENG_STALEN` characters, no control byte or comma, separated by commas, blanks around each removed, letters upper-cased | none |
| `SESSIONS=` | NetBIOS sessions served at once, 0 to `CONN_NETMAX`; 0 for no NetBIOS | `CONN_NETMAX` |
| `LISTENS=` | LISTENs kept posted, 1 to `SESSIONS=`; refused with `SESSIONS=0` | `SRVCFG_DEFLISTENS`, or `SESSIONS=` when that is less |
| `SERIAL=` | serial ports served: `COM1`, `COM2` or both, separated by a comma, in any case | none |
| `BAUD=` | their rate: 1200, 2400, 4800, 9600, 19200 or 38400 (`UART_BAUDOK` of `COMPAT.H`) | `UART_DEFBAUD` |
| `RTSCTS=` | `1`: RTS/CTS flow control on them | `0` |
| `MAILBOX=` | the mailbox directory, holding `REQUEST\` and `RESPONSE\`, as `DATA=` | none |
| `IDLE=` | minutes after which an idle session is closed, 0 to `SRVCFG_IDLEMAX`, 0 for never | `SRVCFG_DEFIDLE` |
| `LOG=` | the event log: 1 to `SRVCFG_LOGMAX` characters, no control byte | none |
| `BIOS=` | `1`: draw the status screen through INT 10h only | `0` |
| `SNOW=` | `1`: wait for the retrace when writing to a CGA | `0` |

Numbers are plain decimal digits: a sign, any other character, an empty value or one beyond
its range is refused. The start stops with exit status 8 and the line's number at an unknown
key, a bad value, a key given twice, a line without `=`, a line longer than `SRVCFG_MAXLINE`
bytes (CR LF excluded), a NUL byte, a CR inside a key or value, a `LISTENS=` above `SESSIONS=`
(reported at the later of the two lines), or a `SESSIONS=0` with neither `SERIAL=` nor
`MAILBOX=`, which would serve nobody (reported at its line). `BAUD=` and `RTSCTS=` without
`SERIAL=` have no effect. A missing file means every default.

## Start-up and exit statuses

The server starts in this order; the first step that fails ends it with the status in
brackets:

1. the far-memory manager (`cmem_init`), the DOS version (3), the stack filled for the log's
   `STACK` event (`cstk_fill`);
2. the command line (1);
3. the critical-error handler. It answers Fail, so a drive not ready or a sharing or lock
   violation becomes an error of the request that met it, not a prompt nobody sees; an error
   DOS does not let fail is answered Retry, its call waiting until the error clears, as DOS
   would take Fail as Abort and end the server with its ports' interrupt handlers installed;
4. the settings file (8);
5. the NetBIOS provider (5), unless `SESSIONS=0`, before anything touches the data;
6. the message file (7);
7. the data directory, whose open enlarges the handle table to `ENG_PROCHANDLES` (2);
8. the event log of `LOG=` (4), then its `START` line;
9. the connection slots, one for each NetBIOS session, serial port and the mailbox, and with
   `MAILBOX=` the store of login tickets (6);
10. BREAK checking off, so neither Ctrl-C nor Ctrl-Break can end the server while the
    provider owns its NCBs or a port's interrupt handler is installed;
11. the NetBIOS transport, unless `SESSIONS=0`, which adds the name (9; 6 without memory);
12. the ports of `SERIAL=` (10; 6 without memory);
13. the mailbox of `MAILBOX=` (11; 6 without memory);
14. the status screen (6).

A failure is said on standard error after `INVSERV: `, in fixed English up to the message file
and in its texts from then on. Every way out, a failed start included, undoes what was started
(Shutdown). The statuses are the `SRV_EXIT_` constants of `SRC/SERVER/SRVRUN.H`:

| Status | Constant | Meaning |
|---|---|---|
| 0 | `SRV_EXIT_OK` | shut down by F3 or `ADMIN SHUTDOWN`, also after `/?` |
| 1 | `SRV_EXIT_USAGE` | usage error |
| 2 | `SRV_EXIT_OPEN` | the data directory could not be opened (the reason is said) |
| 3 | `SRV_EXIT_DOSVER` | DOS older than 3.1 |
| 4 | `SRV_EXIT_IO` | the event log could not be opened, or (in place of 0) the log could not be written or the data files could not be flushed and closed at the end |
| 5 | `SRV_EXIT_NONB` | no NetBIOS provider, `SESSIONS=` not 0 (the message names the way out: `SESSIONS=0` with `SERIAL=` or `MAILBOX=`) |
| 6 | `SRV_EXIT_NOMEM` | no memory for the slots, a transport or the screen |
| 7 | `SRV_EXIT_MSG` | the message file missing, unreadable, malformed or made for another version, or no memory to load it |
| 8 | `SRV_EXIT_CFG` | the settings file unreadable or not valid |
| 9 | `SRV_EXIT_NAME` | the NetBIOS name in use at another station or already in this one's table, or refused by the provider (its NetBIOS code is said in hex) |
| 10 | `SRV_EXIT_PORT` | a port of `SERIAL=` missing or open already (the port is named) |
| 11 | `SRV_EXIT_MBOX` | the directory of `MAILBOX=`, or its `REQUEST\` or `RESPONSE\`, missing: the server never makes them |

## The loop

DOS runs one program at a time, so the server is one cooperative event loop, with no NetBIOS
post routine: every NetBIOS command is a no-wait NCB (NetBIOS control block) polled for
completion. The one hardware interrupt handler is the serial ports' (`CUART.C`), which only
moves bytes between the chip and the port's rings and works the flow control; the loop does the
rest.

```
loop:  tnetb_poll()     post the missing LISTENs and the RECEIVEs and SENDs due; take at most
                        one completed NCB per slot
       tserial_poll()   per port: at most TSER_CHUNK bytes of its receive ring to its slot,
                        output into its transmit ring, a hang-up noticed
       tmbox_poll()     every TMBOX_POLL ticks while no mailbox session runs, look for the
                        next request file; else move the session's next piece and its output
       conn_service()   round-robin; at most ONE request per slot per pass; skip slots with
                        unsent output
       conn_served()    the clock read again: a slot just answered is idle from now on
       conn_timeouts()  idle for IDLE= minutes, or a shutdown past CONN_SHUTWAIT -> close
       srvscr_poll()    keyboard: F3 shutdown, F5 refresh, F2 trace; redraw once a second
       nothing done -> commit the event log, INT 28h (the DOS idle call)
```

No session starves another: every request does bounded work (pages of at most `PROTO_PAGEMAX`
records, scans of at most `ENG_MAXREADS` reads, reports paged; `DOC/ENGINE.md`, Cursors and
bounded scans), and each session gets one request per pass. `TEST/INTEG/stvgate.sh` checks that
a serial session is answered promptly while NetBIOS sessions keep the server busy.

The connection slots (`CONN.H`) number at most `CONN_NETMAX` NetBIOS sessions (the bridge's
included), `CONN_SERMAX` serial ports and `CONN_MBXMAX` mailbox session, sized to the
transports served. A slot goes `FREE -> LISTEN -> OPEN -> CLOSING -> FREE`; the protocol state
(HELLO given, logged in, password change due) is its session's. A slot is wiped, its user and
any request bytes included, and its output block zeroed, when its session ends.

## Sessions

The server adds its name and keeps up to `LISTENS=` LISTENs posted, never more than the free
slots, so open sessions and waiting LISTENs together are at most `SESSIONS=`. A call opens a
session in the slot whose LISTEN it met; the next LISTEN is posted on the following pass. The
station, for the audit trail and `READONLY=`, is the caller's NetBIOS name without blanks at
either end; `HELLO` is still required and its argument checked, but names the station only
when the caller's name is empty or holds a control byte. `INV /NET` calls from the DOS machine
name, or from a name it makes up on a machine without one (`DOC/CLIENT.md`), so a station
meant for `READONLY=` needs a machine name, which MS Network Client 3.0 does not set.

Lines travel in NetBIOS messages of at most `NB_MSGMAX` bytes, each holding several lines or
part of one, answered as if they had come one by one. Each slot has one RECEIVE of `CONN_RXSZ`
bytes posted (a longer message arrives in pieces) and at most one SEND in progress. The next
message is received only once the last one's lines are all answered, so NetBIOS itself holds
back a client that sends faster than it is served. A response waits in the slot's far output
block of `CONN_OUTSZ` bytes until sent. Each pass of the loop answers at most one request per
slot, starting one slot further each time and passing over a slot still sending, so every
session gets the same share; paged answers fit the room left in the block. A pass with nothing
to do calls INT 28h, so resident programs such as `PRINT` run. The pass that first sees the tick
count past midnight reads the date through DOS (`tmr_clock_now`): DOS moves its date on only
when it reads its clock, and the BIOS marks one midnight unread, never two, so a server idle
over a weekend would otherwise wake with the DOS date days behind.

A session ends:

- after the response to `BYE` and to the `PROTO_MAXFAIL`-th failed `LOGIN`, which close it in
  the protocol layer (`DOC/PROTOCOL.md`); the server hangs up once that response is sent;
- after `IDLE=` minutes with no request answered and no output sent, counted from the end of
  its last answer, so a long command (`ADMIN PACK`, `IMPORT`) does not close its own session;
  this also ends a session whose client stopped reading, and the client finds it gone at its
  next request. A running INV sends `PING` while it waits at a key outside a request, often
  enough for the least `IDLE=` (`TUI_PINGT`, `DOC/CLIENT.md`), so `IDLE=` ends the sessions
  whose client stopped or went away, never one left open at a menu;
- when the client hangs up or the session is lost; either frees the slot. NetBEUI ends a
  session whose caller has gone (switched off, its cable pulled) once its keepalive goes
  unanswered, in about a minute on the Workgroup Add-On.

A NetBIOS session that never sends a request, such as one whose caller gave up its CALL just as
the LISTEN completed, holds its slot until `IDLE=` (with `IDLE=0`, until the server stops).

### Failed logins

A session closes after `PROTO_MAXFAIL` failed logins, but a guesser can open another one. The
throttle, `SRC/SERVER/THROTL.H`, counts the failed password logins of each user instead, over
every session, transport and station alike:

- once `THR_FAILS` of them fall within `THR_SPAN` ticks (5 minutes) of the first, every
  password `LOGIN` naming that user is refused for `THR_HOLD` ticks (5 minutes) from the last:
  its password, the right one included, is checked as a wrong one, so the answer, the audit row
  (`F`) and the session's count are those of a wrong password (what that hides:
  `DOC/SECURITY.md`, Failed logins);
- a successful login clears the user's own count, and a failure `THR_SPAN` ticks after the
  count's first starts a new one;
- a login with a ticket is never refused, so a mailbox client that holds one goes on working;
- names that are no user's are not counted: they fail whatever the password.

The table holds `THR_MAX` entries in one far block: `THR_MAX` − 1 users and an overflow entry
shared by all the others. A user new to the table takes a free entry, or one whose count or
refusal is over, and carries the overflow entry's count into it; an entry that still counts or
refuses is never taken from its user. So neither failing as other users nor timing the end of
an entry wins a guesser more than about `THR_FAILS` guesses at a user every `THR_SPAN` ticks.
With no entry left, the failure counts in the overflow entry, and once that one reaches
`THR_FAILS` within `THR_SPAN` ticks, every user outside the table is refused for `THR_HOLD`
ticks, as above. That is the trade-off: a guesser who knows enough user names to fill the table
can have every user outside it refused for `THR_HOLD` ticks, a denial of service, though no more
guesses at any user. A guesser can likewise
keep one user out, `THR_HOLD` ticks at a time, by failing as that user; each failure leaves its
`F` row in the audit trail. The table lives in memory only: a restart clears it.

### Serial lines

Each port of `SERIAL=` carries one session at a time, of kind `COM1` or `COM2`, at `BAUD=` with
8 data bits, no parity and 1 stop bit; `RTSCTS=1` adds hardware flow control. The slot listens
until a byte arrives; the session then opens with no station, so `HELLO` names it and
`READONLY=` on a serial station is advisory. When the session ends (`BYE`, the last failed
`LOGIN`, `IDLE=`, a shutdown) the server drops the input not yet served, lets the last response
go out, and listens again: the next byte opens the next session.

A client that goes without `BYE` (switched off, its cable pulled, INV ended) would leave its
session, logged in, to the next person on the line. A full null-modem cable carries each end's
DTR to the other's DSR and DCD: once DSR or DCD has been up during a session, both down for
`TSER_HANGUP` ticks (half a second) end it at once as a hang-up, its output dropped. A port
whose DSR and DCD never rise (a three-wire cable, an emulator's line without modem signals)
keeps a session until `BYE`, the last failed `LOGIN`, a shutdown or `IDLE=` ends it; with
`IDLE=0` it never ends by itself, so such a line should keep `IDLE=` above 0. INV sends a last
`BYE` when it gives a lost line up.

The line has no checksum, so a byte lost or changed shows only as a receive error the port
counts: an overrun, a framing or a parity error. From the read that finds the count risen
since the port's receive ring was last emptied, the server drops what arrives, and the first
LF among it, which ends the line the error may lie in, ends the session at once, as a hang-up
does, but only flushes the port's receive ring, so the status screen keeps the counts: that
line is not carried out and gets no response, and the client, whose wait for one runs out,
takes the line as lost. An error with no LF after it only drops its bytes. An error counted
while the port listens, as a cable plugged in or the other machine switched on may cause, drops
what the ring holds and opens no session, so it does not cost the next caller its first
line.

Each pass moves at most `TSER_CHUNK` bytes of a port's receive ring to its session, the next
only once these are served. As a session that ends drops its input, a client waits for the
response to a request that may end it (`BYE`, `LOGIN`) before sending again; others it may
send ahead (INV sends ahead only its farewell) while together they fit in the ring's
`UART_RXSZ` − 1 bytes (`COMPAT.H`) and the `TSER_CHUNK` bytes its session holds. With
`RTSCTS=1` the port drops RTS as its ring fills, holding back a client that heeds CTS; without
it, once the ring is full the chip keeps what arrives (one byte on an 8250, 16 in a 16550A's
FIFO) and loses the rest to overruns. `DOC/PROTOCOL.md` gives the limits of a line without
framing. The status screen counts each port's overruns, framing and parity errors, and bytes
lost to a full ring, which stays 0 on a real port, whose receiver is held instead, and counts
only on a test's simulated port; the counts start again from 0 whenever the port is opened
again, after a hang-up or a drain that ran out.

### The mailbox

`MAILBOX=` names a directory, shared over the network or not, holding `REQUEST\` and
`RESPONSE\`; `SRC/SERVER/TMBOX.H` gives the rules. A client writes `REQUEST\<station>.TMP` and
renames it to `.REQ` once complete. A `.TMP` left by a client that stopped before the rename
holds the request, password and all; as the server cannot tell it from one being written, the
station's own client zeroes and removes it before its next post, and a station that never
posts again keeps it.

The server serves one request file at a time as a session of kind `MBOX`, its station the file's
base name, which `READONLY=` caps as any other, and writes the responses to
`RESPONSE\<station>.TMP`, renamed to `.RSP` at the session's end. Every `TMBOX_POLL` ticks while
no session runs, it reads up to `TMBOX_SCANMAX` names and takes the first after the last one
served, in name order, so one station cannot keep another waiting. A file whose station DOS
takes for a device (`CON`, `PRN`, ...) is passed over, never opened, renamed or removed: DOS
would open the device. Before acting on the request it reads it whole, overwrites it in the file
with zeros and commits it (the handle duplicated and the duplicate closed). Every DOS buffer
that held part of it, one its writer filled included, then holds zeros, so no later write
carries the text to the disk: DOS writes whole sectors, and its first write into a sector past a
file's end sends the rest of the sector from a buffer as it is. For the same reason a response,
which may hold a login ticket, ends in a sector of its own: once its last output is written, the
rest of its last sector is written with zeros and the `.TMP` cut back, so no earlier session's
ticket or request rides into it. The response itself is its client's to zero once read, which
leaves a window (`DOC/SECURITY.md`, Mailbox). The engine seals its edit marker against the same
leak (`DOC/ENGINE.md`, Records and fields, Edit marker).

A request of at most `CONN_RXSZ` less 2 bytes (room for the CR LF added after a last line
without LF) is read into a block the server keeps; a longer one takes a far block for its
session (Memory). One that finds no memory for that block or is longer than `CMEM_MAXBLK` less
that CR LF, and one whose read, zeroing or commit fails, ends its session with nothing acted on
and an empty response, and is logged.

At the session's end a request not yet zeroed is zeroed, and the file is removed, so a request
never runs twice and no password stays on the disk. While a request cannot be removed the
response's `.TMP` is held back and each look tries the removal again, so it is answered only
once it is gone. When the zeroing and the removal both fail, a request whose session ended
before it was zeroed keeps its text and is served again; a zeroed one is not. At the start, a
`.TMP` that a stopped run left in `RESPONSE\` ends its session the same way. A mailbox file that
cannot be written, renamed or removed is logged (`MBOX`).

A station only creates, writes, renames, reads and removes files and never asks for a record
lock (INT 21h 5Ch), so the mailbox also serves an 8088 whose only network client is MS Client
3.0's basic redirector over NetBEUI: on a Workgroup Add-On for MS-DOS share every record lock
that redirector asks for fails, while those file calls work. When its link to the server
breaks, the redirector connects again and repeats the call in flight, so a rename or removal
that was done can report file not found, and a read on the old handle fails; the mailbox client
of INVTEST and INVWIN allows for that (`SRC/CLIENT/MBXCLI.H`).

A station's files are its own only while its name is: two clients of one station share them,
crossing their responses and replacing each other's tickets. A computer on MS Network Client 3.0
names its station by `STATION=` of its settings file, and INVWIN refuses a mailbox link without
one of its own (`DOC/CLIENT.md`, MBOX).

### Login tickets

A mailbox session lasts one request file, so a client may log in with its password once,
asking for a ticket (`LOGIN user pw TICKET=NEW`), and with the ticket in its later files
(`DOC/PROTOCOL.md`, Session). Only the sessions of the mailbox are offered tickets
(`conn_tops.tickets`); on a NetBIOS or serial session TICKET= is refused, so a caller who can
redial gains no faster way to guess. The store, `SRC/SERVER/TICKET.H`, opened only with
`MAILBOX=`, holds at most `TKT_MAX` tickets in one far block, each for one user on one station
with the tag of its user's PWHASH:

- A ticket is never kept: an entry holds the `eng_mix128` of its digits, and an entry dropped,
  expired or replaced is zeroed. A station holds at most one ticket; a full store gives up the
  entry idle the longest.
- A ticket ends once unused for `IDLE=` (none with 0), `TKT_LIFE` ticks after it was made, when
  its user's password changes or is reset (the tag no longer matches), at `BYE TICKET=END`, at
  a login with it refused 401 or 403 under any user or station (a leaked ticket dies at its
  first misuse and its holder logs in with the password), and when the server stops: the store
  lives in memory only.
- A ticket mixes a pool of entropy, the count of tickets made, the timer chip's count
  (`tmr_fine`, `SRC/COMPAT/CFINE.C`), the tick, the user's PWHASH, the user and the station;
  the pool is then mixed again, so the tickets a client sees tell nothing of the next. Between
  tickets the pool takes in the timing of every pass of the loop that did work.
- A ticket crosses the share in the request files, which are zeroed as a password is, and in
  the response that gave it, which its client zeroes before it removes it. The server writes it
  to no log, trace or audit row; the slot's output block is zeroed when the slot is freed.

### Levels and exclusive commands

A station named in `READONLY=` logs in at level 1 (`ENG_LVL_READ`) whatever the user's level,
so every change is refused with 203. `IMPORT` and `ADMIN PACK`, `REINDEX`, `BACKUP` and
`RESTORE` need the system to themselves: they are refused with 402 while another session is
open. `STATUS` adds `K UPTIME=<seconds>` and `K SESSIONS=<open sessions, the requester's
included>`; `ADMIN CLIENTS` lists the open sessions (`DOC/PROTOCOL.md`, Administration).

## NetBIOS resources

`SESSIONS=` NetBIOS sessions take, in the server:

- 2 × `SESSIONS=` no-wait NCBs of 64 bytes: per slot one for the LISTEN and then the RECEIVE,
  one for the SEND, so a LISTEN takes no NCB beyond its slot's;
- one wait-form command at a time besides (ADD NAME at the start, a CANCEL or HANG UP as a
  session ends, the CANCELs and DELETE NAME at the stop);
- one name in the provider's name table.

The provider's limits count for the whole machine, shared with the redirector and any other
NetBIOS program; how to set them for MS Network Client NetBEUI is in `DOC/INSTALL.md`
(Requirements). `TEST/INTEG/PROTOCOL.INI` gives `SESSIONS=16` and `NCBS=32`, room for
`CONN_NETMAX` sessions and the clients run beside them. A provider that runs short does not stop
the server: a post refused for want of room, or a LISTEN that completes with "session table
full", is tried again on a later pass, so fewer clients are served at once. A LISTEN that fails
for any other reason (the name deleted under the server, say) is posted again `TMR_SEC` ticks
later.

## Memory

The server is a medium-model program. Besides its code and near data it takes far memory for
the engine (`DOC/ENGINE.md`, Memory per open engine), the message file, the status screen, an
output block of `CONN_OUTSZ` bytes per slot, a receive block of `SESSIONS=` × `CONN_RXSZ` bytes,
the NCBs and the throttle's table of `THR_MAX` entries (`SRC/SERVER/THROTL.H`); per serial port
a block holding its interrupt handler and rings of `UART_RXSZ` and `UART_TXSZ` bytes, and
`TSER_CHUNK` bytes of input; and `CONN_RXSZ` bytes of mailbox input and the login tickets' store
of `TKT_MAX` entries (`SRC/SERVER/TICKET.H`). A longer mailbox request takes, while its session
runs, a block of its size plus the CR LF, which the free-memory figure does not count. That
figure, `INVSERV_FREEKB_<build>`, and how it is measured are in `DOC/BUILD.md` (Runtime
requirements).

## The status screen

```
 RETROINV server RISRV                                             Up 1d 1:01:01

 Data directory      D:\INV\
 Read-only stations  none
 Sessions open       2
 Waiting for calls   2

 Slot Type  Station           User      Level  State               Idle
 1    NETB  LAB1              ADMIN     3      ready               0:01:15
 2    NETB  BENCH-PC                           logging in          0:00:03
 ...
 Serial ports        Baud   Chip    Overrun  Framing  Parity   Lost
 COM2                9600   8250    1        22       333      65535

 Mailbox             M:\BOX\
 Trace off

 F2=Trace  F3=Shut down  F5=Refresh
```

(Put together from `TEST/GOLDEN/SRV/TWO.SNP` and `PORTS.SNP`, whose port is simulated.) Below
the name and uptime come the settings, the sessions open and the slots waiting for a call
(NetBIOS LISTENs and idle serial ports); a row per open session, in slot order, with its
transport (`NETB`, `COM1`, `COM2`, `MBOX`), station, user, level, protocol state (connected,
logging in, changing password, ready, closing) and time since its last request was answered or
output sent; a row per serial port with its rate, chip (8250, 16450, 16550 or 16550A) and error
counts since the start; and the mailbox. The screen is redrawn once a second or so through
COMPAT's video layer, as the client's screens are: a colour palette, or on a monochrome display
only the attributes 07h, 0Fh and 70h; `BIOS=` and `SNOW=` apply.

Keys are read without waiting, so the server goes on serving:

- **F3** shuts the server down, after a question on the bottom row giving the number of sessions
  open, also when none is, which Y confirms and N or Esc cancels, as in INV, and which a
  shutdown begun by `ADMIN SHUTDOWN` removes. While the question shows, F2 and F3 do nothing
  and F5 redraws it with the screen. During a shutdown F3 does nothing.
- **F2** turns the trace row on and off (off at start): each session's opening (with its
  station), login (with the user), request and close. A request shows only its first word and,
  unless that is `HELLO`, `LOGIN` or `PASSWD`, its second, each only when made of letters, cut
  to `CONN_WORDLEN` characters: no argument, and so no password, is ever shown.
- **F5**, and Ctrl-Break, redraw the whole screen (after Ctrl-Break DOS may have echoed `^C`).

## The event log

With `LOG=` the server appends one line per event to that file, held open for the run and
denying writes to other programs. A missing log is created, then, as DOS creates a file in
compatibility mode, closed and opened again with that sharing mode; a log another program makes
meanwhile is opened as it is, never truncated.

```
YYYY-MM-DD HH:MM:SS slot EVENT text
```

Slot 0 is the server: `START` (text: its `NAME=`) as soon as the log is open, then, at every
stop after it, `STACK` (the stack bytes the run never used, counted in the DOS builds, which
fill the stack at the start) and `STOP` (the exit status). A session's events carry its slot
number from 1, as on the screen: `OPEN` (its station, none for a serial session), `LOGIN` (the
user), `REQ` (the request's words as the trace row shows them) and `CLOSE`. `MBOX` reports a
mailbox file that failed, on its session's slot or on slot 0 when no session holds it, with the
`CF_E` code and the station (`0 MBOX 5 OLD`):

```
2026-10-08 12:00:00 0 START RISRV
2026-10-08 12:00:04 1 OPEN RIVMB
2026-10-08 12:00:04 1 REQ HELLO
2026-10-08 12:00:05 1 REQ LOGIN
2026-10-08 12:00:05 1 LOGIN ADMIN
2026-10-08 12:00:05 1 REQ ITEM LIST
2026-10-08 12:00:06 1 REQ BYE
2026-10-08 12:00:06 1 CLOSE
2026-10-08 12:03:10 0 STACK 5752
2026-10-08 12:03:10 0 STOP 0
```

The words are fixed upper-case codes, never translated, for the reader's tools; the log holds no
text of the message file and no argument of a request. New lines are committed (the handle
duplicated and the duplicate closed) on the first idle pass of the loop, or `TMR_SEC` ticks
after the last commit while the server stays busy, so a power cut loses at most about a second
of lines. A log that cannot be written is closed; the server goes on, says so at the stop, and
exits with 4.

## Shutdown

F3 (confirmed with Y) and `ADMIN SHUTDOWN` from a level-3 user, which first
answers `+OK`, start the same shutdown:

1. No new session is taken: the LISTENs are cancelled, and a call that opens meanwhile is hung
   up at once. The bottom row says the server is shutting down.
2. Each open session's next request is answered `-ERR 501` (`ENG_ESHUTDOWN`), which the
   client's link takes as the end of the session, and the session is closed; a response
   already being sent is sent first, and a blank line is still no request.
3. `CONN_SHUTWAIT` ticks after the shutdown began, every slot still in use is closed, its
   session hung up.
4. The loop ends once every slot is free, or, should the provider never confirm a cancel,
   2 × `CONN_SHUTWAIT` ticks after the shutdown began.

The stop, as on every other way out, ends the transports: NetBIOS cancels every command in
progress, hangs up every session the provider holds, deletes the name and closes the NCBs; the
serial ports are closed and their interrupt handlers removed, so no vector points into memory
about to be freed; the mailbox ends its session, its response put in place and its request
removed. It then flushes and closes the data files, restores the DOS screen, writes the log's
`STACK` and `STOP` lines and closes the log. As the provider could still write into a NetBIOS
command it has not given back, the server then says "n NetBIOS commands pending; waiting (any
key ends the wait)" and waits, letting resident programs run, until the provider gives them
back or a key accepts the risk. Only then does it free the slots' memory, restore BREAK
checking and the critical-error handler, and exit.

## The TCP bridge (`INVTCP.EXE`)

`INVTCP` lets TCP/IP clients reach the server. It accepts TCP connections on port `BR_PORT`
(`SRC/TCP/BRIDGE.H`) and relays each one over a NetBIOS session of its own to the server. It
relays bytes, never lines: it does not parse, log or change a request, so the protocol
(`DOC/PROTOCOL.md`) and its level checks are exactly those of a NetBIOS client. A TCP client is
any program that speaks that protocol: a line-mode `nc host port`, or the Windows client
`INVWIN /TCP:addr` (`LINKTCP.C`); `INV` itself does not use TCP. The bridge needs a NetBIOS
provider and a packet driver, and a DOS station of its own, as it and `INVSERV` are both
foreground programs. Its TCP/IP stack is WATTCP (`DOC/DESIGN.md`, Design decisions: TCP stack),
behind the socket layer of the table below; it is a medium-model program built by Open Watcom,
Turbo C 2.01 and Watcom C 6.5 alike.

| File | Responsibility | Uses |
|---|---|---|
| `TCP/INVTCP.C` | command line, start-up, the loop, event lines, the stop (`invtcp_main`) | BRIDGE TSOCK COMPAT |
| `TCP/TCPMAIN.C` | `main`, which calls `invtcp_main`; the tests call it with a `main` of their own | INVTCP |
| `TCP/BRIDGE.C` | the slots: each connection relayed over its NetBIOS session | TSOCK COMPAT |
| `TCP/TSOCK.H` | the thin socket layer: `TS_SLOTS` listening sockets, calls that never wait | COMPAT |
| `TCP/TSWAT.C` | `TSOCK.H` on WATTCP | WATTCP COMPAT |
| `COMPAT/CPKT.C`, `CPKTUP.ASM` | the packet driver and its receive upcall | WATTCP |
| `COMPAT/CWATT.C`, `WTSHIM.H` | WATTCP's helper routines in C, its Ctrl-Break hook, its per-compiler macros | WATTCP |

### Running the bridge

```
INVTCP [/NAME:local] [/ALLOW:net]... server
```

`server` is the server's NetBIOS name (its `NAME=`, default `RETROINV`). `/NAME:local` is the
bridge's own NetBIOS name; without it the bridge takes the DOS machine name when one is set,
else `RTCP` and four hex digits, trying up to `NB_NAMETRIES` such names in all while the name
is taken. Names are upper-cased. A name longer than `NB_NAMELEN` − 1 characters, one holding a
space or a control character, or one starting with `*` is a usage error (`txt_nbname_ok`).
Switches may start with `-` as well as `/`. `INVTCP /?` writes the usage and exits 0. F3,
confirmed with Y, stops the bridge.

Each `/ALLOW:net` adds a network to the bridge's allow-list, up to `BR_ALLOWMAX` of them
(`SRC/TCP/BRIDGE.H`). `net` is an address, `192.168.1.20`, or an address and the number of
its leading bits a client's address must share, `192.168.1.0/24`: four decimal octets of 0 to
255, and 1 to 32 bits, none with a leading zero (which other tools read as octal). With a list,
a connection the bridge sees open from an address on none of its networks is reset at once,
before the bridge calls the server, and reported by a `REFUSE` event; without `/ALLOW:` every
address is let in. `INVTCP /ALLOW:192.168.1.0/24 /NAME:TCPGATE RETROINV` serves one subnet
only. The list keeps out mistakes and hosts beyond a router; a host on the bridge's own
Ethernet segment can take any allowed address (`DOC/SECURITY.md`, TCP through INVTCP).

The station needs:

- MS-DOS 3.1 or later;
- a NetBIOS provider, as a client of `INV /NET` does, with room for the bridge's name, its
  `TS_SLOTS` sessions and two NCBs per session;
- a packet driver for the card that carries TCP/IP, for example the Crynwr `NE2000.COM`
  (`NE2000 0x60 3 0x300` for the vector 60h and a card at IRQ 3 and port 300h). NetBEUI holds
  its card through its own NDIS driver, so with NetBEUI the bridge station has two cards:
  NetBEUI on one, the packet driver on the other. Sharing one card would need an
  NDIS-to-packet-driver shim such as `DIS_PKT`, which is untested;
- `WATTCP.CFG`, WATTCP's settings file, with at least the station's address: `my_ip=`,
  `netmask=` and, to reach other networks, `gateway=`. WATTCP reads it from the directory that
  the environment variable `WATTCP.CFG` names or, when that is not set, from the program's own
  directory, failing that from the current one. The Watcom C 6.5 build, whose library keeps no
  copy of the program's name, skips the program's directory: without the variable it reads
  `WATTCP.CFG` from the current directory only. Every release disk that carries the server
  carries a sample (`REL/WATTCP.CFG`), which `INSTALL` copies with the server.

The server needs `SESSIONS=` room for the bridge's sessions beside its other clients.

### The bridge's start-up and exit statuses

The bridge starts in this order; the first step that fails ends it with the status in
brackets, said on standard error in English:

1. the DOS version (3);
2. the command line (1);
3. the critical-error handler, which answers Fail (Retry where DOS allows no Fail, as Abort
   would leave the packet driver's receiver in place, so that call waits until its error
   clears), and BREAK checking off, so that neither Ctrl-C nor Ctrl-Break ends the bridge;
4. the NetBIOS provider (5);
5. the TCP stack: the packet driver (10), then `WATTCP.CFG` and the stack itself (11);
6. the emergency stop (11);
7. the bridge: its NCBs and far memory (6), its NetBIOS name (9, said with the NetBIOS return
   code) and a listening socket on every slot (11).

The emergency stop is an exit handler: however the program ends, it stops the bridge as F3
does (below), so that neither the provider nor the packet driver writes into memory DOS has
freed. The exit statuses are the `TCP_EXIT_` constants of `SRC/TCP/INVTCP.H`; a number shared
with `INVSERV` means the same there:

| Status | Constant | Meaning |
|---|---|---|
| 0 | `TCP_EXIT_OK` | stopped by F3, also after `/?` |
| 1 | `TCP_EXIT_USAGE` | usage error: no server name or one that is not a NetBIOS name, a bad or repeated argument, more than `BR_ALLOWMAX` `/ALLOW:` |
| 3 | `TCP_EXIT_DOSVER` | DOS older than 3.1 |
| 5 | `TCP_EXIT_NONB` | no NetBIOS provider |
| 6 | `TCP_EXIT_NOMEM` | no memory for the NCBs or the buffers |
| 9 | `TCP_EXIT_NAME` | the bridge's name could not be added |
| 10 | `TCP_EXIT_NOPKT` | no packet driver |
| 11 | `TCP_EXIT_STACK` | `WATTCP.CFG` missing or giving no address, the packet driver or the stack refused to start, the emergency stop could not be set up, or a slot could not listen |

The bridge does not look for the server at start-up and never stops by itself: while the
server is down every connection ends in `NOCALL`.

### Connections

Each of the `TS_SLOTS` slots listens on `BR_PORT`; the stack refuses a connection beyond them.
SYNs whose handshake is never completed can hold every slot for WATTCP's `tcp_TIMEOUT` whatever
`/ALLOW:` says (`DOC/SECURITY.md`, Availability). A connection makes its slot CALL the server;
once the session is open, bytes go both ways, at most `NB_MSGMAX` in one NetBIOS message. A slot
ends:

- `CLOSE TCP` when the client closes or resets the connection: the session is hung up and the
  bytes not yet relayed are dropped. WATTCP has no half-close, so a client's FIN closes the
  connection at once: a client must read its answers before it closes its sending side;
- `CLOSE NETB` when the server ends the session (after `BYE`, the last failed `LOGIN`, its
  idle timeout or its shutdown): the client gets every byte the server sent, then the close;
- `CLOSE LOST` when the session is lost, or when the client takes none of the bytes waiting
  for it for `BR_DELIVERWAIT` ticks;
- `NOCALL` when the CALL fails: the connection, already accepted, is closed without a byte;
- `REFUSE`, in place of `OPEN`, when no network of the allow-list holds the client's address:
  the connection, already accepted, is reset before the CALL;
- `CLOSE STOP` when the bridge stops.

Each wait while a slot closes lasts at most `BR_CLOSEWAIT` ticks; a close the client never
completes is then aborted.

To the server every TCP client is a NetBIOS session whose station is the bridge's name;
`ADMIN CLIENTS` lists it with the type `NETB`, and `READONLY=` in `INVSERV.CFG` naming the
bridge caps every TCP client at level 1, whatever the user.

Request bytes hold passwords: the bridge zeroes its bounce buffer after each use, a slot's
transmit area once its send has completed, and both its areas when its connection ends. On both
networks they still travel in plain text, from any address that reaches the bridge unless
`/ALLOW:` names its networks (`DOC/SECURITY.md`).

### Events

Each event is one line on standard output; redirect it to keep a log:

```
YYYY-MM-DD HH:MM:SS slot EVENT text
```

Slot 0 is the bridge itself: `START` with the server's name and `BR_PORT`, and `STOP` with the
exit status. Slots 1 to `TS_SLOTS` are the connections the bridge sees open (one the client
ends before then leaves no line): `OPEN` with the client's address and port, then `CALL` when
the session opens and `CLOSE` with its reason, or `NOCALL` with the NetBIOS return code in two
hex digits; or `REFUSE` alone, with the address and port of a client whose address no network of
the allow-list holds. The words are fixed upper-case codes, never translated, and no line ever
holds a byte relayed. The loop reads the date through DOS at each midnight, as the server's does
(Sessions, above), so the stamps keep the right date after idle days.

### Stopping the bridge

F3 asks on standard error, so that the question shows when the event lines go to a file,
`sessions open: n.  Stop the bridge?  Y=Yes N=No`, n the NetBIOS sessions the bridge holds, and
the bridge goes on relaying. Y, N or Esc answers, as in INV, and no other key counts: N or Esc
leaves every connection as it is. F3 asks again whenever it is pressed, also before an answer,
as the event lines scroll the question away. Y stops at once: every connection is reset
(`CLOSE STOP`), every session hung up, the name deleted, the packet driver released, and the
BREAK setting and the critical-error handler put back. While the NetBIOS provider still owns an
NCB of the bridge, which it could write into after the program ends, INVTCP says how many on
standard error and waits; a key ends the wait at the operator's risk.

### The bridge's memory

Besides its code and near data (within `INVTCP_MAXNEAR` of `SRCS.MK`), the bridge takes one
far block of 2 × `TS_SLOTS` × `NB_MSGMAX` bytes for the slots' transmit and receive areas, and
WATTCP its socket buffers. Its free-memory figure, `INVTCP_FREEKB_<build>`, and how it is
measured are in `DOC/BUILD.md` (Runtime requirements).
