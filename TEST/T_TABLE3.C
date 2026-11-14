/*
 * T_TABLE3.C - the third part of the T_TABLES test (see T_TABLES.C):
 * items and locations appended past the engine, SEQ set behind its
 * back, the open that rebuilds missing indexes and raises SEQ, an
 * AUDIT2.NDX of other data than its table's, and code tables that stay
 * empty.
 * It is a file of its own only to stay within the compiler memory of
 * Watcom C 6.5; T_TABLES.H holds what the three share.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGSCH.H"
#include "ENGCODE.H"
#include "ENGADMIN.H"
#define TF_PART
#define TF_FILE "T_TABLE3"
#include "TESTFW.H"
#include "T_FILES.H"
#include "T_TABLES.H"

/* The number of field name of t; an unknown name gives a number no
   field has, so the put that uses it fails. */
static u16 field_of(dbf *t, const char *name)
{
    return (u16)dbf_field_index(t, name);
}

/* Append an item directly with the table library, as another program
   (or a stop before SEQ was raised) could leave it, without index
   entries. */
static void append_item(i32 id, const char *mfr, const char *model)
{
    dbf *t;

    if (!TF_EQ(dbf_open(&t, tfs_path("ITEMS", ".DBF"), DBF_RDWR), DBF_OK))
        return;
    dbf_blank(t);
    TF_EQ(dbf_put_num(t, field_of(t, "ITEM_ID"), id), DBF_OK);
    TF_EQ(dbf_put_str(t, field_of(t, "CATEGORY"), "SYS"), DBF_OK);
    TF_EQ(dbf_put_str(t, field_of(t, "MFR"), mfr), DBF_OK);
    TF_EQ(dbf_put_str(t, field_of(t, "MODEL"), model), DBF_OK);
    TF_EQ(dbf_put_num(t, field_of(t, "LOC_ID"), 3), DBF_OK);
    TF_EQ(dbf_put_num(t, field_of(t, "PARENT_ID"), 0), DBF_OK);
    TF_EQ(dbf_put_str(t, field_of(t, "SERIALNO"), "SN-1"), DBF_OK);
    TF_EQ(dbf_append(t), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);
}

/* Append a location with LOC_ID id directly, without index entries. */
static void append_loc(i32 id)
{
    dbf *t;

    if (!TF_EQ(dbf_open(&t, tfs_path("LOCS", ".DBF"), DBF_RDWR), DBF_OK))
        return;
    dbf_blank(t);
    TF_EQ(dbf_put_num(t, field_of(t, "LOC_ID"), id), DBF_OK);
    TF_EQ(dbf_put_num(t, field_of(t, "PARENT_ID"), 0), DBF_OK);
    TF_EQ(dbf_append(t), DBF_OK);
    TF_EQ(dbf_close(t), DBF_OK);
}

/* Set the NEXT_VAL of a SEQ row behind the engine's back. */
void set_seq(const char *name, i32 v)
{
    char n[12];
    dbf *t;
    u32 r;

    if (!TF_EQ(dbf_open(&t, tfs_path("SEQ", ".DBF"), DBF_RDWR), DBF_OK))
        return;
    for (r = 1; r <= dbf_reccount(t); r++) {
        TF_EQ(dbf_read(t, r), DBF_OK);
        dbf_get_str(t, field_of(t, "SEQ_NAME"), n, (u16)sizeof n);
        if (strcmp(n, name) == 0) {
            TF_EQ(dbf_put_num(t, field_of(t, "NEXT_VAL"), v), DBF_OK);
            TF_EQ(dbf_write(t), DBF_OK);
        }
    }
    TF_EQ(dbf_close(t), DBF_OK);
}

/* Lookups through the rebuilt ITEMS indexes. */
static void test_rebuilt_lookups(eng *e)
{
    u8 key[ENG_MAXKEYLEN + 1];
    u32 r;
    u16 len;

    expect_seek(e, ENG_X_ITEMS1, "      41", 1);
    expect_seek(e, ENG_X_ITEMS1, "      57", 2);
    expect_seek(e, ENG_X_ITEMS2, "       3      41", 1);
    expect_seek(e, ENG_X_ITEMS3, "       0      57", 2);
    memset(key, ' ', eng_index_def(ENG_X_ITEMS4)->keylen);
    memcpy(key, "COMPAQ", 6);
    memcpy(key + 30, "PORTABLE", 8);
    key[eng_index_def(ENG_X_ITEMS4)->keylen] = '\0';
    expect_seek(e, ENG_X_ITEMS4, (const char *)key, 2);
    expect_seek(e, ENG_X_ITEMS5, "SN-1", 1);
    TF_EQ(ndx_last(eng_ndx(e, ENG_X_ITEMS1), &r), DBF_OK);
    TF_EQ(r, 2);
    TF_EQ(eng_rec_read(e, ENG_T_ITEMS, 1), ENG_OK);
    expect_text(e, ENG_T_ITEMS, "MFR", "IBM");
    expect_text(e, ENG_T_ITEMS, "ACQ_DATE", "");
    expect_text(e, ENG_T_ITEMS, "QTY", "");
    expect_text(e, ENG_T_ITEMS, "NOTES", "");
    TF_EQ(eng_memo_read(e, ENG_T_ITEMS,
                        (u16)eng_field_num(e, ENG_T_ITEMS, "NOTES"), &len),
          ENG_OK);
    TF_EQ(len, 0);
    TF_STREQ(eng_memobuf(e), "");

    /* A deleted record is skipped by the live lookup. */
    TF_EQ(eng_rec_read(e, ENG_T_ITEMS, 1), ENG_OK);
    dbf_set_deleted(eng_dbf(e, ENG_T_ITEMS), 1);
    TF_EQ(dbf_write(eng_dbf(e, ENG_T_ITEMS)), DBF_OK);
    expect_seek(e, ENG_X_ITEMS5, "sn-1", 0);
    expect_seek(e, ENG_X_ITEMS1, "      57", 2);
}

/* Copy the audit log and SEQ.DBF to names beside them (put 0), or
   those copies back over them (put 1), AUDIT2.NDX only with ix2. */
static void audit_copies(int put, int ix2)
{
    static const char *const k_names[][3] = {
        { "AUDIT", ".DBF", ".KDF" }, { "AUDIT1", ".NDX", ".KNX" },
        { "SEQ", ".DBF", ".KDF" }, { "AUDIT2", ".NDX", ".KNX" }
    };
    const char *const *n;
    u16 i;

    for (i = 0; i < (ix2 ? 4u : 3u); i++) {
        n = k_names[i];
        TF_EQ(tfs_copy(tfs_path(n[0], n[put ? 2 : 1]),
                       tfs_path(n[0], n[put ? 1 : 2])), 0);
    }
}

/* The open succeeds and leaves no marker, and AUDIT2 then holds an
   entry for each of the n audit rows, the first for record first; the
   engine, or NULL. */
static eng *audit2_whole(u32 n, u32 first)
{
    eng *e;
    u32 k;
    u32 r;

    if (!expect_open(&e, ENG_OK))
        return NULL;
    TF_CHECK(!exists(ENG_EDITMARK, ""));
    TF_EQ(eng_reccount(e, ENG_T_AUDIT), n);
    TF_EQ(ndx_first(eng_ndx(e, ENG_X_AUDIT2), &r), DBF_OK);
    TF_EQ(r, first);
    for (k = 0; r != 0; k++)
        TF_EQ(ndx_next(eng_ndx(e, ENG_X_AUDIT2), &r), DBF_OK);
    TF_EQ(k, n);
    return e;
}

/* Append n audit rows as user, and close. */
static void audit_more(const char *user, u16 n)
{
    eng_audrow row;
    eng *e;

    if (!expect_open(&e, ENG_OK))
        return;
    row.action = ENG_A_UPDATE;
    row.fld = "QTY";
    row.oldval = "1";
    row.newval = "2";
    eng_set_actor(e, "ST1", user);
    for (; n > 0; n--)
        TF_EQ(eng_audit(e, ENG_T_ITEMS, 41, &row), ENG_OK);
    expect_close(e);
}

/* The open and AUDIT2.NDX, on the 19 rows by SYSTEM that are kept
   aside first and put back at the end.  An index that is its table's
   is left as it is, byte for byte.  One that is not is built afresh,
   with no marker and no row added: one from before the table's newest
   rows (the files of a release without the index copied over a
   directory that has it); one with later rows of the user of the
   table's newest row (such files put back over data that went on); one
   in which the table's last record is another user's; and an empty one
   beside a table with rows (a rebuild that stopped).  One of another
   expression is refused, as any index is. */
static void test_audit2_stale(void)
{
    static const char k_ndx[] = ".NDX";
    static const char k_grown[] = ".LNX";
    char expr[NDX_MAXEXPR + 1];
    eng *e;

    audit_copies(0, 1);
    /* 24 rows more by BOB and one by SYSTEM: an index of several
       pages, which a build would pack. */
    audit_more("BOB", 24);
    audit_more("SYSTEM", 1);
    TF_EQ(tfs_copy(tfs_path("AUDIT2", k_ndx), tfs_path("AUDIT2", k_grown)),
          0);
    e = audit2_whole(44, 43);
    if (e == NULL)
        return;
    expect_close(e);
    TF_CHECK(tfs_same(tfs_path("AUDIT2", k_ndx), tfs_path("AUDIT2", k_grown)));

    /* The index of the 19 rows beside the table of 44. */
    TF_EQ(tfs_copy(tfs_path("AUDIT2", ".KNX"), tfs_path("AUDIT2", k_ndx)), 0);
    e = audit2_whole(44, 43);
    if (e == NULL)
        return;
    expect_seek(e, ENG_X_AUDIT2, "BOB     99999956", 43);
    expect_seek(e, ENG_X_AUDIT2, "SYSTEM  99999955", 44);
    expect_close(e);
    TF_CHECK(!tfs_same(tfs_path("AUDIT2", k_ndx),
                       tfs_path("AUDIT2", k_grown)));

    /* The index of the 44 rows beside the table of 19. */
    audit_copies(1, 0);
    e = audit2_whole(19, 19);
    if (e == NULL)
        return;
    expect_seek(e, ENG_X_AUDIT2, "BOB     99999956", 0);
    expect_close(e);
    TF_CHECK(tfs_same(tfs_path("AUDIT2", k_ndx), tfs_path("AUDIT2", ".KNX")));

    /* The index of a 20th row by SYSTEM beside a 20th row by BOB. */
    audit_more("SYSTEM", 1);
    TF_EQ(tfs_copy(tfs_path("AUDIT2", k_ndx), tfs_path("AUDIT2", k_grown)),
          0);
    audit_copies(1, 1);
    audit_more("BOB", 1);
    TF_EQ(tfs_copy(tfs_path("AUDIT2", k_grown), tfs_path("AUDIT2", k_ndx)),
          0);
    e = audit2_whole(20, 20);
    if (e == NULL)
        return;
    expect_seek(e, ENG_X_AUDIT2, "BOB     99999979", 20);
    expect_close(e);

    audit_copies(1, 1);
    TF_EQ(eng_index_expr(ENG_X_AUDIT2, expr, (u16)sizeof expr), ENG_OK);
    TF_EQ(ndx_create(tfs_path("AUDIT2", k_ndx), expr,
                     eng_index_def(ENG_X_AUDIT2)->keylen), DBF_OK);
    e = audit2_whole(19, 19);
    if (e != NULL)
        expect_close(e);
    TF_CHECK(tfs_same(tfs_path("AUDIT2", k_ndx), tfs_path("AUDIT2", ".KNX")));

    TF_EQ(ndx_create(tfs_path("AUDIT2", k_ndx), "USERNAME+STR(AUD_ID,8)",
                     eng_index_def(ENG_X_AUDIT2)->keylen), DBF_OK);
    expect_open(&e, ENG_EINDEX);
    expect_why("AUDIT2.NDX");
    audit_copies(1, 1);
    remove_file("AUDIT", ".KDF");
    remove_file("AUDIT1", ".KNX");
    remove_file("SEQ", ".KDF");
    remove_file("AUDIT2", ".KNX");
    remove_file("AUDIT2", k_grown);
}

/* Missing indexes are rebuilt from their tables, AUDIT2 as in a
   directory made before it existed, with an entry for each audit row
   and no row added; SEQ is healed.  Then AUDIT2.NDX files of other data
   (test_audit2_stale). */
void test_rebuild_and_heal(void)
{
    eng *e;
    u32 n;
    u32 r;
    int x;

    append_item(41, "IBM", "5150");
    append_item(57, "Compaq", "Portable");
    /* The largest LOC_ID is not the last record's: LOCS1's last key
       gives it. */
    append_loc(70);
    append_loc(5);
    set_seq("AUDIT", 3);
    set_seq("USERS", 0);
    for (x = ENG_X_ITEMS1; x <= ENG_X_LOCS3; x++)
        remove_file(eng_index_def(x)->name, ".NDX");
    remove_file("AUDIT2", ".NDX");

    if (!expect_open(&e, ENG_OK))
        return;
    TF_CHECK(!exists(ENG_EDITMARK, ""));
    check_seq(e, "ITEMS", "58");
    check_seq(e, "AUDIT", "20");
    check_seq(e, "USERS", "2");
    check_seq(e, "MAINT", "1");
    check_seq(e, "LOCS", "71");
    TF_EQ(eng_reccount(e, ENG_T_AUDIT), 19);
    expect_seek(e, ENG_X_AUDIT2, "SYSTEM  99999980", 19);
    TF_EQ(ndx_first(eng_ndx(e, ENG_X_AUDIT2), &r), DBF_OK);
    for (n = 0; r != 0; n++) {
        TF_EQ(r, 19 - n);
        TF_EQ(ndx_next(eng_ndx(e, ENG_X_AUDIT2), &r), DBF_OK);
    }
    TF_EQ(n, 19);
    test_rebuilt_lookups(e);
    expect_close(e);
    test_audit2_stale();
}

/* A code table its users emptied (CAT DEL and LTYPE DEL of every row,
   audited as D rows) stays empty through PACK and the opens after it,
   which seed only a code table no D row names.  CATS record 1 gets
   ENG_MAXREADS U rows before its D row, so the open's search for that
   row takes more than one scan request. */
void test_codes_emptied(void)
{
    char code[ENG_LTYPELEN + 1];
    char name[ENG_CODENAMELEN + 1];
    eng_audrow row;
    eng *e;
    u16 sortord;
    u16 i;
    u32 n;
    int t;

    remove_all();
    if (!expect_open(&e, ENG_OK))
        return;
    row.action = ENG_A_UPDATE;
    row.fld = "CAT_NAME";
    row.oldval = "A";
    row.newval = "B";
    TF_EQ(eng_audit_begin(e, ENG_MAXREADS), ENG_OK);
    for (i = 0; i < ENG_MAXREADS; i++)
        TF_EQ(eng_audit_add(e, ENG_T_CATS, 1, &row), ENG_OK);
    TF_EQ(eng_audit_end(e), ENG_OK);
    for (i = 0; i < eng_seed_rows(); i++) {
        eng_seed_row(i, &t, code, name, &sortord);
        TF_EQ(eng_code_del(e, t, code), ENG_OK);
    }
    TF_EQ(eng_admin(e, ENG_ADM_PACK, NULL, &n), ENG_OK);
    TF_EQ(n, eng_seed_rows());
    TF_EQ(eng_reccount(e, ENG_T_CATS), 0);
    TF_EQ(eng_reccount(e, ENG_T_LTYPES), 0);
    expect_close(e);
    if (!expect_open(&e, ENG_OK))
        return;
    TF_EQ(eng_reccount(e, ENG_T_CATS), 0);
    TF_EQ(eng_reccount(e, ENG_T_LTYPES), 0);
    expect_close(e);
}
