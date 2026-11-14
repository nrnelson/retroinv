/*
 * T_ITEMLS.C - the item list's row source (SRC/CLIENT/ITEMLS.H).
 *
 * usage: T_ITEMLS dir [msgdir]
 *
 * dir is a scratch data directory prefix holding nothing but this
 * test's files; its engine files are removed first and last.  msgdir is
 * the directory prefix of the built INV.MSG (default: the program's
 * own directory).  Written from the contract in ITEMLS.H alone.
 *
 * The rows are checked against a model of the layout the column
 * headers show: the columns of each order start under their header
 * words, so the widths below are the header's, and a first test checks
 * the header texts for them.
 *
 * Covered:
 * - the template's orders, command, field lists, walk field and messages;
 * - the build's agreement between the browse commands and the orders;
 * - itl_row's columns for every order with canned records (field order,
 *   missing fields, cut, padding, leading spaces, the key, no overrun of
 *   the text, the reason of each AVAIL letter, the word of each WORKS
 *   letter);
 * - the template as a client uses it, a pager over the engine on five
 *   items whose four orders differ: the exact requests of each order, the
 *   rows and keys they give, the End walk's field list, and the template
 *   turned into another item command.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TRLINK.H"
#include "TEWIPE.H"
#define TF_LINK
#include "TESTFW.H"
#define PAGER_CALLS
#include "ITEMLS.H"
#include "SCR.H"

#define PATHMAX         96
#define NORD            4       /* orders: ID, LOC, MFR, SERIAL         */
#define NITEMS          5
#define ROWMAX          (LV_ROWTEXT + 1) /* bytes of a row's text   */

/* Widths of the columns, which the headers must show (test_header). */
#define W_ID            8
#define W_CAT           4
#define W_MFR           15
#define W_MODEL         15
#define W_WORKS         5
#define W_QTY           5
#define W_LOC           10      /* the location's name                  */
#define W_WHY           9       /* why the item is not available        */
#define W_SER           20
#define W_SLOC          16      /* ... in the serial order              */

static char g_ddir[PATHMAX];

/* ---------------------------------------------------------------------
 * The model of a row.
 * --------------------------------------------------------------------- */

/* Append s to t in w cells, cut or padded, right-aligned when right,
   and one space. */
static void cell(char *t, const char *s, unsigned w, int right)
{
    unsigned n;
    unsigned pad;
    unsigned at;

    n = (unsigned)strlen(s);
    at = (unsigned)strlen(t);
    if (n > w)
        n = w;
    pad = w - n;
    if (right) {
        memset(t + at, ' ', pad);
        at += pad;
    }
    memcpy(t + at, s, n);
    at += n;
    if (!right) {
        memset(t + at, ' ', pad);
        at += pad;
    }
    t[at++] = ' ';
    t[at] = '\0';
}

/* Remove t's trailing spaces. */
static void rtrim(char *t)
{
    size_t n;

    n = strlen(t);
    while (n > 0 && t[n - 1] == ' ')
        t[--n] = '\0';
}

/* The row of an item in the default layout. */
static void want_def(char *t, const char *id, const char *cat,
                     const char *mfr, const char *model, const char *works,
                     const char *qty, const char *loc, const char *why)
{
    t[0] = '\0';
    cell(t, id, W_ID, 1);
    cell(t, cat, W_CAT, 0);
    cell(t, mfr, W_MFR, 0);
    cell(t, model, W_MODEL, 0);
    cell(t, works, W_WORKS, 0);
    cell(t, qty, W_QTY, 1);
    cell(t, loc, W_LOC, 0);
    cell(t, why, W_WHY, 0);
    rtrim(t);
}

/* The row of an item in the serial number layout. */
static void want_ser(char *t, const char *id, const char *mfr,
                     const char *model, const char *ser, const char *loc)
{
    t[0] = '\0';
    cell(t, id, W_ID, 1);
    cell(t, mfr, W_MFR, 0);
    cell(t, model, W_MODEL, 0);
    cell(t, ser, W_SER, 0);
    cell(t, loc, W_SLOC, 0);
    rtrim(t);
}

/* ---------------------------------------------------------------------
 * Canned records.
 * --------------------------------------------------------------------- */
#define MAXKV           12

typedef struct {
    const char *name;
    const char *val;
} kv;

/* Make r of the pairs kv[0..n-1]. */
static void make_rec(lv_rec *r, char **names, char **vals, const kv *p,
                     u16 n)
{
    u16 i;

    for (i = 0; i < n; i++) {
        names[i] = (char *)p[i].name;
        vals[i] = (char *)p[i].val;
    }
    r->n = n;
    r->names = names;
    r->vals = vals;
}

/* What itl_row writes for the pairs p in order, checking it stays in
   its text and key, within INV's row: text of ROWMAX bytes, trailing
   spaces removed. */
static void run_row(const kv *p, u16 n, u8 order, char *text, char *key)
{
    struct {
        char t[LV_ROWTEXT + 1];
        char guard[16];
    } buf;
    struct {
        char k[LV_KEYSZ];
        char guard[16];
    } kb;
    char *names[MAXKV];
    char *vals[MAXKV];
    lv_rec r;
    u16 i;

    make_rec(&r, names, vals, p, n);
    memset(&buf, 0, sizeof buf);
    memset(buf.guard, 0x5A, sizeof buf.guard);
    memset(&kb, 0, sizeof kb);
    memset(kb.guard, 0x5A, sizeof kb.guard);
    itl_row(&r, order, buf.t, kb.k);
    for (i = 0; i < sizeof buf.guard; i++)
        TF_CHECK(buf.guard[i] == 0x5A);
    for (i = 0; i < sizeof kb.guard; i++)
        TF_CHECK(kb.guard[i] == 0x5A);
    strcpy(text, buf.t);
    rtrim(text);
    TF_CHECK(strlen(text) <= LV_TEXTLEN);
    strcpy(key, kb.k);
}

/* ---------------------------------------------------------------------
 * The template and the messages.
 * --------------------------------------------------------------------- */

/* 1 when the comma list s holds exactly the names of want (a comma
   list), in any order. */
static int same_names(const char *s, const char *want)
{
    char a[96];
    char *p;
    size_t n;

    if (strlen(s) != strlen(want) || strlen(s) >= sizeof a)
        return 0;
    strcpy(a, want);
    for (p = a; *p != '\0';) {
        const char *e;

        e = strchr(p, ',');
        n = e != NULL ? (size_t)(e - p) : strlen(p);
        {
            char w[32];
            char *at;

            memcpy(w, p, n);
            w[n] = '\0';
            at = strstr(s, w);
            if (at == NULL || (at != s && at[-1] != ',') ||
                (at[n] != ',' && at[n] != '\0'))
                return 0;
        }
        p += n;
        if (*p == ',')
            p++;
    }
    return 1;
}

static void test_src(void)
{
    u8 i;

    TF_EQ(itl_src.norders, NORD);
    TF_STREQ(itl_src.cmd, "ITEM LIST");
    TF_STREQ(itl_src.orders[0], "ID");
    TF_STREQ(itl_src.orders[1], "LOC");
    TF_STREQ(itl_src.orders[2], "MFR");
    TF_STREQ(itl_src.orders[3], "SERIAL");
    /* The default fields for the first three orders, a list of its own
       for SERIAL. */
    for (i = 0; i < 3; i++)
        TF_CHECK(itl_src.fields[i] == NULL);
    TF_CHECK(itl_src.fields[3] != NULL);
    if (itl_src.fields[3] != NULL)
        TF_CHECK(same_names(itl_src.fields[3],
                            "ITEM_ID,MFR,MODEL,SERIALNO,LOC_NAME"));
    TF_CHECK(itl_src.walkfields != NULL);
    if (itl_src.walkfields != NULL)
        TF_STREQ(itl_src.walkfields, "ITEM_ID");
    for (i = 0; i < 3; i++)
        TF_EQ(itl_src.header[i], MI_HDR);
    TF_EQ(itl_src.header[3], MI_HDRSER);
    TF_EQ(itl_src.empty, MI_EMPTY);
    TF_CHECK(itl_src.fmt == NULL);
    TF_CHECK(itl_src.mark == NULL);
    TF_CHECK(itl_src.norders <= LV_MAXORDERS);
    /* The browse commands run one per order, in the orders' sequence. */
    TF_EQ(SCMD_ITEMS_SER - SCMD_ITEMS_ID + 1, itl_src.norders);
    TF_EQ(SCMD_ITEMS_LOC, SCMD_ITEMS_ID + 1);
    TF_EQ(SCMD_ITEMS_MFR, SCMD_ITEMS_ID + 2);
}

/* The column of word w in header h, or -1. */
static int at_of(const char *h, const char *w)
{
    const char *p;

    p = strstr(h, w);
    return p != NULL ? (int)(p - h) : -1;
}

static void test_header(void)
{
    const char *h;
    const char *s;
    int c;

    h = msg(MI_HDR);
    s = msg(MI_HDRSER);
    TF_STREQ(msg(MI_EMPTY), "No items");
    TF_CHECK(strlen(h) <= LV_TEXTLEN);
    TF_CHECK(strlen(s) <= LV_TEXTLEN);
    /* Default order: the columns start under their words. */
    TF_EQ(at_of(h, "ID") + 2, W_ID);
    c = W_ID + 1;
    TF_EQ(at_of(h, "Cat"), c);
    c += W_CAT + 1;
    TF_EQ(at_of(h, "Maker"), c);
    c += W_MFR + 1;
    TF_EQ(at_of(h, "Model"), c);
    c += W_MODEL + 1;
    TF_EQ(at_of(h, "Works"), c);
    c += W_WORKS + 1;
    TF_EQ(at_of(h, "Qty") + 3, c + W_QTY);
    c += W_QTY + 1;
    TF_EQ(at_of(h, "Location"), c);
    c += W_LOC + 1;
    TF_EQ(at_of(h, "Status"), c);
    TF_EQ(c + W_WHY, LV_TEXTLEN);
    /* Serial order. */
    TF_EQ(at_of(s, "ID") + 2, W_ID);
    c = W_ID + 1;
    TF_EQ(at_of(s, "Maker"), c);
    c += W_MFR + 1;
    TF_EQ(at_of(s, "Model"), c);
    c += W_MODEL + 1;
    TF_EQ(at_of(s, "Serial number"), c);
    c += W_SER + 1;
    TF_EQ(at_of(s, "Location"), c);
    TF_CHECK(c + W_SLOC <= LV_TEXTLEN);
}

/* ---------------------------------------------------------------------
 * itl_row on canned records.
 * --------------------------------------------------------------------- */
static const kv full_rec[] = {
    { "ITEM_ID", "12" }, { "CATEGORY", "SYS" }, { "MFR", "IBM" },
    { "MODEL", "5150" }, { "SERIALNO", "0012345" }, { "COND", "GD" },
    { "WORKS", "Y" }, { "QTY", "3" }, { "AVAIL", "Y" },
    { "LOC_NAME", "Shelf A1" }, { "STATUS", "A" }
};

static void test_default_rows(void)
{
    char want[ROWMAX];
    char got[ROWMAX];
    char key[LV_KEYSZ];
    u8 o;

    want_def(want, "12", "SYS", "IBM", "5150", msg(MT_WK_Y), "3",
             "Shelf A1", "");
    /* ID, LOC and MFR orders show the same default columns. */
    for (o = 0; o < 3; o++) {
        run_row(full_rec, 11, o, got, key);
        TF_STREQ(got, want);
        TF_STREQ(key, "12");
    }
    /* The ID is right-aligned, so a longer one pushes nothing. */
    TF_EQ(got[W_ID - 2], '1');
    TF_EQ(got[W_ID - 1], '2');
    TF_EQ(got[W_ID - 3], ' ');
}

static void test_field_order(void)
{
    static const kv shuffled[] = {
        { "AVAIL", "D" }, { "LOC_NAME", "Box B2" }, { "QTY", "11" },
        { "WORKS", "N" }, { "MODEL", "TM100-2" }, { "MFR", "Tandon" },
        { "CATEGORY", "DRV" }, { "ITEM_ID", "2" }
    };
    static const kv extra[] = {
        { "ITEM_ID", "2" }, { "DESCR", "unused" }, { "CATEGORY", "DRV" },
        { "MFR", "Tandon" }, { "MODEL", "TM100-2" }, { "WORKS", "N" },
        { "QTY", "11" }, { "ACQ_DATE", "19840312" }, { "LOC_NAME", "Box B2" },
        { "AVAIL", "D" }
    };
    char want[ROWMAX];
    char got[ROWMAX];
    char key[LV_KEYSZ];

    want_def(want, "2", "DRV", "Tandon", "TM100-2", msg(MT_WK_N), "11",
             "Box B2",
             msg(MH_AV_D));
    run_row(shuffled, 8, 0, got, key);
    TF_STREQ(got, want);
    TF_STREQ(key, "2");
    run_row(extra, 10, 0, got, key);
    TF_STREQ(got, want);
    TF_STREQ(key, "2");
}

static void test_missing(void)
{
    static const kv few[] = { { "ITEM_ID", "7" }, { "MODEL", "XT" } };
    static const kv idonly[] = { { "ITEM_ID", "99999999" } };
    static const kv none[] = { { "MFR", "Zilog" } };
    char want[ROWMAX];
    char got[ROWMAX];
    char key[LV_KEYSZ];

    /* A field the record lacks is a blank column, the others stay in
       their places. */
    want_def(want, "7", "", "", "XT", "", "", "", "");
    run_row(few, 2, 0, got, key);
    TF_STREQ(got, want);
    TF_STREQ(key, "7");
    want_def(want, "99999999", "", "", "", "", "", "", "");
    run_row(idonly, 1, 2, got, key);
    TF_STREQ(got, want);
    TF_STREQ(key, "99999999");
    want_def(want, "", "", "Zilog", "", "", "", "", "");
    run_row(none, 1, 1, got, key);
    TF_STREQ(got, want);
    TF_STREQ(key, "");
    run_row(none, 0, 0, got, key);
    TF_STREQ(got, "");
    TF_STREQ(key, "");
}

static void test_cut(void)
{
    static const kv longs[] = {
        { "ITEM_ID", "31" }, { "CATEGORY", "CARDS" },
        { "MFR", "Manufacturer-of-very-long-names" },
        { "MODEL", "A-model-name-of-thirty-chars-xx" }, { "WORKS", "I" },
        { "QTY", "5" }, { "LOC_NAME", "The shelf by the door" },
        { "AVAIL", "R" }
    };
    static const kv lead[] = {
        { "ITEM_ID", " 4" }, { "CATEGORY", " SYS" }, { "MFR", "  IBM" },
        { "MODEL", " 5150" }, { "WORKS", "?" }, { "QTY", " 2" },
        { "LOC_NAME", " Shelf A1" }, { "AVAIL", "Y" }
    };
    char want[ROWMAX];
    char got[ROWMAX];
    char key[LV_KEYSZ];

    /* Over-long fields are cut at their column, a maker's, a model's and
       a location's name with MSG_CUTMARK in its last cell, and the next
       column still starts where the header has it. */
    want_def(want, "31", "CARDS", "Manufacturer-o~", "A-model-name-o~",
             msg(MT_WK_I), "5", "The shelf~", msg(MH_K_RES));
    run_row(longs, 8, 0, got, key);
    TF_STREQ(got, want);
    TF_EQ(strlen(got), W_ID + 1 + W_CAT + 1 + W_MFR + 1 + W_MODEL + 1 +
          W_WORKS + 1 + W_QTY + 1 + W_LOC + 1 + strlen(msg(MH_K_RES)));
    /* Leading spaces of a value are dropped from the text, but the key is
       ITEM_ID as given. */
    want_def(want, "4", "SYS", "IBM", "5150", msg(MT_WK_Q), "2", "Shelf A1",
             "");
    run_row(lead, 8, 0, got, key);
    TF_STREQ(got, want);
    TF_STREQ(key, " 4");
}

/* The reason column: blank for an available item (Y) or a record
   without AVAIL, else the name of AVAIL's reason, MH_K_NAME and the
   five after it in the order L R U I D N. */
static void test_why(void)
{
    static const char letters[] = "LRUIDN";
    kv rec[3];
    char want[ROWMAX];
    char got[ROWMAX];
    char key[LV_KEYSZ];
    char a[2];
    u16 i;

    rec[0].name = "ITEM_ID";
    rec[0].val = "5";
    rec[1].name = "LOC_NAME";
    rec[1].val = "Den";
    rec[2].name = "AVAIL";
    rec[2].val = a;
    a[1] = '\0';
    for (i = 0; i < sizeof letters - 1; i++) {
        a[0] = letters[i];
        want_def(want, "5", "", "", "", "", "", "Den",
                 msg((u16)(MH_K_NAME + i)));
        run_row(rec, 3, 0, got, key);
        TF_STREQ(got, want);
    }
    want_def(want, "5", "", "", "", "", "", "Den", "");
    a[0] = 'Y';
    run_row(rec, 3, 0, got, key);
    TF_STREQ(got, want);
    a[0] = '\0';
    run_row(rec, 3, 0, got, key);
    TF_STREQ(got, want);
    run_row(rec, 2, 0, got, key);
    TF_STREQ(got, want);
    /* The serial order shows no reason. */
    a[0] = 'L';
    want_ser(want, "5", "", "", "", "Den");
    run_row(rec, 3, 3, got, key);
    TF_STREQ(got, want);
}

/* The works column: the word of each letter of ITL_WORKS, MT_WK_Q and
   the three after it in its order; a letter that is none of them as it
   is; blank for none. */
static void test_works(void)
{
    static const char letters[] = ITL_WORKS;
    kv rec[2];
    char want[ROWMAX];
    char got[ROWMAX];
    char key[LV_KEYSZ];
    char a[2];
    u16 i;

    rec[0].name = "ITEM_ID";
    rec[0].val = "6";
    rec[1].name = "WORKS";
    rec[1].val = a;
    a[1] = '\0';
    for (i = 0; i < sizeof letters - 1; i++) {
        a[0] = letters[i];
        want_def(want, "6", "", "", "", msg((u16)(MT_WK_Q + i)), "", "", "");
        run_row(rec, 2, 0, got, key);
        TF_STREQ(got, want);
    }
    TF_STREQ(msg(MT_WK_I), "intrm");
    a[0] = 'y';
    want_def(want, "6", "", "", "", "y", "", "", "");
    run_row(rec, 2, 0, got, key);
    TF_STREQ(got, want);
    a[0] = '\0';
    want_def(want, "6", "", "", "", "", "", "", "");
    run_row(rec, 2, 0, got, key);
    TF_STREQ(got, want);
}

static void test_serial_rows(void)
{
    static const kv ser[] = {
        { "ITEM_ID", "3" }, { "MFR", "Hercules" },
        { "MODEL", "Graphics Card" }, { "SERIALNO", "HGC-0042" },
        { "LOC_NAME", "Shelf A1" }
    };
    static const kv more[] = {
        { "ITEM_ID", "3" }, { "CATEGORY", "CARD" }, { "MFR", "Hercules" },
        { "MODEL", "Graphics Card" }, { "SERIALNO", "HGC-0042" },
        { "WORKS", "Y" }, { "QTY", "9" }, { "LOC_NAME", "Shelf A1" },
        { "AVAIL", "L" }
    };
    static const kv longs[] = {
        { "ITEM_ID", "15" }, { "MFR", "Manufacturer-of-very-long-names" },
        { "MODEL", "A-model-name-of-thirty-chars-xx" },
        { "SERIALNO", "a-serial-number-over-twenty-four-chars" },
        { "LOC_NAME", "The shelf by the door" }
    };
    static const kv noser[] = {
        { "ITEM_ID", "8" }, { "MFR", "Tandon" }, { "LOC_NAME", "Box B2" }
    };
    char want[ROWMAX];
    char got[ROWMAX];
    char key[LV_KEYSZ];

    want_ser(want, "3", "Hercules", "Graphics Card", "HGC-0042",
             "Shelf A1");
    run_row(ser, 5, 3, got, key);
    TF_STREQ(got, want);
    TF_STREQ(key, "3");
    /* Fields of the default list are not shown in this order. */
    run_row(more, 9, 3, got, key);
    TF_STREQ(got, want);
    TF_STREQ(key, "3");
    want_ser(want, "15", "Manufacturer-o~", "A-model-name-o~",
             "a-serial-number-over-twenty-four-chars", "The shelf by th~");
    run_row(longs, 5, 3, got, key);
    TF_STREQ(got, want);
    TF_CHECK(strlen(got) <= LV_TEXTLEN);
    want_ser(want, "8", "Tandon", "", "", "Box B2");
    run_row(noser, 3, 3, got, key);
    TF_STREQ(got, want);
    TF_STREQ(key, "8");
}

/* ---------------------------------------------------------------------
 * The template over the engine.
 * --------------------------------------------------------------------- */

/* The five items.  Their locations, makers and serial numbers sort the
   four orders differently:
     ID     1 2 3 4 5
     LOC    2 4 1 5 3
     MFR    4 5 3 1 2
     SERIAL 5 1 4 3 2 */
typedef struct {
    const char *cat, *mfr, *model, *ser, *works, *qty, *loc;
} itm;

static const itm items[NITEMS] = {
    { "SYS", "IBM", "5150", "0012345", "Y", "1", "C3" },
    { "DRV", "Tandon", "TM100-2", "TM-7", "N", "2", "A1" },
    { "CARD", "Hercules", "Graphics Card", "HGC-0042", "?", "1", "E5" },
    { "SYS", "Apple", "Apple II", "AP-001", "I", "1", "B2" },
    { "CARD", "Compaq", "Portable", "0000099", "Y", "4", "D4" }
};

static const u8 seq[NORD][NITEMS] = {
    { 1, 2, 3, 4, 5 },
    { 2, 4, 1, 5, 3 },
    { 4, 5, 3, 1, 2 },
    { 5, 1, 4, 3, 2 }
};

static const char *const locs[] = { "A1", "B2", "C3", "D4", "E5" };

static void add_data(void)
{
    char line[80];
    rq q;
    rq_res res;
    u16 i;

    for (i = 1; i < 5; i++) {
        sprintf(line, "LOC ADD LOC_NAME=%s LTYPE=BIN LOC_CODE=%s", locs[i],
                locs[i]);
        TF_EQ(tc_send(line), RQ_OK);
    }
    for (i = 0; i < NITEMS; i++) {
        rq_begin(&q, "ITEM ADD");
        rq_pair(&q, "CATEGORY", items[i].cat);
        rq_pair(&q, "MFR", items[i].mfr);
        rq_pair(&q, "MODEL", items[i].model);
        rq_pair(&q, "SERIALNO", items[i].ser);
        rq_pair(&q, "WORKS", items[i].works);
        rq_pair(&q, "QTY", items[i].qty);
        rq_pair(&q, "LOC_ID", items[i].loc);
        if (!TF_EQ(rq_send(tc_lk, &q, NULL, NULL, &res), RQ_OK))
            return;
    }
}

static lv_src g_src;            /* the template as the client copies it */
static pg g_pg;

static void fmt_itl(void *arg, const lv_rec *r, char *text, char *key)
{
    TF_CHECK(arg == g_src.arg);
    itl_row(r, g_pg.order, text, key);
}

static int g_tag;               /* what the pager passes to its callbacks */

static int wait_no(void *arg, int walk, u32 n)
{
    TF_CHECK(arg == (void *)&g_tag);
    TF_CHECK(walk == 0 || walk == 1);
    TF_CHECK(n < 1000);
    return 0;
}

static int fail_stay(void *arg, const rq_res *res)
{
    TF_CHECK(arg == (void *)&g_tag);
    TF_EQ(res->code, RQ_OK);
    return 1;
}

static void open_pg(u8 ps)
{
    memcpy(&g_src, &itl_src, sizeof g_src);
    g_src.fmt = fmt_itl;
    TF_EQ(pg_open(&g_pg, tc_lk, &g_src, ps, wait_no, fail_stay, &g_tag),
          PG_OK);
}

/* The model row of item n (from 1) in the layout of order o. */
/* The word of works letter w (ITL_WORKS, held once: Turbo C stores a
   repeated literal again). */
static const char *works_word(const char *w)
{
    static const char letters[] = ITL_WORKS;
    const char *p;

    p = strchr(letters, w[0]);
    return msg((u16)(MT_WK_Q + (p - letters)));
}

static void want_item(char *t, u8 n, u8 o)
{
    const itm *m;
    char id[4];

    m = &items[n - 1];
    sprintf(id, "%d", (int)n);
    if (o == 3)
        want_ser(t, id, m->mfr, m->model, m->ser, m->loc);
    else
        want_def(t, id, m->cat, m->mfr, m->model, works_word(m->works),
                 m->qty, m->loc, "");
}

/* The request a first page of order o is sent as: the tree order
   (LV_TREEORDER) as LOC=0 ALL=1, the others as ORDER=. */
static void want_req(char *t, u8 o, u8 ps)
{
    if (strcmp(itl_src.orders[o], LV_TREEORDER) == 0)
        strcpy(t, "ITEM LIST LOC=0 ALL=1");
    else
        sprintf(t, "ITEM LIST ORDER=%s", itl_src.orders[o]);
    if (itl_src.fields[o] != NULL) {
        strcat(t, " FIELDS=");
        strcat(t, itl_src.fields[o]);
    }
    sprintf(t + strlen(t), " PAGE=%u", (unsigned)ps);
}

/* Rows held by g_pg are the items of seq[o], at most the first n. */
static void check_rows(u8 o, u16 n)
{
    char want[ROWMAX];
    char got[ROWMAX];
    char key[LV_KEYSZ];
    char idk[4];
    u16 i;

    TF_EQ(pg_rows(&g_pg), n);
    for (i = 0; i < n && i < pg_rows(&g_pg); i++) {
        want_item(want, seq[o][i], o);
        TF_EQ(pg_text(&g_pg, i, got), 0);
        TF_EQ(strlen(got), LV_ROWTEXT);
        rtrim(got);
        TF_STREQ(got, want);
    }
    TF_CHECK(pg_selected(&g_pg, key));
    sprintf(idk, "%d", (int)seq[o][g_pg.sel]);
    TF_STREQ(key, idk);
}

static void test_orders(void)
{
    char want[ROWMAX];
    u8 o;
    u8 k;

    open_pg(NITEMS);
    TF_STREQ(pg_order_name(&g_pg), "ID");
    for (k = 0; k <= NORD; k++) {
        o = (u8)(k % NORD);
        tl_clear();
        TF_EQ(pg_move(&g_pg, k == 0 ? PG_HOME : PG_ORDER), RQ_OK);
        TF_EQ(g_pg.order, o);
        TF_STREQ(pg_order_name(&g_pg), itl_src.orders[o]);
        want_req(want, o, NITEMS);
        TF_EQ(tl_count(), 1);
        TF_STREQ(tl_req(0), want);
        check_rows(o, NITEMS);
    }
    pg_close(&g_pg);
}

/* Order o with a window of 2 rows, the End walk. */
static void test_walk(u8 o)
{
    char want[ROWMAX];
    char got[ROWMAX];
    char idk[4];
    char key[LV_KEYSZ];
    u16 i;
    u16 walks;
    u16 last;

    walks = 0;
    last = 0;
    open_pg(2);
    TF_CHECK(pg_set_order(&g_pg, itl_src.orders[o]));
    TF_EQ(pg_move(&g_pg, PG_HOME), RQ_OK);
    tl_clear();
    TF_EQ(pg_move(&g_pg, PG_END), RQ_OK);
    for (i = 0; i < tl_count(); i++) {
        const char *q;

        q = tl_req(i);
        TF_CHECK(strstr(q, "ORDER=") != NULL);
        if (strstr(q, "FIELDS=ITEM_ID PAGE") != NULL) {
            walks++;
            TF_CHECK(last == 0);
        } else {
            last++;
            if (itl_src.fields[o] != NULL) {
                strcpy(want, "FIELDS=");
                strcat(want, itl_src.fields[o]);
                TF_CHECK(strstr(q, want) != NULL);
            } else {
                TF_CHECK(strstr(q, "FIELDS=") == NULL);
            }
        }
    }
    TF_CHECK(walks >= 1);
    TF_CHECK(last >= 1);
    /* The window ends on the last row, which is selected. */
    TF_EQ(pg_selected(&g_pg, key), 1);
    sprintf(idk, "%d", (int)seq[o][NITEMS - 1]);
    TF_STREQ(key, idk);
    want_item(want, seq[o][NITEMS - 1], o);
    TF_EQ(pg_text(&g_pg, g_pg.sel, got), 0);
    rtrim(got);
    TF_STREQ(got, want);
    pg_close(&g_pg);
}

/* ITEM FIND's text, added as the client's args function does. */
static void find_args(void *arg, rq *q)
{
    TF_CHECK(arg == g_src.arg);
    rq_word(q, "IBM");
}

/* The template made into the result of another item command: no
   ORDER=, the default fields. */
static void test_other_command(void)
{
    char want[ROWMAX];
    char got[ROWMAX];

    memcpy(&g_src, &itl_src, sizeof g_src);
    g_src.cmd = "ITEM FIND";
    g_src.norders = 0;
    g_src.args = find_args;
    g_src.fmt = fmt_itl;
    TF_EQ(pg_open(&g_pg, tc_lk, &g_src, 4, wait_no, fail_stay, &g_tag),
          PG_OK);
    tl_clear();
    TF_EQ(pg_move(&g_pg, PG_HOME), RQ_OK);
    TF_EQ(tl_count(), 1);
    TF_STREQ(tl_req(0), "ITEM FIND IBM PAGE=4");
    TF_EQ(pg_rows(&g_pg), 1);
    want_item(want, 1, 0);
    TF_EQ(pg_text(&g_pg, 0, got), 0);
    rtrim(got);
    TF_STREQ(got, want);
    pg_close(&g_pg);
}

int main(int argc, char **argv)
{
    link_clock clk;
    char why[LINK_WHYSZ];
    char mdir[PATHMAX];
    int rc;

    tf_begin("T_ITEMLS");
    tf_progdir(mdir, PATHMAX, argv[0]);
    if (argc < 2 || strlen(argv[1]) >= PATHMAX) {
        printf("usage: T_ITEMLS dir [msgdir]\n");
        return 2;
    }
    if (argc > 2 && strlen(argv[2]) < PATHMAX)
        strcpy(mdir, argv[2]);
    cmem_init();
    if (!TF_EQ(msg_load(mdir, MSG_FILE), 0)) {
        printf("T_ITEMLS: %s\n", msg_why());
        return tf_done();
    }
    test_src();
    test_header();
    test_default_rows();
    test_field_order();
    test_missing();
    test_cut();
    test_why();
    test_works();
    test_serial_rows();
    strcpy(g_ddir, argv[1]);
    te_remove(g_ddir, NULL);
    tc_clock(&clk);
    rc = link_open_local(&tc_lk, g_ddir, &clk, why, sizeof why);
    if (!TF_EQ(rc, 0)) {
        printf("T_ITEMLS: %s\n", why);
        return tf_done();
    }
    if (tc_admin()) {
        add_data();
        test_orders();
        test_walk(0);
        test_walk(3);
        test_other_command();
    }
    TF_EQ(link_close(tc_lk), 0);
    te_remove(g_ddir, NULL);
    msg_free();
    return tf_done();
}
