/*
 * T_PAGER.C - the pager (SRC/CLIENT/PAGER.C) over the engine.
 *
 * usage: T_PAGER dir
 *
 * dir is a scratch data directory prefix ("OBJ/HOST/PGDATA/") holding
 * nothing but this test's files; its engine files are removed first and
 * last.  The test links the recording link of TRLINK.H instead of
 * LINK.C, so the request lines the pager sends are checked exactly, and
 * a chosen request can be answered with a response of the test's own.
 * It is written from the contract in PAGER.H and knows nothing of how
 * the pager keeps its pages.
 *
 * Rows are items listed in ID order, so row i of the list holds the
 * item numbered i + 1 and the pager's rows can be checked against a
 * simple model: the numbers in the rows held run one after the other,
 * and the selection and the window top are items of known numbers.
 *
 * Covered:
 * - pg_open (result codes, the far block, nothing held on a failure);
 * - Home, Down and Up across the boundary of the two pages, PgDn and PgUp,
 *   the ring of start cursors and going up past its depth;
 * - the ring's far block, taken by the first push, once, and going up
 *   past A without it; its cursors packed, short ones in its first
 *   block, long ones moving it to its larger block, or without memory
 *   for that fewer pages up, also after a failed End; Home and a new
 *   order keeping it; made-up cursors filling the first block exactly,
 *   passing its end by a byte, and of LINK_MAXCURSOR characters;
 * - End (the walk with the key field alone, a last page shorter than the
 *   window, a list shorter than a page, the ring filling up);
 * - Reload (keeping the row, a list that has shrunk);
 * - sort orders and the exact request lines, a page filled by several
 *   requests;
 * - the wait function (calls, progress, a stop in a fill and in the End
 *   walk, a stopped walk whose last fetch fails, which reports no stop,
 *   and a seek after a stop);
 * - the fail function (failures of each kind of fetch, the restart after a
 *   bad cursor, a failing restart);
 * - pg_seek, the lead row, the accessors, an empty list, a list of
 *   exactly one page and one of a row more;
 * - the rows above page A (above) after every kind of move, against the
 *   list's own order, and PG_NOABOVE after a list changed under a move
 *   up or a reload until a move loads the first page; a page jump of
 *   PAGERINT.H from a survey of the list;
 * - an unpaged pager (pg_unpaged) over CAT LIST: the bare request, one a
 *   fill, its pages cut from the response, PgDn, Down, PgUp through the
 *   ring, End, Reload, above, a window past LINK_PAGEMAX, a list ending
 *   with a full page, a seek over its pages and a lead row.
 */
/* cmem_freeing is declared under COMPAT_FIND, and only the host builds
   this test. */
#define COMPAT_FIND
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "TRLINK.H"
#include "TEWIPE.H"
#define TF_LINK
#include "TESTFW.H"
#define PAGER_CALLS
#include "PAGER.H"
#include "PAGERINT.H"

#define PATHMAX         96
#define NITEMS          250     /* items added                          */
#define NCARD           20      /* the first items are in CARD          */
#define FIND_EVERY      25      /* every n-th item matches ITEM FIND    */
#define MAXCUR          52      /* cursors kept by cursors_of           */
#define MAXW            64      /* wait calls recorded                  */
#define MAXF            4       /* fail calls recorded                  */

static char g_ddir[PATHMAX];
static int g_tag;               /* what the pager passes to its callbacks */

/* ---------------------------------------------------------------------
 * The data.
 * --------------------------------------------------------------------- */
static void add_items(void)
{
    char line[24];
    rq q;
    rq_res res;
    u16 i;

    for (i = 1; i <= NITEMS; i++) {
        rq_begin(&q, "ITEM ADD");
        rq_pair(&q, "CATEGORY", i <= NCARD ? "CARD" : "SYS");
        sprintf(line, "MK%u", (unsigned)(i % 7));
        rq_pair(&q, "MFR", line);
        if (i % FIND_EVERY == 0)
            sprintf(line, "ZQ%u", (unsigned)i);
        else
            sprintf(line, "X%u", (unsigned)i);
        rq_pair(&q, "MODEL", line);
        rq_pair(&q, "LOC_ID", "A1");
        if (!TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK))
            return;
    }
}

/* "ITEM <verb> <id>" for verb DEL or UNDEL. */
static void item_op(const char *verb, u16 id)
{
    char line[24];

    sprintf(line, "ITEM %s %u", verb, (unsigned)id);
    TF_EQ(tc_send(line), RQ_OK);
}

/* ---------------------------------------------------------------------
 * Sources.
 * --------------------------------------------------------------------- */
static pg *g_cur;               /* the pager under test                 */
static u16 g_nfmt;              /* calls of the format function         */
static u8 g_fmt_order;          /* order in use at the last call        */
static u16 g_nmark;             /* calls of the mark function           */

/* The key and the text are the item number; the text goes on with the
   manufacturer when the response has it. */
static void fmt_item(void *arg, const lv_rec *r, char *text, char *key)
{
    TF_CHECK(arg == (void *)&g_tag);
    g_nfmt++;
    g_fmt_order = g_cur->order;
    TF_CHECK(r->n >= 1);
    strcpy(key, lv_val(r, "ITEM_ID"));
    strcpy(text, lv_val(r, "ITEM_ID"));
    if (r->n > 1) {
        strcat(text, " ");
        strcat(text, lv_val(r, "MFR"));
    }
}

static int mark_odd(void *arg, const lv_rec *r)
{
    TF_CHECK(arg == (void *)&g_tag);
    g_nmark++;
    return (int)(strtoul(lv_val(r, "ITEM_ID"), NULL, 10) & 1);
}

static void card_args(void *arg, rq *q)
{
    TF_CHECK(arg == (void *)&g_tag);
    rq_pair(q, "CAT", "CARD");
}

static void none_args(void *arg, rq *q)
{
    TF_CHECK(arg == (void *)&g_tag);
    rq_pair(q, "CAT", "NONE");
}

static void find_args(void *arg, rq *q)
{
    TF_CHECK(arg == (void *)&g_tag);
    rq_word(q, "ZQ");
}

/* ITEM LIST in ID order or in manufacturer order. */
static const lv_src items_src = {
    "ITEM LIST", 2, { "ID", "MFR", NULL, NULL },
    { NULL, "ITEM_ID,MFR", NULL, NULL },
    { 0, 0, 0, 0 }, 0, "ITEM_ID",
    NULL, fmt_item, &g_tag, NULL
};

/* The same without a walk field. */
static const lv_src nowalk_src = {
    "ITEM LIST", 1, { "ID", NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, NULL,
    NULL, fmt_item, &g_tag, NULL
};

/* No orders: no ORDER=, and fields[0] applies. */
static const lv_src plain_src = {
    "ITEM LIST", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, "ITEM_ID",
    NULL, fmt_item, &g_tag, NULL
};

static const lv_src mark_src = {
    "ITEM LIST", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, NULL,
    NULL, fmt_item, &g_tag, mark_odd
};

/* The 20 items of CARD, with orders and arguments. */
static const lv_src card_src = {
    "ITEM LIST", 2, { "ID", "MFR", NULL, NULL },
    { NULL, "ITEM_ID,MFR", NULL, NULL },
    { 0, 0, 0, 0 }, 0, "ITEM_ID",
    card_args, fmt_item, &g_tag, NULL
};

static const lv_src none_src = {
    "ITEM LIST", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, NULL,
    none_args, fmt_item, &g_tag, NULL
};

/* The 10 items that match, every FIND_EVERY-th: with 250 items the
   server's bounded scan ends the first request before the page is
   full. */
static const lv_src find_src = {
    "ITEM FIND", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, NULL,
    find_args, fmt_item, &g_tag, NULL
};

static void mk1_args(void *arg, rq *q)
{
    TF_CHECK(arg == (void *)&g_tag);
    rq_word(q, "MK1");
}

/* The items of MK1, every seventh from the first: NMK1 of them. */
#define NMK1            ((NITEMS + 6) / 7)
static const lv_src mk1_src = {
    "ITEM FIND", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, NULL,
    mk1_args, fmt_item, &g_tag, NULL
};

static const lv_src four_src = {
    "ITEM LIST", 4, { "A", "B", "C", "D" },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, NULL,
    NULL, fmt_item, &g_tag, NULL
};

static const lv_src five_src = {
    "ITEM LIST", 5, { "A", "B", "C", "D" },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, NULL,
    NULL, fmt_item, &g_tag, NULL
};

/* CAT LIST, unpaged: the key and the text are the code. */
static void fmt_cat(void *arg, const lv_rec *r, char *text, char *key)
{
    TF_CHECK(arg == (void *)&g_tag);
    strcpy(key, lv_val(r, "CAT_CODE"));
    strcpy(text, lv_val(r, "CAT_CODE"));
}

static const lv_src cat_src = {
    "CAT LIST", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, NULL,
    NULL, fmt_cat, &g_tag, NULL
};

/* ---------------------------------------------------------------------
 * The pager's callbacks.
 * --------------------------------------------------------------------- */
static u16 g_nwait;             /* wait calls                           */
static int g_wwalk[MAXW];
static u32 g_wn[MAXW];
static u16 g_wstop;             /* the call that returns 1, 0 for none  */

static int wait_fn(void *arg, int walk, u32 n)
{
    TF_CHECK(arg == (void *)&g_tag);
    if (g_nwait < MAXW) {
        g_wwalk[g_nwait] = walk;
        g_wn[g_nwait] = n;
    }
    g_nwait++;
    return g_nwait == g_wstop;
}

static u16 g_nfail;             /* fail calls                           */
static int g_fcode[MAXF];
static char g_ftext[MAXF][RQ_TEXTLEN + 1];
static int g_fstay;             /* what the fail function answers       */

static int fail_fn(void *arg, const rq_res *res)
{
    TF_CHECK(arg == (void *)&g_tag);
    if (g_nfail < MAXF) {
        g_fcode[g_nfail] = res->code;
        strcpy(g_ftext[g_nfail], res->text);
    }
    g_nfail++;
    return g_fstay;
}

/* Responses the test gives in place of the engine's, by request number
   from 1, NULL to let the engine answer. */
#define MAXSCRIPT       8
#define BUSY            "-ERR 402 busy"
#define BADCUR          "-ERR 204 bad cursor"
static const char *g_script[MAXSCRIPT];

static void script_hook(const char *line, u16 len)
{
    u16 n;

    n = tl_count();
    TF_CHECK(len > 0 && line[0] != '\0');
    if (n >= 1 && n <= MAXSCRIPT && g_script[n - 1] != NULL)
        tl_fail(n, g_script[n - 1]);
}

/* Answer the next requests with r1, r2, r3 (NULL: the engine). */
static void script(const char *r1, const char *r2, const char *r3)
{
    u16 i;

    tl_clear();
    for (i = 0; i < MAXSCRIPT; i++)
        g_script[i] = NULL;
    g_script[0] = r1;
    g_script[1] = r2;
    g_script[2] = r3;
    tl_hook = script_hook;
}

static void unscript(void)
{
    tl_hook = NULL;
}

/* ---------------------------------------------------------------------
 * Checks.
 * --------------------------------------------------------------------- */
static u32 num_of(const char *s)
{
    return (u32)strtoul(s, NULL, 10);
}

/* The number of the selected row's key, 0 when none. */
static u32 key_id(pg *p)
{
    char key[LV_KEYSZ];

    if (!pg_selected(p, key))
        return 0;
    return num_of(key);
}

/* The number in row i's text. */
static u32 row_id(pg *p, u16 i)
{
    char t[LV_ROWTEXT + 1];

    pg_text(p, i, t);
    return num_of(t);
}

/* The selection is the item sel and the window starts at the item top;
   also: the selection is in the window, the key is the selected row's
   text, and the window is full of rows unless the list ends in it. */
static void at_(pg *p, u32 sel, u32 top, int line)
{
    u16 n;

    n = pg_rows(p);
    tf_check_long((long)key_id(p), (long)sel, TF_FILE, line, "selected");
    tf_check(n > 0, TF_FILE, line, "rows held");
    if (n == 0)
        return;
    tf_check(p->first <= p->sel && p->sel < p->first + p->ps, TF_FILE,
             line, "selection in window");
    tf_check(p->sel < n, TF_FILE, line, "selection in rows");
    tf_check(p->first + p->ps <= n || pg_at_end(p), TF_FILE, line,
             "window full");
    if (p->first < n)
        tf_check_long((long)row_id(p, p->first), (long)top, TF_FILE,
                      line, "window top");
    tf_check_long((long)row_id(p, p->sel), (long)sel, TF_FILE, line,
                  "text of selection");
}
#define AT(p, sel, top) at_((p), (u32)(sel), (u32)(top), __LINE__)

/* The numbers of the rows held run one after the other. */
static void contig_(pg *p, int line)
{
    u16 n;
    u16 i;

    n = pg_rows(p);

    for (i = 1; i < n; i++)
        if (!tf_check(row_id(p, i) == row_id(p, (u16)(i - 1)) + 1,
                      TF_FILE, line, "rows run on"))
            return;
}
#define CONTIG(p) contig_((p), __LINE__)

/* The AFTER= cursor of request line s, or "". */
static const char *after_of(const char *s)
{
    const char *p;

    p = strstr(s, " AFTER=");
    return p == NULL ? "" : p + 7;
}

/* head, then PAGE=n, then AFTER=after when not empty, into out. */
static const char *mk(char *out, const char *head, u16 n, const char *after)
{
    sprintf(out, "%s PAGE=%u", head, (unsigned)n);
    if (after[0] != '\0') {
        strcat(out, " AFTER=");
        strcat(out, after);
    }
    return out;
}

/* Request i of the log is head, PAGE=n and AFTER=after. */
static void req_(u16 i, const char *head, u16 n, const char *after,
                 int line)
{
    char want[LINK_MAXLINE + 1];

    tf_check_str(tl_req(i), mk(want, head, n, after), TF_FILE, line,
                 "request");
}
#define REQ(i, head, n, after) req_((i), (head), (n), (after), __LINE__)

#define ID_HEAD         "ITEM LIST ORDER=ID"
#define ID_WALK         "ITEM LIST ORDER=ID FIELDS=ITEM_ID"
#define MFR_HEAD        "ITEM LIST ORDER=MFR FIELDS=ITEM_ID,MFR"

/* Open g_pg over src with the test's callbacks; counters and the log
   start again. */
static pg g_pg;

static int open_(const lv_src *src, u8 ps)
{
    int rc;

    g_nwait = 0;
    g_wstop = 0;
    g_nfail = 0;
    g_fstay = 0;
    g_nfmt = 0;
    g_nmark = 0;
    g_fmt_order = 0;
    tl_clear();
    rc = pg_open(&g_pg, tc_lk, src, ps, wait_fn, fail_fn, &g_tag);
    if (rc == PG_OK)
        g_cur = &g_pg;
    return rc;
}

static pg *start(const lv_src *src, u8 ps)
{
    if (!TF_EQ(open_(src, ps), PG_OK))
        return NULL;
    return &g_pg;
}

/* The start cursors of the pages 0 .. n - 1 of the list in order (of
   items_src) for ps rows, taken from the requests of Home and n - 2 Page
   Downs: page k starts at g_cur[k] ("" for page 0).  cursors_of takes
   them in ID order. */
static char g_curs[MAXCUR][LINK_MAXCURSOR + 1];

static void cursors_in(const char *order, u8 ps, u16 n)
{
    pg *p;
    u16 k;

    p = start(&items_src, ps);
    if (p == NULL)
        return;
    TF_EQ(pg_set_order(p, order), 1);
    g_curs[0][0] = '\0';
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (k = 1; k < n; k++) {
        tl_clear();
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        if (!TF_EQ(tl_count(), 1))
            break;
        strcpy(g_curs[k], after_of(tl_req(0)));
        TF_CHECK(g_curs[k][0] != '\0');
    }
    pg_close(p);
}

static void cursors_of(u8 ps, u16 n)
{
    cursors_in("ID", ps, n);
}

/* ---------------------------------------------------------------------
 * pg_open and pg_close.
 * --------------------------------------------------------------------- */
static u16 g_nalloc;
static u16 g_nfree;
static u16 g_failat;
static u16 g_asize;             /* size of the last allocation          */

static int fault_fn(u16 size)
{
    g_asize = size;
    g_nalloc++;
    return g_nalloc == g_failat;
}

static void freeing_fn(cmem_blk *b)
{
    TF_CHECK(b != NULL);
    g_nfree++;
}

static void test_open(void)
{
    pg *p;
    char key[LV_KEYSZ];

    TF_EQ(open_(&items_src, 0), PG_EARG);
    TF_EQ(open_(&items_src, LV_MAXPAGE + 1), PG_EARG);
    TF_EQ(open_(&five_src, 10), PG_EARG);
    TF_EQ(open_(&four_src, 10), PG_OK);
    pg_close(&g_pg);
    TF_EQ(open_(&items_src, 1), PG_OK);
    pg_close(&g_pg);
    TF_EQ(open_(&items_src, LV_MAXPAGE), PG_OK);
    pg_close(&g_pg);
    TF_EQ(tl_count(), 0);

    /* Nothing is held after a refusal or a failure to allocate; and
       everything is given back by pg_close. */
    cmem_fault = fault_fn;
    cmem_freeing = freeing_fn;
    g_nalloc = 0;
    g_nfree = 0;
    g_failat = 0;
    TF_EQ(open_(&items_src, 0), PG_EARG);
    TF_EQ(open_(&five_src, 10), PG_EARG);
    TF_EQ(g_nalloc, 0);
    TF_EQ(g_nfree, 0);
    TF_EQ(open_(&items_src, 10), PG_OK);
    TF_EQ(g_nalloc, 1);
    TF_EQ(g_nfree, 0);
    TF_EQ(g_asize, 2 * 10 * LV_ROWBYTES);
    pg_close(&g_pg);
    TF_EQ(g_nfree, 1);
    g_nalloc = 0;
    g_nfree = 0;
    g_failat = 1;
    TF_EQ(open_(&items_src, 10), PG_ENOMEM);
    TF_EQ(g_nalloc, 1);
    TF_EQ(g_nfree, 0);
    cmem_fault = NULL;
    cmem_freeing = NULL;

    /* A new pager holds nothing and has fetched nothing. */
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(tl_count(), 0);
    TF_EQ(pg_rows(p), 0);
    TF_EQ(p->first, 0);
    TF_EQ(p->sel, 0);
    TF_EQ(p->order, 0);
    TF_EQ(p->stopped, 0);
    TF_EQ(p->ps, 10);
    TF_CHECK(p->src == &items_src);
    TF_STREQ(pg_order_name(p), "ID");
    strcpy(key, "junk");
    TF_EQ(pg_selected(p, key), 0);
    TF_STREQ(key, "");
    pg_close(p);

    p = start(&plain_src, 3);
    if (p == NULL)
        return;
    TF_STREQ(pg_order_name(p), "");
    TF_EQ(pg_selected(p, key), 0);
    TF_EQ(pg_rows(p), 0);
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * Home, Down and Up across the two pages, PgDn and PgUp.
 * --------------------------------------------------------------------- */
static void test_walk_pages(void)
{
    char t[LV_ROWTEXT + 1];
    pg *p;
    u32 id;
    u16 i;

    cursors_of(10, 6);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    tl_clear();
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(tl_count(), 1);
    REQ(0, ID_HEAD, 10, "");
    TF_EQ(pg_rows(p), 10);
    TF_EQ(pg_at_end(p), 0);
    TF_EQ(p->stopped, 0);
    TF_EQ(g_nwait, 0);
    TF_EQ(g_nfail, 0);
    TF_EQ(g_nfmt, 10);
    AT(p, 1, 1);
    CONTIG(p);
    /* The text is padded to its width and ends in a NUL. */
    TF_EQ(pg_text(p, 0, t), 0);
    TF_EQ(strlen(t), LV_ROWTEXT);
    TF_CHECK(tf_starts(t, "1 "));
    TF_CHECK(t[LV_ROWTEXT - 1] == ' ' && t[5] == ' ');

    /* Down inside the window, then the selection leaves it. */
    for (id = 2; id <= 10; id++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        AT(p, id, 1);
    }
    TF_EQ(tl_count(), 1);
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, 11, 2);
    CONTIG(p);
    TF_EQ(tl_count(), 2);
    REQ(1, ID_HEAD, 10, g_curs[1]);
    TF_EQ(pg_rows(p), 20);
    for (id = 12; id <= 19; id++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        AT(p, id, id - 9);
    }
    TF_EQ(tl_count(), 2);
    /* The window lies wholly in B: B becomes A. */
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, 20, 11);
    CONTIG(p);
    TF_EQ(tl_count(), 2);
    TF_EQ(pg_rows(p), 10);
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, 21, 12);
    CONTIG(p);
    TF_EQ(tl_count(), 3);
    REQ(2, ID_HEAD, 10, g_curs[2]);

    /* Up inside the window, then above A: the page before comes back
       from the ring, the first page without a cursor. */
    for (id = 20; id >= 12; id--) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        AT(p, id, 12);
    }
    TF_EQ(pg_move(p, PG_UP), RQ_OK);
    AT(p, 11, 11);
    TF_EQ(tl_count(), 3);
    TF_EQ(pg_move(p, PG_UP), RQ_OK);
    AT(p, 10, 10);
    CONTIG(p);
    TF_EQ(tl_count(), 4);
    REQ(3, ID_HEAD, 10, "");
    for (id = 9; id >= 1; id--) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        AT(p, id, id);
    }
    TF_EQ(pg_move(p, PG_UP), RQ_OK);
    AT(p, 1, 1);
    TF_EQ(tl_count(), 4);

    /* Page Down from Home: the next page is fetched, one request each. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    AT(p, 1, 1);
    for (i = 1; i <= 4; i++) {
        tl_clear();
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        AT(p, 10 * i + 1, 10 * i + 1);
        CONTIG(p);
        TF_EQ(tl_count(), 1);
        REQ(0, ID_HEAD, 10, g_curs[i]);
    }
    /* ... and Page Up goes back by the ring, page by page. */
    for (i = 3; i >= 1; i--) {
        tl_clear();
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        AT(p, 10 * i + 1, 10 * i + 1);
        CONTIG(p);
        TF_EQ(tl_count(), 1);
        REQ(0, ID_HEAD, 10, g_curs[i]);
    }
    tl_clear();
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    AT(p, 1, 1);
    TF_EQ(tl_count(), 1);
    REQ(0, ID_HEAD, 10, "");
    tl_clear();
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    AT(p, 1, 1);
    TF_EQ(tl_count(), 0);

    /* The selection is kept in step with the window, Page Up stops on
       the first row. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 4; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, 5, 1);
    tl_clear();
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    AT(p, 1, 1);
    TF_EQ(tl_count(), 0);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 4; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    AT(p, 15, 11);
    CONTIG(p);

    /* A Page Down that leaves the selection and the window across the
       boundary: Home, ten Downs (window 2..11), Page Down. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 10; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, 11, 2);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    AT(p, 21, 12);
    CONTIG(p);
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    AT(p, 11, 2);
    CONTIG(p);
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * The ring of start cursors and going up past its depth.
 * --------------------------------------------------------------------- */
#define RPS             5       /* rows of a page in the ring tests     */
#define RPAGES          (NITEMS / RPS)

static void test_ring(void)
{
    pg *p;
    u16 k;
    u16 j;

    cursors_of(RPS, RPAGES);
    p = start(&items_src, RPS);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    /* Forty pages down: the ring keeps the last LV_MAXBACK cursors of
       the pages above A, A being the page 40. */
    for (k = 1; k <= 40; k++) {
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        if (k % 10 == 0)
            AT(p, RPS * k + 1, RPS * k + 1);
    }
    for (j = 0; j < LV_MAXBACK; j++) {
        tl_clear();
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        k = (u16)(39 - j);
        AT(p, RPS * k + 1, RPS * k + 1);
        if (!TF_EQ(tl_count(), 1))
            break;
        REQ(0, ID_HEAD, RPS, g_curs[k]);
    }
    /* Past the oldest cursor kept: the first page, as Home does. */
    tl_clear();
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    TF_EQ(tl_count(), 1);
    REQ(0, ID_HEAD, RPS, "");
    TF_EQ(row_id(p, 0), 1);
    CONTIG(p);
    TF_CHECK(p->first <= p->sel && p->sel < p->first + p->ps);
    TF_EQ(tl_full(), 0);

    /* Home empties the ring: from the first page the next Page Down
       and Page Up again need no more than their one request. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    AT(p, 1, 1);
    tl_clear();
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    AT(p, 1, 1);
    TF_EQ(tl_count(), 2);
    REQ(1, ID_HEAD, RPS, "");

    /* Up, a row at a time, past the ring's depth: the same. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (k = 1; k <= 40; k++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    for (j = 0; j < (u16)(LV_MAXBACK * RPS + RPS); j++)
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
    CONTIG(p);
    TF_EQ(row_id(p, p->first), 1);
    pg_close(p);
}

/* The ring's blocks: the allocations of its first, LV_RINGSZ bytes,
   refused while g_noring is 1, and of its larger one, LV_RINGMAX,
   refused while g_nobig is 1, and their frees. */
static u16 g_nring;
static u16 g_nringfree;
static int g_noring;
static u16 g_nbig;
static u16 g_nbigfree;
static int g_nobig;

static int ring_fault(u16 size)
{
    if (size == LV_RINGMAX) {
        g_nbig++;
        return g_nobig;
    }
    if (size != LV_RINGSZ)
        return 0;
    g_nring++;
    return g_noring;
}

static void ring_freeing(cmem_blk *b)
{
    if (cmem_size(b) == LV_RINGMAX)
        g_nbigfree++;
    else if (cmem_size(b) == LV_RINGSZ)
        g_nringfree++;
}

/* Count the ring's allocations and frees from none, refusing the first
   block with noring and the larger with nobig. */
static void ring_watch(int noring, int nobig)
{
    cmem_fault = ring_fault;
    cmem_freeing = ring_freeing;
    g_nring = 0;
    g_nringfree = 0;
    g_noring = noring;
    g_nbig = 0;
    g_nbigfree = 0;
    g_nobig = nobig;
}

static void ring_unwatch(void)
{
    cmem_fault = NULL;
    cmem_freeing = NULL;
}

/* No block for a list that stays on its first two pages, one for the
   first push and none after it, freed by pg_close; while there is no
   memory for it, Page Up from the third page goes to the first, and a
   later push that finds memory takes the block. */
static void test_ring_mem(void)
{
    pg *p;
    u16 k;

    cursors_of(RPS, 3);
    ring_watch(0, 0);
    p = start(&items_src, RPS);
    if (p != NULL) {
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        for (k = 0; k < RPS; k++)
            TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        AT(p, RPS + 1, 2);
        pg_close(p);
        TF_EQ(g_nring, 0);
        TF_EQ(g_nringfree, 0);
    }

    ring_watch(0, 0);
    p = start(&items_src, RPS);
    if (p != NULL) {
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        TF_EQ(g_nring, 1);
        pg_close(p);
        TF_EQ(g_nringfree, 1);
    }

    ring_watch(1, 0);
    p = start(&items_src, RPS);
    if (p != NULL) {
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        AT(p, 2 * RPS + 1, 2 * RPS + 1);
        TF_EQ(g_nring, 2);
        tl_clear();
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        AT(p, 1, 1);
        if (TF_EQ(tl_count(), 1))
            REQ(0, ID_HEAD, RPS, "");
        g_noring = 0;
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        tl_clear();
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        AT(p, RPS + 1, RPS + 1);
        if (TF_EQ(tl_count(), 1))
            REQ(0, ID_HEAD, RPS, g_curs[1]);
        TF_EQ(g_nring, 3);
        pg_close(p);
        TF_EQ(g_nringfree, 1);
    }
    ring_unwatch();
}

/* From A at page top of the list of g_curs, whose requests start with
   head, Page Up goes to each page down to oldest > 0 with one request
   of its start cursor, then to the first page. */
static void ups_(pg *p, const char *head, u16 top, u16 oldest, int line)
{
    char want[LINK_MAXLINE + 1];
    u16 k;

    for (k = top; k > oldest; k--) {
        tl_clear();
        tf_check(pg_move(p, PG_PGUP) == RQ_OK, TF_FILE, line, "page up");
        if (!tf_check(tl_count() == 1, TF_FILE, line, "one request"))
            return;
        tf_check_str(tl_req(0), mk(want, head, RPS, g_curs[k - 1]),
                     TF_FILE, line, "request of the page above");
    }
    tl_clear();
    tf_check(pg_move(p, PG_PGUP) == RQ_OK, TF_FILE, line, "page up");
    tf_check(tl_count() == 1, TF_FILE, line, "one request");
    tf_check_str(tl_req(0), mk(want, head, RPS, ""), TF_FILE, line,
                 "the first page");
}
#define UPS(p, head, top, oldest) ups_((p), (head), (top), (oldest), \
                                       __LINE__)

/* The oldest of the pages 0 .. to whose start cursors (g_curs), pushed
   in that order, a ring in a block of LV_RINGSZ keeps: the newest that
   fit, LV_MAXBACK at most. */
static u16 oldest_fit(u16 to)
{
    u16 used;
    u16 k;

    used = 0;
    for (k = (u16)(to + 1); k > 0 && to + 1 - k < LV_MAXBACK; k--) {
        used = (u16)(used + strlen(g_curs[k - 1]) + 1);
        if (used > LV_RINGSZ)
            break;
    }
    return k;
}

/* Page Down from Home to page 40 of the list in order, with the ring's
   blocks watched (ring_watch). */
static pg *down40(const char *order, int nobig)
{
    pg *p;
    u16 k;

    ring_watch(0, nobig);
    p = start(&items_src, RPS);
    if (p == NULL)
        return NULL;
    TF_EQ(pg_set_order(p, order), 1);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (k = 1; k <= 40; k++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    return p;
}

/* The ring packs its cursors.  Those of the list in ID order are short:
   LV_MAXBACK of them fit its first block, which serves alone.  Those in
   manufacturer order are longer: once the first block is full the ring
   moves to its larger one, freeing the first, and keeps LV_MAXBACK
   pages; without memory for that it keeps the newest that fit the
   first, fewer, and then goes to the first page, and a failed End
   leaves it the cursors it held before but those the walk's pushes
   dropped. */
static void test_ring_pack(void)
{
    pg *p;
    u16 old;
    u16 k;
    int m;

    cursors_of(RPS, RPAGES);
    p = down40("ID", 0);
    if (p != NULL) {
        TF_EQ(g_nring, 1);
        TF_EQ(g_nbig, 0);
        /* Home, and a new order twice (back to ID), empty the full
           ring and keep its block: forty pages down again are held as
           before. */
        for (m = 0; m < 2; m++) {
            TF_EQ(pg_move(p, m == 0 ? PG_HOME : PG_ORDER), RQ_OK);
            if (m == 1)
                TF_EQ(pg_move(p, PG_ORDER), RQ_OK);
            for (k = 1; k <= 40; k++)
                TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
            TF_EQ(g_nring, 1);
            TF_EQ(g_nbig, 0);
        }
        UPS(p, ID_HEAD, 40, 40 - LV_MAXBACK);
        pg_close(p);
        TF_EQ(g_nringfree, 1);
    }

    cursors_in("MFR", RPS, RPAGES);
    TF_CHECK(oldest_fit(39) > 40 - LV_MAXBACK);
    p = down40("MFR", 0);
    if (p != NULL) {
        TF_EQ(g_nring, 1);
        TF_EQ(g_nbig, 1);
        TF_EQ(g_nringfree, 1);
        UPS(p, MFR_HEAD, 40, 40 - LV_MAXBACK);
        pg_close(p);
        TF_EQ(g_nringfree, 1);
        TF_EQ(g_nbigfree, 1);
    }

    p = down40("MFR", 1);
    if (p != NULL) {
        TF_CHECK(g_nbig > 0);
        UPS(p, MFR_HEAD, 40, oldest_fit(39));
        pg_close(p);
    }

    /* End from page 40 pushes the pages 40 to 48 and fails on its fetch
       of the last, the tenth request. */
    p = down40("MFR", 1);
    if (p != NULL) {
        old = oldest_fit(48);
        TF_CHECK(old > oldest_fit(39) && old < 40);
        tl_clear();
        tl_fail(10, "-ERR 402 busy");
        TF_EQ(pg_move(p, PG_END), RQ_EBUSY);
        TF_EQ(tl_count(), 10);
        UPS(p, MFR_HEAD, 40, old);
        pg_close(p);
    }
    ring_unwatch();
}

/* A list the test makes up, to choose the lengths of its cursors: page
   k (from 0) of g_synpages holds RPS rows, the items k * RPS + 1 on,
   and the page after it starts at g_curs[k + 1], "S<k + 1>" padded with
   '-' (syn_start).  syn_hook answers each request from it. */
static u16 g_synpages;
static char g_synresp[LINK_MAXRESP];

static void syn_hook(const char *line, u16 len)
{
    char req[LINK_MAXLINE + 1];
    const char *a;
    u16 k;
    u16 i;

    if (len > LINK_MAXLINE)
        len = LINK_MAXLINE;
    memcpy(req, line, len);
    req[len] = '\0';
    a = after_of(req);
    k = a[0] == 'S' ? (u16)atoi(a + 1) : 0;
    strcpy(g_synresp, "+DATA\nF ITEM_ID\n");
    for (i = 1; i <= RPS; i++)
        sprintf(g_synresp + strlen(g_synresp), "R %u\n",
                (unsigned)(k * RPS + i));
    if (k + 1 < g_synpages)
        sprintf(g_synresp + strlen(g_synresp), "C %s\n", g_curs[k + 1]);
    strcat(g_synresp, ".");
    tl_fail(tl_count(), g_synresp);
}

/* Make up a list of pages pages whose cursors are len characters long,
   the first page's "", answered from then on. */
static void syn_start(u16 pages, u16 len)
{
    u16 k;
    u16 n;

    g_synpages = pages;
    g_curs[0][0] = '\0';
    for (k = 1; k < pages; k++) {
        sprintf(g_curs[k], "S%u", (unsigned)k);
        for (n = (u16)strlen(g_curs[k]); n < len; n++)
            g_curs[k][n] = '-';
        g_curs[k][len] = '\0';
    }
    tl_hook = syn_hook;
}

/* Home and down pages pages of the made-up list, the ring's blocks
   watched, the larger refused with nobig. */
static pg *syn_down(u16 pages, int nobig)
{
    pg *p;
    u16 k;

    ring_watch(0, nobig);
    p = start(&items_src, RPS);
    if (p == NULL)
        return NULL;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (k = 1; k <= pages; k++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    return p;
}

/* The ring's bytes at their bounds, on made-up cursors:
   - of 47 characters, LV_MAXBACK of which fill the first block exactly:
     forty pages down keep LV_MAXBACK pages there, the push past the
     ring's count dropping the oldest before it asks for room;
   - of 63, the first block refused its successor: the push that would
     pass its end by one byte drops the oldest instead, so the block
     ends full with the newest that fit;
   - of LINK_MAXCURSOR, dropped for room, and popped back whole with
     the first page's "". */
static void test_ring_bytes(void)
{
    pg *p;
    u16 fit;
    u16 k;

    syn_start(41, 47);
    p = syn_down(40, 0);
    if (p != NULL) {
        TF_EQ(g_nring, 1);
        TF_EQ(g_nbig, 0);
        UPS(p, ID_HEAD, 40, 40 - LV_MAXBACK);
        pg_close(p);
    }

    syn_start(31, 63);
    fit = LV_RINGSZ / 64;
    TF_CHECK(fit < 30);
    p = syn_down(30, 1);
    if (p != NULL && fit < 30) {
        UPS(p, ID_HEAD, 30, (u16)(30 - fit));
        pg_close(p);
    }

    syn_start(21, LINK_MAXCURSOR);
    p = syn_down(20, 1);
    if (p != NULL) {
        UPS(p, ID_HEAD, 20, (u16)(20 - LV_RINGSZ / LV_CURSZ));
        pg_close(p);
    }
    syn_start(7, LINK_MAXCURSOR);
    p = syn_down(5, 0);
    if (p != NULL) {
        for (k = 5; k > 0; k--) {
            tl_clear();
            TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
            if (!TF_EQ(tl_count(), 1))
                break;
            REQ(0, ID_HEAD, RPS, g_curs[k - 1]);
        }
        TF_EQ(row_id(p, 0), 1);
        pg_close(p);
    }
    tl_hook = NULL;
    ring_unwatch();
}

/* ---------------------------------------------------------------------
 * End.
 * --------------------------------------------------------------------- */
/* After Home the End walk goes on from the cursor after the page held:
   walk requests for the pages from 1 on, each with the start cursor of
   its page, the wait function called before each, the rows passed
   growing by a page each time; then the last n requests, in any order,
   are whole rows, for the pages in last[0..n-1] (start cursors). */
static void walk_reqs_(u16 ps, u16 n, const u16 *last, int line)
{
    char want[LINK_MAXLINE + 1];
    u16 i;
    u16 w;
    u16 j;
    int found;

    w = (u16)(tl_count() - n);
    tf_check(!tl_full(), TF_FILE, line, "log full");
    for (i = 0; i < w; i++)
        tf_check_str(tl_req(i), mk(want, ID_WALK, ps, g_curs[i + 1]),
                     TF_FILE, line, "walk request");
    tf_check_long(g_nwait, w, TF_FILE, line, "waits");
    for (i = 0; i < w && i < MAXW; i++) {
        tf_check(g_wwalk[i] == 1, TF_FILE, line, "wait walk");
        tf_check(g_wn[i] == g_wn[0] + (u32)i * ps, TF_FILE, line,
                 "wait n grows by a page");
    }
    for (j = 0; j < n; j++) {
        found = 0;
        for (i = w; i < tl_count(); i++)
            if (strcmp(tl_req(i), mk(want, ID_HEAD, ps,
                                     g_curs[last[j]])) == 0)
                found = 1;
        tf_check(found, TF_FILE, line, "whole-row request of a last page");
    }
}
#define WALK_REQS(ps, n, last) walk_reqs_((ps), (n), (last), __LINE__)

static void test_end(void)
{
    static const u16 l24[] = { 24 };
    static const u16 l35[] = { 18, 19 };
    char want[LINK_MAXLINE + 1];
    pg *p;
    u16 i;

    cursors_of(10, 25);

    /* A last page that fills the window. */
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    tl_clear();
    g_nwait = 0;
    g_nfmt = 0;
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    WALK_REQS(10, 1, l24);
    TF_STREQ(tl_req((u16)(tl_count() - 1)),
             mk(want, ID_HEAD, 10, g_curs[NITEMS / 10 - 1]));
    AT(p, NITEMS, NITEMS - 9);
    CONTIG(p);
    TF_EQ(pg_rows(p), 10);
    TF_EQ(pg_at_end(p), 1);
    TF_EQ(p->stopped, 0);
    /* The walk did not format its rows. */
    TF_EQ(g_nfmt, 10);
    /* At the last row a Down stays, and Page Down stops on it. */
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, NITEMS, NITEMS - 9);
    TF_EQ(pg_at_end(p), 1);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    AT(p, NITEMS, NITEMS - 9);
    /* The ring serves the way back: the page before, then Home. */
    tl_clear();
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    AT(p, NITEMS - 10, NITEMS - 19);
    TF_EQ(tl_count(), 1);
    REQ(0, ID_HEAD, 10, g_curs[NITEMS / 10 - 2]);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    AT(p, 1, 1);
    pg_close(p);

    /* A last page shorter than the window: the page before it is
       fetched too, so that the window ends on the last row. */
    cursors_of(13, 20);
    p = start(&items_src, 13);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    tl_clear();
    g_nwait = 0;
    g_nfmt = 0;
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    WALK_REQS(13, 2, l35);
    AT(p, NITEMS, NITEMS - 12);
    CONTIG(p);
    TF_EQ(pg_at_end(p), 1);
    TF_EQ(p->first + p->ps, pg_rows(p));
    TF_EQ(g_nfmt, pg_rows(p));
    pg_close(p);

    /* A list shorter than a page: End finds it all held. */
    p = start(&card_src, 25);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    tl_clear();
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_EQ(pg_rows(p), NCARD);
    TF_EQ(p->first, 0);
    AT(p, NCARD, 1);
    TF_EQ(pg_at_end(p), 1);
    pg_close(p);

    /* A source without a walk field walks with whole rows. */
    p = start(&nowalk_src, 25);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    tl_clear();
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    for (i = 0; i < tl_count(); i++)
        TF_CHECK(strstr(tl_req(i), "FIELDS") == NULL);
    AT(p, NITEMS, NITEMS - 24);
    pg_close(p);
}

/* The End walk over more pages than the ring holds, then the way back
   up: the cursors that fit, then the first page. */
static void test_end_ring(void)
{
    pg *p;
    u16 j;
    u16 k;

    cursors_of(RPS, RPAGES);
    p = start(&items_src, RPS);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    AT(p, NITEMS, NITEMS - RPS + 1);
    TF_EQ(pg_at_end(p), 1);
    for (j = 0; j < LV_MAXBACK; j++) {
        tl_clear();
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        k = (u16)(RPAGES - 2 - j);
        AT(p, RPS * k + RPS, RPS * k + 1);
        if (!TF_EQ(tl_count(), 1))
            break;
        REQ(0, ID_HEAD, RPS, g_curs[k]);
    }
    tl_clear();
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    TF_EQ(tl_count(), 1);
    REQ(0, ID_HEAD, RPS, "");
    TF_EQ(row_id(p, 0), 1);
    CONTIG(p);
    pg_close(p);
}

/* End without memory for the ring: a short last page is shown alone,
   and Page Up from it goes to the first page. */
static void end_short(void)
{
    pg *p;

    ring_watch(1, 0);
    p = start(&items_src, 13);
    if (p != NULL) {
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        TF_CHECK(g_nring > 0);
        AT(p, NITEMS, NITEMS - NITEMS % 13 + 1);
        TF_EQ(pg_rows(p), NITEMS % 13);
        TF_EQ(pg_at_end(p), 1);
        CONTIG(p);
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        AT(p, 1, 1);
        pg_close(p);
    }
    ring_unwatch();
}

static void test_end_noring(void)
{
    pg *p;
    int noring;

    end_short();
    /* The walk's last page holds none (the rest deleted meanwhile): the
       page before it is shown, its cursor popped from the ring or, with
       no memory for the ring, the walk's own. */
    cursors_of(RPS, 3);
    for (noring = 0; noring < 2; noring++) {
        ring_watch(noring, 0);
        p = start(&items_src, RPS);
        if (p == NULL)
            break;
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        script(NULL, "+DATA\nF ITEM_ID\n.", NULL);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        unscript();
        AT(p, 2 * RPS, RPS + 1);
        TF_EQ(pg_rows(p), RPS);
        if (TF_EQ(tl_count(), 3))
            REQ(2, ID_HEAD, RPS, g_curs[1]);
        TF_EQ(g_nring, noring ? 2 : 1);
        pg_close(p);
    }
    ring_unwatch();
}

/* ---------------------------------------------------------------------
 * Reload.
 * --------------------------------------------------------------------- */
static void test_reload(void)
{
    pg *p;
    u16 i;
    int found;

    cursors_of(10, 25);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 13; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, 14, 5);
    tl_clear();
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_EQ(tl_count(), 2);
    REQ(0, ID_HEAD, 10, "");
    REQ(1, ID_HEAD, 10, g_curs[1]);
    AT(p, 14, 5);
    CONTIG(p);

    /* The row number is kept when the list changes: item 3 is gone, so
       the rows are 1 2 4 5 6 ..., and row 13 is item 15. */
    item_op("DEL", 3);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    AT(p, 15, 6);
    TF_EQ(pg_rows(p), 20);
    TF_EQ(row_id(p, 9), 11);
    TF_EQ(row_id(p, 10), 12);
    item_op("UNDEL", 3);

    /* Pages further on reload from their own cursors. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 3; i++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    AT(p, 31, 31);
    tl_clear();
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_EQ(tl_count(), 1);
    REQ(0, ID_HEAD, 10, g_curs[3]);
    AT(p, 31, 31);
    CONTIG(p);
    /* ... and Page Up still finds the page before. */
    tl_clear();
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    REQ(0, ID_HEAD, 10, g_curs[2]);
    AT(p, 21, 21);

    /* A list that has shrunk under the window: the rows fetched end the
       list and the window ends on the last row, with the page before
       the short one fetched too, two requests in all, the short page's
       rows kept. */
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    AT(p, NITEMS, NITEMS - 9);
    item_op("DEL", NITEMS);
    item_op("DEL", NITEMS - 1);
    tl_clear();
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_EQ(tl_count(), 2);
    found = 0;
    for (i = 0; i < tl_count(); i++) {
        TF_CHECK(tf_starts(tl_req(i), ID_HEAD " PAGE=10 AFTER="));
        if (strcmp(after_of(tl_req(i)), g_curs[NITEMS / 10 - 2]) == 0)
            found = 1;
    }
    TF_CHECK(found);
    TF_EQ(pg_at_end(p), 1);
    TF_EQ(row_id(p, (u16)(pg_rows(p) - 1)), NITEMS - 2);
    TF_EQ(p->first + p->ps, pg_rows(p));
    TF_EQ(row_id(p, p->first), NITEMS - 11);
    CONTIG(p);
    TF_CHECK(p->first <= p->sel && p->sel < p->first + p->ps);
    item_op("UNDEL", NITEMS);
    item_op("UNDEL", NITEMS - 1);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * Sort orders, request lines, accessors.
 * --------------------------------------------------------------------- */
static void test_orders(void)
{
    char want[LINK_MAXLINE + 1];
    char key[LV_KEYSZ];
    pg *p;

    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_STREQ(pg_order_name(p), "ID");
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(g_fmt_order, 0);
    tl_clear();
    TF_EQ(pg_move(p, PG_ORDER), RQ_OK);
    TF_EQ(tl_count(), 1);
    REQ(0, MFR_HEAD, 10, "");
    TF_STREQ(pg_order_name(p), "MFR");
    TF_EQ(p->order, 1);
    TF_EQ(g_fmt_order, 1);
    TF_EQ(pg_rows(p), 10);
    TF_EQ(p->sel, 0);
    TF_EQ(p->first, 0);
    /* Items of MK0 come first (the index orders by manufacturer, then
       model), every seventh item: the format function saw the field
       list of the order. */
    pg_text(p, 0, want);
    TF_CHECK(strstr(want, " MK0 ") != NULL);
    TF_EQ(key_id(p) % 7, 0);
    TF_EQ(row_id(p, 1) % 7, 0);
    /* Further pages keep the order and its field list. */
    tl_clear();
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_CHECK(tf_starts(tl_req(0), MFR_HEAD " PAGE=10 AFTER="));
    tl_clear();
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_CHECK(tf_starts(tl_req(0), MFR_HEAD " PAGE=10 AFTER="));
    /* It wraps to the first order and goes to the first page. */
    tl_clear();
    TF_EQ(pg_move(p, PG_ORDER), RQ_OK);
    REQ(0, ID_HEAD, 10, "");
    TF_STREQ(pg_order_name(p), "ID");
    TF_EQ(p->order, 0);
    AT(p, 1, 1);

    /* Without a fetch: set_order, and a refusal of any other name. */
    tl_clear();
    TF_EQ(pg_set_order(p, "MFR"), 1);
    TF_EQ(tl_count(), 0);
    TF_EQ(p->order, 1);
    TF_STREQ(pg_order_name(p), "MFR");
    TF_EQ(pg_set_order(p, "BOGUS"), 0);
    TF_EQ(pg_set_order(p, "mfr"), 0);
    TF_STREQ(pg_order_name(p), "MFR");
    TF_EQ(p->order, 1);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    REQ(0, MFR_HEAD, 10, "");
    TF_EQ(pg_set_order(p, "ID"), 1);
    TF_EQ(p->order, 0);
    pg_close(p);

    /* A source without orders: no ORDER=, fields[0], and ORDER goes
       to the first page. */
    p = start(&plain_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_STREQ(tl_req(0), mk(want, "ITEM LIST", 10, ""));
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    tl_clear();
    TF_EQ(pg_move(p, PG_ORDER), RQ_OK);
    TF_STREQ(tl_req(0), mk(want, "ITEM LIST", 10, ""));
    AT(p, 1, 1);
    TF_EQ(p->order, 0);
    TF_STREQ(pg_order_name(p), "");
    TF_EQ(pg_set_order(p, "ID"), 0);
    TF_EQ(pg_set_order(p, ""), 0);
    pg_close(p);

    /* The order of a request: the source's words and filters, ORDER=,
       FIELDS=, PAGE=, AFTER=. */
    p = start(&card_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM LIST CAT=CARD ORDER=ID PAGE=10");
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_CHECK(tf_starts(tl_req(1),
                       "ITEM LIST CAT=CARD ORDER=ID PAGE=10 AFTER="));
    TF_EQ(pg_move(p, PG_ORDER), RQ_OK);
    TF_STREQ(tl_req(2),
             "ITEM LIST CAT=CARD ORDER=MFR FIELDS=ITEM_ID,MFR PAGE=10");
    TF_EQ(pg_selected(p, key), 1);
    /* The End walk keeps the order, with the walk field alone. */
    tl_clear();
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_EQ(tl_count(), 3);
    TF_CHECK(tf_starts(tl_req(0),
             "ITEM LIST CAT=CARD ORDER=MFR FIELDS=ITEM_ID PAGE=10 AFTER="));
    TF_CHECK(tf_starts(tl_req(1),
             "ITEM LIST CAT=CARD ORDER=MFR FIELDS=ITEM_ID PAGE=10 AFTER="));
    TF_CHECK(tf_starts(tl_req(2), "ITEM LIST CAT=CARD ORDER=MFR "
                       "FIELDS=ITEM_ID,MFR PAGE=10 AFTER="));
    TF_EQ(pg_rows(p), 10);
    pg_close(p);

    p = start(&find_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM FIND ZQ PAGE=10");
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * Rows, text, marks, selection.
 * --------------------------------------------------------------------- */
static void test_rows(void)
{
    char t[LV_ROWTEXT + 1];
    char key[LV_KEYSZ];
    pg *p;
    u16 i;

    p = start(&mark_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(g_nmark, 10);
    TF_EQ(pg_rows(p), 10);
    for (i = 0; i < 10; i++) {
        strcpy(t, "junk");
        TF_EQ(pg_text(p, i, t), (i + 1) & 1);
        TF_EQ(row_id(p, i), i + 1);
        TF_EQ(strlen(t), LV_ROWTEXT);
    }
    strcpy(key, "junk");
    TF_EQ(pg_selected(p, key), 1);
    TF_STREQ(key, "1");
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    TF_EQ(pg_selected(p, key), 1);
    TF_STREQ(key, "2");
    /* The second page's marks are made when it is fetched. */
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(g_nmark, 20);
    TF_EQ(key_id(p), 12);
    TF_EQ(pg_text(p, p->sel, t), 0);
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    TF_EQ(key_id(p), 13);
    TF_EQ(pg_text(p, p->sel, t), 1);
    pg_close(p);

    /* Without a mark function no row is marked. */
    p = start(&plain_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 10; i++)
        TF_EQ(pg_text(p, i, t), 0);
    pg_close(p);

    /* Rows made from the F line's names: a response of the test's own,
       with the fields in another order and a key as long as it gets. */
    p = start(&items_src, 4);
    if (p == NULL)
        return;
    script("+DATA\nF MFR ITEM_ID\nR MKX 12345678\nR \"MK Y\" 9\n.",
           NULL, NULL);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    unscript();
    TF_EQ(pg_rows(p), 2);
    TF_EQ(pg_selected(p, key), 1);
    TF_STREQ(key, "12345678");
    pg_text(p, 0, t);
    TF_CHECK(tf_starts(t, "12345678 MKX "));
    pg_text(p, 1, t);
    TF_CHECK(tf_starts(t, "9 MK Y "));
    TF_EQ(pg_at_end(p), 1);
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * Pages filled by several requests, and the wait function.
 * --------------------------------------------------------------------- */
static void test_fill(void)
{
    char c[LINK_MAXCURSOR + 1];
    char want[LINK_MAXLINE + 1];
    pg *p;
    u16 got;
    u16 i;

    p = start(&find_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_STREQ(tl_req(0), "ITEM FIND ZQ PAGE=10");
    /* The server stopped early: the next request asks for what is
       missing from where the first left off. */
    got = (u16)(10 - num_of(strstr(tl_req(1), "PAGE=") + 5));
    TF_CHECK(got > 0 && got < 10);
    sprintf(want, "ITEM FIND ZQ PAGE=%u AFTER=", (unsigned)(10 - got));
    TF_CHECK(tf_starts(tl_req(1), want));
    strcpy(c, after_of(tl_req(1)));
    TF_EQ(pg_rows(p), 10);
    for (i = 0; i < 10; i++)
        TF_EQ(row_id(p, i), (i + 1) * FIND_EVERY);
    TF_EQ(g_nwait, 1);
    TF_EQ(g_wwalk[0], 0);
    TF_EQ(g_wn[0], got);
    TF_EQ(p->stopped, 0);
    TF_EQ(g_nfmt, 10);
    TF_EQ(g_nfail, 0);

    /* The wait function stops the fill: the rows found are kept, and
       so is the cursor after them. */
    g_nwait = 0;
    g_wstop = 1;
    tl_clear();
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(g_nwait, 1);
    TF_EQ(p->stopped, 1);
    TF_EQ(pg_rows(p), got);
    TF_EQ(key_id(p), FIND_EVERY);
    TF_EQ(pg_at_end(p), 0);
    g_wstop = 0;
    /* A move down goes on from there: the rest of the page is fetched
       from the cursor after the rows kept. */
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_CHECK(tf_starts(tl_req(1), "ITEM FIND ZQ PAGE="));
    TF_STREQ(after_of(tl_req(1)), c);
    TF_EQ(p->stopped, 0);
    TF_EQ(pg_rows(p), 10);
    TF_EQ(key_id(p), 2 * FIND_EVERY);
    for (i = 0; i < 10; i++)
        TF_EQ(row_id(p, i), (i + 1) * FIND_EVERY);

    /* A reload fetches whole the page that a stopped Home left short. */
    g_nwait = 0;
    g_wstop = 1;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(p->stopped, 1);
    g_wstop = 0;
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_EQ(p->stopped, 0);
    TF_EQ(pg_rows(p), 10);
    pg_close(p);

    /* Responses of the test's own: none, and a cursor. */
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    script("+DATA\nF ITEM_ID\nC X1\n.",
           "+DATA\nF ITEM_ID\nR 3\nR 4\nC X2\n.",
           "+DATA\nF ITEM_ID\nR 5\nR 6\nR 7\nR 8\nR 9\nR 10\nR 11\nR 12\n.");
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    unscript();
    TF_EQ(tl_count(), 3);
    REQ(0, ID_HEAD, 10, "");
    REQ(1, ID_HEAD, 10, "X1");
    REQ(2, ID_HEAD, 8, "X2");
    TF_EQ(pg_rows(p), 10);
    TF_EQ(pg_at_end(p), 1);
    TF_EQ(g_nwait, 2);
    TF_EQ(g_wwalk[0], 0);
    TF_EQ(g_wn[0], 0);
    TF_EQ(g_wwalk[1], 0);
    TF_EQ(g_wn[1], 2);
    AT(p, 3, 3);
    CONTIG(p);

    /* A list that ended without rows (a stop before any row) fetches on
       when the selection moves down, and selects the first row found. */
    g_nwait = 0;
    g_wstop = 1;
    script("+DATA\nF ITEM_ID\nC X1\n.", NULL, NULL);
    tl_clear();
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    unscript();
    g_wstop = 0;
    TF_EQ(p->stopped, 1);
    TF_EQ(pg_rows(p), 0);
    TF_EQ(pg_at_end(p), 0);
    TF_EQ(pg_selected(p, c), 0);
    TF_EQ(tl_count(), 1);
    script("+DATA\nF ITEM_ID\nR 5\nR 6\n.", NULL, NULL);
    tl_clear();
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    unscript();
    TF_EQ(tl_count(), 1);
    REQ(0, ID_HEAD, 10, "X1");
    TF_EQ(p->stopped, 0);
    TF_EQ(pg_rows(p), 2);
    AT(p, 5, 5);
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * The wait function in the End walk.
 * --------------------------------------------------------------------- */
static void test_wait_walk(void)
{
    char want[LINK_MAXLINE + 1];
    pg *p;

    cursors_of(10, 4);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    g_nwait = 0;
    g_wstop = 3;
    tl_clear();
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_EQ(p->stopped, 1);
    TF_EQ(g_nwait, 3);
    TF_EQ(g_wwalk[0], 1);
    TF_EQ(g_wwalk[1], 1);
    TF_EQ(g_wwalk[2], 1);
    TF_EQ(g_wn[0], 0);
    TF_EQ(g_wn[1], 10);
    TF_EQ(g_wn[2], 20);
    /* Two pages walked, then the page the stop left it on, with whole
       rows. */
    TF_EQ(tl_count(), 3);
    TF_STREQ(tl_req(0), mk(want, ID_WALK, 10, g_curs[1]));
    TF_STREQ(tl_req(1), mk(want, ID_WALK, 10, g_curs[2]));
    TF_STREQ(tl_req(2), mk(want, ID_HEAD, 10, g_curs[2]));
    AT(p, 30, 21);
    CONTIG(p);
    TF_EQ(pg_at_end(p), 0);
    g_wstop = 0;
    /* The next move goes on from the cursor after the page. */
    tl_clear();
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    TF_EQ(p->stopped, 0);
    TF_EQ(tl_count(), 1);
    TF_CHECK(tf_starts(tl_req(0), ID_HEAD " PAGE=10 AFTER="));
    TF_STREQ(tl_req(0), mk(want, ID_HEAD, 10, g_curs[3]));
    AT(p, 31, 22);
    /* A stop at the first call walks nothing. */
    g_nwait = 0;
    g_wstop = 1;
    tl_clear();
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_EQ(p->stopped, 1);
    TF_EQ(g_nwait, 1);
    TF_EQ(g_wwalk[0], 1);
    TF_EQ(g_wn[0], 0);
    TF_CHECK(tl_count() <= 1);
    CONTIG(p);
    TF_CHECK(p->first <= p->sel && p->sel < p->first + p->ps);
    /* A walk stopped at its third wait whose fetch of the page reached
       fails: the move failed, so it reports no stop. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    g_nwait = 0;
    g_wstop = 3;
    g_nfail = 0;
    script(NULL, NULL, BUSY);
    TF_EQ(pg_move(p, PG_END), 402);
    unscript();
    TF_EQ(g_nwait, 3);
    TF_EQ(tl_count(), 3);
    TF_EQ(g_nfail, 1);
    TF_EQ(p->stopped, 0);
    g_wstop = 0;
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * The fail function.
 * --------------------------------------------------------------------- */
static void test_fail(void)
{
    pg *p;
    u16 i;

    /* The very first fetch fails: nothing is held. */
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    script(BUSY, NULL, NULL);
    TF_EQ(pg_move(p, PG_HOME), 402);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_EQ(g_fcode[0], 402);
    TF_STREQ(g_ftext[0], "busy");
    TF_EQ(tl_count(), 1);
    TF_EQ(pg_rows(p), 0);
    TF_EQ(p->stopped, 0);
    /* The answer of the fail function does not matter to a code that
       is not a bad cursor. */
    g_nfail = 0;
    g_fstay = 1;
    script(BUSY, NULL, NULL);
    tl_clear();
    TF_EQ(pg_move(p, PG_HOME), 402);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_EQ(tl_count(), 1);
    g_fstay = 0;

    /* A failed fetch of the first page held leaves it as it was, the
       second page is dropped and the selection and window move back. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 12; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, 13, 4);
    TF_EQ(pg_rows(p), 20);
    g_nfail = 0;
    script(BUSY, NULL, NULL);
    tl_clear();
    TF_EQ(pg_move(p, PG_RELOAD), 402);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_EQ(tl_count(), 1);
    TF_EQ(pg_rows(p), 10);
    TF_EQ(p->first, 0);
    TF_CHECK(p->sel < 10);
    TF_EQ(key_id(p), p->sel + 1);
    CONTIG(p);
    TF_EQ(row_id(p, 0), 1);
    /* Home and End are failed fetches of the first page in the same
       way, going up too. */
    g_nfail = 0;
    script(BUSY, NULL, NULL);
    TF_EQ(pg_move(p, PG_HOME), 402);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_EQ(pg_rows(p), 10);
    TF_EQ(row_id(p, 0), 1);
    g_nfail = 0;
    script(BUSY, NULL, NULL);
    TF_EQ(pg_move(p, PG_END), 402);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_EQ(g_fcode[0], 402);
    TF_EQ(pg_rows(p), 10);
    TF_EQ(row_id(p, 0), 1);
    TF_CHECK(p->first <= p->sel && p->sel < p->first + p->ps);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    AT(p, 21, 21);
    g_nfail = 0;
    script(BUSY, NULL, NULL);
    TF_EQ(pg_move(p, PG_PGUP), 402);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_CHECK(pg_rows(p) >= 1);
    TF_CHECK(p->first <= p->sel && p->sel < p->first + p->ps);
    CONTIG(p);

    /* A failed fetch of the second page keeps the rows of its requests
       that succeeded. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 9; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    g_nfail = 0;
    script("+DATA\nF ITEM_ID\nR 11\nR 12\nR 13\nC X1\n.", BUSY, NULL);
    tl_clear();
    TF_EQ(pg_move(p, PG_DOWN), 402);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_EQ(g_fcode[0], 402);
    TF_EQ(tl_count(), 2);
    REQ(0, ID_HEAD, 10, g_curs[1]);
    REQ(1, ID_HEAD, 7, "X1");
    TF_EQ(pg_rows(p), 13);
    CONTIG(p);
    TF_EQ(row_id(p, 12), 13);
    pg_close(p);
}

/* A bad cursor: the pager starts again at the first page only when the
   fail function says to stay; a failure of that fetch is reported too,
   and a move makes at most two calls. */
static void test_cursor(void)
{
    pg *p;
    u16 i;

    cursors_of(10, 4);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    for (i = 0; i < 2; i++) {
        /* i = 0: the fail function leaves the list; i = 1: stays. */
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        g_nfail = 0;
        g_fstay = (int)i;
        script(BADCUR, NULL, NULL);
        tl_clear();
        TF_EQ(pg_move(p, PG_PGDN), i == 0 ? RQ_ECURSOR : RQ_OK);
        unscript();
        TF_EQ(g_nfail, 1);
        TF_EQ(g_fcode[0], RQ_ECURSOR);
        TF_STREQ(g_ftext[0], "bad cursor");
        if (i == 0) {
            TF_EQ(tl_count(), 1);
        } else {
            TF_EQ(tl_count(), 2);
            REQ(1, ID_HEAD, 10, "");
            TF_EQ(row_id(p, 0), 1);
            CONTIG(p);
            TF_CHECK(p->first <= p->sel && p->sel < p->first + p->ps);
        }
    }

    /* From a page further on, a bad cursor and its restart. */
    g_fstay = 1;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    g_nfail = 0;
    script(BADCUR, NULL, NULL);
    tl_clear();
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_EQ(tl_count(), 2);
    REQ(0, ID_HEAD, 10, g_curs[2]);
    REQ(1, ID_HEAD, 10, "");
    TF_EQ(row_id(p, 0), 1);
    CONTIG(p);

    /* The restart fails too: both are reported, the second code is the
       result, and nothing is tried a third time. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    g_nfail = 0;
    script(BADCUR, BUSY, NULL);
    tl_clear();
    TF_EQ(pg_move(p, PG_PGDN), 402);
    unscript();
    TF_EQ(g_nfail, 2);
    TF_EQ(g_fcode[0], RQ_ECURSOR);
    TF_EQ(g_fcode[1], 402);
    TF_EQ(tl_count(), 2);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    g_nfail = 0;
    script(BADCUR, BADCUR, NULL);
    TF_EQ(pg_move(p, PG_PGDN), RQ_ECURSOR);
    unscript();
    TF_EQ(g_nfail, 2);
    TF_EQ(g_fcode[0], RQ_ECURSOR);
    TF_EQ(g_fcode[1], RQ_ECURSOR);

    /* Home itself cannot have a bad cursor, but a bad answer is one
       call; End's walk: a bad cursor at the third request. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    g_nfail = 0;
    g_fstay = 0;
    script(NULL, NULL, BADCUR);
    tl_clear();
    TF_EQ(pg_move(p, PG_END), RQ_ECURSOR);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_EQ(tl_count(), 3);
    TF_EQ(pg_rows(p), 10);
    g_fstay = 1;
    g_nfail = 0;
    script(NULL, NULL, BADCUR);
    tl_clear();
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_EQ(tl_count(), 4);
    REQ(3, ID_HEAD, 10, "");
    TF_EQ(row_id(p, 0), 1);
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * pg_seek.
 * --------------------------------------------------------------------- */
static u16 g_ncall;
static char g_keys[64][LV_KEYSZ];
static u32 g_want;

/* The row's key and text are both its item's number (fmt_item). */
static int match_id(void *arg, const char *key, const char *text)
{
    TF_CHECK(arg == (void *)&g_want);
    TF_EQ(num_of(text), num_of(key));
    TF_EQ(strlen(text), LV_ROWTEXT);
    if (g_ncall < 64)
        strcpy(g_keys[g_ncall], key);
    g_ncall++;
    return num_of(key) == g_want;
}

static void test_seek(void)
{
    pg *p;
    u16 i;

    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    /* A match on the way: match is not called on the start row before
       the first move, and is on every row reached. */
    g_ncall = 0;
    g_want = 7;
    TF_EQ(pg_seek(p, match_id, &g_want, 1000), RQ_OK);
    TF_EQ(g_ncall, 6);
    TF_STREQ(g_keys[0], "2");
    TF_STREQ(g_keys[5], "7");
    AT(p, 7, 1);
    /* Across the boundary of the two pages. */
    g_ncall = 0;
    g_want = 17;
    TF_EQ(pg_seek(p, match_id, &g_want, 1000), RQ_OK);
    TF_EQ(g_ncall, 10);
    AT(p, 17, 8);
    CONTIG(p);
    /* max rows passed: it stops there, on a row that did not match. */
    g_ncall = 0;
    g_want = 999;
    TF_EQ(pg_seek(p, match_id, &g_want, 5), RQ_OK);
    TF_EQ(g_ncall, 5);
    TF_EQ(key_id(p), 22);
    TF_STREQ(g_keys[4], "22");
    TF_EQ(p->stopped, 0);
    /* A match at the max-th row still counts. */
    g_ncall = 0;
    g_want = 27;
    TF_EQ(pg_seek(p, match_id, &g_want, 5), RQ_OK);
    TF_EQ(key_id(p), 27);
    TF_EQ(g_ncall, 5);

    /* At the last row it goes on at the first row. */
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    AT(p, NITEMS, NITEMS - 9);
    g_ncall = 0;
    g_want = 3;
    TF_EQ(pg_seek(p, match_id, &g_want, 1000), RQ_OK);
    TF_EQ(g_ncall, 3);
    TF_STREQ(g_keys[0], "1");
    AT(p, 3, 1);
    CONTIG(p);
    TF_EQ(pg_at_end(p), 0);

    /* No match anywhere: the walk ends where it started, the start row
       tested last. */
    pg_close(p);
    p = start(&card_src, 25);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 4; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, 5, 1);
    g_ncall = 0;
    g_want = 999;
    TF_EQ(pg_seek(p, match_id, &g_want, 1000), RQ_OK);
    TF_EQ(g_ncall, NCARD);
    TF_STREQ(g_keys[0], "6");
    TF_STREQ(g_keys[NCARD - 1], "5");
    AT(p, 5, 1);
    pg_close(p);

    /* A failed fetch ends the seek with the code. */
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    g_nfail = 0;
    g_ncall = 0;
    g_want = 999;
    script(BUSY, NULL, NULL);
    tl_clear();
    TF_EQ(pg_seek(p, match_id, &g_want, 1000), 402);
    unscript();
    TF_EQ(g_nfail, 1);
    TF_EQ(g_ncall, 9);
    TF_EQ(tl_count(), 1);
    pg_close(p);

    /* The wait function stops a seek that needs a fetch of several
       requests: ITEM FIND has ten rows, the seek comes round to the
       first page and its second request is the call that stops. */
    p = start(&find_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    g_nwait = 0;
    g_wstop = 1;
    g_ncall = 0;
    g_want = 999;
    TF_EQ(pg_seek(p, match_id, &g_want, 1000), RQ_OK);
    TF_EQ(p->stopped, 1);
    TF_CHECK(g_ncall >= 9);
    TF_EQ(g_nwait, 1);
    g_wstop = 0;
    /* The next seek starts afresh: the stop is not taken for its own. */
    g_ncall = 0;
    TF_EQ(pg_seek(p, match_id, &g_want, 3), RQ_OK);
    TF_EQ(g_ncall, 3);
    TF_EQ(p->stopped, 0);
    pg_close(p);

    /* An empty list stays as it is. */
    p = start(&none_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    g_ncall = 0;
    TF_EQ(pg_seek(p, match_id, &g_want, 1000), RQ_OK);
    TF_EQ(g_ncall, 0);
    TF_EQ(pg_rows(p), 0);
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * pg_lead.
 * --------------------------------------------------------------------- */
static const char k_leadkey[] = "0";
static const char k_leadtext[] = "(lead)";

/* 1 when the text of row i is the lead's, padded. */
static int is_lead(pg *p, u16 i)
{
    char t[LV_ROWTEXT + 1];

    pg_text(p, i, t);
    return strncmp(t, k_leadtext, sizeof k_leadtext - 1) == 0 &&
           t[sizeof k_leadtext - 1] == ' ' && strlen(t) == LV_ROWTEXT;
}

static void test_lead(void)
{
    char key[LV_KEYSZ];
    pg *p;
    u16 i;

    /* The first page starts with the lead row and asks for one row
       less; the second page holds rows of the command only. */
    p = start(&card_src, 10);
    if (p == NULL)
        return;
    pg_lead(p, k_leadkey, k_leadtext);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    REQ(0, "ITEM LIST CAT=CARD ORDER=ID", 9, "");
    TF_EQ(tl_count(), 1);
    TF_CHECK(is_lead(p, 0));
    TF_EQ(pg_selected(p, key), 1);
    TF_STREQ(key, k_leadkey);
    TF_EQ(row_id(p, 1), 1);
    TF_EQ(row_id(p, 9), 9);
    for (i = 0; i < 10; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, 10, 1);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    AT(p, NCARD, NCARD - 9);
    /* Back up to the first page: the lead row again, from the ring. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_CHECK(is_lead(p, 0));
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    for (i = 0; i < 25; i++)
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
    TF_EQ(p->sel, 0);
    TF_CHECK(is_lead(p, 0));
    /* A reload keeps it. */
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_CHECK(is_lead(p, 0));
    TF_EQ(row_id(p, 1), 1);
    pg_close(p);

    /* A list without rows has the lead row alone. */
    p = start(&none_src, 10);
    if (p == NULL)
        return;
    pg_lead(p, k_leadkey, k_leadtext);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_rows(p), 1);
    TF_CHECK(is_lead(p, 0));
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_EQ(pg_rows(p), 1);
    TF_EQ(pg_selected(p, key), 1);
    TF_STREQ(key, k_leadkey);
    pg_close(p);

    /* A window of one row has none. */
    p = start(&card_src, 1);
    if (p == NULL)
        return;
    pg_lead(p, k_leadkey, k_leadtext);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    AT(p, 1, 1);
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * An empty list, a list of exactly one page and one of a row more.
 * --------------------------------------------------------------------- */
static void test_sizes(void)
{
    char key[LV_KEYSZ];
    pg *p;
    u16 i;

    /* Empty: every move is fine and holds nothing. */
    p = start(&none_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    /* No row matches, and the server's bounded scan ends the first
       request before the list does. */
    TF_EQ(tl_count(), 2);
    TF_STREQ(tl_req(0), "ITEM LIST CAT=NONE PAGE=10");
    TF_CHECK(tf_starts(tl_req(1), "ITEM LIST CAT=NONE PAGE=10 AFTER="));
    TF_EQ(g_nwait, 1);
    TF_EQ(g_wn[0], 0);
    TF_EQ(pg_rows(p), 0);
    TF_EQ(pg_at_end(p), 1);
    strcpy(key, "junk");
    TF_EQ(pg_selected(p, key), 0);
    TF_STREQ(key, "");
    for (i = PG_HOME; i <= PG_RELOAD; i++) {
        TF_EQ(pg_move(p, i), RQ_OK);
        TF_EQ(pg_rows(p), 0);
        TF_EQ(p->first, 0);
        TF_EQ(p->sel, 0);
        TF_EQ(p->stopped, 0);
        TF_EQ(pg_selected(p, key), 0);
    }
    TF_EQ(g_nfail, 0);
    pg_close(p);

    /* Exactly one page: CARD has 20 items. */
    p = start(&card_src, NCARD);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_rows(p), NCARD);
    AT(p, 1, 1);
    for (i = 2; i <= NCARD; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, NCARD, 1);
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, NCARD, 1);
    TF_EQ(pg_rows(p), NCARD);
    TF_EQ(pg_at_end(p), 1);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    AT(p, NCARD, 1);
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    AT(p, 1, 1);
    TF_EQ(pg_move(p, PG_UP), RQ_OK);
    AT(p, 1, 1);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    AT(p, NCARD, 1);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    AT(p, NCARD, 1);
    TF_EQ(pg_rows(p), NCARD);
    CONTIG(p);
    pg_close(p);

    /* A row more than a page. */
    p = start(&card_src, NCARD - 1);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_rows(p), NCARD - 1);
    TF_EQ(pg_at_end(p), 0);
    for (i = 2; i <= NCARD - 1; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, NCARD - 1, 1);
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, NCARD, 2);
    TF_EQ(pg_rows(p), NCARD);
    TF_EQ(pg_at_end(p), 1);
    CONTIG(p);
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    AT(p, NCARD, 2);
    TF_EQ(pg_move(p, PG_UP), RQ_OK);
    AT(p, NCARD - 1, 2);
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    AT(p, 1, 1);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    AT(p, NCARD, 2);
    CONTIG(p);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    AT(p, NCARD, 2);
    TF_EQ(pg_at_end(p), 1);
    TF_EQ(p->first + p->ps, pg_rows(p));
    pg_close(p);

    /* A window of one row. */
    p = start(&card_src, 1);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    AT(p, 1, 1);
    for (i = 2; i <= 5; i++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        AT(p, i, i);
    }
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    AT(p, 4, 4);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    AT(p, NCARD, NCARD);
    pg_close(p);

    /* The largest window, its pages filled by several requests. */
    p = start(&items_src, LV_MAXPAGE);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_rows(p), LV_MAXPAGE);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    AT(p, LV_MAXPAGE + 1, LV_MAXPAGE + 1);
    CONTIG(p);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    AT(p, NITEMS, NITEMS - LV_MAXPAGE + 1);
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * The rows above page A (the pager's above).
 *
 * Each count is checked against the list itself: the item numbers of
 * the list in order, taken with requests of the test's own (snap), and
 * the number in the pager's row 0, which is A's first row, or B's when A
 * holds none, with as many rows before it.  The rows before it are its
 * place in the list, one more with a lead row, none for the lead row.
 * --------------------------------------------------------------------- */
static u16 g_list[NITEMS];      /* item numbers of the list, in order   */
static u16 g_nlist;
static int g_haslead;           /* the list starts with the lead row    */

static int snap_line(void *arg, const rq_line *ln)
{
    TF_CHECK(arg == (void *)g_list);
    if (ln->kind == 'R' && ln->n > 0 && g_nlist < NITEMS)
        g_list[g_nlist++] = (u16)num_of(ln->v[0]);
    return 0;
}

/* The items of ITEM LIST in order (of CAT=cat when not NULL) into
   g_list, after a lead row with lead.  No script may be in force. */
static void snap(const char *order, const char *cat, int lead)
{
    char cur[LINK_MAXCURSOR + 1];
    rq q;
    rq_res res;

    g_nlist = 0;
    g_haslead = lead;
    cur[0] = '\0';
    do {
        rq_begin(&q, "ITEM LIST");
        if (cat != NULL)
            rq_pair(&q, "CAT", cat);
        rq_pair(&q, "ORDER", order);
        rq_pair(&q, "FIELDS", "ITEM_ID");
        rq_pairu(&q, "PAGE", (u32)LINK_PAGEMAX);
        if (cur[0] != '\0')
            rq_pair(&q, "AFTER", cur);
        if (!TF_EQ(rq_send(tc_lk, &q, snap_line, g_list, &res), RQ_OK))
            return;
        strcpy(cur, res.more);
    } while (cur[0] != '\0');
}

/* above is known and is the rows of the list before the pager's row 0. */
static void above_(pg *p, int line)
{
    u32 id;
    u16 i;

    if (!tf_check(pg_rows(p) > 0, TF_FILE, line, "rows held") ||
        !tf_check(p->above != PG_NOABOVE, TF_FILE, line, "above known"))
        return;
    if (g_haslead && is_lead(p, 0)) {
        tf_check_long((long)p->above, 0L, TF_FILE, line, "above the lead");
        return;
    }
    id = row_id(p, 0);
    for (i = 0; i < g_nlist && g_list[i] != id; i++)
        ;
    if (tf_check(i < g_nlist, TF_FILE, line, "row 0 in the list"))
        tf_check_long((long)p->above, (long)i + (g_haslead ? 1L : 0L),
                      TF_FILE, line, "rows above A");
}
#define ABOVE(p) above_((p), __LINE__)
#define UNKNOWN(p) TF_CHECK((p)->above == PG_NOABOVE)

/* Home, Down and Up row by row and Page Down and Up across many pages:
   B becoming A grows the count, the page above A fetched shrinks it. */
static void test_above_walk(void)
{
    pg *p;
    u16 i;

    snap("ID", NULL, 0);
    TF_EQ(g_nlist, NITEMS);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_CHECK(p->above == 0);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_CHECK(p->above == 0);
    ABOVE(p);
    for (i = 0; i < 50; i++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(key_id(p), 51);
    TF_CHECK(p->above >= 40);
    for (i = 0; i < 50; i++) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(key_id(p), 1);
    TF_CHECK(p->above == 0);
    for (i = 0; i < NITEMS / 10 + 2; i++) {
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(key_id(p), NITEMS);
    TF_EQ(pg_at_end(p), 1);
    TF_EQ(p->above + pg_rows(p), NITEMS);
    for (i = 0; i < NITEMS / 10 + 2; i++) {
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(key_id(p), 1);
    TF_CHECK(p->above == 0);

    /* A window across the boundary of A and B, moved both ways. */
    for (i = 0; i < 15; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    for (i = 0; i < 3; i++) {
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        ABOVE(p);
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        ABOVE(p);
    }
    for (i = 0; i < 4; i++) {
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        ABOVE(p);
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        ABOVE(p);
    }

    /* Home from far down: 0 again. */
    for (i = 0; i < 7; i++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_CHECK(p->above > 0);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_CHECK(p->above == 0);
    ABOVE(p);
    pg_close(p);
}

/* A new order goes to its first page: 0, and its own list after. */
static void test_above_order(void)
{
    pg *p;
    u16 i;

    snap("ID", NULL, 0);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 3; i++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    ABOVE(p);
    TF_CHECK(p->above > 0);
    TF_EQ(pg_move(p, PG_ORDER), RQ_OK);
    TF_STREQ(pg_order_name(p), "MFR");
    TF_CHECK(p->above == 0);
    snap("MFR", NULL, 0);
    TF_EQ(g_nlist, NITEMS);
    ABOVE(p);
    for (i = 0; i < 35; i++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        ABOVE(p);
    }
    for (i = 0; i < 3; i++) {
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    ABOVE(p);
    TF_EQ(p->above + pg_rows(p), NITEMS);
    for (i = 0; i < 25; i++) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(pg_move(p, PG_ORDER), RQ_OK);
    TF_STREQ(pg_order_name(p), "ID");
    TF_CHECK(p->above == 0);
    snap("ID", NULL, 0);
    ABOVE(p);
    pg_close(p);
}

/* End: the rows the walk passed before the pages shown, which with the
   rows held make the whole list; a stopped walk; a ring that dropped
   cursors and going up past them. */
static void test_above_end(void)
{
    pg *p;
    u16 i;
    u16 k;
    int noring;

    snap("ID", NULL, 0);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(p->above == NITEMS - 10);
    ABOVE(p);
    /* The rows held already end the list. */
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(p->above == NITEMS - 10);
    pg_close(p);

    /* A short last page under the page before it, from Home and from a
       window across A and B further down. */
    p = start(&items_src, 13);
    if (p == NULL)
        return;
    for (k = 0; k < 2; k++) {
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        if (k == 1) {
            TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
            TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
            TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
            TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        }
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        TF_EQ(pg_at_end(p), 1);
        ABOVE(p);
        TF_EQ(p->above + pg_rows(p), NITEMS);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        ABOVE(p);
        for (i = 0; i < 20; i++) {
            TF_EQ(pg_move(p, PG_UP), RQ_OK);
            ABOVE(p);
        }
    }
    pg_close(p);

    /* The walk stopped at its third wait: the page it reached, after
       two pages passed. */
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    g_nwait = 0;
    g_wstop = 3;
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_EQ(p->stopped, 1);
    TF_CHECK(p->above == 20);
    ABOVE(p);
    g_wstop = 0;
    for (i = 0; i < 12; i++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        ABOVE(p);
    }
    /* Stopped at its first wait: nothing passed, A where it was. */
    g_nwait = 0;
    g_wstop = 1;
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_EQ(p->stopped, 1);
    ABOVE(p);
    g_wstop = 0;
    for (i = 0; i < 25; i++) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        ABOVE(p);
    }
    pg_close(p);

    /* More pages than the ring holds: up through the cursors kept, then
       past them to the first page. */
    p = start(&items_src, RPS);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(p->above == NITEMS - RPS);
    ABOVE(p);
    for (i = 0; i < LV_MAXBACK; i++) {
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        ABOVE(p);
    }
    TF_CHECK(p->above > 0);
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    TF_EQ(row_id(p, 0), 1);
    TF_CHECK(p->above == 0);
    ABOVE(p);
    /* Row by row up past the ring's depth the same. */
    for (i = 0; i < 40; i++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    ABOVE(p);
    for (i = 0; i < (u16)(LV_MAXBACK * RPS + RPS); i++) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(row_id(p, 0), 1);
    pg_close(p);

    /* A stopped reload leaves A short (LINK_PAGEMAX of a page of 30
       rows), the last page follows it: End shows A whole above that
       page, the rows before A as they were. */
    p = start(&items_src, 30);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 7; i++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(row_id(p, 0), 7 * 30 + 1);
    g_nwait = 0;
    g_wstop = 1;
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    g_wstop = 0;
    TF_EQ(p->stopped, 1);
    TF_EQ(pg_rows(p), LINK_PAGEMAX);
    TF_CHECK(p->above == 7 * 30);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_EQ(key_id(p), NITEMS);
    TF_EQ(row_id(p, 0), 7 * 30 + 1);
    TF_CHECK(p->above == 7 * 30);
    ABOVE(p);
    pg_close(p);

    /* Home stopped short of a window of 30 and of 60, the short last
       page next: End shows the first page whole above it. */
    for (k = 30; k <= 60; k += 30) {
        p = start(&mk1_src, (u8)k);
        if (p == NULL)
            return;
        g_wstop = 1;
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        g_wstop = 0;
        TF_EQ(p->stopped, 1);
        TF_CHECK(pg_rows(p) < k && pg_rows(p) > 0);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        TF_EQ(key_id(p), 7 * (NMK1 - 1) + 1);
        TF_EQ(row_id(p, 0), 1);
        TF_CHECK(p->above == 0);
        TF_EQ(p->above + pg_rows(p), NMK1);
        pg_close(p);
    }
    /* ... and of the whole list, End walking many pages past it. */
    snap("ID", NULL, 0);
    for (k = 30; k <= 60; k += 30) {
        p = start(&items_src, (u8)k);
        if (p == NULL)
            return;
        g_wstop = 1;
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        g_wstop = 0;
        TF_EQ(pg_rows(p), LINK_PAGEMAX);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        TF_EQ(key_id(p), NITEMS);
        ABOVE(p);
        TF_EQ(p->above + pg_rows(p), NITEMS);
        pg_close(p);
    }

    /* Without memory for the ring the short last page is alone; End on
       it again changes nothing, and going up starts at the first page. */
    ring_watch(1, 0);
    p = start(&items_src, 13);
    if (p != NULL) {
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        TF_EQ(pg_rows(p), NITEMS % 13);
        TF_CHECK(p->above == NITEMS - NITEMS % 13);
        ABOVE(p);
        ring_unwatch();
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        TF_CHECK(p->above == NITEMS - NITEMS % 13);
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        TF_EQ(row_id(p, 0), 1);
        TF_CHECK(p->above == 0);
        pg_close(p);
    }
    ring_unwatch();

    /* The walk's last page holds none: the page before it is shown, from
       the ring or the walk's own cursor. */
    for (noring = 0; noring < 2; noring++) {
        ring_watch(noring, 0);
        p = start(&items_src, RPS);
        if (p == NULL)
            break;
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        script(NULL, "+DATA\nF ITEM_ID\n.", NULL);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        unscript();
        TF_EQ(row_id(p, 0), RPS + 1);
        TF_CHECK(p->above == RPS);
        ABOVE(p);
        pg_close(p);
    }
    ring_unwatch();
}

/* Reload: in place, rows gone (the page before shown, or the first
   page without a ring), and a list that ends sooner (the page before
   fetched under a short last page). */
static void test_above_reload(void)
{
    pg *p;
    u16 i;

    snap("ID", NULL, 0);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 13; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    ABOVE(p);
    for (i = 0; i < 3; i++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    ABOVE(p);

    /* The last two items gone: a last page of eight under the page
       before it. */
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(p->above == NITEMS - 10);
    item_op("DEL", NITEMS);
    item_op("DEL", NITEMS - 1);
    snap("ID", NULL, 0);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_EQ(pg_at_end(p), 1);
    TF_CHECK(p->above == NITEMS - 20);
    ABOVE(p);
    TF_EQ(p->above + pg_rows(p), NITEMS - 2);
    item_op("UNDEL", NITEMS);
    item_op("UNDEL", NITEMS - 1);
    snap("ID", NULL, 0);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    ABOVE(p);

    /* A reload whose page before A ends elsewhere than A's start (a
       row gone in it too): unknown, until Home. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(p->above == NITEMS - 10);
    item_op("DEL", NITEMS);
    item_op("DEL", NITEMS - 1);
    item_op("DEL", NITEMS - 14);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_EQ(pg_at_end(p), 1);
    UNKNOWN(p);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_CHECK(p->above == 0);
    item_op("UNDEL", NITEMS - 14);
    item_op("UNDEL", NITEMS - 1);
    item_op("UNDEL", NITEMS);

    /* The last two gone and the reload's fetch of the page before
       failing: unchanged; End then counts from its own walk. */
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(p->above == NITEMS - 10);
    item_op("DEL", NITEMS);
    item_op("DEL", NITEMS - 1);
    snap("ID", NULL, 0);
    script(NULL, BUSY, NULL);
    TF_EQ(pg_move(p, PG_RELOAD), 402);
    unscript();
    ABOVE(p);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(p->above == NITEMS - 20);
    ABOVE(p);
    TF_EQ(p->above + pg_rows(p), NITEMS - 2);
    item_op("UNDEL", NITEMS);
    item_op("UNDEL", NITEMS - 1);
    snap("ID", NULL, 0);

    /* All of A's rows gone: the page before from the ring. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(p->above == NITEMS - 10);
    for (i = NITEMS - 9; i <= NITEMS; i++)
        item_op("DEL", i);
    snap("ID", NULL, 0);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_EQ(row_id(p, 0), NITEMS - 19);
    TF_CHECK(p->above == NITEMS - 20);
    ABOVE(p);
    for (i = NITEMS - 9; i <= NITEMS; i++)
        item_op("UNDEL", i);
    snap("ID", NULL, 0);
    pg_close(p);

    /* All of A's rows gone and the page before, a row gone in it too,
       ending the list short of A's start: unknown, until Home. */
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(p->above == NITEMS - 10);
    for (i = NITEMS - 9; i <= NITEMS; i++)
        item_op("DEL", i);
    item_op("DEL", NITEMS - 14);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_EQ(row_id(p, 0), NITEMS - 29);
    UNKNOWN(p);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_CHECK(p->above == 0);
    for (i = NITEMS - 9; i <= NITEMS; i++)
        item_op("UNDEL", i);
    item_op("UNDEL", NITEMS - 14);
    pg_close(p);

    /* The same when the page before is the first page: 0. */
    snap("ID", "CARD", 0);
    p = start(&card_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(row_id(p, 0), 11);
    TF_CHECK(p->above == 10);
    for (i = 11; i <= NCARD; i++)
        item_op("DEL", i);
    item_op("DEL", 5);
    snap("ID", "CARD", 0);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    TF_EQ(row_id(p, 0), 1);
    TF_CHECK(p->above == 0);
    ABOVE(p);
    for (i = 11; i <= NCARD; i++)
        item_op("UNDEL", i);
    item_op("UNDEL", 5);
    snap("ID", NULL, 0);
    pg_close(p);

    /* All of A's rows gone without a ring: the first page. */
    ring_watch(1, 0);
    p = start(&items_src, 13);
    if (p != NULL) {
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        TF_CHECK(p->above == NITEMS - NITEMS % 13);
        for (i = NITEMS - NITEMS % 13 + 1; i <= NITEMS; i++)
            item_op("DEL", i);
        snap("ID", NULL, 0);
        TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
        TF_EQ(row_id(p, 0), 1);
        TF_CHECK(p->above == 0);
        ABOVE(p);
        for (i = NITEMS - NITEMS % 13 + 1; i <= NITEMS; i++)
            item_op("UNDEL", i);
        snap("ID", NULL, 0);
        pg_close(p);
    }
    ring_unwatch();
}

/* A seek across pages, and its wrap to the first row. */
static void test_above_seek(void)
{
    pg *p;

    snap("ID", NULL, 0);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    g_ncall = 0;
    g_want = 47;
    TF_EQ(pg_seek(p, match_id, &g_want, 1000), RQ_OK);
    TF_EQ(key_id(p), 47);
    ABOVE(p);
    g_want = 999;
    TF_EQ(pg_seek(p, match_id, &g_want, 12), RQ_OK);
    ABOVE(p);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    g_want = 3;
    TF_EQ(pg_seek(p, match_id, &g_want, 1000), RQ_OK);
    TF_EQ(key_id(p), 3);
    TF_CHECK(p->above == 0);
    ABOVE(p);
    pg_close(p);
}

/* Failed fetches leave the count as it was, but for a B that became A
   before the failure; a bad cursor's restart sets 0. */
static void test_above_fail(void)
{
    static const int mv[] = {
        PG_PGUP, PG_UP, PG_RELOAD, PG_HOME, PG_END, PG_PGDN
    };
    pg *p;
    u16 i;

    snap("ID", NULL, 0);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_CHECK(p->above == 20);
    for (i = 0; i < sizeof mv / sizeof mv[0]; i++) {
        script(BUSY, NULL, NULL);
        TF_EQ(pg_move(p, mv[i]), 402);
        unscript();
        TF_CHECK(p->above == 20);
        ABOVE(p);
    }
    /* A bad cursor the fail function leaves the list on: unchanged. */
    script(BADCUR, NULL, NULL);
    TF_EQ(pg_move(p, PG_PGDN), RQ_ECURSOR);
    unscript();
    TF_CHECK(p->above == 20);
    ABOVE(p);

    /* B becomes A, then the fetch of the page after it fails. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 10; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    script(BUSY, NULL, NULL);
    TF_EQ(pg_move(p, PG_PGDN), 402);
    unscript();
    ABOVE(p);

    /* Restarts after a bad cursor, from a Page Down, a reload and the
       End walk. */
    g_fstay = 1;
    for (i = 0; i < 3; i++) {
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        TF_CHECK(p->above == 20);
        script(i == 2 ? NULL : BADCUR, i == 2 ? BADCUR : NULL, NULL);
        TF_EQ(pg_move(p, i == 0 ? PG_PGDN : i == 1 ? PG_RELOAD : PG_END),
              RQ_OK);
        unscript();
        TF_EQ(row_id(p, 0), 1);
        TF_CHECK(p->above == 0);
        ABOVE(p);
    }
    g_fstay = 0;
    pg_close(p);
}

/* The list changed under a move up: unknown, through growing and
   shrinking, a reload and End, until a move loads the first page (a
   move up onto it included). */
static void test_above_changed(void)
{
    static const char r11[] = "+DATA\nF ITEM_ID\nR 11\nR 12\nR 13\nR 14\n"
                              "R 15\nR 16\nR 17\nR 18\nR 19\nR 20\n";
    char resp[LINK_MAXRESP];
    pg *p;
    u16 i;
    u16 m;

    cursors_of(10, 5);
    snap("ID", NULL, 0);
    sprintf(resp, "%sC %s\n.", r11, g_curs[3]);
    p = start(&items_src, 10);
    if (p == NULL)
        return;
    for (m = 0; m < 3; m++) {
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        ABOVE(p);
        /* The page above A ends at another cursor than A's start. */
        script(resp, NULL, NULL);
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        unscript();
        TF_EQ(row_id(p, 0), 11);
        UNKNOWN(p);
        if (m == 0) {
            /* B from the page found's cursor becomes A; the page above
               it ends elsewhere again; then the first page. */
            for (i = 0; i < 20; i++) {
                TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
                UNKNOWN(p);
            }
            TF_EQ(row_id(p, 0), 31);
            TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
            UNKNOWN(p);
            TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
            TF_EQ(row_id(p, 0), 11);
            UNKNOWN(p);
            TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
            TF_EQ(row_id(p, 0), 1);
            TF_CHECK(p->above == 0);
            ABOVE(p);
            for (i = 0; i < 25; i++) {
                TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
                ABOVE(p);
            }
            TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        } else if (m == 1) {
            TF_EQ(pg_move(p, PG_ORDER), RQ_OK);
            TF_CHECK(p->above == 0);
            snap("MFR", NULL, 0);
            ABOVE(p);
            TF_EQ(pg_move(p, PG_ORDER), RQ_OK);
            snap("ID", NULL, 0);
        } else {
            TF_EQ(pg_move(p, PG_END), RQ_OK);
            UNKNOWN(p);
            g_want = 3;
            TF_EQ(pg_seek(p, match_id, &g_want, 1000), RQ_OK);
            TF_EQ(key_id(p), 3);
        }
        TF_CHECK(p->above == 0);
        ABOVE(p);
    }
    pg_close(p);

    /* The page above ends the list, after a ring that dropped cursors:
       unknown up through the cursors kept, then the first page. */
    p = start(&items_src, RPS);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 40; i++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    ABOVE(p);
    script("+DATA\nF ITEM_ID\nR 196\nR 197\nR 198\nR 199\nR 200\n.", NULL,
           NULL);
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    unscript();
    UNKNOWN(p);
    for (i = 1; i < LV_MAXBACK; i++) {
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        UNKNOWN(p);
    }
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    TF_EQ(row_id(p, 0), 1);
    TF_CHECK(p->above == 0);
    ABOVE(p);
    pg_close(p);
}

/* A lead row counts as a row of the first page. */
static void test_above_lead(void)
{
    pg *p;
    u16 i;

    snap("ID", "CARD", 1);
    TF_EQ(g_nlist, NCARD);
    p = start(&card_src, 6);
    if (p == NULL)
        return;
    pg_lead(p, k_leadkey, k_leadtext);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_CHECK(is_lead(p, 0));
    ABOVE(p);
    for (i = 0; i < NCARD + 2; i++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(key_id(p), NCARD);
    for (i = 0; i < NCARD + 2; i++) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        ABOVE(p);
    }
    TF_CHECK(is_lead(p, 0));
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    ABOVE(p);
    TF_EQ(p->above + pg_rows(p), NCARD + 1);
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    ABOVE(p);
    for (i = 0; i < 4; i++) {
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        ABOVE(p);
    }
    TF_CHECK(is_lead(p, 0));
    for (i = 0; i < 4; i++) {
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        ABOVE(p);
    }
    pg_close(p);
}

/* The first page a stop left short: past it, then back up onto it,
   which the server now gives whole, ending elsewhere than A's start:
   the first page, so 0, and it stays 0. */
static void test_above_upfirst(void)
{
    pg *p;
    u16 i;

    snap("ID", NULL, 0);
    p = start(&items_src, 30);
    if (p == NULL)
        return;
    g_wstop = 1;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(p->stopped, 1);
    TF_EQ(pg_rows(p), LINK_PAGEMAX);
    g_wstop = 0;
    for (i = 0; i < LINK_PAGEMAX + 30 + 12; i++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(row_id(p, 0), LINK_PAGEMAX + 1);
    TF_CHECK(p->above == LINK_PAGEMAX);
    while (p->first > 0 || p->sel > 0)
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
    TF_EQ(pg_move(p, PG_UP), RQ_OK);
    TF_EQ(row_id(p, 0), 1);
    TF_CHECK(p->above == 0);
    ABOVE(p);
    for (i = 0; i < 40; i++) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        TF_CHECK(p->above == 0);
    }
    TF_EQ(key_id(p), 1);
    pg_close(p);
}

/* Survey the list of p with pg_fetch, which cuts the pager's own pages
   (the first ps - 1 rows with lead): their start cursors into g_curs
   from page 0.  Returns the pages. */
static u16 survey(pg *p)
{
    char cur[LV_CURSZ];
    rq_res res;
    u16 pages;
    u16 got;
    u8 more;

    pages = 0;
    cur[0] = '\0';
    more = 1;
    p->walk = 1;
    p->walked = 0;
    while (more && pages < MAXCUR) {
        strcpy(g_curs[pages], cur);
        pages++;
        if (!TF_EQ(pg_fetch(p, &res, cur, 0, &got, cur, &more), RQ_OK))
            more = 0;
        p->walked += got;
    }
    p->walk = 0;
    return pages;
}

/* The rows held, their marks, the window and the selection: recorded
   with check 0, compared with what was recorded with check 1. */
static void held(pg *p, int check)
{
    static char text[2 * LV_MAXPAGE][LV_ROWTEXT + 1];
    static int mark[2 * LV_MAXPAGE];
    static u16 n;
    static u16 first;
    static u16 sel;
    char t[LV_ROWTEXT + 1];
    u16 i;

    if (!check) {
        n = pg_rows(p);
        first = p->first;
        sel = p->sel;
        for (i = 0; i < n; i++)
            mark[i] = pg_text(p, i, text[i]);
        return;
    }
    TF_EQ(pg_rows(p), n);
    TF_EQ(p->first, first);
    TF_EQ(p->sel, sel);
    for (i = 0; i < n && i < pg_rows(p); i++) {
        TF_EQ(pg_text(p, i, t), mark[i]);
        TF_STREQ(t, text[i]);
    }
}

/* Jump to page k of the survey with above rows before it, the pages
   before it pushed on the ring. */
static void jump(pg *p, u16 k, u32 above)
{
    rq_res res;
    u16 j;

    p->stopped = 0;
    TF_EQ(pg_load(p, &res, g_curs[k], above), RQ_OK);
    TF_CHECK(p->above == above);
    for (j = 0; j < k; j++)
        pg_ring_push(p, g_curs[j]);
}

/* A page jump (PAGERINT.H): a survey's cursors, a page loaded with its
   count, the pages before it pushed; then the moves keep the count, up
   past the ring's depth to the restart, and End.  A lead row's first
   page holds a row less of the list.  A count below the page above is
   clamped at 0 when that page is fetched. */
static void test_above_jump(void)
{
    pg *p;
    u16 i;

    snap("ID", NULL, 0);
    p = start(&items_src, RPS);
    if (p == NULL)
        return;
    TF_EQ(survey(p), NITEMS / RPS);
    jump(p, 45, 45 * RPS);
    TF_EQ(row_id(p, 0), 45 * RPS + 1);
    ABOVE(p);
    for (i = 0; i < LV_MAXBACK + 2; i++) {
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(row_id(p, 0), 1);
    TF_CHECK(p->above == 0);
    jump(p, 45, 45 * RPS);
    for (i = 0; i < (u16)(LV_MAXBACK * RPS + RPS + 3); i++) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(row_id(p, 0), 1);
    jump(p, 20, 20 * RPS);
    for (i = 0; i < 30; i++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    ABOVE(p);
    TF_EQ(p->above + pg_rows(p), NITEMS);
    jump(p, 0, 0);
    ABOVE(p);

    /* The count clamped: page 2 loaded with 3 rows above it, fewer than
       the page above holds. */
    jump(p, 2, 3);
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    TF_EQ(row_id(p, 0), RPS + 1);
    TF_CHECK(p->above == 0);
    TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    TF_EQ(row_id(p, 0), 1);
    ABOVE(p);
    pg_close(p);

    snap("ID", "CARD", 1);
    p = start(&card_src, 6);
    if (p == NULL)
        return;
    pg_lead(p, k_leadkey, k_leadtext);
    /* A survey only counts: the rows held, A's and B's, stay as they
       were. */
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 6; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    held(p, 0);
    TF_EQ(survey(p), (NCARD + 1 + 5) / 6);
    held(p, 1);
    jump(p, 3, 3 * 6);
    TF_EQ(row_id(p, 0), 3 * 6);
    ABOVE(p);
    for (i = 0; i < NCARD + 2; i++) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        ABOVE(p);
    }
    TF_CHECK(is_lead(p, 0));
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    ABOVE(p);
    pg_close(p);
}

/* ---------------------------------------------------------------------
 * An unpaged list: the categories, NCATADD added to the engine's seeds.
 * --------------------------------------------------------------------- */
#define NCATADD         30      /* categories added                     */
static char g_cat[LINK_MAXCODES][LINK_CATLEN + 1];  /* CAT LIST's codes */
static u16 g_ncat;
static char g_nocat[] = "NONE";         /* no category's code           */

static int cat_line(void *arg, const rq_line *ln)
{
    TF_CHECK(arg == (void *)g_cat);
    if (ln->kind == 'R' && TF_CHECK(g_ncat < LINK_MAXCODES))
        strcpy(g_cat[g_ncat++], ln->v[0]);
    return 0;
}

/* Add the categories and read the list's codes, in its order. */
static void add_cats(void)
{
    char line[40];
    rq q;
    rq_res res;
    u16 i;

    for (i = 1; i <= NCATADD; i++) {
        sprintf(line, "CAT ADD U%02u CAT_NAME=N%u", (unsigned)i,
                (unsigned)i);
        TF_EQ(tc_send(line), RQ_OK);
    }
    g_ncat = 0;
    rq_begin(&q, "CAT LIST");
    TF_EQ(rq_send(tc_lk, &q, cat_line, g_cat, &res), RQ_OK);
    TF_EQ(res.more[0], '\0');
}

/* The window starts at the list's row top (the pager's numbering, a
   lead row first when lead), the selection is row sel, above + first is
   top, and every row held is the list's, from above on. */
static void cats_at_(pg *p, u16 sel, u16 top, int lead, int line)
{
    char t[LV_ROWTEXT + 1];
    u16 n;
    u16 i;
    u32 j;

    tf_check_long((long)(p->above + p->first), (long)top, TF_FILE, line,
                  "window top");
    tf_check_long((long)(p->above + p->sel), (long)sel, TF_FILE, line,
                  "selection");
    tf_check(p->first <= p->sel && p->sel < p->first + p->ps, TF_FILE,
             line, "selection in window");
    n = pg_rows(p);
    for (i = 0; i < n; i++) {
        j = p->above + i;
        pg_text(p, i, t);
        if (j == 0 && lead) {
            if (!tf_check(is_lead(p, i), TF_FILE, line, "lead row"))
                return;
        } else if (!tf_check(j < (u32)g_ncat + (lead ? 1 : 0) &&
                             strncmp(t, g_cat[j - (lead ? 1 : 0)],
                                     strlen(g_cat[j - (lead ? 1 : 0)]))
                             == 0, TF_FILE, line, "row of the list")) {
            return;
        }
    }
}
#define CATS_AT(p, sel, top, lead) \
    cats_at_((p), (u16)(sel), (u16)(top), (lead), __LINE__)

/* pg_seek's match of the category whose code is arg. */
static int match_cat(void *arg, const char *key, const char *text)
{
    TF_CHECK(strncmp(text, key, strlen(key)) == 0);
    return strcmp(key, (const char *)arg) == 0;
}

/* Every request since tl_clear was the bare CAT LIST, n of them. */
static void bare_(u16 n, int line)
{
    u16 i;

    tf_check_long((long)tl_count(), (long)n, TF_FILE, line, "requests");
    for (i = 0; i < tl_count(); i++)
        if (!tf_check_str(tl_req(i), "CAT LIST", TF_FILE, line,
                          "request"))
            return;
}
#define BARE(n) bare_((n), __LINE__)

static void test_unpaged(void)
{
    pg *p;
    u16 i;

    add_cats();
    if (!TF_EQ(g_ncat, 10 + NCATADD))
        return;

    /* Pages of 6: one bare request a fill, its rows cut from the
       response at the offset its cursor gives. */
    p = start(&cat_src, 6);
    if (p == NULL)
        return;
    pg_unpaged(p);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    BARE(1);
    TF_EQ(pg_rows(p), 6);
    TF_EQ(pg_at_end(p), 0);
    CATS_AT(p, 0, 0, 0);
    tl_clear();
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    BARE(1);
    CATS_AT(p, 6, 6, 0);
    for (i = 0; i < 9; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    CATS_AT(p, 15, 10, 0);
    for (i = 0; i < 5; i++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    CATS_AT(p, g_ncat - 1, g_ncat - 6, 0);
    TF_EQ(pg_at_end(p), 1);
    tl_clear();
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    BARE(0);
    CATS_AT(p, g_ncat - 1, g_ncat - 6, 0);
    /* Up through the ring's cursors, above kept. */
    for (i = 0; i < 7; i++)
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    CATS_AT(p, 0, 0, 0);
    TF_CHECK(p->above != PG_NOABOVE);
    /* End walks the list a page a request, then shows its last rows. */
    tl_clear();
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(tl_count() >= g_ncat / 6);
    BARE(tl_count());
    CATS_AT(p, g_ncat - 1, g_ncat - 6, 0);
    for (i = 0; i < 8; i++)
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
    CATS_AT(p, g_ncat - 9, g_ncat - 9, 0);
    tl_clear();
    TF_EQ(pg_move(p, PG_RELOAD), RQ_OK);
    CATS_AT(p, g_ncat - 9, g_ncat - 9, 0);
    pg_close(p);

    /* A list ending with a full page ends there, without a request for
       the page after it. */
    p = start(&cat_src, 8);
    if (p == NULL)
        return;
    pg_unpaged(p);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    for (i = 0; i < 4; i++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    CATS_AT(p, 32, 32, 0);
    TF_EQ(pg_at_end(p), 1);
    tl_clear();
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    BARE(0);
    CATS_AT(p, g_ncat - 1, 32, 0);
    pg_close(p);

    /* A window past LINK_PAGEMAX rows takes one request a fill. */
    p = start(&cat_src, 30);
    if (p == NULL)
        return;
    pg_unpaged(p);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    BARE(1);
    TF_EQ(pg_rows(p), 30);
    CATS_AT(p, 0, 0, 0);
    tl_clear();
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    BARE(1);
    CATS_AT(p, 30, g_ncat - 30, 0);
    pg_close(p);

    /* A seek walks down the pages to its row, each fill a bare request
       and the row found the window's last, and comes round to the row it
       started on when no row matches. */
    p = start(&cat_src, 6);
    if (p == NULL)
        return;
    pg_unpaged(p);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    tl_clear();
    TF_EQ(pg_seek(p, match_cat, g_cat[g_ncat - 2], LINK_MAXCODES + 1),
          RQ_OK);
    BARE(tl_count());
    TF_CHECK(tl_count() >= g_ncat / 6 - 1);
    CATS_AT(p, g_ncat - 2, g_ncat - 7, 0);
    TF_EQ(pg_seek(p, match_cat, g_nocat, LINK_MAXCODES + 1), RQ_OK);
    CATS_AT(p, g_ncat - 2, g_ncat - 7, 0);
    pg_close(p);

    /* A lead row: the first page holds it and one row of the list
       less, the next starts a row earlier in the list. */
    p = start(&cat_src, 6);
    if (p == NULL)
        return;
    pg_unpaged(p);
    pg_lead(p, k_leadkey, k_leadtext);
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    CATS_AT(p, 0, 0, 1);
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    CATS_AT(p, 6, 6, 1);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    CATS_AT(p, g_ncat, g_ncat - 5, 1);
    for (i = 0; i < 7; i++)
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
    CATS_AT(p, 0, 0, 1);
    pg_close(p);
}

int main(int argc, char **argv)
{
    link_clock clk;
    char why[LINK_WHYSZ];
    int rc;

    tf_begin("T_PAGER");
    cmem_init();
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_PAGER dir\n");
        return 2;
    }
    strcpy(g_ddir, argv[1]);
    te_remove(g_ddir, NULL);
    tc_clock(&clk);
    rc = link_open_local(&tc_lk, g_ddir, &clk, why, sizeof why);
    if (!TF_EQ(rc, 0)) {
        printf("T_PAGER: %s\n", why);
        return tf_done();
    }
    if (!tc_admin())
        return tf_done();
    add_items();
    test_open();
    test_walk_pages();
    test_ring();
    test_ring_mem();
    test_ring_pack();
    test_ring_bytes();
    test_end();
    test_end_ring();
    test_end_noring();
    test_reload();
    test_orders();
    test_rows();
    test_fill();
    test_wait_walk();
    test_fail();
    test_cursor();
    test_seek();
    test_lead();
    test_sizes();
    test_above_walk();
    test_above_order();
    test_above_end();
    test_above_reload();
    test_above_seek();
    test_above_fail();
    test_above_changed();
    test_above_lead();
    test_above_upfirst();
    test_above_jump();
    test_unpaged();
    TF_EQ(link_close(tc_lk), 0);
    te_remove(g_ddir, NULL);
    return tf_done();
}
