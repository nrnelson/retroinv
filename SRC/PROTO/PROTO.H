/*
 * PROTO.H - the line protocol: request tokenizer, response writer,
 * session state and the single entry point proto_dispatch.
 *
 * The server's NetBIOS sessions, those of the TCP bridge included, and
 * the local client feed request lines to proto_dispatch and send the
 * bytes it writes to a caller-supplied sink, so all of them behave the
 * same and enforce the same permissions.
 *
 *   TOKEN.C     line assembly, tokenizer
 *   RESPONSE.C  response writer, error texts
 *   SESSION.C   session state changes
 *   DISPATCH.C  proto_dispatch, the command table, the gate, and the
 *               helpers shared by the handlers (arguments, replies,
 *               paging)
 *   DPSESS.C    HELLO LOGIN PASSWD WHOAMI BYE PING VER STATUS
 *   DPUSER.C    USER commands
 *   DPITEM.C    ITEM commands
 *   DPLOC.C     LOC, CAT and LTYPE commands
 *   DPASSY.C    ASSY commands
 *   DPMAINT.C   MAINT commands
 *   DPLOAN.C    LOAN commands
 *   DPCSV.C     EXPORT IMPORT
 *   DPADMIN.C   AUDIT LIST, ADMIN CLIENTS FLUSH SHUTDOWN PACK REINDEX
 *               BACKUP RESTORE
 *
 * The request in progress (proto_call), the helpers DISPATCH.C shares
 * with the handlers and the handlers of the DP*.C files are declared in
 * DPCMD.H, which only DISPATCH.C and those files include.
 *
 * Every function returning int returns 0 or one of the ENG_E codes of
 * ENGINE.H, which are the protocol's error numbers, unless it says
 * otherwise.
 */
#ifndef PROTO_H
#define PROTO_H

#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"

#define PROTO_VERSION   "RETROINV 1.0"

/* ---------------------------------------------------------------------
 * Limits.
 *
 * A request fits PROTO_MAXLINE with every argument at its longest.  The
 * longest are the paged lists: the command words, then " KEY=" and a
 * value for every key the command takes (of AVAIL= and STATUS=, which
 * the item lists refuse together, the longer), which is a category of
 * ENG_CATLEN, a location of ENG_LOCLEN (a code; a LOC_ID is shorter),
 * an ID of ENG_IDLEN digits, a date of ENG_DATELEN, a flag or a status
 * of one character, the tags of TAG= (ENG_TAGFILT, joined by commas),
 * the letters of WORKS= (ENG_NWORKS), a page size of two digits, a
 * FIELDS= list of ENG_MAXFSPEC characters, or AFTER= with the cursor of
 * the command's index (ENG_IXNAMELEN, ':', its key in hex, ':', eight
 * hex digits; a scan in record order has a table name of ENG_TBLNAMELEN
 * and no key).
 * ORDER= and AFTER= count together, as the order's word and the cursor
 * of its index; the longest request is ITEM LIST with ORDER=MFR, whose
 * index has the longest key, ENG_MAXKEYLEN.
 * A text that may need quotes (ITEM SERIAL's serial number, ITEM FIND's
 * text, AUDIT LIST's station) counts its quotes and every byte escaped;
 * so do EXPORT's FILE= path of ENG_CSVPATH characters and the DIR= of
 * ADMIN BACKUP and RESTORE of ENG_MAXDIR, which never need quotes but
 * which a client may quote with their backslashes doubled, as RQ.C
 * does.  DPCSV.C checks ITEM LIST, ITEM FIND, ITEM SERIAL, ASSY BOM,
 * LOAN LIST, LOAN OVERDUE, REPORT ITEMS, REPORT LOANS, EXPORT, AUDIT
 * LIST and ADMIN RESTORE this way at compile time.
 * An R line holds "R" and, per column, a space, two quotes and every
 * byte of the value escaped.  DPCSV.C checks that the fixed columns of
 * REPORT ITEMS, REPORT LOANS and LOC LIST, and any FIELDS= list
 * (ENG_MAXFLIST fields of ENG_MAXRWIDTH characters in all), stay within
 * PROTO_MAXRESP, DPLOC.C that the PATH line of LOC GET (ENG_LOCPATHLEN
 * characters) does, and DPCSV.C that four of the unpaged lists stay
 * within PROTO_MAXUNPAGED: ASSY PATH ("+DATA", the F line of its fixed
 * columns, ENG_MAXDEPTH - 1 R lines at most, the terminator), CAT LIST
 * and LTYPE LIST (every field of the table, ENG_MAXCODES R lines at
 * most) and ITEM TAGS (the tags of a set of ENG_TAGSET bytes and a C
 * line).  DPADMIN.C checks the fifth, ADMIN CLIENTS (PROTO_MAXPEERS R
 * lines at most), the same way.
 * The answer of IMPORT with refused lines is not paged either: "+DATA",
 * the F line, at most PROTO_IMPERRS R lines (a line number, a field
 * name, a code and an error text of at most ENG_TXMAX characters),
 * "K ERRORS=n", possibly "K ERROR=code", and the terminator, well below
 * PROTO_MAXUNPAGED.
 * Requests that create or change a record with text fields (ITEM ADD,
 * LOAN OUT, MAINT ADD) may need more room than one line holds when
 * every field is long and quoted; clients send the rest with SET (the
 * forms of the full-screen client do, see FORM.H).
 * A request that escapes more than it needs to may pass PROTO_MAXLINE
 * and is then refused with ENG_ETOOLONG.
 * --------------------------------------------------------------------- */
#define PROTO_MAXLINE   292     /* request payload, CR LF excluded      */
#define PROTO_MAXRESP   512     /* response line, CR LF excluded        */
#define PROTO_MAXUNPAGED 4096   /* bytes of any unpaged response        */
#define PROTO_MEMOLINE  200     /* bytes of memo text per L line        */
#define PROTO_MAXARGS   8       /* positional tokens, command included  */
#define PROTO_MAXPAIRS  24      /* KEY=value pairs                      */
#define PROTO_MAXKEY    DBF_NAMELEN /* characters in a pair's key       */
#define PROTO_PAGEMAX   25      /* PAGE= range 1..PROTO_PAGEMAX         */
#define PROTO_PAGEDEF   20
#define PROTO_MAXFAIL   3       /* failed logins that close a session   */
#define PROTO_IMPERRS   10      /* refused lines an IMPORT answer lists */

/* Output room a paged response keeps free: a whole R line and its CR
   LF, the C line ("C ", a cursor, CR LF), the terminator, and one spare
   byte, kept so that the page boundaries clients already see do not
   move.  A page stops early when the sink reports less. */
#define PROTO_ROOMMIN   ((PROTO_MAXRESP + 2) + (2 + ENG_CURSORLEN + 2) + \
                         3 + 1)

/* ---------------------------------------------------------------------
 * Line assembly (TOKEN.C).
 *
 * Bytes from a transport are fed one at a time.  A line ends at LF; a CR
 * just before the LF is dropped.  A line longer than PROTO_MAXLINE is
 * consumed up to its LF and reported as PROTO_ACC_LONG, so the caller
 * answers it with error 103 and stays in step.
 * --------------------------------------------------------------------- */
#define PROTO_ACC_NONE  0       /* no complete line yet                 */
#define PROTO_ACC_LINE  1       /* buf holds len bytes and a NUL        */
#define PROTO_ACC_LONG  2       /* an over-long line was discarded      */

typedef struct {
    char buf[PROTO_MAXLINE + 2];
    u16  len;
    u8   over;                  /* discarding until the next LF         */
    u8   ready;                 /* the last feed returned a line        */
} proto_acc;

/* Start an empty assembler, wiping whatever buf held. */
void proto_acc_init(proto_acc *a);

/* Feed byte c (0..255); returns PROTO_ACC_.  After PROTO_ACC_LINE the
   line stays in buf until the next feed, which wipes it first. */
int proto_acc_feed(proto_acc *a, int c);

/* Wipe the assembled line now, keeping the assembler's state.  A
   transport calls it once proto_dispatch has returned, so a password
   in the line does not wait in memory for the next byte. */
void proto_acc_wipe(proto_acc *a);

/* ---------------------------------------------------------------------
 * Tokenizer (TOKEN.C).
 *
 * Tokens are separated by spaces (and tabs).  A token with '=' outside
 * quotes is a pair: the part before the first '=' is its key, 1 ..
 * PROTO_MAXKEY characters A-Z a-z 0-9 '_' starting with a letter or '_',
 * stored upper-cased, and the rest is its value (possibly empty).  Other
 * tokens are positional.  Quotes may surround a whole positional token
 * or a pair's value and keep spaces and '='; inside quotes the escapes
 * \" \\ \n (byte 0A) and \xHH (two hex digits, not 00) are decoded;
 * outside quotes a backslash is an ordinary character.  Positional
 * tokens come first: a positional token after a pair, an unterminated
 * quote, an unknown escape, a pair with an empty or invalid key, or more
 * than PROTO_MAXARGS positional tokens or PROTO_MAXPAIRS pairs give
 * ENG_ESYNTAX.  A key given twice gives ENG_EARG naming it.  Bytes
 * 80..FF pass through unchanged.
 * --------------------------------------------------------------------- */
typedef struct {
    char buf[PROTO_MAXLINE + PROTO_MAXARGS + PROTO_MAXPAIRS + 1];
    u16  nargs;                 /* 0 for an empty line                  */
    char *arg[PROTO_MAXARGS];   /* arg[0] is the command word           */
    u16  npairs;
    eng_pair pair[PROTO_MAXPAIRS];
    char bad[PROTO_MAXKEY + 1]; /* the key of an ENG_EARG result        */
} proto_req;

/* Split line (len bytes, no CR LF) into r; a line of more than
   PROTO_MAXLINE bytes gives ENG_ETOOLONG.  A raw NUL byte, a '"' inside
   a bare token or value, or anything but a space, tab or end of line
   after a closing quote gives ENG_ESYNTAX; the whole line is split
   before keys are compared, so a syntax error takes precedence over a
   repeated key.  A line of pairs only (nargs 0, npairs > 0) is not an
   empty line; the dispatcher answers it with ENG_ESYNTAX.  The tokens
   point into r->buf, so line may be reused afterwards. */
int proto_parse(proto_req *r, const char *line, u16 len);

/* The value of key (upper-case) in r, or NULL when absent. */
const char *proto_get(const proto_req *r, const char *key);

/* 1 if key is one of the names in the comma list list, compared without
   regard to case and to spaces around each name; an empty name matches
   nothing. */
int proto_in_list(const char *list, const char *key);

/* ---------------------------------------------------------------------
 * Response writer (RESPONSE.C).
 *
 * The writer builds one line at a time in its buffer and hands each
 * finished line, CR LF included, to the sink.  Between proto_data_begin
 * and proto_data_end, a line that begins with '.' is sent with a second
 * '.' in front, which counts towards PROTO_MAXRESP.  Values are sent bare
 * when they are non-empty and hold no space, '"', '\', '=' or byte below
 * 20 or equal to 7F; otherwise they are quoted with the tokenizer's
 * escapes (\" \\ \n, \xHH for other such bytes).  A part that would
 * take the line past PROTO_MAXRESP is left out whole, so a value is never
 * cut inside, the line's cut flag is set, and the line is still ended;
 * the limits above keep this from happening.  Callers that build lines
 * from data check cut after each line: proto_send_rows and
 * proto_send_record (DPCMD.H) treat a cut line as ENG_EINTERNAL and end
 * the block with its "K ERROR=" line.  A sink error is remembered: later
 * output is dropped and proto_out_failed returns 1.
 * --------------------------------------------------------------------- */

/* Send len bytes; nonzero means the connection is lost. */
typedef int (*proto_write_fn)(void *arg, const char *buf, u16 len);

/* Bytes the sink can take without blocking; NULL means unlimited. */
typedef u16 (*proto_room_fn)(void *arg);

typedef struct {
    proto_write_fn write;
    proto_room_fn  room;
    void *arg;
    u8   failed;
    u8   indata;                /* between +DATA and its terminator     */
    u8   cut;                   /* a part was left out of this line     */
    u16  len;                   /* bytes in line                        */
    char line[PROTO_MAXRESP + 2];   /* a line and CR LF                 */
} proto_out;

void proto_out_init(proto_out *o, proto_write_fn write, proto_room_fn room,
                    void *arg);
int  proto_out_failed(const proto_out *o);

/* Room left in the sink, 0xFFFF when unlimited. */
u16  proto_out_room(const proto_out *o);

/* A sink saved while another one is wrapped around it: proto_sink_push
   keeps o's sink in *saved and installs write, room (NULL for
   unlimited) and arg; proto_sink_pop puts the saved one back.  The
   wrapper's own functions pass the bytes on to saved->write and answer
   proto_sink_room(saved) for the room. */
typedef struct proto_sink {
    proto_write_fn write;
    proto_room_fn  room;
    void *arg;
} proto_sink;

void proto_sink_push(proto_out *o, proto_sink *saved, proto_write_fn write,
                     proto_room_fn room, void *arg);
void proto_sink_pop(proto_out *o, const proto_sink *saved);

/* Room left in the saved sink s, 0xFFFF when unlimited. */
u16  proto_sink_room(const proto_sink *s);

/* "+OK" or "+OK text" (text sent as it is; NULL for none). */
void proto_ok(proto_out *o, const char *text);

/* "-ERR code text" and " FIELD=name" when field is not NULL or "".
   text NULL gives the standard text of the code (proto_errtext), ""
   sends the code alone.  Inside an open +DATA block it writes
   "K ERROR=<code>" and the terminator instead; text and field are not
   sent there. */
void proto_err(proto_out *o, int code, const char *text, const char *field);

/* The standard text of an error code, e.g. "not found", copied out of
   far data into a static buffer that the next call overwrites. */
const char *proto_errtext(int code);

/* "+DATA" and the terminating "." line. */
void proto_data_begin(proto_out *o);
void proto_data_end(proto_out *o);

/* Building a line: begin with its first word ("+OK", "F", "R", "K",
   "L", "H", "P" or "C"), add parts, end it. */
void proto_line_begin(proto_out *o, const char *word);
void proto_line_word(proto_out *o, const char *word);    /* " word" raw */
void proto_line_value(proto_out *o, const char *val);    /* " value"    */
void proto_line_kv(proto_out *o, const char *key, const char *val);
                                                         /* " KEY=value" */
void proto_line_num(proto_out *o, const char *key, u32 n);  /* " KEY=n" */
void proto_line_unum(proto_out *o, u32 n);               /* " n"        */
void proto_line_end(proto_out *o);

/* A whole "K KEY=n" or "K KEY=value" line. */
void proto_k_num(proto_out *o, const char *key, u32 n);
void proto_k_kv(proto_out *o, const char *key, const char *val);

/* ---------------------------------------------------------------------
 * Sessions (SESSION.C).
 *
 *   PROTO_S_NEW      after connect: HELLO first
 *   PROTO_S_HELLO    HELLO given: LOGIN next
 *   PROTO_S_MUSTCHG  logged in with MUSTCHG set: PASSWD first
 *   PROTO_S_READY    logged in
 *   PROTO_S_CLOSED   BYE, PROTO_MAXFAIL failed logins or a failed ticket
 *                    login: the transport sends the output and closes
 *
 * HELLO in any open state starts again at PROTO_S_HELLO with the new
 * station, logging the user out.  LOGIN is accepted in PROTO_S_HELLO,
 * PROTO_S_MUSTCHG and PROTO_S_READY: success replaces the user, and a
 * failure once its arguments are present (ENG_EBADPW, ENG_EDISABLED, a
 * bad user name or an engine error) logs the session out to
 * PROTO_S_HELLO and counts; the PROTO_MAXFAIL-th failure closes it.  The
 * count is reset only by sess_init and a successful LOGIN.  The level is
 * fixed at LOGIN: USERS.LEVEL, capped at ENG_LVL_READ on a station named
 * in proto_host.readonly; account changes take effect at the next LOGIN.
 *
 * A session whose transport offers login tickets (tkok, which the
 * transport sets after sess_init) may also log in with a ticket of the
 * host's store (proto_tickets): a password LOGIN with TICKET=NEW is given
 * one, and LOGIN with TICKET= and a ticket in place of the password logs
 * in as its user, or, failing once its arguments are valid, closes the
 * session at once (sess_bye).  tk names the ticket the session holds,
 * from either LOGIN or from a PASSWD that replaced it; a password LOGIN
 * and logging out clear it.
 * --------------------------------------------------------------------- */
#define PROTO_S_NEW     0
#define PROTO_S_HELLO   1
#define PROTO_S_MUSTCHG 2
#define PROTO_S_READY   3
#define PROTO_S_CLOSED  4

typedef struct {
    u8   state;                 /* PROTO_S_                             */
    u8   fixsta;                /* station set by the transport         */
    char station[ENG_STALEN + 1];
    char user[ENG_USERLEN + 1];
    u8   userlevel;             /* USERS.LEVEL at LOGIN                 */
    u8   level;                 /* effective level, 0 before LOGIN      */
    u8   readonly;              /* station capped at ENG_LVL_READ       */
    u8   fails;                 /* failed LOGINs                        */
    u8   tkok;                  /* the transport offers tickets         */
    u8   tk;                    /* the ticket held: its number, or 0    */
} proto_session;

/* Start a session in PROTO_S_NEW.  A non-NULL station (a transport's
   own name for the caller: the NetBIOS caller's name, or the base name
   of a mailbox's request file) is kept, with spaces at both ends
   removed and cut to ENG_STALEN characters: HELLO is still required
   and its argument still checked as by sess_hello, but the transport's
   station is the one used.  A station that is then empty or holds a
   control byte is not kept; NULL (a serial line) leaves the station to
   HELLO.  The session holds no ticket and is offered none. */
void sess_init(proto_session *s, const char *station);

/* HELLO: set the station (1..ENG_STALEN characters, no control byte,
   no space first or last, else ENG_EARG naming STATION; AUDIT.STATION
   drops trailing spaces, so "STA1 " would be recorded as STA1 while
   escaping a cap on STA1), its read-only cap from the comma list
   readonly (NULL for none; names compared case-insensitively), and log
   out. */
int sess_hello(proto_session *s, const char *station, const char *readonly);

/* After eng_user_login or eng_user_resume: success sets the user,
   levels and state and clears tk; failure logs out and returns 1 when
   the session is now closed. */
void sess_login_ok(proto_session *s, const eng_login *l);
int  sess_login_failed(proto_session *s);

/* After a successful PASSWD: PROTO_S_MUSTCHG becomes PROTO_S_READY. */
void sess_passwd_ok(proto_session *s);

/* BYE: PROTO_S_CLOSED. */
void sess_bye(proto_session *s);

/* 1 if the session's effective level is at least level. */
int sess_level_ok(const proto_session *s, u8 level);

/* ---------------------------------------------------------------------
 * The entry point (DISPATCH.C).
 * --------------------------------------------------------------------- */

/* Extra K lines of STATUS from the program hosting the engine (uptime,
   sessions); NULL in local mode. */
typedef void (*proto_status_fn)(void *arg, proto_out *o);

/* One open session of a server, as ADMIN CLIENTS lists it. */
#define PROTO_KINDLEN   4       /* characters of a transport's kind     */
#define PROTO_MAXPEERS  10      /* sessions ADMIN CLIENTS lists at most */

typedef struct {
    u32  idle;                  /* seconds since its last activity      */
    u8   slot;                  /* server's number, 1 .. PROTO_MAXPEERS */
    u8   state;                 /* PROTO_S_                             */
    u8   level;                 /* effective level, 0 before LOGIN      */
    u8   self;                  /* 1 for the session whose request runs */
    char kind[PROTO_KINDLEN + 1];   /* "NETB", "COM1", "COM2", "MBOX"   */
    char station[ENG_STALEN + 1];   /* "" before its station is known   */
    char user[ENG_USERLEN + 1];     /* "" before LOGIN                  */
} proto_peer;

/* Fill p with open session i (from 0, in the server's slot order) and
   return 0, or return 1, p unchanged, when fewer than i + 1 sessions
   are open.  The sessions do not change while a request runs, so the
   calls for i = 0, 1, ... of one request see one consistent list. */
typedef int (*proto_peer_fn)(void *arg, u16 i, proto_peer *p);

/* Begin an orderly shutdown of the server and return at once.  ADMIN
   SHUTDOWN calls it after writing its +OK; a server already stopping
   ignores the call. */
typedef void (*proto_stop_fn)(void *arg);

/* The login table of a server: its login tickets, for the sessions
   whose transport offers them, and its failed-login throttle, for every
   session.  Each ticket is PROTO_TKTLEN upper-case hex digits, held for
   one user on one station, and named by a number of 1..255.  The store
   never keeps the ticket itself, and decides alone when one expires;
   the engine checks the tag a ticket was issued with against the user's
   PWHASH (eng_user_resume, ENGUSER.H).  The throttle refuses the
   password LOGINs of a user who has failed too often of late, whatever
   the session, so that a guesser gains nothing by opening another.  The
   host's table also names the session commands that use it (DPTKT.C),
   so that only a server links them and the engine's ticket calls
   (USERTK.C), never a client.  Only a file that defines PROTO_TICKETS
   before it includes this header reads the table (Watcom C 6.5 runs out
   of memory on some files if every file reads it). */
#ifdef PROTO_TICKETS
#define PROTO_TKTLEN    ENG_HASHLEN     /* hex digits of a ticket       */

struct proto_call;

typedef struct proto_tickets {
    /* A new ticket for user (the stored name) on station, whose PWHASH
       is hash, at tick now: its digits and a NUL into t (PROTO_TKTLEN +
       1 bytes); returns its number, never 0. */
    u8 (*issue)(u32 now, const char *user, const char *station,
                const char *hash, char *t);
    /* The number of ticket t if the store holds it for user on station
       at tick now, its tag (ENG_TAGLEN characters, no NUL) into tag;
       else 0, an entry of t held for another user or station dropped. */
    u8 (*redeem)(u32 now, const char *t, const char *user,
                 const char *station, char *tag);
    void (*drop)(u8 tk);                    /* drop ticket number tk    */
    void (*drop_user)(const char *user);    /* drop all of user's       */
    /* 1 while the password LOGINs naming user, as the request gives
       it, are refused at tick now. */
    int (*refused)(u32 now, const char *user);
    /* A password LOGIN of the live user user (its stored name), not
       refused, failed on its password (ok 0) or succeeded (ok 1) at
       tick now. */
    void (*note)(u32 now, const char *user, int ok);
    int (*cmd)(struct proto_call *c);       /* proto_ticket_cmd         */
} proto_tickets;

/* LOGIN, PASSWD or BYE on a host with a login table (DPTKT.C), which
   DISPATCH.C runs in place of the command's own handler.  On a session
   offered no tickets, or without TICKET=, the command answers as the one
   of DPSESS.C, its arguments checked alike, but that a password LOGIN
   naming a user the throttle refuses has its password checked as a
   wrong one (the same 401, audit row and count, whatever the password),
   and that a successful PASSWD drops every ticket of the user.  On a
   session offered tickets: LOGIN user password TICKET=NEW also gets a
   ticket; LOGIN user TICKET=<ticket> logs in with that, and any failure
   then closes the session after its -ERR line, ENG_EBADPW and
   ENG_EDISABLED also dropping the ticket; a successful PASSWD gives the
   session that held a ticket a new one, or, when the new PWHASH cannot
   be read, none; BYE TICKET=END drops the session's ticket before it
   ends the session. */
int proto_ticket_cmd(struct proto_call *c);
#endif

/* The program hosting the engine.  A local client sets e and leaves
   every other member 0 or NULL, which gives local mode: no capped
   station, no extra STATUS lines, no other sessions, no login table, and
   ADMIN CLIENTS and ADMIN SHUTDOWN answer ENG_ENOTAVAIL. */
typedef struct {
    eng *e;
    const char *readonly;       /* "STA1,STA2": capped stations, or NULL */
    proto_status_fn status;
    void *statusarg;
    u16 others;                 /* sessions open besides the one whose
                                   request runs (the server counts them
                                   before each request); 0 in local mode */
    proto_peer_fn peers;        /* the sessions, for ADMIN CLIENTS      */
    proto_stop_fn stop;         /* ADMIN SHUTDOWN                       */
    void *hostarg;              /* the argument of peers and stop       */
    struct proto_tickets *tickets;  /* the login table, or NULL         */
    u32 now;                    /* the request's tick, for the table    */
} proto_host;

/* Results of proto_dispatch. */
#define PROTO_R_OK      0       /* response written, session goes on    */
#define PROTO_R_CLOSE   1       /* response written, then close         */
#define PROTO_R_NONE    2       /* empty line: nothing written          */

/* Process one request line (len bytes, no CR LF) and write exactly one
   response to o; a line of only spaces is no request and gets none.  It
   clears the engine's error and sets its actor from the session first.
   On a PROTO_S_CLOSED session it writes nothing and returns
   PROTO_R_CLOSE.  A line the assembler reported as PROTO_ACC_LONG is
   answered by the transport with proto_err(o, ENG_ETOOLONG, NULL, NULL)
   instead. */
int proto_dispatch(proto_host *h, proto_session *s, const char *line,
                   u16 len, proto_out *o);

#endif
