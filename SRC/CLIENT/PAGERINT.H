/*
 * PAGERINT.H - the pager's own requests and page loads (PAGER.C), for a
 * module that surveys a list or jumps into it.
 *
 * A list's survey counts its rows and keeps its pages' start cursors,
 * one key-only page after another; a page jump then loads the page one
 * of those cursors starts, with the rows above it known.  Both work on
 * an open pager (PAGER.H; a file that includes this header defines
 * PAGER_CALLS first, as PAGER.C does) and through the functions the
 * pager's own moves use, so a survey's requests and a jump's page are
 * those of the moves.  Such a module also reads p->leadtext, which is
 * not NULL while the pager has a lead row (pg_lead).  The functions
 * exist only where the pager keeps above (PAGER_ABOVE of PAGER.H); the
 * DOS programs have none.
 *
 * The jump's rule.  A jump keeps the pager's count and its moves up
 * exact only on the pager's own page starts: the start of the page it
 * loads and the cursors it pushes lie ps rows apart, but the first page
 * holds ps - 1 list rows when a lead row is shown (pg_lead, ps > 1), and
 * the newest pushed cursor's page ends exactly at the loaded page's
 * start.  A survey that fetches its pages with pg_fetch cuts them so.
 * Otherwise the first move up finds the page above A ending elsewhere,
 * as when the list changed: that page is shown alone and above is
 * PG_NOABOVE.
 */
#ifndef PAGERINT_H
#define PAGERINT_H

#include "COMPAT.H"
#include "RQ.H"
#include "PAGER.H"

#ifdef PAGER_ABOVE
/* Fill the page of p that starts at cursor start ("" for the first page)
   into B's half of the rows' block, the first have of its rows already
   there, with as many requests as it takes, each asking for the rows
   still missing, LINK_PAGEMAX at most.  *n receives the rows of the page,
   next (LV_CURSZ bytes, which may be start itself) the cursor after them
   and *more whether there is one (also after a failure, for the rows of
   the requests that succeeded).  Between requests the wait function gets
   the progress and may stop the fill, which sets p->stopped.  The first
   page (start "", have 0) begins with the lead row (pg_lead) when the
   pager has one and its window more than one row.  While p->walk is 1
   the rows, the lead row included, are counted, not kept, so the rows
   held stay as they were; the requests ask for src->walkfields when the
   source has them and the progress counts from p->walked: a survey sets
   walk and walked around its pages, as the End walk does.
   Returns RQ_OK or the failed request's code, with res holding the
   response. */
int pg_fetch(pg *p, rq_res *res, const char *start, u16 have, u16 *n,
             char *next, u8 *more);

/* Load page A of p from cursor start ("" for the first page), one of the
   pager's own page starts (the jump's rule), as PG_HOME loads the first
   page: B dropped, the ring emptied, the window and the selection on its
   first row and above set to the given count, the rows before start.
   The wait function may stop the fill, which sets p->stopped (the caller
   clears it first, as pg_move does).  A failure leaves A as a failed
   fetch does (PAGER.H) and the rest as it was.  Returns RQ_OK or the
   failed request's code, with res holding the response, the fail
   function not called. */
int pg_load(pg *p, rq_res *res, const char *start, u32 above);

/* Push cur on p's ring as its newest cursor: after pg_load, the start
   cursors of the pages before A, oldest first, each one the pager's own
   page before the next (the jump's rule), so that going up from A
   fetches them.
   When the ring is full the oldest cursor is dropped first.  A cursor
   that then does not fit a block of LV_RINGSZ moves the ring to one of
   LV_RINGMAX, when there is the memory, else the oldest cursors are
   dropped until it fits.  Without memory for the ring's first block cur
   is dropped: the ring holds none until a later push finds the memory. */
void pg_ring_push(pg *p, const char *cur);
#endif

#endif
