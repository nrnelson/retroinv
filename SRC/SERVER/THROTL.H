/*
 * THROTL.H - the server's failed-login throttle (THROTL.C).
 *
 * A session closes after PROTO_MAXFAIL failed logins, but a guesser may
 * open another; the throttle counts the failed password LOGINs of each
 * user instead, whatever the session, transport or station.  Once
 * THR_FAILS of them fall within THR_SPAN ticks of the first, every
 * password LOGIN naming that user is refused for THR_HOLD ticks, the
 * right password included, with the 401 of a wrong one (proto_tickets,
 * PROTO.H); a success before then clears the user's own count.  A
 * ticket LOGIN is never refused.
 *
 * The table holds THR_MAX entries in one far block: THR_MAX - 1 users
 * and one overflow entry that all other users share.  A user new to the
 * table takes a free entry, or one whose count or refusal is over,
 * carrying the overflow entry's count with it; an entry that still
 * counts or refuses is never taken from its user.  When none is left,
 * the failure counts in the overflow entry, and once that one refuses,
 * every user not in the table is refused alike.  So no guesser can push
 * a user's count out by failing as other users, and a user who leaves
 * the overflow keeps what was counted there; but one who knows enough
 * user names can have every user outside the table refused for THR_HOLD
 * ticks, a denial of service.  A name that is no live user's is not
 * counted: the engine refuses it whatever the password.  The table lives
 * in memory only, so a restart of the server clears it.  now is CONN.H's
 * tick count.
 */
#ifndef THROTL_H
#define THROTL_H

#include "COMPAT.H"

#define THR_MAX         8       /* entries: users and the overflow      */
#define THR_FAILS       5       /* failed logins that start a refusal   */
#define THR_SPAN        ((u32)30 * TMR_10SEC)   /* ticks: 5 minutes     */
#define THR_HOLD        ((u32)30 * TMR_10SEC)   /* ticks: 5 minutes     */

/* Open the table, empty: 0, or 1 when there is no memory or it is open
   already. */
int thr_open(void);

/* Free the table; nothing when it is not open. */
void thr_close(void);

/* proto_tickets.refused and .note (PROTO.H), for the server's login
   table (TICKET.H); while the table is closed nobody is refused and
   nothing is noted. */
int thr_refused(u32 now, const char *user);
void thr_note(u32 now, const char *user, int ok);

#endif
