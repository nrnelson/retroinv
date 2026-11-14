/*
 * CONN.H - the server's connection slots (CONN.C).
 *
 * A slot carries one session of a transport to the protocol: its own
 * proto_session and line assembler, and a far output block of
 * CONN_OUTSZ bytes.  A slot is served only while its block is empty, so
 * one proto_out, built for each request, writes every slot's responses
 * into its block.  The transports move bytes; CONN turns them into
 * requests, runs proto_dispatch and holds the responses until the
 * transport has sent them.  There are three transports, each naming
 * its slots' kind:
 *
 *   NetBIOS  TNETB.H, kind "NETB": up to CONN_NETMAX sessions
 *   serial   TSERIAL.H, kind "COM1" or "COM2": one session per port, up
 *            to CONN_SERMAX
 *   mailbox  TMBOX.H, kind "MBOX": one request file at a time, up to
 *            CONN_MBXMAX
 *
 * There is one table per program, of at most CONN_MAX slots, room for
 * all three; conn_init gives it the size the program's transports
 * need.
 *
 * A slot is CONN_FREE; CONN_LISTEN once a transport has claimed it and
 * waits for a caller; CONN_OPEN while its session takes requests (the
 * protocol state, HELLO given or a user logged in, is the session's);
 * CONN_CLOSING once the session is done (BYE, the last failed LOGIN, a
 * request refused at shutdown, an idle timeout) and the slot waits for
 * its output to drain and for the transport to hang up.  A slot of any
 * other state becomes CONN_CLOSING before ops->close is called for it,
 * a CONN_LISTEN one included; its input not yet read and its partial
 * line are then zeroed and dropped.
 *
 * Input.  The transport hands over each message it has received with
 * conn_input; the bytes stay in the transport's own far buffer, which
 * CONN reads and the transport leaves alone until CONN gives it back
 * with ops->rx_done, after zeroing every byte, so no request (and no
 * password in one) waits in memory for the next message.  The
 * transport receives the next message only then, which is the flow
 * control: a client that sends faster than it is served is held back
 * by its own transport.
 *
 * Service.  conn_service makes one pass over the slots in turn,
 * starting one slot further on each pass, and answers at most one
 * request per slot, so every session gets the same share; a slot whose
 * output has not all been sent is passed over, so the output block
 * holds at most one response, which a paged response keeps within the
 * room left (PROTO_ROOMMIN).  The assembled line, and the proto_out
 * that wrote the response, are wiped as soon as proto_dispatch has
 * returned, as either may hold a secret: a password, a login ticket.
 * An over-long line is answered with ENG_ETOOLONG.  A blank line is no
 * request: it is consumed, gets no response and no event, and does not
 * count as activity.
 *
 * Output.  After a request CONN calls ops->kick; the transport then
 * asks for the waiting bytes with conn_out_peek, sends them from the
 * block in pieces of its own size, and reports each piece sent with
 * conn_out_done.  The bytes stay where they are until then.
 *
 * Time.  CONN never reads the clock: the program passes now, a count
 * of timer ticks (about 18.2 to a second; TMR_SEC is the whole part)
 * that never goes back, to every call that needs it.  Spans of ticks
 * are turned into whole seconds as TMR_10SEC (COMPAT.H) says.  A
 * request's work may take long (ADMIN PACK, IMPORT), so the program
 * reads the clock again after conn_service and gives it to conn_served,
 * from which the slots answered count their idle time.
 *
 * Shutdown.  conn_shutdown, or ADMIN SHUTDOWN through the host's stop
 * callback, refuses new slots, has the transports end their LISTENs and
 * answers the next request of every open session with ENG_ESHUTDOWN,
 * closing it afterwards; the response being sent is sent first.  An
 * over-long line is answered so too; a blank line is still no request.
 * After CONN_SHUTWAIT ticks every slot still in use is closed.
 * conn_done then tells the program it may stop its transports.
 *
 * The transport's callbacks call back into CONN only as conn_tops
 * allows; the calls that call ops (conn_service, conn_out_done,
 * conn_timeouts, conn_shutdown, conn_open) allow that.
 *
 * CONN.C checks at compile time that CONN_OUTSZ is at least
 * PROTO_MAXUNPAGED + PROTO_ROOMMIN (the longest unpaged response with
 * room for a page to spare) and at most CMEM_MAXBLK, and that CONN_MAX
 * is at most PROTO_MAXPEERS.
 */
#ifndef CONN_H
#define CONN_H

#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"

#define CONN_NETMAX     6       /* NetBIOS sessions at most             */
#define CONN_SERMAX     2       /* serial ports at most                 */
#define CONN_MBXMAX     1       /* mailbox sessions at most             */
#define CONN_MAX        (CONN_NETMAX + CONN_SERMAX + CONN_MBXMAX)
#define CONN_OUTSZ      6144u   /* output block of a slot               */
#define CONN_RXSZ       512     /* longest input a transport hands over */
#define CONN_SHUTWAIT   (5 * TMR_SEC)   /* ticks a shutdown waits       */
#define CONN_WORDLEN    8       /* characters of a word in a trace text */
#define CONN_TRACELEN   (2 * CONN_WORDLEN + 1)  /* a trace text         */

/* Slot states, from conn_state. */
#define CONN_FREE       0
#define CONN_LISTEN     1
#define CONN_OPEN       2
#define CONN_CLOSING    3

/* A transport's callbacks, each given the slot.  The table holds a
   pointer to the struct, which lives as long as the program.  A
   callback only starts the transport's work: a post that fails or
   completes at once is handled on the transport's next poll. */
typedef struct {
    /* Output waits: start sending it unless a send is in progress.  It
       may call conn_out_peek for c, nothing else of CONN. */
    void (*kick)(u8 c);
    /* The input given to conn_input is consumed and its bytes zeroed:
       receive the next message.  It calls nothing of CONN. */
    void (*rx_done)(u8 c);
    /* End the slot's session: hang it up, or end the LISTEN of a
       CONN_LISTEN slot; then call conn_closed for c, before it returns
       or later, and nothing else of CONN.  Called once per session. */
    void (*close)(u8 c);
    const char *kind;           /* PROTO_KINDLEN characters, as above   */
    u8   tickets;               /* 1: sessions may use login tickets    */
} conn_tops;

/* Events for the status screen and the event log, from conn_watch. */
#define CONN_EV_OPEN    1       /* a session opened; text: its station  */
#define CONN_EV_LOGIN   2       /* a LOGIN answered "+OK"; text: the
                                   user                                 */
#define CONN_EV_REQ     3       /* a request was answered; text below   */
#define CONN_EV_CLOSE   4       /* an open session ended; text ""       */

/* Receives event ev of slot c.  The text of CONN_EV_REQ names the
   request without ever showing an argument: its first word, and, unless
   that word is HELLO, LOGIN or PASSWD, a space and its second word,
   each only when it consists of letters alone, upper-cased and cut to
   CONN_WORDLEN characters ("ITEM LIST", "LOGIN"; "" for an over-long
   line or one that starts with anything else, a blank included).  The
   first word starts at the line's first byte.  CONN_EV_LOGIN follows
   the CONN_EV_REQ of a request whose first word is LOGIN and whose
   response starts with '+'.  text holds at most CONN_TRACELEN
   characters and lasts until the callback returns. */
typedef void (*conn_event_fn)(u8 c, u8 ev, const char *text);

/* Start the table with nslots usable slots (1..CONN_MAX, numbered 0 ..
   nslots - 1), each with its output block, all CONN_FREE, serving the
   engine e; readonly is the host's list of capped stations (proto_host,
   NULL for none), kept by pointer; idle is the idle timeout in ticks, 0
   for none; now starts the uptime.  The host passed to proto_dispatch
   carries the STATUS lines "K UPTIME=<seconds>" and "K SESSIONS=<open
   sessions, the requester's included>", the count of the other open
   sessions, the peers for ADMIN CLIENTS (conn_info), the stop
   callback, the request's now and the login table of conn_tickets.
   Returns 0; CF_EINVAL when nslots is out of range or the table is
   started already; or CF_ENOMEM.  Nothing is allocated or changed
   then.  CONN has no codes of its own: a bad argument and no memory,
   its only failures, are named by those of the cf_ layer. */
int conn_init(eng *e, const char *readonly, u8 nslots, u32 idle, u32 now);

/* Free the output blocks and stop the table, which conn_init may start
   again.  Every slot must be CONN_FREE: the transports have stopped.
   Does nothing when the table is not started. */
void conn_free(void);

/* Send events to fn from now on; NULL for none (the start). */
void conn_watch(conn_event_fn fn);

/* Have every session's LOGIN, PASSWD and BYE use the login table t
   from now on, and the sessions of the transports whose
   conn_tops.tickets is 1 its tickets (proto_session.tkok); NULL for
   none (the start).  The table lives as long as CONN uses it.
   Declared under PROTO_TICKETS (PROTO.H). */
#ifdef PROTO_TICKETS
void conn_tickets(proto_tickets *t);
#endif

/* Claim a free slot for the transport ops: 1 with *c set, the slot then
   CONN_LISTEN; 0 when no slot is free or a shutdown has begun. */
int conn_claim(const conn_tops *ops, u8 *c);

/* A call has opened a session on the CONN_LISTEN slot c, from station
   (the transport's name for the caller, kept as sess_init keeps it, or
   NULL for the station HELLO gives): the slot becomes CONN_OPEN with a
   new session in PROTO_S_NEW, offered tickets as its transport's
   conn_tops says, its idle time starting at now, and
   CONN_EV_OPEN is sent.  During a shutdown the slot goes instead
   straight to CONN_CLOSING and ops->close, with no event. */
void conn_open(u8 c, u32 now, const char *station);

/* The transport has received len bytes (1..CONN_RXSZ) for slot c, at
   off in b.  CONN reads them during conn_service, zeroes them and calls
   ops->rx_done; until then the transport neither changes them nor
   calls conn_input for c again.  On a slot not CONN_OPEN the bytes are
   zeroed at once and ops->rx_done is not called. */
void conn_input(u8 c, cmem_blk *b, u16 off, u16 len);

/* The output of slot c not yet sent: its byte count, 0 when there is
   none, with *b and *off set to where the bytes are when there are
   some.  They stay there, unchanged, until conn_out_done. */
u16 conn_out_peek(u8 c, cmem_blk **b, u16 *off);

/* n bytes (1 .. what conn_out_peek gave) of slot c's output have been
   sent; its idle time starts again at now.  The transport sends the
   rest after peeking again; CONN does not kick for it.  Once nothing is
   left the slot is served again, or, when CONN_CLOSING, ops->close is
   called. */
void conn_out_done(u8 c, u32 now, u16 n);

/* The transport has ended slot c's session or LISTEN (a hang-up by
   either end, a lost session, a LISTEN cancelled or failed): the slot
   is wiped, input not yet read included, its output block zeroed, and
   becomes CONN_FREE; an open session's end sends CONN_EV_CLOSE.
   Nothing is sent to the transport for c afterwards.  Does nothing for
   a CONN_FREE slot. */
void conn_closed(u8 c);

/* One pass of service, as described above, at time now.  Returns the
   number of requests answered (0 when there was nothing to do). */
u16 conn_service(u32 now);

/* The service just run by conn_service ended at now: every slot it
   answered a request of is idle from now, not from the service's
   start. */
void conn_served(u32 now);

/* Close, through ops->close, every CONN_OPEN or CONN_CLOSING slot that
   has been idle for the idle timeout (no request answered and no
   output sent since then, which also ends a send that never
   completes), and, once a shutdown has waited CONN_SHUTWAIT ticks,
   every slot not free.  Each slot is closed once. */
void conn_timeouts(u32 now);

/* Begin the shutdown described above at time now (the operator's
   request; ADMIN SHUTDOWN uses the time of its request).  Further calls
   do nothing. */
void conn_shutdown(u32 now);

/* 1 once a shutdown has begun, else 0. */
int conn_stopping(void);

/* 1 once a shutdown has begun and every slot is CONN_FREE, else 0. */
int conn_done(void);

/* CONN_ state of slot c (CONN_FREE for c beyond the table). */
u8 conn_state(u8 c);

/* Number of CONN_OPEN slots. */
u16 conn_count(void);

/* Fill p with CONN_OPEN slot i (from 0, in slot order) and return 0, or
   return 1, p unchanged, when fewer than i + 1 slots are open; the
   host's peers callback answers with it.  slot is the slot number + 1,
   kind the transport's, idle in seconds since its last request answered
   or output sent, up to the latest now any CONN call was given (0 for
   the slot whose request is running), self 1 only for that slot. */
int conn_info(u16 i, proto_peer *p);

/* Seconds since conn_init. */
u32 conn_uptime(u32 now);

/* The bytes of the slots themselves, *size of them: an observation
   point for tests, which check that no password is left in them once
   its request has been answered. */
const void *conn_bytes(u16 *size);

#endif
