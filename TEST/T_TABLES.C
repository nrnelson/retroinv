/*
 * T_TABLES.C - opening a data directory: creation, layouts, seed rows,
 * SEQ repair, recovery, index rebuild and checks, files in use, handles.
 *
 * usage: T_TABLES dir [SHARE]
 *
 * dir is a scratch directory used only by this test ("D:\" under DOS);
 * every engine file and pack or reindex leftover in it is removed first.
 * The engine runs with the clock fixed at 2026-10-08 12:00:00, so every
 * byte it writes is reproducible.  The created files are checked byte
 * for byte against the layouts of the schema document, written out here
 * independently of SCHTAB.H.  The test ends with a clean, reopened data
 * directory, which a structural checker can be run over afterwards.
 *
 * With the word SHARE after dir, SHARE.EXE must be loaded: a data file
 * held deny-read/write by another handle must then make the open fail
 * with ENG_EINUSE, and so must a second engine on the same directory.
 * Without SHARE (and on the host) sharing modes are not enforced, so
 * those checks are skipped.
 *
 * This file holds the layouts, the helpers, the first tests and main;
 * the other tests are in T_TABLE2.C and T_TABLE3.C, and T_TABLES.H
 * declares what the three share.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGSCH.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TEWIPE.H"
#include "T_TABLES.H"

#define HDRMAX          1024

char g_why[TFS_WHYSZ];
u8  *g_buf;
static int g_base;              /* the probe handle with nothing open   */

static const eng_time g_now = { 2026, 10, 8, 12, 0, 0 };

/* ---------------------------------------------------------------------
 * The layouts of the schema document.
 * --------------------------------------------------------------------- */
/* Each table's fields in file order, as "NAME:Tlen" or "NAME:Tlen.dec"
   separated by single spaces. */
static const char x_items[] =
    "ITEM_ID:N8 CATEGORY:C4 ITEMTYPE:C20 MFR:C30 MODEL:C30 "
    "SERIALNO:C30 DESCR:C60 COND:C3 WORKS:C1 QTY:N5 ON_LOAN:N5 "
    "RESERVED:N5 IN_USE:N5 ATTACHED:N5 LOC_ID:N8 "
    "PARENT_ID:N8 ACQ_DATE:D8 ACQ_SRC:C30 ACQ_PRICE:N9.2 "
    "CUR_VALUE:N9.2 TAGS:C60 STATUS:C1 CREATED:D8 UPDATED:D8 "
    "NOTES:M10";
static const char x_locs[] =
    "LOC_ID:N8 PARENT_ID:N8 LOC_NAME:C30 LOC_CODE:C10 LTYPE:C8 "
    "CAPACITY:N4 NOTES:M10";
static const char x_cats[] =
    "CAT_CODE:C4 CAT_NAME:C20 SORTORD:N2";
static const char x_ltypes[] =
    "LT_CODE:C8 LT_NAME:C20 SORTORD:N2";
static const char x_maint[] =
    "MAINT_ID:N8 ITEM_ID:N8 MDATE:D8 MTYPE:C2 SUMMARY:C60 "
    "COST:N8.2 DONE_BY:C20 DETAIL:M10";
static const char x_loans[] =
    "LOAN_ID:N8 ITEM_ID:N8 KIND:C1 QTY:N5 HOLDER:C30 HOST_ID:N8 "
    "CONTACT:C30 OUT_DATE:D8 DUE_DATE:D8 RET_DATE:D8 NOTES:C60";
static const char x_audit[] =
    "AUD_ID:N8 ADATE:D8 ATIME:C8 STATION:C16 USERNAME:C8 TBL:C8 "
    "REC_ID:N8 ACTION:C1 FLD:C10 OLDVAL:C40 NEWVAL:C40";
static const char x_users[] =
    "USER_ID:N4 USERNAME:C8 FULLNAME:C30 GROUPNAME:C8 LEVEL:N1 "
    "SALT:C4 PWHASH:C32 STATUS:C1 MUSTCHG:L1 LASTLOGIN:D8 "
    "CREATED:D8";
static const char x_seq[] =
    "SEQ_NAME:C10 NEXT_VAL:N8";

typedef struct {
    const char *name;
    int memo;
    const char *fields;
    u32 rows;                   /* records after the first open         */
} xtable;

static const xtable x_tables[ENG_NTABLES] = {
    { "ITEMS", 1, x_items, 0 },
    { "LOCS", 1, x_locs, 0 },
    { "CATS", 0, x_cats, 10 },
    { "LTYPES", 0, x_ltypes, 8 },
    { "MAINT", 1, x_maint, 0 },
    { "LOANS", 0, x_loans, 0 },
    { "AUDIT", 0, x_audit, 19 },
    { "USERS", 0, x_users, 1 },
    { "SEQ", 0, x_seq, 6 }
};

typedef struct {
    const char *name;
    const char *expr;
    u16 keylen;
} xindex;

static const xindex x_indexes[ENG_NINDEX] = {
    { "ITEMS1", "STR(ITEM_ID,8)", 8 },
    { "ITEMS2", "STR(LOC_ID,8)+STR(ITEM_ID,8)", 16 },
    { "ITEMS3", "STR(PARENT_ID,8)+STR(ITEM_ID,8)", 16 },
    { "ITEMS4", "UPPER(MFR)+UPPER(MODEL)", 60 },
    { "ITEMS5", "UPPER(SERIALNO)", 30 },
    { "LOCS1", "STR(LOC_ID,8)", 8 },
    { "LOCS2", "STR(PARENT_ID,8)+UPPER(LOC_NAME)+STR(LOC_ID,8)", 46 },
    { "LOCS3", "LOC_CODE", 10 },
    { "MAINT1", "STR(ITEM_ID,8)+STR(YEAR(MDATE),4)+"
      "RIGHT(STR(MONTH(MDATE)+100,3),2)+RIGHT(STR(DAY(MDATE)+100,3),2)",
      16 },
    { "LOANS1", "STR(ITEM_ID,8)+STR(LOAN_ID,8)", 16 },
    { "LOANS2", "STR(YEAR(RET_DATE),4)+STR(YEAR(DUE_DATE),4)+"
      "RIGHT(STR(MONTH(DUE_DATE)+100,3),2)+RIGHT(STR(DAY(DUE_DATE)+100,3),2)"
      "+STR(LOAN_ID,8)", 20 },
    { "AUDIT1", "TBL+STR(REC_ID,8)+STR(AUD_ID,8)", 24 },
    { "AUDIT2", "USERNAME+STR(99999999-AUD_ID,8)", 16 },
    { "USERS1", "USERNAME", 8 }
};

/* One field of a layout string. */
typedef struct {
    char name[DBF_NAMELEN + 1];
    char type;
    u8 len;
    u8 dec;
} xfield;

/* Parse the field at *p into f and advance *p; 0 at the end. */
static int next_field(const char **p, xfield *f)
{
    const char *q;
    u16 n;

    q = *p;
    if (*q == '\0')
        return 0;
    n = 0;
    while (*q != ':' && *q != '\0' && n < DBF_NAMELEN)
        f->name[n++] = *q++;
    f->name[n] = '\0';
    if (*q == ':')
        q++;
    f->type = *q != '\0' ? *q++ : '?';
    f->len = 0;
    while (*q >= '0' && *q <= '9')
        f->len = (u8)(f->len * 10 + (*q++ - '0'));
    f->dec = 0;
    if (*q == '.') {
        q++;
        while (*q >= '0' && *q <= '9')
            f->dec = (u8)(f->dec * 10 + (*q++ - '0'));
    }
    if (*q == ' ')
        q++;
    *p = q;
    return 1;
}

/* ---------------------------------------------------------------------
 * Helpers.
 * --------------------------------------------------------------------- */

int exists(const char *stem, const char *ext)
{
    return tfs_exists(tfs_path(stem, ext));
}

void remove_file(const char *stem, const char *ext)
{
    cf_remove(tfs_path(stem, ext));
}

/* Remove every file of the data directory: the engine's, and the memo
   file test_mismatch_table keeps aside. */
void remove_all(void)
{
    static const char *const k_aside[] = { "LOCS.OLD", NULL };

    te_remove(tfs_dir(), k_aside);
}

/* Read the whole file, or its first HDRMAX bytes, into g_buf; its size
   (0 when it is missing). */
u32 read_file(const char *stem, const char *ext, u16 *got)
{
    u32 size;

    *got = 0;
    size = tfs_size(tfs_path(stem, ext));
    if (!TF_EQ(tfs_read_all(tfs_path(stem, ext), g_buf, HDRMAX, got), 0))
        return 0;
    return size;
}

/* A file that the test writes itself. */
void write_bytes(const char *stem, const char *ext, const char *p,
                        u16 n)
{
    TF_EQ(tfs_write_all(tfs_path(stem, ext), p, n), 0);
}

void write_file(const char *stem, const char *ext, const char *text)
{
    write_bytes(stem, ext, text, (u16)strlen(text));
}

/* The DOS handle the next open gets: handles are given out lowest
   first, so this tells how many the engine holds. */
static int probe_handle(void)
{
    int h;

    h = -1;
    if (!TF_EQ(cf_open(tfs_path("PROBE", ".TMP"), CF_READ | CF_DENYNONE, &h),
               0))
        return -1;
    cf_close(h);
    return h;
}

static int open_eng(eng **e)
{
    int rc;

    g_why[0] = '\0';
    rc = eng_open(e, tfs_dir(), &g_now, g_why, (u16)sizeof g_why);
    if (rc != ENG_OK)
        printf("T_TABLES: open %d: %s\n", rc, g_why);
    return rc;
}

static void text_of(eng *e, int t, const char *field, char *out)
{
    int f;

    out[0] = '\0';
    f = eng_field_num(e, t, field);
    if (TF_CHECK(f >= 0))
        TF_EQ(eng_rec_text(e, t, (u16)f, out), ENG_OK);
}

/* Open the engine, which must give want; the check's result. */
int expect_open(eng **e, int want)
{
    return TF_EQ(open_eng(e), want);
}

void expect_close(eng *e)
{
    TF_EQ(eng_close(e), ENG_OK);
}

/* The failed open's text names the file name. */
void expect_why(const char *name)
{
    if (!TF_CHECK(strstr(g_why, name) != NULL))
        printf("T_TABLES: want %s in \"%s\"\n", name, g_why);
}

/* The live lookup of key on index x finds record want (0: none). */
void expect_seek(eng *e, int x, const char *key, u32 want)
{
    u32 r;

    TF_EQ(eng_seek_live(e, x, key, &r), ENG_OK);
    if (!TF_EQ(r, want))
        printf("T_TABLES: seek \"%s\" on index %d\n", key, x);
}

/* Field field of table t's buffer must read as want. */
void expect_text(eng *e, int t, const char *field, const char *want)
{
    char v[ENG_MAXVAL];

    text_of(e, t, field, v);
    if (!TF_STREQ(v, want))
        printf("T_TABLES: field %s\n", field);
}

/* FIELDS=spec of table t is refused with 102 naming field. */
static void expect_badlist(eng *e, int t, const char *spec,
                           const char *field, const char *msg)
{
    u16 idx[ENG_MAXFLIST];
    u16 n;

    if (!TF_EQ(eng_fieldlist(e, t, spec, idx, &n), ENG_EARG))
        printf("T_TABLES: FIELDS=%s\n", spec);
    TF_STREQ(eng_errfield(e), field);
    TF_STREQ(eng_errmsg(e), msg);
}

/* Nothing of an engine stays open. */
void expect_closed(void)
{
    TF_EQ(probe_handle(), g_base);
}

/* ---------------------------------------------------------------------
 * Byte checks of the created files.
 * --------------------------------------------------------------------- */

static void check_table_file(const xtable *xt)
{
    const char *p;
    xfield f;
    u8 *d;
    u16 got;
    u16 nf;
    u16 hdr;
    u16 rec;
    u16 i;
    u32 size;
    u32 count;

    size = read_file(xt->name, ".DBF", &got);
    nf = 0;
    rec = 1;
    for (p = xt->fields; next_field(&p, &f); ) {
        nf++;
        rec = (u16)(rec + f.len);
    }
    hdr = (u16)(32u + 32u * nf + 1u);
    if (!TF_CHECK(got >= hdr))
        return;
    TF_EQ(g_buf[0], xt->memo ? 0x83 : 0x03);
    TF_EQ(g_buf[1], 26);
    TF_EQ(g_buf[2], 10);
    TF_EQ(g_buf[3], 8);
    count = get_le32(g_buf + 4);
    TF_EQ(count, xt->rows);
    TF_EQ(get_le16(g_buf + 8), hdr);
    TF_EQ(get_le16(g_buf + 10), rec);
    for (i = 12; i < 32; i++)
        TF_EQ(g_buf[i], 0);
    for (i = 0, p = xt->fields; next_field(&p, &f); i++) {
        d = g_buf + 32 + 32 * i;
        TF_CHECK(memcmp(d, f.name, strlen(f.name)) == 0);
        for (got = (u16)strlen(f.name); got < 11; got++)
            TF_EQ(d[got], 0);
        TF_EQ(d[11], f.type);
        TF_EQ(get_le32(d + 12), 0);
        TF_EQ(d[16], f.len);
        TF_EQ(d[17], f.dec);
        for (got = 18; got < 32; got++)
            TF_EQ(d[got], 0);
    }
    TF_EQ(g_buf[hdr - 1], 0x0D);
    TF_EQ(size, (u32)hdr + count * rec + 1u);
}

static void check_memo_file(const char *name)
{
    u16 got;
    u16 i;
    u32 size;

    size = read_file(name, ".DBT", &got);
    TF_EQ(size, 512);
    TF_EQ(got, 512);
    TF_EQ(get_le32(g_buf), 1);
    for (i = 4; i < got && i < 512; i++)
        if (!TF_EQ(g_buf[i], 0))
            break;
}

/* The index holds entries entries in key order, each naming a record
   from 1 to entries, as the rebuild of a table of that many records
   writes them. */
static void count_entries(const xindex *xi, u32 entries)
{
    u8 key[ENG_MAXKEYLEN];
    u8 prev[ENG_MAXKEYLEN];
    ndx *x;
    u32 n;
    u32 r;

    if (!TF_EQ(ndx_open(&x, tfs_path(xi->name, ".NDX"), DBF_RDONLY),
               DBF_OK))
        return;
    n = 0;
    r = 0;
    TF_EQ(ndx_first(x, &r), DBF_OK);
    while (r != 0) {
        n++;
        TF_CHECK(r <= entries);
        TF_EQ(ndx_key(x, key), DBF_OK);
        if (n > 1)
            TF_CHECK(memcmp(prev, key, xi->keylen) <= 0);
        memcpy(prev, key, xi->keylen);
        if (ndx_next(x, &r) != DBF_OK)
            break;
    }
    if (!TF_EQ(n, entries))
        printf("T_TABLES: %s entries\n", xi->name);
    ndx_close(x);
}

static void check_index_file(const xindex *xi, u32 entries)
{
    u16 got;
    u16 entsz;
    u32 size;
    u32 root;

    size = read_file(xi->name, ".NDX", &got);
    if (!TF_CHECK(got >= 512))
        return;
    entsz = (u16)((8u + xi->keylen + 3u) / 4u * 4u);
    TF_EQ(size % 512u, 0);
    TF_EQ(get_le32(g_buf + 4), size / 512u);
    TF_EQ(get_le16(g_buf + 12), xi->keylen);
    TF_EQ(get_le16(g_buf + 14), 508u / entsz);
    TF_EQ(get_le16(g_buf + 16), 0);
    TF_EQ(get_le16(g_buf + 18), entsz);
    TF_EQ(g_buf[23], 0);
    TF_STREQ((const char *)g_buf + 24, xi->expr);
    root = get_le32(g_buf);
    if (entries <= 508u / entsz && TF_EQ(got, 1024)) {
        /* Up to one page of keys: the header and a single leaf, the
           root, holding every entry. */
        TF_EQ(size, 1024);
        TF_EQ(root, 1);
        TF_EQ(get_le16(g_buf + 512), entries);
    }
    count_entries(xi, entries);
}

/* ---------------------------------------------------------------------
 * The tests.
 * --------------------------------------------------------------------- */

/* Every field of the open engine's tables equals its schema row, and
   field numbers are found by name in any case. */
static void check_layout(eng *e)
{
    const dbf_field *got;
    dbf_field want;
    int t;
    u16 f;

    for (t = 0; t < ENG_NTABLES; t++) {
        for (f = 0; f < eng_table_def(t)->nfields; f++) {
            got = eng_field(e, t, f);
            if (!TF_CHECK(got != NULL) ||
                !TF_EQ(eng_schema_field(t, f, &want), ENG_OK))
                continue;
            TF_STREQ(got->name, want.name);
            TF_EQ(got->type, want.type);
            TF_EQ(got->len, want.len);
            TF_EQ(got->dec, want.dec);
            TF_EQ(eng_field_num(e, t, want.name), (int)f);
        }
        TF_CHECK(eng_field(e, t, f) == NULL);
    }
    TF_CHECK(eng_field(e, ENG_NTABLES, 0) == NULL);
    TF_CHECK(eng_field(e, -1, 0) == NULL);
    TF_EQ(eng_field_num(e, ENG_T_ITEMS, "loc_id"), 14);
    TF_EQ(eng_field_num(e, ENG_T_ITEMS, "LOC_I"), -1);
    TF_EQ(eng_field_num(e, ENG_T_ITEMS, "LOC_IDX"), -1);
    TF_EQ(eng_field_num(e, ENG_T_ITEMS, NULL), -1);
    TF_EQ(eng_field_num(e, ENG_NTABLES, "LOC_ID"), -1);
    TF_CHECK((eng_field_flags(ENG_T_USERS,
                              (u16)eng_field_num(e, ENG_T_USERS, "PWHASH")) &
              ENG_F_HIDDEN) != 0);
    TF_EQ(eng_field_flags(ENG_T_ITEMS,
                          (u16)eng_field_num(e, ENG_T_ITEMS, "LOC_ID")),
          ENG_F_LIST | ENG_F_AUDMOVE);
}

/* An empty directory is set up completely. */
static void test_create(void)
{
    eng *e;
    int i;
    int h;

    remove_all();
    write_file("PROBE", ".TMP", "probe");
    g_base = probe_handle();
    TF_CHECK(g_base >= 0);
    if (!expect_open(&e, ENG_OK))
        return;
    h = probe_handle();
    TF_EQ(h, g_base + ENG_NHANDLES);
    TF_STREQ(eng_errfield(e), "");
    TF_STREQ(eng_errmsg(e), "");
    for (i = 0; i < ENG_NTABLES; i++)
        TF_EQ(eng_reccount(e, i), x_tables[i].rows);
    check_layout(e);
    expect_close(e);
    expect_closed();

    for (i = 0; i < ENG_NTABLES; i++) {
        check_table_file(&x_tables[i]);
        if (x_tables[i].memo)
            check_memo_file(x_tables[i].name);
        else
            TF_CHECK(!exists(x_tables[i].name, ".DBT"));
    }
    for (i = 0; i < ENG_NINDEX; i++)
        check_index_file(&x_indexes[i],
                         x_tables[eng_index_def(i)->table].rows);
}

/* The seed rows of the schema document: the categories, then the
   location types, each table's listed by SORTORD from 1. */
#define NSEED   ((u16)(sizeof seed_code / sizeof seed_code[0]))
#define NSEEDC  10              /* the categories among them            */
static const char *const seed_code[] = {
    "SYS", "MOBO", "CPU", "CARD", "DRV", "PERI", "SW", "MAN", "CBL", "PART",
    "ROOM", "SHELF", "RACK", "CABINET", "DRAWER", "BOX", "BIN", "BUCKET"
};
static const char *const seed_name[] = {
    "Systems", "Motherboards", "Processors", "Expansion cards", "Drives",
    "Peripherals", "Software", "Manuals", "Cables", "Parts",
    "Room", "Shelf", "Rack", "Cabinet", "Drawer", "Box", "Bin", "Bucket"
};

/* The schema descriptions agree with the document. */
static void test_schema(void)
{
    const eng_tdef *td;
    const eng_xdef *xd;
    const char *p;
    char expr[NDX_MAXEXPR + 1];
    char code[ENG_LTYPELEN + 1];
    char name[ENG_CODENAMELEN + 1];
    dbf_field df;
    xfield f;
    u16 sortord;
    int t;
    int i;
    u16 j;

    for (i = 0; i < ENG_NTABLES; i++) {
        td = eng_table_def(i);
        TF_STREQ(td->name, x_tables[i].name);
        TF_EQ(td->memo, x_tables[i].memo);
        for (j = 0, p = x_tables[i].fields; next_field(&p, &f); j++) {
            if (!TF_EQ(eng_schema_field(i, j, &df), ENG_OK))
                continue;
            TF_STREQ(df.name, f.name);
            TF_EQ(df.type, f.type);
            TF_EQ(df.len, f.len);
            TF_EQ(df.dec, f.dec);
        }
        TF_EQ(td->nfields, j);
        TF_EQ(eng_schema_field(i, j, &df), ENG_EINTERNAL);
        TF_EQ(eng_field_flags(i, j), 0);
    }
    TF_EQ(eng_schema_field(ENG_NTABLES, 0, &df), ENG_EINTERNAL);
    TF_EQ(eng_schema_field(-1, 0, &df), ENG_EINTERNAL);
    TF_CHECK(eng_table_def(ENG_NTABLES) == NULL);
    TF_CHECK(eng_table_def(-1) == NULL);
    for (i = 0; i < ENG_NINDEX; i++) {
        xd = eng_index_def(i);
        TF_STREQ(xd->name, x_indexes[i].name);
        TF_EQ(eng_index_expr(i, expr, (u16)sizeof expr), ENG_OK);
        TF_STREQ(expr, x_indexes[i].expr);
        TF_EQ(xd->keylen, x_indexes[i].keylen);
    }
    TF_EQ(eng_index_expr(ENG_NINDEX, expr, (u16)sizeof expr),
          ENG_EINTERNAL);
    TF_STREQ(expr, "");
    TF_EQ(eng_index_expr(ENG_X_LOCS3, expr, 8), ENG_EINTERNAL);
    TF_EQ(eng_index_expr(ENG_X_LOCS3, expr, 9), ENG_OK);
    TF_STREQ(expr, "LOC_CODE");
    TF_CHECK(eng_index_def(ENG_NINDEX) == NULL);
    TF_EQ(eng_index_def(ENG_X_ITEMS1)->unique, 1);
    TF_EQ(eng_index_def(ENG_X_LOCS1)->unique, 1);
    TF_EQ(eng_index_def(ENG_X_LOCS3)->unique, 0);
    /* USERNAME is the second field of USERS. */
    TF_EQ(eng_index_def(ENG_X_USERS1)->unique, 2);
    TF_EQ(eng_index_def(ENG_X_ITEMS4)->unique, 0);
    TF_EQ(eng_seed_rows(), NSEED);
    for (j = 0; j < NSEED; j++) {
        eng_seed_row(j, &t, code, name, &sortord);
        TF_EQ(t, j < NSEEDC ? ENG_T_CATS : ENG_T_LTYPES);
        TF_STREQ(code, seed_code[j]);
        TF_STREQ(name, seed_name[j]);
        TF_EQ(sortord, j < NSEEDC ? j + 1 : j + 1 - NSEEDC);
    }
    eng_seed_row(NSEED, &t, code, name, &sortord);
    TF_EQ(t, -1);
    TF_STREQ(code, "");
    TF_STREQ(name, "");
    TF_EQ(sortord, 0);
}

/* Check the SEQ row of name: NEXT_VAL want. */
void check_seq(eng *e, const char *name, const char *want)
{
    char n[ENG_MAXVAL];
    u32 r;
    int found;

    found = 0;
    for (r = 1; r <= eng_reccount(e, ENG_T_SEQ); r++) {
        TF_EQ(eng_rec_read(e, ENG_T_SEQ, r), ENG_OK);
        text_of(e, ENG_T_SEQ, "SEQ_NAME", n);
        if (strcmp(n, name) != 0)
            continue;
        found++;
        expect_text(e, ENG_T_SEQ, "NEXT_VAL", want);
    }
    if (!TF_EQ(found, 1))
        printf("T_TABLES: SEQ row %s found %d times\n", name, found);
}

/* The seeded ADMIN user: field, value pairs. */
static const char *const admin_row[] = {
    "USER_ID", "1", "USERNAME", "ADMIN", "FULLNAME", "System Administrator",
    "GROUPNAME", "ADMIN", "LEVEL", "3", "SALT", "0000",
    "PWHASH", "A049CE34F45C0DB9864186CE4761D6AB", "STATUS", "A",
    "MUSTCHG", "T", "LASTLOGIN", "", "CREATED", "20261008", NULL
};

/* Check the buffer of table t against field, value pairs. */
static void check_fields(eng *e, int t, const char *const *pairs)
{
    char v[ENG_MAXVAL];

    for (; *pairs != NULL; pairs += 2) {
        text_of(e, t, pairs[0], v);
        if (!TF_STREQ(v, pairs[1]))
            printf("T_TABLES: field %s\n", pairs[0]);
    }
}

/* One I row per seed row, written by SYSTEM at the fixed clock. */
static void test_seed_audit(eng *e)
{
    char v[ENG_MAXVAL];
    char want[12];
    u32 r;

    for (r = 1; r <= NSEED + 1u; r++) {
        TF_EQ(eng_rec_read(e, ENG_T_AUDIT, r), ENG_OK);
        text_of(e, ENG_T_AUDIT, "AUD_ID", v);
        sprintf(want, "%lu", (unsigned long)r);
        TF_STREQ(v, want);
        expect_text(e, ENG_T_AUDIT, "ADATE", "20261008");
        expect_text(e, ENG_T_AUDIT, "ATIME", "12:00:00");
        expect_text(e, ENG_T_AUDIT, "STATION", ENG_SYSTEM);
        expect_text(e, ENG_T_AUDIT, "USERNAME", ENG_SYSTEM);
        expect_text(e, ENG_T_AUDIT, "ACTION", "I");
        expect_text(e, ENG_T_AUDIT, "OLDVAL", "");
        text_of(e, ENG_T_AUDIT, "TBL", v);
        if (r <= NSEEDC) {
            TF_STREQ(v, "CATS");
            expect_text(e, ENG_T_AUDIT, "REC_ID", want);
            expect_text(e, ENG_T_AUDIT, "FLD", "CAT_CODE");
            expect_text(e, ENG_T_AUDIT, "NEWVAL", seed_code[(int)(r - 1)]);
        } else if (r <= NSEED) {
            TF_STREQ(v, "LTYPES");
            sprintf(want, "%lu", (unsigned long)(r - NSEEDC));
            expect_text(e, ENG_T_AUDIT, "REC_ID", want);
            expect_text(e, ENG_T_AUDIT, "FLD", "LT_CODE");
            expect_text(e, ENG_T_AUDIT, "NEWVAL", seed_code[(int)(r - 1)]);
        } else {
            TF_STREQ(v, "USERS");
            expect_text(e, ENG_T_AUDIT, "REC_ID", "1");
            expect_text(e, ENG_T_AUDIT, "FLD", "USER_ID");
            expect_text(e, ENG_T_AUDIT, "NEWVAL", "1");
        }
    }
}

/* Lookups, field lists, actor and memo reads on the seeded directory. */
static void test_lookups(eng *e)
{
    char v[ENG_MAXVAL];
    const char *sta;
    const char *usr;
    u16 idx[ENG_MAXFLIST];
    u16 n;
    u16 len;

    /* Lookups through the indexes the seed rows were entered in. */
    expect_seek(e, ENG_X_USERS1, "ADMIN", 1);
    expect_seek(e, ENG_X_USERS1, "ADMI", 0);
    expect_seek(e, ENG_X_USERS1, "ADMINISTR", 0);
    expect_seek(e, ENG_X_AUDIT1, "USERS          1      19", 19);
    expect_seek(e, ENG_X_AUDIT1, "LTYPES         8      18", 18);
    expect_seek(e, ENG_X_AUDIT2, "SYSTEM  99999980", 19);
    expect_seek(e, ENG_X_AUDIT2, "SYSTEM  99999998", 1);
    expect_seek(e, ENG_X_AUDIT2, "ADMIN   99999980", 0);
    expect_seek(e, ENG_X_LOCS1, "       1", 0);
    expect_seek(e, ENG_X_LOCS3, "A1", 0);

    /* Field lists. */
    TF_EQ(eng_fieldlist(e, ENG_T_ITEMS, NULL, idx, &n), ENG_OK);
    TF_EQ(n, 8);
    TF_EQ(idx[0], 0);
    TF_EQ(idx[4], 8);
    TF_EQ(idx[5], 9);
    TF_EQ(idx[7], 21);
    TF_EQ(eng_fieldlist(e, ENG_T_USERS, "", idx, &n), ENG_OK);
    TF_EQ(n, 7);
    TF_EQ(eng_fieldlist(e, ENG_T_CATS, "sortord,CAT_CODE", idx, &n),
          ENG_OK);
    TF_EQ(n, 2);
    TF_EQ(idx[0], 2);
    TF_EQ(idx[1], 0);
    TF_EQ(eng_fieldlist(e, ENG_NTABLES, NULL, idx, &n), ENG_EINTERNAL);
    TF_EQ(n, 0);
    expect_badlist(e, ENG_T_USERS, "USERNAME,PWHASH", "PWHASH",
                   "no such field");
    expect_badlist(e, ENG_T_ITEMS, "MFR,notes", "notes",
                   "memo fields cannot be listed");
    expect_badlist(e, ENG_T_ITEMS, "BOGUS", "BOGUS", "no such field");
    expect_badlist(e, ENG_T_ITEMS, "MFR,", "FIELDS", "empty field name");
    expect_badlist(e, ENG_T_ITEMS, "DESCR,TAGS,MFR,MODEL", "FIELDS",
                   "fields too wide");
    expect_badlist(e, ENG_T_ITEMS,
                   "QTY,QTY,QTY,QTY,QTY,QTY,QTY,QTY,QTY,QTY,QTY", "FIELDS",
                   "field list too long");
    eng_clear_error(e);
    TF_STREQ(eng_errfield(e), "");
    TF_STREQ(eng_errmsg(e), "");

    /* Actor names are cut to their fields. */
    eng_set_actor(e, "STATION-NAME-LONGER", "OPERATOR9");
    eng_actor(e, &sta, &usr);
    TF_STREQ(sta, "STATION-NAME-LON");
    TF_STREQ(usr, "OPERATOR");
    eng_today(e, v);
    TF_STREQ(v, "20261008");

    /* A memo read of a field that is not a memo is refused. */
    TF_EQ(eng_rec_read(e, ENG_T_CATS, 1), ENG_OK);
    TF_EQ(eng_memo_read(e, ENG_T_CATS, 0, &len), ENG_EINTERNAL);
}

/* The seed rows and their audit. */
static void test_seed(void)
{
    char v[ENG_MAXVAL];
    char want[12];
    const char *sta;
    const char *usr;
    eng *e;
    u32 r;

    if (!expect_open(&e, ENG_OK))
        return;
    eng_actor(e, &sta, &usr);
    TF_STREQ(sta, "");
    TF_STREQ(usr, "");
    for (r = 1; r <= NSEEDC; r++) {
        TF_EQ(eng_rec_read(e, ENG_T_CATS, r), ENG_OK);
        TF_CHECK(!dbf_is_deleted(eng_dbf(e, ENG_T_CATS)));
        expect_text(e, ENG_T_CATS, "CAT_CODE", seed_code[(int)(r - 1)]);
        expect_text(e, ENG_T_CATS, "CAT_NAME", seed_name[(int)(r - 1)]);
        text_of(e, ENG_T_CATS, "SORTORD", v);
        sprintf(want, "%lu", (unsigned long)r);
        TF_STREQ(v, want);
    }
    for (r = 1; r <= NSEED - NSEEDC; r++) {
        TF_EQ(eng_rec_read(e, ENG_T_LTYPES, r), ENG_OK);
        expect_text(e, ENG_T_LTYPES, "LT_CODE",
                    seed_code[(int)(NSEEDC + r - 1)]);
        expect_text(e, ENG_T_LTYPES, "LT_NAME",
                    seed_name[(int)(NSEEDC + r - 1)]);
        text_of(e, ENG_T_LTYPES, "SORTORD", v);
        sprintf(want, "%lu", (unsigned long)r);
        TF_STREQ(v, want);
    }

    TF_EQ(eng_rec_read(e, ENG_T_USERS, 1), ENG_OK);
    check_fields(e, ENG_T_USERS, admin_row);

    check_seq(e, "ITEMS", "1");
    check_seq(e, "LOCS", "1");
    check_seq(e, "MAINT", "1");
    check_seq(e, "LOANS", "1");
    check_seq(e, "AUDIT", "20");
    check_seq(e, "USERS", "2");
    test_seed_audit(e);
    test_lookups(e);
    TF_EQ(eng_flush(e), ENG_OK);
    expect_close(e);
}

/* Reopening a set-up directory writes nothing.  Leftovers of a pack and
   a reindex that never reached their commit point are removed first. */
static void test_reopen(void)
{
    eng *e;
    int i;

    write_file("LOCS1", ".$NX", "partial");
    write_file("LOCS1", ".#NX", "scratch");
    write_file("ITEMS", ".$DB", "partial");
    write_file("ITEMS", ".$DT", "partial");
    if (!expect_open(&e, ENG_OK))
        return;
    TF_CHECK(!exists("LOCS1", ".$NX"));
    TF_CHECK(!exists("LOCS1", ".#NX"));
    TF_CHECK(!exists("ITEMS", ".$DB"));
    TF_CHECK(!exists("ITEMS", ".$DT"));
    for (i = 0; i < ENG_NTABLES; i++)
        TF_EQ(eng_reccount(e, i), x_tables[i].rows);
    check_seq(e, "AUDIT", "20");
    check_seq(e, "USERS", "2");
    expect_close(e);
}

int main(int argc, char **argv)
{
    tf_begin("T_TABLES");
    if (argc < 2) {
        printf("usage: T_TABLES dir [SHARE]\n");
        return 2;
    }
    tfs_set_dir(argv[1]);
    g_buf = (u8 *)malloc(HDRMAX);
    if (g_buf == NULL) {
        printf("T_TABLES: out of memory\n");
        return 2;
    }
    test_schema();
    test_create();
    test_seed();
    test_reopen();
    test_rebuild_and_heal();
    test_mismatch();
    test_edit_marker();
    test_marker_kept();
    test_index_then_fail();
    test_create_fails();
    test_torn_append();
    test_lone_memo();
    test_kept_repairs();
    test_long_markers();
    test_items_status();
    test_seq_moved();
    if (argc > 2 && strcmp(argv[2], "SHARE") == 0)
        test_in_use();
    test_handles();
    test_codes_emptied();
    remove_file("PROBE", ".TMP");
    free(g_buf);
    return tf_done();
}
