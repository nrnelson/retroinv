/*
 * TNETB.H - the server's NetBIOS transport (TNETB.C).
 *
 * The server adds its name and keeps LISTENs posted for it, each on a
 * slot claimed from CONN.H, at most as many as LISTENS= asks and as
 * slots are free.  A call that arrives opens the slot's session with
 * the caller's NetBIOS name as its station.  Each open slot has a far
 * buffer of CONN_RXSZ bytes, into which a RECEIVE is posted whenever
 * CONN has given the last input back (ops->rx_done), and at most one
 * SEND of up to NB_MSGMAX bytes in progress, sent straight from the
 * slot's output block.  A message may hold several lines or part of
 * one; a message longer than the buffer arrives in pieces
 * (NB_RC_INCOMPLETE), each handed to conn_input in turn.  Everything
 * runs on no-wait NCBs (nba_, COMPAT.H), polled, two per slot: one for
 * the LISTEN and then the RECEIVE, one for the SEND.  A post refused,
 * or a LISTEN completed, with NB_RC_TOOMANY or NB_RC_SESSFULL is tried
 * again on a later pass.  A LISTEN refused or completed with any other
 * code (the name deleted under the server, say) keeps its slot, and no
 * LISTEN is posted until TMR_SEC ticks after it, so the server can
 * idle meanwhile.  A session lost or hung up by the caller frees its
 * slot (conn_closed).
 *
 * The transport never reads the clock; now is CONN.H's tick count.
 */
#ifndef TNETB_H
#define TNETB_H

#include "COMPAT.H"

/* Results of tnetb_start. */
#define TNETB_OK        0
#define TNETB_EARG      1       /* an argument out of range, or the
                                   transport started or its buffers
                                   still kept (tnetb_stop)              */
#define TNETB_ENONB     2       /* no NetBIOS provider                  */
#define TNETB_ENAME     3       /* the name is taken: NB_RC_DUPNAME or
                                   NB_RC_NAMEINUSE                      */
#define TNETB_ENB       4       /* ADD NAME failed with another code    */
#define TNETB_ENOMEM    5       /* no memory for the NCBs or buffers    */

/* Start the transport: add name (1..NB_NAMELEN - 1 characters) and
   take the NCBs and receive buffers for sessions slots (1..CONN_NETMAX),
   with at most listens (1..sessions) LISTENs posted at once.  conn_init
   must have been called, and the caller keeps sessions at most its
   nslots, which this call cannot check.  The first LISTENs are posted
   by tnetb_poll.  Returns TNETB_OK, or a TNETB_E code with nothing left
   behind: TNETB_EARG for a name, sessions or listens out of range, a
   transport already started, or the receive buffers of a stuck stop
   still kept, the others as their definitions say;
   *nbrc receives the NetBIOS return code of the command that failed,
   NB_OK when none did.  nb_present() must have returned 1, or the
   result is TNETB_ENONB. */
int tnetb_start(const char *name, u8 sessions, u8 listens, u8 *nbrc);

/* One pass at time now: post the LISTENs that are missing and the
   RECEIVEs and SENDs that are due, and handle at most one completed
   command per slot.  Returns the number of commands posted or
   completed and of slots ended (0 when there was nothing to do).  Does
   nothing before tnetb_start or after tnetb_stop. */
u16 tnetb_poll(u32 now);

/* Stop the transport: cancel every LISTEN and RECEIVE, cancel or hang
   up every SEND, hang up every session, each slot then given back with
   conn_closed, delete the name and close the NCBs.  Returns what
   nba_close returned: the NCBs the provider still owned, 0 normally;
   with a nonzero count the server must not end while the provider may
   still write into them, and waits in nba_drain as nba_close (COMPAT.H)
   says.  The receive buffers are then kept, as they are: while the
   transport is not started, this call and tnetb_start zero and free
   them once nba_stuck reports that the provider owns no NCB; until then
   tnetb_start refuses, so that at most one block is ever kept.  Returns
   0 when the transport is not started.
   Every exit path of the server calls it, and calls it again after the
   wait. */
u16 tnetb_stop(void);

/* Number of LISTENs in progress. */
u16 tnetb_listening(void);

#endif
