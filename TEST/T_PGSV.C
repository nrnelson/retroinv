/*
 * T_PGSV.C - the survey of a pager's list and the jump into it
 * (SRC/CLIENT/PAGERSV.C) over the engine.
 *
 * usage: T_PGSV dir
 *
 * dir is a scratch data directory prefix ("OBJ/HOST/PGSVDT/") holding
 * nothing but this test's files; its engine files are removed first and
 * last.  The test links the recording link of TRLINK.H instead of
 * LINK.C, so every request a survey or a jump sends is checked exactly,
 * and a chosen request can be answered with a response of the test's
 * own.  It is written from the contract in PAGERSV.H and knows nothing
 * of how the survey keeps its pages.
 *
 * The oracle.  Before each survey the test pages the list itself
 * through rq_send, as PAGERSV.H says a survey does: the requests the
 * survey must send, the rows it must have counted after each, the page
 * starts it must keep, its rows, pages and all, and the item numbers of
 * the whole list in order, against which the rows a jump shows and the
 * pager's above are checked.
 *
 * Covered:
 * - psv_init and psv_free; the block of cursors, taken once by the first
 *   survey, its size, freed by psv_free; a survey without memory for it;
 * - survey counts and requests: pages of 1 row to LV_MAXPAGE, around
 *   LINK_PAGEMAX and past it (several requests a page), an empty list, a
 *   lead row, a source without a walk field, a second order, filtered
 *   lists whose bounded scans end requests early and with no row;
 * - a filtered list whose last full page ended on a C line: the empty
 *   page after it not kept, a jump to its last row and the moves after;
 * - lists of exactly PSV_MAXPAGES pages (all 1), one row longer and
 *   longer still: the survey stops at the table's end, its pages kept,
 *   all 0, and the moves from the last kept page stay exact;
 * - the wait function: its calls and progress, a stop between a page's
 *   requests and between pages; failed requests, the bad cursor
 *   included, at the first request and later;
 * - the rows held, window, selection, ring, above and order of the pager
 *   unchanged by a survey, a stopped one and a failed one included;
 * - the keys a survey collects: every row's of the list in order, the
 *   lead row's not; over 1 also for a list longer than the
 *   table and when the set has no memory to grow, the count going on;
 *   none after a stop, a failure or without memory for the
 *   cursors, a set held before emptied; no set given (NULL); the key
 *   sink NULL again after, so an End walk collects nothing;
 * - psv_jump to a row of every page and past the last kept, its request,
 *   rows, window and above; then moves up past the ring's depth, down and
 *   End, above checked against the list; a lead row's list, a filtered
 *   list, a second order, a list longer than the table;
 * - psv_jump's failures: a bad cursor (the survey forgotten, the restart
 *   when the fail function stays, a restart that fails), another
 *   failure, a fill the wait function stops;
 * - an unpaged pager (pg_unpaged) over CAT LIST, with and without a
 *   lead row: one bare request a page, the count, pages, keys, and a
 *   jump to every page.
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
#include "KEYSET.H"
#include "PAGERSV.H"

#define PATHMAX         96
#define NITEMS          250     /* items added                          */
#define NCARD           20      /* the first items are in CARD          */
#define ZQ_EVERY        25      /* every n-th item matches ITEM FIND ZQ */
#define MAXREQ          300     /* requests recorded and expected       */
#define MAXSCRIPT       8       /* requests the test may answer         */
#define BLKSZ           ((u16)(PSV_MAXPAGES * LV_CURSZ))

#define BUSY    "-ERR 402 busy"
#define BADCUR  "-ERR 204 bad cursor"

static char g_ddir[PATHMAX];
static int g_tag;               /* what the pager passes to its callbacks */

/* ---------------------------------------------------------------------
 * The data: item i is in CARD for i <= NCARD; its MODEL is ZQi for every
 * ZQ_EVERY-th, else Xi; its MFR is GAPQ for the items 1, 2 and NITEMS,
 * which ITEM FIND GAPQ finds with a whole bounded scan between them,
 * else MKn.
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
        if (i <= 2 || i == NITEMS)
            strcpy(line, "GAPQ");
        else
            sprintf(line, "MK%u", (unsigned)(i % 7));
        rq_pair(&q, "MFR", line);
        sprintf(line, i % ZQ_EVERY == 0 ? "ZQ%u" : "X%u", (unsigned)i);
        rq_pair(&q, "MODEL", line);
        rq_pair(&q, "LOC_ID", "A1");
        if (!TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK))
            return;
    }
}

/* ---------------------------------------------------------------------
 * Sources.  The heads are the requests up to PAGE=: W for a survey's
 * (the walk field), L for a page load's (the order's field list).
 * --------------------------------------------------------------------- */
static u32 num_of(const char *s)
{
    return (u32)strtoul(s, NULL, 10);
}

static void fmt_item(void *arg, const lv_rec *r, char *text, char *key)
{
    TF_CHECK(arg == (void *)&g_tag);
    TF_CHECK(r->n >= 1);
    strcpy(key, lv_val(r, "ITEM_ID"));
    strcpy(text, lv_val(r, "ITEM_ID"));
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

static void zq_args(void *arg, rq *q)
{
    TF_CHECK(arg == (void *)&g_tag);
    rq_word(q, "ZQ");
}

static void gap_args(void *arg, rq *q)
{
    TF_CHECK(arg == (void *)&g_tag);
    rq_word(q, "GAPQ");
}

static const lv_src items_src = {
    "ITEM LIST", 2, { "ID", "MFR", NULL, NULL },
    { NULL, "ITEM_ID,MFR", NULL, NULL },
    { 0, 0, 0, 0 }, 0, "ITEM_ID",
    NULL, fmt_item, &g_tag, NULL
};
#define ID_W            "ITEM LIST ORDER=ID FIELDS=ITEM_ID"
#define ID_L            "ITEM LIST ORDER=ID"
#define MFR_W           "ITEM LIST ORDER=MFR FIELDS=ITEM_ID"
#define MFR_L           "ITEM LIST ORDER=MFR FIELDS=ITEM_ID,MFR"

/* No walk field: a survey asks for the order's field list. */
static const lv_src nowalk_src = {
    "ITEM LIST", 1, { "ID", NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, NULL,
    NULL, fmt_item, &g_tag, NULL
};

static const lv_src card_src = {
    "ITEM LIST", 1, { "ID", NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, "ITEM_ID",
    card_args, fmt_item, &g_tag, NULL
};
#define CARD_W          "ITEM LIST CAT=CARD ORDER=ID FIELDS=ITEM_ID"
#define CARD_L          "ITEM LIST CAT=CARD ORDER=ID"

static const lv_src none_src = {
    "ITEM LIST", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, NULL,
    none_args, fmt_item, &g_tag, NULL
};
#define NONE_W          "ITEM LIST CAT=NONE"

static const lv_src zq_src = {
    "ITEM FIND", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, "ITEM_ID",
    zq_args, fmt_item, &g_tag, NULL
};
#define ZQ_W            "ITEM FIND ZQ FIELDS=ITEM_ID"
#define ZQ_L            "ITEM FIND ZQ"

static const lv_src gap_src = {
    "ITEM FIND", 0, { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { 0, 0, 0, 0 }, 0, "ITEM_ID",
    gap_args, fmt_item, &g_tag, NULL
};
#define GAP_W           "ITEM FIND GAPQ FIELDS=ITEM_ID"
#define GAP_L           "ITEM FIND GAPQ"

static const char k_leadkey[] = "0";
static const char k_leadtext[] = "(lead)";

/* ---------------------------------------------------------------------
 * The pager's callbacks, and the requests as they are sent.
 * --------------------------------------------------------------------- */
static u16 g_nwait;             /* wait calls                           */
static int g_wwalk[MAXREQ];
static u32 g_wn[MAXREQ];
static u16 g_wstop;             /* the call that returns 1, 0 for none  */

static int wait_fn(void *arg, int walk, u32 n)
{
    TF_CHECK(arg == (void *)&g_tag);
    if (g_nwait < MAXREQ) {
        g_wwalk[g_nwait] = walk;
        g_wn[g_nwait] = n;
    }
    g_nwait++;
    return g_nwait == g_wstop;
}

#define MAXF            4
static u16 g_nfail;             /* fail calls                           */
static int g_fcode[MAXF];
static int g_fstay;             /* what the fail function answers       */

static int fail_fn(void *arg, const rq_res *res)
{
    TF_CHECK(arg == (void *)&g_tag);
    if (g_nfail < MAXF)
        g_fcode[g_nfail] = res->code;
    g_nfail++;
    return g_fstay;
}

/* Every request line sent since tl_clear, by number from 0, and the
   responses of the test's own, by number from 1 (NULL: the engine's). */
static char g_line[MAXREQ][LINK_MAXLINE + 1];
static const char *g_script[MAXSCRIPT];

static void req_hook(const char *line, u16 len)
{
    u16 n;

    n = tl_count();
    if (n >= 1 && n <= MAXREQ) {
        if (len > LINK_MAXLINE)
            len = LINK_MAXLINE;
        memcpy(g_line[n - 1], line, len);
        g_line[n - 1][len] = '\0';
    }
    if (n >= 1 && n <= MAXSCRIPT && g_script[n - 1] != NULL)
        tl_fail(n, g_script[n - 1]);
}

/* Counters, the log and the script start again. */
static void begin(void)
{
    u16 i;

    g_nwait = 0;
    g_wstop = 0;
    g_nfail = 0;
    g_fstay = 0;
    for (i = 0; i < MAXSCRIPT; i++)
        g_script[i] = NULL;
    tl_clear();
}

/* Answer request n (from 1) with r1 and the next with r2 (NULL: the
   engine). */
static void script(u16 n, const char *r1, const char *r2)
{
    g_script[n - 1] = r1;
    g_script[n] = r2;
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

/* Request i (from 0) since begin was head, PAGE=n and AFTER=after. */
static void req_(u16 i, const char *head, u16 n, const char *after,
                 int line)
{
    char want[LINK_MAXLINE + 1];

    tf_check_str(g_line[i], mk(want, head, n, after), TF_FILE, line,
                 "request");
}
#define REQ(i, head, n, after) req_((i), (head), (n), (after), __LINE__)

/* ---------------------------------------------------------------------
 * The oracle.
 * --------------------------------------------------------------------- */
static u16 g_list[NITEMS];      /* the whole list's items, in order     */
static u16 g_nlist;
static u16 g_lead;              /* 1 when the list starts with a lead row */
static int g_idcol;             /* ITEM_ID's place in the R lines       */
static const char *g_lhead;     /* a page load's head                   */

static char g_eline[MAXREQ][LINK_MAXLINE + 1];  /* a survey's requests */
static u32 g_eafter[MAXREQ];    /* its rows counted after each          */
static u16 g_ereq;
static char g_estart[PSV_MAXPAGES][LV_CURSZ];   /* its page starts      */
static u16 g_epages;
static u32 g_erows;
static u8 g_eall;
static u16 g_ezero;             /* its requests that found no row and
                                   ended on a C line                    */
static u16 g_etail;             /* 1 when the list ended on a page of no
                                   row after a full one                 */

static int list_line(void *arg, const rq_line *ln)
{
    u16 i;

    TF_CHECK(arg == (void *)g_list);
    if (ln->kind == 'F') {
        g_idcol = -1;
        for (i = 0; i < ln->n; i++)
            if (strcmp(ln->v[i], "ITEM_ID") == 0)
                g_idcol = (int)i;
    }
    if (ln->kind == 'R' && TF_CHECK(g_idcol >= 0 && g_idcol < (int)ln->n)
        && TF_CHECK(g_nlist < NITEMS))
        g_list[g_nlist++] = (u16)num_of(ln->v[g_idcol]);
    return 0;
}

/* Page the list of whead, as a survey of pages of ps rows does, a lead
   row first when lead and ps > 1, to its end: what the survey must do,
   up to the start of page PSV_MAXPAGES, into g_e..., and the whole list
   into g_list.  A page that proves empty after a full one is not kept,
   page 0 of an empty list is.  lhead is the head of the list's page loads. */
static void oracle(const char *whead, const char *lhead, u8 ps, int lead)
{
    char cur[LV_CURSZ];
    char line[LINK_MAXLINE + 1];
    rq q;
    rq_res res;
    u16 have;
    u16 n;
    u16 got;
    int more;
    int cut;

    g_nlist = 0;
    g_lead = (u16)(lead && ps > 1);
    g_idcol = -1;
    g_lhead = lhead;
    g_ereq = 0;
    g_epages = 0;
    g_erows = 0;
    g_eall = 0;
    g_ezero = 0;
    g_etail = 0;
    cut = 0;
    cur[0] = '\0';
    more = 1;
    while (more) {
        if (g_epages == PSV_MAXPAGES)
            cut = 1;
        if (!cut)
            strcpy(g_estart[g_epages++], cur);
        have = g_epages == 1 && !cut ? g_lead : 0;
        if (!cut)
            g_erows += have;
        while (more && have < ps) {
            n = (u16)(ps - have);
            if (n > LINK_PAGEMAX)
                n = LINK_PAGEMAX;
            mk(line, whead, n, cur);
            rq_begin(&q, line);
            got = g_nlist;
            if (!TF_EQ(rq_send(tc_lk, &q, list_line, g_list, &res), RQ_OK))
                return;
            got = (u16)(g_nlist - got);
            TF_CHECK(got <= n);
            have = (u16)(have + got);
            if (!cut) {
                if (!TF_CHECK(g_ereq < MAXREQ))
                    return;
                strcpy(g_eline[g_ereq], line);
                g_erows += got;
                g_eafter[g_ereq++] = g_erows;
                if (got == 0 && res.more[0] != '\0')
                    g_ezero++;
            }
            if (res.more[0] == '\0')
                more = 0;
            else
                strcpy(cur, res.more);
        }
        if (have == 0 && g_epages > 1 && !cut) {
            g_epages--;
            g_etail = 1;
        }
    }
    g_eall = (u8)!cut;
}

/* ---------------------------------------------------------------------
 * Checks.
 * --------------------------------------------------------------------- */
/* The number in row i's text. */
static u32 row_id(pg *p, u16 i)
{
    char t[LV_ROWTEXT + 1];

    pg_text(p, i, t);
    return num_of(t);
}

/* 1 when the text of row i is the lead's, padded. */
static int is_lead(pg *p, u16 i)
{
    char t[LV_ROWTEXT + 1];

    pg_text(p, i, t);
    return strncmp(t, k_leadtext, sizeof k_leadtext - 1) == 0 &&
           t[sizeof k_leadtext - 1] == ' ' && strlen(t) == LV_ROWTEXT;
}

/* Rows in the pager's numbering: the list's and its lead row. */
#define TOTAL()         ((u32)g_nlist + g_lead)

/* The rows held are those of the pager's numbering from row at on. */
static void shows_(pg *p, u32 at, int line)
{
    u32 j;
    u16 n;
    u16 i;

    n = pg_rows(p);
    for (i = 0; i < n; i++) {
        j = at + i;
        if (!tf_check(j < TOTAL(), TF_FILE, line, "row in the list"))
            return;
        if (j < g_lead) {
            if (!tf_check(is_lead(p, i), TF_FILE, line, "lead row"))
                return;
        } else if (!tf_check(row_id(p, i) == g_list[j - g_lead], TF_FILE,
                             line, "row of the list"))
            return;
    }
}

/* above is known, the rows held follow it in the list, and the window
   and the selection are in them. */
static void above_(pg *p, int line)
{
    if (!tf_check(pg_rows(p) > 0, TF_FILE, line, "rows held") ||
        !tf_check(p->above != PG_NOABOVE, TF_FILE, line, "above known"))
        return;
    shows_(p, p->above, line);
    tf_check(p->first <= p->sel && p->sel < p->first + p->ps &&
             p->sel < pg_rows(p), TF_FILE, line, "selection in window");
}
#define ABOVE(p) above_((p), __LINE__)

/* The survey's page starts are the oracle's. */
static void starts_(psv *s, int line)
{
    char c[LV_CURSZ];
    u16 k;

    if (!tf_check(s->cur != NULL, TF_FILE, line, "cursor block"))
        return;
    for (k = 0; k < s->pages && k < PSV_MAXPAGES; k++) {
        cmem_get(s->cur, (u16)(k * LV_CURSZ), c, LV_CURSZ);
        c[LV_CURSZ - 1] = '\0';
        if (!tf_check_str(c, g_estart[k], TF_FILE, line, "page start"))
            return;
    }
}

/* s is a survey of nothing, its block kept or not. */
static void nothing_(psv *s, int line)
{
    tf_check_long((long)s->rows, 0L, TF_FILE, line, "rows 0");
    tf_check_long((long)s->pages, 0L, TF_FILE, line, "pages 0");
    tf_check_long((long)s->all, 0L, TF_FILE, line, "all 0");
}
#define NOTHING(s) nothing_((s), __LINE__)

static ks_set g_ks;              /* the keys of a survey                 */

/* keys is empty. */
static void nokeys_(ks_set *keys, int line)
{
    tf_check(keys->n == 0 && keys->b == NULL && keys->over == 0, TF_FILE,
             line, "no keys");
}
#define NOKEYS(keys) nokeys_((keys), __LINE__)

/* keys holds those of the rows the oracle's survey counted, the lead row
   excepted, in order, over 1 when the survey was cut. */
static void keys_(ks_set *keys, int line)
{
    char key[LV_KEYSZ];
    u16 n;
    u16 i;

    n = (u16)(g_erows - g_lead);
    tf_check_long((long)keys->over, (long)!g_eall, TF_FILE, line, "over");
    if (!tf_check_long((long)keys->n, (long)n, TF_FILE, line, "keys"))
        return;
    for (i = 0; i < n; i++)
        if (!tf_check(ks_get(keys, i, key) && num_of(key) == g_list[i],
                      TF_FILE, line, "key"))
            return;
}

/* Survey p into s, which must do as the oracle did: its requests,
   waits, counts and page starts, and the keys into g_ks, which holds
   another list's before; no failure, no stop. */
static void survey_(psv *s, pg *p, int line)
{
    u16 i;

    begin();
    if (g_ks.n == 0)
        ks_add(&g_ks, "99999");
    tf_check_long((long)psv_survey(s, p, &g_ks), (long)RQ_OK, TF_FILE,
                  line, "survey");
    keys_(&g_ks, line);
    tf_check(p->keyfn == NULL, TF_FILE, line, "key sink NULL");
    tf_check_long((long)s->rows, (long)g_erows, TF_FILE, line, "rows");
    tf_check_long((long)s->pages, (long)g_epages, TF_FILE, line, "pages");
    tf_check_long((long)s->all, (long)g_eall, TF_FILE, line, "all");
    if (!tf_check_long((long)tl_count(), (long)g_ereq, TF_FILE, line,
                       "requests"))
        return;
    for (i = 0; i < g_ereq; i++)
        if (!tf_check_str(g_line[i], g_eline[i], TF_FILE, line,
                          "survey request"))
            break;
    if (tf_check_long((long)g_nwait, (long)g_ereq - 1, TF_FILE, line,
                      "wait calls"))
        for (i = 0; i + 1 < g_ereq; i++)
            if (!tf_check(g_wwalk[i] == 1 && g_wn[i] == g_eafter[i],
                          TF_FILE, line, "progress"))
                break;
    tf_check_long((long)g_nfail, 0L, TF_FILE, line, "no failure");
    tf_check_long((long)p->stopped, 0L, TF_FILE, line, "not stopped");
    tf_check_long((long)p->walk, 0L, TF_FILE, line, "walk 0");
    starts_(s, line);
}
#define SURVEY(s, p) survey_((s), (p), __LINE__)
#define KEYS(keys) keys_((keys), __LINE__)

/* Jump p to row r with s, which must load the page of r, or the last
   kept, from its start, the window and the selection on its first row;
   s stays as it was. */
static void jump_(psv *s, pg *p, u32 r, int line)
{
    u32 rows;
    u32 k;
    u32 want;
    u16 pages;
    u16 n;
    u16 i;
    u8 all;

    rows = s->rows;
    pages = s->pages;
    all = s->all;
    if (!tf_check(pages > 0, TF_FILE, line, "pages kept"))
        return;
    k = r / p->ps;
    if (k >= pages)
        k = pages - 1;
    begin();
    tf_check_long((long)psv_jump(s, p, r), (long)RQ_OK, TF_FILE, line,
                  "jump");
    tf_check_long((long)p->above, (long)(k * p->ps), TF_FILE, line,
                  "above");
    tf_check(p->first == 0 && p->sel == 0, TF_FILE, line,
             "window and selection on the first row");
    tf_check_long((long)p->stopped, 0L, TF_FILE, line, "not stopped");
    tf_check_long((long)g_nfail, 0L, TF_FILE, line, "no failure");
    tf_check(s->rows == rows && s->pages == pages && s->all == all,
             TF_FILE, line, "survey kept");
    want = TOTAL() - k * p->ps;
    if (want > p->ps)
        want = p->ps;
    tf_check_long((long)pg_rows(p), (long)want, TF_FILE, line, "page rows");
    ABOVE(p);
    n = (u16)(p->ps - (k == 0 ? g_lead : 0));
    if (tf_check(tl_count() >= 1, TF_FILE, line, "load request"))
        req_(0, g_lhead, n > LINK_PAGEMAX ? LINK_PAGEMAX : n, g_estart[k],
             line);
    for (i = 1; i < tl_count(); i++)
        tf_check(tf_starts(g_line[i], g_lhead) &&
                 strstr(g_line[i], " AFTER=") != NULL, TF_FILE, line,
                 "load request");
}
#define JUMP(s, p, r) jump_((s), (p), (u32)(r), __LINE__)

/* The rows held, their marks, the window, the selection and above:
   recorded with check 0, compared with what was recorded with check 1. */
static void held_(pg *p, int check, int line)
{
    static char text[2 * LV_MAXPAGE][LV_ROWTEXT + 1];
    static u16 n;
    static u16 first;
    static u16 sel;
    static u32 above;
    static u8 order;
    char t[LV_ROWTEXT + 1];
    u16 i;

    if (!check) {
        n = pg_rows(p);
        first = p->first;
        sel = p->sel;
        above = p->above;
        order = p->order;
        for (i = 0; i < n; i++)
            pg_text(p, i, text[i]);
        return;
    }
    tf_check_long((long)pg_rows(p), (long)n, TF_FILE, line, "rows held");
    tf_check(p->first == first && p->sel == sel, TF_FILE, line,
             "window and selection");
    tf_check(p->above == above, TF_FILE, line, "above");
    tf_check(p->order == order, TF_FILE, line, "order");
    for (i = 0; i < n && i < pg_rows(p); i++) {
        pg_text(p, i, t);
        if (!tf_check_str(t, text[i], TF_FILE, line, "row held"))
            break;
    }
}
#define HELD(p, check) held_((p), (check), __LINE__)

/* ---------------------------------------------------------------------
 * The pager and the survey under test.
 * --------------------------------------------------------------------- */
static pg g_pg;
static psv g_sv;

/* Open g_pg over src for ps rows, a lead row when lead, the order when
   not NULL, at the first page; then the oracle of whead and lhead. */
static pg *open_(const lv_src *src, u8 ps, int lead, const char *order,
                 const char *whead, const char *lhead)
{
    begin();
    if (!TF_EQ(pg_open(&g_pg, tc_lk, src, ps, wait_fn, fail_fn, &g_tag),
               PG_OK))
        return NULL;
    if (lead)
        pg_lead(&g_pg, k_leadkey, k_leadtext);
    if (order != NULL)
        TF_EQ(pg_set_order(&g_pg, order), 1);
    TF_EQ(pg_move(&g_pg, PG_HOME), RQ_OK);
    oracle(whead, lhead, ps, lead);
    return &g_pg;
}

/* PgUp from a page jumped to, page k, ups times: each fetches the page
   above A from the start the jump pushed, until the ring's depth, past
   which the pager starts again at the first page. */
static void pgups_(pg *p, u16 k, u16 ups, int line)
{
    u16 i;
    u16 n;

    for (i = 1; i <= ups; i++) {
        begin();
        tf_check_long((long)pg_move(p, PG_PGUP), (long)RQ_OK, TF_FILE,
                      line, "PgUp");
        above_(p, line);
        if (i > k)
            continue;
        if (i <= LV_MAXBACK) {
            n = (u16)(p->ps - (i == k ? g_lead : 0));
            req_(0, g_lhead, n > LINK_PAGEMAX ? LINK_PAGEMAX : n,
                 g_estart[k - i], line);
            tf_check_long((long)p->above, (long)(k - i) * p->ps, TF_FILE,
                          line, "above after PgUp");
        } else if (i == LV_MAXBACK + 1) {
            tf_check_long((long)p->above, 0L, TF_FILE, line, "restart");
        }
    }
}
#define PGUPS(p, k, ups) pgups_((p), (k), (ups), __LINE__)

/* ---------------------------------------------------------------------
 * The tests.
 * --------------------------------------------------------------------- */
static u16 g_nalloc;            /* allocations of the cursors' size     */
static u16 g_nfree;             /* frees of the block watched           */
static int g_nomem;             /* fail allocations of the cursors' size */
static cmem_blk *g_watch;

static int fault_fn(u16 size)
{
    if (size != BLKSZ)
        return 0;
    g_nalloc++;
    return g_nomem;
}

/* Fail the keys' second block. */
static int keys_fault(u16 size)
{
    return size == 2 * KS_FIRST;
}

static void freeing_fn(cmem_blk *b)
{
    if (b != NULL && b == g_watch)
        g_nfree++;
}

/* psv_init, psv_free, the block and a survey without memory. */
static void test_init(void)
{
    psv s;
    pg *p;

    memset(&s, 0x5A, sizeof s);
    psv_init(&s);
    TF_CHECK(s.cur == NULL);
    NOTHING(&s);
    psv_free(&s);
    TF_CHECK(s.cur == NULL);
    NOTHING(&s);

    p = open_(&items_src, 10, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;
    cmem_fault = fault_fn;
    cmem_freeing = freeing_fn;
    g_nalloc = 0;
    g_nfree = 0;
    g_nomem = 1;
    g_watch = NULL;
    HELD(p, 0);
    begin();
    ks_add(&g_ks, "1");
    TF_EQ(psv_survey(&s, p, &g_ks), RQ_OK);
    TF_EQ(g_nalloc, 1);
    TF_EQ(tl_count(), 0);
    TF_CHECK(s.cur == NULL);
    NOTHING(&s);
    NOKEYS(&g_ks);
    TF_EQ(g_nfail, 0);
    TF_EQ(g_nwait, 0);
    HELD(p, 1);

    /* The first survey takes the block, the next ones keep it. */
    g_nomem = 0;
    g_nalloc = 0;
    SURVEY(&s, p);
    TF_EQ(g_nalloc, 1);
    TF_CHECK(s.cur != NULL);
    if (s.cur != NULL)
        TF_EQ(cmem_size(s.cur), BLKSZ);
    g_watch = s.cur;
    SURVEY(&s, p);
    TF_EQ(g_nalloc, 1);
    TF_CHECK(s.cur == g_watch);
    pg_close(p);
    p = open_(&items_src, 30, 0, NULL, ID_W, ID_L);
    if (p != NULL) {
        SURVEY(&s, p);
        pg_close(p);
    }
    TF_EQ(g_nalloc, 1);
    TF_CHECK(s.cur == g_watch);
    TF_EQ(g_nfree, 0);
    psv_free(&s);
    TF_EQ(g_nfree, 1);
    TF_CHECK(s.cur == NULL);
    NOTHING(&s);
    cmem_fault = NULL;
    cmem_freeing = NULL;
    g_watch = NULL;
}

/* Survey counts and requests over lists of many shapes. */
static void test_counts(void)
{
    static const u8 sizes[] = { 2, 5, 10, 24, 25, 26, 30, 50, LV_MAXPAGE };
    pg *p;
    u16 i;

    for (i = 0; i < sizeof sizes; i++) {
        p = open_(&items_src, sizes[i], 0, NULL, ID_W, ID_L);
        if (p == NULL)
            return;
        SURVEY(&g_sv, p);
        TF_EQ(g_sv.rows, NITEMS);
        TF_EQ(g_sv.pages, (NITEMS + sizes[i] - 1) / sizes[i]);
        TF_EQ(g_sv.all, 1);
        pg_close(p);
    }
    /* A page of more than LINK_PAGEMAX rows takes several requests. */
    p = open_(&items_src, 30, 0, NULL, ID_W, ID_L);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        REQ(0, ID_W, LINK_PAGEMAX, "");
        TF_CHECK(tf_starts(g_line[1], ID_W " PAGE=5 AFTER="));
        REQ(2, ID_W, LINK_PAGEMAX, g_estart[1]);
        pg_close(p);
    }

    /* The second order, a walk with the key field alone. */
    p = open_(&items_src, 10, 0, "MFR", MFR_W, MFR_L);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        TF_EQ(g_sv.rows, NITEMS);
        pg_close(p);
    }
    /* Without a walk field, the order's own field list. */
    p = open_(&nowalk_src, 10, 0, NULL, ID_L, ID_L);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        TF_EQ(g_sv.rows, NITEMS);
        TF_EQ(g_sv.pages, NITEMS / 10);
        pg_close(p);
    }

    /* An empty list: page 0 kept, no row. */
    p = open_(&none_src, 10, 0, NULL, NONE_W, NONE_W);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        TF_EQ(g_sv.rows, 0);
        TF_EQ(g_sv.pages, 1);
        TF_EQ(g_sv.all, 1);
        pg_close(p);
    }
    /* With a lead row it counts the lead row alone. */
    p = open_(&none_src, 10, 1, NULL, NONE_W, NONE_W);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        REQ(0, NONE_W, 9, "");
        TF_EQ(g_sv.rows, 1);
        TF_EQ(g_sv.pages, 1);
        TF_EQ(g_sv.all, 1);
        pg_close(p);
    }
}

/* A lead row: row 0 of page 0, which holds ps - 1 rows of the list; a
   window of one row has none. */
static void test_lead(void)
{
    pg *p;

    p = open_(&card_src, 6, 1, NULL, CARD_W, CARD_L);
    if (p == NULL)
        return;
    SURVEY(&g_sv, p);
    REQ(0, CARD_W, 5, "");
    TF_EQ(g_sv.rows, NCARD + 1);
    TF_EQ(g_sv.pages, (NCARD + 1 + 5) / 6);
    TF_EQ(g_sv.all, 1);
    pg_close(p);

    p = open_(&card_src, 1, 1, NULL, CARD_W, CARD_L);
    if (p == NULL)
        return;
    SURVEY(&g_sv, p);
    REQ(0, CARD_W, 1, "");
    TF_EQ(g_sv.rows, NCARD);
    pg_close(p);
}

/* Filtered lists: the server's bounded scans end requests before a page
   is full, some with no row at all, and the survey asks again for the
   same page. */
static void test_filtered(void)
{
    static const u8 sizes[] = { 1, 2, 3, 10, 30 };
    pg *p;
    u16 i;

    for (i = 0; i < sizeof sizes; i++) {
        p = open_(&zq_src, sizes[i], 0, NULL, ZQ_W, ZQ_L);
        if (p == NULL)
            return;
        TF_EQ(g_nlist, NITEMS / ZQ_EVERY);
        SURVEY(&g_sv, p);
        TF_EQ(g_sv.rows, NITEMS / ZQ_EVERY);
        TF_EQ(g_sv.all, 1);
        if (sizes[i] >= 10)
            /* Not vacuous: a page took more than one request. */
            TF_CHECK(g_ereq > g_epages);
        pg_close(p);
    }
    for (i = 0; i < sizeof sizes; i++) {
        p = open_(&gap_src, sizes[i], 0, NULL, GAP_W, GAP_L);
        if (p == NULL)
            return;
        TF_EQ(g_nlist, 3);
        if (sizes[i] == 2)
            TF_CHECK(g_ezero > 0);
        SURVEY(&g_sv, p);
        TF_EQ(g_sv.rows, 3);
        TF_EQ(g_sv.all, 1);
        pg_close(p);
    }
}

/* A list longer than the table: the survey stops at the start of page
   PSV_MAXPAGES with every page kept, all 0. */
static void test_table(void)
{
    pg *p;

    TF_CHECK(PSV_MAXPAGES < NITEMS);
    p = open_(&items_src, 1, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;
    SURVEY(&g_sv, p);
    TF_EQ(g_sv.pages, PSV_MAXPAGES);
    TF_EQ(g_sv.rows, PSV_MAXPAGES);
    TF_EQ(g_sv.all, 0);
    TF_EQ(tl_count(), PSV_MAXPAGES);
    pg_close(p);

    /* Pages of two rows fit. */
    p = open_(&items_src, 2, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;
    SURVEY(&g_sv, p);
    TF_EQ(g_sv.pages, NITEMS / 2);
    TF_EQ(g_sv.all, 1);
    pg_close(p);
}

/* The wait function stops a survey: between a page's requests and
   between pages, nothing kept. */
static void test_wait(void)
{
    pg *p;
    u16 stop;

    p = open_(&items_src, 30, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;
    HELD(p, 0);
    for (stop = 1; stop <= 4; stop++) {
        SURVEY(&g_sv, p);
        begin();
        g_wstop = stop;
        TF_EQ(psv_survey(&g_sv, p, &g_ks), RQ_OK);
        NOKEYS(&g_ks);
        TF_EQ(g_nwait, stop);
        TF_EQ(tl_count(), stop);
        TF_EQ(g_wn[stop - 1], g_eafter[stop - 1]);
        TF_EQ(p->stopped, 1);
        TF_EQ(p->walk, 0);
        TF_EQ(g_nfail, 0);
        NOTHING(&g_sv);
        HELD(p, 1);
    }
    /* The next survey goes through, and leaves stopped 0. */
    SURVEY(&g_sv, p);
    HELD(p, 1);
    pg_close(p);
}

/* The keys: a survey without a set counts as one with it; the key sink
   off after a survey, so End collects nothing; a set without memory to
   grow keeps its first keys, over, while the count goes on. */
static void test_keys(void)
{
    ks_set keep;
    pg *p;

    p = open_(&items_src, 10, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;
    begin();
    TF_EQ(psv_survey(&g_sv, p, NULL), RQ_OK);
    TF_EQ(g_sv.rows, NITEMS);
    TF_EQ(g_sv.all, 1);
    TF_EQ(tl_count(), g_ereq);
    TF_CHECK(p->keyfn == NULL);
    SURVEY(&g_sv, p);
    keep = g_ks;
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    TF_CHECK(g_ks.n == keep.n && g_ks.b == keep.b);
    KEYS(&g_ks);
    pg_close(p);

    p = open_(&items_src, 30, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;
    cmem_fault = keys_fault;
    begin();
    TF_EQ(psv_survey(&g_sv, p, &g_ks), RQ_OK);
    cmem_fault = NULL;
    TF_EQ(g_sv.rows, NITEMS);
    TF_EQ(g_sv.all, 1);
    TF_EQ(g_ks.n, KS_FIRST / KS_KEYW);
    TF_EQ(g_ks.over, 1);
    pg_close(p);
}

/* A failed request ends a survey with nothing kept, its response given
   to the fail function once; a bad cursor as any other failure. */
static void test_fail(void)
{
    static const u16 at[] = { 1, 2, 3, 7 };
    pg *p;
    u16 i;
    u16 stay;

    p = open_(&items_src, 10, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;
    TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    HELD(p, 0);
    for (i = 0; i < sizeof at / sizeof at[0]; i++)
        for (stay = 0; stay < 2; stay++) {
            SURVEY(&g_sv, p);
            begin();
            g_fstay = (int)stay;
            script(at[i], stay ? BADCUR : BUSY, NULL);
            TF_EQ(psv_survey(&g_sv, p, &g_ks), stay ? RQ_ECURSOR : 402);
            NOKEYS(&g_ks);
            TF_EQ(tl_count(), at[i]);
            TF_EQ(g_nfail, 1);
            TF_EQ(g_fcode[0], stay ? RQ_ECURSOR : 402);
            TF_EQ(p->stopped, 0);
            TF_EQ(p->walk, 0);
            NOTHING(&g_sv);
            HELD(p, 1);
        }
    pg_close(p);
}

/* What the pager shows stays as it was: rows held in A and B, the
   window, the selection, above, the order and the ring, whose cursors
   PgUp still fetches. */
static void test_held(void)
{
    pg *p;
    u16 i;

    p = open_(&items_src, 10, 0, "MFR", MFR_W, MFR_L);
    if (p == NULL)
        return;
    for (i = 0; i < 3; i++)
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
    for (i = 0; i < 14; i++)
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
    TF_CHECK(pg_rows(p) > p->ps && p->first > 0);
    ABOVE(p);
    HELD(p, 0);
    SURVEY(&g_sv, p);
    HELD(p, 1);
    TF_STREQ(pg_order_name(p), "MFR");
    for (i = 0; i < 6; i++) {
        TF_EQ(pg_move(p, PG_PGUP), RQ_OK);
        ABOVE(p);
    }
    TF_CHECK(p->above == 0 && p->sel == 0);
    pg_close(p);

    /* A stopped fill before the survey: stopped goes back to 0. */
    p = open_(&zq_src, 10, 0, NULL, ZQ_W, ZQ_L);
    if (p == NULL)
        return;
    begin();
    g_wstop = 1;
    TF_EQ(pg_move(p, PG_HOME), RQ_OK);
    TF_EQ(p->stopped, 1);
    HELD(p, 0);
    SURVEY(&g_sv, p);
    HELD(p, 1);
    pg_close(p);
}

/* A jump to a row of every page, to the last row and past it. */
static void test_jump_pages(void)
{
    static const u8 sizes[] = { 5, 30 };
    pg *p;
    u16 i;
    u16 k;

    for (i = 0; i < sizeof sizes; i++) {
        p = open_(&items_src, sizes[i], 0, NULL, ID_W, ID_L);
        if (p == NULL)
            return;
        SURVEY(&g_sv, p);
        for (k = 0; k < g_sv.pages; k++)
            JUMP(&g_sv, p, (u32)k * sizes[i] + (u32)(k % sizes[i]));
        for (k = g_sv.pages; k-- > 0;)
            JUMP(&g_sv, p, (u32)k * sizes[i] + sizes[i] - 1);
        JUMP(&g_sv, p, NITEMS - 1);
        JUMP(&g_sv, p, NITEMS);
        JUMP(&g_sv, p, 0xFFFFFFFFUL);
        JUMP(&g_sv, p, 0);
        pg_close(p);
    }
    /* A page of more than LINK_PAGEMAX rows is loaded in one fill. */
    p = open_(&items_src, 30, 0, NULL, ID_W, ID_L);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        JUMP(&g_sv, p, 100);
        TF_EQ(tl_count(), 2);
        TF_CHECK(tf_starts(g_line[1], ID_L " PAGE=5 AFTER="));
        pg_close(p);
    }

    /* The second order. */
    p = open_(&items_src, 10, 0, "MFR", MFR_W, MFR_L);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        for (k = 0; (u32)k * 10 < TOTAL(); k++)
            JUMP(&g_sv, p, (u32)k * 10 + 9);
        pg_close(p);
    }

    /* A lead row: page 0 shows it first, the later pages start a row
       earlier in the list. */
    p = open_(&card_src, 6, 1, NULL, CARD_W, CARD_L);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        for (k = 0; k <= NCARD + 1; k++)
            JUMP(&g_sv, p, k);
        JUMP(&g_sv, p, 0);
        TF_CHECK(is_lead(p, 0));
        pg_close(p);
    }
    p = open_(&none_src, 10, 1, NULL, NONE_W, NONE_W);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        JUMP(&g_sv, p, 0);
        TF_CHECK(is_lead(p, 0));
        pg_close(p);
    }

    /* Filtered lists, their pages filled by several requests. */
    p = open_(&zq_src, 3, 0, NULL, ZQ_W, ZQ_L);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        for (k = 0; k < NITEMS / ZQ_EVERY; k++)
            JUMP(&g_sv, p, k);
        pg_close(p);
    }
    p = open_(&gap_src, 2, 0, NULL, GAP_W, GAP_L);
    if (p != NULL) {
        SURVEY(&g_sv, p);
        for (k = 0; k < 3; k++)
            JUMP(&g_sv, p, k);
        pg_close(p);
    }
}

/* After a jump the pager's moves keep above: up past the ring's depth to
   the restart at the first page, down, End. */
static void test_jump_moves(void)
{
    pg *p;
    u16 i;

    p = open_(&items_src, 5, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;
    SURVEY(&g_sv, p);
    JUMP(&g_sv, p, 45 * 5 + 2);
    PGUPS(p, 45, LV_MAXBACK + 2);
    TF_CHECK(p->above == 0 && row_id(p, 0) == 1);
    JUMP(&g_sv, p, 10 * 5);
    PGUPS(p, 10, 11);
    TF_CHECK(p->above == 0 && row_id(p, 0) == 1);
    JUMP(&g_sv, p, 45 * 5);
    for (i = 0; i < (u16)(LV_MAXBACK * 5 + 5 + 3); i++) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(row_id(p, p->sel), 1);
    JUMP(&g_sv, p, 20 * 5);
    for (i = 0; i < 30; i++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        ABOVE(p);
    }
    for (i = 0; i < 4; i++) {
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    ABOVE(p);
    TF_EQ(p->above + pg_rows(p), NITEMS);
    pg_close(p);

    /* A lead row's list, up to the lead row and End. */
    p = open_(&card_src, 6, 1, NULL, CARD_W, CARD_L);
    if (p == NULL)
        return;
    SURVEY(&g_sv, p);
    JUMP(&g_sv, p, NCARD);
    PGUPS(p, NCARD / 6, NCARD / 6);
    TF_CHECK(is_lead(p, 0));
    JUMP(&g_sv, p, 13);
    for (i = 0; i < NCARD; i++) {
        TF_EQ(pg_move(p, PG_UP), RQ_OK);
        ABOVE(p);
    }
    TF_CHECK(is_lead(p, p->sel));
    JUMP(&g_sv, p, 7);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    ABOVE(p);
    TF_EQ(p->above + pg_rows(p), NCARD + 1);
    pg_close(p);

    /* A list longer than the table: past the last page kept the jump
       loads that page, and the moves go on from it. */
    p = open_(&items_src, 1, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;
    SURVEY(&g_sv, p);
    JUMP(&g_sv, p, NITEMS - 5);
    TF_EQ(p->above, PSV_MAXPAGES - 1);
    for (i = 0; i < NITEMS - PSV_MAXPAGES; i++) {
        TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
        ABOVE(p);
    }
    TF_EQ(row_id(p, p->sel), NITEMS);
    JUMP(&g_sv, p, PSV_MAXPAGES - 1);
    PGUPS(p, PSV_MAXPAGES - 1, LV_MAXBACK + 1);
    JUMP(&g_sv, p, NITEMS - 5);
    TF_EQ(pg_move(p, PG_END), RQ_OK);
    ABOVE(p);
    TF_EQ(p->above + pg_rows(p), NITEMS);
    pg_close(p);
}

/* psv_jump's failures and stops. */
static void test_jump_fail(void)
{
    pg *p;
    psv keep;

    p = open_(&items_src, 5, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;

    /* Another failure: given to the fail function, the survey kept. */
    SURVEY(&g_sv, p);
    JUMP(&g_sv, p, 50);
    keep = g_sv;
    begin();
    script(1, BUSY, NULL);
    g_fstay = 1;
    TF_EQ(psv_jump(&g_sv, p, 120), 402);
    TF_EQ(g_nfail, 1);
    TF_EQ(g_fcode[0], 402);
    TF_EQ(tl_count(), 1);
    TF_CHECK(g_sv.rows == keep.rows && g_sv.pages == keep.pages &&
             g_sv.all == keep.all && g_sv.cur == keep.cur);
    TF_EQ(p->above, 50);
    JUMP(&g_sv, p, 120);

    /* A bad cursor, the fail function leaving the list: no restart, the
       survey forgotten, its block kept. */
    begin();
    script(1, BADCUR, NULL);
    TF_EQ(psv_jump(&g_sv, p, 200), RQ_ECURSOR);
    TF_EQ(g_nfail, 1);
    TF_EQ(g_fcode[0], RQ_ECURSOR);
    TF_EQ(tl_count(), 1);
    NOTHING(&g_sv);
    TF_CHECK(g_sv.cur == keep.cur);

    /* Staying: the pager starts again at the first page, RQ_OK. */
    SURVEY(&g_sv, p);
    begin();
    script(1, BADCUR, NULL);
    g_fstay = 1;
    TF_EQ(psv_jump(&g_sv, p, 200), RQ_OK);
    TF_EQ(g_nfail, 1);
    TF_EQ(g_fcode[0], RQ_ECURSOR);
    TF_EQ(tl_count(), 2);
    REQ(1, ID_L, 5, "");
    NOTHING(&g_sv);
    TF_EQ(p->above, 0);
    TF_EQ(row_id(p, 0), 1);
    TF_CHECK(p->first == 0 && p->sel == 0);
    ABOVE(p);

    /* The restart fails too: both reported, the second code returned. */
    SURVEY(&g_sv, p);
    begin();
    script(1, BADCUR, BUSY);
    g_fstay = 1;
    TF_EQ(psv_jump(&g_sv, p, 200), 402);
    TF_EQ(g_nfail, 2);
    TF_EQ(g_fcode[0], RQ_ECURSOR);
    TF_EQ(g_fcode[1], 402);
    TF_EQ(tl_count(), 2);
    NOTHING(&g_sv);
    pg_close(p);

    /* The wait function stops the fill: the rows fetched shown, stopped
       1; the next jump clears it. */
    p = open_(&items_src, 30, 0, NULL, ID_W, ID_L);
    if (p == NULL)
        return;
    SURVEY(&g_sv, p);
    begin();
    g_wstop = 1;
    TF_EQ(psv_jump(&g_sv, p, 100), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(p->stopped, 1);
    TF_EQ(g_nfail, 0);
    TF_EQ(p->above, 90);
    TF_EQ(pg_rows(p), LINK_PAGEMAX);
    ABOVE(p);
    TF_EQ(g_sv.pages, g_epages);
    JUMP(&g_sv, p, 100);
    pg_close(p);
}

/* A filtered list whose last full page ended on a C line: the page after
   it proves empty and is not kept, so a jump to the last row, or past it,
   loads the last page with rows, and the moves from it stay exact. */
static void test_tail(void)
{
    static const u8 sizes[] = { 1, 2, 5 };
    pg *p;
    u16 i;

    for (i = 0; i < sizeof sizes; i++) {
        p = open_(&card_src, sizes[i], 0, NULL, CARD_W, CARD_L);
        if (p == NULL)
            return;
        /* Not vacuous: the server's last C line led to no row. */
        TF_EQ(g_etail, 1);
        SURVEY(&g_sv, p);
        TF_EQ(g_sv.pages, NCARD / sizes[i]);
        TF_EQ(g_sv.rows, NCARD);
        TF_EQ(g_sv.all, 1);
        JUMP(&g_sv, p, NCARD - 1);
        TF_EQ(row_id(p, (u16)(sizes[i] - 1)), NCARD);
        JUMP(&g_sv, p, NCARD);
        TF_EQ(row_id(p, 0), NCARD - sizes[i] + 1);
        TF_EQ(pg_move(p, PG_PGDN), RQ_OK);
        ABOVE(p);
        TF_EQ(p->above, NCARD - sizes[i]);
        TF_EQ(row_id(p, p->sel), NCARD);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        ABOVE(p);
        TF_EQ(p->above + pg_rows(p), NCARD);
        TF_EQ(row_id(p, p->sel), NCARD);
        JUMP(&g_sv, p, 0);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        ABOVE(p);
        TF_EQ(p->above + pg_rows(p), NCARD);
        pg_close(p);
    }
}

/* "ITEM <verb> <id>" for verb DEL or UNDEL. */
static void item_op(const char *verb, u16 id)
{
    char line[24];

    sprintf(line, "ITEM %s %u", verb, (unsigned)id);
    TF_EQ(tc_send(line), RQ_OK);
}

/* A list of exactly PSV_MAXPAGES pages, the server ending it on its
   last row, is surveyed whole; one a row longer is cut there, all 0.
   The first items are deleted so that the list still ends at the
   index's end. */
static void test_exact(void)
{
    u16 extra;
    u16 i;
    pg *p;

    for (i = 1; i <= NITEMS - PSV_MAXPAGES; i++)
        item_op("DEL", i);
    for (extra = 0; extra < 2; extra++) {
        if (extra)
            item_op("UNDEL", NITEMS - PSV_MAXPAGES);
        p = open_(&items_src, 1, 0, NULL, ID_W, ID_L);
        if (p == NULL)
            break;
        TF_EQ(g_nlist, PSV_MAXPAGES + extra);
        SURVEY(&g_sv, p);
        TF_EQ(g_sv.pages, PSV_MAXPAGES);
        TF_EQ(g_sv.rows, PSV_MAXPAGES);
        TF_EQ(g_sv.all, (u8)!extra);
        TF_EQ(tl_count(), PSV_MAXPAGES);
        JUMP(&g_sv, p, PSV_MAXPAGES - 1);
        TF_EQ(row_id(p, 0), NITEMS - extra);
        JUMP(&g_sv, p, PSV_MAXPAGES + 5);
        for (i = 0; i < 3; i++) {
            TF_EQ(pg_move(p, PG_DOWN), RQ_OK);
            ABOVE(p);
        }
        TF_EQ(row_id(p, p->sel), NITEMS);
        TF_EQ(pg_move(p, PG_END), RQ_OK);
        ABOVE(p);
        TF_EQ(p->above + pg_rows(p), PSV_MAXPAGES + extra);
        for (i = 0; i < 3; i++) {
            TF_EQ(pg_move(p, PG_UP), RQ_OK);
            ABOVE(p);
        }
        pg_close(p);
    }
    for (i = 1; i < NITEMS - PSV_MAXPAGES; i++)
        item_op("UNDEL", i);
}

/* An unpaged list: the categories, NCATADD added to the engine's
   seeds, read whole into g_cat. */
#define NCATADD         30      /* categories added                     */
static char g_cat[LINK_MAXCODES][LINK_CATLEN + 1];
static u16 g_ncat;

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

static int cat_line(void *arg, const rq_line *ln)
{
    TF_CHECK(arg == (void *)g_cat);
    if (ln->kind == 'R' && TF_CHECK(g_ncat < LINK_MAXCODES))
        strcpy(g_cat[g_ncat++], ln->v[0]);
    return 0;
}

static void test_unpaged(void)
{
    char line[40];
    char key[LV_KEYSZ];
    char t[LV_ROWTEXT + 1];
    rq q;
    rq_res res;
    pg *p;
    u16 lead;
    u16 pages;
    u16 k;
    u16 i;

    for (i = 1; i <= NCATADD; i++) {
        sprintf(line, "CAT ADD U%02u CAT_NAME=N%u", (unsigned)i,
                (unsigned)i);
        TF_EQ(tc_send(line), RQ_OK);
    }
    g_ncat = 0;
    rq_begin(&q, "CAT LIST");
    TF_EQ(rq_send(tc_lk, &q, cat_line, g_cat, &res), RQ_OK);
    if (!TF_EQ(g_ncat, 10 + NCATADD))
        return;
    for (lead = 0; lead < 2; lead++) {
        begin();
        if (!TF_EQ(pg_open(&g_pg, tc_lk, &cat_src, 6, wait_fn, fail_fn,
                           &g_tag), PG_OK))
            return;
        p = &g_pg;
        pg_unpaged(p);
        if (lead)
            pg_lead(p, k_leadkey, k_leadtext);
        TF_EQ(pg_move(p, PG_HOME), RQ_OK);
        begin();
        TF_EQ(psv_survey(&g_sv, p, &g_ks), RQ_OK);
        pages = (u16)((g_ncat + lead + 5) / 6);
        TF_EQ(g_sv.rows, g_ncat + lead);
        TF_EQ(g_sv.pages, pages);
        TF_EQ(g_sv.all, 1);
        TF_EQ(tl_count(), pages);
        for (i = 0; i < tl_count(); i++)
            TF_STREQ(g_line[i], "CAT LIST");
        TF_EQ(g_nwait, pages - 1);
        TF_EQ(g_ks.over, 0);
        if (TF_EQ(g_ks.n, g_ncat))
            for (i = 0; i < g_ncat; i++) {
                ks_get(&g_ks, i, key);
                TF_STREQ(key, g_cat[i]);
            }
        for (k = 0; k < pages; k++) {
            begin();
            TF_EQ(psv_jump(&g_sv, p, (u32)k * 6), RQ_OK);
            TF_EQ(tl_count(), 1);
            TF_EQ(p->above, k * 6);
            for (i = 0; i < pg_rows(p); i++) {
                pg_text(p, i, t);
                if (k == 0 && i == 0 && lead)
                    TF_CHECK(is_lead(p, 0));
                else
                    TF_CHECK(strncmp(t, g_cat[k * 6 + i - lead],
                                     strlen(g_cat[k * 6 + i - lead])) == 0);
            }
        }
        pg_close(p);
    }
}

int main(int argc, char **argv)
{
    link_clock clk;
    char why[LINK_WHYSZ];
    int rc;

    tf_begin("T_PGSV");
    cmem_init();
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_PGSV dir\n");
        return 2;
    }
    strcpy(g_ddir, argv[1]);
    te_remove(g_ddir, NULL);
    tc_clock(&clk);
    rc = link_open_local(&tc_lk, g_ddir, &clk, why, sizeof why);
    if (!TF_EQ(rc, 0)) {
        printf("T_PGSV: %s\n", why);
        return tf_done();
    }
    if (!tc_admin())
        return tf_done();
    add_items();
    tl_hook = req_hook;
    psv_init(&g_sv);
    ks_init(&g_ks);
    test_init();
    test_counts();
    test_lead();
    test_filtered();
    test_table();
    test_wait();
    test_keys();
    test_fail();
    test_held();
    test_jump_pages();
    test_jump_moves();
    test_jump_fail();
    test_tail();
    test_exact();
    test_unpaged();
    psv_free(&g_sv);
    ks_free(&g_ks);
    tl_hook = NULL;
    TF_EQ(link_close(tc_lk), 0);
    te_remove(g_ddir, NULL);
    return tf_done();
}
