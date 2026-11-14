# RETROINV — Security

What RETROINV protects, what it leaves to the site, and how a site keeps its inventory safe. It
is written for whoever installs and runs the server: read it before a network, a serial line, a
mailbox share or the TCP bridge serves the data. What a site does comes first
(Recommendations); the sections after it give the reasons.

RETROINV keeps the access model of its era, dBASE III Plus PROTECT (1986): user accounts with a
password and a level, checked by the server for every request on every link. It does not
protect the network or the files:

- **Protected:** who may do what through the protocol. Every command needs a login and a level
  (Authorisation), every change is recorded with its user and station, and RETROINV keeps and
  logs no password in plain text, outside the mailbox's request files, which hold one until it
  is read.
- **Not protected:** what crosses a link. On every link but local mode, every request, answer
  and password travels in plain text (Transports), and the period network software beneath
  RETROINV keeps copies of it that RETROINV cannot reach (The period drivers).
- **Not protected:** the files. Anyone who can read the data directory, a backup or the mailbox
  reads the inventory; anyone who can write the data directory can change any record, the user
  accounts included, beyond the reach of the levels and of the audit trail (Data at rest).

Two terms recur. A **station** is the name a session's machine goes by: the server writes it in
every audit row and caps the stations `READONLY=` names at level 1, and each link takes it from
somewhere else (Transports). A **login ticket** is a code the server gives a mailbox client at a
password login, so that its later request files log in without the password (Mailbox).

The figures below are the values of constants in the sources; Limits, at the end, says where
each one lives.

## Recommendations

1. **Keep the network to machines you trust.** Run the server, the bridge and their clients on a
   LAN whose machines, and their users, you trust with the passwords that cross it. Put no
   machine you do not trust on the server's segment.
2. **Keep the bridge on the LAN.** Never forward `INVTCP`'s TCP port, 7000 (`BR_PORT`, the last
   word of its `START` line), from the Internet or another network; where the bridge's network
   is shared, put a firewall in front of it that passes that port from known hosts only. Name
   the clients' addresses or networks with `/ALLOW:` as well (`/ALLOW:192.168.1.0/24`), so that
   the bridge resets every other connection, though a host on the bridge's own segment can take
   an allowed address (TCP through INVTCP). Give the bridge a fixed NetBIOS name,
   `INVTCP /NAME:TCPGATE RETROINV`, so that the audit trail and `READONLY=` can name it: without
   `/NAME:`, on MS Network Client 3.0, it calls from a name made anew at every start. Keep its
   event lines: `INVTCP /NAME:TCPGATE RETROINV >>INVTCP.LOG` appends them to a file, and F3
   still stops it, its question showing on the screen.
3. **Give every user a password of their own,** used nowhere else, as it crosses the network in
   plain text and lingers in padding, and as the hash kept of it does not withstand guessing
   once `USERS.DBF` is copied (Users and passwords). Use the full length, up to 16 characters
   (`ENG_PWMAX`).
4. **Close the published passwords** before the server serves anyone, as `DOC/INSTALL.md`
   (First login) says: a new password for `ADMIN`, a new temporary one for each sample user you
   keep, and the rest deleted.
5. **Keep the files to the server.** Keep the data directory on the server PC's own disk and
   share neither it nor a directory above it. Give the mailbox's share a full-access password
   only its stations' users know. Keep backup disks and exported files as you keep the data, and
   do not leave the server's PC where others can use it.
6. **Give the levels sparingly.** Level 1 reads the names and contacts of borrowers and of whom
   items are reserved for or used with; level 3 also works on the server's files. Treat
   `READONLY=` as a convenience, not a barrier.
7. **Read the trail.** Admin > Audit log in `INV`, with Table `USERS`, or `AUDIT LIST TBL=USERS`,
   shows the failed logins (`F`) with the name tried; many of them mean someone is guessing, and
   a user who cannot log in with the right password is being guessed at (Failed logins).
   Keep `LOG=` set, and `IDLE=` above 0 (its default is 10 minutes, `SRVCFG_DEFIDLE`) so that
   abandoned sessions end.

### A lost password

A user who has forgotten their password asks an administrator, who sets a new one with
`USER RESET name PASSWORD=` (Admin > Users in `INV` or `INVWIN`: open the user, type the password
twice, then Enter in `INV`, OK in `INVWIN`). The user must change it at the next login. A reset
also ends the user's login tickets.

If no administrator can log in, restore `USERS.DBF` and `USERS1.NDX` from a backup whose
administrator password is known, with every program stopped, or give one administrator the
published password `admin` again in dBASE III Plus:

1. Stop every RETROINV program, `CD` to the data directory, and keep a copy:
   `COPY USERS.DBF USERS.OLD`.
2. Start dBASE (`DBASE`), then `USE USERS` and `LIST USERNAME, LEVEL, STATUS` to see the
   administrators (level 3).
3. Set the salt and hash of the password `admin`, which `DOC/ENGINE.md` gives, the forced change
   and the active status, for one administrator, here `ADMIN`:

   ```
   REPLACE SALT WITH "0000", PWHASH WITH "A049CE34F45C0DB9864186CE4761D6AB", MUSTCHG WITH .T., STATUS WITH "A" FOR USERNAME = "ADMIN   "
   ```

   The name is padded with spaces to the field's 8 characters (`ENG_USERLEN`): dBASE compares
   only as many characters as the quoted name has, so `"ADMIN"` alone would also match `ADMIN2`
   and every other name that starts with it. Change no other field.
4. `USE`, then `QUIT`.
5. Start `INV`, log in as that user with the password `admin`, and choose a new password at once.

The hash covers only the salt and the password, so the same values work for an administrator of
any name, written in place of `ADMIN` and padded alike. That this works is the point of Data at
rest: whoever can write the data directory can make themselves an administrator.

## Authentication

### Users and passwords

`USERS.DBF` holds the accounts: a user name of 1–8 characters `A-Z 0-9 _` (`ENG_USERLEN`), a
level from 1 to 3, a status (active or disabled), a full name and a group (`DOC/ENGINE.md`,
USERS.DBF). A password is 4–16 characters (`ENG_PWMIN`–`ENG_PWMAX`) 21h–7Eh, case-sensitive.

The file holds no password, only a salted one-way hash of it: 4 hex digits of salt
(`ENG_SALTLEN`), made anew at every change, and `PWHASH`, 32 hex digits (`ENG_HASHLEN`) mixed
from salt and password (`DOC/ENGINE.md`, Users and the password hash). It is a hash of its era,
integer arithmetic an 8088 runs at a login, not modern cryptography. What it does: it keeps
passwords out of the file, out of a dBASE `BROWSE` and out of a backup, and together with logs
and an audit trail that take no password (What is recorded) it leaves none in the data files,
the logs or the audit trail; a mailbox's request files hold theirs until the server has read
them (Mailbox). What it does not: resist guessing by whoever holds a copy of `USERS.DBF` or
of a backup. `eng_mix128` is one quick pass with `ENG_HASHROUNDS` final rounds, so guesses are
tested at a modern computer's speed, away from the server and its limit on failed logins, and
the salt's `ENG_SALTLEN` hex digits are so few values that the guesses can be worked out once
for every salt: a short or common password falls at once. Nor does it cover the links, where
the password itself travels (Transports). So keep the data directory and its backups from being
copied (Recommendations, Keep the files to the server), give every user a password used nowhere
else, and have every password changed once a copy may have been taken.

`USER LIST` and `USER GET` never send SALT or PWHASH, and `EXPORT` takes no `USERS`. RETROINV
wipes its own copies of a password from memory once used: the clients after each request, the
server and the bridge when a request has been served or a session ends.

### The forced change

A user made by `USER ADD`, or whose password an administrator set with `USER RESET`, must change
it at the next login: `LOGIN` answers `MUSTCHG=1`, and until `PASSWD old new` succeeds the
session runs only HELLO, PING, VER, BYE, LOGIN and PASSWD (`DOC/PROTOCOL.md`, Identity and
login). `INV` and `INVWIN` ask for the new password at once. A new password must differ from the
old one. `PASSWD` changes only the logged-in user's own password; only `USER RESET`, at level 3,
sets another user's.

The forced change protects a password that someone else chose, not an account nobody has used
yet: until its first login, whoever knows the initial password logs in and chooses the new one.
The first passwords of a new data directory's `ADMIN` and of the sample data's users are
published (`DOC/INSTALL.md`, First login), hence "Close the published passwords" in
Recommendations, above.

### Failed logins

A wrong user name and a wrong password get the same answer, 401. Two limits bound the guesses:

- a session closes after its third failed login (`PROTO_MAXFAIL`); only a successful login
  resets the count. A caller who opens a new session, a new connection, a new serial session or
  a new mailbox file starts again;
- the server's throttle counts the failed password logins of each user over every session and,
  past its limit, refuses that user's password logins for a while, the right password included,
  as wrong ones, so a guess that happens to be right then is never known to be
  (`DOC/SERVER.md`, Failed logins). It lives in the server's memory: a restart clears it, and
  local mode has none.

No account is ever locked, so no guesser can lock one out for good, and however a guesser
spreads the failures, the throttle allows only a few guesses at a user every few minutes; its
price is a denial of service (Availability, below).

`INV` and `INVWIN` pause about a second (`TMR_SEC` ticks) after each refused login, as PROTECT
did, but that is the client's courtesy: any other program speaking the protocol sends its
guesses as fast as the link carries its lines, up to the throttle's limit for each user.
Each failure is recorded (below), so a guesser leaves a trail an administrator can read. A
wrong old password in `PASSWD` is refused but is no failed login: it lets a logged-in user test
guesses only at their own password.

A wrong user name also takes the time of a wrong password, so that timing the answers does not
tell which names are users': a login naming no user reads a record of `USERS.DBF`, the first,
where one naming a user reads that user's, and hashes the password it was given as a wrong
password is hashed; a login with a ticket naming no user reads that record too, and a login the
throttle refuses does the work of a wrong password. Three differences remain: DOS may hold one
record in its buffers and not another, so that one read is quicker; a name whose user was
deleted also reads that user's old record until `ADMIN PACK` or `DBPACK`; and the server's
throttle counts a failure of a user's name, a few copies within memory, but not of other names.
A name no user can have, malformed or `SYSTEM`, is refused without a lookup.

A disabled user is refused with 403 only once the password has been checked, so only someone who
knows the password learns the account is disabled. Disabling a user or changing a level takes
effect at that user's next login: a session already open keeps its level until it ends.

### What is recorded

- **The audit trail** (`AUDIT.DBF`, read with `AUDIT LIST` at level 3; `DOC/ENGINE.md`, Audit
  rows) gives every row a date, a time, a station and a user. It records every login with a
  password (`L`), every failed login (`F`, with the name tried, or `?` when it names no live
  user), every password change or reset (`W`, without values), every change to a record with its
  old and new values cut to 40 characters (`ENG_AUDVAL`), and `IMPORT`, `ADMIN PACK`, `REINDEX`,
  `BACKUP` and `RESTORE` (`K`). Reads, `EXPORT` and `ADMIN SHUTDOWN` leave no row.
- **The server's event log** (`LOG=` of `INVSERV.CFG`, none by default; the sample settings
  file sets `INVSERV.LOG`; `DOC/SERVER.md`, The event log) has a line for every session's
  opening with its station (none on a serial line), every request answered, the user of every
  login that succeeds, and every close. A request is logged by its first words only, never an
  argument; a failed login is a `REQ LOGIN` line with no `LOGIN` line after it. The trace row of
  the status screen shows the same.
- **The bridge's event lines** (`DOC/SERVER.md`, Events) give each TCP connection's address and
  port, its NetBIOS call and its close, or its refusal by the allow-list, on standard output
  only.

No password and no login ticket is written to any of them.

## Authorisation

Every command is checked in `DISPATCH.C` against the session's state and the user's level, the
same way for every link, so a client cannot do more by sending its own requests than its user
may. The level of each command is in the command-level table of `DOC/ENGINE.md` (Protocol
layer). In outline:

| Level | Sees and does |
|---|---|
| none | `HELLO`, `PING`, `VER` (which gives the version) and `BYE` answer any caller; `LOGIN` after `HELLO` |
| 1, read-only | the whole inventory: items and their notes, locations, assemblies, service records, the reports, and the loans, reservations and uses, with each one's holder (`HOLDER`: the borrower, or whom a hold is for or with), its host item and its contact in `LOAN GET`, `LOAN LIST`, `LOAN OVERDUE` and `REPORT LOANS`; `WHOAMI`, `STATUS` and its own `PASSWD` |
| 2, operator | also adds, changes and deletes items, categories, locations and their types and service records, builds and changes assemblies, and lends, reserves and marks items in use, changes those rows, and returns and releases them |
| 3, administrator | also the user accounts, the audit trail, the sessions open (`ADMIN CLIENTS`), shutdown, pack, reindex, and the commands that work by path on the files of the PC running the engine, the server's: `EXPORT` writes and `IMPORT` reads any path there, `ADMIN BACKUP` and `ADMIN RESTORE` any directory, and `ADMIN RESTORE` replaces the whole data, accounts included |

Level 1 is therefore no privacy boundary within the inventory: a site whose holders' names and
contacts must stay private gives level 1 only to people who may read them. Level 3 is trust in
the server's PC as well as in the data.

`READONLY=` of `INVSERV.CFG` caps the stations it names at level 1, whatever the user. A station
is a name the caller's own machine chooses (Transports), so the cap keeps a known machine, a
public catalogue terminal say, from changing data, but stops nobody who controls their machine's
settings. The user's level is the control; `READONLY=` is a convenience on top.

## Transports

No link encrypts its bytes or proves which machine is at the other end. What each one exposes:

| Link | Who can connect | Station, as the audit trail names it |
|---|---|---|
| Local (`INV`, `INVWIN` local) | whoever runs the client on a PC that reaches the data directory | `HELLO`'s: `STATION=` of `INV.CFG`, else the DOS machine name, else `PC` (`CFG_DEFSTATION`) |
| NetBIOS (`/NET`) | any machine whose NetBIOS reaches the server | the caller's NetBIOS name |
| Serial (`/COM1`, `/COM2`) | whoever holds the other end of the cable | `HELLO`'s, advisory |
| Mailbox (`INVWIN /MBOX:`) | any machine that can write the mailbox's `REQUEST\` | the request file's name |
| TCP (`INVTCP`, `INVWIN /TCP:`) | any IP address that reaches the bridge's PC, or with `/ALLOW:` one on its networks | the bridge's NetBIOS name, for every TCP client |

### Local

The engine runs inside the client on the data directory. The login and the levels apply, but the
person at the PC can reach the files themselves (Data at rest), so on a PC that holds the data
the accounts keep honest users apart, no more. No byte leaves the PC, unless the data directory
is on a network drive.

### NetBIOS

The server adds its `NAME=` (by default `SRVCFG_DEFNAME`) to the NetBIOS network and accepts a
call to it from any station, up to `SESSIONS=` at once. Any station can find the name with a
NetBIOS name query. NetBEUI, the tested provider, cannot be routed, so the NetBIOS network is
the LAN segment and anything bridged to it; a NetBIOS provider that runs over TCP/IP would carry
the sessions as far as its IP network reaches.

Every line crosses the cable as it is: a `LOGIN` frame holds the user name and the password in
plain text. On a hub or a coaxial cable every station sees every frame; on a switch, a station
sees the frames sent to it, and also, through the padding of short frames (The period drivers),
pieces of what the sender recently sent to others.

The station is the caller's NetBIOS name, whatever `HELLO` says, but a machine may register any
name that is free. `INV` and `INVWIN` call from the DOS machine name; MS Network Client 3.0 sets
none, so there they call from a name of their own, `RINV` and four hex digits, new at every
start, and the audit trail cannot tell which PC such a client ran on.

### Serial lines

A serial line has two ends and no address: whoever holds the other end of the cable is the
caller, and the station is whatever `HELLO` names. A client that goes without `BYE` leaves its
session logged in to the next person on the line unless the server sees the hang-up, which needs
a full null-modem cable; on a cable without modem signals only `IDLE=` ends it
(`DOC/SERVER.md`, Serial lines).

### Mailbox

The mailbox is a directory, usually on a file server's share, and its rights are its only
guard:

- Any machine that can create files in `REQUEST\` can post a request under any station name, and
  try three passwords per file, one file after another, up to the throttle's limit (Failed
  logins).
- A request file holds its password, or its login ticket, in plain text until the server has read
  it, then zeroes and removes it; a `.TMP` that a client left when it stopped before the rename
  keeps it until that station's client posts again (`DOC/SERVER.md`, The mailbox). On a share,
  the file crosses the network as file-server traffic, in plain text too.
- Any machine that can read `RESPONSE\` reads the answers meant for other stations, and the
  login ticket in a response until its client has zeroed it. A ticket logs in as its user only
  under its own station, but the station is a file name anyone with write access can use, so a
  ticket read there logs in until it ends (`DOC/SERVER.md`, Login tickets).
- When the mailbox is on the server's own disk, a response that gives a ticket may leave part of
  it elsewhere on that disk. The server zeroes requests and pads responses against the way DOS
  writes a file's last sector (`DOC/SERVER.md`, The mailbox), but it cannot zero a response its
  client has yet to read. Until a later write through DOS overwrites the buffer that held it
  (the client's zeroing does, when the share's server writes through the same DOS), the
  server's first write past the end of another file (a data file, a memo or index page,
  `INVSERV.LOG`) may carry part of the ticket into that file's slack; DOS 3.1 has no call that
  clears its buffers. Such a copy is read only from the raw disk, and the ticket ends as any
  other does. A server that reaches the mailbox through a redirector keeps no response in its
  DOS buffers.

A ticket is 32 hex digits (`PROTO_TKTLEN`) mixed from the server's timing, and a failed login
with a ticket ends its session, so a guesser gets one try per request file, served one at a time:
too few to find one. The server keeps only a mix of each ticket, in memory only, and drops it at
the first login that shows it under the wrong user or station.

### TCP through INVTCP

`INVTCP` accepts a TCP connection on its port from any IP address, up to 4 at once (`TS_SLOTS`),
and relays its bytes unchanged over a NetBIOS session to the server (`DOC/SERVER.md`, The TCP
bridge). The lines, passwords included, therefore cross two networks in plain text: the IP
network from the client to the bridge, and the NetBIOS network from the bridge to the server. A
router that forwards the port exposes the server's login to every host that reaches it.

Started with `/ALLOW:`, the bridge resets at once, before any byte reaches the server, a
connection it sees open from an address on none of the networks named, and logs it as
`REFUSE`. The check trusts the source address. On the bridge's own Ethernet segment any host can
take an allowed address: WATTCP answers a SYN at the Ethernet address it came from, so that host
needs to guess nothing. From beyond a router a host must forge the address blind, and the
bridge's unpredictable sequence numbers (WATTCP, below) make that hard. `/ALLOW:` therefore keeps
out mistakes and hosts beyond a router, not a host on the bridge's segment; a firewall stays the
barrier.

To the server every TCP client is one station, the bridge's NetBIOS name: the audit trail and the
server's log name the bridge, and `READONLY=` naming the bridge caps every TCP client. Only the
bridge's event lines give a client's IP address and port, matched to the server's sessions by
time; they are kept only when the bridge's standard output goes to a file.

A TCP client that reconnects starts a new session, but not a new count of the throttle's
(Failed logins).

### Availability

A caller need not log in to take a session: any station that can connect holds a slot (one of
`SESSIONS=` on NetBIOS, of `TS_SLOTS` on the bridge, the one session of a serial port) until
`IDLE=` ends it, and can so keep others out. On the bridge a connection need not even open: SYNs
whose handshake is never completed, from any address, forged ones included, hold every one of
the `TS_SLOTS` slots for 13 seconds each (WATTCP's `tcp_TIMEOUT`), whatever `/ALLOW:` says, as
the bridge sees only connections that open. A caller who fails as one user keeps that user's
password logins refused, and one who knows enough user names, every user outside the throttle's
table (Failed logins). Each failed login adds a row to `AUDIT.DBF`, so endless guessing also
grows that file.

## The period drivers

RETROINV hands the network software exact lengths and zeroed control blocks and wipes its own
copies of each request, but the drivers beneath it are period software with period habits.

### MS Network Client 3.0 NetBEUI

In short: a password typed on one PC can reappear inside unrelated frames that PC sends later.

An Ethernet frame is at least 60 bytes long, and a shorter one is padded. MS Network Client 3.0's
NetBEUI stack, on the NDIS driver of the tested card, fills that padding with stale bytes of its
transmit buffers, left from frames it sent earlier; the frames do not show whether NetBEUI or the
card's driver puts them there. Up to about 40 bytes of an earlier frame thus go out again inside
short frames, such as acknowledgements:

- a client's short frames repeat its last request, a `LOGIN` or `PASSWD` line with its password
  included;
- the server's short frames to one session carry pieces of answers sent to another;
- a reserved field of its add-name query frames carries a NetBIOS name of an earlier frame.

The bytes go to whichever station the short frame is for. The bridge's NetBIOS side runs on the
same stack, so its frames carry the requests of its TCP clients the same way. Nothing in RETROINV
can change it; another NetBIOS stack or card driver may pad with zeros, which a network capture
shows.

### WATTCP

In short: a host on the bridge's IP network can break, misroute or fake its connections, which
is why the bridge must stay off the Internet.

`INVTCP`'s TCP/IP stack is WATTCP of 1994 (`DOC/DESIGN.md`, Design decisions: TCP stack). It
pads short frames with zeros, so its own frames carry no stale bytes. It has the habits of its
time:

- it answers ping, so the bridge's PC is easy to find;
- WATTCP started every connection of a listening socket at a fixed sequence number, which let a
  host that cannot see the replies forge a connection from another address; RETROINV's change to
  it (`SRC/TCP/WATTCP.DIF`) starts each at a number mixed from a secret pool, which takes in
  the timer chip's count on every pass of the bridge's loop, with a counter and the tick
  (`wt_isn`, `SRC/COMPAT/CWATT.C`). The numbers a host sees never give the pool away, so a host
  beyond a router cannot predict the next one; it is not cryptography, and it does nothing
  against a host on the bridge's own segment, which sees the replies (TCP through INVTCP);
- it acts on ICMP messages from any sender: a few forged "destination unreachable" or "time
  exceeded" messages abort a connection, a source quench slows it, and a redirect sends its
  frames to the host the redirect names.

## Data at rest

- **The data directory** holds plain dBASE III files, which dBASE or any program that reads them
  opens: the inventory, the loans and holds with holders and contacts, `USERS.DBF` with the
  hashes, and `AUDIT.DBF` with the history of every change. Deleted records, and the text a memo
  had before its last change, stay in the files until `ADMIN PACK` or `DBPACK`; `AUDIT.DBF` is
  append-only, though `ADMIN RESTORE` puts back the backup's copy. DOS has no file permissions:
  on a PC that others use, anyone at its keyboard reads and writes them. While the server runs
  it holds every file open, denying other programs, which under `SHARE.EXE` stops a second
  program opening them by mistake, not a person once the server is stopped. A change made to the
  files outside RETROINV writes no audit row.
- **The mailbox** (Mailbox, above) holds request files with their password or ticket until the
  server zeroes them, a `.TMP` left by a stopped client, and response files with their answers.
- **The logs.** `INVSERV.LOG` names stations, users and the commands they ran, with times; the
  bridge's event lines name the clients' IP addresses. Neither holds a password.
- **Backups and exports.** `ADMIN BACKUP` copies every table, memo file and index, `USERS.DBF`
  and `AUDIT.DBF` included, and `EXPORT` writes a table as text: each is the data, to be kept as
  carefully. A backup restored brings back the accounts and passwords of the day it was made.
- **The clients** write no password to a file: `INV.CFG` keeps the station and the user name
  that the login offers, and `INVWIN` keeps a login ticket in memory only. Other programs may:
  a terminal's scrollback after `nc`, or a script of requests given to `nc` or `INV /CMD`, keeps
  a `LOGIN` line as it was typed.

## Limits

| Constant | Header | Value | Meaning |
|---|---|---|---|
| `PROTO_MAXFAIL` | `SRC/PROTO/PROTO.H` | 3 | failed logins that close a session |
| `THR_FAILS` | `SRC/SERVER/THROTL.H` | 5 | failed logins of a user that start the throttle's refusal |
| `THR_SPAN`, `THR_HOLD` | `SRC/SERVER/THROTL.H` | 5 minutes | ticks in which they count, ticks the refusal lasts |
| `THR_MAX` | `SRC/SERVER/THROTL.H` | 8 | entries of the throttle's table: users, and one shared by the others |
| `ENG_USERLEN` | `SRC/ENGINE/ENGINE.H` | 8 | characters of a user name |
| `ENG_PWMIN`, `ENG_PWMAX` | `SRC/ENGINE/ENGINE.H` | 4, 16 | characters of a password |
| `ENG_SALTLEN`, `ENG_HASHLEN` | `SRC/ENGINE/ENGINE.H` | 4, 32 | hex digits of SALT and PWHASH |
| `ENG_HASHROUNDS` | `SRC/ENGINE/ENGUSER.H` | 64 | final rounds of `eng_mix128` |
| `ENG_AUDVAL` | `SRC/ENGINE/ENGINE.H` | 40 | characters of an old or new value in an audit row |
| `PROTO_TKTLEN` | `SRC/PROTO/PROTO.H` | 32 | hex digits of a login ticket (`ENG_HASHLEN`) |
| `SRVCFG_DEFIDLE` | `SRC/SERVER/SRVCFG.H` | 10 | minutes of `IDLE=` when the settings give none |
| `SRVCFG_DEFNAME` | `SRC/SERVER/SRVCFG.H` | `RETROINV` | the server's NetBIOS name when `NAME=` gives none |
| `TMR_SEC` | `SRC/COMPAT/COMPAT.H` | 18 | timer ticks in about a second |
| `BR_PORT` | `SRC/TCP/BRIDGE.H` | 7000 | the bridge's TCP port |
| `TS_SLOTS` | `SRC/TCP/TSOCK.H` | 4 | TCP connections the bridge serves at once |
| `BR_ALLOWMAX` | `SRC/TCP/BRIDGE.H` | 4 | networks of the bridge's `/ALLOW:` list |
| `CFG_DEFSTATION` | `SRC/CLIENT/CFG.H` | `PC` | a client's station when nothing else names one |
