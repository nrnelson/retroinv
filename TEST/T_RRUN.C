/*
 * T_RRUN.C - the run of a report or label (SRC/CLIENT/RPTRUN.C) over
 * the engine, checked against the contract of RPTRUN.H alone.
 *
 * usage: T_RRUN dir [msgdir]
 *
 * dir is a scratch data directory prefix holding nothing but this
 * test's files (its engine files and its output file are removed first
 * and last); msgdir is the directory of the built INV.MSG (default: the
 * program's own directory).  The test links the recording link of
 * TRLINK.H, so the requests of each run are checked word by word, and a
 * chosen request can be made to fail.  The layout goes into a text file
 * (a PR_FILE job), which is searched for the lines that tell a run
 * that ended in full from one that was cut short.
 *
 * Covered:
 * - the keys of the filters;
 * - rrun_open and its allocation failure;
 * - rrun_layout and its failure;
 * - rrun_one;
 * - the requests of every report kind with and without filters, their pages
 *   with the cursors, the step function's calls (more, pages, records so
 *   far);
 * - the tree's order of the whole tree and below a location, its group
 *   headings (a location gone since its rows came headed by its name),
 *   the items of no location first, and the bin labels with their paths
 *   built from LOC LIST across its pages;
 * - the options of the layout (location groups, the overdue title);
 * - a stop by the step function in the middle and on the last page, and in
 *   a GET run;
 * - a failed request, first and later, with the response in res;
 * - the record of a GET that does not exist;
 * - a job that fails while a page is laid out;
 * - a printer job's wait function, asked about a failed write only once
 *   the response is read;
 * - a row wider than RPT_ROWMAX, cut where its bytes run out;
 * - the count and the choice between ending and aborting that rrun_close
 *   makes.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "PRINT.H"
#include "RPT.H"
#include "RPTRUN.H"
#include "TRLINK.H"
#include "TEWIPE.H"
#define TF_MALLOC
#define TF_LINK
#include "TESTFW.H"
#include "T_FILES.H"

#define PATHMAX         96
#define NITEMS          60      /* items 1..NITEMS: three pages         */
#define NCARD           20      /* the first of them are in CARD        */
#define NDISP           2       /* the last of them are disposed        */
#define STEPMAX         8
#define FILEMAX         24000   /* bytes of output read back            */
#define ASOF            "20261009"
#define SUB             "Subtitle Q7"

/* The texts of RPT.MSG that tell a complete run from a cut one. */
#define S_TOTAL         "Total items:"
#define S_STOPPED       "Report stopped before the end"

static const rpt_geom g_geom = { 66, 40, 6 };

static rrun g_u;
static pr *g_p;
static rq_res g_res;
static int g_rc;
static u32 g_cnt;               /* rrun_close's count                   */
static char g_out[TFS_MAXDIR + 13];
static char g_buf[FILEMAX + 1]; /* the output file                      */

/* What the step function was called with. */
typedef struct {
    int  calls;
    int  more[STEPMAX];
    u16  pages[STEPMAX];
    u32  count[STEPMAX];
    int  stop_at;               /* the call that returns 1, 0 for none  */
} steprec;

static steprec g_sr;

static int step(void *arg, int more)
{
    steprec *s;

    TF_CHECK(arg == (void *)&g_sr);
    s = (steprec *)arg;
    if (s->calls < STEPMAX) {
        s->more[s->calls] = more;
        s->pages[s->calls] = g_u.pages;
        s->count[s->calls] = g_u.r != NULL ? rpt_count(g_u.r) : 0;
    }
    s->calls++;
    return s->stop_at != 0 && s->calls == s->stop_at;
}

/* ---------------------------------------------------------------------
 * The data: NITEMS items, odd ones in A1 and even in B2, the first
 * NCARD in CARD and the rest in SYS, the last NDISP disposed; the
 * locations A1 (LOC_ID 1, a shelf at the top level), B2 (2, a box in
 * A1) and C3 (3, a room at the top level, empty), each named as its
 * code; two open loans, of items 1 and 2, due before and after ASOF.
 * --------------------------------------------------------------------- */
static void add_data(void)
{
    char num[12];
    char line[64];
    rq q;
    rq_res res;
    u16 i;

    TF_EQ(tc_send("LOC ADD LOC_NAME=B2 LTYPE=BOX LOC_CODE=B2 PARENT_ID=A1"),
          RQ_OK);
    TF_EQ(tc_send("LOC ADD LOC_NAME=C3 LTYPE=ROOM LOC_CODE=C3"), RQ_OK);
    for (i = 1; i <= NITEMS; i++) {
        rq_begin(&q, "ITEM ADD");
        rq_pair(&q, "CATEGORY", i <= NCARD ? "CARD" : "SYS");
        rq_pair(&q, "MFR", "MAKER");
        strcpy(line, "M");
        txt_u32(line + 1, i);
        rq_pair(&q, "MODEL", line);
        rq_pair(&q, "LOC_ID", i % 2 != 0 ? "A1" : "B2");
        if (!TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK))
            return;
    }
    for (i = NITEMS - NDISP + 1; i <= NITEMS; i++) {
        strcpy(line, "ITEM SET ");
        txt_u32(num, i);
        strcat(line, num);
        strcat(line, " STATUS=D");
        TF_EQ(tc_send(line), RQ_OK);
    }
    TF_EQ(tc_send("LOAN OUT 1 HOLDER=Smith DATE=20260901 DUE=20261001"),
          RQ_OK);
    TF_EQ(tc_send("LOAN OUT 2 HOLDER=Jones DUE=20261201"), RQ_OK);
}

/* ---------------------------------------------------------------------
 * Running.
 * --------------------------------------------------------------------- */

/* 1 when the request req has the word tok (KEY=value) as one of its
   words. */
static int has(const char *req, const char *tok)
{
    const char *p;
    size_t n;

    n = strlen(tok);
    for (p = req; (p = strstr(p, tok)) != NULL; p++)
        if (p > req && p[-1] == ' ' && (p[n] == ' ' || p[n] == '\0'))
            return 1;
    return 0;
}

/* 1 when the request req has a pair KEY=, whatever its value. */
static int haskey(const char *req, const char *key)
{
    char w[16];

    strcpy(w, " ");
    strcat(w, key);
    strcat(w, "=");
    return strstr(req, w) != NULL;
}

/* 1 when the output holds s. */
static int outhas(const char *s)
{
    return strstr(g_buf, s) != NULL;
}

/* End the job, ignoring how it ended. */
static void jobend(void)
{
    int cferr;

    pr_close(g_p, &cferr);
}

/* Start a run of kind with the filters empty, on a new file job. */
static void begin(u8 kind)
{
    int cferr;

    memset(&g_u, 0, sizeof g_u);
    memset(&g_sr, 0, sizeof g_sr);
    g_u.kind = kind;
    strcpy(g_u.date, ASOF);
    strcpy(g_out, tfs_path("RRUN", ".TXT"));
    TF_EQ(pr_open(&g_p, PR_FILE, PR_PLAIN, 0, g_out, NULL, NULL, &cferr),
          PR_OK);
    g_u.p = g_p;
}

/* rrun_open, rrun_layout, the request n of the run answered with resp
   (n 0 for none), rrun_go.  The run is left open. */
static void run(u16 failn, const char *resp)
{
    TF_EQ(rrun_open(&g_u), PR_OK);
    TF_EQ(rrun_layout(&g_u, &g_geom, SUB), PR_OK);
    TF_CHECK(g_u.r != NULL);
    tl_clear();
    if (failn != 0)
        tl_fail(failn, resp);
    memset(&g_res, 0, sizeof g_res);
    g_rc = rrun_go(&g_u, tc_lk, step, &g_sr, &g_res);
}

/* End the run with rrun_close and the job, and read the output. */
static void finish(void)
{
    u16 got;

    g_cnt = rrun_close(&g_u, g_rc);
    jobend();
    got = 0;
    TF_EQ(tfs_read_all(g_out, g_buf, FILEMAX, &got), 0);
    g_buf[got] = '\0';
}

static void run_all(void)
{
    run(0, NULL);
    finish();
}

/* The output of a run that ended in full: titled, with the totals, one
   of whose texts is total. */
static void check_complete(const char *total)
{
    TF_CHECK(outhas(SUB));
    TF_CHECK(outhas(total));
    TF_CHECK(!outhas(S_STOPPED));
}

/* The output of a run that was cut short: the stop line, no totals. */
static void check_cut(void)
{
    TF_CHECK(outhas(S_STOPPED));
    TF_CHECK(!outhas(S_TOTAL));
}

/* ---------------------------------------------------------------------
 * Tests.
 * --------------------------------------------------------------------- */
static void test_keys(void)
{
    TF_STREQ(rrun_k_cat, "CAT");
    TF_STREQ(rrun_k_loc, "LOC");
    TF_STREQ(rrun_k_status, "STATUS");
    TF_STREQ(rrun_k_order, "ORDER");
    TF_STREQ(rrun_k_overdue, "OVERDUE");
    TF_STREQ(rrun_k_asof, "ASOF");
}

static int fail_cmem(u16 size)
{
    TF_CHECK(size > 0);
    return 1;
}

static void test_lifecycle(void)
{
    u16 held;

    begin(RPT_LIST);
    held = tm_held();
    TF_EQ(rrun_open(&g_u), PR_OK);
    TF_CHECK(g_u.pg != NULL);
    TF_CHECK(g_u.r == NULL);
    TF_EQ(g_u.pages, 0);
    TF_EQ(g_u.nrows, 0);
    TF_EQ(g_u.stopped, 0);
    TF_EQ(g_u.kind, RPT_LIST);
    /* A run that never got a layout has no records and frees its page
       buffer. */
    TF_EQ(rrun_close(&g_u, RQ_OK), 0);
    TM_HELD(held);
    jobend();

    /* No memory for the page buffer: nothing held. */
    begin(RPT_LIST);
    held = tm_held();
    cmem_fault = fail_cmem;
    TF_EQ(rrun_open(&g_u), PR_ENOMEM);
    cmem_fault = NULL;
    TF_CHECK(g_u.pg == NULL);
    TM_HELD(held);
    jobend();

    /* No memory for the layout: the code of rpt_open, r NULL, and
       rrun_close still ends the run and frees the page buffer. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    TF_EQ(rrun_open(&g_u), PR_OK);
    held = tm_held();
    tm_fail(1);
    TF_EQ(rrun_layout(&g_u, &g_geom, SUB), PR_ENOMEM);
    tm_fail(0);
    TF_CHECK(g_u.r == NULL);
    TM_HELD(held);
    TF_EQ(rrun_close(&g_u, PR_ENOMEM), 0);
    jobend();

    /* A kind that is none of the five. */
    begin(99);
    TF_EQ(rrun_open(&g_u), PR_OK);
    TF_EQ(rrun_layout(&g_u, &g_geom, SUB), RPT_EKIND);
    TF_CHECK(g_u.r == NULL);
    TF_EQ(rrun_close(&g_u, RPT_EKIND), 0);
    jobend();
}

static void test_one(void)
{
    static const u8 kinds[] = { RPT_LIST, RPT_VALUE, RPT_LOANS, RPT_ILABEL,
                                RPT_BLABEL };
    u16 i;

    memset(&g_u, 0, sizeof g_u);
    for (i = 0; i < sizeof kinds; i++) {
        g_u.kind = kinds[i];
        TF_EQ(rrun_one(&g_u), 0);
    }
    strcpy(g_u.item, "7");
    strcpy(g_u.loc, "A1");
    for (i = 0; i < sizeof kinds; i++) {
        g_u.kind = kinds[i];
        TF_EQ(rrun_one(&g_u),
              kinds[i] == RPT_ILABEL || kinds[i] == RPT_BLABEL);
    }
    g_u.item[0] = '\0';
    g_u.kind = RPT_ILABEL;
    TF_EQ(rrun_one(&g_u), 0);
    g_u.kind = RPT_BLABEL;
    TF_EQ(rrun_one(&g_u), 1);
    g_u.below = 1;
    TF_EQ(rrun_one(&g_u), 0);
    g_u.below = 0;
    g_u.loc[0] = '\0';
    TF_EQ(rrun_one(&g_u), 0);
    g_u.kind = RPT_ILABEL;
    strcpy(g_u.item, "7");
    TF_EQ(rrun_one(&g_u), 1);
}

/* The cursors of the first two pages of REPORT ITEMS, asked for apart. */
static char g_cur1[LINK_MAXCURSOR + 1];
static char g_cur2[LINK_MAXCURSOR + 1];

static void cursors(void)
{
    rq q;
    rq_res res;

    rq_begin(&q, "REPORT ITEMS");
    rq_pairu(&q, "PAGE", LINK_PAGEMAX);
    TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK);
    strcpy(g_cur1, res.more);
    TF_CHECK(g_cur1[0] != '\0');
    rq_begin(&q, "REPORT ITEMS");
    rq_pairu(&q, "PAGE", LINK_PAGEMAX);
    rq_pair(&q, "AFTER", g_cur1);
    TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK);
    strcpy(g_cur2, res.more);
    TF_CHECK(g_cur2[0] != '\0');
}

static void test_list_pages(void)
{
    char w[LINK_MAXCURSOR + 8];

    cursors();
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(g_u.stopped, 0);
    TF_EQ(tl_count(), 3);
    TF_CHECK(tf_starts(tl_req(0), "REPORT ITEMS"));
    TF_CHECK(has(tl_req(0), "PAGE=25"));
    TF_CHECK(!haskey(tl_req(0), "AFTER"));
    TF_CHECK(!haskey(tl_req(0), "CAT"));
    TF_CHECK(!haskey(tl_req(0), "LOC"));
    TF_CHECK(!haskey(tl_req(0), "STATUS"));
    TF_CHECK(has(tl_req(0), "ORDER=ID"));
    strcpy(w, "AFTER=");
    strcat(w, g_cur1);
    TF_CHECK(tf_starts(tl_req(1), "REPORT ITEMS"));
    TF_CHECK(has(tl_req(1), "PAGE=25"));
    TF_CHECK(has(tl_req(1), w));
    strcpy(w, "AFTER=");
    strcat(w, g_cur2);
    TF_CHECK(tf_starts(tl_req(2), "REPORT ITEMS"));
    TF_CHECK(has(tl_req(2), w));
    TF_EQ(g_sr.calls, 3);
    TF_EQ(g_sr.more[0], 1);
    TF_EQ(g_sr.more[1], 1);
    TF_EQ(g_sr.more[2], 0);
    TF_EQ(g_sr.pages[0], 1);
    TF_EQ(g_sr.pages[1], 2);
    TF_EQ(g_sr.pages[2], 3);
    TF_EQ(g_sr.count[0], 25);
    TF_EQ(g_sr.count[1], 50);
    TF_EQ(g_sr.count[2], NITEMS);
    TF_EQ(g_u.pages, 3);
    TF_EQ(g_cnt, NITEMS);
    check_complete(S_TOTAL);
    /* ID order: no location groups. */
    TF_CHECK(!outhas("A1 > B2"));
}

static void test_list_filters(void)
{
    /* One page: a page of fewer rows comes without a cursor. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    strcpy(g_u.cat, "CARD");
    strcpy(g_u.loc, "B2");
    strcpy(g_u.status, "A");
    strcpy(g_u.order, "MFR");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_CHECK(tf_starts(tl_req(0), "REPORT ITEMS"));
    TF_CHECK(has(tl_req(0), "CAT=CARD"));
    TF_CHECK(has(tl_req(0), "LOC=B2"));
    TF_CHECK(has(tl_req(0), "STATUS=A"));
    TF_CHECK(has(tl_req(0), "ORDER=MFR"));
    TF_EQ(g_sr.calls, 1);
    TF_EQ(g_sr.more[0], 0);
    TF_EQ(g_cnt, NCARD / 2);
    check_complete(S_TOTAL);

    /* Disposed items only. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    strcpy(g_u.status, "D");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_CHECK(has(tl_req(0), "STATUS=D"));
    TF_CHECK(!haskey(tl_req(0), "CAT"));
    TF_EQ(g_cnt, NDISP);

    /* Two pages. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    strcpy(g_u.loc, "A1");
    run_all();
    TF_EQ(tl_count(), 2);
    TF_CHECK(has(tl_req(0), "LOC=A1"));
    TF_CHECK(has(tl_req(1), "LOC=A1"));
    TF_CHECK(haskey(tl_req(1), "AFTER"));
    TF_EQ(g_sr.more[0], 1);
    TF_EQ(g_sr.more[1], 0);
    TF_EQ(g_cnt, NITEMS / 2);

    /* Nothing matches. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    strcpy(g_u.cat, "NONE");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(g_sr.calls, 1);
    TF_EQ(g_sr.more[0], 0);
    TF_EQ(g_cnt, 0);
}

/* ORDER left "" is not sent (the engine refuses an empty ORDER). */
static void test_list_noorder(void)
{
    begin(RPT_LIST);
    strcpy(g_u.cat, "CARD");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_CHECK(!haskey(tl_req(0), "ORDER"));
    TF_EQ(g_cnt, NCARD);
}

/* The walk: no location first, then LOC LIST and each location in tree
   order, its path asked for once it has rows. */
static void test_list_group(void)
{
    static const char *const want[] = {
        "REPORT ITEMS LOC=0 ALL=1 PAGE=25", "LOC GET 1",
        "REPORT ITEMS LOC=0 ALL=1 PAGE=25 AFTER=", "LOC GET 2",
        "REPORT ITEMS LOC=0 ALL=1 PAGE=25 AFTER="
    };
    u16 i;

    begin(RPT_LIST);
    strcpy(g_u.order, "LOC");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), sizeof want / sizeof want[0]);
    for (i = 0; i < sizeof want / sizeof want[0]; i++)
        if (!TF_CHECK(tf_starts(tl_req(i), want[i])))
            printf("  request %u: %s\n", (unsigned)i, tl_req(i));
    TF_CHECK(!haskey(tl_req(0), "ORDER"));
    /* One stream of three pages, A1's items then B2's: a step after
       each, more on all but the last. */
    TF_EQ(g_sr.calls, 3);
    TF_EQ(g_sr.more[0], 1);
    TF_EQ(g_sr.more[1], 1);
    TF_EQ(g_sr.more[2], 0);
    TF_EQ(g_sr.count[0], 25);
    TF_EQ(g_cnt, NITEMS);
    /* RPT_O_GROUP: a heading, the path, and a count for each location
       with items, a group starting where the location changes. */
    TF_CHECK(outhas("\nA1\r"));
    TF_CHECK(outhas("\nA1 > B2\r"));
    TF_CHECK(!outhas("No location"));
    TF_CHECK(!outhas("\nC3\r"));
    TF_CHECK(outhas("Items: 30"));
    check_complete(S_TOTAL);

    /* A location that has gone since its rows came: headed with the
       name its rows carry, the run going on. */
    begin(RPT_LIST);
    strcpy(g_u.order, "LOC");
    run(4, "-ERR 200 no such location FIELD=LOC");
    TF_EQ(g_rc, RQ_OK);
    finish();
    TF_EQ(g_cnt, NITEMS);
    TF_CHECK(outhas("\nB2\r"));
    TF_CHECK(!outhas("A1 > B2"));
    check_complete(S_TOTAL);
}

/* Below a location: its items, then those of every location inside it,
   in the tree's order whatever the order asked for, grouped. */
static void test_list_below(void)
{
    begin(RPT_LIST);
    strcpy(g_u.order, "MFR");
    strcpy(g_u.loc, "A1");
    g_u.below = 1;
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 5);
    TF_STREQ(tl_req(0), "REPORT ITEMS LOC=A1 ALL=1 PAGE=25");
    TF_STREQ(tl_req(1), "LOC GET 1");
    TF_EQ(g_cnt, NITEMS);
    TF_CHECK(outhas("\nA1\r"));
    TF_CHECK(outhas("\nA1 > B2\r"));
    check_complete(S_TOTAL);

    /* Below no location: the items of no location alone, never the
       whole tree. */
    begin(RPT_LIST);
    strcpy(g_u.loc, "0");
    g_u.below = 1;
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "REPORT ITEMS LOC=0 PAGE=25");
    TF_EQ(g_cnt, 0);

    /* One location, not below: its own items only, one group. */
    begin(RPT_LIST);
    strcpy(g_u.order, "LOC");
    strcpy(g_u.loc, "B2");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_STREQ(tl_req(0), "REPORT ITEMS LOC=B2 PAGE=25");
    TF_STREQ(tl_req(1), "LOC GET 2");
    TF_EQ(g_cnt, NITEMS / 2);
    TF_CHECK(outhas("\nA1 > B2\r"));

    /* A location that does not exist. */
    begin(RPT_LIST);
    strcpy(g_u.loc, "ZZ");
    g_u.below = 1;
    run_all();
    TF_EQ(g_rc, RQ_ENOTFOUND);
    TF_EQ(tl_count(), 1);
    TF_EQ(g_cnt, 0);
}

static void test_value(void)
{
    begin(RPT_VALUE);
    strcpy(g_u.status, "D");       /* ignored: STATUS=A */
    strcpy(g_u.order, "ID");       /* ignored: the tree's order */
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 1 + 5);
    TF_STREQ(tl_req(0), "CAT LIST");
    TF_STREQ(tl_req(1), "REPORT ITEMS LOC=0 ALL=1 STATUS=A PAGE=25");
    TF_STREQ(tl_req(2), "LOC GET 1");
    TF_CHECK(haskey(tl_req(3), "AFTER"));
    TF_CHECK(has(tl_req(3), "STATUS=A"));
    TF_CHECK(!haskey(tl_req(3), "ORDER"));
    TF_EQ(g_sr.calls, 3);
    TF_EQ(g_sr.more[0], 1);
    TF_EQ(g_sr.more[2], 0);
    TF_EQ(g_cnt, NITEMS - NDISP);
    check_complete("Items: 58");
    /* The category rows reached the layout: its table, with the names
       of the categories. */
    TF_CHECK(outhas("Totals by category"));
    TF_CHECK(outhas("Systems"));
    TF_CHECK(outhas("Expansion cards"));
    TF_CHECK(outhas("\nA1 > B2\r"));

    /* One location, not below: its own items only. */
    begin(RPT_VALUE);
    strcpy(g_u.cat, "CARD");
    strcpy(g_u.loc, "A1");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 3);
    TF_STREQ(tl_req(1), "REPORT ITEMS CAT=CARD LOC=A1 STATUS=A PAGE=25");
    TF_STREQ(tl_req(2), "LOC GET 1");
    TF_EQ(g_cnt, NCARD / 2);
}

static void test_loans(void)
{
    begin(RPT_LOANS);
    g_u.overdue = 1;
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_CHECK(tf_starts(tl_req(0), "REPORT LOANS"));
    TF_CHECK(has(tl_req(0), "OVERDUE=1"));
    TF_CHECK(has(tl_req(0), "ASOF=" ASOF));
    TF_CHECK(has(tl_req(0), "PAGE=25"));
    TF_EQ(g_sr.calls, 1);
    TF_EQ(g_sr.more[0], 0);
    TF_EQ(g_cnt, 1);
    TF_CHECK(outhas("Overdue loans"));
    TF_CHECK(outhas("Smith"));
    TF_CHECK(!outhas("Jones"));
    TF_CHECK(!outhas(S_STOPPED));

    begin(RPT_LOANS);
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_CHECK(!haskey(tl_req(0), "OVERDUE"));
    TF_CHECK(has(tl_req(0), "ASOF=" ASOF));
    TF_EQ(g_cnt, 2);
    TF_CHECK(outhas("Open loans"));
    TF_CHECK(outhas("Smith"));
    TF_CHECK(outhas("Jones"));
}

static void test_ilabel(void)
{
    /* Many: the tree's order, without a heading's LOC GET. */
    begin(RPT_ILABEL);
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 3);
    TF_STREQ(tl_req(0), "REPORT ITEMS LOC=0 ALL=1 PAGE=25");
    TF_CHECK(tf_starts(tl_req(2), "REPORT ITEMS LOC=0 ALL=1 PAGE=25 AFTER="));
    TF_EQ(g_sr.calls, 3);
    TF_EQ(g_sr.more[2], 0);
    TF_EQ(g_cnt, NITEMS);
    TF_CHECK(outhas("Loc B2"));

    begin(RPT_ILABEL);
    strcpy(g_u.cat, "CARD");
    strcpy(g_u.loc, "A1");
    strcpy(g_u.status, "D");        /* not a filter of labels */
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "REPORT ITEMS CAT=CARD LOC=A1 PAGE=25");
    TF_EQ(g_cnt, NCARD / 2);

    /* One: ITEM GET, then its location's name, one record, one
       page. */
    begin(RPT_ILABEL);
    strcpy(g_u.item, "3");
    strcpy(g_u.cat, "SYS");         /* the one record's request has none */
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_STREQ(tl_req(0), "ITEM GET 3");
    TF_STREQ(tl_req(1), "LOC GET 1");
    TF_CHECK(outhas("Loc A1"));
    TF_EQ(g_sr.calls, 1);
    TF_EQ(g_sr.more[0], 0);
    TF_EQ(g_sr.pages[0], 1);
    TF_EQ(g_sr.count[0], 1);
    TF_EQ(g_u.pages, 1);
    TF_EQ(g_cnt, 1);
    TF_CHECK(outhas("M3"));
    TF_CHECK(!outhas("M4"));

    /* Its location gone since ITEM GET: the label without it. */
    begin(RPT_ILABEL);
    strcpy(g_u.item, "3");
    run(2, "-ERR 200 no such location");
    TF_EQ(g_rc, RQ_OK);
    finish();
    TF_EQ(g_cnt, 1);
    TF_CHECK(!outhas("Loc A1"));

    /* The item does not exist. */
    begin(RPT_ILABEL);
    strcpy(g_u.item, "9999");
    run_all();
    TF_EQ(g_rc, RQ_ENOTFOUND);
    TF_EQ(g_res.code, RQ_ENOTFOUND);
    TF_STREQ(tl_req(0), "ITEM GET 9999");
    TF_EQ(tl_count(), 1);
    TF_EQ(g_sr.calls, 0);
    TF_EQ(g_u.pages, 0);
    TF_EQ(g_u.stopped, 0);
    TF_EQ(g_cnt, 0);
}

static void test_blabel(void)
{
    /* All: the type names, then the tree, each label's path made of the
       rows above it: no LOC GET. */
    begin(RPT_BLABEL);
    strcpy(g_u.cat, "CARD");        /* not a filter of bin labels */
    strcpy(g_u.item, "1");          /* nor an item */
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_STREQ(tl_req(0), "LTYPE LIST");
    TF_STREQ(tl_req(1), "LOC LIST PAGE=25");
    TF_EQ(g_sr.calls, 1);
    TF_EQ(g_sr.more[0], 0);
    TF_EQ(g_cnt, 3);
    /* Name, code, path and the type's name. */
    TF_CHECK(outhas("Code B2\r\nA1 > B2\r\nBox\r"));
    TF_CHECK(outhas("Code C3\r\nC3\r\nRoom\r"));

    /* "0", no location, is every location too. */
    begin(RPT_BLABEL);
    strcpy(g_u.loc, "0");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_STREQ(tl_req(1), "LOC LIST PAGE=25");
    TF_EQ(g_cnt, 3);

    /* One location. */
    begin(RPT_BLABEL);
    strcpy(g_u.loc, "C3");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 2);
    TF_STREQ(tl_req(1), "LOC GET C3");
    TF_EQ(g_sr.calls, 1);
    TF_EQ(g_sr.more[0], 0);
    TF_EQ(g_u.pages, 1);
    TF_EQ(g_cnt, 1);
    TF_CHECK(outhas("Room"));
    TF_CHECK(!outhas("A1"));

    /* A location and those below it, their paths after its own. */
    begin(RPT_BLABEL);
    strcpy(g_u.loc, "A1");
    g_u.below = 1;
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 3);
    TF_STREQ(tl_req(1), "LOC GET A1");
    TF_STREQ(tl_req(2), "LOC LIST UNDER=A1 PAGE=25");
    TF_EQ(g_sr.calls, 2);
    TF_EQ(g_sr.more[0], 1);
    TF_EQ(g_cnt, 2);
    TF_CHECK(outhas("Code B2\r\nA1 > B2\r"));
    TF_CHECK(!outhas("C3"));

    begin(RPT_BLABEL);
    strcpy(g_u.loc, "ZZ");
    run_all();
    TF_EQ(g_rc, RQ_ENOTFOUND);
    TF_EQ(g_res.code, RQ_ENOTFOUND);
    TF_STREQ(tl_req(1), "LOC GET ZZ");
    TF_EQ(g_sr.calls, 0);
    TF_EQ(g_cnt, 0);
}

/* More locations than a page, nested, and an item of no location: the
   bin labels follow LOC LIST's cursor with each path built across the
   pages, and the items of no location come first in the tree's order.
   Run last, as it adds to the data. */
static void test_many(void)
{
    char line[80];
    u16 i;

    TF_EQ(tc_send("LOC ADD LOC_NAME=Deep LTYPE=BOX LOC_CODE=D1 "
                  "PARENT_ID=C3"), RQ_OK);
    for (i = 1; i <= 30; i++) {
        sprintf(line, "LOC ADD LOC_NAME=Bin%02u LTYPE=BIN PARENT_ID=D1",
                (unsigned)i);
        TF_EQ(tc_send(line), RQ_OK);
    }
    TF_EQ(tc_send("ITEM ADD CATEGORY=SYS MFR=LOOSE MODEL=L1"), RQ_OK);
    begin(RPT_BLABEL);
    strcpy(g_u.loc, "C3");
    g_u.below = 1;
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 4);
    TF_STREQ(tl_req(2), "LOC LIST UNDER=C3 PAGE=25");
    TF_CHECK(tf_starts(tl_req(3), "LOC LIST UNDER=C3 PAGE=25 AFTER="));
    TF_EQ(g_cnt, 32);
    TF_CHECK(outhas("Bin01\r\nC3 > Deep > Bin01\r\nBin\r"));
    TF_CHECK(outhas("Bin30\r\nC3 > Deep > Bin30\r\nBin\r"));

    begin(RPT_LIST);
    strcpy(g_u.order, "LOC");
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(g_cnt, NITEMS + 1);
    TF_CHECK(outhas("No location"));
    TF_CHECK(strstr(g_buf, "No location") < strstr(g_buf, "\nA1\r"));

    /* Its item label: no location line. */
    begin(RPT_ILABEL);
    sprintf(g_u.item, "%u", (unsigned)(NITEMS + 1));
    run_all();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(g_cnt, 1);
    TF_CHECK(outhas("LOOSE L1"));
    TF_CHECK(!outhas("Loc "));
}

static void test_stop(void)
{
    /* After the second page of three. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    g_sr.stop_at = 2;
    run(0, NULL);
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(g_u.stopped, 1);
    TF_EQ(g_sr.calls, 2);
    TF_EQ(tl_count(), 2);
    TF_EQ(g_u.pages, 2);
    finish();
    TF_EQ(g_cnt, 50);
    check_cut();

    /* After the last page, which was a complete run all the same. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    strcpy(g_u.cat, "CARD");
    g_sr.stop_at = 1;
    run(0, NULL);
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(g_u.stopped, 1);
    TF_EQ(g_sr.more[0], 0);
    finish();
    TF_EQ(g_cnt, NCARD);
    check_cut();

    /* After the second page of a value report. */
    begin(RPT_VALUE);
    g_sr.stop_at = 2;
    run(0, NULL);
    TF_EQ(g_u.stopped, 1);
    TF_EQ(tl_count(), 5);
    finish();
    TF_EQ(g_cnt, 50);
    TF_CHECK(outhas(S_STOPPED));
    TF_CHECK(!outhas("Totals by category"));

    /* A GET run. */
    begin(RPT_ILABEL);
    strcpy(g_u.item, "5");
    g_sr.stop_at = 1;
    run(0, NULL);
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(g_u.stopped, 1);
    finish();
    TF_EQ(g_cnt, 1);

    /* Not stopped: stopped stays 0. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    strcpy(g_u.cat, "CARD");
    run_all();
    TF_EQ(g_u.stopped, 0);
}

static void test_close(void)
{
    /* rc other than RQ_OK ends the run cut short whatever its value. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    strcpy(g_u.cat, "CARD");
    run(0, NULL);
    TF_EQ(g_rc, RQ_OK);
    g_rc = RQ_EIO;
    finish();
    TF_EQ(g_cnt, NCARD);
    check_cut();

    /* rc RQ_OK on a run that was not stopped ends it with totals. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    strcpy(g_u.cat, "CARD");
    run(0, NULL);
    finish();
    TF_EQ(g_cnt, NCARD);
    TF_CHECK(outhas(S_TOTAL));
}

static void test_failures(void)
{
    /* The first request. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    run(1, "-ERR 203 no way");
    TF_EQ(g_rc, RQ_EPERM);
    TF_EQ(g_res.code, RQ_EPERM);
    TF_STREQ(g_res.text, "no way");
    TF_EQ(tl_count(), 1);
    TF_EQ(g_sr.calls, 0);
    TF_EQ(g_u.pages, 0);
    TF_EQ(g_u.stopped, 0);
    finish();
    TF_EQ(g_cnt, 0);
    TF_CHECK(!outhas(S_TOTAL));

    /* A later one: the pages before it are laid out and the run is cut
       short, without totals. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    run(2, "-ERR 300 disk");
    TF_EQ(g_rc, RQ_EIO);
    TF_EQ(g_res.code, RQ_EIO);
    TF_STREQ(g_res.text, "disk");
    TF_EQ(tl_count(), 2);
    TF_EQ(g_u.pages, 1);
    TF_EQ(g_u.stopped, 0);
    finish();
    TF_EQ(g_cnt, 25);
    check_cut();

    /* CAT LIST of a value report: nothing else is asked. */
    begin(RPT_VALUE);
    run(1, "-ERR 500 bad");
    TF_EQ(g_rc, RQ_EINTERNAL);
    TF_EQ(tl_count(), 1);
    TF_EQ(g_u.pages, 0);
    finish();
    TF_EQ(g_cnt, 0);

    /* The REPORT ITEMS after it. */
    begin(RPT_VALUE);
    run(2, "-ERR 102 FIELD=CAT");
    TF_EQ(g_rc, RQ_EARG);
    TF_STREQ(g_res.field, "CAT");
    TF_EQ(tl_count(), 2);
    finish();
    TF_CHECK(!outhas("Totals by category"));

    /* A loans run and a GET run. */
    begin(RPT_LOANS);
    run(1, "-ERR 102 FIELD=ASOF");
    TF_EQ(g_rc, RQ_EARG);
    TF_STREQ(g_res.field, "ASOF");
    finish();
    TF_EQ(g_cnt, 0);

    begin(RPT_BLABEL);
    strcpy(g_u.loc, "A1");
    run(1, "-ERR 203 no");
    TF_EQ(g_rc, RQ_EPERM);
    TF_EQ(g_u.pages, 0);
    finish();
    TF_EQ(g_cnt, 0);
}

static void test_job_failure(void)
{
    /* The file cannot be written: the page heading laid out with the
       first row overflows the job's buffer (PR_BUFLEN), so the job
       fails on that row and no other row of the page is laid out.  The
       run ends after that page, without an error of its own, and is
       aborted. */
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    TF_EQ(rrun_open(&g_u), PR_OK);
    TF_EQ(rrun_layout(&g_u, &g_geom, SUB), PR_OK);
    tl_clear();
    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 0, 0);
    g_rc = rrun_go(&g_u, tc_lk, step, &g_sr, &g_res);
    tfs_fault_off();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(g_u.stopped, 0);
    TF_CHECK(pr_failed(g_p) != PR_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(g_u.pages, 1);
    TF_EQ(g_sr.calls, 1);
    TF_EQ(g_sr.more[0], 0);
    g_cnt = rrun_close(&g_u, g_rc);
    TF_EQ(g_cnt, 1);
    jobend();
}

/* The wait function of a printer job: counts its calls, and those made
   while a response is being read. */
static int g_waits;
static int g_waits_reading;

static int wait_fn(void *arg, int status)
{
    TF_CHECK(arg == (void *)&g_waits);
    TF_CHECK(status != PRN_OK);
    g_waits++;
    if (tl_reading())
        g_waits_reading++;
    return 1;
}

/* A printer that fails a write is asked about only once the response
   is read.  The printer is a capture file whose first write fails. */
static void test_wait(void)
{
    int cferr;

    begin(RPT_LIST);
    jobend();
    strcpy(g_out, tfs_path("RRUN", ".TXT"));
    TF_EQ(prn_capture(g_out), 0);
    TF_EQ(pr_open(&g_p, PR_PRINTER, PR_PLAIN, PRN_LPT1, NULL, wait_fn,
                  &g_waits, &cferr), PR_OK);
    g_u.p = g_p;
    strcpy(g_u.order, "ID");
    g_waits = 0;
    g_waits_reading = 0;
    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 1, 0);
    run(0, NULL);
    tfs_fault_off();
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(tl_count(), 3);
    TF_CHECK(g_waits >= 1);
    TF_EQ(g_waits_reading, 0);
    TF_EQ(rrun_close(&g_u, g_rc), NITEMS);
    jobend();
    TF_EQ(prn_capture(NULL), 0);
}

/* A row wider than RPT_ROWMAX is cut where its bytes run out: the
   values after the cut are missing, the ones before it laid out. */
static char g_wide[LINK_MAXRESP + 1];

static void test_wide(void)
{
    static const char head[] = "+DATA\n"
        "F ITEM_ID CATEGORY MFR MODEL SERIALNO COND QTY LOC_NAME\n"
        "R 7 SYS ";
    static const char tail[] = " MODELX SERIALX GD 1 A1\n.";
    u16 n;

    strcpy(g_wide, head);
    n = (u16)strlen(g_wide);
    memset(g_wide + n, 'W', RPT_ROWMAX + 60);
    g_wide[n + RPT_ROWMAX + 60] = '\0';
    strcat(g_wide, tail);
    TF_CHECK(strlen(g_wide) <= LINK_MAXRESP);
    begin(RPT_LIST);
    strcpy(g_u.order, "ID");
    run(1, g_wide);
    TF_EQ(g_rc, RQ_OK);
    TF_EQ(g_sr.calls, 1);
    finish();
    TF_EQ(g_cnt, 1);
    check_complete(S_TOTAL);
    TF_CHECK(outhas("WWWW"));
    TF_CHECK(!outhas("MODELX"));
    TF_CHECK(!outhas("SERIALX"));
}

/* ---------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    link_clock clk;
    char why[LINK_WHYSZ];
    char mdir[PATHMAX];
    static const char *const extra[] = { "RRUN.TXT", NULL };
    int rc;

    tf_begin("T_RRUN");
    cmem_init();
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_RRUN dir [msgdir]\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    if (argc > 2 && strlen(argv[2]) < PATHMAX)
        strcpy(mdir, argv[2]);
    else
        tf_progdir(mdir, sizeof mdir, argv[0]);
    te_remove(tfs_dir(), extra);
    tc_clock(&clk);
    rc = link_open_local(&tc_lk, tfs_dir(), &clk, why, sizeof why);
    if (!TF_EQ(rc, 0)) {
        printf("T_RRUN: %s\n", why);
        return tf_done();
    }
    if (!TF_EQ(msg_load(mdir, MSG_FILE), 0)) {
        printf("T_RRUN: %s\n", msg_why());
        return tf_done();
    }
    if (!tc_admin())
        return tf_done();
    add_data();
    test_keys();
    test_lifecycle();
    test_one();
    test_list_pages();
    test_list_filters();
    test_list_noorder();
    test_list_group();
    test_list_below();
    test_value();
    test_loans();
    test_ilabel();
    test_blabel();
    test_stop();
    test_close();
    test_failures();
    test_job_failure();
    test_wait();
    test_wide();
    test_many();
    TF_EQ(link_close(tc_lk), 0);
    msg_free();
    te_remove(tfs_dir(), extra);
    return tf_done();
}
