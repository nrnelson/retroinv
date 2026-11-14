/*
 * BRIDGE.H - the TCP bridge: each TCP connection relayed over its own
 * NetBIOS session to the server (BRIDGE.C).
 *
 * Every idle slot of TSOCK.H listens on BR_PORT.  A connection that arrives
 * makes its slot CALL the server's name from the bridge's one local name,
 * and once the session is open the slot relays bytes, never lines, both
 * ways: it never parses them, so it can neither log nor alter a request,
 * nor a password in one.  With an allow-list, a connection the slot sees
 * open from an address on none of its networks is reset (ts_abort) at
 * once, before any CALL, and the slot listens again.  A slot runs through
 *
 *   listening -> calling -> open -> draining -> closing -> listening
 *
 * on two no-wait NCBs (nba_, COMPAT.H): one for the CALL and then the
 * RECEIVE, one for the SEND.  The CALL asks for no session timeouts;
 * idle sessions are the server's to end.  A CALL that completes with
 * NB_RC_TIMEOUT is posted once more TMR_SEC ticks later; a post
 * refused with NB_RC_TOOMANY is made again on a later pass.
 *
 * TCP to NetBIOS: while no SEND is in progress the slot reads what the
 * client sent, through the bounce buffer, into its transmit area, up to
 * NB_MSGMAX bytes in one pass, and sends it as one message.  While the
 * SEND is in progress the bytes stay in the stack, whose receive window
 * then closes on the client.  NetBIOS to TCP: one RECEIVE of up to
 * NB_MSGMAX bytes into the receive area is in progress at a time; what
 * it received goes to the client as fast as ts_room allows, and the
 * next RECEIVE is posted only once all of it has, so a slow client
 * holds the server's SEND back.
 *
 * Ends:
 *
 *   the client closes or resets the connection: the CALL, RECEIVE and
 *       SEND in progress are cancelled, the session hung up unless the
 *       provider has removed it (below), every byte not yet relayed
 *       either way dropped; CLOSE TCP;
 *   the server hangs up (NB_RC_SESSCLOSED: after BYE, the last failed
 *       LOGIN, a shutdown, or an idle session): the slot drops the
 *       client's bytes not yet sent and drains: it gives the client
 *       every byte received, waits for ts_unsent to reach 0, closes the
 *       connection (ts_close) and waits for it to close; each wait lasts
 *       at most BR_CLOSEWAIT ticks, after which the slot goes on, a
 *       close then being aborted; CLOSE NETB;
 *   the session is lost (NB_RC_ABORTED, NB_RC_BADLSN, a SEND that
 *       fails otherwise, or a post refused with another code): the
 *       same, CLOSE LOST;
 *   the client takes none of the bytes received for BR_DELIVERWAIT
 *       ticks: the slot cancels its commands, hangs the session up
 *       unless the provider has removed it, drops every byte not yet
 *       relayed either way and closes the connection, its close bounded
 *       as above; CLOSE LOST;
 *   the CALL fails (NB_RC_NOANSWER, NB_RC_REJECTED, NB_RC_SESSFULL, a
 *       second NB_RC_TIMEOUT, or another code): the connection is
 *       closed without a byte, so the client sees it accepted, then its
 *       end; NOCALL.
 *
 * A session the provider has removed (a command on it completed or was
 * refused with NB_RC_SESSCLOSED, NB_RC_ABORTED or NB_RC_BADLSN) is
 * never hung up: its LSN may already be another session's.  A CALL
 * cancelled as it completed leaves a session, which is hung up.  A
 * cancel the provider does not confirm (NB_RC_PENDING) leaves its NCB
 * in progress: the slot cancels it again, or polls it, on later passes,
 * and hangs up only a session the command's final code shows still
 * held.  A slot zeroes its areas and listens again only once both its
 * NCBs are free and its connection is TS_CLOSED.  While the server is
 * down every connection ends in NOCALL; the bridge never stops by
 * itself.
 *
 * Request bytes hold passwords.  The bounce buffer is zeroed after each
 * use, a slot's transmit area once its SEND has completed, and both its
 * areas when its connection ends.
 *
 * The bridge reads the clock only in br_start, for the digits of a
 * generated name, and never calls ts_poll; br_poll's now is the
 * caller's tick count, TMR_SEC to the second, which never goes back.
 * br_poll waits only inside the provider's wait-form CANCEL and HANG
 * UP.
 */
#ifndef BRIDGE_H
#define BRIDGE_H

#include "COMPAT.H"

#define BR_PORT         7000    /* the TCP port the slots listen on     */
#define BR_BOUNCE       512     /* bytes of the near bounce buffer      */
#define BR_CLOSEWAIT    TMR_10SEC /* ticks a closing wait lasts         */
#define BR_DELIVERWAIT  (6 * TMR_10SEC) /* ticks a delivery may stall   */

/* Results of br_start. */
#define BR_OK           0
#define BR_EARG         1       /* an argument out of range, or the
                                   bridge started or its block still
                                   kept (br_stop)                       */
#define BR_ENOMEM       2       /* no memory for the NCBs or buffers    */
#define BR_ENAME        3       /* the local name is taken:
                                   NB_RC_NAMEINUSE, or every generated
                                   name taken                           */
#define BR_ENB          4       /* ADD NAME failed with another code    */
#define BR_ELISTEN      5       /* a slot could not listen (ts_listen)  */

/* Events, reported through br_events with their text. */
#define BR_EV_OPEN      0       /* a connection: "a.b.c.d:port" of the
                                   client                               */
#define BR_EV_CALL      1       /* its session opened: ""               */
#define BR_EV_NOCALL    2       /* its CALL failed: the NetBIOS return
                                   code in two upper-case hex digits    */
#define BR_EV_CLOSE     3       /* it ended: "TCP", "NETB", "LOST" or
                                   "STOP"                               */
#define BR_EV_REFUSE    4       /* a connection the allow-list refused,
                                   reset: as OPEN's                     */
/* A slot's OPEN is followed by CALL and then CLOSE, by NOCALL, or, when
   the client leaves or the bridge stops during the CALL, by CLOSE; a
   REFUSE stands alone. */

#define BR_ALLOWMAX     4       /* networks of an allow-list            */

/* A network of the allow-list: the addresses whose bits under mask are
   those of addr, an address being a u32 whose highest byte is the first
   of its dotted form. */
typedef struct {
    u32  addr;
    u32  mask;
} br_net;

/* Start the bridge: add the local name, take the NCBs (two per slot)
   and one far block for every slot's transmit and receive areas
   (NB_MSGMAX bytes each), and make every slot listen on BR_PORT.
   server is the server's NetBIOS name, upper-cased (ASCII letters) and
   cut to NB_NAMELEN - 1 characters.  The bridge's local name is set up
   by nb_local_name (COMPAT.H) from local, with the prefix "RTCP" and
   NB_NAMETRIES tries; br_stop deletes it only if it was added.  The
   allow-list is the nallow networks (0..BR_ALLOWMAX, 0 letting every
   address in) at allow, which the bridge reads until br_stop.
   nb_present() must have returned 1 and ts_init TS_OK, or the result is
   BR_ENB or BR_ELISTEN.  Returns BR_OK, or a BR_E code with nothing
   left behind: BR_EARG for an empty name, more than BR_ALLOWMAX
   networks, a bridge already started or the block of a stuck stop
   still kept, the others as their definitions say; *nbrc receives the
   NetBIOS return code of the command that failed, NB_OK when none
   did. */
int br_start(const char *server, const char *local, const br_net *allow,
             u8 nallow, u8 *nbrc);

/* One pass at tick time now: each slot in turn, starting from the one
   after the last pass's first, handles at most one completed NetBIOS
   command and relays at most one piece each way.  Returns the number
   of commands posted or completed and pieces relayed, 0 when there was
   nothing to do.  Does nothing before br_start or after br_stop. */
u16 br_poll(u32 now);

/* Stop the bridge: abort every connection (ts_abort), report CLOSE STOP
   for each slot that had one, cancel every command, hang up every
   session the provider still holds, delete the name if br_start added
   it and close the NCBs.  Returns what nba_close returned: the NCBs
   the provider still owned, 0 normally; the far block is then zeroed
   and freed.  With a nonzero count the block stays allocated, as it is,
   since the provider may still use it, and the program waits in
   nba_drain as nba_close (COMPAT.H) says before it ends.  While the
   bridge is not started, this call and br_start zero and free a block
   so kept once nba_stuck reports that the provider owns no NCB; until
   then br_start refuses, so that at most one block is ever kept.
   Returns 0 when the bridge is not started. */
u16 br_stop(void);

/* The number of NetBIOS sessions the bridge holds. */
u8 br_sessions(void);

/* Receives an event of slot (0..TS_SLOTS - 1); text is valid only
   during the call and never holds a byte relayed. */
typedef void (*br_event_fn)(void *arg, u8 slot, u8 ev, const char *text);

/* Report every event from now on through fn(arg, ...), from inside
   br_poll and br_stop; NULL for none, the setting at the start of a
   run. */
void br_events(br_event_fn fn, void *arg);

/* Observation points for the tests' wipe checks.  br_buffers is the
   far block of the slots' areas, slot k's transmit area at offset
   2 * k * NB_MSGMAX and its receive area NB_MSGMAX bytes after it, or
   NULL when the bridge is not started; br_bounce is the bounce buffer,
   BR_BOUNCE bytes. */
cmem_blk *br_buffers(void);
const u8 *br_bounce(void);

#endif
