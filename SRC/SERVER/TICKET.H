/*
 * TICKET.H - the server's store of login tickets and its login table
 * (TICKET.C).
 *
 * A mailbox session lasts one request file, so a mailbox client logs in
 * with its password once, asking for a ticket (LOGIN TICKET=NEW,
 * PROTO.H), and with the ticket in its later files.  The store, which
 * the server opens only with MAILBOX=, holds at most TKT_MAX tickets in
 * one far block, each for one user on one station, with the tag of the
 * PWHASH the user had then (ENGUSER.H).  A ticket is never kept: an
 * entry holds the eng_mix128 of its digits, from which no ticket can be
 * made, and an entry dropped, expired or replaced is zeroed.  A station
 * holds at most one ticket, a new one replacing it whatever its user;
 * a full store gives up the entry idle the longest.  A ticket expires
 * once it has gone unused for the idle limit, or TKT_LIFE ticks after
 * its issue; each issue and redeem first drops those expired.  The
 * store lives in memory only, so a restart of the server ends every
 * ticket.
 *
 * A ticket is eng_mix128 over a pool of entropy, a count of the tickets
 * made, the timer chip's count (tmr_fine), the tick, the user's PWHASH,
 * the user and the station; the pool is then mixed with the count, so
 * that the tickets a client sees tell nothing of the next.  Between
 * tickets the pool takes in, byte by byte, the timing of the server's
 * events (tkt_stir): the time of day in BIOS ticks, the main loop's
 * passes since the last event and the chip's count.
 *
 * The store never reads the clock: now is CONN.H's tick count.  A file
 * that includes this header defines PROTO_TICKETS first (PROTO.H).
 */
#ifndef TICKET_H
#define TICKET_H

#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"

#define TKT_MAX         8       /* tickets the store holds              */
#define TKT_LIFE        ((u32)12 * 360 * TMR_10SEC) /* ticks: 12 hours  */

/* Open the store, empty, for tickets that expire unused after idle
   ticks (0: never), its pool stirred with ticks, the time of day
   (tmr_ticks).  Returns the login table (tkt_logins), or NULL when
   there is no memory or the store is open already. */
proto_tickets *tkt_open(u32 idle, u32 ticks);

/* The server's login table (proto_tickets, PROTO.H): the store's calls,
   the failed-login throttle's (THROTL.H) and the commands that use
   them.  The server gives it to every session's LOGIN, the store open
   or not: only a session offered tickets, which the store must be open
   for, calls the store but for drop_user, which a closed store
   answers as holding no ticket. */
proto_tickets *tkt_logins(void);

/* Zero and free the store and its pool; nothing when it is not open. */
void tkt_close(void);

/* An event of the server: stir the pool with ticks, the time of day
   (tmr_ticks), passes, the main loop's passes since the last event,
   and tmr_fine.  Nothing when the store is not open. */
void tkt_stir(u32 ticks, u16 passes);

/* The store's far block, NULL when it is not open: an observation point
   for tests, which check that no ticket is ever in it. */
cmem_blk *tkt_block(void);

#endif
