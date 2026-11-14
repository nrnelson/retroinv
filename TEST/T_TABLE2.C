/*
 * T_TABLE2.C - the second half of the T_TABLES test (see T_TABLES.C):
 * mismatched layouts and indexes, schema rows that cannot be copied,
 * the edit marker, failed creates, torn appends, lone memo files, files
 * in use, handles, the markers kept for the repairs of eng_init_data,
 * STATUS D, and SEQ rows that move.  It is a file of its own only to
 * stay within the compiler memory of Watcom C 6.5; T_TABLES.H holds
 * what both share.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGSCH.H"
#include "ENGID.H"
#include "ENGCODE.H"
#include "ENGLOCS.H"
#include "ENGITEM.H"
#define TF_PART
#define TF_FILE "T_TABLE2"
#include "TESTFW.H"
#include "T_FILES.H"
#include "T_TABLES.H"

/* Write a CATS.DBF whose SORTORD is one wider than the schema's. */
static void bad_cats(void)
{
    dbf_field f[3];

    strcpy(f[0].name, "CAT_CODE");
    f[0].type = 'C';
    f[0].len = 4;
    f[0].dec = 0;
    strcpy(f[1].name, "CAT_NAME");
    f[1].type = 'C';
    f[1].len = 20;
    f[1].dec = 0;
    strcpy(f[2].name, "SORTORD");
    f[2].type = 'N';
    f[2].len = 3;
    f[2].dec = 0;
    TF_EQ(dbf_create(tfs_path("CATS", ".DBF"), f, 3), DBF_OK);
}

/* A table not as the schema, or with its memo file missing, is 300. */
static void test_mismatch_table(void)
{
    eng *e;

    bad_cats();
    expect_open(&e, ENG_EIO);
    expect_why("CATS.DBF");
    expect_closed();
    remove_file("CATS", ".DBF");

    /* A missing memo file is never recreated. */
    TF_EQ(cf_rename(tfs_path("LOCS", ".DBT"), tfs_path("LOCS", ".OLD")), 0);
    expect_open(&e, ENG_EIO);
    expect_why("LOCS.DBT");
    TF_CHECK(!exists("LOCS", ".DBT"));
    expect_closed();
    TF_EQ(cf_rename(tfs_path("LOCS", ".OLD"), tfs_path("LOCS", ".DBT")), 0);

    /* The recreated CATS.DBF is seeded again, and the LOCS3.NDX written
       with a lower-case expression is rebuilt as the schema's. */
    remove_file("LOCS3", ".NDX");
    if (expect_open(&e, ENG_OK)) {
        TF_STREQ(ndx_expr(eng_ndx(e, ENG_X_LOCS3)), "LOC_CODE");
        TF_EQ(eng_reccount(e, ENG_T_CATS), 10);
        TF_EQ(eng_reccount(e, ENG_T_AUDIT), 29);
        check_seq(e, "AUDIT", "30");
        expect_close(e);
    }
}

/* The fault hook of the far schema copies: they are counted from 1, and
   the g_failat-th fails (0: none). */
static u16 g_copied;
static u16 g_failat;

static int fail_copy(void)
{
    return ++g_copied == g_failat;
}

/* eng_open_files with the failat-th schema copy failing (0: none). */
static int open_copying(eng **e, u16 failat)
{
    int rc;

    g_copied = 0;
    g_failat = failat;
    eng_schema_fault = fail_copy;
    rc = eng_open_files(e, tfs_dir(), NULL, g_why, (u16)sizeof g_why);
    eng_schema_fault = NULL;
    return rc;
}

/* A schema row that cannot be copied out of far data stops the open
   with 500 "schema error": the last key expression, the first field of
   a table checked, and the first field of a table being created, after
   which the next open creates the table. */
static void test_schema_copy(void)
{
    eng *e;
    u16 total;

    if (TF_EQ(open_copying(&e, 0), ENG_OK))
        expect_close(e);
    total = g_copied;
    TF_CHECK(total > ENG_NINDEX);
    TF_EQ(open_copying(&e, total), ENG_EINTERNAL);
    expect_why(eng_index_def(ENG_NINDEX - 1)->name);
    expect_why("schema error");
    expect_closed();
    TF_EQ(open_copying(&e, 1), ENG_EINTERNAL);
    expect_why(eng_table_def(0)->name);
    expect_why("schema error");
    expect_closed();
    remove_file("CATS", ".DBF");
    TF_EQ(open_copying(&e, 1), ENG_EINTERNAL);
    expect_why("CATS");
    expect_why("schema error");
    TF_CHECK(!exists("CATS", ".DBF"));
    expect_closed();
    if (expect_open(&e, ENG_OK)) {
        TF_EQ(eng_reccount(e, ENG_T_CATS), 10);
        expect_close(e);
    }
}

/* eng_open with CATS.DBF removed, so that it seeds CATS, and the
   failat-th schema copy failing (0: none). */
static int open_seeding(eng **e, u16 failat)
{
    int rc;

    remove_file("CATS", ".DBF");
    g_copied = 0;
    g_failat = failat;
    eng_schema_fault = fail_copy;
    rc = eng_open(e, tfs_dir(), NULL, g_why, (u16)sizeof g_why);
    eng_schema_fault = NULL;
    return rc;
}

/* CATS.DBF, read raw: n records, record r the seed row r - 1, its code
   and name neither blank. */
static void expect_cats(u16 n)
{
    char code[ENG_LTYPELEN + 1];
    char name[ENG_CODENAMELEN + 1];
    char got[ENG_CODENAMELEN + 1];
    dbf *t;
    u16 r;
    u16 sortord;
    int st;

    if (!TF_EQ(dbf_open(&t, tfs_path("CATS", ".DBF"), DBF_RDONLY), DBF_OK))
        return;
    if (TF_EQ(dbf_reccount(t), n))
        for (r = 1; r <= n && TF_EQ(dbf_read(t, r), DBF_OK); r++) {
            eng_seed_row((u16)(r - 1), &st, code, name, &sortord);
            dbf_get_str(t, ENG_FIELD(CATS, CAT_CODE), got,
                        (u16)sizeof got);
            TF_CHECK(got[0] != '\0');
            TF_STREQ(got, code);
            dbf_get_str(t, ENG_FIELD(CATS, CAT_NAME), got,
                        (u16)sizeof got);
            TF_CHECK(got[0] != '\0');
            TF_STREQ(got, name);
        }
    TF_EQ(dbf_close(t), DBF_OK);
}

/* A seed row that cannot be copied stops the open with 500 before its
   row is written; the rows of the seeds before it stay, none blank:
   none when the first seed's code fails, five when the sixth seed's
   code or name does, nine when the last category's name does, all ten
   when the last seed's name does (of LTYPES, which is not seeded).  A
   CATS left so is not seeded again; one made anew is seeded whole. */
static void test_seed_copy(void)
{
    eng *e;
    u16 total;
    u16 first;

    if (TF_EQ(open_seeding(&e, 0), ENG_OK))
        expect_close(e);
    total = g_copied;
    TF_CHECK(total > 2 * eng_seed_rows());
    /* The copies of seed i, code then name, are first + 2i and the
       next. */
    first = (u16)(total - 2 * eng_seed_rows() + 1);
    TF_EQ(open_seeding(&e, first), ENG_EINTERNAL);
    expect_closed();
    expect_cats(0);
    TF_EQ(open_seeding(&e, (u16)(first + 2 * 5)), ENG_EINTERNAL);
    expect_closed();
    expect_cats(5);
    if (expect_open(&e, ENG_OK)) {
        TF_EQ(eng_reccount(e, ENG_T_CATS), 5);
        expect_close(e);
    }
    TF_EQ(open_seeding(&e, (u16)(first + 2 * 5 + 1)), ENG_EINTERNAL);
    expect_closed();
    expect_cats(5);
    TF_EQ(open_seeding(&e, (u16)(first + 2 * 9 + 1)), ENG_EINTERNAL);
    expect_closed();
    expect_cats(9);
    TF_EQ(open_seeding(&e, total), ENG_EINTERNAL);
    expect_closed();
    expect_cats(10);
    if (TF_EQ(open_seeding(&e, 0), ENG_OK)) {
        TF_EQ(eng_reccount(e, ENG_T_CATS), 10);
        expect_close(e);
    }
}

/* An index not as the schema, a LOANS2.NDX of the older expression
   among them, is 301, a table not as the schema or with its memo file
   missing 300; deleting the index lets it be rebuilt. */
void test_mismatch(void)
{
    eng *e;

    TF_EQ(ndx_create(tfs_path("LOCS3", ".NDX"), "UPPER(LOC_CODE)", 10),
          DBF_OK);
    expect_open(&e, ENG_EINDEX);
    TF_CHECK(e == NULL);
    expect_why("LOCS3.NDX");
    expect_closed();

    TF_EQ(ndx_create(tfs_path("LOCS3", ".NDX"), "LOC_CODE", 11), DBF_OK);
    expect_open(&e, ENG_EINDEX);
    expect_closed();

    /* Case and trailing blanks of the stored expression do not matter,
       as dBASE keeps a trailing space. */
    TF_EQ(ndx_create(tfs_path("LOCS3", ".NDX"), "loc_code ", 10), DBF_OK);
    if (expect_open(&e, ENG_OK))
        expect_close(e);

    write_file("USERS1", ".NDX", "not an index");
    expect_open(&e, ENG_EINDEX);
    expect_why("USERS1.NDX");
    expect_closed();
    remove_file("USERS1", ".NDX");
    if (expect_open(&e, ENG_OK)) {
        TF_CHECK(e != NULL);
        expect_close(e);
    }

    /* A LOANS2.NDX of the older expression is 301 until it is deleted
       and rebuilt. */
    TF_EQ(ndx_create(tfs_path("LOANS2", ".NDX"),
                     "STR(YEAR(DUE_DATE),4)+"
                     "RIGHT(STR(MONTH(DUE_DATE)+100,3),2)+"
                     "RIGHT(STR(DAY(DUE_DATE)+100,3),2)+STR(LOAN_ID,8)", 16),
          DBF_OK);
    expect_open(&e, ENG_EINDEX);
    expect_why("LOANS2.NDX");
    expect_closed();
    remove_file("LOANS2", ".NDX");
    if (expect_open(&e, ENG_OK)) {
        TF_EQ(ndx_keylen(eng_ndx(e, ENG_X_LOANS2)), 20);
        TF_CHECK(strncmp(ndx_expr(eng_ndx(e, ENG_X_LOANS2)),
                         "STR(YEAR(RET_DATE),4)+", 22) == 0);
        expect_close(e);
    }
    expect_closed();

    test_mismatch_table();
    test_schema_copy();
    test_seed_copy();
}

/* The key of a LOCS3 entry for a record LOCS.DBF does not hold. */
static const char k_stale[] = "ZZ9       ";

/* Add a LOCS3 entry for a record LOCS.DBF does not hold, as an edit cut
   off between its writes could leave it. */
static void stale_locs_entry(void)
{
    ndx *x;

    if (!TF_EQ(ndx_open(&x, tfs_path("LOCS3", ".NDX"), DBF_RDWR), DBF_OK))
        return;
    TF_EQ(ndx_insert(x, (const u8 *)k_stale, 7), DBF_OK);
    TF_EQ(ndx_close(x), DBF_OK);
}

/* LOCS3 holds the stale entry (1) or not (0). */
static void check_locs3(eng *e, int stale)
{
    u32 r;
    int exact;

    TF_EQ(ndx_seek(eng_ndx(e, ENG_X_LOCS3), (const u8 *)k_stale,
                   (u16)(sizeof k_stale - 1), &r, &exact), DBF_OK);
    TF_EQ(exact, stale);
}

/* Edit-marker cases, run in order on a LOCS3 that holds a stale entry
   (set up when add is 1): the marker's content, and whether LOCS3 is
   rebuilt.  A marker naming LOCS rebuilds it; one naming only other
   tables (AUDIT included, which names its own indexes alone), after a
   count line or none, leaves it; an unknown name, a malformed line (a
   NUL, a bare CR LF, CR CR LF, no CR LF), a count line not first, not
   ended by CR LF or over a u32, a marker too long to be one, or an
   empty marker rebuilds every index. */
typedef struct {
    const char *text;
    u16 len;                    /* bytes of text; 0: up to its NUL      */
    int add;
    int rebuilt;
} xmark;

/* Long markers: g_long is filled with letters before the cases run,
   the first of them one byte longer than the longest marker an open
   reads (ENG_MARKMAX); mark_of then writes valid ones into it. */
#define LONGMARK        300     /* bytes of g_long                      */
static char g_long[LONGMARK];

static const xmark x_marks[] = {
    { "LOCS\r\n", 0, 0, 1 },
    { "locs\r\n", 0, 1, 1 },
    { "CATS\r\n", 0, 1, 0 },
    { "AUDIT\r\n", 0, 0, 0 },
    { "", 0, 0, 1 },
    { "CATS\r\nLOCS\r\n", 0, 1, 1 },
    { "CATS\r\nUSERS\r\nCATS\r\n", 0, 1, 0 },
    { "CATS\r\nLOCS", 0, 0, 1 },
    { "ITEMSITEMS\r\n", 0, 1, 1 },
    { "CATS\n", 0, 1, 1 },
    { "CA\0S\r\n", 6, 1, 1 },
    { g_long, ENG_MARKMAX + 1, 1, 1 },
    { g_long, LONGMARK, 1, 1 },
    { "\r\n", 0, 1, 1 },
    { "CATS\r\r\n", 0, 1, 1 },
    { "5\r\nCATS\r\n", 0, 1, 0 },
    { "5\r\n", 0, 0, 0 },
    { "4294967295\r\nCATS\r\n", 0, 0, 0 },
    { "4294967296\r\nCATS\r\n", 0, 0, 1 },
    { "5\nCATS\r\n", 0, 1, 1 },
    { "CATS\r\n5\r\n", 0, 1, 1 },
    { NULL, 0, 0, 0 }
};

/* Into g_long, a valid marker of exactly n bytes (n at least 35 more
   than the length of first): the line first, then AUDIT and CATS
   lines. */
static void mark_of(const char *first, u16 n)
{
    u16 a;

    strcpy(g_long, first);
    for (a = (u16)((n - strlen(first)) % 6u); a > 0; a--)
        strcat(g_long, "AUDIT\r\n");
    while (strlen(g_long) < n)
        strcat(g_long, "CATS\r\n");
}

/* The edit marker makes the open rebuild the indexes of the tables it
   names and those of AUDIT, and is removed. */
void test_edit_marker(void)
{
    const xmark *m;
    eng *e;
    u32 r;
    int i;

    stale_locs_entry();
    if (expect_open(&e, ENG_OK)) {
        check_locs3(e, 1);
        expect_close(e);
    }
    memset(g_long, 'L', LONGMARK);
    for (m = x_marks; m->text != NULL; m++) {
        if (m->add)
            stale_locs_entry();
        write_bytes(ENG_EDITMARK, "", m->text,
                    m->len != 0 ? m->len : (u16)strlen(m->text));
        if (!expect_open(&e, ENG_OK))
            continue;
        if (!TF_CHECK(!exists(ENG_EDITMARK, "")))
            printf("T_TABLES: marker case %d\n", (int)(m - x_marks));
        check_locs3(e, !m->rebuilt);
        expect_close(e);
    }
    /* A valid marker of ENG_MARKMAX bytes naming CATS and AUDIT is read;
       one byte longer, it counts as unreadable. */
    stale_locs_entry();
    for (i = 0; i < 2; i++) {
        mark_of("", (u16)(ENG_MARKMAX + i));
        write_bytes(ENG_EDITMARK, "", g_long, (u16)strlen(g_long));
        if (!expect_open(&e, ENG_OK))
            continue;
        TF_CHECK(!exists(ENG_EDITMARK, ""));
        check_locs3(e, i == 0);
        expect_close(e);
    }
    if (expect_open(&e, ENG_OK)) {
        TF_EQ(ndx_last(eng_ndx(e, ENG_X_AUDIT1), &r), DBF_OK);
        TF_CHECK(r != 0);
        expect_seek(e, ENG_X_ITEMS1, "      57", 2);
        TF_STREQ(eng_dir(e), tfs_dir());
        expect_close(e);
    }
    expect_closed();
}

/* An index the marker names is built afresh, never from its old file,
   and so are the indexes of AUDIT under any marker: a damaged LOCS3.NDX
   and AUDIT2.NDX are replaced and the marker removed.  A marker
   whose open fails stays for the next one: here CATS.DBF is not as the
   schema. */
void test_marker_kept(void)
{
    eng *e;
    u16 got;

    write_file(ENG_EDITMARK, "", "LOCS\r\n");
    write_file("LOCS3", ".NDX", "not an index");
    write_file("AUDIT2", ".NDX", "not an index");
    if (expect_open(&e, ENG_OK)) {
        TF_CHECK(!exists(ENG_EDITMARK, ""));
        expect_seek(e, ENG_X_AUDIT2, "SYSTEM  99999998", 1);
        TF_STREQ(ndx_expr(eng_ndx(e, ENG_X_LOCS3)), "LOC_CODE");
        check_locs3(e, 0);
        expect_close(e);
    }
    write_file(ENG_EDITMARK, "", "LOCS\r\n");
    bad_cats();
    expect_open(&e, ENG_EIO);
    read_file(ENG_EDITMARK, "", &got);
    TF_EQ(got, 6);
    expect_closed();
    remove_file("CATS", ".DBF");
    if (expect_open(&e, ENG_OK)) {
        TF_CHECK(!exists(ENG_EDITMARK, ""));
        expect_close(e);
    }
}

/* An index created beside an existing table is announced in the marker
   first, so an open that fails before the rebuild (here on a CATS.DBF
   not as the schema) leaves the rebuild to the next one instead of an
   empty USERS1 that nobody could log in through. */
void test_index_then_fail(void)
{
    static const char k_cats_users[] = "CATS\r\nUSERS\r\n";
    eng *e;
    u16 got;

    /* The line is appended to a marker already there, and only once. */
    write_file(ENG_EDITMARK, "", "CATS\r\n");
    remove_file("USERS1", ".NDX");
    bad_cats();
    expect_open(&e, ENG_EIO);
    expect_why("CATS.DBF");
    expect_closed();
    expect_open(&e, ENG_EIO);
    read_file(ENG_EDITMARK, "", &got);
    TF_EQ(got, sizeof k_cats_users - 1);
    TF_CHECK(memcmp(g_buf, k_cats_users, sizeof k_cats_users - 1) == 0);
    remove_file("CATS", ".DBF");
    if (expect_open(&e, ENG_OK)) {
        expect_seek(e, ENG_X_USERS1, "ADMIN", 1);
        TF_CHECK(!exists(ENG_EDITMARK, ""));
        expect_close(e);
    }

    /* Nothing is added to a marker longer than ENG_MARKMAX, which
       already names every table. */
    memset(g_long, 'X', ENG_MARKMAX + 1);
    write_bytes(ENG_EDITMARK, "", g_long, ENG_MARKMAX + 1);
    remove_file("USERS1", ".NDX");
    bad_cats();
    expect_open(&e, ENG_EIO);
    TF_EQ(read_file(ENG_EDITMARK, "", &got), ENG_MARKMAX + 1);
    remove_file("CATS", ".DBF");
    if (expect_open(&e, ENG_OK)) {
        expect_seek(e, ENG_X_USERS1, "ADMIN", 1);
        TF_CHECK(!exists(ENG_EDITMARK, ""));
        expect_close(e);
    }
}

/* Fault hook: every write after the third create fails as on a full
   disk.  Creating ITEMS creates the marker naming it, then ITEMS.DBT
   and ITEMS.DBF under their scratch names. */
static int g_creates;

static int fail_after_third_create(int op)
{
    if (op == CF_OP_CREATE)
        g_creates++;
    if (op == CF_OP_WRITE && g_creates >= 3)
        return CF_EFULL;
    return 0;
}

/* 1 when the marker is the count line of count audit records and then
   the table lines tables, else 0 after printing what it holds. */
static int marker_is(u32 count, const char *tables)
{
    char want[ENG_MARKMAX + 1];
    u16 got;

    sprintf(want, "%lu\r\n", (unsigned long)count);
    strcat(want, tables);
    read_file(ENG_EDITMARK, "", &got);
    if (got == strlen(want) && memcmp(g_buf, want, got) == 0)
        return 1;
    printf("T_TABLES: marker %.*s, want %s\n", (int)got, (char *)g_buf,
           want);
    return 0;
}

/* A new directory is seeded in one batch: one marker create and one
   remove for the seed rows of the code tables and ADMIN.  A stop during
   the seeding leaves the marker, and the next open completes the
   seeding.  ADMIN alone, when USERS.DBF is gone, is written in the same
   kind of batch, begun on CATS. */
static void seed_batch(void)
{
    eng *e;
    u32 total;
    u32 aud;
    int i;

    total = 0;
    for (i = 0; i < 2; i++) {
        remove_all();
        if (!TF_EQ(eng_open_files(&e, tfs_dir(), NULL, g_why,
                                  (u16)sizeof g_why), ENG_OK))
            return;
        tfs_fault(TFS_ALLOPS, i == 0 ? 0 : total / 2, 0, 0);
        TF_EQ(eng_init_data(e) == ENG_OK, i == 0);
        eng_close(e);
        tfs_fault_off();
        if (i == 0) {
            total = tfs_calls();
            TF_EQ(tfs_count(CF_OP_CREATE), 1);
            TF_EQ(tfs_count(CF_OP_REMOVE), 1);
        } else {
            TF_CHECK(exists(ENG_EDITMARK, ""));
        }
        expect_closed();
        if (expect_open(&e, ENG_OK)) {
            TF_EQ(eng_reccount(e, ENG_T_CATS), 10);
            TF_EQ(eng_reccount(e, ENG_T_USERS), 1);
            TF_CHECK(!exists(ENG_EDITMARK, ""));
            expect_close(e);
        }
    }
    remove_file("USERS", ".DBF");
    if (!TF_EQ(eng_open_files(&e, tfs_dir(), NULL, g_why,
                              (u16)sizeof g_why), ENG_OK))
        return;
    aud = eng_reccount(e, ENG_T_AUDIT);
    tfs_fault(TFS_OP(CF_OP_REMOVE), 1, 0, CF_EACCES);
    TF_EQ(eng_init_data(e), ENG_OK);
    tfs_fault_off();
    TF_EQ(eng_reccount(e, ENG_T_USERS), 1);
    expect_close(e);
    TF_CHECK(marker_is(aud, "CATS\r\nUSERS\r\n"));
    if (expect_open(&e, ENG_OK))
        expect_close(e);
}

/* A table whose .DBF cannot be written leaves no .DBF behind, only its
   scratch file, so the next open removes that and creates the table
   instead of refusing a damaged one. */
void test_create_fails(void)
{
    eng *e;

    remove_file("ITEMS", ".DBF");
    remove_file("ITEMS", ".DBT");
    g_creates = 0;
    cf_fault = fail_after_third_create;
    expect_open(&e, ENG_EFULL);
    cf_fault = NULL;
    expect_why("ITEMS.&DB");
    TF_CHECK(!exists("ITEMS", ".DBF"));
    TF_CHECK(!exists("ITEMS", ".DBT"));
    expect_closed();
    if (expect_open(&e, ENG_OK)) {
        TF_CHECK(!exists("ITEMS", ".&DB"));
        TF_CHECK(!exists("ITEMS", ".&DT"));
        TF_CHECK(!exists(ENG_EDITMARK, ""));
        TF_EQ(eng_reccount(e, ENG_T_ITEMS), 0);
        expect_seek(e, ENG_X_ITEMS1, "      57", 0);
        expect_close(e);
    }
    seed_batch();
}

/* Append an uncounted record after the last record of stem.DBF, as
   tfs_add_tail does; the size before is returned. */
static u32 add_tail(const char *stem)
{
    u32 size;

    size = tfs_size(tfs_path(stem, ".DBF"));
    TF_EQ(tfs_add_tail(stem), 0);
    return size;
}

/* Open and close the engine; the size of stem.DBF afterwards. */
static u32 size_after_open(const char *stem)
{
    eng *e;
    u16 got;

    if (expect_open(&e, ENG_OK))
        expect_close(e);
    return read_file(stem, ".DBF", &got);
}

/* Bytes after the last counted record: a table the marker names, and
   SEQ, lose them at the next open; any other table keeps them (another
   program may have been appending), and its next append writes over
   them. */
void test_torn_append(void)
{
    static const eng_pair name = { "CAT_NAME", "Torn" };
    eng *e;
    u32 size;
    u32 count;
    u16 got;

    if (!expect_open(&e, ENG_OK))
        return;
    count = eng_reccount(e, ENG_T_CATS);
    expect_close(e);
    size = add_tail("CATS");
    TF_EQ(size_after_open("CATS"), size + 27u);
    if (expect_open(&e, ENG_OK)) {
        TF_EQ(eng_reccount(e, ENG_T_CATS), count);
        eng_set_actor(e, "T_TABLES", "TEST");
        TF_EQ(eng_code_add(e, ENG_T_CATS, "TORN", &name, 1), ENG_OK);
        TF_EQ(eng_reccount(e, ENG_T_CATS), count + 1u);
        expect_close(e);
    }
    /* The header, count + 1 records of 27 bytes and the 0x1A. */
    size = read_file("CATS", ".DBF", &got);
    TF_EQ(size, 129u + (count + 1u) * 27u + 1u);
    TF_EQ(get_le32(g_buf + 4), count + 1u);

    size = add_tail("CATS");
    write_file(ENG_EDITMARK, "", "CATS\r\n");
    TF_EQ(size_after_open("CATS"), size);
    TF_CHECK(!exists(ENG_EDITMARK, ""));
    size = add_tail("SEQ");
    TF_EQ(size_after_open("SEQ"), size);
}

/* A .DBT without its .DBF is replaced when it is an empty memo file or
   the marker names its table; one holding memos is refused. */
void test_lone_memo(void)
{
    eng *e;
    u16 i;

    remove_file("LOCS", ".DBF");
    if (expect_open(&e, ENG_OK)) {
        TF_EQ(eng_reccount(e, ENG_T_LOCS), 0);
        expect_close(e);
    }
    remove_file("LOCS", ".DBF");
    for (i = 0; i < 1024; i++)
        g_buf[i] = 0;
    put_le32(g_buf, 2);
    memcpy(g_buf + 512, "a memo\x1A\x1A", 8);
    write_bytes("LOCS", ".DBT", (const char *)g_buf, 520);
    expect_open(&e, ENG_EIO);
    expect_why("LOCS.DBT: memo file without its table");
    TF_CHECK(!exists("LOCS", ".DBF"));
    expect_closed();
    write_file(ENG_EDITMARK, "", "LOCS\r\n");
    if (expect_open(&e, ENG_OK)) {
        TF_EQ(eng_reccount(e, ENG_T_LOCS), 0);
        expect_close(e);
    }
    TF_EQ(read_file("LOCS", ".DBT", &i), 512);
}

/* Files held deny-read/write by another handle (SHARE loaded). */
void test_in_use(void)
{
    eng *e;
    eng *e2;
    int h;

    if (!TF_EQ(cf_open(tfs_path("AUDIT", ".DBF"), CF_RDWR | CF_DENYRW, &h),
               0))
        return;
    expect_open(&e, ENG_EINUSE);
    expect_why("AUDIT.DBF");
    cf_close(h);
    expect_closed();

    if (!expect_open(&e, ENG_OK))
        return;
    expect_open(&e2, ENG_EINUSE);
    expect_close(e);
    expect_closed();
}

/* With ENG_SPAREHANDLES handles of the program's own held, the engine
   still flushes, commits (which writes and removes the edit marker) and
   opens a directory whose USERS1 must be rebuilt, the largest number of
   files it holds at once: its transient handles are part of
   ENG_PROCHANDLES. */
void test_handles(void)
{
    static const eng_pair h1[] = {
        { "LOC_NAME", "H1" }, { "LOC_CODE", "H1" }, { "LTYPE", "BIN" }
    };
    int h[ENG_SPAREHANDLES];
    eng *e;
    u32 id;
    int n;
    int rc;

    if (!expect_open(&e, ENG_OK))
        return;
    rc = 0;
    for (n = 0; n < ENG_SPAREHANDLES; n++) {
        rc = cf_open(tfs_path("PROBE", ".TMP"), CF_READ | CF_DENYNONE,
                     &h[n]);
        if (rc != 0)
            break;
    }
    TF_EQ(rc, 0);
    TF_EQ(n, ENG_SPAREHANDLES);
    TF_EQ(eng_flush(e), ENG_OK);
    eng_set_actor(e, "T_TABLES", "TEST");
    TF_EQ(eng_loc_add(e, h1, sizeof h1 / sizeof h1[0], &id), ENG_OK);
    TF_EQ(id, 1);
    TF_CHECK(!exists(ENG_EDITMARK, ""));
    expect_close(e);
    remove_file("USERS1", ".NDX");
    if (expect_open(&e, ENG_OK)) {
        expect_seek(e, ENG_X_LOCS3, "H1", 1);
        expect_seek(e, ENG_X_USERS1, "ADMIN", 1);
        expect_close(e);
    }
    while (n > 0)
        cf_close(h[--n]);
    expect_closed();
}

/* Open the files with marker text m (n bytes; 0: its length) written
   first.  want NULL: the open removes the marker; else it keeps it,
   rewritten as the count line of from (MARK_ALL: every AUDIT record)
   and the table lines want, and eng_marker_from gives from.
   eng_repaired names AUDIT whenever there was one. */
#define MARK_ALL        0xFFFFFFFFUL
static eng *open_marked(const char *m, u16 n, const char *want, u32 from)
{
    eng *e;

    write_bytes(ENG_EDITMARK, "", m, n != 0 ? n : (u16)strlen(m));
    if (!TF_EQ(eng_open_files(&e, tfs_dir(), NULL, g_why,
                              (u16)sizeof g_why), ENG_OK))
        return NULL;
    TF_EQ(eng_marker_kept(e), want != NULL);
    TF_EQ(exists(ENG_EDITMARK, ""), want != NULL);
    if (from == MARK_ALL)
        from = eng_reccount(e, ENG_T_AUDIT);
    if (want != NULL) {
        TF_CHECK(marker_is(from, want));
        TF_EQ(eng_marker_from(e), from);
    }
    TF_EQ(eng_repaired(e, ENG_T_AUDIT), 1);
    return e;
}

/* 1 when the last AUDIT record of e is the X row flagging the records
   after from up to record last; else 0 after printing it. */
static int cut_is(eng *e, u32 from, u32 last)
{
    char want[ENG_MAXVAL];
    char got[ENG_MAXVAL];
    i32 first;

    if (eng_rec_read(e, ENG_T_AUDIT, from + 1) != ENG_OK)
        return 0;
    first = eng_rec_num(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, AUD_ID));
    if (eng_rec_read(e, ENG_T_AUDIT, last) != ENG_OK)
        return 0;
    sprintf(want, "AUDIT %ld X AUD_ID %ld", (long)first,
            (long)eng_rec_num(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, AUD_ID)));
    if (eng_rec_read(e, ENG_T_AUDIT, eng_reccount(e, ENG_T_AUDIT)) !=
        ENG_OK)
        return 0;
    eng_rec_str(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, TBL), got, ENG_MAXVAL);
    sprintf(got + strlen(got), " %ld ",
            (long)eng_rec_num(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, REC_ID)));
    eng_rec_str(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, ACTION), got + strlen(got),
                2);
    strcat(got, " ");
    eng_rec_str(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, FLD), got + strlen(got),
                DBF_NAMELEN + 1);
    strcat(got, " ");
    eng_rec_str(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, NEWVAL), got + strlen(got),
                TXT_U32LEN + 1);
    if (strcmp(got, want) == 0)
        return 1;
    printf("T_TABLES: last audit row %s, want %s\n", got, want);
    return 0;
}

/* The rewrite of a marker naming every table. */
static const char x_allmark[] =
    "ITEMS\r\nLOCS\r\nCATS\r\nLTYPES\r\nMAINT\r\nLOANS\r\nAUDIT\r\nUSERS\r\n"
    "SEQ\r\n";

/* Open with eng_open three times; each must succeed and leave no
   marker. */
static void open_thrice(void)
{
    eng *e;
    int i;

    for (i = 0; i < 3; i++)
        if (expect_open(&e, ENG_OK)) {
            TF_CHECK(!exists(ENG_EDITMARK, ""));
            expect_close(e);
        }
}

/* A marker naming ITEMS or LOANS, or unreadable, outlives the open of
   the files, rewritten as one line per table, and goes with
   eng_init_data's batch; its repairs failing (here the commit of a SEQ
   row healed past the AUDIT IDs) keep it, again and again in one
   process, until an open that succeeds removes it. */
void test_kept_repairs(void)
{
    eng *e;
    u32 n;
    u16 total;
    int i;

    e = open_marked("LOCS\r\n", 0, NULL, 0);
    if (e != NULL) {
        TF_EQ(eng_repaired(e, ENG_T_LOCS), 1);
        TF_EQ(eng_repaired(e, ENG_T_ITEMS), 0);
        TF_EQ(eng_repaired(e, -1), 0);
        expect_close(e);
    }
    e = open_marked("LOANS\r\nLOANS\r\n", 0, "LOANS\r\nAUDIT\r\n",
                    MARK_ALL);
    if (e != NULL) {
        TF_EQ(eng_repaired(e, ENG_T_LOANS), 1);
        TF_EQ(eng_repaired(e, ENG_T_LOCS), 0);
        TF_EQ(eng_init_data(e), ENG_OK);
        TF_CHECK(!exists(ENG_EDITMARK, ""));
        TF_EQ(eng_marker_kept(e), 0);
        expect_close(e);
    }
    e = open_marked("??", 0, x_allmark, 0);
    if (e != NULL) {
        TF_EQ(eng_repaired(e, ENG_T_SEQ), 1);
        n = eng_reccount(e, ENG_T_AUDIT);
        TF_EQ(eng_init_data(e), ENG_OK);
        TF_CHECK(cut_is(e, 0, n));
        expect_close(e);
    }

    /* A marker whose count line leaves out audit records is kept, its
       count carried over, and the rows after it flagged by one X row;
       when that open cannot remove the marker, the next one flags them
       again, the first X row included. */
    e = open_marked("1\r\nLOCS\r\n", 0, "LOCS\r\nAUDIT\r\n", 1);
    if (e != NULL) {
        n = eng_reccount(e, ENG_T_AUDIT);
        TF_CHECK(n > 1);
        tfs_fault(TFS_OP(CF_OP_REMOVE), 1, 0, CF_EACCES);
        TF_EQ(eng_init_data(e), ENG_OK);
        tfs_fault_off();
        TF_CHECK(cut_is(e, 1, n));
        expect_close(e);
        TF_CHECK(marker_is(1, "LOCS\r\nAUDIT\r\n"));
    }
    if (expect_open(&e, ENG_OK)) {
        TF_CHECK(cut_is(e, 1, n + 1u));
        TF_CHECK(!exists(ENG_EDITMARK, ""));
        expect_close(e);
    }
    for (i = 0; i < 2; i++) {
        set_seq("AUDIT", 3);
        e = open_marked("CATS\r\nITEMS\r\n", 0,
                        "ITEMS\r\nCATS\r\nAUDIT\r\n", MARK_ALL);
        if (e == NULL)
            break;
        tfs_fault(TFS_OP(CF_OP_COMMIT), 1, 0, CF_EACCES);
        TF_EQ(eng_init_data(e), ENG_EINUSE);
        tfs_fault_off();
        TF_CHECK(marker_is(eng_marker_from(e),
                           "ITEMS\r\nCATS\r\nAUDIT\r\n"));
        expect_close(e);
    }
    if (expect_open(&e, ENG_OK)) {
        TF_CHECK(!exists(ENG_EDITMARK, ""));
        expect_close(e);
    }
    if (expect_open(&e, ENG_OK)) {
        TF_EQ(eng_marker_kept(e), 0);
        TF_EQ(eng_repaired(e, ENG_T_AUDIT), 0);
        expect_close(e);
    }

    /* The rewrite is the open's last create; when it fails the open
       fails, the old marker stays, and the next open repairs what it
       names. */
    write_file(ENG_EDITMARK, "", "ITEMS\r\n");
    tfs_fault(TFS_OP(CF_OP_CREATE), 0, 0, 0);
    TF_EQ(eng_open_files(&e, tfs_dir(), NULL, g_why, (u16)sizeof g_why),
          ENG_OK);
    tfs_fault_off();
    total = tfs_count(CF_OP_CREATE);
    TF_CHECK(total > 0);
    if (e != NULL)
        expect_close(e);
    write_file(ENG_EDITMARK, "", "ITEMS\r\n");
    tfs_fault(TFS_OP(CF_OP_CREATE), total, 1, CF_EACCES);
    TF_CHECK(eng_open_files(&e, tfs_dir(), NULL, g_why,
                            (u16)sizeof g_why) != ENG_OK);
    tfs_fault_off();
    expect_why(ENG_EDITMARK);
    expect_closed();
    open_thrice();
}

/* On a fresh directory: an unreadable marker and a valid one naming
   ITEMS, both of ENG_MARKMAX bytes, with no room for the lines the open
   adds, block no open. */
void test_long_markers(void)
{
    eng *e;
    int i;

    for (i = 0; i < 2; i++) {
        remove_all();
        if (i == 0)
            memset(g_long, 'X', ENG_MARKMAX);
        else
            mark_of("ITEMS\r\n", ENG_MARKMAX);
        write_bytes(ENG_EDITMARK, "", g_long, ENG_MARKMAX);
        open_thrice();
        if (expect_open(&e, ENG_OK)) {
            TF_EQ(eng_reccount(e, ENG_T_CATS), 10);
            expect_close(e);
        }
    }
}

/* Field f of item id written raw, as a number. */
static void raw_item(eng *e, u32 id, u16 f, i32 v)
{
    u32 r;

    TF_EQ(eng_item_get(e, id, &r), ENG_OK);
    TF_EQ(dbf_put_num(eng_dbf(e, ENG_T_ITEMS), f, v), DBF_OK);
    TF_EQ(dbf_write(eng_dbf(e, ENG_T_ITEMS)), DBF_OK);
}

static void set_status(eng *e, u32 id, const char *v, int want,
                       const char *msg)
{
    eng_pair p;

    p.key = "STATUS";
    p.val = v;
    eng_clear_error(e);
    TF_EQ(eng_item_set(e, id, &p, 1), want);
    TF_STREQ(eng_errfield(e), want == ENG_OK ? "" : "STATUS");
    TF_STREQ(eng_errmsg(e), msg);
}

/* STATUS D refused while on loan or installed (ON_LOAN and PARENT_ID
   written raw), and D to D and D to A allowed. */
void test_items_status(void)
{
    eng_pair p;
    eng *e;
    u32 id;

    remove_all();
    if (!expect_open(&e, ENG_OK))
        return;
    eng_set_actor(e, "STA1", "TESTER");
    p.key = "CATEGORY";
    p.val = "SYS";
    TF_EQ(eng_item_add(e, &p, 1, &id), ENG_OK);
    raw_item(e, id, ENG_FIELD(ITEMS, ON_LOAN), 1);
    set_status(e, id, "D", ENG_ECONSTR, "item is on loan");
    raw_item(e, id, ENG_FIELD(ITEMS, ON_LOAN), 0);
    raw_item(e, id, ENG_FIELD(ITEMS, PARENT_ID), 5);
    set_status(e, id, "d", ENG_ECONSTR, "installed in an assembly");
    raw_item(e, id, ENG_FIELD(ITEMS, PARENT_ID), 0);
    set_status(e, id, "D", ENG_OK, "");
    raw_item(e, id, ENG_FIELD(ITEMS, ON_LOAN), 1);
    raw_item(e, id, ENG_FIELD(ITEMS, PARENT_ID), 5);
    set_status(e, id, "D", ENG_OK, "");
    set_status(e, id, "A", ENG_OK, "");
    raw_item(e, id, ENG_FIELD(ITEMS, PARENT_ID), 0);
    expect_close(e);
}

/* ---------------------------------------------------------------------
 * SEQ rows that move.
 * --------------------------------------------------------------------- */

static dbf_field x_seqf[2] = {
    { "SEQ_NAME", 'C', 10, 0, 0 },
    { "NEXT_VAL", 'N', 8, 0, 0 }
};

/* The SEQ rows of a second data set: other values, rows in reverse
   order of the ones the engine appends, then a second live ITEMS row,
   which IDs never come from as the first one comes before it. */
static const char *const x_seqb_name[] = {
    "USERS", "AUDIT", "LOANS", "MAINT", "ITEMS", "ITEMS"
};
static const i32 x_seqb_val[] = { 800, 700, 300, 400, 600, 900 };

/* Rename stem.ext to stem2.ext2 in the data directory. */
static void rename_file(const char *stem, const char *ext,
                        const char *stem2, const char *ext2)
{
    TF_EQ(cf_rename(tfs_path(stem, ext), tfs_path(stem2, ext2)), 0);
}

/* Issue one ID of table t, which must be want. */
static void expect_id(eng *e, int t, u32 want)
{
    u32 id;

    TF_EQ(eng_seq_next(e, t, 1, &id), ENG_OK);
    if (!TF_EQ(id, want))
        printf("T_TABLES: ID of table %d\n", t);
}

/* SEQ.DBF record r, read raw: deleted flag, SEQ_NAME and NEXT_VAL. */
static void expect_seq_rec(u32 r, int deleted, const char *name, i32 next)
{
    char n[DBF_NAMELEN + 1];
    dbf *t;
    i32 v;
    int blank;

    if (!TF_EQ(dbf_open(&t, tfs_path("SEQ", ".DBF"), DBF_RDONLY), DBF_OK))
        return;
    if (TF_EQ(dbf_read(t, r), DBF_OK)) {
        TF_EQ(dbf_is_deleted(t), deleted);
        TF_EQ(dbf_get_str(t, ENG_FIELD(SEQ, SEQ_NAME), n,
                          (u16)sizeof n), DBF_OK);
        TF_STREQ(n, name);
        TF_EQ(dbf_get_num(t, ENG_FIELD(SEQ, NEXT_VAL), &v, &blank),
              DBF_OK);
        TF_EQ(blank, 0);
        if (!TF_EQ(v, next))
            printf("T_TABLES: SEQ record %lu\n", (unsigned long)r);
    }
    TF_EQ(dbf_close(t), DBF_OK);
}

/* Delete the ITEMS row of SEQ.DBF and append a new one at the end with
   NEXT_VAL 500, behind the engine's back. */
static void move_items_row(void)
{
    char n[DBF_NAMELEN + 1];
    dbf *t;
    u32 r;
    int found;

    if (!TF_EQ(dbf_open(&t, tfs_path("SEQ", ".DBF"), DBF_RDWR), DBF_OK))
        return;
    found = 0;
    for (r = 1; r <= dbf_reccount(t); r++) {
        TF_EQ(dbf_read(t, r), DBF_OK);
        TF_EQ(dbf_get_str(t, ENG_FIELD(SEQ, SEQ_NAME), n,
                          (u16)sizeof n), DBF_OK);
        if (strcmp(n, "ITEMS") == 0) {
            dbf_set_deleted(t, 1);
            TF_EQ(dbf_write(t), DBF_OK);
            found++;
        }
    }
    TF_EQ(found, 1);
    dbf_blank(t);
    TF_EQ(dbf_put_str(t, ENG_FIELD(SEQ, SEQ_NAME), "ITEMS"), DBF_OK);
    TF_EQ(dbf_put_num(t, ENG_FIELD(SEQ, NEXT_VAL), 500), DBF_OK);
    TF_EQ(dbf_append(t), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);
}

/* Write the second data set's SEQ.DBF. */
static void write_seqb(void)
{
    dbf *t;
    int i;

    dbf_set_today(2026, 10, 8);
    if (!TF_EQ(dbf_create(tfs_path("SEQ", ".DBF"), x_seqf, 2), DBF_OK))
        return;
    if (!TF_EQ(dbf_open(&t, tfs_path("SEQ", ".DBF"), DBF_RDWR), DBF_OK))
        return;
    for (i = 0; i < (int)(sizeof x_seqb_val / sizeof x_seqb_val[0]); i++) {
        dbf_blank(t);
        TF_EQ(dbf_put_str(t, ENG_FIELD(SEQ, SEQ_NAME), x_seqb_name[i]),
              DBF_OK);
        TF_EQ(dbf_put_num(t, ENG_FIELD(SEQ, NEXT_VAL), x_seqb_val[i]),
              DBF_OK);
        TF_EQ(dbf_append(t), DBF_OK);
    }
    TF_EQ(dbf_close(t), DBF_OK);
}

/* IDs come from the live SEQ row of their table wherever it is: after
   the ITEMS row is deleted and appended again at the end, and in a
   second data set whose rows are in another order, then back in the
   first.  Of two live rows of one name the first is used, though the
   row the last open used is the other one: every open looks its rows
   up again. */
void test_seq_moved(void)
{
    eng *e;
    u32 next;

    remove_all();
    if (!expect_open(&e, ENG_OK))
        return;
    expect_id(e, ENG_T_ITEMS, 1);
    expect_id(e, ENG_T_MAINT, 1);
    expect_close(e);
    expect_seq_rec(1, 0, "ITEMS", 2);

    move_items_row();
    if (expect_open(&e, ENG_OK)) {
        TF_EQ(eng_reccount(e, ENG_T_SEQ), 7);
        TF_EQ(eng_seq_peek(e, ENG_T_ITEMS, &next), ENG_OK);
        TF_EQ(next, 500);
        expect_id(e, ENG_T_ITEMS, 500);
        expect_id(e, ENG_T_ITEMS, 501);
        expect_id(e, ENG_T_MAINT, 2);
        expect_id(e, ENG_T_LOANS, 1);
        expect_close(e);
    }
    expect_seq_rec(1, 1, "ITEMS", 2);
    expect_seq_rec(7, 0, "ITEMS", 502);

    /* The second data set: A's SEQ.DBF is kept as SEQA.DBF meanwhile.
       Its record 6 is its second ITEMS row, and the open appends the
       missing LOCS row as record 7. */
    rename_file("SEQ", ".DBF", "SEQA", ".DBF");
    write_seqb();
    if (expect_open(&e, ENG_OK)) {
        TF_EQ(eng_reccount(e, ENG_T_SEQ), 7);
        expect_id(e, ENG_T_ITEMS, 600);
        expect_id(e, ENG_T_MAINT, 400);
        expect_id(e, ENG_T_LOANS, 300);
        expect_id(e, ENG_T_ITEMS, 601);
        expect_close(e);
    }
    expect_seq_rec(1, 0, "USERS", 800);
    expect_seq_rec(4, 0, "MAINT", 401);
    expect_seq_rec(5, 0, "ITEMS", 602);
    expect_seq_rec(6, 0, "ITEMS", 900);

    /* Back to the first. */
    remove_file("SEQ", ".DBF");
    rename_file("SEQA", ".DBF", "SEQ", ".DBF");
    if (expect_open(&e, ENG_OK)) {
        expect_id(e, ENG_T_ITEMS, 502);
        expect_id(e, ENG_T_MAINT, 3);
        expect_id(e, ENG_T_LOANS, 2);
        expect_close(e);
    }
    expect_seq_rec(7, 0, "ITEMS", 503);
    expect_closed();
}
