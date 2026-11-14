/*
 * PAGER.H - paging one list command through the protocol, for any
 * client: the rows of a window and a selection over them.
 *
 * A pager holds the records of one paged command (ITEM LIST, LOC LIST,
 * LOAN LIST, MAINT RECENT, ...), one record per row, for a window of ps
 * rows (at most LV_MAXPAGE), and moves a selection over them.  Drawing,
 * keys and the user's texts are the caller's: LISTV.C draws a pager in
 * the full-screen client.  The server keeps no state between requests and
 * pages only forward, so the pager keeps:
 *
 *   - two adjacent pages of rows, A and B, in one far block of 2 * ps
 *     rows of LV_ROWBYTES bytes; the window shows rows from A and, once
 *     scrolled past A's end, from B;
 *   - the cursor at which each of A and B starts ("" for the first
 *     page) and the cursor after B, from the response's C line;
 *   - a ring of up to LV_MAXBACK page start cursors before A, in a
 *     second far block, so a move up can fetch the pages above A again.
 *     The cursors are packed, each taking its length: the block holds
 *     LV_RINGSZ bytes, which fit LV_MAXBACK cursors of the lists whose
 *     keys are IDs, until a cursor does not fit; the ring then moves to
 *     a block of LV_RINGMAX, which fits LV_MAXBACK of any length, when
 *     there is the memory.  When the ring is full, or the block has no
 *     room left, the oldest cursors are dropped; going up past them
 *     starts again at the first page, as PG_HOME does.  The block is
 *     allocated when the first cursor goes on the ring; while there is
 *     no memory for it the ring holds none until a later push finds the
 *     memory, and going up past A starts again at the first page too.
 *
 * Rows are numbered from 0 across A and then B (pg_rows of them); the
 * window starts at the pager's row first and the selection is row sel.
 * After every move first <= sel < first + ps while rows are held, and
 * both are 0 when none are.
 *
 * Requests.  Every request of a pager is src->cmd, then what src->args
 * adds (when not NULL), then ORDER= the order in use (when the source
 * has orders), FIELDS= the order's field list (when not NULL), PAGE=
 * the rows the page still misses, at most LINK_PAGEMAX, and AFTER= the
 * cursor to go on from (left out for the first page).  A request may
 * return fewer rows than asked for, or none, and still have a C line,
 * because the server stops after a bounded amount of work; the pager
 * then asks again from that cursor, as it does after LINK_PAGEMAX rows
 * of a longer page, until the page is full or the list ends.  It never
 * asks for more rows than it can keep, so the cursor after a page
 * always follows the page's last row exactly.  A fill the wait function
 * stopped leaves its page short: a short B is filled on later with the
 * rows it misses, while A stays short and the page after it is filled
 * from its start.
 *
 * Waiting.  Between two requests of one page, and before each request
 * of the End walk, the pager calls the wait function with the progress;
 * when it returns 1 the move stops there: a fill keeps the rows it has
 * and the cursor after them, so a move down goes on from there, and the
 * End walk fetches the page it has reached (PG_END); the pager's stopped
 * is 1 after it.
 *
 * Failures.  A failed request ends the move and its response goes to
 * the fail function.  A failed fetch of the first page held (Home, a
 * reload, End, going up) leaves that page as it was, but the second
 * page's room serves the fetch, so the second page is dropped: a
 * selection that was in it goes to the first page's last row, and the
 * window is kept within the first page's rows.  A failed fetch of the
 * second page keeps the rows of its requests that succeeded.  A bad
 * cursor (RQ_ECURSOR, e.g. after an index was rebuilt) makes the pager
 * start again at the first page, when the fail function says to stay,
 * and leaves it as PG_HOME does.
 *
 * Rows above.  Where PAGER_ABOVE is defined (below: the Windows client
 * and the host builds, not the DOS programs, whose memory floors leave
 * no room for it), the pager counts the rows of the list before A's
 * first row, above, a lead row included, from its own moves alone: 0
 * whenever it loads the first page; grown by A's rows when B becomes A;
 * shrunk, not below 0, by the rows of a page fetched above A, or before
 * a page of a reload whose rows have gone; set by the End walk from the
 * rows it passed.  A failed move leaves it as it was.  A page fetched above A
 * that no longer ends where A started (the list changed, or the page
 * was a stopped fill's, short) makes it PG_NOABOVE, which no move but a
 * load of the first page changes; a reload whose page before is not
 * known to end there does the same.  Rows other users add or delete
 * before A are counted only when the first page is loaded again.
 *
 * Unpaged lists.  Where PAGER_ABOVE is defined, a pager may be made
 * unpaged (pg_unpaged) for a command that takes no PAGE= or AFTER= and
 * returns the whole list, at most LINK_MAXCODES rows (CAT LIST, LTYPE
 * LIST): every request leaves both out and the pager pages the response
 * itself.  Its cursor is the decimal number of rows before the page (""
 * for the first page): a fill skips that many R lines, keeps the rows the
 * page still misses, and ends with the cursor of the row after its last
 * and, while the list has that row, a page to follow.  Every fill takes
 * one request, which reads the whole list again.  An offset cannot follow
 * a list that changes under it: a row added or deleted before a page
 * shifts it, so a row may show twice or be missed where A meets B until
 * the first page is loaded again.  That is accepted for the short,
 * rarely edited code lists these commands return.
 *
 * Each row is LV_KEYSZ bytes of key (the record's ID or code, NUL
 * terminated, which the caller uses to act on the selection; PAGER.C
 * checks at compile time that LINK_IDLEN, LINK_CATLEN and LINK_USERLEN
 * all fit; loan, service record and location IDs are LINK_IDLEN digits
 * like item IDs) and LV_ROWTEXT bytes of display text, both
 * made by the source's format function from the R line, and one byte
 * holding the result of src->mark (0 without it).  Rows are text once
 * fetched: a reload (after an edit, a delete or a sort change) fetches
 * them again.
 */
#ifndef PAGER_H
#define PAGER_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"

/* The pager keeps above, and PAGERINT.H serves a list's survey; its
   rows are wider than the DOS screen's (LV_ROWTEXT). */
#if defined(COMPAT_WIN16) || defined(COMPAT_HOST)
#define PAGER_ABOVE
#define LV_ROWTEXT      160
#endif

/* ---------------------------------------------------------------------
 * Rows and their source.
 * --------------------------------------------------------------------- */
/* The most rows of a pager's window, and so of a page: room for a
   maximized list window on the displays Windows 3.0 drives. */
#define LV_MAXPAGE      64
#define LV_KEYSZ        (LINK_IDLEN + 1)        /* key bytes of a row   */
/* LV_TEXTLEN is the display bytes of a row as the DOS client shows it,
   the screen's width, which the shared format functions fill; the
   pager keeps LV_ROWTEXT of a row, LV_TEXTLEN unless set above. */
#define LV_TEXTLEN      78
#ifndef LV_ROWTEXT
#define LV_ROWTEXT      LV_TEXTLEN
#endif
#define LV_ROWBYTES     (LV_KEYSZ + LV_ROWTEXT + 1) /* last byte: mark  */
#define LV_MAXBACK      32      /* page start cursors kept for going up */
#define LV_CURSZ        (LINK_MAXCURSOR + 1)    /* one stored cursor    */
/* The ring's blocks: the first holds LV_MAXBACK cursors as long as one
   whose key is two IDs (an index name of up to six characters, ':', 2 *
   2 * LINK_IDLEN hex digits, ':', a record number of up to seven hex
   digits and the NUL); the larger, LV_MAXBACK of any length. */
#define LV_RINGSZ       (LV_MAXBACK * (6 + 1 + 4 * LINK_IDLEN + 1 + 7 + 1))
#define LV_RINGMAX      (LV_MAXBACK * LV_CURSZ)
#define LV_MAXFIELDS    RQ_MAXVALS      /* fields of the F line kept    */

/* One R line as the format function sees it: n values, and the field
   names of the F line before it in the same order.  The F line's first
   LV_MAXFIELDS names are kept, each cut to LINK_NAMELEN characters, and
   an R line's values beyond them are not passed. */
typedef struct {
    u16  n;
    char **names;
    char **vals;
} lv_rec;

/* The value of field name in r, or "" when the line has no such field. */
const char *lv_val(const lv_rec *r, const char *name);

/* Add the command's own parts to a request after its command words:
   its positional words first (a code, a search text), then its filter
   pairs. */
typedef void (*lv_args_fn)(void *arg, rq *q);

/* Make a row from r: text of LV_ROWTEXT bytes (padded with spaces by
   the pager after the NUL the function writes) and key of at most
   LV_KEYSZ - 1 characters.  text starts as "" and key as LV_KEYSZ NUL
   bytes. */
typedef void (*lv_fmt_fn)(void *arg, const lv_rec *r, char *text,
                          char *key);

/* Helpers for format functions.  lv_col appends s to text t (LV_ROWTEXT
   + 1 bytes) at *at in w cells, leading spaces dropped, cut or padded,
   right-aligned with LV_RIGHT in flags (numbers), its last cell
   MSG_CUTMARK when cut with LV_MARK (names), then one space while there
   is room; a column that would pass LV_ROWTEXT is left out.  lv_datecol
   appends a wire date (YYYYMMDD) as YYYY-MM-DD in MSG_DATEW cells,
   blank when wire is not a date (msg_date). */
#define LV_RIGHT        0x01
#define LV_MARK         0x02
void lv_col(char *t, u16 *at, const char *s, u16 w, u8 flags);
void lv_datecol(char *t, u16 *at, const char *wire);

/* 1 to mark the row (LISTV.C draws it TA_ALERT when it is not
   selected), else 0. */
typedef int (*lv_mark_fn)(void *arg, const lv_rec *r);

/* 1 when the row of key and display text (LV_ROWTEXT characters, as
   the format function made it) is the one a seek looks for. */
typedef int (*lv_match_fn)(void *arg, const char *key, const char *text);

/* Sort orders a source offers, at most LV_MAXORDERS.  Each is sent as
   ORDER=name but LV_TREEORDER, the location tree's order, which is
   LOC=0 ALL=1: the items of no location, then those of each location
   depth first. */
#define LV_MAXORDERS    4
#define LV_TREEORDER    "LOC"

/* The most rows a seek of the screens looks through: a letter's jump to
   the next name starting with it, a row sought after a change. */
#define LV_SEEKMAX      500

typedef struct {
    const char *cmd;            /* command words, e.g. "ITEM LIST"      */
    u8   norders;               /* 0: no ORDER=, fields[0] applies      */
    const char *orders[LV_MAXORDERS];   /* ORDER= values ("ID", "LOC",
                                   "MFR", "SERIAL")                     */
    const char *fields[LV_MAXORDERS];   /* FIELDS= value per order
                                   (e.g. "ITEM_ID,MFR,MODEL,SERIALNO,
                                   LOC_NAME" for SERIAL), or NULL for
                                   the command's default list           */
    u16  header[LV_MAXORDERS];  /* message number of the column header
                                   per order (for the caller)           */
    u16  empty;                 /* message shown when there are no rows
                                   (for the caller)                     */
    const char *walkfields;     /* FIELDS= of the key field alone, for
                                   the End walk, or NULL                */
    lv_args_fn args;            /* may be NULL                          */
    lv_fmt_fn  fmt;             /* knows the field lists, by order      */
    void *arg;                  /* passed to args and fmt               */
    lv_mark_fn mark;            /* may be NULL: no row is marked; gets
                                   arg like args and fmt                */
} lv_src;

/* ---------------------------------------------------------------------
 * The pager.
 *
 * Only a file that defines PAGER_CALLS before it includes this header
 * sees this section: PAGER.C, the list views (LISTV.C, and
 * SRC/WIN/WLIST.C for Windows, with its owners SRC/WIN/INVWIN.C,
 * SRC/WIN/WPICK.C and TEST/WLISTT.C, which name its moves and results),
 * the survey PAGERSV.C and the tests of the pager and of the survey
 * (T_PGSV.C).  The screens see the rows and their source through
 * LISTV.H, as the tests of the list view and of the screens (T_LISTV.C,
 * T_SCRR.C) would have almost no Watcom C 6.5 compiler memory left if
 * they saw more.
 * --------------------------------------------------------------------- */
#ifdef PAGER_CALLS
/* Progress, and the user's wish to stop: n is the rows the End walk has
   passed when walk is 1, else the rows of the page being filled.  1
   stops the move. */
typedef int (*pg_wait_fn)(void *arg, int walk, u32 n);

/* Report the failed request whose response is res: 1 when the caller
   stays on the list, 0 when it leaves it.  After RQ_ECURSOR the pager
   starts again at the first page only on 1, and reports a failure of
   that fetch too, so a move makes at most two calls. */
typedef int (*pg_fail_fn)(void *arg, const rq_res *res);

#ifdef PAGER_ABOVE
/* above while the rows before page A are not known. */
#define PG_NOABOVE      0xFFFFFFFFUL
#endif

/* A pager, held by its caller (a list view holds one).  The caller
   reads first, sel, order, stopped, ps, above (with PAGER_ABOVE) and
   src, and changes no member; the others are the pager's own. */
typedef struct {
    u16  first;                 /* row at the window's top              */
    u16  sel;                   /* selected row                         */
    u8   order;                 /* index of src->orders in use (0
                                   without orders), for the format
                                   function                             */
    u8   stopped;               /* 1 when the wait function stopped the
                                   last move, which then failed in no
                                   request                              */
    u8   ps;                    /* rows of a page and of the window     */
    u8   amore;                 /* a page follows A, starting at curb   */
    u8   bload;                 /* B holds that page                    */
    u8   bmore;                 /* a page follows B, starting at curn   */
    u8   ringn;                 /* cursors in the ring                  */
    u8   nf;                    /* field names of the last F line       */
    u8   walk;                  /* the End walk is running              */
    u16  na;                    /* rows in A                            */
    u16  nb;                    /* rows in B (when bload)               */
    u16  ringb;                 /* bytes of the ring's cursors          */
    u32  walked;                /* rows the End walk has passed         */
#ifdef PAGER_ABOVE
    u32  above;                 /* rows of the list before A's first
                                   row, or PG_NOABOVE                   */
    /* The key sink: while walk is 1 and keyfn is not NULL, each row the
       walk counts, the lead row excepted, is formatted as a kept row is
       and its key, LV_KEYSZ bytes, handed to keyfn with karg.  Set by a
       survey around its pages (PAGERSV.H); NULL from pg_open. */
    void (*keyfn)(void *karg, const char *key);
    void *karg;
    u8   unpaged;               /* 1 after pg_unpaged                   */
#endif
    link *l;
    const lv_src *src;
    pg_wait_fn wait;
    pg_fail_fn fail;
    void *arg;                  /* passed to wait and fail              */
    cmem_blk *rows;             /* 2 * ps rows of LV_ROWBYTES           */
    cmem_blk *ring;             /* LV_RINGSZ or LV_RINGMAX bytes, NULL
                                   until the first push                 */
    char cura[LV_CURSZ];        /* where A starts ("" first page)       */
    char curb[LV_CURSZ];        /* where B starts                       */
    char curn[LV_CURSZ];        /* the cursor after B                   */
    const char *leadkey;        /* the lead row (pg_lead), or NULL      */
    const char *leadtext;
    char fn[LV_MAXFIELDS][LINK_NAMELEN + 1];
    char *fnp[LV_MAXFIELDS];    /* fn's names, as lv_rec.names          */
} pg;

/* Results of pg_open. */
#define PG_OK           0
#define PG_EARG         1       /* ps 0 or over LV_MAXPAGE, or more than
                                   LV_MAXORDERS orders                  */
#define PG_ENOMEM       2       /* no memory for its rows               */

/* Open p as a pager over src on link l, for a window of ps rows; wait
   and fail get arg.  The first order is in use, no rows are held and
   nothing is fetched yet.  Returns PG_OK, or PG_EARG or PG_ENOMEM with
   nothing held, so p is not to be closed. */
int pg_open(pg *p, link *l, const lv_src *src, u8 ps, pg_wait_fn wait,
            pg_fail_fn fail, void *arg);

/* Give p a lead row: a row of key and text that the first page holds before
   the command's rows (a pick list's "no location" choice), so that a list
   without rows still has it; a window of one row has none.  The pager keeps
   both pointers, which must stay valid while it is open; text is cut to
   LV_ROWTEXT and key to LV_KEYSZ - 1 characters.  Set it before the first
   move. */
void pg_lead(pg *p, const char *key, const char *text);

#ifdef PAGER_ABOVE
/* Make p page an unpaged command's response itself (Unpaged lists,
   above).  Set it before the first move. */
void pg_unpaged(pg *p);
#endif

/* Free the far blocks of p, opened with PG_OK. */
void pg_close(pg *p);

/* Moves of pg_move. */
#define PG_HOME         0
#define PG_END          1
#define PG_UP           2
#define PG_DOWN         3
#define PG_PGUP         4
#define PG_PGDN         5
#define PG_RELOAD       6
#define PG_ORDER        7

/* Make move m, one of the PG_ moves.  Each fetches what it needs and
   returns RQ_OK or the code of the failed request, already given to the
   fail function; after a bad cursor and a restart at the first page
   that succeeded, RQ_OK.

   PG_HOME goes to the first page (also the first load), the selection
   on its first row and the ring emptied.  PG_DOWN moves the selection
   one row down and the window with it when the selection leaves it; at
   the last row it stays.  On a list that ended without rows (a stop, or
   nothing found yet) it fetches on, and the first row found is
   selected.  PG_UP moves the selection one row up, fetching the page
   above A when the window is at A's top; at the first row it stays.
   PG_PGDN moves the window and the selection a window's height down,
   the selection stopping on the last row and the window once it ends
   on that row; PG_PGUP a window's height up, the selection stopping on
   the first row.  When the window lies wholly in B after a move, B
   becomes A, A's start cursor goes on the ring and the page after it is
   fetched into B when the window needs it.

   PG_END walks forward page by page from the page after A (a B held is
   fetched again) to the last one (lists have no backward cursor),
   pushing each start cursor on the ring; the walk asks for
   src->walkfields when the source has them, to keep each response short,
   and counts its rows without calling the format function.  The last
   page (or the page a stop left it on) is then fetched with the order's
   field list; when the last page holds fewer rows than the window, the
   page before it is fetched too, so the window ends on the last row,
   which is selected (a list shorter than the window shows from its first
   row).  Without memory for the ring a short last page is shown alone,
   its rows at the window's top, while a last page of none shows the
   page before it, from the walk's own copy of its cursor.  A failed walk
   leaves the ring as it was, but for the cursors the walk's pushes
   dropped.  When the rows held already end the list, PG_END moves the
   window to the last row; a short page alone gets the page before it
   from the ring, and without one stays alone.

   PG_RELOAD fetches the pages shown again from their start cursors,
   keeping the selection on the same row number when the list still has
   it.  When the rows fetched end the list, the window ends on the last
   row, as PG_END leaves it: rows held above it fill the window, and a
   page alone that is shorter than the window gets the page before it
   fetched too, when the pager has that page's cursor.

   PG_ORDER selects the next of src->orders (wrapping; none without
   orders) and goes to the first page. */
int pg_move(pg *p, int m);

/* Move the selection down row by row, going on at the first row after
   the last, until match is 1 for the selected row, the
   row the seek started on comes round again, max rows have been
   passed, a fetch fails or the wait function stops one.  match is
   called on every row reached, the start row included when the walk
   comes round to it, and not on the start row before the first move.
   An empty list stays as it is.  Returns as a move does. */
int pg_seek(pg *p, lv_match_fn match, void *arg, u16 max);

/* Rows held, in A and B together. */
u16 pg_rows(pg *p);

/* 1 when no page follows the rows held: the list ends with them.  It is
   the server's word, 0 while the response that ended the last page held
   had a C line, even when the page it leads to proves empty: after a
   filtered list's full last page a move down at its last row takes at
   least one more request to learn that the list ends. */
int pg_at_end(pg *p);

/* Row i's display text (i < pg_rows) into text, LV_ROWTEXT + 1 bytes,
   NUL-terminated; its mark, 1 or 0. */
int pg_text(pg *p, u16 i, char *text);

/* The selected row's key into key (LV_KEYSZ bytes) and 1, or 0 with
   key "" when no rows are held. */
int pg_selected(pg *p, char *key);

/* The ORDER= value in use, or "" without orders. */
const char *pg_order_name(pg *p);

/* Select the sort order name (one of src->orders) without fetching;
   0 if it is not one of them. */
int pg_set_order(pg *p, const char *name);
#endif

#endif
