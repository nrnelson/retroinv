/*
 * T_ENGIN2.C - the second part of the T_ENGINE test (see T_ENGINE.C):
 * the lists, categories and bounded phases, with the paging helper they
 * share.  It is a file of its own only to stay within the compiler
 * memory of Watcom C 6.5; T_ENGINE.H holds what the parts share.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGSCH.H"
#include "ENGCODE.H"
#include "ENGLOCS.H"
#include "ENGITEM.H"
#define TF_PART
#define TF_FILE "T_ENGIN2"
/* Checks keep no file name or expression text: Turbo C stores a
   separate copy of every string literal, which would cost this test
   kilobytes of near data. */
#ifndef TF_TERSE
#define TF_TERSE
#endif
#include "TESTFW.H"
#include "TENGH.H"
#include "T_ENGINE.H"

#define OUTMAX          1000    /* the joined entries of one listing    */
#define PAGES_MAX       400     /* pages a listing may take, at most    */
/* The non-matching items of the bounded phase. */
#define NFILLER         (ENG_MAXREADS + 1)

/* ---------------------------------------------------------------------
 * Paging: every page of a scan, as the protocol layer pages it.
 * --------------------------------------------------------------------- */
typedef int (*opener)(eng_scan *s, const char *after);

static int g_order;
static const char *g_cat;
static const char *g_loc;
static const char *g_stat;
static const char *g_text;
static int g_flags;
static char g_out[OUTMAX];
static char g_first_cursor[ENG_CURSORMAX];
static u16 g_npages;
static u16 g_first_count;

/* eng_item_scan with the filters text, cat, stat and flags
   (eng_item_filters). */
static int scan(eng_scan *s, int order, const char *text, const char *cat,
                const char *loc, const char *stat, int flags,
                const char *after)
{
    eng_ifilt f;
    int rc;

    rc = eng_item_filters(g_e, &f, text, cat, stat, NULL, NULL, flags);
    if (rc == ENG_OK)
        rc = eng_item_scan(g_e, s, order, &f, loc, after);
    return rc;
}

static int op_list(eng_scan *s, const char *a)
{
    return scan(s, g_order, NULL, g_cat, g_loc, g_stat, g_flags, a);
}

static int op_find(eng_scan *s, const char *a)
{
    return scan(s, ENG_ORD_ID, g_text, g_cat, g_loc, g_stat, g_flags, a);
}

static int op_serial(eng_scan *s, const char *a)
{
    return eng_item_serial(g_e, s, g_text, a);
}

/* Run the scan page by page, pg entries a page, and join field name of
   table t of every record returned with ','.  Sets g_npages,
   g_first_count and g_first_cursor; "ERR n" when a call fails. */
static const char *pages(opener op, u16 pg, int t, const char *name)
{
    eng_scan s;
    char cur[ENG_CURSORMAX];
    u32 r;
    u16 got;
    int more;
    int rc;

    g_out[0] = '\0';
    cur[0] = '\0';
    g_first_cursor[0] = '\0';
    g_npages = 0;
    g_first_count = 0;
    do {
        rc = op(&s, cur);
        if (rc != ENG_OK) {
            sprintf(g_out, "ERR %d", rc);
            eng_clear_error(g_e);
            return g_out;
        }
        g_npages++;
        got = 0;
        while (got < pg) {
            rc = eng_scan_next(g_e, &s, &r);
            if (rc != ENG_OK) {
                sprintf(g_out, "ERR %d", rc);
                return g_out;
            }
            if (r == 0)
                break;
            if (g_out[0] != '\0')
                strcat(g_out, ",");
            if (strlen(g_out) + 40 < OUTMAX)
                strcat(g_out, te_rtext(t, TE_BUF, name));
            got++;
        }
        TF_CHECK(s.reads <= ENG_MAXREADS);
        if (g_npages == 1)
            g_first_count = got;
        cur[0] = '\0';
        if (!TF_EQ(eng_scan_more(g_e, &s, &more), ENG_OK)) {
            sprintf(g_out, "ERR more");
            return g_out;
        }
        if (more)
            eng_scan_cursor(&s, cur);
        if (g_npages == 1)
            strcpy(g_first_cursor, cur);
    } while (cur[0] != '\0' && g_npages < PAGES_MAX);
    TF_CHECK(g_npages < PAGES_MAX);
    return g_out;
}

/* Every ITEM_ID the rest of scan s returns, joined with ','; "ERR n"
   when a read fails. */
static const char *collect(eng_scan *s)
{
    u32 r;
    int rc;

    g_out[0] = '\0';
    for (;;) {
        rc = eng_scan_next(g_e, s, &r);
        if (rc != ENG_OK) {
            sprintf(g_out, "ERR %d", rc);
            eng_clear_error(g_e);
            break;
        }
        if (r == 0 || strlen(g_out) + ENG_IDLEN + 2 > OUTMAX)
            break;
        if (g_out[0] != '\0')
            strcat(g_out, ",");
        strcat(g_out, te_rtext(ENG_T_ITEMS, TE_BUF, "ITEM_ID"));
    }
    return g_out;
}

static const char *list(int order, const char *cat, const char *loc,
                        const char *stat, u16 pg)
{
    g_order = order;
    g_cat = cat;
    g_loc = loc;
    g_stat = stat;
    return pages(op_list, pg, ENG_T_ITEMS, "ITEM_ID");
}

static const char *find(const char *text, u16 pg)
{
    g_text = text;
    g_cat = NULL;
    g_loc = NULL;
    g_stat = NULL;
    return pages(op_find, pg, ENG_T_ITEMS, "ITEM_ID");
}

/* ---------------------------------------------------------------------
 * Lists, FIND and cursors.
 * --------------------------------------------------------------------- */
/* The test items, every ITEM LIST order and the filters. */
static void lists_orders(void)
{
    eng_scan s;

    te_loc("A1", 0);
    te_loc("A2", 0);
    te_loc("B1", 0);
    add_item("CATEGORY=CARD|MFR=IBM|MODEL=CGA Card|SERIALNO=S100|LOC_ID=A1");
    add_item("CATEGORY=SYS|MFR=Compaq|MODEL=Portable|SERIALNO=s200|"
        "LOC_ID=B1");
    add_item("CATEGORY=CARD|MFR=ibm|MODEL=EGA Card|SERIALNO=s100|LOC_ID=1");
    add_item("CATEGORY=DRV|MFR=Seagate|MODEL=ST225");
    add_item("CATEGORY=DRV|MFR=Seagate|MODEL=ST225|SERIALNO=X9|LOC_ID=A2");
    add_item("CATEGORY=MAN|MFR=Microsoft|MODEL=DOS 3.1 manual|"
        "DESCR=Blue binder|TAGS=docs,dos|LOC_ID=B1|STATUS=D");
    add_item("CATEGORY=CBL|ITEMTYPE=Serial cable|LOC_ID=A2");

    /* Every order, two entries a page. */
    TF_STREQ(list(ENG_ORD_ID, NULL, NULL, NULL, 2), "1,2,3,4,5,6,7");
    TF_EQ(g_npages, 4);
    TF_STREQ(g_first_cursor, "ITEMS1:2020202020202032:2");
    TF_STREQ(list(ENG_ORD_LOC, NULL, NULL, NULL, 2), "4,1,3,5,7,2,6");
    TF_STREQ(list(ENG_ORD_MFR, NULL, NULL, NULL, 2), "7,2,1,3,6,4,5");
    TF_STREQ(list(ENG_ORD_MFR, NULL, NULL, NULL, 1), "7,2,1,3,6,4,5");
    TF_STREQ(list(ENG_ORD_SERIAL, NULL, NULL, NULL, 2), "4,6,7,1,3,2,5");
    TF_STREQ(list(ENG_ORD_SERIAL, NULL, NULL, NULL, 1), "4,6,7,1,3,2,5");
    TF_STREQ(list(ENG_ORD_ID, NULL, NULL, NULL, 25), "1,2,3,4,5,6,7");
    TF_EQ(g_npages, 1);

    /* Filters, in any order; unknown values match nothing, but an
       unknown location is 200. */
    TF_STREQ(list(ENG_ORD_ID, "drv", NULL, NULL, 2), "4,5");
    TF_STREQ(list(ENG_ORD_LOC, NULL, "a1", NULL, 2), "1,3");
    TF_STREQ(g_first_cursor, "");
    TF_STREQ(list(ENG_ORD_LOC, NULL, "A2", NULL, 1), "5,7");
    TF_EQ(g_npages, 2);
    TF_STREQ(list(ENG_ORD_ID, NULL, "A2", NULL, 2), "5,7");
    TF_STREQ(list(ENG_ORD_MFR, "CARD", "A1", "A", 2), "1,3");
    TF_STREQ(list(ENG_ORD_ID, NULL, NULL, "d", 2), "6");
    TF_STREQ(list(ENG_ORD_SERIAL, "SYS", "B1", "A", 2), "2");
    TF_STREQ(list(ENG_ORD_ID, "ZZZ", NULL, NULL, 2), "");
    TF_STREQ(list(ENG_ORD_LOC, NULL, "0", NULL, 2), "4");
    TF_EQ(g_npages, 1);
    TF_STREQ(list(ENG_ORD_SERIAL, NULL, "0", NULL, 2), "4");
    TF_STREQ(list(ENG_ORD_LOC, NULL, "3", NULL, 1), "2,6");
    TF_STREQ(list(ENG_ORD_ID, "", "", "", 7), "1,2,3,4,5,6,7");
    TF_STREQ(list(ENG_ORD_ID, "CARDS", NULL, NULL, 2), "ERR 102");
    TF_STREQ(list(ENG_ORD_LOC, NULL, "Q9", NULL, 2), "ERR 200");
    TF_STREQ(list(ENG_ORD_ID, NULL, "4", NULL, 2), "ERR 200");
    TF_STREQ(list(ENG_ORD_ID, NULL, NULL, "AD", 2), "ERR 102");
    TE_FAIL(scan(&s, 9, NULL, NULL, NULL, NULL, 0, NULL), ENG_EARG, "ORDER",
            NULL);
    TE_FAIL(scan(&s, ENG_ORD_ID, NULL, "CARDS", NULL, NULL, 0, NULL),
            ENG_EARG, "CAT", NULL);
    TE_FAIL(scan(&s, ENG_ORD_ID, NULL, NULL, "ABCDEFGHIJK", NULL, 0, NULL),
            ENG_ENOTFOUND, "LOC", "no such location");
    TE_FAIL(scan(&s, ENG_ORD_ID, NULL, NULL, NULL, "AD", 0, NULL), ENG_EARG,
            "STATUS", NULL);

    /* The switches: top level, available (STATUS A, top level, QTY above
       ON_LOAN), which refuses a status beside it. */
    OK(eng_item_set(g_e, 3, te_p, te_kv("QTY=0")));
    g_flags = ENG_IF_AVAIL;
    TF_STREQ(list(ENG_ORD_ID, NULL, NULL, NULL, 2), "1,2,4,5,7");
    TF_STREQ(list(ENG_ORD_LOC, "card", "A1", "", 2), "1");
    TF_STREQ(list(ENG_ORD_ID, NULL, NULL, "A", 2), "ERR 102");
    g_flags = ENG_IF_TOP;
    TF_STREQ(list(ENG_ORD_ID, NULL, NULL, "D", 2), "6");
    g_flags = 0;
    OK(eng_item_set(g_e, 3, te_p, te_kv("QTY=1")));
}

/* Bad cursors, ITEM SERIAL, ITEM FIND, deleted items. */
static void lists_find(void)
{
    eng_scan s;

    /* Bad cursors. */
    TE_FAIL(scan(&s, ENG_ORD_ID, NULL, NULL, NULL, NULL, 0,
                 "ITEMS2:2020202020202032:2"), ENG_ECURSOR, NULL, NULL);
    TE_FAIL(scan(&s, ENG_ORD_ID, NULL, NULL, NULL, NULL, 0, "junk"),
            ENG_ECURSOR, NULL, NULL);
    TE_FAIL(scan(&s, ENG_ORD_LOC, NULL, NULL, "A1", NULL, 0, "ITEMS2:4132:5"),
            ENG_ECURSOR, NULL, NULL);
    TE_FAIL(scan(&s, ENG_ORD_ID, "x", NULL, NULL, NULL, 0, "ITEMS1:20:1"),
            ENG_ECURSOR, NULL, NULL);

    /* SERIAL: exact match ignoring case, paged; too long is 102. */
    g_text = "s100";
    TF_STREQ(pages(op_serial, 1, ENG_T_ITEMS, "ITEM_ID"), "1,3");
    g_text = "S2";
    TF_STREQ(pages(op_serial, 1, ENG_T_ITEMS, "ITEM_ID"), "");
    g_text = "";
    TF_STREQ(pages(op_serial, 2, ENG_T_ITEMS, "ITEM_ID"), "4,6,7");
    TE_FAIL(eng_item_serial(g_e, &s, "0123456789012345678901234567890", NULL),
            ENG_EARG, "SERIAL", NULL);

    /* FIND: substrings of each searched field, A-Z folded. */
    TF_STREQ(find("card", 1), "1,3");
    TF_STREQ(find("BLUE", 2), "6");
    TF_STREQ(find("dos", 2), "6");
    TF_STREQ(find("S1", 2), "1,3");
    TF_STREQ(find("cable", 2), "7");
    TF_STREQ(find("eAgAt", 2), "4,5");
    TF_STREQ(find("portable", 2), "2");
    TF_STREQ(find("nothing", 2), "");
    TF_STREQ(find("c", 2), "1,2,3,6,7");
    TE_FAIL(scan(&s, ENG_ORD_ID, "", NULL, NULL, NULL, 0, NULL), ENG_EARG,
            "TEXT", NULL);
    TE_FAIL(scan(&s, ENG_ORD_ID, "0123456789012345678901234567890", NULL,
                 NULL, NULL, 0, NULL), ENG_EARG, "TEXT", NULL);
    TF_STREQ(find("012345678901234567890123456789", 2), "");
    /* FIND's filters. */
    g_text = "card";
    g_cat = "CARD";
    g_loc = "a1";
    TF_STREQ(pages(op_find, 2, ENG_T_ITEMS, "ITEM_ID"), "1,3");
    g_loc = "B1";
    TF_STREQ(pages(op_find, 2, ENG_T_ITEMS, "ITEM_ID"), "");

    /* Deleted items are skipped by every scan. */
    OK(eng_item_del(g_e, 3));
    TF_STREQ(list(ENG_ORD_MFR, NULL, NULL, NULL, 2), "7,2,1,6,4,5");
    TF_STREQ(find("card", 2), "1");
    g_text = "S100";
    TF_STREQ(pages(op_serial, 2, ENG_T_ITEMS, "ITEM_ID"), "1");
    OK(eng_item_undel(g_e, 3));
}

/* Cursors that stay valid while entries change. */
static void lists_stable(void)
{
    u32 r;
    eng_scan s;
    u32 n8;

    /* Cursor stability: the entry a cursor names is deleted and others
       change around it; the next page continues right after it. */
    OK(scan(&s, ENG_ORD_ID, NULL, NULL, NULL, NULL, 0, NULL));
    OK(eng_scan_next(g_e, &s, &r));
    OK(eng_scan_next(g_e, &s, &r));
    TF_STREQ(te_rtext(ENG_T_ITEMS, TE_BUF, "ITEM_ID"), "2");
    eng_scan_cursor(&s, g_first_cursor);
    OK(eng_item_del(g_e, 2));
    OK(eng_item_del(g_e, 3));
    n8 = add_item("CATEGORY=SW|MFR=Aardvark");
    OK(eng_item_set(g_e, 1, te_p, te_kv("MFR=Zenith")));
    OK(scan(&s, ENG_ORD_ID, NULL, NULL, NULL, NULL, 0, g_first_cursor));
    TF_STREQ(collect(&s), "4,5,6,7,8");
    TF_EQ(n8, 8);
    /* An MFR cursor on a key that has since changed. */
    OK(scan(&s, ENG_ORD_MFR, NULL, NULL, NULL, NULL, 0, NULL));
    OK(eng_scan_next(g_e, &s, &r));
    TF_STREQ(te_rtext(ENG_T_ITEMS, TE_BUF, "ITEM_ID"), "7");
    eng_scan_cursor(&s, g_first_cursor);
    OK(eng_item_set(g_e, 7, te_p, te_kv("MFR=Zzz")));
    OK(scan(&s, ENG_ORD_MFR, NULL, NULL, NULL, NULL, 0, g_first_cursor));
    TF_STREQ(collect(&s), "8,6,4,5,1,7");
}

void test_lists(void)
{
    if (!phase_start())
        return;
    lists_orders();
    lists_find();
    lists_stable();
    phase_end();
}

/* ---------------------------------------------------------------------
 * Code tables: categories and location types.
 * --------------------------------------------------------------------- */
/* The live rows of code table t in list order, each code and ',' in
   g_out; "ERR n" when the list fails. */
static const char *codes(int t)
{
    u32 recs[ENG_MAXCODES];
    u16 n;
    u16 i;
    int rc;

    g_out[0] = '\0';
    rc = eng_code_list(g_e, t, recs, &n);
    if (rc != ENG_OK) {
        sprintf(g_out, "ERR %d", rc);
        eng_clear_error(g_e);
        return g_out;
    }
    for (i = 0; i < n; i++) {
        strcat(g_out, te_rtext(t, recs[i], t == ENG_T_CATS ? "CAT_CODE" :
                                                              "LT_CODE"));
        strcat(g_out, ",");
    }
    return g_out;
}

/* The seed rows of code table t: each table's rows in their list order,
   their sort order counting from 1; *n receives their count. */
static int seeds_ok(int t, u16 *n)
{
    char code[ENG_LTYPELEN + 1];
    char name[ENG_CODENAMELEN + 1];
    char want[OUTMAX];
    u16 sortord;
    u16 i;
    int st;

    want[0] = '\0';
    *n = 0;
    for (i = 0; i < eng_seed_rows(); i++) {
        eng_seed_row(i, &st, code, name, &sortord);
        if (st != t)
            continue;
        if (sortord != ++*n)
            return 0;
        strcat(want, code);
        strcat(want, ",");
    }
    eng_seed_row(eng_seed_rows(), &st, code, name, &sortord);
    return st == -1 && code[0] == '\0' && sortord == 0 &&
           strcmp(codes(t), want) == 0;
}

/* The seed rows, CAT ADD and CAT LIST order. */
static void cats_add(void)
{
    u32 r;
    u32 a0;
    u16 n;

    TF_CHECK(seeds_ok(ENG_T_CATS, &n));
    TF_EQ(n, 10);
    TF_EQ(eng_reccount(g_e, ENG_T_CATS), n);
    TF_CHECK(seeds_ok(ENG_T_LTYPES, &n));
    TF_EQ(n, 8);
    TF_EQ(eng_reccount(g_e, ENG_T_LTYPES), n);
    TF_STREQ(codes(ENG_T_LTYPES), "ROOM,SHELF,RACK,CABINET,DRAWER,BOX,BIN,"
                                  "BUCKET,");
    TF_STREQ(te_rtext(ENG_T_LTYPES, 8, "LT_NAME"), "Bucket");
    a0 = te_naud();
    OK(eng_code_add(g_e, ENG_T_CATS, "zz", te_p, te_kv("CAT_NAME=Last")));
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "CATS|11|I|CAT_CODE||ZZ");
    if (OK(eng_code_get(g_e, ENG_T_CATS, "ZZ", &r))) {
        TF_EQ(r, 11);
        TF_STREQ(te_rtext(ENG_T_CATS, TE_BUF, "SORTORD"), "50");
    }
    OK(eng_code_add(g_e, ENG_T_CATS, "AA", te_p,
                    te_kv("CAT_NAME=First|SORTORD=0")));
    OK(eng_code_add(g_e, ENG_T_CATS, "MM", te_p, te_kv("CAT_NAME=Mid")));
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "SYS", te_p, te_kv("CAT_NAME=x")),
            ENG_EDUP, "CAT_CODE", "category already exists");
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "QQ", te_p, 0), ENG_EARG,
            "CAT_NAME", NULL);
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "QQ", te_p, te_kv("CAT_NAME=")),
            ENG_EARG, "CAT_NAME", NULL);
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "QQ", te_p,
                         te_kv("CAT_NAME=x|SORTORD=-1")), ENG_EARG,
            "SORTORD", NULL);
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "QQ", te_p,
                         te_kv("CAT_NAME=x|SORTORD=100")), ENG_EARG,
            "SORTORD", NULL);
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "QQ", te_p,
                         te_kv("CAT_NAME=x|CAT_CODE=QQ")), ENG_EARG,
            "CAT_CODE", NULL);
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "Q-Q", te_p, te_kv("CAT_NAME=x")),
            ENG_EARG, "CAT_CODE", "bad category code");
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "QQQQQ", te_p,
                         te_kv("CAT_NAME=x")), ENG_EARG, "CAT_CODE", NULL);
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "", te_p, te_kv("CAT_NAME=x")),
            ENG_EARG, "CAT_CODE", NULL);
    TE_FAIL(eng_code_add(g_e, ENG_T_SEQ, "QQ", te_p, te_kv("CAT_NAME=x")),
            ENG_EINTERNAL, "", NULL);
    TF_STREQ(codes(ENG_T_CATS),
             "AA,SYS,MOBO,CPU,CARD,DRV,PERI,SW,MAN,CBL,PART,MM,ZZ,");
    TF_STREQ(codes(ENG_T_ITEMS), "ERR 500");
}

/* CAT SET and CAT DEL. */
static void cats_setdel(void)
{
    u32 id;
    u32 r;
    u32 a0;

    /* SET. */
    a0 = te_naud();
    OK(eng_code_set(g_e, ENG_T_CATS, "mm", te_p,
                    te_kv("CAT_NAME=Middle|SORTORD=5")));
    TF_EQ(te_naud(), a0 + 2);
    TF_STREQ(aud(2), "CATS|13|U|CAT_NAME|Mid|Middle");
    TF_STREQ(aud(1), "CATS|13|U|SORTORD|50|5");
    TE_FAIL(eng_code_set(g_e, ENG_T_CATS, "MM", te_p, te_kv("CAT_NAME=")),
            ENG_EARG, "CAT_NAME", NULL);
    TE_FAIL(eng_code_set(g_e, ENG_T_CATS, "MM", te_p, te_kv("CAT_CODE=NN")),
            ENG_EARG, "CAT_CODE", NULL);
    TE_FAIL(eng_code_set(g_e, ENG_T_CATS, "MM", te_p, te_kv("SORTORD=")),
            ENG_EARG, "SORTORD", NULL);
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "QQ", te_p,
                         te_kv("CAT_NAME=x|SORTORD=")), ENG_EARG, "SORTORD",
            NULL);
    OK(eng_code_set(g_e, ENG_T_CATS, "SYS", te_p, te_kv("SORTORD=0")));
    OK(eng_code_set(g_e, ENG_T_CATS, "SYS", te_p, te_kv("SORTORD=1")));
    TE_FAIL(eng_code_set(g_e, ENG_T_CATS, "NONE", te_p, te_kv("CAT_NAME=x")),
            ENG_ENOTFOUND, "", "no such category");

    /* DEL: refused while a live item uses the code. */
    id = add_item("CATEGORY=MM");
    TE_FAIL(eng_code_del(g_e, ENG_T_CATS, "MM"), ENG_ECONSTR, "",
            "items still use this category");
    OK(eng_item_del(g_e, id));
    a0 = te_naud();
    OK(eng_code_del(g_e, ENG_T_CATS, "MM"));
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "CATS|13|D|||DELETED");
    TE_FAIL(eng_code_get(g_e, ENG_T_CATS, "MM", &r), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_code_del(g_e, ENG_T_CATS, "MM"), ENG_ENOTFOUND, "", NULL);
    TE_FAIL(eng_item_add(g_e, te_p, te_kv("CATEGORY=MM"), &id), ENG_ECONSTR,
            "CATEGORY", NULL);
}

/* The location types: the same rules over LTYPES.DBF, a code of up to
   ENG_LTYPELEN, DEL refused while a live location has the type. */
static void ltypes(void)
{
    u32 r;
    u32 id;
    u32 a0;

    a0 = te_naud();
    OK(eng_code_add(g_e, ENG_T_LTYPES, "pallet", te_p,
                    te_kv("LT_NAME=Pallet|SORTORD=3")));
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "LTYPES|9|I|LT_CODE||PALLET");
    TF_STREQ(codes(ENG_T_LTYPES), "ROOM,SHELF,PALLET,RACK,CABINET,DRAWER,"
                                  "BOX,BIN,BUCKET,");
    TE_FAIL(eng_code_add(g_e, ENG_T_LTYPES, "ABCDEFGHI", te_p,
                         te_kv("LT_NAME=x")), ENG_EARG, "LT_CODE",
            "bad location type code");
    TE_FAIL(eng_code_add(g_e, ENG_T_LTYPES, "BIN", te_p, te_kv("LT_NAME=x")),
            ENG_EDUP, "LT_CODE", "location type already exists");
    TE_FAIL(eng_code_add(g_e, ENG_T_LTYPES, "TOTE", te_p, 0), ENG_EARG,
            "LT_NAME", NULL);
    OK(eng_code_add(g_e, ENG_T_LTYPES, "ABCDEFGH", te_p,
                    te_kv("LT_NAME=Max")));
    OK(eng_code_set(g_e, ENG_T_LTYPES, "pallet", te_p,
                    te_kv("LT_NAME=Skid")));
    TF_STREQ(aud(1), "LTYPES|9|U|LT_NAME|Pallet|Skid");
    TE_FAIL(eng_code_get(g_e, ENG_T_LTYPES, "TOTE", &r), ENG_ENOTFOUND, "",
            "no such location type");
    id = te_loc("P1", 0);
    OK(eng_loc_set(g_e, "P1", te_p, te_kv("LTYPE=pallet")));
    TE_FAIL(eng_code_del(g_e, ENG_T_LTYPES, "PALLET"), ENG_ECONSTR, "",
            "locations still use this location type");
    TE_FAIL(eng_loc_set(g_e, "P1", te_p, te_kv("LTYPE=TOTE")), ENG_ECONSTR,
            "LTYPE", "unknown location type");
    OK(eng_loc_del(g_e, "P1"));
    OK(eng_code_del(g_e, ENG_T_LTYPES, "PALLET"));
    TF_STREQ(aud(1), "LTYPES|9|D|||DELETED");
    TF_EQ(id, 1);
}

/* A full CATS.DBF and the reuse of deleted records. */
static void cats_full(void)
{
    u32 r;
    u32 a0;
    u32 recs[ENG_MAXCODES];
    char code[12];
    u16 n;
    u16 i;

    /* Fill CATS.DBF to ENG_MAXCODES records (record 13 deleted). */
    for (i = (u16)eng_reccount(g_e, ENG_T_CATS); i < ENG_MAXCODES; i++) {
        sprintf(code, "C%02u", (unsigned)i);
        OK(eng_code_add(g_e, ENG_T_CATS, code, te_p,
                        te_kv("CAT_NAME=Filler")));
    }
    TF_EQ(eng_reccount(g_e, ENG_T_CATS), ENG_MAXCODES);
    OK(eng_code_del(g_e, ENG_T_CATS, "C20"));
    /* The first deleted record (13) is taken over, then record 21. */
    a0 = te_naud();
    OK(eng_code_add(g_e, ENG_T_CATS, "NEW1", te_p,
                    te_kv("CAT_NAME=Reused")));
    TF_EQ(te_naud(), a0 + 1);
    TF_STREQ(aud(1), "CATS|13|I|CAT_CODE||NEW1");
    TF_EQ(eng_reccount(g_e, ENG_T_CATS), ENG_MAXCODES);
    if (OK(eng_code_get(g_e, ENG_T_CATS, "NEW1", &r))) {
        TF_EQ(r, 13);
        TF_STREQ(te_rtext(ENG_T_CATS, TE_BUF, "CAT_NAME"), "Reused");
        TF_STREQ(te_rtext(ENG_T_CATS, TE_BUF, "SORTORD"), "50");
    }
    OK(eng_code_add(g_e, ENG_T_CATS, "C20", te_p,
                    te_kv("CAT_NAME=Back|SORTORD=7")));
    if (OK(eng_code_get(g_e, ENG_T_CATS, "C20", &r)))
        TF_EQ(r, 21);
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "FULL", te_p, te_kv("CAT_NAME=x")),
            ENG_ECONSTR, "", "category table full");
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "NEW1", te_p, te_kv("CAT_NAME=x")),
            ENG_EDUP, "CAT_CODE", NULL);
    /* A failed take-over leaves the deleted record as it was. */
    OK(eng_code_del(g_e, ENG_T_CATS, "C30"));
    TE_FAIL(eng_code_add(g_e, ENG_T_CATS, "BAD", te_p,
                         te_kv("CAT_NAME=x|SORTORD=-5")), ENG_EARG, "SORTORD",
            NULL);
    TE_FAIL(eng_code_get(g_e, ENG_T_CATS, "BAD", &r), ENG_ENOTFOUND, "", NULL);
    if (TF_EQ(eng_rec_read(g_e, ENG_T_CATS, 31), ENG_OK)) {
        TF_CHECK(dbf_is_deleted(eng_dbf(g_e, ENG_T_CATS)));
        TF_STREQ(te_rtext(ENG_T_CATS, TE_BUF, "CAT_CODE"), "C30");
    }
    if (OK(eng_code_list(g_e, ENG_T_CATS, recs, &n))) {
        TF_EQ(n, ENG_MAXCODES - 1);
        TF_STREQ(te_rtext(ENG_T_CATS, recs[0], "CAT_CODE"), "AA");
    }
}

void test_cats(void)
{
    if (!phase_start())
        return;
    cats_add();
    cats_setdel();
    ltypes();
    cats_full();
    phase_end();
}

/* ---------------------------------------------------------------------
 * Bounded scans: more items than one request may read.
 * --------------------------------------------------------------------- */
void test_bounded(void)
{
    char want[ENG_CURSORMAX];
    u32 first;
    u32 parent;
    u32 last;
    u32 id;

    if (!phase_start())
        return;
    te_loc("Z1", 0);
    te_loc("Z2", 0);
    first = add_item("CATEGORY=PART|MFR=Needle|LOC_ID=Z1");
    parent = add_item("CATEGORY=SYS|MFR=Rack|LOC_ID=Z1");
    id = add_item("CATEGORY=PART|MFR=Filler|LOC_ID=Z1");
    set_parent(id, parent);
    raw_copies(id, NFILLER - 1);
    last = add_item("CATEGORY=PART|MODEL=haystack needle");
    TF_EQ(first, 1);
    TF_EQ(parent, 2);
    TF_EQ(last, NFILLER + 3);

    /* The first page stops at the read limit after one match. */
    sprintf(want, "1,%lu", (unsigned long)last);
    TF_STREQ(find("NEEDLE", 20), want);
    TF_EQ(g_npages, 2);
    TF_EQ(g_first_count, 1);
    sprintf(want, "ITEMS::%X", (unsigned)ENG_MAXREADS);
    TF_STREQ(g_first_cursor, want);
    /* Every page of 25 entries is full but the last. */
    TF_CHECK(strncmp(list(ENG_ORD_LOC, NULL, "Z1", NULL, 25), "1,2,3,",
                     6) == 0);
    TF_EQ(g_npages, (NFILLER + 2 + 24) / 25);

    /* Live entries refuse at once. */
    TE_FAIL(eng_loc_del(g_e, "Z1"), ENG_ECONSTR, "",
            "location holds items: move or delete them");
    TE_FAIL(eng_item_del(g_e, parent), ENG_ECONSTR, "",
            "item has parts installed: remove them first");
    TE_FAIL(eng_item_set(g_e, parent, te_p, te_kv("LOC_ID=Z2")), ENG_ECONSTR,
            "LOC_ID", "assemblies move as a whole");

    /* ITEM DEL: one deleted child more than a request may read is
       unsettled; exactly ENG_MAXREADS is settled. */
    for (id = 3; id < last; id++)
        raw_delete(id);
    TF_EQ(dbf_flush(eng_dbf(g_e, ENG_T_ITEMS)), DBF_OK);
    TE_FAIL(eng_item_del(g_e, parent), ENG_ECONSTR, "",
            "too many deleted parts; run Admin > Pack");
    TE_FAIL(eng_item_set(g_e, parent, te_p, te_kv("LOC_ID=Z2")), ENG_ECONSTR,
            "LOC_ID", "too many deleted parts; run Admin > Pack");
    TF_EQ(te_raw_put(ENG_T_ITEMS, last - 1, ENG_X_ITEMS3, "PARENT_ID", NULL,
                     0), ENG_OK);
    OK(eng_item_del(g_e, parent));

    /* LOC DEL likewise: ENG_MAXREADS + 3 deleted entries in Z1, then
       three moved to Z2. */
    OK(eng_item_del(g_e, first));
    TE_FAIL(eng_loc_del(g_e, "Z1"), ENG_ECONSTR, "",
            "too many deleted items; run Admin > Pack");
    for (id = last - 3; id < last; id++)
        TF_EQ(te_raw_put(ENG_T_ITEMS, id, ENG_X_ITEMS2, "LOC_ID", NULL, 2),
              ENG_OK);
    OK(eng_loc_del(g_e, "Z1"));
    sprintf(want, "%lu", (unsigned long)last);
    TF_STREQ(find("needle", 20), want);
    TF_STREQ(list(ENG_ORD_LOC, NULL, "Z2", NULL, 25), "");
    phase_end();
}
