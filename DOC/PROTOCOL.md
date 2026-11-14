# RETROINV — Wire Protocol

One ASCII line protocol between a client and the engine. Every transport carries the same
lines into `proto_dispatch`, which makes the level checks, so all of them enforce the same
permissions. Limits are given as a value with the name of its constant in
`SRC/PROTO/PROTO.H`, `SRC/ENGINE/ENGINE.H` or `SRC/ENGINE/ENGCSV.H`.

## Transports

Every transport but local mode carries the lines as they are, in plain text, passwords
included, and none of them proves which machine is calling: `DOC/SECURITY.md` says what each
one exposes.

- **Local mode**: the full-screen client calls `proto_dispatch()` directly and parses the bytes
  `proto_dispatch()` writes.
- **NetBIOS**: sessions with the server `INVSERV` (`DOC/SERVER.md`, Sessions). A message holds
  at most `NB_MSGMAX` bytes (`SRC/COMPAT/COMPAT.H`): several lines or part of one. The server
  receives a session's next message only once it has taken up the last request of the one
  before, and takes up a request only after the response before it is sent. A client that sends
  each request in a message of its own and keeps a RECEIVE of one message posted, as `NBCLI.C`
  does, therefore gets three SENDs through: the SEND of a fourth waits until the client has read
  the first response, and already that of a third when no RECEIVE is posted or the first
  response takes more than one message. `NBCLI.C`'s callers keep at most 2 requests in flight,
  so no SEND of theirs waits for a read. A client posts its RECEIVE before it sends a request:
  the server's provider may acknowledge the request in the frame that carries the response, and
  NetBEUI sends again a message that finds no RECEIVE posted. A TCP client reaches the server
  through the bridge `INVTCP` (`DOC/SERVER.md`, The TCP bridge), which relays its connection
  over a NetBIOS session; the bridge's stack has no half-close, so a TCP client reads its
  answers before it closes its sending side.
- **Serial lines**: a byte stream on COM1 or COM2 (8 data bits, no parity, 1 stop bit) with no
  framing of its own. A session opens at the first byte and has no station until HELLO names
  one; when and how it ends, and what a client may send ahead of its responses, are in
  `DOC/SERVER.md` (Serial lines). The line has no checksum and cannot resynchronize, so each
  end watches its port's receive errors (overruns, framing and parity errors, `uart_rxerrs` of
  `COMPAT.H`): the server carries out no request line received while its port counted one, and
  ends the session without a response, as on a hang-up; the client takes the line as lost
  (`SERCLI_ELOST`) when one is counted while a response is due, and shows nothing read after
  it. A noise byte the port counts thus ends the session at its first line, unless it came
  while no session ran: the server then drops it and opens none. One that arrives as a valid
  byte before HELLO opens a session and becomes part of the first request line, which is
  refused with 101 (noise alone holds the slot until the idle timeout); so is a line whose
  start was lost as the server's port opened. The refusal answers the garbled line, so the
  session stays in step, and the client sends HELLO once more. The server sends nothing but
  responses, so a client drops whatever it received while no response was due before each
  request. An LF that noise turns into
  another valid byte joins two requests into one and leaves the client one response out of
  step until its wait for a response runs out. INV then takes the line as lost, says so and
  ends (`DOC/CLIENT.md`), sending a last CR LF and BYE without waiting for an answer, so that
  the server ends the session.
- **The mailbox**: a directory, shared or not (`DOC/SERVER.md`, The mailbox). A client leaves
  one request file per session, `REQUEST\<station>.REQ`, written under another name and renamed
  once complete; the server answers with `RESPONSE\<station>.RSP`, renamed into place the same
  way. The file's base name, at most 8 characters (`TMBOX_STALEN` of `SRC/SERVER/TMBOX.H`), is
  the station; the lines still start with HELLO and LOGIN. The session ends at the file's end,
  or earlier (BYE, the last failed LOGIN, a failed LOGIN with a ticket, the idle timeout, a
  shutdown) with the rest of the file ignored. A session that a failed write ended still puts
  in place what was written, so a client reads a response that stops short as cut. A request
  file longer than 32766 bytes (`CMEM_MAXBLK` of `COMPAT.H` less 2), or one the server has no
  memory for, gets an empty `.RSP`, unserved. A session of the mailbox, and only of it, may log
  in with a login ticket (Session, below), so that a client sends its password in one file
  only, as the Windows client's MBOX link does (`DOC/CLIENT.md`, MBOX).

## Framing

- Lines end with CR LF; a bare LF is accepted on input. A request line holds at most 292 bytes
  (`PROTO_MAXLINE`; a longer one is 103), a response line at most 512 (`PROTO_MAXRESP`), CR LF
  excluded in both. Every request fits `PROTO_MAXLINE` with its arguments at their longest and
  quoted where they may need it; one that escapes bytes that need no escape can pass it.
- One request gets exactly one response; a response that is not paged is at most 4096 bytes
  (`PROTO_MAXUNPAGED`). A line that is empty or holds only spaces and tabs is no request and
  gets no response; a line of `KEY=value` pairs only is 100 (`no command`).
- Keywords are case-insensitive; values keep their case. CP437 high bytes pass unchanged.
- Tokens are separated by spaces or tabs. A token with `=` outside quotes is a pair, whose key
  is 1–10 characters (`PROTO_MAXKEY`) of `A-Z a-z 0-9 _`, a letter or `_` first, upper-cased.
  `"..."` quotes a positional token or a pair's value and keeps spaces and `=`; inside quotes
  the escapes are `\"`, `\\`, `\n` (LF, a memo line break) and `\xHH`; outside them `\` is an
  ordinary character. A request is the command and its positional arguments, at most 8 tokens
  in all (`PROTO_MAXARGS`), followed by at most 24 pairs (`PROTO_MAXPAIRS`) in any order.
- Syntax errors (100): an unterminated quote, an unknown escape, `\x` not followed by two hex
  digits or followed by `00`, a raw NUL, a `"` inside a bare token, anything but a space or tab
  after a closing quote, a bad key, a positional token after a pair, too many tokens, and a
  positional argument the command does not take (`too many arguments`). A key given twice is
  102 naming it.

## Responses

```
+OK [text]
-ERR <code> <text> [FIELD=name]
+DATA
<data lines>
.
```

Data lines inside `+DATA` begin with a kind letter and a space:

| Kind | Meaning |
|---|---|
| `F f1 f2 ...` | field header, sent once before records |
| `R v1 v2 ...` | one record, values positional per `F` |
| `L text` | text line (the memo lines of the NOTES commands) |
| `H text` | heading; reserved, no command sends it |
| `P` | page break; reserved, no command sends it |
| `K key=value` | key/value information |
| `C <cursor>` | continuation: more results exist; repeat the request with `AFTER=<cursor>` |

Data lines that begin with `.` are dot-stuffed (`..`); the terminator is a line holding a
single `.`. GET commands answer with one `K FIELD=value` line per field in file order, hidden
and memo fields left out; lists answer with `F` and `R` lines. A value is sent bare unless it
is empty or holds a space, `"`, `\`, `=` or a byte below 20h or 7Fh; then it is quoted with the
tokenizer's escapes (`\n` for LF, `\xHH` for the other control bytes). A failure after `+DATA`
has begun is sent as a last line `K ERROR=<code>` before the terminator.

The NOTES commands send a memo as `L` lines, unquoted. A stored line ends at CR LF, a lone LF,
a lone CR or 8Dh 0Ah (the dBASE soft return). In the text `\` is written as `\\`, and bytes
below 20h other than TAB, and 7Fh, as `\xHH`; once 200 written bytes (`PROTO_MEMOLINE`) fill
an `L` line, the stored line continues on the next one. An empty stored line is sent as `L`
alone. When the lines would pass `PROTO_MAXUNPAGED` (possible only for a memo written by
another program) they stop there and `K TRUNCATED=1` precedes the terminator.

## Paging

`PAGE=n` (1..25, `PROTO_PAGEMAX`; default 20, `PROTO_PAGEDEF`; else 102 `FIELD=PAGE`) is a
maximum. A page stops early, even when partial, once the connection's output buffer is nearly
full (`PROTO_ROOMMIN`) or the scan has examined 200 entries past the cursor (`ENG_MAXREADS`,
counted as `DOC/ENGINE.md` describes in "Cursors and bounded scans"), so every request moves
forward by at least one entry. A trailing `C` line means "call again with `AFTER=`"; the next
page may be empty. The cursor is opaque (`index:hexkey:recno`, at most 136 characters,
`ENG_CURSORLEN`) and stateless: the server keeps no per-cursor memory, and a cursor it cannot
use is 204. Backward paging is done client-side by stacking page cursors.

`FIELDS=a,b,...` chooses the columns of the lists that take it: field names of the listed table,
in any case, at most 40 characters (`ENG_MAXFSPEC`), 24 fields (`ENG_MAXFLIST`) and 160
characters of field width (`ENG_MAXRWIDTH`) (else 102 `FIELD=FIELDS`, as is an empty name in the
list); an unknown, memo or hidden field is 102 naming it. Without it a list sends the table's
default fields, those flagged `LST` in `SRC/ENGINE/SCHTAB.H`. An item row also has two columns
that are no field of ITEMS, each right after its field in the default columns and allowed once
in FIELDS=, outside its field limits: `LOC_NAME`, the name of the item's location (empty for
none), after LOC_ID; and `AVAIL`, after QTY, one letter saying whether the item is available
(`Y`, as `DOC/ENGINE.md` (Counters) defines it) or why not: `L`, `R` or `U` for the first of
ON_LOAN, RESERVED and IN_USE above 0 when they hold every unit, `I` installed in another item,
`D` disposed of, `N` a QTY of 0.

## Identity and login

Two steps open a session, in this order:

1. `HELLO <station>`: the station is 1–16 characters (`ENG_STALEN`), with no control byte and
   no space first or last (else 102 `FIELD=STATION`), and is written to `AUDIT.STATION`. A
   transport that names the caller itself keeps that name as the station instead (NetBIOS: the
   caller's name without its blanks at either end; the mailbox: the request file's base name),
   unless it is empty or holds a control byte; HELLO is still required and its argument still
   checked.
2. `LOGIN <username> <password>`: checked against `USERS.DBF` (modelled on dBASE III Plus
   PROTECT: a user name of 1–8 characters `A-Z 0-9 _` (`ENG_USERLEN`), upper-cased on input,
   and an access level 1–3). A password is 4–16 characters (`ENG_PWMIN`–`ENG_PWMAX`), bytes
   21h–7Eh, case-sensitive; the same rule holds for the new password of `PASSWD`, `USER ADD`
   and `USER RESET`, and that of `PASSWD` must also differ from the old one. Response
   `+OK LEVEL=n NAME="Full Name"`, or `+OK LEVEL=n MUSTCHG=1` when a password change is
   required (only `HELLO`, `PING`, `VER`, `BYE`, `LOGIN` and `PASSWD` are then accepted;
   others give 404). An argument error (100 or 102) does not count; any other failed login
   (401, 403, or a file error) logs the session out and counts, and the third on a session
   (`PROTO_MAXFAIL`) is answered and then closes the session. Only a successful login resets
   the count. Successful and failed logins are audited. A wrong old password in `PASSWD` is
   401 but not a failed login. While the server's throttle refuses a user after repeated
   failures (`DOC/SERVER.md`, Failed logins), every password `LOGIN` naming that user gets
   that same 401, the right password included, and counts as a failed login.
3. Login tickets, on a session of the mailbox: a ticket is `PROTO_TKTLEN` upper-case hex
   digits, held for one user on one station in the server's memory only, and ends when it has
   been idle for `IDLE=`, `TKT_LIFE` ticks after it was made, when its user's password
   changes or is reset, at `BYE TICKET=END` and when the server restarts (`DOC/SERVER.md`,
   Login tickets).
   - `LOGIN user pw TICKET=NEW` is a password login that is also given a ticket, the last pair
     of its answer: `+OK LEVEL=n NAME="..." TICKET=<t>` or `+OK LEVEL=n MUSTCHG=1 TICKET=<t>`.
     A failure counts as any failed login's.
   - `LOGIN user TICKET=<t>` logs in with the ticket and answers as a password login does,
     with no new ticket. LEVEL, STATUS and MUSTCHG are read anew, and the station's read-only
     cap applies. Every failure is answered, audited, and closes the session at once: 401 for
     a ticket the server does not hold for this user and station (unknown, expired, older than
     a password change), or an unknown or deleted user; 403 for a disabled user; 300–304 for
     a file error. A 401 or 403 also drops the ticket shown, even one held for another user
     or station, whose holder must then log in with the password; a failure that shows no
     held ticket drops none.
   - `PASSWD old new`, once it succeeds, drops every ticket of the user; a session that holds
     one, from either LOGIN, gets a new one, `+OK TICKET=<t2>`. Otherwise the answer is `+OK`.
   - `BYE TICKET=END` drops the session's ticket, answers `+OK` and ends the session.
   - Argument errors, which do not count: a TICKET= other than NEW and a ticket's digits (on
     BYE, other than END) is 102 `FIELD=TICKET` (`bad ticket`); `LOGIN user pw TICKET=<t>` is
     100; `LOGIN user TICKET=NEW` is 102 `FIELD=PASSWORD`. On a NetBIOS or serial session, and
     in local mode, TICKET= is a key LOGIN and BYE do not take: 102 `FIELD=TICKET` (`unexpected
     key`).

`HELLO`, `PING`, `VER` and `BYE` are accepted before `LOGIN`; a second `HELLO` logs the user out
and sets the new station, unless the transport fixes it. An unknown command is 101 in every
state. The level that `LOGIN` and `WHOAMI` show, and every check uses, is fixed at login: the
user's level, capped at 1 on a station the host names read-only (none in local mode); changes to
the account take effect at the next login. `LOGIN` needs only `HELLO`; every other command needs
a logged-in user (else 400) whose level is at least the one the command-level table of
`DOC/ENGINE.md` ("Protocol layer") gives it (else 203). The user name is written to
`AUDIT.USERNAME` on every change.

The server ends a session after sending the response to `BYE` (`+OK`), the response to the
`PROTO_MAXFAIL`-th failed login or to a failed login with a ticket and, while it shuts down, the
`-ERR 501` to any request; it also ends a session idle for its `IDLE=` time (`DOC/SERVER.md`),
which gets no response. The client's remote links (NET, serial, TCP and MBOX) judge the end the
same way and send nothing after such a response.

## Error codes

| Code | Meaning |
|---|---|
| 100 | syntax error |
| 101 | unknown command |
| 102 | bad or missing argument (`FIELD=` names it) |
| 103 | line too long |
| 104 | command needs the server: ADMIN CLIENTS or ADMIN SHUTDOWN in local mode |
| 200 | not found (a record, a file, a directory, or a file of a backup set) |
| 201 | duplicate |
| 202 | constraint violated: the change would break a rule over the data, such as an assembly or a location containing itself, nested too deeply or with too many parts, an item with parts, installed, on loan, reserved or in use, equipment reserved for or in use with an item, a host that is the item or one of its parts or would go inside the item it serves, not enough on hand, a row already returned or released, an unknown category or location type, an unknown location, a location that holds items or locations, a category or location type in use, a full code table, the last active administrator, a memo too long |
| 203 | permission denied (user level too low or read-only station) |
| 204 | bad cursor |
| 300 | database I/O error, or data damaged by another program, such as a location tree with a loop; also every command but HELLO, BYE, PING and VER once an ADMIN command has failed to open the data files again |
| 301 | index damaged or not as the schema (delete the `.NDX` so that the next open rebuilds it, or rebuild it with `DBPACK table /R index`) |
| 302 | disk full |
| 303 | file handles exhausted (raise `FILES=` to `ENG_FILES` or more); the engine's open holds every handle it will need at once, so too small a `FILES=` stops a program at its start |
| 304 | data files in use by another program |
| 400 | HELLO or LOGIN required |
| 401 | bad user name or password, or a login ticket the server does not hold for this user and station |
| 402 | busy: IMPORT, ADMIN PACK, REINDEX, BACKUP or RESTORE, which need the system to themselves, while other sessions are open; checked before their arguments |
| 403 | account disabled |
| 404 | password change required (`PASSWD` first) |
| 500 | internal error, out of memory included |
| 501 | server shutting down |

## Commands

| Group | Commands |
|---|---|
| Session | `HELLO sta` · `LOGIN user pw [TICKET=NEW]` · `LOGIN user TICKET=t` · `PASSWD old new` · `WHOAMI` (K USER/LEVEL/STATION) · `BYE [TICKET=END]` · `PING` · `VER` (`+OK RETROINV <version>`) · `STATUS` (K lines: the record count of every table but SEQ, deleted records included, then the host's lines, none in local mode: the server's `K UPTIME=<seconds>` and `K SESSIONS=<open sessions, the requester's included>`; `K TRUNCATED=1` if cut) |
| Users | `USER LIST [PAGE=][AFTER=]` (name order) · `USER GET name` · `USER ADD name LEVEL= PASSWORD= [FULLNAME=][GROUP=]` (sets MUSTCHG) · `USER SET name [LEVEL=][FULLNAME=][GROUP=][STATUS=A\|D]` · `USER RESET name PASSWORD=` (sets MUSTCHG) · `USER DEL name` (refused for the last active administrator). ADD and SET take `GROUPNAME=`, the name LIST and GET give the field, for `GROUP=`; the two together are 102 naming the second |
| Items | `ITEM GET id` · `ITEM ADD k=v...` · `ITEM SET id k=v...` · `ITEM DEL id` · `ITEM UNDEL id` · `ITEM LIST [CAT=][LOC=][STATUS=][TAG=][WORKS=][AVAIL=][ORDER=ID\|LOC\|MFR\|SERIAL][ALL=1][FIELDS=][PAGE=][AFTER=]` · `ITEM FIND "text" [CAT=][LOC=][STATUS=][TAG=][WORKS=][AVAIL=][ALL=1][FIELDS=][PAGE=][AFTER=]` (text of 1–30 characters, `ENG_MAXFIND`, else 102 `FIELD=TEXT`; a case-insensitive substring of MFR, MODEL, DESCR, TAGS, SERIALNO or ITEMTYPE, in record order, or with `ALL=1` in the tree below `LOC=`) · `ITEM SERIAL "sn" [FIELDS=][PAGE=][AFTER=]` (SERIALNO equal to sn, case ignored; an sn longer than SERIALNO is 102 `FIELD=SERIAL`) · `ITEM NOTES id [ADD=\|NEW=\|PART=\|END="text"]` · `ITEM COPY id` (a new top-level item copied from id) · `ITEM SPLIT id QTY=n` · `ITEM TAGS [AFTER=]` (the tags in use) |
| Categories | `CAT LIST` (unpaged: every field of each live category, by SORTORD then code) · `CAT ADD code CAT_NAME= [SORTORD=]` · `CAT SET code k=v...` · `CAT DEL code` |
| Location types | `LTYPE LIST` (unpaged, as CAT LIST) · `LTYPE ADD code LT_NAME= [SORTORD=]` · `LTYPE SET code k=v...` · `LTYPE DEL code` |
| Locations | `LOC GET loc` (the record, `DEPTH` and `PATH`) · `LOC ADD LOC_NAME= LTYPE= [PARENT_ID=][LOC_CODE=][CAPACITY=][NOTES=]` · `LOC SET loc k=v...` · `LOC DEL loc` (refused while it holds items or locations) · `LOC LIST [UNDER=loc][PAGE=][AFTER=]` (the tree, or the part below loc, in tree order) · `LOC CONTENTS loc [ALL=1][TOP=1][FIELDS=][PAGE=][AFTER=]` (the items at loc, with `ALL=1` also those below it; installed parts included unless `TOP=1`; FIELDS= of ITEMS) · `LOC NOTES loc [ADD=\|NEW=\|PART=\|END="text"]` |
| Assemblies | `ASSY INSTALL child parent` · `ASSY REMOVE child LOC=` · `ASSY MOVE id LOC=` · `ASSY BOM id [FIELDS=][PAGE=][AFTER=]` · `ASSY PATH id` · `ASSY CHILDREN id [FIELDS=][PAGE=][AFTER=]` · `ASSY LIST [TOP=1][FIELDS=][PAGE=][AFTER=]` |
| Maintenance | `MAINT ADD item MTYPE= SUMMARY= [MDATE=][COST=][DONE_BY=][NOTES=]` · `MAINT GET id` · `MAINT SET id k=v...` · `MAINT DEL id` · `MAINT NOTES id [ADD=\|NEW=\|PART=\|END="text"]` · `MAINT LIST item [FIELDS=][PAGE=][AFTER=]` · `MAINT RECENT [FIELDS=][PAGE=][AFTER=]` |
| Loans and holds | `LOAN OUT item HOLDER= DUE= [QTY=][CONTACT=][DATE=][NOTES=]` (also `LOAN ADD`) · `LOAN RESERVE item [HOLDER=][HOST=][DUE=][QTY=][CONTACT=][DATE=][NOTES=]` · `LOAN USE item [HOLDER=][HOST=][QTY=][CONTACT=][DATE=][NOTES=]` · `LOAN RETURN id [DATE=]` (ends a row of any kind) · `LOAN GET id` · `LOAN SET id [HOLDER=][HOST=][CONTACT=][DUE=][NOTES=]` · `LOAN LIST [ITEM=][HOST=][KIND=L\|R\|U][HOLDER=][OPEN=1][PAGE=][AFTER=]` · `LOAN OVERDUE [ASOF=][PAGE=][AFTER=]` |
| Reports | `REPORT ITEMS [CAT=][LOC=][STATUS=][TAG=][WORKS=][AVAIL=][ORDER=ID\|LOC\|MFR\|SERIAL][ALL=1][PAGE=][AFTER=]` · `REPORT LOANS [OVERDUE=1][ASOF=][PAGE=][AFTER=]` |
| Export/import | `EXPORT ITEMS\|LOCS\|CATS\|LTYPES\|MAINT\|LOANS\|AUDIT FILE=path [HEADER=1][REPLACE=1][AFTER=]` · `IMPORT ITEMS\|LOCS FILE=path [HEADER=1]` |
| Administration | `AUDIT LIST [TBL=][REC=][STATION=][USER=][FIELDS=][PAGE=][AFTER=]` · `ADMIN CLIENTS` · `ADMIN FLUSH` · `ADMIN SHUTDOWN` · `ADMIN PACK` · `ADMIN REINDEX` · `ADMIN BACKUP DIR=` · `ADMIN RESTORE DIR=` |

A request that changes data answers `+OK` unless its section says otherwise; ITEM ADD, ITEM
COPY, ITEM SPLIT, LOC ADD, MAINT ADD, LOAN OUT, LOAN RESERVE and LOAN USE answer `+OK ID=n` with
the new record's ID. The `KEY=value` pairs of ADD and SET name fields of the table, whose names,
types and widths are in `DOC/ENGINE.md`, each table under its file name, and whose value rules
are in its "Records and fields"; the engine refuses any field it may not set. The `ADD=` of
ITEM, LOC and MAINT NOTES appends a line to the memo, and `NOTES=` on ITEM and LOC ADD and SET,
and `NOTES=` or `DETAIL=` on MAINT ADD and SET, replaces it; an LF (`\n`) in them not after a CR
is stored as CR LF. (`NOTES=` of the LOAN commands is the C field LOANS.NOTES.) A memo too long
for one request is saved whole in parts (below, Memos saved in parts). A group word that is
looked up with its next word (USER ITEM CAT LTYPE LOC ASSY MAINT LOAN REPORT AUDIT ADMIN), alone
or with an unknown second word, is 101: `REPORT`, `REPORT BINS` and `ADMIN BOGUS` included.

Record IDs, positional or in a pair, are decimal digits with no sign, leading zeros allowed, of
value 1..99999999 (`ENG_MAXID`); a pair that is absent or empty filters nothing. A bad one, or a
missing positional one, is 102 naming the argument: `CHILD` and `PARENT` for ASSY INSTALL,
`CHILD` for REMOVE, `ITEM` for the item of MAINT ADD, MAINT LIST, LOAN OUT, RESERVE and USE,
`ID` for the other positional IDs. In the ASSY, MAINT and LOAN commands a record that is not
there is 200 naming the same.

A location is named by a token `loc`: one of digits is its LOC_ID, anything else its code, in
any case (a code starts with a letter, so the two never meet); responses always give the
LOC_ID. Where a location may be none, 0 stands for it: the top level for PARENT_ID and `UNDER=`,
an unknown location for an item's LOC_ID, LOC CONTENTS, the `LOC=` filter and ASSY's `LOC=`. A
missing positional one is 102 `FIELD=LOC`; one that names no live location is 200 `no such
location` as the LOC commands' argument and `UNDER=` and 200 `FIELD=LOC` as the filter, and 202
`unknown location` naming the key for PARENT_ID, LOC_ID and ASSY's `LOC=`.

A flag pair (`TOP=`, `OPEN=`, `OVERDUE=`, `ALL=`, `HEADER=`, `REPLACE=`) takes `1` or `0`; one
that is absent or empty is `0`, any other value 102 naming it. `AVAIL=` takes `1` or `0` too,
but there `0` is a filter of its own and only an absent or empty one filters nothing.

### Memos saved in parts

A memo of up to `ENG_MAXMEMO` bytes does not fit one request of `PROTO_MAXLINE`, so a client
saves a longer one in parts, which the server stages for the session and writes only when the
last part has come:

- `<table> NOTES id NEW="text"` starts the session's stage for the record with text, in place
  of any stage the session had in that table;
- `<table> NOTES id PART="text"` appends text to it as a new line, as `ADD=` does to a memo;
- `<table> NOTES id END="text"` appends its line too, then makes the stage the record's memo
  in one change, with one audit row as `ADD=` writes, and ends the stage.

Until `END=` the record, its memo and the audit are as they were, so a save that stops part-way
(a refusal, a full disk, a lost connection, a session that ends) leaves the memo the record had,
and two sessions saving one memo at once each write a whole memo, the last `END=` winning. A
stage belongs to the session's slot, station and user; on a mailbox, whose every request file is
a session of its own, to the station and user alone, so a save goes on in that station's later
sessions. `PART=` and `END=` without such a stage of that record are 204 `FIELD=NOTES` (`no note
being saved`). A `PART=` refused (204, 102 for its text, 202 `FIELD=NOTES` past `ENG_MAXMEMO`
bytes in all) leaves the stage as it was, so it may be sent again; one that cannot be written
whole (302) drops the stage, and an `END=` that read its stage ends it whatever its outcome; a
client then starts again with `NEW=`. The stage is a file in the data directory named by the
session's slot, or on a mailbox (and in local mode) by a digit hashed from the station
(`DOC/ENGINE.md`, Records and fields). Sessions whose digits agree share the file: the later
`NEW=` takes it, and the other's `PART=` or `END=` is then 204, never a mixed memo. The pairs
`ADD=`, `NEW=`, `PART=` and `END=` exclude each other (102 naming the second); any of them needs
level 2 (`DOC/ENGINE.md`, Protocol layer).

```
ITEM NOTES 12 NEW="first line\nsecond line"
+OK
ITEM NOTES 12 PART="third line"
+OK
ITEM NOTES 12 END="fourth line"
+OK
```

`PROTO_VERSION`, which `VER` gives, is the same with or without these pairs, so a client learns
that a server lacks them only from its answer to `NEW=`: 102 `FIELD=NEW`, before anything is
written, so a long memo is refused whole there; a memo sent as a `SET` and `ADD=`s changes with
each request.

### Item fields, filters, ITEM TAGS and ITEM SPLIT

The engine's rules for items are in `DOC/ENGINE.md`; on the wire:

- `COND=` is the cosmetic grade, `NEW EXC GD FR PR` or empty (not graded; any other is 102 `bad
  condition code FIELD=COND`). `WORKS=` is `Y` (works), `I` (intermittently), `N` (does not
  work) or `?` (not tested, a new item's), any case, stored upper case (else 102 `works must be
  Y, N, I or ? FIELD=WORKS`, or `value too long` for more than one character). `TAGS=` is a list
  of tags, commas or spaces in any run between them, each 1 to 12 characters (`ENG_ITAGLEN`) of
  `A-Z a-z 0-9 - _ . / +` (else 102 `each tag 1-12 of A-Z a-z 0-9 - _ . / + FIELD=TAGS`); it is
  stored in the order given, joined by single commas, a repeat (case ignored) left out and the
  first spelling kept, so `Retro, ISA  retro,,pci` comes back from GET and the lists as
  `Retro,ISA,pci` and travels bare. In IMPORT ITEMS an empty WORKS gives `?`, and a bad TAGS
  refuses its line with 102 `FIELD=TAGS`.
- `ITEM LIST`, `ITEM FIND` and `REPORT ITEMS` take the filters `CAT=`, `LOC=`, `STATUS=`, `TAG=`,
  `WORKS=` and `AVAIL=`, each set one narrowing the others and FIND's text; an empty one filters
  nothing. A `CAT=` or `STATUS=` longer than its field is 102 naming the key, and any other
  unknown value matches nothing. `AVAIL=1` keeps the available items, the rows whose `AVAIL`
  column is `Y`; `AVAIL=0` keeps every other live item; a `STATUS=` value beside either is 102
  `FIELD=STATUS`. ITEM FIND stays in record order (cursor `ITEMS::recno`) and takes no ORDER=
  (102 `unexpected key`).
- `ORDER=` of ITEM LIST and REPORT ITEMS, any case, sorts the rows: `ID` by ITEM_ID, `LOC` by
  LOC_ID and then ITEM_ID (unknown location first), `MFR` by MFR and then MODEL, `SERIAL` by
  SERIALNO (blank serials first), these two with case ignored and equal keys in record order; any
  other value is 102 `FIELD=ORDER`. Without it the order is `LOC` when `LOC=` is set and not
  empty, else `ID`.
- `TAG=retro,isa` keeps the items carrying every tag listed, whole and case ignored (`retro` is
  neither `retro2` nor `metro`): its text is at most 25 characters (`ENG_TAGFILT`, two tags and
  their comma; else 102 `filter value too long FIELD=TAG`), and a word that is no tag, or a value
  of separators only, is 102 `each tag 1-12 of A-Z a-z 0-9 - _ . / + FIELD=TAG`. FIND's text
  matches a part of a tag (`net` finds `network`), `TAG=` only a whole one. `WORKS=` takes 1 to 4
  (`ENG_NWORKS`) of the letters `Y I N ?`, any case, each once, and keeps the items whose WORKS
  is one of them: `WORKS=Y?I` leaves out only those known not to work. Another letter, a repeat
  or a fifth letter is 102 `works must be Y, N, I or ? FIELD=WORKS`. Neither has an index, so a
  selective one can give short or empty pages with a `C` line, as `STATUS=D` can.

  ```
  ITEM FIND network CAT=CARD TAG=retro WORKS=Y?I
  +DATA
  F ITEM_ID CATEGORY MFR MODEL WORKS QTY AVAIL LOC_ID LOC_NAME STATUS
  R 17 CARD "Western Digital" WD8003E ? 3 Y 3 "Bin 1" A
  R 18 CARD Novell NE2000 Y 2 Y 3 "Bin 1" A
  R 38 CARD 3Com 3C905C-TX Y 2 Y 3 "Bin 1" A
  .
  ```

- `ITEM TAGS [AFTER=]` gives the tags in use, for a pick list: `+DATA`, `F TAG`, one `R tag` per
  tag in A-Z order (case ignored), a `C` line while items remain, and `.`. Each request reads at
  most 200 records of ITEMS in record order (`ENG_MAXREADS`, cursor `ITEMS::recno`), live items
  of any STATUS, and lists each of their tags once, case ignored, in the first spelling it met;
  a page stops early as its tags fill the engine's `ENG_TAGSET` bytes (`DOC/ENGINE.md`, Items,
  locations and code tables), and may hold no `R` line. A client follows the cursor to the end and
  merges the pages (a tag met on two pages is on both). Words another program left in TAGS that
  are no tags are left out. A key is 102 `unexpected key`, a word 100 `too many arguments`, a
  bad cursor 204.

  ```
  ITEM TAGS
  +DATA
  F TAG
  R modern
  R network
  R Retro
  C ITEMS::C8
  .
  ```

- `LOC=A1` keeps the items at that one location, not those below it, and `LOC=0` those of
  unknown location. With `ALL=1`, ITEM LIST, ITEM FIND and REPORT ITEMS give the items at `LOC=`
  and at every location below it in tree order, as `LOC CONTENTS loc ALL=1` does (`LOC=0`: the
  items of unknown location, then the whole tree), the other filters, FIND's text included,
  applied inside the walk and its cursor `ITEMS2:hexkey:recno`; `LOC=` is needed (102 `missing
  argument FIELD=LOC`) and `ORDER=` refused (102 `FIELD=ORDER`), the tree being the order.
- `ITEM SPLIT 4 QTY=2` → `+OK ID=n`, the new item that takes two of item 4's units
  (`DOC/ENGINE.md`, Items, locations and code tables). `QTY=` missing, empty or 0 is `-ERR 102
  bad or missing argument FIELD=QTY`, and a value that is no N(5) quantity 102 `FIELD=QTY` with
  the text ITEM SET gives (`not a number`, `negative value`, `bad value`); any other key is 102
  `unexpected key`; a bad ID is 102 `FIELD=ID`, an unknown or deleted item 200. A QTY that would
  leave item 4 fewer than one unit, or fewer than it has lent, reserved or in use, is `-ERR 202
  not enough on hand FIELD=QTY`.

### Locations and location types

The tree's rules are in `DOC/ENGINE.md` (Items, locations and code tables); on the wire:

- `LOC ADD LOC_NAME="Bin 1" LTYPE=BIN PARENT_ID=A LOC_CODE=A1 CAPACITY=40` → `+OK ID=n`, the new
  LOC_ID. `LOC SET loc k=v...` takes any field but LOC_ID. `LOC_CODE=` is empty or 1 to
  `ENG_LOCLEN` characters, a letter then letters, digits or `-`, any case, stored upper case.
  The refusals name the field: 102 `value required` (LOC_NAME, LTYPE), 102 `bad location code`,
  201 `name already used at this level` (LOC_NAME, among the parent's locations), 201 `location
  code already used` (LOC_CODE), 202 `unknown location type`, 202 `unknown location`, `cannot be
  put inside itself or its contents` and `locations nested more than 8 levels deep` (PARENT_ID;
  the number is `ENG_MAXLOCDEPTH`).
- `LOC GET loc` answers the record's `K` lines, then its depth (1 at the top) and its path, cut
  at the top to `ENG_LOCPATHLEN` characters when longer (`... > Bin 1`). `LOC GET 0` is 200.

  ```
  LOC GET A1
  +DATA
  K LOC_ID=3
  K PARENT_ID=2
  K LOC_NAME="Bin 1"
  K LOC_CODE=A1
  K LTYPE=BIN
  K CAPACITY=40
  K DEPTH=3
  K PATH="Office > Shelf A > Bin 1"
  .
  ```

- `LOC DEL loc` → `+OK`; `-ERR 202 location holds items: move or delete them` while an item is
  there, `-ERR 202 location holds locations: move or delete them` while a location is in it.
- `LOC LIST` has fixed columns, DEPTH first, and takes no FIELDS=: the locations below `UNDER=`
  (all of them without it, or with 0) in tree order, each followed by those in it, the locations
  of one parent by name. Its cursor is `LOCS2:hexkey:recno`.

  ```
  LOC LIST UNDER=A
  +DATA
  F DEPTH LOC_ID PARENT_ID LOC_NAME LOC_CODE LTYPE CAPACITY
  R 3 3 2 "Bin 1" A1 BIN 40
  R 3 4 2 "Bin 2" A2 BIN 20
  R 3 5 2 "Bin 3" A3 BIN 50
  .
  ```

- `LOC CONTENTS loc` lists the items at loc by ITEM_ID, and with `ALL=1` then those at each
  location below it, in the order of LOC LIST; `LOC CONTENTS 0` lists the items of unknown
  location, and with `ALL=1` then the whole tree. The rows are those of `ITEM LIST`, LOC_NAME
  telling where each item sits; the cursor is `ITEMS2:hexkey:recno`.
- A LOC LIST or LOC CONTENTS cursor from another `UNDER=` or location, or of a location moved
  out of it since, is 204. A location deleted between two pages ends its part of the walk,
  repeating or hiding no other, but a move or rename of a location above the cursor's may skip
  or repeat rows: a client that changes the tree lists it again from the start.
- `LTYPE` keeps the location types as `CAT` keeps the categories: `LTYPE LIST` (`F LT_CODE
  LT_NAME SORTORD`), `LTYPE ADD code LT_NAME= [SORTORD=]`, `LTYPE SET code [LT_NAME=][SORTORD=]`
  and `LTYPE DEL code`, which is 202 `locations still use this location type` while a live
  location has it. A code, of a category or a type, is 1–`ENG_CATLEN` or 1–`ENG_LTYPELEN`
  letters and digits, any case, stored upper case; any other is 102 `bad category code
  FIELD=CAT_CODE` or `bad location type code FIELD=LT_CODE`.

### Assemblies, maintenance, loans and holds

The rules behind these commands are in `DOC/ENGINE.md`; the wire forms are:

- `ASSY INSTALL 7 1` → `+OK`; `-ERR 202 cannot be put inside itself or its contents
  FIELD=PARENT`, `-ERR 202 parts nested more than 16 levels deep FIELD=PARENT` (the number is
  `ENG_MAXDEPTH`), `-ERR 202 too many parts FIELD=PARENT`, `-ERR 202 item is on loan FIELD=CHILD`
  (or `item is reserved`, `item is in use`), `-ERR 202 cannot be used with itself or its own parts
  FIELD=PARENT` when an open reservation or assignment of the item the child joins names the
  child or one of its parts as host, `-ERR 202 item is reserved FIELD=PARENT` (or `item is in
  use`) when more than `ENG_MAXREADS` of that item's own rows would have to be read to tell.
  `ASSY REMOVE 7 LOC=B2` → `+OK`; `-ERR 202 not part of an assembly FIELD=CHILD`; `LOC=`
  missing or blank is 102 `FIELD=LOC`, an unknown location 202 `FIELD=LOC`. `ASSY MOVE 1 LOC=B2`
  moves the whole assembly → `+OK`; `-ERR 202 installed in an assembly FIELD=ID`.
- `ASSY BOM 1` → `+DATA`, `F LEVEL ITEM_ID CATEGORY ...` (LEVEL, then the FIELDS= list of ITEMS
  fields, or the item list's default), `R 1 7 MOBO ...` per part, depth first, LEVEL 1 for a
  part of the item named; a `C` line when more remain. `ASSY PATH 9` → unpaged, fixed columns
  `F LEVEL ITEM_ID CATEGORY MFR MODEL LOC_ID LOC_NAME` (no FIELDS=), the ancestors, the
  top-level item first with LEVEL 1; no `R` lines for a top-level item. Unlike the other unpaged
  answers it checks its room: a transport whose output falls below `PROTO_ROOMMIN` before the
  last row gets `K ERROR=500` instead of a shortened path. `ASSY CHILDREN 1` (the direct parts)
  and `ASSY LIST` (the items with at least one live part; `TOP=1` only the top-level ones)
  answer like ITEM LIST, in ID order. Their cursors, and BOM's, are `ITEMS3:hexkey:recno`.
- `MAINT ADD 12 MTYPE=RP SUMMARY="Recapped the PSU" COST=12.50` → `+OK ID=n`; MTYPE is one of `RP
  CL RC TS MD IN` (102 `FIELD=MTYPE`), SUMMARY is required (102 `FIELD=SUMMARY`). `NOTES=` (or
  `DETAIL=`) sets the DETAIL memo, which `MAINT NOTES id` sends; `MAINT GET id` → `K` lines
  without the memo. `MAINT LIST item` is oldest first (cursor `MAINT1:...`), `MAINT RECENT`
  newest recorded first, whatever their MDATE (cursor `MAINT::recno`); both list `MAINT_ID
  ITEM_ID MDATE MTYPE SUMMARY COST` (file order) by default.
- `LOAN OUT 12 HOLDER="Jane Smith" DUE=20261022` → `+OK ID=n`, a loan (KIND L). `LOAN RESERVE 12
  HOLDER="Club show" DUE=20261031` (KIND R) and `LOAN USE 9 HOST=4` (KIND U) answer the same; the
  fields each kind takes are in `DOC/ENGINE.md` (Loans, reservations and in-use equipment). `DUE`
  is DUE_DATE, `DATE` OUT_DATE and `HOST` HOST_ID; the full field names are accepted too, and
  errors name the field: `-ERR 102 value required FIELD=HOLDER` or `FIELD=DUE_DATE` (a loan's),
  `-ERR 102 name who or which item it is for FIELD=HOLDER` (a reservation or assignment with
  neither), `-ERR 102 ... FIELD=DUE_DATE` (invalid, or before OUT_DATE), `FIELD=QTY` (below 1);
  `-ERR 200 no such item FIELD=ITEM` for an unknown or deleted item, `-ERR 202 not enough on hand
  FIELD=QTY`, `-ERR 202 installed in an assembly FIELD=ITEM`, `-ERR 202 item was disposed of
  FIELD=ITEM`; for the host `-ERR 200 no such item FIELD=HOST_ID`, `-ERR 202 item was disposed of
  FIELD=HOST_ID`, `-ERR 202 cannot be used with itself or its own parts FIELD=HOST_ID`. A key
  that names a LOANS field the command does not set (`DUE` of LOAN USE, `HOST` of LOAN OUT,
  `RET_DATE`) is `-ERR 102 field not settable` naming the key, and any other key `-ERR 102
  unexpected key` (`LOAN_ID`, `ITEM_ID`, `KIND`).
- `LOAN RETURN 5 [DATE=20261020]` (or `RET_DATE=`; both together are 102 `FIELD=RET_DATE`) ends
  a row of any kind, a return or a release → `+OK`; `-ERR 202 already returned or released
  FIELD=ID`, `-ERR 102 end date before out date FIELD=RET_DATE` for a date before OUT_DATE.
  `LOAN SET` takes the keys of the fields it may change (`DOC/ENGINE.md`, Loans, reservations
  and in-use equipment): HOLDER, CONTACT, NOTES, DUE and HOST. A key naming another LOANS field,
  or `DATE`, is `-ERR 102 field not settable` naming it, and any other key `-ERR 102 unexpected
  key`.
- While units are held the item rules refuse with the reason: `ITEM SET id STATUS=D` is 202
  `FIELD=STATUS` (`item is on loan`, `item is reserved`, `item is in use`, `installed in an
  assembly`, or for a host `equipment is reserved for or in use with it`); `ITEM DEL id` is 202
  with the reason of the first counter above 0, naming it (`FIELD=ON_LOAN`, `RESERVED`, `IN_USE`
  or `ATTACHED`, in that order); a QTY below the units held is 202 `quantity below units lent,
  reserved or in use FIELD=QTY`.
- `LOAN LIST`, `LOAN OVERDUE` and `REPORT LOANS` have fixed columns and take no FIELDS=:
  `F LATE LOAN_ID ITEM_ID KIND QTY HOLDER HOST_ID CONTACT OUT_DATE DUE_DATE RET_DATE MFR MODEL`
  (REPORT LOANS without RET_DATE), LATE `1` for an open row due before today (before ASOF for
  OVERDUE, where every row is late), else `0`, and MFR and MODEL of the row's item (empty for a
  deleted item). Without `ITEM=` the open rows come first by due date, then the ended ones by
  year of return and due date (cursor `LOANS2:...`); with it, LOAN_ID order (`LOANS1:...`); a
  cursor of the other order is 204. `ITEM=` and `HOST=` take an item ID (empty for none);
  `HOST=` keeps the open rows naming that item as host (200 `FIELD=HOST` for one that is no live
  item), whatever `OPEN=`; `KIND=L`, `R` or `U` (any case) keeps the rows of one kind (else 102
  `FIELD=KIND`); `HOLDER=` keeps the rows whose HOLDER is that whole name, compared without regard
  to case, never a part of it (at most the field's 30 characters, `ENG_FW_LOANS_HOLDER`, else 102
  `filter value too long FIELD=HOLDER`; empty filters nothing); `ASOF=` takes a date `YYYYMMDD`
  (102 `FIELD=ASOF`). A row is listed only when it passes every filter given. No index holds
  `HOST=`, `KIND=` or `HOLDER=`, so they are tested on each row the scan reads, and a page
  filtered by them may hold fewer rows than `PAGE=`, or none, with a `C` line. LOAN OVERDUE lists
  the loans and reservations due before ASOF; an assignment has no due date and is never overdue.

### Reports, export and import

Reports are fixed-column paged lists: the server sends rows, and the client lays out pages,
group headings, totals and labels for its own printer, so the server keeps no state between
pages and every request stays bounded. Labels need no command of their own: item labels come
from `REPORT ITEMS` rows or `ITEM GET`, bin labels from `LOC LIST` rows or `LOC GET`.

- `REPORT ITEMS` takes the keys `CAT LOC STATUS TAG WORKS AVAIL ORDER ALL PAGE AFTER` (any other
  is 102 `unexpected key`); its filters, orders and cursors are those of `ITEM LIST`, and the
  default order is LOC when `LOC=` is given, else ID; `LOC=x ALL=1` gives the report of a
  location and everything below it in tree order, one request per page. The columns are fixed
  (no FIELDS=):

  ```
  REPORT ITEMS LOC=A1 PAGE=25
  +DATA
  F ITEM_ID CATEGORY MFR MODEL SERIALNO COND WORKS QTY LOC_ID LOC_NAME STATUS ACQ_DATE ACQ_PRICE CUR_VALUE
  R 16 CARD Hercules "Graphics Card" "" EXC Y 1 3 "Bin 1" A 20110220 35.00 90.00
  C ITEMS2:...
  .
  ```

- `REPORT LOANS` lists the open rows of every kind by due date, those without one first (cursor
  `LOANS2:...`; a cursor of `LOAN LIST` without `ITEM=` is accepted while it is within the open
  rows; one on an ended row, or of `LOANS1`, is 204). `OVERDUE=1` keeps those due before ASOF;
  `ASOF=YYYYMMDD` (default today) is also the reference of the LATE column, so a printed page
  and its late marks agree. A bad date is 102 `FIELD=ASOF` in both forms (before any cursor
  error). MFR and MODEL are empty for a deleted item.

  ```
  REPORT LOANS ASOF=20261009
  +DATA
  F LATE LOAN_ID ITEM_ID KIND QTY HOLDER HOST_ID CONTACT OUT_DATE DUE_DATE MFR MODEL
  R 1 5 12 L 1 "Jane Smith" 0 555-0101 20261001 20261008 IBM 5150
  .
  ```

EXPORT and IMPORT write and read CSV files in the format of `DOC/ENGINE.md` ("Text files (CSV)")
on the machine running the engine; a path that does not start at a root is taken from that
program's current drive and directory, as DOS takes it. The table is the only positional
argument (missing, or one the command does not take, is 102 `FIELD=TABLE`; a second one is 100
`too many arguments`), and a key the command does not take is 102 naming it. A `FILE=` path
that breaks the rules of `DOC/ENGINE.md` ("CSV export and import", Paths), among them one too
long, one naming a DOS device (`PRN`, `NUL.CSV`) or one with an extension DOS would cut to that
of a data or scratch file (`.DBFX`), is 102 `FIELD=FILE` (`bad file name`).
`HEADER=1` puts the field names on the first line.

- `EXPORT table FILE=path` writes the live records of ITEMS, LOCS, CATS, LTYPES, MAINT, LOANS or
  AUDIT (never USERS or SEQ). Each request examines at most 50 records (`ENG_EXPROWS`) and
  answers `K ROWS=n` with a `C` line while more remain; the client repeats the request, with the
  same FILE and flags, adding `AFTER=`. The file is written under a scratch name that holds the
  session's slot (`DOC/ENGINE.md`, CSV export and import) and renamed to `path` by the last
  request, so `path` never holds a partial export and the exports of two sessions never share a
  scratch file; one session's exports to paths that differ only in their extension do share one,
  so a client runs one export at a time. A first request naming an existing file is 201
  `FIELD=FILE` (`file already exists`) unless `REPLACE=1`; `AFTER=` without the session's
  scratch file, as from another session, is 204 (`no export in progress`); two exports to one
  path both run, and the last request of the one that ends second is 201 `FIELD=FILE` unless it
  has `REPLACE=1`. A directory that does not exist is 200 `FIELD=FILE`, other file errors are
  300, 302, 303 and 304 `FIELD=FILE`, and 500 on a memory fault. A continuation that fails may
  be sent again as it was.

  ```
  EXPORT ITEMS FILE=A:\ITEMS.CSV HEADER=1
  +DATA
  K ROWS=50
  C ITEMS::32
  .
  EXPORT ITEMS FILE=A:\ITEMS.CSV HEADER=1 AFTER=ITEMS::32
  +DATA
  K ROWS=12
  .
  ```

- `IMPORT ITEMS|LOCS FILE=path [HEADER=1]` adds every line of the file as a new record, as `ITEM
  ADD` or `LOC ADD` would, in one request; a location's PARENT_ID may also name another line of
  the file (`DOC/ENGINE.md`, Text files (CSV)). The whole file is checked first and nothing is
  written unless every line passes; a file that does not exist is 200 `FIELD=FILE`, too little
  disk space 302 `FIELD=FILE` (`not enough disk space`). The answers:

  ```
  IMPORT ITEMS FILE=A:\ITEMS.CSV HEADER=1
  +OK ADDED=62

  IMPORT ITEMS FILE=A:\NEW.CSV
  +DATA
  F LINE FIELD CODE TEXT
  R 3 CATEGORY 202 "unknown category"
  R 7 LINE 102 "bad quoting"
  K ERRORS=12
  .
  ```

  The refused lines (at most 10 of them, `PROTO_IMPERRS`, then the count of all) name the line of
  the file (blank lines and the field-name line counted) and the field, or `LINE` for the line as
  a whole; nothing was added. A failure of 300 or more while the file is checked, after refused
  lines, adds `K ERROR=code` after `K ERRORS=n`. A failure while adding (a disk error) answers
  `+DATA`, `K ADDED=n` (the records already added, which stay) and `K ERROR=code`. A failure
  while no line has been refused and no record added is an ordinary `-ERR` line.

  ```
  IMPORT ITEMS FILE=A:\NEW.CSV
  +DATA
  K ADDED=37
  K ERROR=302
  .
  ```

### Administration

The `ADMIN` commands take no positional argument (100 `too many arguments`) and no pair but
the `DIR=` that BACKUP and RESTORE need (another pair is 102 naming its key). `ADMIN CLIENTS`
and `ADMIN SHUTDOWN` need a server: in local mode they are 104 to a user of their level, before
their arguments are checked. The others work in local mode too.

- `AUDIT LIST` pages the rows of `AUDIT.DBF` as `ITEM LIST` pages items, every AUDIT field by
  default (`F AUD_ID ADATE ATIME STATION USERNAME TBL REC_ID ACTION FLD OLDVAL NEWVAL`).
  Without `TBL=` the rows come newest first (cursor `AUDIT::recno`); with `TBL=` (a table
  name, at most 8 characters, `ENG_TBLNAMELEN`, else 102 `FIELD=TBL`; compared upper-cased) they
  are those of that table, by `REC_ID`, then oldest first (cursor `AUDIT1:hexkey:recno`), and
  `REC=` (an ID, 102 `bad ID FIELD=REC`) keeps one record's; `REC=` without `TBL=` is 102
  `FIELD=TBL`. `STATION=` (at most 16 characters, `ENG_STALEN`, else 102 `FIELD=STATION`)
  keeps one station's rows and `USER=` (at most 8 characters, `ENG_USERLEN`, else 102
  `FIELD=USER`) the rows of one user name (`USERNAME`), what that user changed; each compares the
  whole value without regard to case, and a row is listed only when it passes every filter given.
  `USER=` without `TBL=` reads that user's rows alone through an index, newest first (cursor
  `AUDIT2:hexkey:recno`), whatever the length of the log. `STATION=`, and `USER=` beside `TBL=`,
  are tested on each row the scan reads, so a page may then hold fewer rows than `PAGE=`, or
  none, with a `C` line. An empty value filters nothing; a cursor of another order, or of another
  table, record or user, is 204.

  ```
  AUDIT LIST TBL=ITEMS REC=1 FIELDS=AUD_ID,ACTION,FLD,OLDVAL,NEWVAL
  +DATA
  F AUD_ID ACTION FLD OLDVAL NEWVAL
  R 15 I ITEM_ID "" 1
  R 18 U MODEL 5150 5160
  .
  ```

- `ADMIN PACK`, `ADMIN REINDEX`, `ADMIN BACKUP DIR=dir` and `ADMIN RESTORE DIR=dir` work on the
  data files as a whole, so each takes as long as the data needs. PACK drops the deleted records
  of every table and the memo text no record points at, and rebuilds every index:
  `+OK REMOVED=n`, the deleted records dropped. It renumbers the records, so the audit rows of
  CATS and LTYPES, whose `REC_ID` is a record number, name the numbers from before it. REINDEX
  rebuilds every index from its table: `+OK`. BACKUP copies every table, memo file and index
  into the directory `dir` on the machine running the engine, which must exist: `+OK`. RESTORE
  copies the tables and memo files of a backup in `dir` back and rebuilds every index: `+OK`.
  Each works on the files as `DOC/ENGINE.md` (Administration) describes and writes one `K`
  audit row (`DOC/ENGINE.md`, Audit rows).
- `DIR=` is missing: 102 `FIELD=DIR` (`missing argument`); empty, longer than 64 characters
  (`ENG_MAXDIR`) with the `\` added that it needs when it does not end in `\`, `/` or `:`, or
  holding a space, a control byte, `*` or `?`: 102 `FIELD=DIR` (`bad directory name`); the data
  directory under any spelling, which a probe file tells before anything changes
  (`DOC/ENGINE.md`, Administration): 102 `FIELD=DIR` (`same as the data directory`); a directory
  that does not exist: 200 `FIELD=DIR` (`no such directory`). RESTORE refuses, before it changes
  anything, a backup with a table or memo file missing, or one that holds the commit mark of a
  BACKUP stopped after writing it, which a BACKUP that succeeds there clears: 200 `FIELD=DIR`
  (`backup set incomplete`); a table that has other fields than the schema's: 300 `FIELD=DIR`.
  Any other failure of a table or memo file of the backup, or of the probe file in either
  directory, is its storage error naming `DIR`: 300, 302, 303 or 304 `FIELD=DIR`, or 500
  `FIELD=DIR` on a memory or internal fault; but a data directory gone, so that RESTORE cannot
  make its probe there, is 300 naming no field (`no such directory`), as the open reports it.
- A failure while the files are copied, packed or rebuilt is 300, 302 or 303 with the data
  opened again: PACK leaves each table packed or as it was, RESTORE the data as it was (a
  failure writing its audit row leaves the backup's), BACKUP and REINDEX the data unchanged. If
  a stop or a failure interrupts RESTORE after its commit mark is written, the next open
  finishes it. When the data files cannot be opened again the request gets that error, and every
  later command but HELLO, BYE, PING and VER is 300 until the program opens the data anew, which
  repairs what the command left.
- `ADMIN CLIENTS` lists the server's open sessions in slot order, the requester's included, at
  most `PROTO_MAXPEERS` of them, unpaged. The columns: `SLOT` the server's number for the
  session, from 1; `TYPE` its transport (`NETB` for NetBIOS, `COM1` or `COM2` for a serial
  line, `MBOX` for the mailbox); `STATION` and `USER`, `""` before HELLO and LOGIN; `LEVEL` the
  effective level, 0 before LOGIN; `STATE` 0 before HELLO, 1 after HELLO, 2 logged in with a
  password change pending, 3 logged in; `IDLE` the seconds since its last request was answered
  or output sent (0 for the requester's); `SELF` 1 for the requester's session, else 0.

  ```
  ADMIN CLIENTS
  +DATA
  F SLOT TYPE STATION USER LEVEL STATE IDLE SELF
  R 1 NETB PC1 ADMIN 3 3 0 1
  R 2 NETB LAB2 "" 0 1 75 0
  .
  ```

- `ADMIN FLUSH` writes every file with unwritten changes to the disk: `+OK`, or a file error
  of the 300 range.
- `ADMIN SHUTDOWN` answers `+OK` and starts the server's orderly shutdown, as F3 on its
  status screen does: the server takes no new session, answers the next request of every
  session, the requester's included, with `-ERR 501` and ends that session, and after a few
  seconds (`CONN_SHUTWAIT`, `SRC/SERVER/CONN.H`) ends every session still open
  (`DOC/SERVER.md`, Shutdown).
