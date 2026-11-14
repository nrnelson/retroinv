/*
 * PAGERSV.H - the survey of a pager's list and the jump into it
 * (PAGERSV.C), for a list window that shows its length and its place in
 * it.
 *
 * The protocol tells no list's length, only whether a page follows, so
 * a survey counts the rows itself: from the first page to the end, with
 * key-only requests, keeping each page's start cursor.  A jump then
 * loads the page holding any row counted, in one fill, with the rows
 * above it known.  Both work on an open pager (PAGER.H; a file that
 * includes this header defines PAGER_CALLS first) through its own
 * request and page load (PAGERINT.H), so their requests and pages are
 * those of the pager's moves.
 *
 * Pages.  The survey cuts the list as the pager does (PAGERINT.H, the
 * jump's rule): page k holds the rows k * ps to k * ps + ps - 1 of the
 * pager's numbering, a lead row (pg_lead, ps > 1) row 0 of page 0, so
 * page 0 holds ps - 1 rows of the list then and every other page ps.
 * Every count here is in that numbering, the lead row included.
 *
 * Requests.  The survey fetches each page with pg_fetch (PAGERINT.H), p->walk
 * 1 and p->walked the rows counted before the page, so its requests ask for
 * src->walkfields when the source has them and its rows are counted, not kept:
 * PAGE= the rows the page still misses, at most LINK_PAGEMAX, and AFTER= the
 * cursor the request before ended on (none for the first page's first
 * request).  A page ends once it holds ps rows or a response has no C line,
 * which ends the list; a request that returns fewer rows than it asked for but
 * a C line is followed by another for the same page.  The cursor after a full
 * page's last request starts the next page, kept as it, unless that page
 * proves to hold no rows and end the list (a filtered list's C line after
 * its last full page): such a start is dropped again, as PG_END shows no
 * last page of none, so every kept page but an empty list's page 0 holds
 * rows.  Once PSV_MAXPAGES pages are kept and the last ended with a C line,
 * the survey stops there, all 0, without a request to learn whether the list
 * ends.  An unpaged pager's survey (pg_unpaged, PAGER.H) sends the command
 * bare, one request a page.
 *
 * Cost.  A page takes ceil(ps / LINK_PAGEMAX) requests, so a list of r rows
 * takes about (r / ps) * ceil(ps / LINK_PAGEMAX), more where the server stops
 * early (a filtered list's bounded scans); an unpaged list takes one request
 * a page, each reading the whole list (PAGER.H).  The cursors take one far
 * block of PSV_MAXPAGES * LV_CURSZ bytes, at most CMEM_MAXBLK, held from the
 * first survey until psv_free.  A list longer than PSV_MAXPAGES pages is
 * surveyed only so far.  Like PAGERINT.H's functions, the survey exists only
 * where the pager keeps above (PAGER_ABOVE of PAGER.H): the DOS programs
 * neither link nor declare it.
 */
#ifndef PAGERSV_H
#define PAGERSV_H

#include "COMPAT.H"
#include "PAGER.H"
#include "KEYSET.H"

#ifdef PAGER_ABOVE
/* The page starts a survey keeps: one far block of LV_CURSZ each. */
#define PSV_MAXPAGES    (CMEM_MAXBLK / LV_CURSZ)

/* A survey, held by its caller (a list window holds one), who reads
   rows, pages and all, and changes no member but these: rows and all to
   a count it knows itself (the records it added or deleted, a count
   carried over from a list it replaces), pages to 0 whenever the page
   starts kept no longer begin pages of the list as it is. */
typedef struct {
    cmem_blk *cur;              /* PSV_MAXPAGES cursors of LV_CURSZ
                                   bytes, page k's start at k *
                                   LV_CURSZ ("" for page 0), or NULL
                                   before the first survey              */
    u32  rows;                  /* rows counted                         */
    u16  pages;                 /* page starts kept, 0 when nothing was
                                   surveyed                             */
    u8   all;                   /* 1 when rows is the whole list's      */
} psv;

/* Make s a survey of nothing, holding no block. */
void psv_init(psv *s);

/* Free the block of s and leave it as psv_init does. */
void psv_free(psv *s);

/* Survey the list of p from its first page, as the header says, for the
   order p has in use: s's rows, pages and all start at 0, and page 0's
   start, "", is kept before the first request.  rows counts the lead
   row, when p shows one, and every row received.  The cursor that
   starts page PSV_MAXPAGES ends the survey, with all 0.  Before every
   request but the first (pg_fetch between a page's requests, the survey
   before each later page) the wait function gets 1 and rows; when it
   returns 1 the survey ends there, with p->stopped 1 (0 otherwise, as
   pg_move leaves it).  A failed request ends the survey, its response
   given to the fail function once, as a move's is, a bad cursor
   (RQ_ECURSOR) as any other failure.  A survey stopped or failed leaves
   rows, pages and all 0, so no page start of a list half seen is ever
   jumped to.  all is 1 when a response without a C line ended the
   list.  Without memory for the block, nothing is sent, rows, pages and
   all stay 0 and RQ_OK is returned.  Nothing the pager shows changes:
   its rows held, ring, window, selection, above and order; p->walk is 0
   again after, and p->walked, which only the End walk reads, is left as
   the survey leaves it.  keys, when not NULL, is emptied first and
   collects the key of every row counted but the lead row, in the list's
   order (KEYSET.H), through the pager's key sink, which is NULL again
   after.  A survey stopped, failed or without memory for the block
   leaves keys empty; one that ends leaves over 1 also when it stopped
   at PSV_MAXPAGES pages, short of the list's end.
   Returns RQ_OK or the failed request's code. */
int psv_survey(psv *s, pg *p, ks_set *keys);

/* Load page k of the pages s kept (pages > 0) into p, k = r / p->ps,
   or the last kept when r lies beyond them: pg_load (PAGERINT.H) from
   its start with above k * ps, the window and the selection on its first
   row, then the starts of pages k - LV_MAXBACK (0 at least) to k - 1
   pushed on the ring, oldest first, so that moves up fetch them; p's
   stopped as a move leaves it, the wait function able to stop the fill.
   A failure goes to the fail function and leaves p as pg_load leaves
   it; after a bad cursor (RQ_ECURSOR) s's rows, pages and all are 0, as
   the list changed under it, and when the fail function returned 1 the
   pager starts again at the first page (pg_move's PG_HOME, whose
   failure goes to the fail function too).  Returns RQ_OK, also after a
   bad cursor and a restart that succeeded, as pg_move does, or the code
   of the last failed request. */
int psv_jump(psv *s, pg *p, u32 r);
#endif

#endif
