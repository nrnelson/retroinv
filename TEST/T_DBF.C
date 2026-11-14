/*
 * T_DBF.C - unit tests for the dBASE III table functions (DBF.C).
 *
 * usage: T_DBF [dir]
 *
 * Test tables are created in the scratch directory dir (default: the
 * current directory), which is cleared of them before and after the
 * run.  The file bytes are inspected through the cf_ functions, so the
 * same checks run on the host and under DOS.  Prints each failure and a
 * summary line; exit status 0 when every check passes, else 1 (2 when
 * dir is too long).
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "DBFINT.H"
#include "TESTFW.H"
#include "T_FILES.H"

/* Every file the tests make, removed before and after them. */
static const char *const k_scratch[] = {
    "ITEMS.DBF", "TYPES.DBF", "REC.DBF", "BAD.DBF", "OK.DBF",
    "CORRUPT.DBF", "YEARS.DBF", "MEMOVER.DBF", "MUTATE.DBF", "MANY.DBF",
    "ORD.DBF", "MAP.DBF", "CFO.DBF", "TRIM.DBF", "NUMLIM.DBF", NULL
};

static u8 g_file[1024];         /* a whole small test file            */

/* Read the scratch file name into g_file; returns its size, or 0xFFFF
   on error. */
static u16 slurp(const char *name)
{
    u16 got;

    if (tfs_read_all(tfs_path(name, ""), g_file, (u16)sizeof g_file, &got)
        != 0)
        return 0xFFFFu;
    return got;
}

/* Write the first n bytes of g_file as the scratch file name. */
static int spit(const char *name, u16 n)
{
    return tfs_write_all(tfs_path(name, ""), g_file, n);
}

static void set_field(dbf_field *f, const char *name, char type, u8 len,
                      u8 dec)
{
    memset(f, 0, sizeof *f);
    strcpy(f->name, name);
    f->type = type;
    f->len = len;
    f->dec = dec;
}

/* ---------------------------------------------------------------------
 * The ITEMS.DBF layout, kept here independently of any engine code.
 * --------------------------------------------------------------------- */
struct fdef {
    const char *name;
    char type;
    u8 len;
    u8 dec;
};

static const struct fdef items_defs[] = {
    {"ITEM_ID", 'N', 8, 0},
    {"CATEGORY", 'C', 4, 0},
    {"ITEMTYPE", 'C', 20, 0},
    {"MFR", 'C', 30, 0},
    {"MODEL", 'C', 30, 0},
    {"SERIALNO", 'C', 30, 0},
    {"DESCR", 'C', 60, 0},
    {"COND", 'C', 3, 0},
    {"QTY", 'N', 5, 0},
    {"ON_LOAN", 'N', 5, 0},
    {"LOC_CODE", 'C', 6, 0},
    {"PARENT_ID", 'N', 8, 0},
    {"ACQ_DATE", 'D', 8, 0},
    {"ACQ_SRC", 'C', 30, 0},
    {"ACQ_PRICE", 'N', 9, 2},
    {"CUR_VALUE", 'N', 9, 2},
    {"TAGS", 'C', 60, 0},
    {"STATUS", 'C', 1, 0},
    {"CREATED", 'D', 8, 0},
    {"UPDATED", 'D', 8, 0},
    {"NOTES", 'M', 10, 0}
};
#define ITEMS_N ((u16)(sizeof items_defs / sizeof items_defs[0]))

static dbf_field g_fields[DBF_MAXFIELDS];

static void items_fields(void)
{
    u16 i;

    for (i = 0; i < ITEMS_N; i++)
        set_field(&g_fields[i], items_defs[i].name, items_defs[i].type,
                  items_defs[i].len, items_defs[i].dec);
}

static void test_golden_header(void)
{
    u8 want[32];
    u16 size;
    u16 i;
    u16 off;
    u16 hdrlen;
    u16 reclen;
    u16 year;
    u8 month;
    u8 day;
    dbf *t;

    items_fields();
    dbf_set_today(2026, 10, 8);
    TF_EQ(dbf_create(tfs_path("ITEMS.DBF", ""), g_fields, ITEMS_N), DBF_OK);

    /* 21 fields: header 32 + 21 * 32 + 1 = 705, record 1 + 352 = 353. */
    hdrlen = 705;
    reclen = 353;
    size = slurp("ITEMS.DBF");
    TF_EQ(size, hdrlen + 1);
    if (size != hdrlen + 1)
        return;

    memset(want, 0, sizeof want);
    want[0] = 0x83;
    want[1] = 26;
    want[2] = 10;
    want[3] = 8;
    want[8] = (u8)(hdrlen & 0xFF);
    want[9] = (u8)(hdrlen >> 8);
    want[10] = (u8)(reclen & 0xFF);
    want[11] = (u8)(reclen >> 8);
    TF_CHECK(memcmp(g_file, want, 32) == 0);

    off = 1;
    for (i = 0; i < ITEMS_N; i++) {
        memset(want, 0, sizeof want);
        memcpy(want, items_defs[i].name, strlen(items_defs[i].name));
        want[11] = (u8)items_defs[i].type;
        want[16] = items_defs[i].len;
        want[17] = items_defs[i].dec;
        if (!TF_CHECK(memcmp(g_file + 32 + 32 * i, want, 32) == 0))
            printf("  descriptor %u (%s) differs\n", (unsigned)i,
                   items_defs[i].name);
        TF_EQ(g_fields[i].offset, off);
        off = (u16)(off + items_defs[i].len);
    }
    TF_EQ(off, reclen);
    TF_EQ(g_file[hdrlen - 1], 0x0D);
    TF_EQ(g_file[hdrlen], 0x1A);

    TF_EQ(dbf_open(&t, tfs_path("ITEMS.DBF", ""), DBF_RDONLY), DBF_OK);
    if (t != NULL) {
        TF_EQ(dbf_nfields(t), ITEMS_N);
        TF_EQ(dbf_reclen(t), reclen);
        TF_EQ(dbf_reccount(t), 0);
        TF_CHECK(dbf_has_memo(t));
        TF_EQ(dbf_version(t), 0x83);
        TF_EQ(dbf_hdrlen(t), hdrlen);
        dbf_lastupdate(t, &year, &month, &day);
        TF_EQ(year, 2026);
        TF_EQ(month, 10);
        TF_EQ(day, 8);
        TF_EQ(dbf_field_at(t, 14)->dec, 2);
        TF_EQ(dbf_field_at(t, 20)->offset, 343);
        TF_CHECK(dbf_field_at(t, ITEMS_N) == NULL);

        TF_EQ(dbf_field_index(t, "ITEM_ID"), 0);
        TF_EQ(dbf_field_index(t, "item_id"), 0);
        TF_EQ(dbf_field_index(t, "Cur_Value"), 15);
        TF_EQ(dbf_field_index(t, "notes"), 20);
        TF_EQ(dbf_field_index(t, "NOTE"), -1);
        TF_EQ(dbf_field_index(t, "NOTESX"), -1);
        TF_EQ(dbf_field_index(t, ""), -1);
        TF_EQ(dbf_field_index(t, "PARENT_ID_LONGER"), -1);
        TF_EQ(dbf_close(t), DBF_OK);
    }
    /* A read-only open and close leaves the file untouched. */
    TF_EQ(slurp("ITEMS.DBF"), hdrlen + 1);
    TF_EQ(g_file[1], 26);
    TF_EQ(cf_remove(tfs_path("ITEMS.DBF", "")), 0);
}

/* ---------------------------------------------------------------------
 * Field types and boundaries.
 * --------------------------------------------------------------------- */
#define F_BIG   0
#define F_N19   1
#define F_PRICE 2
#define F_Q     3
#define F_DT    4
#define F_OK    5
#define F_MEMO  6
#define F_C1    7

static void raw_put(dbf *t, u16 f, const char *text)
{
    const dbf_field *fd;
    u8 *p;

    fd = dbf_field_at(t, f);
    p = dbf_recbuf(t) + fd->offset;
    memset(p, ' ', fd->len);
    memcpy(p, text, strlen(text));
}

static int raw_eq(dbf *t, u16 f, const char *text)
{
    const dbf_field *fd;

    fd = dbf_field_at(t, f);
    return strlen(text) == fd->len
           && memcmp(dbf_recbuf(t) + fd->offset, text, fd->len) == 0;
}

static i32 g_num;
static int g_blank;

static int num_of(dbf *t, u16 f, const char *text)
{
    raw_put(t, f, text);
    g_num = -1;
    g_blank = -1;
    return dbf_get_num(t, f, &g_num, &g_blank);
}

static void test_types(void)
{
    static char big[300];
    char out[300];
    char small[5];
    u32 block;
    dbf *t;
    u16 i;

    set_field(&g_fields[F_BIG], "BIG", 'C', DBF_MAXCHAR, 0);
    set_field(&g_fields[F_N19], "N19", 'N', 19, 0);
    set_field(&g_fields[F_PRICE], "PRICE", 'N', 9, 2);
    set_field(&g_fields[F_Q], "Q", 'N', 5, 0);
    set_field(&g_fields[F_DT], "DT", 'D', 8, 0);
    set_field(&g_fields[F_OK], "OK", 'L', 1, 0);
    set_field(&g_fields[F_MEMO], "MEMO", 'M', 10, 0);
    set_field(&g_fields[F_C1], "C1", 'C', 1, 0);
    TF_EQ(dbf_create(tfs_path("TYPES.DBF", ""), g_fields, 8), DBF_OK);
    TF_EQ(dbf_open(&t, tfs_path("TYPES.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(dbf_reclen(t), 308);
    dbf_blank(t);
    TF_EQ(dbf_recno(t), 0);
    TF_CHECK(!dbf_is_deleted(t));

    /* C: full width, one over, truncating reads. */
    for (i = 0; i < DBF_MAXCHAR; i++)
        big[i] = (char)('A' + i % 26);
    big[DBF_MAXCHAR] = '\0';
    TF_EQ(dbf_put_str(t, F_BIG, big), DBF_OK);
    TF_EQ(dbf_get_str(t, F_BIG, out, sizeof out), DBF_OK);
    TF_STREQ(out, big);
    TF_EQ(dbf_get_str(t, F_BIG, out, DBF_MAXCHAR + 1), DBF_OK);
    TF_EQ(dbf_get_str(t, F_BIG, out, DBF_MAXCHAR), DBF_ERANGE);
    TF_EQ(strlen(out), DBF_MAXCHAR - 1);
    big[DBF_MAXCHAR] = 'X';
    big[DBF_MAXCHAR + 1] = '\0';
    TF_EQ(dbf_put_str(t, F_BIG, big), DBF_ERANGE);
    big[DBF_MAXCHAR] = '\0';
    TF_EQ(dbf_get_str(t, F_BIG, out, sizeof out), DBF_OK);
    TF_STREQ(out, big);
    TF_EQ(dbf_put_str(t, F_BIG, "  hi  "), DBF_OK);
    TF_EQ(dbf_get_str(t, F_BIG, out, sizeof out), DBF_OK);
    TF_STREQ(out, "  hi");
    TF_EQ(dbf_get_str(t, F_BIG, small, sizeof small), DBF_OK);
    TF_EQ(dbf_put_str(t, F_BIG, "ABCDEFG"), DBF_OK);
    TF_EQ(dbf_get_str(t, F_BIG, small, sizeof small), DBF_ERANGE);
    TF_STREQ(small, "ABCD");
    TF_EQ(dbf_get_str(t, F_BIG, small, 0), DBF_ERANGE);
    TF_EQ(dbf_put_str(t, F_C1, "Z"), DBF_OK);
    TF_EQ(dbf_put_str(t, F_C1, "ZZ"), DBF_ERANGE);
    TF_CHECK(raw_eq(t, F_C1, "Z"));
    TF_EQ(dbf_put_str(t, F_C1, ""), DBF_OK);
    TF_CHECK(raw_eq(t, F_C1, " "));
    TF_EQ(dbf_put_str(t, 8, "x"), DBF_ERANGE);
    TF_EQ(dbf_get_str(t, 8, out, sizeof out), DBF_ERANGE);
    TF_EQ(dbf_put_blank(t, 8), DBF_ERANGE);

    /* N(19,0): the i32 limits. */
    TF_EQ(dbf_put_num(t, F_N19, 2147483647L), DBF_OK);
    TF_CHECK(raw_eq(t, F_N19, "         2147483647"));
    TF_EQ(dbf_get_str(t, F_N19, out, sizeof out), DBF_OK);
    TF_STREQ(out, "2147483647");
    TF_EQ(dbf_get_num(t, F_N19, &g_num, &g_blank), DBF_OK);
    TF_CHECK(g_num == 2147483647L);
    TF_EQ(g_blank, 0);
    TF_EQ(dbf_put_num(t, F_N19, -2147483647L - 1), DBF_OK);
    TF_CHECK(raw_eq(t, F_N19, "        -2147483648"));
    TF_EQ(dbf_get_num(t, F_N19, &g_num, &g_blank), DBF_OK);
    TF_CHECK(g_num == -2147483647L - 1);
    TF_EQ(dbf_put_num(t, F_N19, 0), DBF_OK);
    TF_CHECK(raw_eq(t, F_N19, "                  0"));
    TF_EQ(num_of(t, F_N19, "9999999999999999999"), DBF_ERANGE);
    TF_EQ(num_of(t, F_N19, "         2147483648"), DBF_ERANGE);
    TF_EQ(num_of(t, F_N19, "        -2147483649"), DBF_ERANGE);
    TF_EQ(num_of(t, F_N19, "        -2147483648"), DBF_OK);
    TF_CHECK(g_num == -2147483647L - 1);
    TF_EQ(num_of(t, F_N19, "        00000000042"), DBF_OK);
    TF_EQ(g_num, 42);
    TF_EQ(num_of(t, F_N19, "  00000002147483647"), DBF_OK);
    TF_CHECK(g_num == 2147483647L);
    TF_EQ(num_of(t, F_N19, "  00000002147483648"), DBF_ERANGE);
    TF_EQ(num_of(t, F_N19, "  -0000002147483648"), DBF_OK);
    TF_CHECK(g_num == -2147483647L - 1);
    TF_EQ(num_of(t, F_N19, "  -0000002147483649"), DBF_ERANGE);
    TF_EQ(num_of(t, F_N19, "*******************"), DBF_ERANGE);
    TF_EQ(num_of(t, F_N19, "                 -0"), DBF_OK);
    TF_EQ(g_num, 0);
    TF_EQ(num_of(t, F_N19, "                1.0"), DBF_EARG);

    /* N(9,2): scaling, negatives, decimals, width. */
    TF_EQ(dbf_put_num(t, F_PRICE, -12345L), DBF_OK);
    TF_CHECK(raw_eq(t, F_PRICE, "  -123.45"));
    TF_EQ(dbf_get_num(t, F_PRICE, &g_num, &g_blank), DBF_OK);
    TF_EQ(g_num, -12345L);
    TF_EQ(dbf_get_str(t, F_PRICE, out, sizeof out), DBF_OK);
    TF_STREQ(out, "-123.45");
    TF_EQ(dbf_put_num(t, F_PRICE, 5), DBF_OK);
    TF_CHECK(raw_eq(t, F_PRICE, "     0.05"));
    TF_EQ(dbf_put_num(t, F_PRICE, -5), DBF_OK);
    TF_CHECK(raw_eq(t, F_PRICE, "    -0.05"));
    TF_EQ(dbf_get_num(t, F_PRICE, &g_num, &g_blank), DBF_OK);
    TF_EQ(g_num, -5);
    TF_EQ(dbf_put_num(t, F_PRICE, 0), DBF_OK);
    TF_CHECK(raw_eq(t, F_PRICE, "     0.00"));
    TF_EQ(dbf_put_num(t, F_PRICE, 99999999L), DBF_OK);
    TF_CHECK(raw_eq(t, F_PRICE, "999999.99"));
    TF_EQ(dbf_put_num(t, F_PRICE, 100000000L), DBF_ERANGE);
    TF_CHECK(raw_eq(t, F_PRICE, "999999.99"));
    TF_EQ(dbf_put_num(t, F_PRICE, -9999999L), DBF_OK);
    TF_CHECK(raw_eq(t, F_PRICE, "-99999.99"));
    TF_EQ(dbf_put_num(t, F_PRICE, -10000000L), DBF_ERANGE);
    TF_EQ(dbf_put_num(t, F_PRICE, -2147483647L - 1), DBF_ERANGE);
    TF_EQ(num_of(t, F_PRICE, "     12.5"), DBF_OK);
    TF_EQ(g_num, 1250);
    TF_EQ(num_of(t, F_PRICE, "       12"), DBF_OK);
    TF_EQ(g_num, 1200);
    TF_EQ(num_of(t, F_PRICE, "      12."), DBF_OK);
    TF_EQ(g_num, 1200);
    TF_EQ(num_of(t, F_PRICE, "      .50"), DBF_OK);
    TF_EQ(g_num, 50);
    TF_EQ(num_of(t, F_PRICE, "     -.50"), DBF_OK);
    TF_EQ(g_num, -50);
    TF_EQ(num_of(t, F_PRICE, "    1.234"), DBF_EARG);
    TF_EQ(num_of(t, F_PRICE, "*********"), DBF_ERANGE);
    TF_EQ(num_of(t, F_PRICE, "         "), DBF_OK);
    TF_EQ(g_num, 0);
    TF_EQ(g_blank, 1);
    TF_EQ(num_of(t, F_PRICE, "      abc"), DBF_EARG);
    TF_EQ(num_of(t, F_PRICE, "        -"), DBF_EARG);
    TF_EQ(num_of(t, F_PRICE, "        ."), DBF_EARG);
    TF_EQ(num_of(t, F_PRICE, "      1 2"), DBF_EARG);
    TF_EQ(num_of(t, F_PRICE, "    1.2.3"), DBF_EARG);
    TF_EQ(num_of(t, F_PRICE, "     +1.0"), DBF_EARG);
    TF_EQ(num_of(t, F_PRICE, "1.00     "), DBF_OK);
    TF_EQ(g_num, 100);
    TF_EQ(dbf_put_blank(t, F_PRICE), DBF_OK);
    TF_CHECK(raw_eq(t, F_PRICE, "         "));
    TF_EQ(dbf_get_str(t, F_PRICE, out, sizeof out), DBF_OK);
    TF_STREQ(out, "");

    /* N(5,0). */
    TF_EQ(num_of(t, F_Q, "   1."), DBF_EARG);
    TF_EQ(num_of(t, F_Q, "   .5"), DBF_EARG);
    TF_EQ(num_of(t, F_Q, " 12 3"), DBF_EARG);
    TF_EQ(num_of(t, F_Q, "  -12"), DBF_OK);
    TF_EQ(g_num, -12);
    TF_EQ(dbf_put_num(t, F_Q, 99999L), DBF_OK);
    TF_CHECK(raw_eq(t, F_Q, "99999"));
    TF_EQ(dbf_put_num(t, F_Q, 100000L), DBF_ERANGE);
    TF_EQ(dbf_put_num(t, F_Q, -9999L), DBF_OK);
    TF_CHECK(raw_eq(t, F_Q, "-9999"));
    TF_EQ(dbf_put_num(t, F_Q, -10000L), DBF_ERANGE);
    TF_EQ(dbf_put_num(t, F_Q, 7), DBF_OK);
    TF_CHECK(raw_eq(t, F_Q, "    7"));
    TF_EQ(dbf_put_str(t, F_Q, "7"), DBF_EARG);
    TF_EQ(dbf_put_num(t, F_BIG, 7), DBF_EARG);
    TF_EQ(dbf_get_num(t, F_BIG, &g_num, &g_blank), DBF_EARG);

    /* D: validity and leap years. */
    TF_EQ(dbf_put_str(t, F_DT, "20240229"), DBF_OK);
    TF_CHECK(raw_eq(t, F_DT, "20240229"));
    TF_EQ(dbf_put_str(t, F_DT, "20000229"), DBF_OK);
    TF_EQ(dbf_put_str(t, F_DT, "20230229"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_DT, "19000229"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_DT, "21000229"), DBF_EARG);
    TF_CHECK(raw_eq(t, F_DT, "20000229"));
    TF_EQ(dbf_put_str(t, F_DT, "20241231"), DBF_OK);
    TF_EQ(dbf_put_str(t, F_DT, "20241301"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_DT, "20240001"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_DT, "20240100"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_DT, "20240431"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_DT, "00000101"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_DT, "2024011"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_DT, "2024010a"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_DT, "202401011"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_DT, "2024-1-1"), DBF_EARG);
    TF_CHECK(raw_eq(t, F_DT, "20241231"));
    TF_EQ(dbf_get_str(t, F_DT, out, sizeof out), DBF_OK);
    TF_STREQ(out, "20241231");
    TF_EQ(dbf_put_str(t, F_DT, ""), DBF_OK);
    TF_CHECK(raw_eq(t, F_DT, "        "));

    /* L: stored as T or F; '?' is read but not written. */
    TF_EQ(dbf_put_str(t, F_OK, "t"), DBF_OK);
    TF_CHECK(raw_eq(t, F_OK, "T"));
    TF_EQ(dbf_put_str(t, F_OK, "N"), DBF_OK);
    TF_CHECK(raw_eq(t, F_OK, "F"));
    TF_EQ(dbf_put_str(t, F_OK, "y"), DBF_OK);
    TF_CHECK(raw_eq(t, F_OK, "T"));
    TF_EQ(dbf_put_str(t, F_OK, "n"), DBF_OK);
    TF_CHECK(raw_eq(t, F_OK, "F"));
    TF_EQ(dbf_put_str(t, F_OK, "Y"), DBF_OK);
    TF_EQ(dbf_put_str(t, F_OK, "f"), DBF_OK);
    TF_CHECK(raw_eq(t, F_OK, "F"));
    TF_EQ(dbf_put_str(t, F_OK, "?"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_OK, "x"), DBF_EARG);
    TF_EQ(dbf_put_str(t, F_OK, "TT"), DBF_EARG);
    TF_CHECK(raw_eq(t, F_OK, "F"));
    raw_put(t, F_OK, "?");
    TF_EQ(dbf_get_str(t, F_OK, out, sizeof out), DBF_OK);
    TF_STREQ(out, "?");
    TF_EQ(dbf_put_str(t, F_OK, ""), DBF_OK);
    TF_CHECK(raw_eq(t, F_OK, " "));

    /* M: block numbers. */
    TF_EQ(dbf_put_str(t, F_MEMO, "1"), DBF_EARG);
    TF_EQ(dbf_get_memo(t, F_MEMO, &block), DBF_OK);
    TF_CHECK(block == 0);
    TF_EQ(dbf_put_memo(t, F_MEMO, 7), DBF_OK);
    TF_CHECK(raw_eq(t, F_MEMO, "         7"));
    TF_EQ(dbf_get_memo(t, F_MEMO, &block), DBF_OK);
    TF_CHECK(block == 7);
    TF_EQ(dbf_put_memo(t, F_MEMO, 4294967295UL), DBF_OK);
    TF_CHECK(raw_eq(t, F_MEMO, "4294967295"));
    TF_EQ(dbf_get_memo(t, F_MEMO, &block), DBF_OK);
    TF_CHECK(block == 4294967295UL);
    TF_EQ(dbf_put_memo(t, F_MEMO, 0), DBF_OK);
    TF_CHECK(raw_eq(t, F_MEMO, "          "));
    raw_put(t, F_MEMO, "4294967296");
    TF_EQ(dbf_get_memo(t, F_MEMO, &block), DBF_ERANGE);
    raw_put(t, F_MEMO, "      12x ");
    TF_EQ(dbf_get_memo(t, F_MEMO, &block), DBF_EARG);
    TF_EQ(dbf_get_memo(t, F_BIG, &block), DBF_EARG);
    TF_EQ(dbf_put_memo(t, F_BIG, 1), DBF_EARG);

    /* A stored record keeps every value. */
    dbf_blank(t);
    TF_EQ(dbf_put_str(t, F_BIG, big), DBF_OK);
    TF_EQ(dbf_put_num(t, F_N19, -2147483647L - 1), DBF_OK);
    TF_EQ(dbf_put_num(t, F_PRICE, -12345L), DBF_OK);
    TF_EQ(dbf_put_str(t, F_DT, "20240229"), DBF_OK);
    TF_EQ(dbf_put_str(t, F_OK, "t"), DBF_OK);
    TF_EQ(dbf_put_memo(t, F_MEMO, 12), DBF_OK);
    TF_EQ(dbf_append(t), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);

    TF_EQ(dbf_open(&t, tfs_path("TYPES.DBF", ""), DBF_RDONLY), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(dbf_reccount(t), 1);
    TF_EQ(dbf_read(t, 1), DBF_OK);
    TF_EQ(dbf_get_str(t, F_BIG, out, sizeof out), DBF_OK);
    TF_STREQ(out, big);
    TF_EQ(dbf_get_num(t, F_N19, &g_num, &g_blank), DBF_OK);
    TF_CHECK(g_num == -2147483647L - 1);
    TF_EQ(dbf_get_num(t, F_PRICE, &g_num, &g_blank), DBF_OK);
    TF_EQ(g_num, -12345L);
    TF_EQ(dbf_get_num(t, F_Q, &g_num, &g_blank), DBF_OK);
    TF_EQ(g_blank, 1);
    TF_EQ(dbf_get_str(t, F_DT, out, sizeof out), DBF_OK);
    TF_STREQ(out, "20240229");
    TF_EQ(dbf_get_str(t, F_OK, out, sizeof out), DBF_OK);
    TF_STREQ(out, "T");
    TF_EQ(dbf_get_memo(t, F_MEMO, &block), DBF_OK);
    TF_CHECK(block == 12);
    TF_EQ(dbf_close(t), DBF_OK);
    TF_EQ(cf_remove(tfs_path("TYPES.DBF", "")), 0);
}

/* N(19,2): the i32 limits reached through decimals, written or implied. */
static void test_num_limits(void)
{
    dbf *t;

    set_field(&g_fields[0], "N2", 'N', 19, 2);
    TF_EQ(dbf_create(tfs_path("NUMLIM.DBF", ""), g_fields, 1), DBF_OK);
    if (!TF_EQ(dbf_open(&t, tfs_path("NUMLIM.DBF", ""), DBF_RDWR), DBF_OK))
        return;
    TF_EQ(num_of(t, 0, "        21474836.47"), DBF_OK);
    TF_CHECK(g_num == 2147483647L);
    TF_EQ(num_of(t, 0, "        21474836.48"), DBF_ERANGE);
    TF_EQ(num_of(t, 0, "       -21474836.48"), DBF_OK);
    TF_CHECK(g_num == -2147483647L - 1);
    TF_EQ(num_of(t, 0, "       -21474836.49"), DBF_ERANGE);
    TF_EQ(num_of(t, 0, "  00000021474836.47"), DBF_OK);
    TF_CHECK(g_num == 2147483647L);
    TF_EQ(num_of(t, 0, " -00000021474836.48"), DBF_OK);
    TF_CHECK(g_num == -2147483647L - 1);
    TF_EQ(num_of(t, 0, "         21474836.4"), DBF_OK);
    TF_CHECK(g_num == 2147483640L);
    TF_EQ(num_of(t, 0, "        -21474836.4"), DBF_OK);
    TF_CHECK(g_num == -2147483640L);
    TF_EQ(num_of(t, 0, "         21474836.5"), DBF_ERANGE);
    TF_EQ(num_of(t, 0, "           21474836"), DBF_OK);
    TF_CHECK(g_num == 2147483600L);
    TF_EQ(num_of(t, 0, "           21474837"), DBF_ERANGE);
    TF_EQ(num_of(t, 0, "          -21474837"), DBF_ERANGE);
    TF_EQ(num_of(t, 0, "          214748364"), DBF_ERANGE);
    TF_EQ(dbf_close(t), DBF_OK);
    TF_EQ(cf_remove(tfs_path("NUMLIM.DBF", "")), 0);
}

/* ---------------------------------------------------------------------
 * Record life cycle, 0x1A placement, reopen, read-only.
 * --------------------------------------------------------------------- */
static void two_fields(void)
{
    set_field(&g_fields[0], "ID", 'N', 8, 0);
    set_field(&g_fields[1], "NAME", 'C', 20, 0);
}

static int append_rec(dbf *t, i32 id, const char *name)
{
    dbf_blank(t);
    if (dbf_put_num(t, 0, id) != DBF_OK || dbf_put_str(t, 1, name) != DBF_OK)
        return DBF_EARG;
    return dbf_append(t);
}

static void test_records(void)
{
    char out[32];
    dbf *t;
    dbf *t2;
    u16 size;
    u16 hdr;
    u16 year;
    u8 month;
    u8 day;

    two_fields();
    dbf_set_today(2026, 10, 8);
    TF_EQ(dbf_create(tfs_path("REC.DBF", ""), g_fields, 2), DBF_OK);
    hdr = 32 + 2 * 32 + 1;
    TF_CHECK(tfs_size(tfs_path("REC.DBF", "")) == (u32)hdr + 1);
    TF_EQ(slurp("REC.DBF"), (u16)(hdr + 1));
    TF_EQ(g_file[0], 0x03);
    TF_EQ(g_file[(u16)hdr], 0x1A);

    TF_EQ(dbf_open(&t, tfs_path("REC.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(dbf_reclen(t), 29);
    TF_CHECK(!dbf_has_memo(t));
    TF_EQ(dbf_write(t), DBF_ERANGE);
    TF_EQ(dbf_read(t, 1), DBF_ERANGE);
    TF_EQ(append_rec(t, 1, "ONE"), DBF_OK);
    TF_EQ(dbf_recno(t), 1);
    TF_EQ(append_rec(t, 2, "TWO"), DBF_OK);
    TF_EQ(append_rec(t, 3, "THREE"), DBF_OK);
    TF_EQ(dbf_recno(t), 3);
    TF_EQ(dbf_reccount(t), 3);

    /* Until the flush the header on disk still counts no records.  A
       second handle sees the size from the directory entry, which DOS
       updates only when the file is committed, so the records are
       checked after the flush. */
    TF_CHECK(slurp("REC.DBF") >= hdr);
    TF_EQ(g_file[4], 0);

    dbf_set_today(2027, 1, 2);
    TF_EQ(dbf_flush(t), DBF_OK);
    TF_CHECK(tfs_size(tfs_path("REC.DBF", "")) == (u32)hdr + 3 * 29 + 1);
    size = slurp("REC.DBF");
    TF_EQ(size, (u16)(hdr + 3 * 29 + 1));
    TF_EQ(g_file[1], 27);
    TF_EQ(g_file[2], 1);
    TF_EQ(g_file[3], 2);
    TF_EQ(g_file[4], 3);
    TF_EQ(g_file[5], 0);
    TF_EQ(g_file[size - 1], 0x1A);
    TF_CHECK(memcmp(g_file + hdr + 29, "        2TWO ", 13) == 0);
    TF_EQ(dbf_version(t), 0x03);
    TF_EQ(dbf_hdrlen(t), hdr);
    dbf_lastupdate(t, &year, &month, &day);
    TF_EQ(year, 2027);
    TF_EQ(month, 1);
    TF_EQ(day, 2);

    TF_EQ(dbf_read(t, 0), DBF_ERANGE);
    TF_EQ(dbf_read(t, 4), DBF_ERANGE);
    TF_EQ(dbf_read(t, 2), DBF_OK);
    TF_EQ(dbf_recno(t), 2);
    TF_EQ(dbf_get_str(t, 1, out, sizeof out), DBF_OK);
    TF_STREQ(out, "TWO");
    TF_EQ(dbf_put_str(t, 1, "DEUX"), DBF_OK);
    dbf_set_deleted(t, 1);
    TF_CHECK(dbf_is_deleted(t));
    TF_EQ(dbf_recbuf(t)[0], '*');
    TF_EQ(dbf_write(t), DBF_OK);
    TF_EQ(dbf_read(t, 3), DBF_OK);
    TF_CHECK(!dbf_is_deleted(t));
    TF_EQ(dbf_read(t, 2), DBF_OK);
    TF_CHECK(dbf_is_deleted(t));
    TF_EQ(dbf_get_str(t, 1, out, sizeof out), DBF_OK);
    TF_STREQ(out, "DEUX");
    TF_EQ(dbf_reccount(t), 3);

    /* A deny-write reader is refused while the table is open for
       writing, but only under DOS with SHARE loaded; the host ignores
       sharing modes.  Either outcome is accepted here. */
    if (dbf_open(&t2, tfs_path("REC.DBF", ""), DBF_RDONLY) == DBF_OK)
        TF_EQ(dbf_close(t2), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);

    /* Reopen: data preserved; undelete. */
    TF_EQ(dbf_open(&t, tfs_path("REC.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(dbf_reccount(t), 3);
    TF_EQ(dbf_read(t, 2), DBF_OK);
    TF_CHECK(dbf_is_deleted(t));
    dbf_set_deleted(t, 0);
    TF_EQ(dbf_write(t), DBF_OK);
    TF_EQ(dbf_read(t, 1), DBF_OK);
    TF_EQ(dbf_get_num(t, 0, &g_num, &g_blank), DBF_OK);
    TF_EQ(g_num, 1);
    TF_EQ(dbf_read(t, 3), DBF_OK);
    TF_EQ(dbf_get_str(t, 1, out, sizeof out), DBF_OK);
    TF_STREQ(out, "THREE");
    TF_EQ(dbf_close(t), DBF_OK);
    TF_EQ(dbf_open(&t, tfs_path("REC.DBF", ""), DBF_RDONLY), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(dbf_read(t, 2), DBF_OK);
    TF_CHECK(!dbf_is_deleted(t));

    /* Read-only: every write refused, reads fine. */
    TF_EQ(dbf_write(t), DBF_ERDONLY);
    TF_EQ(dbf_append(t), DBF_ERDONLY);
    TF_EQ(dbf_flush(t), DBF_ERDONLY);
    TF_EQ(dbf_reccount(t), 3);
    TF_EQ(dbf_read(t, 3), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);
    TF_CHECK(tfs_size(tfs_path("REC.DBF", "")) == (u32)hdr + 3 * 29 + 1);

    /* A missing final 0x1A is tolerated and restored by the next append;
       bytes after the last record are ignored. */
    size = slurp("REC.DBF");
    TF_EQ(spit("REC.DBF", (u16)(size - 1)), 0);
    TF_EQ(dbf_open(&t, tfs_path("REC.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(dbf_reccount(t), 3);
    TF_EQ(append_rec(t, 4, "FOUR"), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);
    size = slurp("REC.DBF");
    TF_EQ(size, (u16)(hdr + 4 * 29 + 1));
    TF_EQ(g_file[size - 1], 0x1A);
    TF_EQ(g_file[4], 4);

    g_file[size] = 'J';
    g_file[size + 1] = 'U';
    TF_EQ(spit("REC.DBF", (u16)(size + 2)), 0);
    TF_EQ(dbf_open(&t, tfs_path("REC.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(dbf_reccount(t), 4);
    TF_EQ(dbf_read(t, 4), DBF_OK);
    TF_EQ(dbf_get_str(t, 1, out, sizeof out), DBF_OK);
    TF_STREQ(out, "FOUR");
    TF_EQ(append_rec(t, 5, "FIVE"), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);
    size = slurp("REC.DBF");
    TF_EQ(g_file[(u16)(hdr + 5 * 29)], 0x1A);
    TF_EQ(g_file[4], 5);

    /* Flushing an unchanged table keeps the date and count but restores
       a missing 0x1A. */
    TF_EQ(spit("REC.DBF", (u16)(hdr + 5 * 29)), 0);
    dbf_set_today(2030, 6, 7);
    TF_EQ(dbf_open(&t, tfs_path("REC.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(dbf_flush(t), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);
    size = slurp("REC.DBF");
    TF_EQ(size, (u16)(hdr + 5 * 29 + 1));
    TF_EQ(g_file[size - 1], 0x1A);
    TF_EQ(g_file[1], 27);
    TF_EQ(g_file[4], 5);

    /* A lone dbf_write stores the record and dates the header. */
    TF_EQ(dbf_open(&t, tfs_path("REC.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(dbf_read(t, 1), DBF_OK);
    TF_EQ(dbf_put_str(t, 1, "UNO"), DBF_OK);
    TF_EQ(dbf_write(t), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);
    size = slurp("REC.DBF");
    TF_EQ(size, (u16)(hdr + 5 * 29 + 1));
    TF_EQ(g_file[1], 30);
    TF_EQ(g_file[2], 6);
    TF_EQ(g_file[3], 7);
    TF_EQ(g_file[4], 5);
    TF_CHECK(memcmp(g_file + hdr + 9, "UNO ", 4) == 0);
    TF_EQ(dbf_open(&t, tfs_path("REC.DBF", ""), DBF_RDONLY), DBF_OK);
    if (t == NULL)
        return;
    dbf_lastupdate(t, &year, &month, &day);
    TF_EQ(year, 2030);
    TF_EQ(month, 6);
    TF_EQ(day, 7);
    TF_EQ(dbf_read(t, 1), DBF_OK);
    TF_EQ(dbf_get_str(t, 1, out, sizeof out), DBF_OK);
    TF_STREQ(out, "UNO");
    TF_EQ(dbf_close(t), DBF_OK);

    /* dbf_create replaces a larger existing file completely. */
    memset(g_file, 'Q', sizeof g_file);
    TF_EQ(spit("REC.DBF", (u16)sizeof g_file), 0);
    two_fields();
    TF_EQ(dbf_create(tfs_path("REC.DBF", ""), g_fields, 2), DBF_OK);
    TF_CHECK(tfs_size(tfs_path("REC.DBF", "")) == (u32)hdr + 1);
    TF_EQ(dbf_open(&t, tfs_path("REC.DBF", ""), DBF_RDONLY), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(dbf_reccount(t), 0);
    TF_EQ(dbf_close(t), DBF_OK);
    TF_EQ(cf_remove(tfs_path("REC.DBF", "")), 0);
}

/* ---------------------------------------------------------------------
 * Bad arguments and corrupt files.
 * --------------------------------------------------------------------- */
static void test_create_args(void)
{
    dbf *t;

    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 0), DBF_EARG);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, DBF_MAXFIELDS + 1),
          DBF_EARG);
    set_field(&g_fields[0], "id", 'N', 8, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "1D", 'N', 8, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "", 'N', 8, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "A-B", 'C', 8, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "ID", 'N', 8, 0);
    set_field(&g_fields[1], "ID", 'C', 8, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 2), DBF_EARG);
    set_field(&g_fields[0], "N", 'N', 3, 2);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "N", 'N', 20, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "N", 'N', 0, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "C", 'C', 255, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "C", 'C', 10, 1);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "D", 'D', 6, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "L", 'L', 2, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "M", 'M', 8, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "X", 'F', 8, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    set_field(&g_fields[0], "ABCDEFGHIJK", 'C', 8, 0);
    TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 1), DBF_EARG);
    TF_CHECK(tfs_size(tfs_path("BAD.DBF", "")) == TFS_NOFILE);

    /* Widest names and records that still fit. */
    set_field(&g_fields[0], "ABCDEFGHIJ", 'N', 4, 2);
    TF_EQ(dbf_create(tfs_path("OK.DBF", ""), g_fields, 1), DBF_OK);
    TF_EQ(dbf_open(&t, tfs_path("OK.DBF", ""), DBF_RDWR), DBF_OK);
    if (t != NULL) {
        TF_EQ(dbf_field_index(t, "abcdefghij"), 0);
        TF_EQ(dbf_put_num(t, 0, 999), DBF_OK);
        TF_CHECK(raw_eq(t, 0, "9.99"));
        TF_EQ(dbf_put_num(t, 0, -100), DBF_ERANGE);
        TF_EQ(dbf_put_num(t, 0, -99), DBF_OK);
        TF_CHECK(raw_eq(t, 0, "-.99"));
        TF_EQ(dbf_get_num(t, 0, &g_num, &g_blank), DBF_OK);
        TF_EQ(g_num, -99);
        TF_EQ(dbf_close(t), DBF_OK);
    }
    {
        u16 i;
        char name[11];

        /* 15 widest C fields and one taking the rest of the longest
           record, delete flag included. */
        for (i = 0; i < 16; i++) {
            sprintf(name, "F%u", (unsigned)i);
            set_field(&g_fields[i], name, 'C', (u8)(i < 15 ? DBF_MAXCHAR
                      : DBF_MAXRECLEN - 1 - 15 * DBF_MAXCHAR), 0);
        }
        TF_EQ(dbf_create(tfs_path("OK.DBF", ""), g_fields, 16), DBF_OK);
        g_fields[15].len++;
        TF_EQ(dbf_create(tfs_path("BAD.DBF", ""), g_fields, 16), DBF_EARG);
        TF_EQ(dbf_open(&t, tfs_path("OK.DBF", ""), DBF_RDWR), DBF_OK);
        if (t != NULL) {
            TF_EQ(dbf_reclen(t), DBF_MAXRECLEN);
            TF_EQ(dbf_close(t), DBF_OK);
        }
        for (i = 0; i < DBF_MAXFIELDS; i++) {
            sprintf(name, "F%u", (unsigned)i);
            set_field(&g_fields[i], name, 'L', 1, 0);
        }
        TF_EQ(dbf_create(tfs_path("OK.DBF", ""), g_fields, DBF_MAXFIELDS),
              DBF_OK);
        TF_EQ(dbf_open(&t, tfs_path("OK.DBF", ""), DBF_RDWR), DBF_OK);
        if (t != NULL) {
            TF_EQ(dbf_nfields(t), DBF_MAXFIELDS);
            TF_EQ(dbf_field_index(t, "f127"), DBF_MAXFIELDS - 1);
            TF_EQ(dbf_close(t), DBF_OK);
        }
    }
    TF_EQ(dbf_open(&t, tfs_path("OK.DBF", ""), 7), DBF_EARG);
    TF_CHECK(t == NULL);
    TF_EQ(cf_remove(tfs_path("OK.DBF", "")), 0);

    TF_EQ(dbf_open(&t, tfs_path("NONE.DBF", ""), DBF_RDONLY), DBF_ENOENT);
    TF_CHECK(t == NULL);
    TF_EQ(dbf_open(&t, tfs_path("NODIR/NONE.DBF", ""), DBF_RDONLY),
          DBF_ENOENT);
    two_fields();
    TF_EQ(dbf_create(tfs_path("NODIR/NONE.DBF", ""), g_fields, 2), DBF_ENOENT);
}

static u16 g_good;              /* size of the good file in g_save      */
static u8 g_save[512];

/* Write g_save with one byte changed and try to open it. */
static int open_patched(u16 at, u8 value, u16 size)
{
    dbf *t;
    int rc;

    memcpy(g_file, g_save, sizeof g_save);
    if (at < size)
        g_file[at] = value;
    if (spit("CORRUPT.DBF", size) != 0)
        return 99;
    rc = dbf_open(&t, tfs_path("CORRUPT.DBF", ""), DBF_RDONLY);
    if (rc == DBF_OK && dbf_close(t) != DBF_OK)
        rc = 98;
    return rc;
}

static void test_corrupt(void)
{
    dbf *t;
    u16 hdr;

    /* A good two-field table with two records: header 97, record 29. */
    two_fields();
    TF_EQ(dbf_create(tfs_path("CORRUPT.DBF", ""), g_fields, 2), DBF_OK);
    TF_EQ(dbf_open(&t, tfs_path("CORRUPT.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    TF_EQ(append_rec(t, 1, "A"), DBF_OK);
    TF_EQ(append_rec(t, 2, "B"), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);
    g_good = slurp("CORRUPT.DBF");
    hdr = 97;
    TF_EQ(g_good, hdr + 2 * 29 + 1);
    memcpy(g_save, g_file, sizeof g_save);

    TF_EQ(open_patched(0xFFFF, 0, g_good), DBF_OK);
    TF_EQ(open_patched(0, 0x04, g_good), DBF_EFORMAT);
    TF_EQ(open_patched(0, 0x8B, g_good), DBF_EFORMAT);
    TF_EQ(open_patched(0, 0x83, g_good), DBF_OK);
    TF_EQ(open_patched(4, 3, g_good), DBF_EFORMAT);         /* count */
    TF_EQ(open_patched(7, 1, g_good), DBF_EFORMAT);
    TF_EQ(open_patched(8, (u8)(hdr + 2), g_good), DBF_EFORMAT);
    TF_EQ(open_patched(8, (u8)(hdr - 1), g_good), DBF_EFORMAT);
    TF_EQ(open_patched(8, (u8)(hdr - 32), g_good), DBF_EFORMAT);
    TF_EQ(open_patched(8, 33, g_good), DBF_EFORMAT);
    TF_EQ(open_patched(9, 1, g_good), DBF_EFORMAT);
    TF_EQ(open_patched(10, 28, g_good), DBF_EFORMAT);       /* reclen */
    TF_EQ(open_patched(10, 30, g_good), DBF_EFORMAT);
    TF_EQ(open_patched(11, 1, g_good), DBF_EFORMAT);
    TF_EQ(open_patched(32, 0, g_good), DBF_EFORMAT);        /* name */
    TF_EQ(open_patched(43, 'X', g_good), DBF_EFORMAT);      /* type */
    TF_EQ(open_patched(43, 'M', g_good), DBF_EFORMAT);
    TF_EQ(open_patched(48, 20, g_good), DBF_EFORMAT);       /* N width */
    TF_EQ(open_patched(49, 7, g_good), DBF_EFORMAT);        /* N dec */
    TF_EQ(open_patched(75, 'D', g_good), DBF_EFORMAT);      /* C as D */
    TF_EQ(open_patched(81, 1, g_good), DBF_EFORMAT);        /* C dec */
    TF_EQ(open_patched(96, 0x00, g_good), DBF_EFORMAT);     /* 0x0D */
    TF_EQ(open_patched(12, 0x55, g_good), DBF_OK);          /* reserved */
    TF_EQ(open_patched(44, 0x55, g_good), DBF_OK);          /* address */
    TF_EQ(open_patched(43, 'n', g_good), DBF_OK);
    TF_EQ(open_patched(0xFFFF, 0, (u16)(g_good - 2)), DBF_EFORMAT);
    TF_EQ(open_patched(0xFFFF, 0, (u16)(hdr + 29)), DBF_EFORMAT);
    TF_EQ(open_patched(0xFFFF, 0, (u16)(hdr + 2 * 29)), DBF_OK);
    TF_EQ(open_patched(0xFFFF, 0, 31), DBF_EFORMAT);
    TF_EQ(open_patched(0xFFFF, 0, 50), DBF_EFORMAT);
    TF_EQ(open_patched(0xFFFF, 0, (u16)(hdr - 1)), DBF_EFORMAT);
    TF_EQ(open_patched(0xFFFF, 0, 0), DBF_EFORMAT);

    /* A header counting one extra byte after the 0x0D is accepted. */
    memcpy(g_file, g_save, hdr);
    g_file[8] = (u8)(hdr + 1);
    g_file[hdr] = 0x00;
    memcpy(g_file + hdr + 1, g_save + hdr, (size_t)(g_good - hdr));
    TF_EQ(spit("CORRUPT.DBF", (u16)(g_good + 1)), 0);
    TF_EQ(dbf_open(&t, tfs_path("CORRUPT.DBF", ""), DBF_RDONLY), DBF_OK);
    if (t != NULL) {
        TF_EQ(dbf_read(t, 2), DBF_OK);
        TF_EQ(dbf_get_num(t, 0, &g_num, &g_blank), DBF_OK);
        TF_EQ(g_num, 2);
        TF_EQ(dbf_close(t), DBF_OK);
    }

    /* The same file opened for writing: records stay after the extra
       byte, which is kept. */
    TF_EQ(dbf_open(&t, tfs_path("CORRUPT.DBF", ""), DBF_RDWR), DBF_OK);
    if (t != NULL) {
        TF_EQ(dbf_hdrlen(t), hdr + 1);
        TF_EQ(append_rec(t, 3, "C"), DBF_OK);
        TF_EQ(dbf_read(t, 1), DBF_OK);
        TF_EQ(dbf_put_str(t, 1, "ALPHA"), DBF_OK);
        TF_EQ(dbf_write(t), DBF_OK);
        TF_EQ(dbf_close(t), DBF_OK);
    }
    TF_EQ(slurp("CORRUPT.DBF"), (u16)(hdr + 1 + 3 * 29 + 1));
    TF_EQ(g_file[4], 3);
    TF_EQ(g_file[8], hdr + 1);
    TF_EQ(g_file[hdr - 1], 0x0D);
    TF_EQ(g_file[hdr], 0x00);
    TF_CHECK(memcmp(g_file + hdr + 1, "        1ALPHA ", 15) == 0);
    TF_CHECK(memcmp(g_file + hdr + 1 + 29, "        2B ", 11) == 0);
    TF_CHECK(memcmp(g_file + hdr + 1 + 2 * 29, "        3C ", 11) == 0);
    TF_EQ(g_file[hdr + 1 + 3 * 29], 0x1A);
    TF_EQ(cf_remove(tfs_path("CORRUPT.DBF", "")), 0);
}

/* Header year bytes: two-digit years around the 1980 pivot, and the
   1900 + value form of some later xBase programs. */
static void year_case(u8 yy, u16 want)
{
    dbf *t;
    u16 year;
    u8 month;
    u8 day;

    g_file[1] = yy;
    TF_EQ(spit("YEARS.DBF", 98), 0);
    TF_EQ(dbf_open(&t, tfs_path("YEARS.DBF", ""), DBF_RDONLY), DBF_OK);
    if (t == NULL)
        return;
    dbf_lastupdate(t, &year, &month, &day);
    TF_EQ(year, want);
    TF_EQ(month, 10);
    TF_EQ(day, 8);
    TF_EQ(dbf_close(t), DBF_OK);
}

static void test_years(void)
{
    two_fields();
    dbf_set_today(2026, 10, 8);
    TF_EQ(dbf_create(tfs_path("YEARS.DBF", ""), g_fields, 2), DBF_OK);
    TF_EQ(slurp("YEARS.DBF"), 98);
    TF_EQ(g_file[1], 0x1A);
    year_case(0x1A, 2026);
    year_case(0x63, 1999);
    year_case(0x50, 1980);
    year_case(0x4F, 2079);
    year_case(0x00, 2000);
    year_case(0x7E, 2026);
    year_case(0xFF, 2155);

    /* Years outside 1980..2079 are written as the nearest end. */
    dbf_set_today(1975, 10, 8);
    TF_EQ(dbf_create(tfs_path("YEARS.DBF", ""), g_fields, 2), DBF_OK);
    TF_EQ(slurp("YEARS.DBF"), 98);
    TF_EQ(g_file[1], 80);
    dbf_set_today(2100, 10, 8);
    TF_EQ(dbf_create(tfs_path("YEARS.DBF", ""), g_fields, 2), DBF_OK);
    TF_EQ(slurp("YEARS.DBF"), 98);
    TF_EQ(g_file[1], 79);
    TF_EQ(cf_remove(tfs_path("YEARS.DBF", "")), 0);
    dbf_set_today(2026, 10, 8);
}

/* A valid memo field is refused in a 0x03 table and accepted in 0x83. */
static void test_memo_version(void)
{
    dbf *t;
    u16 size;

    two_fields();
    set_field(&g_fields[2], "NOTES", 'M', 10, 0);
    TF_EQ(dbf_create(tfs_path("MEMOVER.DBF", ""), g_fields, 3), DBF_OK);
    size = slurp("MEMOVER.DBF");
    TF_EQ(size, 32 + 3 * 32 + 1 + 1);
    TF_EQ(g_file[0], 0x83);
    TF_EQ(g_file[32 + 2 * 32 + 11], 'M');
    TF_EQ(g_file[32 + 2 * 32 + 16], 10);
    TF_EQ(dbf_open(&t, tfs_path("MEMOVER.DBF", ""), DBF_RDONLY), DBF_OK);
    if (t != NULL) {
        TF_CHECK(dbf_has_memo(t));
        TF_EQ(dbf_close(t), DBF_OK);
    }
    g_file[0] = 0x03;
    TF_EQ(spit("MEMOVER.DBF", size), 0);
    TF_EQ(dbf_open(&t, tfs_path("MEMOVER.DBF", ""), DBF_RDONLY), DBF_EFORMAT);
    TF_CHECK(t == NULL);
    g_file[0] = 0x83;
    TF_EQ(spit("MEMOVER.DBF", size), 0);
    TF_EQ(dbf_open(&t, tfs_path("MEMOVER.DBF", ""), DBF_RDONLY), DBF_OK);
    if (t != NULL)
        TF_EQ(dbf_close(t), DBF_OK);
    TF_EQ(cf_remove(tfs_path("MEMOVER.DBF", "")), 0);
}

/* ---------------------------------------------------------------------
 * Mutated files: random byte changes and truncations of a valid table
 * must give a clean error or a table whose every operation is safe.
 * --------------------------------------------------------------------- */
#define MUTATIONS 300

static u32 g_seed = 20261008UL;

static u16 rnd(u16 n)
{
    g_seed = (u32)(g_seed * 1103515245UL + 12345UL);
    return (u16)((g_seed >> 16) % n);
}

static int known_rc(int rc)
{
    return rc == DBF_OK || rc == DBF_EFORMAT || rc == DBF_ERANGE
           || rc == DBF_EARG || rc == DBF_EIO;
}

static void test_mutate(void)
{
    static const u8 vals[] = {0x00, 0x01, 0x03, 0x0D, 0x1A, 0x20, 0x2A,
                              0x2E, 0x2D, 0x30, 0x39, 0x83, 0xFF};
    char out[40];
    u32 block;
    dbf *t;
    u16 size;
    u16 n;
    u16 i;
    u16 k;
    u16 f;
    u32 r;
    u16 bad;
    u16 opened;
    int rc;

    two_fields();
    set_field(&g_fields[2], "DT", 'D', 8, 0);
    set_field(&g_fields[3], "OK", 'L', 1, 0);
    set_field(&g_fields[4], "NOTES", 'M', 10, 0);
    set_field(&g_fields[5], "PRICE", 'N', 9, 2);
    TF_EQ(dbf_create(tfs_path("MUTATE.DBF", ""), g_fields, 6), DBF_OK);
    TF_EQ(dbf_open(&t, tfs_path("MUTATE.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    for (i = 1; i <= 3; i++) {
        dbf_blank(t);
        TF_EQ(dbf_put_num(t, 0, (i32)i), DBF_OK);
        TF_EQ(dbf_put_str(t, 1, "NAME"), DBF_OK);
        TF_EQ(dbf_put_str(t, 2, "20240229"), DBF_OK);
        TF_EQ(dbf_put_str(t, 3, "T"), DBF_OK);
        TF_EQ(dbf_put_memo(t, 4, (u32)i), DBF_OK);
        TF_EQ(dbf_put_num(t, 5, -1234L), DBF_OK);
        TF_EQ(dbf_append(t), DBF_OK);
    }
    TF_EQ(dbf_close(t), DBF_OK);
    size = slurp("MUTATE.DBF");
    TF_CHECK(size <= sizeof g_save);
    if (size > sizeof g_save)
        return;
    memcpy(g_save, g_file, size);

    bad = 0;
    opened = 0;
    for (i = 0; i < MUTATIONS; i++) {
        memcpy(g_file, g_save, size);
        n = size;
        k = (u16)(1 + rnd(3));
        while (k-- > 0) {
            if (rnd(2) == 0)
                g_file[rnd(n)] = vals[rnd(sizeof vals)];
            else
                g_file[rnd(n)] = (u8)rnd(256);
        }
        if (rnd(8) == 0)
            n = rnd(size);
        if (spit("MUTATE.DBF", n) != 0) {
            bad++;
            continue;
        }
        rc = dbf_open(&t, tfs_path("MUTATE.DBF", ""), DBF_RDWR);
        if (rc != DBF_OK) {
            if (rc != DBF_EFORMAT || t != NULL)
                bad++;
            continue;
        }
        opened++;
        for (r = 0; r <= dbf_reccount(t) + 1; r++) {
            rc = dbf_read(t, r);
            if (!known_rc(rc))
                bad++;
            if (rc != DBF_OK)
                continue;
            for (f = 0; f <= dbf_nfields(t); f++) {
                if (!known_rc(dbf_get_str(t, f, out, sizeof out))
                    || !known_rc(dbf_get_num(t, f, &g_num, &g_blank))
                    || !known_rc(dbf_get_memo(t, f, &block)))
                    bad++;
            }
            if (dbf_write(t) != DBF_OK)
                bad++;
        }
        dbf_blank(t);
        if (dbf_append(t) != DBF_OK || dbf_close(t) != DBF_OK)
            bad++;
    }
    TF_EQ(bad, 0);
    TF_CHECK(opened > 0);
    TF_CHECK(opened < MUTATIONS);
    TF_EQ(cf_remove(tfs_path("MUTATE.DBF", "")), 0);
}

/* ---------------------------------------------------------------------
 * Many records.
 * --------------------------------------------------------------------- */
#define MANY 1000

static void name_of(u32 n, char *out)
{
    sprintf(out, "REC%05lu-%lu", (unsigned long)n,
            (unsigned long)(n * 7919UL % 1000UL));
}

static void test_many(void)
{
    char want[24];
    char out[24];
    dbf *t;
    u32 i;
    u32 r;
    u32 seed;
    u16 bad;

    two_fields();
    TF_EQ(dbf_create(tfs_path("MANY.DBF", ""), g_fields, 2), DBF_OK);
    TF_EQ(dbf_open(&t, tfs_path("MANY.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    bad = 0;
    for (i = 1; i <= MANY; i++) {
        name_of(i, want);
        if (append_rec(t, (i32)(i * 1000UL), want) != DBF_OK)
            bad++;
    }
    TF_EQ(bad, 0);
    TF_CHECK(dbf_reccount(t) == MANY);
    TF_EQ(dbf_close(t), DBF_OK);
    TF_CHECK(tfs_size(tfs_path("MANY.DBF", "")) == 97UL + MANY * 29UL + 1UL);

    TF_EQ(dbf_open(&t, tfs_path("MANY.DBF", ""), DBF_RDWR), DBF_OK);
    if (t == NULL)
        return;
    TF_CHECK(dbf_reccount(t) == MANY);
    seed = 12345UL;
    bad = 0;
    for (i = 0; i < 500; i++) {
        seed = (u32)(seed * 1103515245UL + 12345UL);
        r = (seed >> 16) % MANY + 1;
        name_of(r, want);
        if (dbf_read(t, r) != DBF_OK
            || dbf_get_num(t, 0, &g_num, &g_blank) != DBF_OK
            || g_num != (i32)(r * 1000UL)
            || dbf_get_str(t, 1, out, sizeof out) != DBF_OK
            || strcmp(out, want) != 0
            || dbf_recno(t) != r)
            bad++;
    }
    TF_EQ(bad, 0);
    TF_EQ(dbf_read(t, MANY), DBF_OK);
    TF_EQ(dbf_read(t, MANY + 1), DBF_ERANGE);
    TF_EQ(dbf_close(t), DBF_OK);
    TF_EQ(cf_remove(tfs_path("MANY.DBF", "")), 0);
}

/* After an append the file's size is committed before the header count
   is written, and the file is committed again afterwards; a flush with
   no append writes the header and commits once. */
static void test_flush_order(void)
{
    dbf *t;

    two_fields();
    TF_EQ(dbf_create(tfs_path("ORD.DBF", ""), g_fields, 2), DBF_OK);
    if (!TF_EQ(dbf_open(&t, tfs_path("ORD.DBF", ""), DBF_RDWR), DBF_OK))
        return;
    dbf_blank(t);
    tfs_fault(0, 0, 0, 0);
    TF_EQ(dbf_append(t), DBF_OK);
    TF_EQ(dbf_flush(t), DBF_OK);
    tfs_fault_off();
    TF_EQ(tfs_calls(), 4);
    TF_EQ(tfs_ops[0], CF_OP_WRITE);
    TF_EQ(tfs_ops[1], CF_OP_COMMIT);
    TF_EQ(tfs_ops[2], CF_OP_WRITE);
    TF_EQ(tfs_ops[3], CF_OP_COMMIT);

    tfs_fault(0, 0, 0, 0);
    TF_EQ(dbf_write(t), DBF_OK);
    TF_EQ(dbf_flush(t), DBF_OK);
    tfs_fault_off();
    TF_EQ(tfs_calls(), 3);
    TF_EQ(tfs_ops[0], CF_OP_WRITE);
    TF_EQ(tfs_ops[1], CF_OP_WRITE);
    TF_EQ(tfs_ops[2], CF_OP_COMMIT);

    /* dbf_close flushes in the same order. */
    dbf_blank(t);
    TF_EQ(dbf_append(t), DBF_OK);
    tfs_fault(0, 0, 0, 0);
    TF_EQ(dbf_close(t), DBF_OK);
    tfs_fault_off();
    TF_EQ(tfs_calls(), 3);
    TF_EQ(tfs_ops[0], CF_OP_COMMIT);
    TF_EQ(tfs_ops[1], CF_OP_WRITE);
    TF_EQ(tfs_ops[2], CF_OP_COMMIT);

    /* dbf_drop writes nothing: a record appended since the last flush
       stays uncounted. */
    if (!TF_EQ(dbf_open(&t, tfs_path("ORD.DBF", ""), DBF_RDWR), DBF_OK))
        return;
    dbf_blank(t);
    TF_EQ(dbf_append(t), DBF_OK);
    tfs_fault(0, 0, 0, 0);
    TF_EQ(dbf_drop(t), DBF_OK);
    tfs_fault_off();
    TF_EQ(tfs_calls(), 0);
    if (TF_EQ(dbf_open(&t, tfs_path("ORD.DBF", ""), DBF_RDONLY), DBF_OK)) {
        TF_EQ(dbf_reccount(t), 2);
        TF_EQ(dbf_close(t), DBF_OK);
    }
    TF_EQ(cf_remove(tfs_path("ORD.DBF", "")), 0);
}

/* A flush whose size commit fails writes no header: the count on the
   disk stays old until a later flush, which commits first again.  An
   append whose write fails still counts as growth, so the next flush
   commits before it writes the end byte.  A commit that finds no free
   handle, first or last, is DBF_EHANDLES. */
static void test_flush_fail(void)
{
    dbf *t;

    two_fields();
    TF_EQ(dbf_create(tfs_path("ORD.DBF", ""), g_fields, 2), DBF_OK);
    if (!TF_EQ(dbf_open(&t, tfs_path("ORD.DBF", ""), DBF_RDWR), DBF_OK))
        return;
    dbf_blank(t);
    TF_EQ(dbf_append(t), DBF_OK);
    tfs_fault(TFS_OP(CF_OP_COMMIT), 1, 1, CF_EMFILE);
    TF_EQ(dbf_flush(t), DBF_EHANDLES);
    tfs_fault_off();
    TF_EQ(tfs_calls(), 1);
    slurp("ORD.DBF");
    TF_EQ(get_le32(g_file + 4), 0);
    tfs_fault(0, 0, 0, 0);
    TF_EQ(dbf_flush(t), DBF_OK);
    tfs_fault_off();
    TF_EQ(tfs_calls(), 3);
    TF_EQ(tfs_ops[0], CF_OP_COMMIT);
    slurp("ORD.DBF");
    TF_EQ(get_le32(g_file + 4), 1);

    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 1, 0);
    TF_EQ(dbf_append(t), DBF_EFULL);
    TF_EQ(dbf_flush(t), DBF_OK);
    tfs_fault_off();
    TF_EQ(dbf_reccount(t), 1);
    TF_EQ(tfs_calls(), 4);
    TF_EQ(tfs_ops[0], CF_OP_WRITE);
    TF_EQ(tfs_ops[1], CF_OP_COMMIT);
    TF_EQ(tfs_ops[2], CF_OP_WRITE);
    TF_EQ(tfs_ops[3], CF_OP_COMMIT);

    TF_EQ(dbf_append(t), DBF_OK);
    tfs_fault(TFS_OP(CF_OP_COMMIT), 2, 1, CF_EMFILE);
    TF_EQ(dbf_flush(t), DBF_EHANDLES);
    tfs_fault_off();
    TF_EQ(tfs_count(CF_OP_COMMIT), 2);
    TF_EQ(dbf_close(t), DBF_OK);
    TF_EQ(tfs_size(tfs_path("ORD.DBF", "")), 32u + 64u + 1u + 2u * 29u + 1u);
    TF_EQ(cf_remove(tfs_path("ORD.DBF", "")), 0);
}

/* ---------------------------------------------------------------------
 * The cf_ error mapping, the open modes and dbf_trim.
 * --------------------------------------------------------------------- */

/* Every cf_ code, mapped directly and as dbf_create returns it when the
   create fails with that code (code 0: the create runs). */
static void test_map_cf(void)
{
    static const int map[][2] = {
        {0, DBF_OK},
        {CF_ENOENT, DBF_ENOENT},
        {CF_ENOPATH, DBF_ENOENT},
        {CF_EMFILE, DBF_EHANDLES},
        {CF_EACCES, DBF_EINUSE},
        {CF_EBADF, DBF_EIO},
        {CF_ENOMEM, DBF_ENOMEM},
        {CF_EINVACC, DBF_EIO},
        {CF_EINVAL, DBF_EIO},
        {CF_ESHARE, DBF_EINUSE},
        {CF_ELOCK, DBF_EINUSE},
        {CF_EFULL, DBF_EFULL},
        {1, DBF_EIO}            /* a code COMPAT.H does not name        */
    };
    int i;

    two_fields();
    for (i = 0; i < (int)(sizeof map / sizeof map[0]); i++) {
        TF_EQ(dbf_map_cf(map[i][0]), map[i][1]);
        tfs_fault(TFS_OP(CF_OP_CREATE), map[i][0] != 0 ? 1 : 0, 0,
                  map[i][0]);
        TF_EQ(dbf_create(tfs_path("MAP.DBF", ""), g_fields, 2), map[i][1]);
        tfs_fault_off();
    }
    TF_EQ(cf_remove(tfs_path("MAP.DBF", "")), 0);
}

/* dbf_cf_open: DBF_RDWR gives a handle that writes, DBF_RDONLY one that
   reads but does not write, any other mode DBF_EARG.  (The sharing
   flags are DOS's to enforce; the host has no SHARE.) */
static void test_cf_open(void)
{
    u8 b;
    u16 got;
    int h;

    two_fields();
    TF_EQ(dbf_create(tfs_path("CFO.DBF", ""), g_fields, 2), DBF_OK);
    if (TF_EQ(dbf_cf_open(tfs_path("CFO.DBF", ""), DBF_RDWR, &h), DBF_OK)) {
        TF_EQ(cf_read(h, &b, 1, &got), 0);
        TF_EQ(b, 0x03);
        TF_EQ(cf_seek(h, 0), 0);
        TF_EQ(cf_write(h, &b, 1), 0);
        TF_EQ(cf_close(h), 0);
    }
    if (TF_EQ(dbf_cf_open(tfs_path("CFO.DBF", ""), DBF_RDONLY, &h), DBF_OK)) {
        TF_EQ(cf_read(h, &b, 1, &got), 0);
        TF_EQ(b, 0x03);
        TF_EQ(cf_seek(h, 0), 0);
        TF_CHECK(cf_write(h, &b, 1) != 0);
        TF_EQ(cf_close(h), 0);
    }
    TF_EQ(dbf_cf_open(tfs_path("CFO.DBF", ""), 2, &h), DBF_EARG);
    TF_EQ(dbf_cf_open(tfs_path("CFO.DBF", ""), -1, &h), DBF_EARG);
    TF_EQ(dbf_cf_open(tfs_path("NONE.DBF", ""), DBF_RDONLY, &h), DBF_ENOENT);
    TF_EQ(cf_remove(tfs_path("CFO.DBF", "")), 0);
}

#define TRIM_LEN        156     /* two records: 97 + 2 * 29 + 1         */

static u8 g_trim[TRIM_LEN];

/* Write the good table's first n bytes plus the tail text, trim it with
   dbf_trim and close it.  1 when the file is then the good table byte
   for byte and the trim made exactly the disk calls in ops (nops of
   them). */
static int trim_case(u16 n, const char *tail, const int *ops, int nops)
{
    dbf *t;
    u16 tl;
    int ok;
    int i;

    tl = (u16)strlen(tail);
    memcpy(g_file, g_trim, n);
    memcpy(g_file + n, tail, tl);
    if (spit("TRIM.DBF", (u16)(n + tl)) != 0)
        return 0;
    if (dbf_open(&t, tfs_path("TRIM.DBF", ""), DBF_RDWR) != DBF_OK)
        return 0;
    tfs_fault(0, 0, 0, 0);
    ok = dbf_trim(t) == DBF_OK && dbf_reccount(t) == 2;
    tfs_fault_off();
    ok = ok && tfs_calls() == (u32)nops;
    for (i = 0; ok && i < nops; i++)
        ok = tfs_ops[i] == ops[i];
    if (dbf_close(t) != DBF_OK)
        ok = 0;
    return ok && slurp("TRIM.DBF") == TRIM_LEN &&
           memcmp(g_file, g_trim, TRIM_LEN) == 0;
}

/* dbf_trim on every tail a table can have after its last counted
   record: the bytes end as a clean table's, with no disk call when
   nothing is wrong, one write for a missing or overwritten 0x1A, and a
   cut then a write when more than one byte follows.  The next flush
   commits the cut size without writing; a flush with nothing pending
   makes no disk call. */
static void test_trim(void)
{
    static const int k_write[1] = {CF_OP_WRITE};
    static const int k_cut[2] = {CF_OP_TRUNC, CF_OP_WRITE};
    dbf *t;

    two_fields();
    TF_EQ(dbf_create(tfs_path("TRIM.DBF", ""), g_fields, 2), DBF_OK);
    if (!TF_EQ(dbf_open(&t, tfs_path("TRIM.DBF", ""), DBF_RDWR), DBF_OK))
        return;
    TF_EQ(append_rec(t, 1, "ONE"), DBF_OK);
    TF_EQ(append_rec(t, 2, "TWO"), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);
    if (!TF_EQ(slurp("TRIM.DBF"), TRIM_LEN))
        return;
    memcpy(g_trim, g_file, TRIM_LEN);
    TF_EQ(g_trim[TRIM_LEN - 1], 0x1A);

    TF_CHECK(trim_case(TRIM_LEN, "", k_write, 0));
    TF_CHECK(trim_case(TRIM_LEN - 1, "", k_write, 1));
    TF_CHECK(trim_case(TRIM_LEN - 1, "Q", k_write, 1));
    TF_CHECK(trim_case(TRIM_LEN - 1, "ZZZZZ", k_cut, 2));
    TF_CHECK(trim_case(TRIM_LEN, "Z", k_cut, 2));
    TF_CHECK(trim_case(TRIM_LEN, "ZZ", k_cut, 2));
    TF_CHECK(trim_case(TRIM_LEN - 1, "        3THREE", k_cut, 2));

    memcpy(g_file, g_trim, TRIM_LEN);
    memcpy(g_file + TRIM_LEN, "ZZ", 2);
    TF_EQ(spit("TRIM.DBF", TRIM_LEN + 2), 0);
    if (TF_EQ(dbf_open(&t, tfs_path("TRIM.DBF", ""), DBF_RDWR), DBF_OK)) {
        TF_EQ(dbf_trim(t), DBF_OK);
        tfs_fault(0, 0, 0, 0);
        TF_EQ(dbf_flush(t), DBF_OK);
        tfs_fault_off();
        TF_EQ(tfs_count(CF_OP_COMMIT), 1);
        TF_EQ(tfs_calls(), 1);
        tfs_fault(0, 0, 0, 0);
        TF_EQ(dbf_flush(t), DBF_OK);
        tfs_fault_off();
        TF_EQ(tfs_calls(), 0);
        TF_EQ(dbf_close(t), DBF_OK);
    }

    /* A read-only table is refused and left as it is. */
    memcpy(g_file, g_trim, TRIM_LEN);
    memcpy(g_file + TRIM_LEN, "ZZ", 2);
    TF_EQ(spit("TRIM.DBF", TRIM_LEN + 2), 0);
    if (TF_EQ(dbf_open(&t, tfs_path("TRIM.DBF", ""), DBF_RDONLY), DBF_OK)) {
        TF_EQ(dbf_trim(t), DBF_ERDONLY);
        TF_EQ(dbf_close(t), DBF_OK);
    }
    TF_EQ(slurp("TRIM.DBF"), TRIM_LEN + 2);
    TF_EQ(memcmp(g_file, g_trim, TRIM_LEN), 0);
    TF_EQ(cf_remove(tfs_path("TRIM.DBF", "")), 0);
}

int main(int argc, char **argv)
{
    tf_begin("T_DBF");
    if (argc > 1)
        tfs_set_dir(argv[1]);
    tfs_wipe(k_scratch);

    test_golden_header();
    test_types();
    test_num_limits();
    test_records();
    test_create_args();
    test_corrupt();
    test_memo_version();
    test_years();
    test_mutate();
    test_many();
    test_flush_order();
    test_flush_fail();
    test_map_cf();
    test_cf_open();
    test_trim();
    tfs_wipe(k_scratch);
    return tf_done();
}
