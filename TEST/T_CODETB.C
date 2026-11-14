/*
 * T_CODETB.C - the tags of a code list (SRC/CLIENT/CODETAB.H):
 * ct_load_tags over the engine and ct_tags_add.
 *
 * usage: T_CODETB dir
 *
 * dir is a scratch data directory prefix holding nothing but this
 * test's files; its engine files are removed first and last.  Written
 * from the contract in CODETAB.H alone.
 *
 * Covered:
 * - ct_tags_add: an empty value, an append after a comma with the
 *   blanks and commas the value ends in dropped, a tag already there
 *   (whole, A-Z folded, between blanks or commas), a part of a tag that
 *   is not one, the exact fit and one over; ct_tags_bare;
 * - ct_load_tags over the engine: no tags, then the tags of several
 *   items, each once in its first spelling and in A-Z folded order;
 *   pages ended by the engine's full tag set, merged, with the wait
 *   function called between them; more than LINK_MAXCODES tags kept as
 *   the first LINK_MAXCODES A-Z with part set; a later page's tag that
 *   sorts first pushing the last out; a stop by the wait function; a
 *   failed first or later request leaving no rows and part clear.
 *
 * Exit status 0 when every check passed, else 1.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "CODETAB.H"
#include "TRLINK.H"
#include "TEWIPE.H"
#define TF_LINK
#include "TESTFW.H"

#define PATHMAX         96
#define NLONG           40      /* items of five distinct 11-character
                                   tags each, which fill several pages  */

static char g_ddir[PATHMAX];
static ctab g_t;
static u16 g_waits;             /* calls of the wait function           */
static u16 g_stopat;            /* the call that stops the load, 0 none */

/* ct_wait_fn: counts its calls; stops at call g_stopat.  arg is the
   ctab, which holds the tags so far. */
static int wait_fn(void *arg)
{
    TF_CHECK(arg == (void *)&g_t);
    g_waits++;
    return g_waits == g_stopat;
}

/* ct_load_tags into g_t, the wait function stopping at call stop:
   its code. */
static int load(u16 stop)
{
    rq_res res;

    g_waits = 0;
    g_stopat = stop;
    return ct_load_tags(&g_t, tc_lk, wait_fn, &g_t, &res);
}

/* The code of row i of g_t. */
static const char *code(u16 i)
{
    static ct_row r;

    ct_get(&g_t, i, &r);
    return r.code;
}

/* 1 when the rows of g_t are in strictly rising A-Z folded order. */
static int rising(void)
{
    char a[LINK_ITAGLEN + 1];
    char b[LINK_ITAGLEN + 1];
    u16 i;

    for (i = 1; i < g_t.n; i++) {
        txt_ucopy(a, code((u16)(i - 1)), (u16)sizeof a);
        txt_ucopy(b, code(i), (u16)sizeof b);
        if (strcmp(a, b) >= 0)
            return 0;
    }
    return 1;
}

static void test_add(void)
{
    char t[LINK_DESCRLEN + 1];

    t[0] = '\0';
    TF_EQ(ct_tags_add(t, 25, "retro"), 1);
    TF_STREQ(t, "retro");
    TF_EQ(ct_tags_add(t, 25, "ISA"), 1);
    TF_STREQ(t, "retro,ISA");
    /* There already, whole and folded: unchanged. */
    TF_EQ(ct_tags_add(t, 25, "RETRO"), 1);
    TF_EQ(ct_tags_add(t, 25, "isa"), 1);
    TF_STREQ(t, "retro,ISA");
    /* A part of a tag is not the tag; the separators it ends in go. */
    strcpy(t, " retro2  metro , ,");
    TF_EQ(ct_tags_add(t, 25, "retro"), 1);
    TF_STREQ(t, " retro2  metro,retro");
    strcpy(t, "a b,c");
    TF_EQ(ct_tags_add(t, 25, "B"), 1);
    TF_STREQ(t, "a b,c");
    strcpy(t, " ,");
    TF_EQ(ct_tags_add(t, 25, "x"), 1);
    TF_STREQ(t, "x");
    /* The exact fit, and one over: unchanged. */
    strcpy(t, "abcdefghijkl");
    TF_EQ(ct_tags_add(t, 25, "mnopqrstuvwx"), 1);
    TF_STREQ(t, "abcdefghijkl,mnopqrstuvwx");
    strcpy(t, "abcdefghijkl");
    TF_EQ(ct_tags_add(t, 24, "mnopqrstuvwx"), 0);
    TF_STREQ(t, "abcdefghijkl");
    strcpy(t, " retro  ISA,pc ");
    ct_tags_bare(t);
    TF_STREQ(t, ",retro,,ISA,pc");
    strcpy(t, " ,");
    ct_tags_bare(t);
    TF_STREQ(t, "");
}

/* ITEM ADD of an item with the tags given. */
static void add_item(const char *tags)
{
    rq q;
    rq_res res;

    rq_begin(&q, "ITEM ADD CATEGORY=PART");
    rq_pair(&q, "TAGS", tags);
    TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK);
}

static void test_few(void)
{
    rq_res res;

    TF_EQ(load(0), RQ_OK);
    TF_EQ(g_t.n, 0);
    TF_EQ(g_t.part, 0);
    TF_EQ(g_waits, 0);
    add_item("Retro,ISA");
    add_item("retro pci");
    add_item("net,isa");
    tl_clear();
    TF_EQ(load(0), RQ_OK);
    TF_STREQ(tl_req(0), "ITEM TAGS");
    TF_EQ(tl_count(), 1);
    TF_EQ(g_t.n, 4);
    TF_EQ(g_t.part, 0);
    TF_STREQ(code(0), "ISA");
    TF_STREQ(code(1), "net");
    TF_STREQ(code(2), "pci");
    TF_STREQ(code(3), "Retro");
    /* A later page whose tag sorts first and one held already; the
       answer stands for the first page, the engine gives the rest. */
    tl_clear();
    tl_fail(1, "+DATA\nF TAG\nR aaa\nR RETRO\nC ITEMS::1\n.");
    TF_EQ(load(0), RQ_OK);
    TF_EQ(g_waits, 1);
    TF_STREQ(tl_req(1), "ITEM TAGS AFTER=ITEMS::1");
    TF_EQ(g_t.n, 5);
    TF_STREQ(code(0), "aaa");
    TF_STREQ(code(1), "isa");
    TF_STREQ(code(4), "RETRO");
    /* A failed request: no rows, and part cleared. */
    tl_clear();
    tl_fail(1, "-ERR 204 bad cursor");
    g_t.part = 1;
    TF_EQ(ct_load_tags(&g_t, tc_lk, wait_fn, &g_t, &res), RQ_ECURSOR);
    TF_EQ(res.code, RQ_ECURSOR);
    TF_EQ(g_t.n, 0);
    TF_EQ(g_t.part, 0);
}

static void test_pages(void)
{
    char tags[LINK_DESCRLEN + 1];
    u16 i;
    u16 n;
    u16 pages;

    /* Five distinct tags of 11 characters to an item, numbered down, so
       that each later page's sort before the last tags held. */
    for (i = 0; i < NLONG; i++) {
        n = (u16)(NLONG - 1 - i);
        sprintf(tags, "tag%02u-a-xyz,tag%02u-b-xyz,tag%02u-c-xyz,"
                "tag%02u-d-xyz,tag%02u-e-xyz", (unsigned)n, (unsigned)n,
                (unsigned)n, (unsigned)n, (unsigned)n);
        add_item(tags);
    }
    tl_clear();
    TF_EQ(load(0), RQ_OK);
    pages = tl_count();
    TF_CHECK(pages >= 3);
    TF_EQ(g_waits, pages - 1);
    TF_EQ(g_t.n, LINK_MAXCODES);
    TF_EQ(g_t.part, 1);
    TF_CHECK(rising());
    /* The first LINK_MAXCODES A-Z: the four short tags, then those of
       the last items, read on the last pages. */
    TF_STREQ(code(0), "ISA");
    TF_STREQ(code(3), "Retro");
    TF_STREQ(code(4), "tag00-a-xyz");
    TF_STREQ(code(LINK_MAXCODES - 1), "tag09-a-xyz");
    /* Stopped after the first page: the tags of the items it read. */
    tl_clear();
    TF_EQ(load(1), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_EQ(g_waits, 1);
    TF_EQ(g_t.part, 1);
    TF_CHECK(rising());
    TF_STREQ(code(0), "ISA");
    TF_CHECK(strcmp(code(4), "tag00-a-xyz") != 0);
    /* A later page failing: the rows and part the first page left (it
       held more than LINK_MAXCODES tags) are dropped. */
    tl_clear();
    tl_fail(2, "-ERR 500 oops");
    TF_EQ(load(0), RQ_EINTERNAL);
    TF_EQ(tl_count(), 2);
    TF_EQ(g_t.n, 0);
    TF_EQ(g_t.part, 0);
}

int main(int argc, char **argv)
{
    link_clock clk;
    char why[LINK_WHYSZ];
    int rc;

    tf_begin("T_CODETB");
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_CODETB dir\n");
        return 2;
    }
    cmem_init();
    test_add();
    strcpy(g_ddir, argv[1]);
    te_remove(g_ddir, NULL);
    tc_clock(&clk);
    rc = link_open_local(&tc_lk, g_ddir, &clk, why, sizeof why);
    if (!TF_EQ(rc, 0)) {
        printf("T_CODETB: %s\n", why);
        return tf_done();
    }
    if (tc_admin() && TF_CHECK(ct_open(&g_t))) {
        test_few();
        test_pages();
        ct_close(&g_t);
    }
    TF_EQ(link_close(tc_lk), 0);
    te_remove(g_ddir, NULL);
    return tf_done();
}
