/*
 * T_LISTV.C - the paged list view (SRC/CLIENT/LISTV.C) over the
 * engine, on the shadow screen.
 *
 * usage: T_LISTV dir [items] [msgdir]
 *
 * dir is a scratch data directory prefix ("OBJ/HOST/LDATA/", "C:\")
 * holding nothing but this test's files; its engine files are removed
 * first and last.  items is the number of items added (default
 * LV_TITEMS; at least what the scenarios need); msgdir is the directory
 * prefix of the built INV.MSG (default: the program's own directory).
 * The test links the recording link of TRLINK.H instead of LINK.C, so
 * the request sequences the view makes are checked exactly and a chosen
 * request can be made to fail.  Keys come from a key source; when a
 * scenario's keys run out, tui_key ends the program through the exit
 * function, which leaves with longjmp.
 *
 * Covered:
 * - the first page, Down and Up across the boundary of the two pages, PgDn
 *   and PgUp through the ring of start cursors and past LV_MAXBACK
 *   (restarting at the first page), End walking with the key field alone
 *   and then fetching the last page in full;
 * - a page filled by several requests (ITEM FIND with sparse matches)
 *   asking each for the rows still missing;
 * - reload after a delete keeping the row number, a bad cursor making the
 *   view start again, a failure that makes the screen leave (lv_leave),
 *   also of that new start;
 * - sort orders with their field lists and headers, an empty list, painting
 *   and lv_run, the selected row TA_SELECT only while lv_run reads the
 *   keys (TA_BOLD before it, once it has handed a key back, and under the
 *   error box of a move that failed inside it), the status information
 *   back under that box after an End walk's progress, the pending note
 *   and its display after a reload, the row search (lv_seek), F5
 *   fetching the rows again in lv_run, Tab left to the caller;
 * - Esc and Ctrl-Break typed during a fill and during the End walk
 *   (typed-ahead keys from a key source that answers key_peek): the
 *   progress shown, counting past 65535 rows in a long walk, the keys
 *   before the Esc dropped and those after it kept, the stop message, a
 *   move that asks for nothing more once stopped, PgDn going on from the
 *   stop;
 * - the link calling a callback that asked to stop no more, link_send
 *   then giving LINK_EOUTPUT;
 * - list columns and date columns, and windows taller than a protocol
 *   page (T_LISTV2.C).
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "LISTV.H"
#include "TRLINK.H"
#include "TEWIPE.H"
/* Checks keep no expression text: Turbo C stores every string literal
   again, and the near data of this test would pass the limit. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#define TF_KEYSRC
#define TK_EXIT0
#define TF_LINK
#include "TESTFW.H"
#include "T_LISTV.H"

#define PATHMAX         96
#define LV_TITEMS       250     /* items added by default               */
#define LV_MAXITEMS     270     /* most items: the End walk's requests
                                   must fit TL_LOGMAX                   */
#define FIND_EVERY      25      /* every n-th item matches ITEM FIND    */
#define NCARD           20      /* the first items are in CARD          */

/* Message numbers of the test's headers: any known message will do. */
#define HDR_ID          MT_NOTAVAIL
#define HDR_MFR         MT_NOTFOUND
#define HDR_EMPTY       MT_NOCHOICE

/* ---------------------------------------------------------------------
 * Requests of the test itself.
 * --------------------------------------------------------------------- */
u16 g_nitems;
static char g_ddir[PATHMAX];

/* "ITEM <verb> <id>" (verb DEL or UNDEL); the rq_send code. */
static int item_op(const char *verb, u32 id)
{
    char line[24];

    strcpy(line, "ITEM ");
    strcat(line, verb);
    strcat(line, " ");
    txt_u32(line + strlen(line), id);
    return tc_send(line);
}

static void add_range(u16 from, u16 n)
{
    char line[80];
    rq q;
    rq_res res;
    u16 i;

    for (i = from; i < from + n; i++) {
        rq_begin(&q, "ITEM ADD");
        rq_pair(&q, "CATEGORY", i <= NCARD ? "CARD" : "SYS");
        strcpy(line, "MK");
        txt_u32(line + 2, (u32)(i % 7));
        rq_pair(&q, "MFR", line);
        strcpy(line, i % FIND_EVERY == 0 ? "ZQ" : "X");
        txt_u32(line + strlen(line), i);
        rq_pair(&q, "MODEL", line);
        rq_pair(&q, "LOC_ID", "A1");
        if (!TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK))
            return;
    }
}

/* n more items after the first g_nitems. */
static void add_more(u16 n)
{
    add_range((u16)(g_nitems + 1), n);
}

/* ---------------------------------------------------------------------
 * List sources.
 * --------------------------------------------------------------------- */
static u16 g_fmt_calls;
static u8 g_fmt_order;          /* lv_order_index at the last row       */
static lv *g_cur_view;

static void fmt_item(void *arg, const lv_rec *r, char *text, char *key)
{
    TF_CHECK(arg == (void *)&g_nitems);
    g_fmt_calls++;
    if (g_cur_view != NULL)
        g_fmt_order = lv_order_index(g_cur_view);
    strcpy(key, lv_val(r, "ITEM_ID"));
    strcpy(text, lv_val(r, "ITEM_ID"));
    strcat(text, " ");
    strcat(text, lv_val(r, "MFR"));
    strcat(text, " ");
    strcat(text, lv_val(r, "MODEL"));
}

const lv_src items_src = {
    "ITEM LIST", 2, { "ID", "MFR", NULL, NULL },
    { NULL, "ITEM_ID,MFR", NULL, NULL },
    { HDR_ID, HDR_MFR, 0, 0 }, HDR_EMPTY, "ITEM_ID",
    NULL, fmt_item, &g_nitems, NULL
};

static u16 g_mark_calls;

/* Marks the rows of odd item numbers. */
static int mark_odd(void *arg, const lv_rec *r)
{
    TF_CHECK(arg == (void *)&g_nitems);
    g_mark_calls++;
    return (int)(strtoul(lv_val(r, "ITEM_ID"), NULL, 10) & 1);
}

static const lv_src mark_src = {
    "ITEM LIST", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { HDR_ID, 0, 0, 0 }, HDR_EMPTY, NULL,
    NULL, fmt_item, &g_nitems, mark_odd
};

static void find_args(void *arg, rq *q)
{
    TF_CHECK(arg == (void *)&g_nitems);
    rq_word(q, "ZQ");
}

static const lv_src find_src = {
    "ITEM FIND", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { HDR_ID, 0, 0, 0 }, HDR_EMPTY, NULL,
    find_args, fmt_item, &g_nitems, NULL
};

static void none_args(void *arg, rq *q)
{
    TF_CHECK(arg == (void *)&g_nitems);
    rq_pair(q, "CAT", "NONE");
}

static void card_args(void *arg, rq *q)
{
    TF_CHECK(arg == (void *)&g_nitems);
    rq_pair(q, "CAT", "CARD");
}

static const lv_src card_src = {
    "ITEM LIST", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { HDR_ID, 0, 0, 0 }, HDR_EMPTY, "ITEM_ID",
    card_args, fmt_item, &g_nitems, NULL
};

static const lv_src none_src = {
    "ITEM LIST", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { HDR_ID, 0, 0, 0 }, HDR_EMPTY, NULL,
    none_args, fmt_item, &g_nitems, NULL
};

u32 sel_id(lv *v)
{
    char key[LV_KEYSZ];

    if (!lv_selected(v, key))
        return 0;
    return (u32)strtoul(key, NULL, 10);
}

/* The AFTER= cursor of request line s, or "". */
const char *after_of(const char *s)
{
    const char *p;

    p = strstr(s, " AFTER=");
    return p == NULL ? "" : p + 7;
}

#define PS              10      /* rows of a page in most scenarios     */

static void test_first_pages(void)
{
    lv *v;
    char c1[LINK_MAXCURSOR + 1];
    u16 i;

    v = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    g_cur_view = v;
    tl_clear();
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "ITEM LIST ORDER=ID PAGE=10");
    TF_EQ(lv_rows(v), PS);
    TF_EQ(sel_id(v), 1);
    TF_STREQ(lv_order_name(v), "ID");
    TF_EQ(lv_order_index(v), 0);

    /* Down within A costs nothing; past A it fetches B once. */
    for (i = 0; i < PS - 1; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(sel_id(v), PS);
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_CHECK(tf_starts(tl_req(1), "ITEM LIST ORDER=ID PAGE=10 AFTER="));
    strcpy(c1, after_of(tl_req(1)));
    TF_EQ(sel_id(v), PS + 1);
    TF_EQ(lv_rows(v), 2 * PS);
    /* Back up across the boundary: no request. */
    TF_EQ(lv_up(v), RQ_OK);
    TF_EQ(lv_up(v), RQ_OK);
    TF_EQ(sel_id(v), PS - 1);
    TF_EQ(tl_count(), 2);
    /* Down until the window lies wholly in B: B becomes A. */
    for (i = 0; i < 2 * PS; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), 3 * PS - 1);
    TF_EQ(tl_count(), 3);
    TF_CHECK(tf_starts(tl_req(2), "ITEM LIST ORDER=ID PAGE=10 AFTER="));
    TF_CHECK(strcmp(after_of(tl_req(2)), c1) != 0);

    /* Up to the top again: one request for the first page, from the
       ring (the first page's cursor is "", so no AFTER=). */
    for (i = 0; i < 3 * PS; i++)
        TF_EQ(lv_up(v), RQ_OK);
    TF_EQ(sel_id(v), 1);
    TF_EQ(lv_up(v), RQ_OK);
    TF_EQ(sel_id(v), 1);
    TF_EQ(tl_count(), 4);
    TF_STREQ(tl_req(3), "ITEM LIST ORDER=ID PAGE=10");

    /* Painting outside lv_run: header, rows, the selection bold. */
    lv_paint(v);
    TF_STREQ(tk_row(2) + 1, msg(HDR_ID));
    TF_STREQ(tk_row(3), " 1 MK1 X1");
    TF_EQ(tk_attr(3, 1), tui_attr(TA_BOLD));
    TF_EQ(tk_attr(4, 1), tui_attr(TA_NORMAL));

    /* PgDn moves a page; PgUp comes back. */
    tl_clear();
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(sel_id(v), PS + 1);
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(sel_id(v), 2 * PS + 1);
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), PS + 1);
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), 1);
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), 1);
    for (i = 0; i < tl_count(); i++)
        TF_CHECK(tf_starts(tl_req(i), "ITEM LIST ORDER=ID PAGE=10"));

    /* End: a walk asking for the key field alone, then the last page in
       full; it ends on the last item. */
    tl_clear();
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(sel_id(v), g_nitems);
    TF_EQ(tl_count(), (g_nitems + PS - 1) / PS);
    for (i = 0; i + 1 < tl_count(); i++)
        TF_CHECK(tf_starts(tl_req(i), "ITEM LIST ORDER=ID FIELDS=ITEM_ID "
                           "PAGE=10 AFTER="));
    TF_CHECK(!tl_full());
    TF_CHECK(tf_starts(tl_req((u16)(tl_count() - 1)),
                       "ITEM LIST ORDER=ID PAGE=10 AFTER="));
    strcpy(c1, after_of(tl_req((u16)(tl_count() - 1))));
    TF_STREQ(c1, after_of(tl_req((u16)(tl_count() - 2))));
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), g_nitems);
    /* End again on the last page: nothing to walk. */
    tl_clear();
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(tl_count(), 0);
    /* Up from the last page uses the ring the walk filled. */
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), (u32)(g_nitems - PS));
    TF_EQ(tl_count(), 1);
    TF_CHECK(tf_starts(tl_req(0), "ITEM LIST ORDER=ID PAGE=10 AFTER="));

    /* Sort orders: the field list and the header follow the order. */
    tl_clear();
    g_fmt_order = 9;
    TF_EQ(lv_set_order(v, "MFR"), 1);
    TF_EQ(lv_home(v), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM LIST ORDER=MFR FIELDS=ITEM_ID,MFR PAGE=10");
    TF_STREQ(lv_order_name(v), "MFR");
    TF_EQ(lv_order_index(v), 1);
    TF_EQ(g_fmt_order, 1);
    TF_STREQ(tk_row(2) + 1, msg(HDR_MFR));
    TF_EQ(lv_set_order(v, "SERIAL"), 0);
    TF_STREQ(lv_order_name(v), "MFR");
    TF_EQ(tl_count(), 1);
    TF_EQ(lv_set_order(v, "ID"), 1);
    TF_EQ(lv_home(v), RQ_OK);
    TF_STREQ(tl_req(1), "ITEM LIST ORDER=ID PAGE=10");
    g_cur_view = NULL;
    lv_close(v);
}

/* A source with a mark function: marked rows are drawn TA_ALERT
   unless selected, and the mark moves with its row. */
static void test_marked(void)
{
    lv *v;
    u16 i;

    v = lv_open(tc_lk, &mark_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    g_cur_view = v;
    g_mark_calls = 0;
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(g_mark_calls, PS);
    lv_paint(v);
    TF_STREQ(tk_row(3), " 1 MK1 X1");
    TF_EQ(tk_attr(3, 1), tui_attr(TA_BOLD));
    TF_EQ(tk_attr(4, 1), tui_attr(TA_NORMAL));
    TF_EQ(tk_attr(5, 1), tui_attr(TA_ALERT));
    TF_EQ(tk_attr(5, 59), tui_attr(TA_ALERT));
    TF_EQ(tk_attr(6, 1), tui_attr(TA_NORMAL));
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(lv_down(v), RQ_OK);
    lv_paint(v);
    TF_EQ(tk_attr(3, 1), tui_attr(TA_ALERT));
    TF_EQ(tk_attr(5, 1), tui_attr(TA_BOLD));
    /* Scroll one row past the first page: the rows move up, marks
       with them, and the new last row has its own mark. */
    for (i = 2; i < PS; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), PS + 1);
    lv_paint(v);
    TF_STREQ(tk_row(3), " 2 MK2 X2");
    TF_EQ(tk_attr(3, 1), tui_attr(TA_NORMAL));
    TF_EQ(tk_attr(4, 1), tui_attr(TA_ALERT));
    TF_EQ(tk_attr((u8)(2 + PS), 1), tui_attr(TA_BOLD));
    TF_EQ(lv_up(v), RQ_OK);
    lv_paint(v);
    TF_EQ(tk_attr((u8)(2 + PS), 1), tui_attr(TA_ALERT));
    TF_EQ(tk_attr((u8)(1 + PS), 1), tui_attr(TA_BOLD));
    lv_close(v);
    g_cur_view = NULL;
}

/* A two-row window, so the ring of LV_MAXBACK cursors overflows. */
static void test_ring(void)
{
    lv *v;
    u16 i;
    u16 pages;

    v = lv_open(tc_lk, &items_src, 2, 1, 3, 40);
    pages = LV_MAXBACK + 8;
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    for (i = 0; i < pages; i++)
        TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(sel_id(v), (u32)(2 * pages + 1));
    /* LV_MAXBACK pages back come from the ring ... */
    tl_clear();
    for (i = 0; i < LV_MAXBACK; i++)
        TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), (u32)(2 * (pages - LV_MAXBACK) + 1));
    TF_EQ(tl_count(), LV_MAXBACK);
    for (i = 0; i < tl_count(); i++)
        TF_CHECK(tf_starts(tl_req(i), "ITEM LIST ORDER=ID PAGE=2 AFTER="));
    /* ... and above the oldest kept the view starts at the first page. */
    tl_clear();
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "ITEM LIST ORDER=ID PAGE=2");
    TF_EQ(sel_id(v), 1);
    lv_close(v);
}

/* ITEM FIND reads a bounded number of records per request, so a page
   takes several requests, each asking for the rows still missing. */
static void test_sparse(void)
{
    lv *v;
    u16 matches;
    u16 want;
    u16 i;
    u16 rows;
    const char *p;

    v = lv_open(tc_lk, &find_src, 2, 1, PS + 1, 60);
    matches = (u16)(g_nitems / FIND_EVERY);
    want = matches < PS ? matches : PS;
    rows = 0;
    if (!TF_CHECK(v != NULL))
        return;
    tl_clear();
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(lv_rows(v), want);
    TF_CHECK(tl_count() >= 2);
    TF_STREQ(tl_req(0), "ITEM FIND ZQ PAGE=10");
    for (i = 1; i < tl_count(); i++) {
        p = tl_req(i);
        TF_CHECK(tf_starts(p, "ITEM FIND ZQ PAGE="));
        TF_CHECK(*after_of(p) != '\0');
    }
    /* The rows asked for go down by the rows found. */
    TF_CHECK(tf_starts(tl_req(1), "ITEM FIND ZQ PAGE=2 AFTER="));
    TF_EQ(sel_id(v), FIND_EVERY);
    for (i = 0; i < want; i++) {
        if (sel_id(v) == (u32)(FIND_EVERY * (i + 1)))
            rows++;
        lv_down(v);
    }
    TF_EQ(rows, want);
    lv_close(v);
}

static const char k_itemlist[] = "ITEM LIST";

/* 1 when the request line of len bytes (no NUL after them) is an ITEM
   LIST. */
int is_list(const char *line, u16 len)
{
    return len >= sizeof k_itemlist - 1 &&
           memcmp(line, k_itemlist, sizeof k_itemlist - 1) == 0;
}

/* tl_hook: answer the second request, the first page asked for again
   after a bad cursor, with -ERR 400. */
static void expire_second(const char *line, u16 len)
{
    if (tl_count() == 2 && is_list(line, len))
        tl_fail(2, "-ERR 400 HELLO or LOGIN required");
}

/* Reload after a delete keeps the row number; a bad cursor restarts,
   and a failure of that restart makes the screen leave. */
static void test_reload(void)
{
    static const u16 k1[] = { KEY_ENTER, KEY_NONE };
    lv *v;
    u16 i;

    v = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(lv_pgdn(v), RQ_OK);
    for (i = 0; i < 3; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), PS + 4);
    TF_EQ(item_op("DEL", PS + 4), RQ_OK);
    tl_clear();
    TF_EQ(lv_reload(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_CHECK(tf_starts(tl_req(0), "ITEM LIST ORDER=ID PAGE=10 AFTER="));
    TF_EQ(sel_id(v), PS + 5);
    TF_EQ(lv_up(v), RQ_OK);
    TF_EQ(sel_id(v), PS + 3);
    TF_EQ(item_op("UNDEL", PS + 4), RQ_OK);

    /* 204 on the next page: the view tells and starts again at the top. */
    tl_clear();
    tl_fail(1, "-ERR 204 bad cursor");
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_CHECK(tf_starts(tl_req(0), "ITEM LIST ORDER=ID PAGE=10 AFTER="));
    TF_STREQ(tl_req(1), "ITEM LIST ORDER=ID PAGE=10");
    TF_EQ(sel_id(v), 1);
    TF_STREQ(tk_row(TUI_HINTROW) + 1, msg(MT_RESTART));
    /* Another failure leaves the view as it was. */
    tl_clear();
    tl_fail(1, "-ERR 402 busy");
    TF_EQ(lv_pgdn(v), RQ_EBUSY);
    TF_EQ(sel_id(v), 1);
    TF_EQ(lv_rows(v), PS);
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(sel_id(v), PS + 1);
    TF_EQ(lv_leave(v), TUI_CMD_NONE);
    tl_clear();
    tl_fail(1, "-ERR 204 bad cursor");
    tl_hook = expire_second;
    tk_feed(k1, 0);
    if (setjmp(tk_jb) == 0)
        TF_EQ(lv_pgdn(v), RQ_ENOLOGIN);
    tl_hook = NULL;
    key_set_source(NULL, NULL);
    TF_EQ(tl_count(), 2);
    TF_EQ(tk_pos(), 1);
    TF_EQ(lv_leave(v), TUI_CMD_LOGIN);
    lv_close(v);
}

/* A failure that ends the session's login: lv_leave gives the command,
   lv_run returns Esc, and the next successful move clears it. */
static void test_leaving(void)
{
    static const u16 k1[] = { KEY_ENTER, KEY_NONE };
    static const u16 k2[] = { KEY_PGDN, KEY_ENTER, KEY_NONE };
    lv *v;
    u16 k;

    v = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    k = 0;
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    tl_clear();
    tl_fail(1, "-ERR 404 password change required");
    tk_feed(k1, 0);
    if (setjmp(tk_jb) == 0)
        TF_EQ(lv_pgdn(v), RQ_EMUSTCHG);
    TF_EQ(tk_pos(), 1);
    TF_EQ(lv_leave(v), TUI_CMD_LOGIN);
    TF_EQ(sel_id(v), 1);
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(lv_leave(v), TUI_CMD_NONE);
    tl_clear();
    tl_fail(1, "-ERR 404 password change required");
    tk_feed(k2, 0);
    if (setjmp(tk_jb) == 0)
        k = lv_run(v);
    TF_EQ(k, KEY_ESC);
    TF_EQ(lv_leave(v), TUI_CMD_LOGIN);
    key_set_source(NULL, NULL);
    lv_close(v);
}

static u8 g_box_attr;

/* tk_copy: the selected row's attribute and the status information
   while the error box is up. */
static void box_attr(void)
{
    g_box_attr = tk_attr(3, 1);
    txt_copy(g_seen, tui_info_text(), TUI_ST_INFOW + 1);
}

/* A move inside lv_run whose request fails with an error shown in a box:
   the selected row is bold under the box, the bar again after it.  An
   End walk whose first request fails: the status information its
   progress replaced is back under the box. */
static void test_fail_box(void)
{
    static const u16 k[] = { KEY_PGDN, KEY_ENTER, KEY_F4, KEY_NONE };
    static const u16 k_end[] = { KEY_END, KEY_ENTER, KEY_F4, KEY_NONE };
    static const char readerr[] = "-ERR 300 read error";
    lv *v;
    u16 key;

    v = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    key = 0;
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    tl_clear();
    tl_fail(1, readerr);
    tk_copy = box_attr;
    g_box_attr = 0;
    tk_feed(k, 2);
    if (setjmp(tk_jb) == 0)
        key = lv_run(v);
    tk_copy = NULL;
    key_set_source(NULL, NULL);
    TF_EQ(key, KEY_F4);
    TF_EQ(tk_pos(), 3);
    TF_EQ(lv_leave(v), TUI_CMD_NONE);
    TF_EQ(sel_id(v), 1);
    TF_EQ(g_box_attr, tui_attr(TA_BOLD));

    tui_info("");
    tl_clear();
    tl_fail(1, readerr);
    tk_copy = box_attr;
    g_seen[0] = 'x';
    tk_feed(k_end, 2);
    key = 0;
    if (setjmp(tk_jb) == 0)
        key = lv_run(v);
    tk_copy = NULL;
    key_set_source(NULL, NULL);
    TF_EQ(key, KEY_F4);
    TF_EQ(tl_count(), 1);
    TF_STREQ(g_seen, "");
    lv_close(v);
}

/* End where the cursor after the last rows leads to a page of none:
   the matches of a filter fill whole pages, or the last entries are
   deleted.  End stops on the last page with rows. */
static void test_end_cases(void)
{
    lv *v;
    u16 i;

    v = lv_open(tc_lk, &card_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(lv_rows(v), PS);
    TF_EQ(sel_id(v), NCARD);
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), NCARD - PS);
    lv_close(v);

    add_more(2);
    for (i = 1; i <= 2; i++)
        TF_EQ(item_op("DEL", (u32)(g_nitems + i)), RQ_OK);
    v = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(lv_rows(v), PS);
    TF_EQ(sel_id(v), g_nitems);
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), g_nitems);
    lv_close(v);
    /* A short last page: the window ends on the last row, the page
       before filling it; End again asks for nothing. */
    v = lv_open(tc_lk, &card_src, 2, 1, 8, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(sel_id(v), NCARD);
    TF_EQ(lv_rows(v), 7 + NCARD % 7);
    lv_paint(v);
    TF_CHECK(tf_starts(tk_row(3), " 14 "));
    TF_CHECK(tf_starts(tk_row(9), " 20 "));
    TF_EQ(tk_attr(9, 1), tui_attr(TA_BOLD));
    tl_clear();
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(tl_count(), 0);
    for (i = 0; i < 6; i++)
        TF_EQ(lv_up(v), RQ_OK);
    TF_EQ(sel_id(v), NCARD - 6);
    TF_EQ(tl_count(), 0);
    TF_EQ(lv_up(v), RQ_OK);
    TF_EQ(sel_id(v), NCARD - 7);
    /* The last row deleted: the window still ends on the last row. */
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(item_op("DEL", NCARD), RQ_OK);
    TF_EQ(lv_reload(v), RQ_OK);
    TF_EQ(sel_id(v), NCARD - 1);
    lv_paint(v);
    TF_CHECK(tf_starts(tk_row(3), " 13 "));
    TF_CHECK(tf_starts(tk_row(9), " 19 "));
    TF_EQ(tk_attr(9, 1), tui_attr(TA_BOLD));
    TF_EQ(item_op("UNDEL", NCARD), RQ_OK);
    lv_close(v);

    /* A full last page alone, as End leaves it: its last row deleted,
       the page before fills the window, which ends on the new last row,
       selected. */
    v = lv_open(tc_lk, &card_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(sel_id(v), NCARD);
    TF_EQ(item_op("DEL", NCARD), RQ_OK);
    TF_EQ(lv_reload(v), RQ_OK);
    TF_EQ(sel_id(v), NCARD - 1);
    lv_paint(v);
    TF_CHECK(tf_starts(tk_row(3), " 10 "));
    TF_CHECK(tf_starts(tk_row(PS + 2), " 19 "));
    TF_EQ(tk_attr(PS + 2, 1), tui_attr(TA_BOLD));
    TF_EQ(lv_up(v), RQ_OK);
    TF_EQ(sel_id(v), NCARD - 2);
    TF_EQ(item_op("UNDEL", NCARD), RQ_OK);
    lv_close(v);

    /* A list shorter than the window shows from its first row. */
    v = lv_open(tc_lk, &card_src, 2, 1, NCARD + 2, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    tl_clear();
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(tl_count(), 0);
    TF_EQ(sel_id(v), NCARD);
    lv_paint(v);
    TF_CHECK(tf_starts(tk_row(3), " 1 "));
    lv_close(v);
}

/* The row text fmt_item makes for item id (decimal in id). */
static const char *line_of(const char *id)
{
    static char t[40];
    u32 n;

    n = (u32)strtoul(id, NULL, 10);
    strcpy(t, id);
    strcat(t, " MK");
    txt_u32(t + strlen(t), n % 7);
    strcat(t, n % FIND_EVERY == 0 ? " ZQ" : " X");
    strcat(t, id);
    return t;
}

/* End with a full ring, failing on its last fetch: the ring keeps only
   the cursors the walk did not overwrite. */
static void test_ring_full_end(void)
{
    u16 ps;
    lv *v;
    u16 pages;
    char want[40];
    u16 i;

    ps = g_nitems % 3 != 0 ? 3 : 7;
    v = lv_open(tc_lk, &items_src, 2, 1, (u8)(ps + 1), 40);
    pages = (u16)(g_nitems / ps);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    for (i = 0; i + 1 < pages; i++)
        TF_EQ(lv_pgdn(v), RQ_OK);
    /* The second last page; the last holds g_nitems % ps rows, so End
       needs the page before it too, and that fetch fails. */
    TF_EQ(sel_id(v), (u32)(ps * (pages - 1) + 1));
    tl_clear();
    tl_fail(2, "-ERR 402 busy");
    TF_EQ(lv_end(v), RQ_EBUSY);
    TF_EQ(tl_count(), 2);
    TF_EQ(sel_id(v), (u32)(ps * (pages - 1) + 1));
    for (i = 0; i < LV_MAXBACK - 1; i++)
        TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), (u32)(ps * (pages - LV_MAXBACK) + 1));
    tl_clear();
    TF_EQ(lv_pgup(v), RQ_OK);
    strcpy(want, "ITEM LIST ORDER=ID PAGE=");
    txt_u32(want + strlen(want), ps);
    TF_STREQ(tl_req(0), want);
    TF_EQ(sel_id(v), 1);
    lv_close(v);
}

/* A reload finding the rows of A gone steps back a page; when that
   fails, the ring still holds the page's cursor. */
static void test_reload_back(void)
{
    lv *v;
    char cur[LINK_MAXLINE + 1];
    u16 i;

    v = lv_open(tc_lk, &card_src, 2, 1, 6, 40);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    for (i = 0; i < 3; i++)
        TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(sel_id(v), 16);
    for (i = 16; i <= NCARD; i++)
        TF_EQ(item_op("DEL", i), RQ_OK);
    /* The reload reads to the end of the index in two requests and finds
       nothing; the third, for the page before, fails. */
    tl_clear();
    tl_fail(3, "-ERR 402 busy");
    TF_EQ(lv_reload(v), RQ_EBUSY);
    TF_EQ(tl_count(), 3);
    strcpy(cur, tl_req(0));
    TF_CHECK(tf_starts(tl_req(2), "ITEM LIST CAT=CARD PAGE=5 AFTER="));
    TF_CHECK(strcmp(tl_req(2), cur) != 0);
    TF_EQ(lv_rows(v), 0);
    for (i = 16; i <= NCARD; i++)
        TF_EQ(item_op("UNDEL", i), RQ_OK);
    TF_EQ(lv_reload(v), RQ_OK);
    TF_EQ(sel_id(v), 16);
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), 11);
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), 6);
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), 1);
    lv_close(v);
}

/* A failure while the window shows rows of both pages keeps the view
   on what it still holds; a failed End keeps the ring. */
static void test_failures(void)
{
    lv *v;
    char line[12];
    u16 i;

    v = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    for (i = 0; i < PS + 2; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), PS + 3);
    tl_clear();
    tl_fail(1, "-ERR 402 busy");
    TF_EQ(lv_home(v), RQ_EBUSY);
    TF_EQ(lv_rows(v), PS);
    TF_EQ(sel_id(v), PS);
    lv_paint(v);
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), PS + 1);
    TF_EQ(lv_rows(v), 2 * PS);
    tl_clear();
    tl_fail(1, "-ERR 402 busy");
    TF_EQ(lv_reload(v), RQ_EBUSY);
    TF_EQ(sel_id(v), PS);

    /* End failing in its walk: the ring still leads back up. */
    TF_EQ(lv_home(v), RQ_OK);
    for (i = 0; i < 3; i++)
        TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(sel_id(v), 3 * PS + 1);
    tl_clear();
    tl_fail(2, "-ERR 402 busy");
    TF_EQ(lv_end(v), RQ_EBUSY);
    TF_EQ(sel_id(v), 3 * PS + 1);
    tl_clear();
    for (i = 0; i < 3; i++)
        TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), 1);
    TF_STREQ(tl_req((u16)(tl_count() - 1)), "ITEM LIST ORDER=ID PAGE=10");
    TF_EQ(tl_count(), 3);

    /* A reload whose second page fails: the selection moves back into
       the rows still held, and stays usable. */
    TF_EQ(lv_home(v), RQ_OK);
    for (i = 0; i < PS + 5; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), PS + 6);
    tl_clear();
    tl_fail(2, "-ERR 402 busy");
    TF_EQ(lv_reload(v), RQ_EBUSY);
    TF_EQ(lv_rows(v), PS);
    TF_EQ(sel_id(v), PS);
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), PS + 1);

    /* PgDn on the last page of an exact multiple keeps a full window. */
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_EQ(sel_id(v), (u32)(g_nitems - PS));
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(sel_id(v), g_nitems);
    lv_paint(v);
    txt_u32(line, (u32)(g_nitems - PS + 1));
    TF_STREQ(tk_row(3) + 1, line_of(line));
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(sel_id(v), g_nitems);
    lv_paint(v);
    TF_STREQ(tk_row(3) + 1, line_of(line));
    lv_close(v);
}

static void test_empty(void)
{
    lv *v;
    char key[LV_KEYSZ];

    v = lv_open(tc_lk, &none_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    tl_clear();
    TF_EQ(lv_home(v), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM LIST CAT=NONE PAGE=10");
    TF_EQ(lv_rows(v), 0);
    TF_EQ(lv_selected(v, key), 0);
    TF_STREQ(key, "");
    TF_STREQ(tk_row(3) + 1, msg(HDR_EMPTY));
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(lv_pgup(v), RQ_OK);
    TF_STREQ(lv_order_name(v), "");
    TF_EQ(lv_order_index(v), 0);
    lv_close(v);
}

static u16 g_run_key;
static lv *g_run_view;

static void do_run(void)
{
    g_run_key = lv_run(g_run_view);
}

static void test_running(void)
{
    static const u16 k1[] = {
        KEY_DOWN, KEY_DOWN, KEY_PGDN, KEY_UP, KEY_HOME, KEY_DOWN,
        KEY_ENTER, KEY_NONE
    };
    static const u16 k2[] = { KEY_END, KEY_HOME, KEY_TAB, KEY_NONE };
    static const u16 k3[] = { KEY_DOWN, KEY_NONE };

    g_run_view = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(g_run_view != NULL))
        return;
    TF_EQ(lv_home(g_run_view), RQ_OK);
    tk_feed(k1, 0);
    g_run_key = 0;
    if (setjmp(tk_jb) == 0)
        do_run();
    TF_EQ(g_run_key, KEY_ENTER);
    TF_EQ(sel_id(g_run_view), 2);
    TF_STREQ(lv_order_name(g_run_view), "ID");
    TF_EQ(tk_attr(4, 1), tui_attr(TA_BOLD));
    TF_EQ(tk_attr(4, 60), tui_attr(TA_BOLD));
    TF_EQ(tk_attr(3, 1), tui_attr(TA_NORMAL));
    lv_close(g_run_view);

    /* Tab goes back to the caller. */
    g_run_view = lv_open(tc_lk, &find_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(g_run_view != NULL))
        return;
    TF_EQ(lv_home(g_run_view), RQ_OK);
    tk_feed(k2, 0);
    g_run_key = 0;
    if (setjmp(tk_jb) == 0)
        do_run();
    TF_EQ(g_run_key, KEY_TAB);
    TF_EQ(sel_id(g_run_view), FIND_EVERY);
    TF_EQ(tk_attr(3, 1), tui_attr(TA_BOLD));
    /* The end of the keys leaves through the exit function, inside
       lv_run: the selection is the bar again. */
    tk_feed(k3, 0);
    g_run_key = 0;
    if (setjmp(tk_jb) == 0)
        do_run();
    TF_EQ(g_run_key, 0);
    TF_EQ(sel_id(g_run_view), 2 * FIND_EVERY);
    TF_EQ(tk_attr(3, 1), tui_attr(TA_NORMAL));
    TF_EQ(tk_attr(4, 1), tui_attr(TA_SELECT));
    lv_close(g_run_view);
    key_set_source(NULL, NULL);
}

/* ---------------------------------------------------------------------
 * The pending note.
 * --------------------------------------------------------------------- */

/* A known text on the hint row, to see whether a note replaced it. */
#define NOTE_MARK       "."

static void note_mark(void)
{
    tui_msg(NOTE_MARK, 0);
}

/* The hint row text after its first column. */
static const char *hint_text(void)
{
    const char *s;

    s = tk_row(TUI_HINTROW);
    return *s != '\0' ? s + 1 : s;
}

/* The note text as the hint row shows it: msg_sub of m, a and b,
   trailing spaces removed, into w (MSG_MAXLEN + 1 bytes). */
static void note_want(char *w, u16 m, const char *a, const char *b)
{
    size_t n;

    strcpy(w, msg_sub(m, a, b));
    n = strlen(w);
    while (n > 0 && w[n - 1] == ' ')
        w[--n] = '\0';
}

/* lv_note keeps one note (arguments cut at LINK_IDLEN, NULL allowed),
   lv_note_show shows it once; lv_reload shows it after a reload that
   worked and drops it either way. */
static void test_notes(void)
{
    lv *v;
    char w[MSG_MAXLEN + 1];
    char a[LINK_IDLEN + 1];
    char b[LINK_LOCNAMELEN + 1];

    v = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);

    /* Shown on demand, and only once. */
    lv_note(MT_DBERR, "7", "X");
    note_mark();
    TF_STREQ(hint_text(), NOTE_MARK);
    lv_note_show();
    note_want(w, MT_DBERR, "7", "X");
    TF_STREQ(hint_text(), w);
    note_mark();
    lv_note_show();
    TF_STREQ(hint_text(), NOTE_MARK);

    /* NULL arguments. */
    lv_note(MT_DBERR, NULL, NULL);
    lv_note_show();
    note_want(w, MT_DBERR, NULL, NULL);
    TF_STREQ(hint_text(), w);
    lv_note(MT_DBERR, "5", NULL);
    lv_note_show();
    note_want(w, MT_DBERR, "5", NULL);
    TF_STREQ(hint_text(), w);
    lv_note(MT_DBERR, NULL, "Q");
    lv_note_show();
    note_want(w, MT_DBERR, NULL, "Q");
    TF_STREQ(hint_text(), w);

    /* The first argument is cut at LINK_IDLEN characters, the second at
       LINK_LOCNAMELEN. */
    lv_note(MT_DBERR, "123456789012345",
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    lv_note_show();
    memcpy(a, "123456789012345", LINK_IDLEN);
    a[LINK_IDLEN] = '\0';
    memcpy(b, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", LINK_LOCNAMELEN);
    b[LINK_LOCNAMELEN] = '\0';
    note_want(w, MT_DBERR, a, b);
    TF_STREQ(hint_text(), w);
    /* Arguments of exactly that many characters are kept whole. */
    lv_note(MT_DBERR, a, b);
    note_mark();
    lv_note_show();
    TF_STREQ(hint_text(), w);

    /* The arguments are copied when the note is made. */
    strcpy(a, "11");
    lv_note(MT_DBERR, a, NULL);
    strcpy(a, "22");
    lv_note_show();
    note_want(w, MT_DBERR, "11", NULL);
    TF_STREQ(hint_text(), w);

    /* Message 0 clears the note. */
    lv_note(MT_DBERR, "1", "2");
    lv_note(0, NULL, NULL);
    note_mark();
    lv_note_show();
    TF_STREQ(hint_text(), NOTE_MARK);
    /* A later note replaces an earlier one. */
    lv_note(MT_DBERR, "1", "2");
    lv_note(MT_INTERNAL, "3", "4");
    lv_note_show();
    note_want(w, MT_INTERNAL, "3", "4");
    TF_STREQ(hint_text(), w);

    /* A reload that works shows the note, and drops it. */
    lv_note(MT_DBERR, "7", "X");
    note_mark();
    tl_clear();
    TF_EQ(lv_reload(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    note_want(w, MT_DBERR, "7", "X");
    TF_STREQ(hint_text(), w);
    note_mark();
    lv_note_show();
    TF_STREQ(hint_text(), NOTE_MARK);
    /* Without a note a reload shows nothing. */
    note_mark();
    TF_EQ(lv_reload(v), RQ_OK);
    TF_STREQ(hint_text(), NOTE_MARK);
    lv_note(MT_DBERR, "1", "2");
    lv_note(0, NULL, NULL);
    TF_EQ(lv_reload(v), RQ_OK);
    TF_STREQ(hint_text(), NOTE_MARK);

    /* A failed reload shows the failure, not the note, and drops the
       note all the same. */
    lv_note(MT_DBERR, "8", "Y");
    tl_clear();
    tl_fail(1, "-ERR 402 busy");
    TF_EQ(lv_reload(v), RQ_EBUSY);
    note_want(w, MT_DBERR, "8", "Y");
    TF_CHECK(strcmp(hint_text(), w) != 0);
    TF_STREQ(hint_text(), msg(MT_BUSY));
    note_mark();
    lv_note_show();
    TF_STREQ(hint_text(), NOTE_MARK);
    /* ... and the next good reload has none to show. */
    TF_EQ(lv_reload(v), RQ_OK);
    TF_STREQ(hint_text(), NOTE_MARK);
    TF_EQ(sel_id(v), 1);
    lv_close(v);
}

/* ---------------------------------------------------------------------
 * The row search.
 * --------------------------------------------------------------------- */
static u16 g_seek_calls;

/* True on the row whose key is the string arg; the row's text, padded
   to LV_ROWTEXT, starts with the key. */
static int seek_key(void *arg, const char *key, const char *text)
{
    g_seek_calls++;
    TF_EQ(strlen(text), LV_ROWTEXT);
    TF_EQ(strtoul(text, NULL, 10), strtoul(key, NULL, 10));
    return strcmp(key, (const char *)arg) == 0;
}

/* True on no row. */
static int seek_never(void *arg, const char *key, const char *text)
{
    TF_CHECK(arg == (void *)&g_seek_calls);
    g_seek_calls++;
    return key == NULL || text == NULL;
}

/* True on the row whose text holds the string arg. */
static int seek_text(void *arg, const char *key, const char *text)
{
    g_seek_calls++;
    return key[0] != '\0' && strstr(text, (const char *)arg) != NULL;
}

/* 1 when request s was made since tl_clear. */
static int has_req(const char *s)
{
    u16 i;

    for (i = 0; i < tl_count(); i++)
        if (strcmp(tl_req(i), s) == 0)
            return 1;
    return 0;
}

/* lv_seek: from the row after the selection, wrapping to the top, up to
   the first row that matches; it stops when the start row comes round,
   after max rows, or on a failed fetch, and moves and paints like a
   move. */
static void test_seeking(void)
{
    static const u16 kenter[] = { KEY_ENTER, KEY_NONE };
    lv *v;
    u16 i;

    v = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    lv_paint(v);

    /* A match in the rows held: no request; the bar is drawn there. */
    tl_clear();
    g_seek_calls = 0;
    TF_EQ(lv_seek(v, seek_key, (void *)"7", 100), RQ_OK);
    TF_EQ(sel_id(v), 7);
    TF_EQ(g_seek_calls, 6);
    TF_EQ(tl_count(), 0);
    TF_EQ(tk_attr(9, 1), tui_attr(TA_BOLD));
    TF_EQ(tk_attr(3, 1), tui_attr(TA_NORMAL));
    TF_EQ(lv_leave(v), TUI_CMD_NONE);

    /* A match on the next page: fetched, the window scrolls to it. */
    tl_clear();
    g_seek_calls = 0;
    TF_EQ(lv_seek(v, seek_key, (void *)"15", 100), RQ_OK);
    TF_EQ(sel_id(v), 15);
    TF_EQ(g_seek_calls, 8);
    TF_EQ(tl_count(), 1);
    TF_CHECK(tf_starts(tl_req(0), "ITEM LIST ORDER=ID PAGE=10 AFTER="));
    TF_CHECK(tf_starts(tk_row(PS + 2), " 15 "));
    TF_EQ(tk_attr(PS + 2, 1), tui_attr(TA_BOLD));

    /* max rows: the walk stops after them, where it has got to. */
    TF_EQ(lv_home(v), RQ_OK);
    g_seek_calls = 0;
    TF_EQ(lv_seek(v, seek_never, &g_seek_calls, 5), RQ_OK);
    TF_EQ(g_seek_calls, 5);
    TF_EQ(sel_id(v), 6);
    lv_paint(v);
    TF_EQ(tk_attr(8, 1), tui_attr(TA_BOLD));

    /* A match on the row's text: item 10 is the next whose maker is
       MK3 (MK followed by its number modulo 7). */
    g_seek_calls = 0;
    TF_EQ(lv_seek(v, seek_text, (void *)" MK3 ", 100), RQ_OK);
    TF_EQ(sel_id(v), 10);
    TF_EQ(g_seek_calls, 4);

    /* Wrapping: from the last row the search goes on at the top. */
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(sel_id(v), g_nitems);
    tl_clear();
    g_seek_calls = 0;
    TF_EQ(lv_seek(v, seek_key, (void *)"3", 1000), RQ_OK);
    TF_EQ(sel_id(v), 3);
    TF_EQ(g_seek_calls, 3);
    TF_CHECK(has_req("ITEM LIST ORDER=ID PAGE=10"));
    TF_STREQ(tk_row(3), " 1 MK1 X1");
    TF_EQ(tk_attr(5, 1), tui_attr(TA_BOLD));
    lv_close(v);

    /* No match: the start row comes round and the bar is back on it.
       match sees every row reached, the start row too when the walk
       comes round to it: NCARD calls. */
    v = lv_open(tc_lk, &card_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    for (i = 0; i < 4; i++)
        TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), 5);
    g_seek_calls = 0;
    TF_EQ(lv_seek(v, seek_never, &g_seek_calls, 1000), RQ_OK);
    TF_EQ(sel_id(v), 5);
    TF_EQ(g_seek_calls, NCARD);
    lv_paint(v);
    TF_CHECK(tf_starts(tk_row(3), " 1 "));
    TF_EQ(tk_attr(7, 1), tui_attr(TA_BOLD));
    /* The same from the last row of the list. */
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(sel_id(v), NCARD);
    g_seek_calls = 0;
    TF_EQ(lv_seek(v, seek_never, &g_seek_calls, 1000), RQ_OK);
    TF_EQ(sel_id(v), NCARD);
    TF_EQ(g_seek_calls, NCARD);
    /* A search for the selected row's own key finds it only after the
       whole round. */
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(lv_down(v), RQ_OK);
    TF_EQ(sel_id(v), 2);
    TF_EQ(lv_seek(v, seek_key, (void *)"2", 1000), RQ_OK);
    TF_EQ(sel_id(v), 2);
    lv_close(v);

    /* An empty list: no move, no request, nothing matched. */
    v = lv_open(tc_lk, &none_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    tl_clear();
    g_seek_calls = 0;
    TF_EQ(lv_seek(v, seek_never, &g_seek_calls, 100), RQ_OK);
    TF_EQ(g_seek_calls, 0);
    TF_EQ(tl_count(), 0);
    TF_EQ(lv_rows(v), 0);
    TF_EQ(sel_id(v), 0);
    TF_EQ(lv_leave(v), TUI_CMD_NONE);
    lv_close(v);

    /* A failed fetch stops the search and reports like a move. */
    v = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    tl_clear();
    tl_fail(1, "-ERR 402 busy");
    TF_EQ(lv_seek(v, seek_key, (void *)"15", 100), RQ_EBUSY);
    TF_EQ(tl_count(), 1);
    TF_CHECK(sel_id(v) >= 1 && sel_id(v) <= PS);
    TF_EQ(lv_leave(v), TUI_CMD_NONE);
    /* One that ends the login: lv_leave gives the command until a move
       works. */
    TF_EQ(lv_home(v), RQ_OK);
    tl_clear();
    tl_fail(1, "-ERR 404 password change required");
    tk_feed(kenter, 0);
    if (setjmp(tk_jb) == 0)
        TF_EQ(lv_seek(v, seek_key, (void *)"15", 100), RQ_EMUSTCHG);
    TF_EQ(lv_leave(v), TUI_CMD_LOGIN);
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(lv_leave(v), TUI_CMD_NONE);
    key_set_source(NULL, NULL);
    lv_close(v);
}

/* lv_lead: the lead row is drawn first and selected, the rows after
   it. */
static void test_lead(void)
{
    char key[LV_KEYSZ];
    lv *v;

    v = lv_open(tc_lk, &card_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    lv_lead(v, "0", "(none)");
    TF_EQ(lv_home(v), RQ_OK);
    lv_paint(v);
    TF_STREQ(tk_row(3), " (none)");
    TF_CHECK(tf_starts(tk_row(4), " 1 "));
    TF_EQ(lv_selected(v, key), 1);
    TF_STREQ(key, "0");
    TF_EQ(lv_rows(v), PS);
    lv_close(v);
}

/* ---------------------------------------------------------------------
 * F5 and Tab in lv_run.
 * --------------------------------------------------------------------- */

/* F5 fetches the rows again where they are and goes on with the next
   key; when that fails in a way that must leave, lv_run returns KEY_ESC
   and lv_leave gives the command.  Tab, with orders too, is the
   caller's, with no request. */
static void test_refresh(void)
{
    static const u16 kf5[] = { KEY_F5, KEY_ENTER, KEY_NONE };
    static const u16 ktab[] = { KEY_TAB, KEY_NONE };

    g_run_view = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(g_run_view != NULL))
        return;
    TF_EQ(lv_home(g_run_view), RQ_OK);
    TF_EQ(lv_down(g_run_view), RQ_OK);
    tl_clear();
    tk_feed(kf5, 0);
    g_run_key = 0;
    if (setjmp(tk_jb) == 0)
        do_run();
    TF_EQ(g_run_key, KEY_ENTER);
    TF_EQ(tk_pos(), 2);
    TF_EQ(tl_count(), 1);
    TF_CHECK(tf_starts(tl_req(0), "ITEM LIST ORDER=ID PAGE=10"));
    TF_EQ(sel_id(g_run_view), 2);
    TF_EQ(lv_leave(g_run_view), TUI_CMD_NONE);

    /* The fetch fails with 404: its error box reads the Enter; then Esc,
       and the login command. */
    tl_clear();
    tl_fail(1, "-ERR 404 password change required");
    tk_feed(kf5, 0);
    g_run_key = 0;
    if (setjmp(tk_jb) == 0)
        do_run();
    TF_EQ(g_run_key, KEY_ESC);
    TF_EQ(tk_pos(), 2);
    TF_EQ(tl_count(), 1);
    TF_EQ(lv_leave(g_run_view), TUI_CMD_LOGIN);
    TF_EQ(lv_home(g_run_view), RQ_OK);
    TF_EQ(lv_leave(g_run_view), TUI_CMD_NONE);

    tl_clear();
    tk_feed(ktab, 0);
    g_run_key = 0;
    if (setjmp(tk_jb) == 0)
        do_run();
    TF_EQ(g_run_key, KEY_TAB);
    TF_EQ(tl_count(), 0);
    TF_STREQ(lv_order_name(g_run_view), "ID");
    lv_close(g_run_view);
    key_set_source(NULL, NULL);
}

/* ---------------------------------------------------------------------
 * Keys typed during a fill or the End walk.
 * --------------------------------------------------------------------- */
#define ML_WALK         200     /* LISTV.MSG: progress of the End walk  */
#define ML_STOPPED      201     /* LISTV.MSG: a fill stopped by Esc     */

static const u16 *g_ahead;      /* keys typed ahead, up to KEY_NONE     */
static u16 g_wait;              /* the look they wait from, from 1      */
static u16 g_peeks;             /* looks for a waiting key so far       */
char g_seen[TUI_ST_INFOW + 1];   /* status information then      */

/* A key source whose keys g_ahead are typed ahead from look g_wait on;
   at that look it notes the status information in g_seen. */
static u16 ahead_src(void *arg, int peek)
{
    TF_CHECK(arg == (void *)&g_wait);
    if (peek && ++g_peeks == g_wait)
        txt_copy(g_seen, tui_info_text(), (u16)sizeof g_seen);
    if (peek && g_peeks < g_wait)
        return KEY_NONE;
    if (*g_ahead == KEY_NONE)
        return peek ? KEY_NONE : KEY_EOS;
    return peek ? *g_ahead : *g_ahead++;
}

void ahead(const u16 *keys, u16 look)
{
    g_ahead = keys;
    g_wait = look;
    g_peeks = 0;
    g_seen[0] = '\0';
    key_set_source(ahead_src, &g_wait);
}

/* Esc (or Ctrl-Break) typed during a fill stops it between requests:
   the keys before it are dropped, those after it stay, the rows found
   are kept with the cursor after them, the stop is said, and the next
   move goes on from there. */
static void test_esc(void)
{
    static const u16 k_xesc[] = { 'x', KEY_ESC, KEY_NONE };
    static const u16 k_escy[] = { KEY_ESC, 'y', KEY_NONE };
    static const u16 k_brk[] = { KEY_BREAK, KEY_NONE };
    static const u16 k_none[] = { KEY_NONE };
    static char fake[LINK_MAXCURSOR + sizeof "+DATA\nC \n."];
    char c1[LINK_MAXCURSOR + 1];
    char num[TXT_U32LEN + 1];
    lv *v;
    u16 i;

    v = lv_open(tc_lk, &find_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    /* ITEM FIND's first page takes two requests, the second from c1. */
    tl_clear();
    TF_EQ(lv_home(v), RQ_OK);
    TF_CHECK(tl_count() >= 2);
    strcpy(c1, after_of(tl_req(1)));
    tui_info("");
    ahead(k_xesc, 1);
    tl_clear();
    TF_EQ(lv_home(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(*g_ahead, KEY_NONE);
    TF_CHECK(lv_rows(v) > 0 && lv_rows(v) < PS);
    txt_u32(num, lv_rows(v));
    TF_STREQ(g_seen, msg_sub(MT_WORKING, num, NULL));
    TF_STREQ(tui_info_text(), "");
    TF_STREQ(tk_row(TUI_HINTROW) + 1, msg(ML_STOPPED));
    ahead(k_none, 1);
    tl_clear();
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_STREQ(after_of(tl_req(0)), c1);
    TF_EQ(sel_id(v), (u32)FIND_EVERY * (g_nitems / FIND_EVERY));
    lv_close(v);

    /* The End walk stops before its third page: that page is shown in
       full, and PgDn goes on after it. */
    v = lv_open(tc_lk, &items_src, 2, 1, PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    ahead(k_escy, 3);
    tl_clear();
    TF_EQ(lv_end(v), RQ_OK);
    TF_EQ(tl_count(), 3);
    for (i = 0; i < 2; i++)
        TF_CHECK(tf_starts(tl_req(i), "ITEM LIST ORDER=ID FIELDS=ITEM_ID "
                           "PAGE=10 AFTER="));
    TF_CHECK(tf_starts(tl_req(2), "ITEM LIST ORDER=ID PAGE=10 AFTER="));
    strcpy(c1, after_of(tl_req(1)));
    TF_STREQ(after_of(tl_req(2)), c1);
    txt_u32(num, 2 * PS);
    TF_STREQ(g_seen, msg_sub(ML_WALK, num, NULL));
    TF_EQ(*g_ahead, 'y');
    TF_EQ(sel_id(v), 3 * PS);
    TF_STREQ(tk_row(TUI_HINTROW) + 1, msg(ML_STOPPED));
    ahead(k_none, 1);
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(sel_id(v), 4 * PS);

    /* Ctrl-Break during the fill of the page after A, whose first
       answer (the test's) has no rows: the move asks for nothing more
       and stops on A's last row, and the next flush writes every cell;
       the next PgDn goes on from the cursor that answer gave. */
    TF_EQ(lv_home(v), RQ_OK);
    tl_clear();
    TF_EQ(lv_pgdn(v), RQ_OK);
    strcpy(c1, after_of(tl_req(0)));
    TF_EQ(lv_home(v), RQ_OK);
    strcpy(fake, "+DATA\nC ");
    strcat(fake, c1);
    strcat(fake, "\n.");
    ahead(k_brk, 1);
    tl_clear();
    tl_fail(1, fake);
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(*g_ahead, KEY_NONE);
    TF_EQ(sel_id(v), PS);
    /* DOS may have echoed ^C: the whole screen is written again. */
    vid_flush();
    TF_EQ(vid_flushed(), VID_CELLS);
    TF_STREQ(tk_row(TUI_HINTROW) + 1, msg(ML_STOPPED));
    ahead(k_none, 1);
    tl_clear();
    TF_EQ(lv_pgdn(v), RQ_OK);
    TF_STREQ(after_of(tl_req(0)), c1);
    TF_EQ(sel_id(v), 2 * PS);
    key_set_source(NULL, NULL);
    lv_close(v);
}

/* The answer to each request of the long walk: WALK_ROWS rows and a
   cursor after them, fewer than the WALK_PS rows of its page, so that
   each page takes two requests. */
#define WALK_PS         21
#define WALK_ROWS       13
#define WALK_HEAD       "+DATA\n"
#define WALK_ROW        "R 1\n"
#define WALK_TAIL       "C W\n."

static const char *g_page;     /* the answer of answer_page            */

/* tl_hook: answer every ITEM LIST with g_page. */
static void answer_page(const char *line, u16 len)
{
    if (is_list(line, len))
        tl_fail(tl_count(), g_page);
}

/* The End walk counts past 65535 rows: every request is answered with
   the long walk's rows, and the progress shown when Esc is typed
   between the two requests of a page counts the rows of the pages
   walked and of that page's first request. */
static void test_walk_count(void)
{
    static const u16 k_esc[] = { KEY_ESC, KEY_NONE };
    char page[sizeof WALK_HEAD - 1 + WALK_ROWS * (sizeof WALK_ROW - 1) +
              sizeof WALK_TAIL];
    char num[TXT_U32LEN + 1];
    u32 pages;
    lv *v;
    u16 i;

    v = lv_open(tc_lk, &items_src, 2, 1, WALK_PS + 1, 60);
    if (!TF_CHECK(v != NULL))
        return;
    TF_EQ(lv_home(v), RQ_OK);
    strcpy(page, WALK_HEAD);
    for (i = 0; i < WALK_ROWS; i++)
        strcat(page, WALK_ROW);
    strcat(page, WALK_TAIL);
    g_page = page;
    /* Each page looks for Esc before its first request and after it. */
    pages = ((u32)0xFFFF - WALK_ROWS) / WALK_PS + 1;
    ahead(k_esc, (u16)(2 * pages + 2));
    tl_hook = answer_page;
    TF_EQ(lv_end(v), RQ_OK);
    tl_hook = NULL;
    txt_u32(num, pages * WALK_PS + WALK_ROWS);
    TF_STREQ(g_seen, msg_sub(ML_WALK, num, NULL));
    key_set_source(NULL, NULL);
    lv_close(v);
}

static void test_vals(void)
{
    char *names[2];
    char *vals[2];
    lv_rec r;

    names[0] = "A";
    names[1] = "B";
    vals[0] = "1";
    vals[1] = "2";
    r.n = 2;
    r.names = names;
    r.vals = vals;
    TF_STREQ(lv_val(&r, "B"), "2");
    TF_STREQ(lv_val(&r, "C"), "");
    TF_CHECK(lv_open(tc_lk, &items_src, 2, 1, LV_MAXPAGE + 2, 60) == NULL);
    TF_CHECK(lv_open(tc_lk, &items_src, 2, 1, 1, 60) == NULL);
    TF_CHECK(lv_open(tc_lk, &items_src, 2, 1, 5, LV_TEXTLEN + 1) == NULL);
}

/* ---------------------------------------------------------------------
 * The link's delivery.
 * --------------------------------------------------------------------- */
static const char k_list[] = "ITEM LIST FIELDS=ITEM_ID";
static const char k_list3[] = "+DATA\nF ITEM_ID\nR 1\nR 2\nR 3\n.";
static u16 g_stop_calls;

/* rq_fn: counts the data lines and asks to stop at the second. */
static int stop_second(void *arg, const rq_line *ln)
{
    TF_CHECK(arg == (void *)&g_stop_calls);
    TF_CHECK(ln->kind == 'F' || ln->kind == 'R');
    return ++g_stop_calls == 2;
}

/* link_line_fn: counts the response lines and asks to stop at the
   second. */
static int stop_second_line(void *arg, const char *line, u16 len)
{
    TF_CHECK(arg == (void *)&g_stop_calls);
    TF_EQ(strlen(line), len);
    return ++g_stop_calls == 2;
}

/* A callback that asks to stop is not called again, whether the
   response comes from the engine or from tl_fail, link_send then
   returns LINK_EOUTPUT (which rq_send turns into RQ_ESTOP), and the
   link stays in step. */
static void test_stop(void)
{
    rq q;
    rq_res res;
    int i;

    for (i = 0; i < 2; i++) {
        tl_clear();
        if (i == 1)
            tl_fail(1, k_list3);
        g_stop_calls = 0;
        rq_begin(&q, k_list);
        TF_EQ(rq_send(tc_lk, &q, stop_second, &g_stop_calls, &res),
              RQ_ESTOP);
        TF_EQ(g_stop_calls, 2);
        TF_EQ(tc_send("PING"), RQ_OK);
        tl_clear();
        if (i == 1)
            tl_fail(1, k_list3);
        g_stop_calls = 0;
        TF_EQ(link_send(tc_lk, k_list, (u16)(sizeof k_list - 1),
                        stop_second_line, &g_stop_calls), LINK_EOUTPUT);
        TF_EQ(g_stop_calls, 2);
        TF_EQ(tc_send("PING"), RQ_OK);
    }
    tl_clear();
}

/* The arguments after dir: a number is the item count, anything else
   the message directory; without one, the program's own directory. */
static void args(int argc, char **argv, char *mdir)
{
    int i;

    tf_progdir(mdir, PATHMAX, argv[0]);
    for (i = 2; i < argc; i++) {
        if (argv[i][0] >= '0' && argv[i][0] <= '9')
            g_nitems = (u16)atoi(argv[i]);
        else if (strlen(argv[i]) < PATHMAX)
            strcpy(mdir, argv[i]);
    }
}

int main(int argc, char **argv)
{
    link_clock clk;
    char why[LINK_WHYSZ];
    char mdir[PATHMAX];
    int rc;

    tf_begin("T_LISTV");
    cmem_init();
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_LISTV dir [items] [msgdir]\n");
        return 2;
    }
    strcpy(g_ddir, argv[1]);
    g_nitems = LV_TITEMS;
    args(argc, argv, mdir);
    /* The server's per-request scan bound, with a match every FIND_EVERY
       items and at least LV_TITEMS items, makes the first page of ITEM
       FIND take two requests; the End cases need whole pages. */
    if (g_nitems < LV_TITEMS)
        g_nitems = LV_TITEMS;
    if (g_nitems > LV_MAXITEMS)
        g_nitems = LV_MAXITEMS;
    g_nitems = (u16)(g_nitems - g_nitems % PS);
    te_remove(g_ddir, NULL);
    tc_clock(&clk);
    rc = link_open_local(&tc_lk, g_ddir, &clk, why, sizeof why);
    if (!TF_EQ(rc, 0)) {
        printf("T_LISTV: %s\n", why);
        return tf_done();
    }
    /* The far blocks come after the engine's near heap has grown: a
       DOS program's near heap cannot grow past memory allocated after
       it. */
    if (!TF_EQ(msg_load(mdir, MSG_FILE), 0)) {
        printf("T_LISTV: %s\n", msg_why());
        return tf_done();
    }
    if (!TF_EQ(vid_open(0), 0))
        return tf_done();
    tui_set_exit(tk_exit);
    tui_palette(TP_COLOR);
    tui_s.l = tc_lk;
    tui_s.level = TUI_LVL_ADMIN;
    if (!tc_admin())
        return tf_done();
    add_range(1, g_nitems);
    test_vals();
    test_stop();
    test_first_pages();
    test_marked();
    test_ring();
    test_sparse();
    test_esc();
    test_walk_count();
    test_reload();
    test_leaving();
    test_fail_box();
    test_failures();
    test_ring_full_end();
    test_reload_back();
    test_empty();
    test_running();
    test_columns();
    test_notes();
    test_seeking();
    test_lead();
    test_refresh();
    test_end_cases();
    test_tall();
    vid_close();
    TF_EQ(link_close(tc_lk), 0);
    msg_free();
    te_remove(g_ddir, NULL);
    return tf_done();
}
