/*
 * T_RPT.C - the report and label layout (SRC/CLIENT/RPT.C).
 *
 * usage: T_RPT dir [msgdir [golddir]]
 *        T_RPT -u golddir dir [msgdir]
 *
 * dir is a scratch directory, the test's files there removed first and,
 * when every check passed, last; INV.MSG is read from msgdir and the
 * goldens from golddir, both by default the program's own directory.
 * With -u the outputs are written into golddir instead of compared, to
 * make new goldens (which are then read by eye before they are kept).
 *
 * First msg_path (MSG.C), with which the layout cuts the paths of its
 * group headings and bin labels, is checked on paths it keeps and cuts.
 * Then every report and label kind is fed made-up F, R and K lines, as the
 * response decoder gives them, and laid out into a text file (a PR_FILE
 * job) or, through prn_capture, into the bytes an Epson printer would
 * receive (a PR_PRINTER job of type PR_EPSON).  Each output is compared
 * byte for byte with its golden file, RPT<n>.TXT or RPT<n>.PRN; the
 * last check is that every output was compared, or none with -u.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#define MSG_PATHS
#include "MSG.H"
#include "RPT.H"
#include "ITEMLS.H"
#define TF_MALLOC
#define TF_CMPFILE
#include "TESTFW.H"
#include "T_FILES.H"

#define PATHMAX         80
#define FILEMAX         (PATHMAX + 13)  /* a directory, 8.3 name, NUL   */

static char g_gdir[PATHMAX];
static int g_writing;
static u16 g_nout;              /* outputs finished                     */
static u16 g_ncmp;              /* of them compared with their goldens  */
static char g_cappath[FILEMAX];

/* Every file the test makes in the scratch directory. */
static const char *const k_files[] = {
    "RPT1.TXT", "RPT2.PRN", "RPT3.TXT", "RPT4.TXT", "RPT5.TXT", "RPT6.PRN",
    "RPT7.TXT", "RPT8.PRN", "RPT9.TXT", "RPT10.TXT", "RPT11.TXT",
    "RPT12.TXT", "RPT13.TXT", "RPT14.TXT", "RPT15.TXT", "RPTF.TXT",
    "RPTX.TXT", NULL
};

/* dir + name into out (FILEMAX bytes). */
static void path_of(char *out, const char *dir, const char *name)
{
    strcpy(out, dir);
    strcat(out, name);
}

/* A job for output name: a text file, or (epson) the printer through a
   capture file.  out receives the file's path. */
static pr *job(const char *name, int epson, char *out)
{
    int err;
    int cferr;
    pr *p;

    path_of(out, g_writing ? g_gdir : tfs_dir(), name);
    if (epson) {
        strcpy(g_cappath, out);
        TF_EQ(prn_capture(g_cappath), 0);
        err = pr_open(&p, PR_PRINTER, PR_EPSON, PRN_LPT1, NULL, NULL, NULL,
                      &cferr);
    } else {
        err = pr_open(&p, PR_FILE, PR_PLAIN, 0, out, NULL, NULL, &cferr);
    }
    TF_EQ(err, PR_OK);
    TF_CHECK(p != NULL);
    return p;
}

/* End the job and the capture, and compare the file at path with the
   golden file name, which it must not be. */
static void finish(pr *p, const char *path, const char *name)
{
    char gp[FILEMAX];
    int cferr;

    TF_EQ(pr_close(p, &cferr), PR_OK);
    prn_capture(NULL);
    g_nout++;
    if (!g_writing) {
        g_ncmp++;
        path_of(gp, g_gdir, name);
        TF_CHECK(strcmp(path, gp) != 0 && tf_cmpfile(path, gp));
    }
}

/* Feed one line of kind k whose values are separated by '|'; returns
   what rpt_line returns. */
static int feed_rc(rpt *r, char k, const char *vals)
{
    static char buf[RPT_ROWMAX + 1];
    rq_line ln;
    char *s;

    s = buf;
    strcpy(buf, vals);
    ln.kind = k;
    ln.n = 0;
    ln.over = 0;
    for (;;) {
        ln.v[ln.n++] = s;
        s = strchr(s, '|');
        if (s == NULL || ln.n == RQ_MAXVALS)
            break;
        *s++ = '\0';
    }
    return rpt_line(r, &ln);
}

static void feed(rpt *r, char k, const char *vals)
{
    TF_EQ(feed_rc(r, k, vals), PR_OK);
}

static void geom(rpt_geom *g, u8 pagelen, u8 labelw, u8 labelh)
{
    g->pagelen = pagelen;
    g->labelw = labelw;
    g->labelh = labelh;
}

static const char items_f[] = "ITEM_ID|CATEGORY|MFR|MODEL|SERIALNO|WORKS"
    "|QTY|LOC_ID|LOC_NAME|STATUS|ACQ_DATE|ACQ_PRICE|CUR_VALUE";

/* Item listing by location, 60 rows on pages of 20 lines: groups of
   different sizes, so that headings meet a page's foot; the first is
   the group of no location, the last a path too long for a heading,
   cut to keep its nearest names, also where it is continued. */
static void test_list_paged(void)
{
    static const char *const paths[] = {
        "", "Office", "Office > Shelf A", "Office > Shelf A > Bin 1",
        "Office > Shelf B", "Garage",
        "Garage > Rack with a long name 1 > Box of many cables 3 > "
        "Small bag of screws"
    };
    static const char *const names[] = {
        "", "Office", "Shelf A", "Bin 1", "Shelf B", "Garage",
        "Small bag of screws"
    };
    static const u8 sizes[] = { 3, 7, 5, 12, 1, 22, 10 };
    char path[FILEMAX];
    char row[RPT_ROWMAX];
    rpt_geom g;
    pr *p;
    rpt *r;
    u16 id;
    u8 i;
    u8 k;

    id = 1;
    geom(&g, 20, 35, 6);
    p = job("RPT1.TXT", 0, path);
    TF_EQ(rpt_open(&r, RPT_LIST, RPT_O_GROUP, p, &g, "20261008",
                   "Order LOC"), PR_OK);
    TF_CHECK(r != NULL);
    feed(r, 'F', items_f);
    for (i = 0; i < 7; i++) {
        rpt_group(r, paths[i]);
        for (k = 0; k < sizes[i]; k++, id++) {
            sprintf(row, "%u|%s|Maker %u|Model %u|SN%05u|%c|%u|%u|%s|A"
                    "|19840312|1.00|2.00", id, (id % 3) ? "CARD" : "SYS",
                    id, id * 7u, id, ITL_WORKS[id % 4],
                    (unsigned)(id % 4 + 1), (unsigned)i, names[i]);
            feed(r, 'R', row);
        }
    }
    TF_EQ(rpt_count(r), 60);
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT1.TXT");
}

/* Item listing on the Epson printer, not grouped (rpt_group does
   nothing): cut values, the page length given below the range (taken
   as RPT_MINPAGE), fields in another order. */
static void test_list_epson(void)
{
    char path[FILEMAX];
    rpt_geom g;
    pr *p;
    rpt *r;

    geom(&g, 5, 35, 6);
    p = job("RPT2.PRN", 1, path);
    TF_EQ(rpt_open(&r, RPT_LIST, 0, p, &g, "20261008",
                   "Category CARD"), PR_OK);
    TF_CHECK(r != NULL);
    feed(r, 'F', "LOC_NAME|QTY|ITEM_ID|CATEGORY|MFR|MODEL|SERIALNO|WORKS");
    rpt_group(r, "Office");
    feed(r, 'R', "Box of cables|1|12|CARD|Hercules Computer Technology"
         "|Graphics Card Plus|HGC-0001-ABCDEFGHIJ|I");
    feed(r, 'R', "|2|13|CARD|IBM|CGA||N");
    feed(r, 'R', "Shelf A1|99999|12345678|CARD|AST|SixPakPlus|X|n");
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT2.PRN");
}

/* Valuation by location on pages of 20 lines, with category totals: one
   category not given by rpt_cat, a negative amount, blank amounts, and
   sums that leave the 32-bit range. */
static void test_valuation(void)
{
    char path[FILEMAX];
    char row[RPT_ROWMAX];
    rpt_geom g;
    pr *p;
    rpt *r;
    u16 id;

    geom(&g, 20, 35, 6);
    p = job("RPT3.TXT", 0, path);
    TF_EQ(rpt_open(&r, RPT_VALUE, RPT_O_GROUP, p, &g, "20261008",
                   "Status A"), PR_OK);
    TF_CHECK(r != NULL);
    TF_EQ(rpt_cat(r, "SYS", "Systems"), 0);
    TF_EQ(rpt_cat(r, "CARD", "Expansion cards"), 0);
    TF_EQ(rpt_cat(r, "MAN", "Manuals"), 0);
    feed(r, 'F', items_f);
    for (id = 1; id <= 14; id++) {
        if (id == 1 || id == 9)
            rpt_group(r, id < 9 ? "Office > Shelf A1" : "Office > Shelf A2");
        sprintf(row, "%u|%s|IBM|5150 model %u||Y|%u|%u|%s|A|19840312"
                "|%u.%02u|%u.50", id, (id & 1) ? "SYS" : "CARD", id,
                (unsigned)(id % 3 + 1), id < 9 ? 2u : 3u,
                id < 9 ? "Shelf A1" : "Shelf A2",
                id * 100u, (unsigned)(id % 100), id * 37u);
        feed(r, 'R', row);
    }
    feed(r, 'R', "15|ODD|Acme|Widget||Y|1|3|Shelf A2|A||-12.50|");
    rpt_group(r, "Garage > Bin 1");
    feed(r, 'R', "16|SYS|Big|Spender||Y|1|4|Bin 1|A||20000000.00|1.00");
    feed(r, 'R', "17|SYS|Big|Spender||Y|1|4|Bin 1|A||20000000.00|1.00");
    TF_EQ(rpt_count(r), 17);
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT3.TXT");
}

/* A valuation whose rows, the blank line after them and the total fill
   the first page exactly (RPT_TOP + RPT_BOTTOM lines of a page are not
   body), so "Totals by category" opens the second page, under the
   category headings of its page header and not under a second copy. */
static void test_cat_page(void)
{
    char path[FILEMAX];
    char row[RPT_ROWMAX];
    rpt_geom g;
    pr *p;
    rpt *r;
    u16 id;

    geom(&g, 20, 35, 6);
    p = job("RPT15.TXT", 0, path);
    TF_EQ(rpt_open(&r, RPT_VALUE, 0, p, &g, "20261008", NULL), PR_OK);
    TF_CHECK(r != NULL);
    TF_EQ(rpt_cat(r, "SYS", "Systems"), 0);
    TF_EQ(rpt_cat(r, "CARD", "Expansion cards"), 0);
    feed(r, 'F', items_f);
    for (id = 1; id <= 20 - RPT_TOP - RPT_BOTTOM - 2; id++) {
        sprintf(row, "%u|%s|IBM|5150||Y|1|1|A1|A||%u.00|%u.00", id,
                (id & 1) ? "SYS" : "CARD", id * 10u, id);
        feed(r, 'R', row);
    }
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT15.TXT");
}

/* Overdue loans, with dates, late marks and a deleted item's blank
   maker and model. */
static void test_loans(void)
{
    char path[FILEMAX];
    rpt_geom g;
    pr *p;
    rpt *r;

    geom(&g, 66, 35, 6);
    p = job("RPT4.TXT", 0, path);
    TF_EQ(rpt_open(&r, RPT_LOANS, RPT_O_OVERDUE, p, &g, "20261008",
                   NULL), PR_OK);
    TF_CHECK(r != NULL);
    feed(r, 'F', "LATE|LOAN_ID|ITEM_ID|KIND|QTY|HOLDER|HOST_ID|CONTACT"
         "|OUT_DATE|DUE_DATE|MFR|MODEL");
    feed(r, 'R', "1|5|2|L|1|Eve|0||20260920|20261001|Hercules"
         "|Graphics Card");
    feed(r, 'R', "1|7|12|R|2|Jonathan Livingston|0|555-0101-2233"
         "|20260101|20260201|IBM|5150");
    feed(r, 'R', "0|9|40|R|1||3|x|20261001|20261009||");
    feed(r, 'R', "0|11|41|U|1||12||20261001|||");
    TF_EQ(rpt_count(r), 4);
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT4.TXT");
}

/* An empty report: one page holding "No records". */
static void test_empty(void)
{
    char path[FILEMAX];
    rpt_geom g;
    pr *p;
    rpt *r;

    geom(&g, 66, 35, 6);
    p = job("RPT5.TXT", 0, path);
    TF_EQ(rpt_open(&r, RPT_LIST, RPT_O_GROUP, p, &g, "20261008", ""), PR_OK);
    TF_CHECK(r != NULL);
    feed(r, 'F', items_f);
    TF_EQ(rpt_count(r), 0);
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT5.TXT");
}

/* Item labels 35 x 6 on the Epson printer from rows. */
static void test_ilabel_rows(void)
{
    char path[FILEMAX];
    rpt_geom g;
    pr *p;
    rpt *r;

    geom(&g, 66, 35, 6);
    p = job("RPT6.PRN", 1, path);
    TF_EQ(rpt_open(&r, RPT_ILABEL, 0, p, &g, "20261008", NULL), PR_OK);
    TF_CHECK(r != NULL);
    feed(r, 'F', items_f);
    feed(r, 'R', "12|CARD|IBM|5150 CGA|SN-0001|Y|1|1|Shelf A1|A|19840312"
         "|1.00|2.00");
    feed(r, 'R', "13|SYS|Compaq Computer Corporation|Portable Plus Model 2"
         "||Y|1|0||A|19840312|1.00|2.00");
    TF_EQ(rpt_count(r), 2);
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT6.PRN");
}

/* Item labels 20 x 3 as text, from the K lines of single records (the
   width and height given out of range); fields the kind does not read
   are left out. */
static void test_ilabel_get(void)
{
    char path[FILEMAX];
    rpt_geom g;
    pr *p;
    rpt *r;

    geom(&g, 66, 90, 2);
    p = job("RPT7.TXT", 0, path);
    g.labelw = 10;
    TF_EQ(rpt_open(&r, RPT_ILABEL, 0, p, &g, "20261008", NULL), PR_OK);
    TF_CHECK(r != NULL);
    feed(r, 'K', "ITEM_ID|12345678");
    feed(r, 'K', "CATEGORY|PERI");
    feed(r, 'K', "MFR|Hayes Microcomputer Products");
    feed(r, 'K', "DESCR|not read by a label");
    feed(r, 'K', "MODEL|Smartmodem 1200");
    feed(r, 'K', "LOC_ID|12");
    feed(r, 'K', "LOC_NAME|Bin twelve on the left");
    TF_EQ(rpt_record(r), PR_OK);
    feed(r, 'K', "ITEM_ID|7");
    feed(r, 'K', "MFR|Apple");
    TF_EQ(rpt_record(r), PR_OK);
    TF_EQ(rpt_count(r), 2);
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT7.TXT");
}

/* Bin labels 35 x 6 on the Epson printer from rows: the name double
   width (cut), the code only when there is one, the path cut to keep
   its nearest names, the type's name. */
static void test_blabel_rows(void)
{
    char path[FILEMAX];
    rpt_geom g;
    pr *p;
    rpt *r;

    geom(&g, 66, 35, 6);
    p = job("RPT8.PRN", 1, path);
    TF_EQ(rpt_open(&r, RPT_BLABEL, 0, p, &g, "20261008", NULL), PR_OK);
    TF_CHECK(r != NULL);
    feed(r, 'F', "LOC_NAME|LOC_CODE|PATH|LT_NAME");
    feed(r, 'R', "Shelf A|A1|Garage > Shelf A|Shelf");
    feed(r, 'R', "A bin with a long name that is cut||Office > Cabinet of "
         "drawers > Drawer 3 > A bin with a long name that is cut|Bin");
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT8.PRN");
}

/* A bin label 20 x 4 as text from K lines, at the narrowest width: the
   name in double width, the code, and the path, as the label's three
   text lines. */
static void test_blabel_get(void)
{
    char path[FILEMAX];
    rpt_geom g;
    pr *p;
    rpt *r;

    geom(&g, 66, 20, 4);
    p = job("RPT9.TXT", 0, path);
    TF_EQ(rpt_open(&r, RPT_BLABEL, 0, p, &g, "20261008", NULL), PR_OK);
    TF_CHECK(r != NULL);
    feed(r, 'K', "LOC_NAME|Attic box");
    feed(r, 'K', "LOC_CODE|ATTIC-BOX2");
    feed(r, 'K', "PATH|House > Attic > Attic box");
    feed(r, 'K', "LT_NAME|Box");
    TF_EQ(rpt_record(r), PR_OK);
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT9.TXT");
}

/* Item listing totals: an R line before the F line (ignored), a row with
   fewer values than the F line names, and quantities whose sum leaves
   the 32-bit range. */
static void test_list_edges(void)
{
    char path[FILEMAX];
    rpt_geom g;
    pr *p;
    rpt *r;

    geom(&g, 66, 35, 6);
    p = job("RPT10.TXT", 0, path);
    TF_EQ(rpt_open(&r, RPT_LIST, 0, p, &g, "20261008", "Totals"), PR_OK);
    TF_CHECK(r != NULL);
    feed(r, 'R', "1|SYS|before|the F line");
    TF_EQ(rpt_count(r), 0);
    feed(r, 'F', items_f);
    feed(r, 'R', "2|SYS|Short|row");
    feed(r, 'R', "3|SYS|Many|units||Y|2000000000|1|A1");
    feed(r, 'R', "4|SYS|Many|more||Y|2000000000|1|A1");
    TF_EQ(rpt_count(r), 3);
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT10.TXT");
}

/* Valuation with more categories than the table holds: three given by
   rpt_cat, then one new code a row, 120 rows; the codes beyond the
   table are summed on the "Other categories" line, and the table runs
   over several pages.  One price does not fit 32 bits at all. */
static void test_many_cats(void)
{
    char path[FILEMAX];
    char row[RPT_ROWMAX];
    rpt_geom g;
    pr *p;
    rpt *r;
    u16 id;

    geom(&g, 30, 35, 6);
    p = job("RPT11.TXT", 0, path);
    TF_EQ(rpt_open(&r, RPT_VALUE, 0, p, &g, "20261008", NULL), PR_OK);
    TF_CHECK(r != NULL);
    TF_EQ(rpt_cat(r, "C1", "First"), 0);
    TF_EQ(rpt_cat(r, "C2", "Second"), 0);
    TF_EQ(rpt_cat(r, "C3", "Third"), 0);
    feed(r, 'F', items_f);
    for (id = 1; id <= 120; id++) {
        sprintf(row, "%u|C%u|Mfr|Model||Y|1|1|A1|A||%u.00|1.00", id, id,
                id);
        feed(r, 'R', row);
    }
    feed(r, 'R', "121|C1|Gold|Bar||Y|1|1|A1|A||99999999999.00|1.00");
    TF_EQ(rpt_count(r), 121);
    TF_EQ(rpt_close(r), PR_OK);
    finish(p, path, "RPT11.TXT");
}

/* Runs ended by rpt_abort: a grouped listing and a valuation stop
   after their last row with no subtotal, total or category table; a
   label run, which takes no groups, stops after its last label. */
static void test_stopped(void)
{
    static const u8 kinds[3] = { RPT_LIST, RPT_VALUE, RPT_ILABEL };
    static const char *const names[3] = { "RPT12.TXT", "RPT13.TXT",
                                          "RPT14.TXT" };
    char path[FILEMAX];
    char row[RPT_ROWMAX];
    rpt_geom g;
    pr *p;
    rpt *r;
    u16 id;
    u8 k;

    geom(&g, 20, 35, 4);
    for (k = 0; k < 3; k++) {
        p = job(names[k], 0, path);
        TF_EQ(rpt_open(&r, kinds[k], k == 2 ? 0 : RPT_O_GROUP, p, &g,
                       "20261008", "Stopped"), PR_OK);
        TF_CHECK(r != NULL);
        TF_EQ(rpt_cat(r, "SYS", "Systems"), 0);
        feed(r, 'F', items_f);
        for (id = 1; id <= (k == 2 ? 2 : 12); id++) {
            if (id == 1 || id == 6)
                rpt_group(r, id < 6 ? "A1" : "A1 > B2");
            sprintf(row, "%u|SYS|IBM|5150|S%u|Y|1|%u|%s|A||10.00|20.00",
                    id, id, id < 6 ? 1u : 2u, id < 6 ? "A1" : "B2");
            feed(r, 'R', row);
        }
        TF_EQ(rpt_abort(r), PR_OK);
        finish(p, path, names[k]);
    }
    TF_EQ(rpt_abort(NULL), PR_OK);
}

/* A job whose file cannot be written: rpt_line, rpt_close, rpt_abort
   and pr_close report PR_EFILE once the first buffer is sent. */
static void test_failed_job(void)
{
    char path[FILEMAX];
    rpt_geom g;
    pr *p;
    rpt *r;
    int rc;
    int cferr;
    u8 i;

    rc = PR_OK;
    geom(&g, 66, 35, 6);
    path_of(path, tfs_dir(), "RPTF.TXT");
    TF_EQ(pr_open(&p, PR_FILE, PR_PLAIN, 0, path, NULL, NULL, &cferr), PR_OK);
    TF_CHECK(p != NULL);
    TF_EQ(rpt_open(&r, RPT_LIST, 0, p, &g, "20261008", NULL), PR_OK);
    TF_CHECK(r != NULL);
    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 0, CF_EFULL);
    rc = feed_rc(r, 'F', items_f);
    for (i = 0; i < 10 && rc == PR_OK; i++)
        rc = feed_rc(r, 'R', "1|SYS|IBM|5150|X|Y|1|1|A1");
    TF_EQ(rc, PR_EFILE);
    TF_EQ(rpt_close(r), PR_EFILE);
    TF_EQ(pr_close(p, &cferr), PR_EFILE);
    TF_EQ(cferr, CF_EFULL);
    TF_EQ(pr_open(&p, PR_FILE, PR_PLAIN, 0, path, NULL, NULL, &cferr), PR_OK);
    TF_CHECK(p != NULL);
    TF_EQ(rpt_open(&r, RPT_LIST, 0, p, &g, "20261008", NULL), PR_OK);
    TF_CHECK(r != NULL);
    TF_EQ(feed_rc(r, 'F', items_f), PR_OK);
    TF_EQ(feed_rc(r, 'R', "1|SYS|IBM|5150|X|Y|1|1|A1"), PR_EFILE);
    TF_EQ(rpt_abort(r), PR_EFILE);
    TF_EQ(pr_close(p, &cferr), PR_EFILE);
    tfs_fault_off();
}

static int fail_alloc(u16 size)
{
    return size > 0;
}

#ifdef COMPAT_HOST
/* rpt_open of kind with each of its allocations failed in turn: every
   one gives PR_ENOMEM, no report and no memory held. */
static void no_memory(pr *p, const rpt_geom *g, u8 kind)
{
    rpt *r;
    u16 n;
    u16 i;
    u16 held;

    held = tm_held();
    tm_fail(0);
    TF_EQ(rpt_open(&r, kind, 0, p, g, "20261008", NULL), PR_OK);
    n = tm_calls();
    rpt_close(r);
    TF_CHECK(n > 0);
    for (i = 1; i <= n; i++) {
        tm_fail(i);
        TF_EQ(rpt_open(&r, kind, 0, p, g, "20261008", NULL), PR_ENOMEM);
        TF_CHECK(r == NULL);
        TM_HELD(held);
    }
    tm_fail(0);
}
#endif

/* Refusals: an unknown kind, a full category table, no memory (near, on
   the host, and far) for the report; rpt_cat outside a valuation, and a
   NULL close, do nothing. */
static void test_edges(void)
{
    char path[FILEMAX];
    rpt_geom g;
    pr *p;
    rpt *r;
    u16 i;
    char code[8];
    int rc;
    int cferr;

    rc = 0;
    geom(&g, 66, 35, 6);
    path_of(path, tfs_dir(), "RPTX.TXT");
    TF_EQ(pr_open(&p, PR_FILE, PR_PLAIN, 0, path, NULL, NULL, &cferr), PR_OK);
    TF_CHECK(p != NULL);
    TF_EQ(rpt_open(&r, 0, 0, p, &g, "20261008", NULL), RPT_EKIND);
    TF_CHECK(r == NULL);
    TF_EQ(rpt_open(&r, RPT_BLABEL + 1, 0, p, &g, "20261008", NULL),
          RPT_EKIND);
    TF_CHECK(r == NULL);
    TF_EQ(rpt_close(NULL), PR_OK);
    TF_EQ(rpt_open(&r, RPT_LIST, 0, p, &g, "20261008", NULL), PR_OK);
    TF_CHECK(r != NULL);
    TF_EQ(rpt_cat(r, "SYS", "Systems"), 0);
    rpt_close(r);
    TF_EQ(rpt_open(&r, RPT_VALUE, 0, p, &g, "20261008", NULL), PR_OK);
    TF_CHECK(r != NULL);
    rc = 0;
    for (i = 0; i < LINK_MAXCODES && rc == 0; i++) {
        sprintf(code, "C%u", i);
        rc = rpt_cat(r, code, "x");
    }
    TF_EQ(rc, 0);
    TF_EQ(rpt_cat(r, "ONE", "too many"), PR_ENOMEM);
    rpt_close(r);
    cmem_fault = fail_alloc;
    TF_EQ(rpt_open(&r, RPT_VALUE, 0, p, &g, "20261008", NULL), PR_ENOMEM);
    TF_CHECK(r == NULL);
    cmem_fault = NULL;
#ifdef COMPAT_HOST
    no_memory(p, &g, RPT_LIST);
    no_memory(p, &g, RPT_VALUE);
    no_memory(p, &g, RPT_ILABEL);
#endif
    TF_EQ(pr_close(p, &cferr), PR_OK);
}

/* msg_path, with which the layout cuts headings and labels, of path at
   width w is want, written within w + 1 bytes. */
static void path_is(const char *path, u16 w, const char *want)
{
    char out[96];

    memset(out, '#', sizeof out);
    msg_path(out, path, w);
    if (!TF_STREQ(out, want))
        printf("  msg_path(\"%s\", %u)\n", path, (unsigned)w);
    TF_CHECK(strlen(out) <= w);
    TF_EQ(out[w + 1], '#');
}

static void test_paths(void)
{
    path_is("", 10, "");
    path_is("Office", 10, "Office");
    path_is("Office > Shelf A", 16, "Office > Shelf A");
    /* The nearest names that fit after the cut. */
    path_is("Office > Shelf A", 15, "... > Shelf A");
    path_is("Office > Shelf A > Bin 1", 21, "... > Shelf A > Bin 1");
    path_is("Office > Shelf A > Bin 1", 20, "... > Bin 1");
    path_is("Office > Shelf A > Bin 1", 11, "... > Bin 1");
    /* A last name too long for that stands alone, cut at its end when
       even that is too long. */
    path_is("Office > Shelf A > Bin 1", 10, "Bin 1");
    path_is("Office > Shelf A > Bin 12345", 7, "Bin 12~");
    path_is("A very long name of one location", 12, "A very long~");
    path_is("Office > A long name of a location", 20,
            "A long name of a lo~");
    path_is("... > Rack 1 > Box 3", 14, "... > Box 3");
}

int main(int argc, char **argv)
{
    char mdir[PATHMAX];
    int a;

    a = 1;
    cmem_init();
    tf_begin("T_RPT");
    tf_progdir(mdir, PATHMAX, argv[0]);
    strcpy(g_gdir, mdir);
    if (argc > 2 && strcmp(argv[1], "-u") == 0 &&
        strlen(argv[2]) < PATHMAX) {
        g_writing = 1;
        strcpy(g_gdir, argv[2]);
        a = 3;
    }
    if (argc <= a || strlen(argv[a]) >= PATHMAX) {
        printf("usage: T_RPT [-u golddir] dir [msgdir [golddir]]\n");
        return 2;
    }
    tfs_set_dir(argv[a]);
    tfs_wipe(k_files);
    if (argc > a + 1 && strlen(argv[a + 1]) < PATHMAX)
        strcpy(mdir, argv[a + 1]);
    if (!g_writing && argc > a + 2 && strlen(argv[a + 2]) < PATHMAX)
        strcpy(g_gdir, argv[a + 2]);
    if (!TF_EQ(msg_load(mdir, MSG_FILE), 0)) {
        printf("  %s\n", msg_why());
        return tf_done();
    }
    test_paths();
    test_list_paged();
    test_list_epson();
    test_valuation();
    test_cat_page();
    test_loans();
    test_empty();
    test_ilabel_rows();
    test_ilabel_get();
    test_blabel_rows();
    test_blabel_get();
    test_list_edges();
    test_many_cats();
    test_stopped();
    test_failed_job();
    test_edges();
    TF_EQ(g_ncmp, g_writing ? 0 : g_nout);
    msg_free();
    if (tf_fails == 0)
        tfs_wipe(k_files);
    return tf_done();
}
