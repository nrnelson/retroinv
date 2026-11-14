/*
 * TRLINK.H - a recording LOCAL link for the client tests (TRLINK.C).
 *
 * TRLINK.C implements link_open_local, link_send, link_close and
 * link_netname of LINK.H itself, in place of SRC/CLIENT/LINK.C and
 * LINKLOC.C (link_feed is left out): the engine and the protocol
 * dispatcher run in the process, as with the LOCAL link, and every
 * request line sent is also appended to a log, so a test can check the
 * exact requests a screen module makes.  A link may be given the NetBIOS
 * name a NET link would call from (tl_netname).  One chosen request can
 * be answered with response lines of the test's own instead of being
 * dispatched, to make a request fail, or bring an answer the engine
 * would not give here, at a given point, or can end the session, lose
 * the link or find a fresh session.  Kept apart from the tests so that
 * they need no engine or protocol header, which a period compiler has no
 * room for beside the client's.
 *
 * Delivery is LINK.C's: once the line function returns non-zero it is
 * not called again for that request, and link_send returns LINK_EOUTPUT.
 * What still differs from LINK.C and LINKLOC.C:
 * - dir is passed to the engine as it is: no separator is added, so the
 *   tests pass a prefix that ends in one; a name too long gets the
 *   engine's text (naming the directory), and a missing directory no
 *   "no such directory" text;
 * - why is not cleared first and is set only by eng_open: a failure to
 *   enlarge the handle table or to allocate the link leaves it as it was;
 * - a line longer than LINK_MAXLINE is dispatched, not answered with
 *   103;
 * - a response line longer than LINK_MAXRESP is cut there, not delivered
 *   in pieces, and a last response line without its LF is not delivered
 *   at all (the dispatcher ends every line with one);
 * - link_close frees the link without wiping it first.
 */
#ifndef TRLINK_H
#define TRLINK_H

#include "COMPAT.H"

#define TL_LOGMAX       2048    /* bytes of request lines logged        */

/* Empty the log, count requests from 0 again, and cancel tl_fail. */
void tl_clear(void);

/* Requests sent since tl_clear. */
u16 tl_count(void);

/* 1 when a request sent since tl_clear did not fit the log (it is
   counted by tl_count but missing from tl_log and tl_req), else 0. */
int tl_full(void);

/* Every request logged, each ended by '\n'; valid until the next
   tl_req. */
const char *tl_log(void);

/* Request i (from 0) of the log, or "" when there is none: the log
   itself, cut at the request's end until the next call of tl_log,
   tl_req or tl_clear. */
const char *tl_req(u16 i);

/* Answer request n (counted as tl_count counts it, from 1) with the
   response resp, which must stay valid: one line, or several each ended
   by '\n' but the last (a +DATA answer, its "." line last); 0 cancels.
   A '\n' after the last line too ends the session once it is answered,
   as a server that answered and then went away: request n and every
   later one get LINK_CLOSED.  A NULL resp ends the session there
   instead: request n is not answered.  tl_lost loses the link there,
   as a NET session the server hung up: request n and every later one
   on the link get LINK_ELOST.  tl_gone dispatches request n, then loses
   the link before its answer, as a connection that breaks once the
   server has acted: request n and every later one get LINK_ELOST.
   tl_reset dispatches request n in a fresh session, as a server's after
   its IDLE= ended the session or after a restart, which a serial line's
   next request finds.  Only a file that defines TL_ENDS before the
   include sees those three, for Watcom C 6.5's compiler memory. */
void tl_fail(u16 n, const char *resp);
#ifdef TL_ENDS
extern const char tl_lost[];
extern const char tl_reset[];
extern const char tl_gone[];

/* The NetBIOS name that the links opened from now on call from, as
   link_netname reports it, which must stay valid; NULL, the start, for
   none, as on a LOCAL link. */
void tl_netname(const char *name);
#endif

/* 1 while link_send is delivering a response to the line function (or
   dispatching the request that makes it), 0 otherwise, so a test can
   tell whether code it calls back, a printer's wait function for one,
   runs while a response is being read. */
int tl_reading(void);

/* When not NULL, called with each request line (len bytes, no NUL
   after them) as it is sent, before it is answered, so a test can look
   at what the screen has drawn (and flushed) meanwhile. */
extern void (*tl_hook)(const char *line, u16 len);

#endif
