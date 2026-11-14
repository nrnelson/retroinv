/*
 * T_EDIT.C - unit test for record edits (EDIT.C), audit rows (AUDIT.C)
 * and ID numbers (IDGEN.C).
 *
 * usage: T_EDIT [dir]
 *
 * Works on a fresh data directory in dir (a prefix such as "OBJ/HOST/DDATA/"
 * or "D:\"; a missing separator is added), removing every engine file
 * first and at the end.  The ID keys, note lines, the marker and the
 * batches are tested in T_EDIT2.C, the marker's writes and memos saved
 * in parts in T_EDIT3.C.
 * Checks:
 *   - the ID keys STR(id,8) (eng_id_key, eng_key_id);
 *   - SEQ rows, issue, peek, exhaustion of a narrow ID field and repair;
 *   - streamed audit rows: reserved IDs, stamps, cut values, gaps; on
 *     the host, a row written as the DOS clock passes midnight has the
 *     date and the time of one reading;
 *   - the audit rows of an insert, of updates (U, M and P), of delete and
 *     undelete, none for NOAUDIT and HIDDEN fields or an unchanged record;
 *   - value checks of every field type, apply with denied fields, and
 *     required fields;
 *   - memo values (LF stored as CR LF, refused bytes, the length limit,
 *     putmemo, note lines) and their audit rows; an old block number
 *     that cannot be read counts as text, and a commit reads the old
 *     memo once; memos saved in parts, staged per tag and written whole
 *     by their END, the old memo kept through a disk full;
 *   - index upkeep: after every edit each index holds exactly one entry
 *     per record, with the key of the record;
 *   - begin_copy (fields and memo copied to a new record) and
 *     begin_reuse (a new record written over a deleted one);
 *   - cancel, and a second begin while an edit is open;
 *   - eng_close ends an open edit and audit reservation of its engine;
 *   - the ENG_EDITMARK marker: absent after a commit, never written by an
 *     unchanged one, left by a stop at every later disk write holding
 *     the count of AUDIT records before the change and naming the table
 *     (AUDIT for audit rows written outside a commit), then kept,
 *     gaining the tables of later changes, until the next open; with no
 *     change under way, replaced by the next change's own; single failed
 *     writes in a pair of commits never lose a table owed a repair; a
 *     marker that cannot be removed is no error;
 *   - the marker's writes: a new marker's line, then zeros up to the
 *     end of its disk sector, then a cut back to the line, so the
 *     sector holds nothing of what DOS's buffer held before; a cut that
 *     fails refuses the change, leaving the padded sector for the next
 *     open to repair; a line added to a marker is one write; the marker
 *     an open keeps for its repairs and writes afresh is padded so too,
 *     and a cut of it that fails fails the open;
 *   - puts by field number, and the O and R rows of ON_LOAN changes;
 *   - batches: commits inside one flush only SEQ, and AUDIT.DBF before a
 *     record rewritten in place; its end flushes the rest, writing no
 *     clean table or memo file, and removes the marker
 *     only when ok and flushed; commits after a failed change still
 *     flush;
 *   - a quiet commit writes no audit rows;
 *   - the ENG_E code of every DBF_E code.
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, else 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGID.H"
#include "ENGEDIT.H"
#include "TESTFW.H"
#include "T_FILES.H"
#include "TENGH.H"
#include "TEWIPE.H"
#include "T_EDIT.H"

/* MAINT_ID, the first field, starts after the deletion flag. */
#define ID_AT           1
/* An aud row: seven fields, six separators and the NUL. */
#define AUD_ROWMAX      (2 * ENG_IDLEN + ENG_TBLNAMELEN + 1 + DBF_NAMELEN + \
                         2 * ENG_AUDVAL + 7)

eng *g_e;
u16 g_f_mfr;
static u16 g_f_notes;           /* ITEMS NOTES                          */
static u16 g_f_lnotes;          /* LOCS NOTES                           */
static char g_text[ENG_MAXVAL];

static const eng_time g_now = { 2026, 10, 8, 12, 34, 56 };

int fresh(void)
{
    char why[TFS_WHYSZ];
    int rc;

    if (g_e != NULL)
        eng_close(g_e);
    g_e = NULL;
    te_remove(tfs_dir(), NULL);
    rc = eng_open_files(&g_e, tfs_dir(), &g_now, why, (u16)sizeof why);
    if (!TF_EQ(rc, ENG_OK)) {
        printf("open: %s\n", why);
        g_e = NULL;
        return 0;
    }
    TF_EQ(eng_seq_rows(g_e), ENG_OK);
    eng_set_actor(g_e, "STA1", "TESTER");
    g_f_mfr = (u16)eng_field_num(g_e, ENG_T_ITEMS, "MFR");
    g_f_notes = (u16)eng_field_num(g_e, ENG_T_ITEMS, "NOTES");
    g_f_lnotes = (u16)eng_field_num(g_e, ENG_T_LOCS, "NOTES");
    return 1;
}

void reopen(void)
{
    char why[TFS_WHYSZ];

    TF_EQ(eng_close(g_e), ENG_OK);
    g_e = NULL;
    TF_EQ(eng_open_files(&g_e, tfs_dir(), &g_now, why, (u16)sizeof why),
          ENG_OK);
    if (g_e != NULL && eng_marker_kept(g_e)) {
        eng_mark_adopt(g_e);
        TF_EQ(eng_batch_end(g_e, ENG_OK), ENG_OK);
    }
    eng_set_actor(g_e, "STA1", "TESTER");
}

/* Field name of record recno of table t as text, into g_text. */
static const char *fld(int t, u32 recno, const char *name)
{
    int f;

    g_text[0] = '\0';
    f = eng_field_num(g_e, t, name);
    if (!TF_CHECK(f >= 0))
        return g_text;
    TF_EQ(eng_rec_read(g_e, t, recno), ENG_OK);
    TF_EQ(eng_rec_text(g_e, t, (u16)f, g_text), ENG_OK);
    return g_text;
}

/* eng_edit_putnumf of field name of table t, the table being edited. */
static int putnum(int t, const char *name, i32 scaled)
{
    return eng_edit_putnumf(g_e, (u16)eng_field_num(g_e, t, name), scaled);
}

/* Audit row recno as "AUD_ID TBL REC_ID ACTION FLD|OLDVAL|NEWVAL". */
static const char *aud(u32 recno)
{
    static char row[AUD_ROWMAX];
    static const char *const names[] = { "AUD_ID", "TBL", "REC_ID",
                                         "ACTION", "FLD", "OLDVAL",
                                         "NEWVAL" };
    int i;

    row[0] = '\0';
    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
        fld(ENG_T_AUDIT, recno, names[i]);
        strcat(row, g_text);
        strcat(row, i < 4 ? " " : i < 6 ? "|" : "");
    }
    return row;
}

static u32 naud(void)
{
    return eng_reccount(g_e, ENG_T_AUDIT);
}

void put_ok(const char *field, const char *val)
{
    if (!TF_EQ(eng_edit_put(g_e, field, val), ENG_OK))
        printf("  put %s=\"%s\"\n", field, val);
}

void begin_ok(int t, u32 recno)
{
    if (!TF_EQ(eng_edit_begin(g_e, t, recno), ENG_OK))
        printf("  begin %d %lu\n", t, (unsigned long)recno);
}

void commit_ok(u32 recid)
{
    if (!TF_EQ(eng_edit_commit(g_e, recid), ENG_OK))
        printf("  commit %lu\n", (unsigned long)recid);
}

static void aud_is(u32 recno, const char *want)
{
    if (!TF_STREQ(aud(recno), want))
        printf("  audit row %lu\n", (unsigned long)recno);
}

void fld_is(int t, u32 recno, const char *name, const char *want)
{
    if (!TF_STREQ(fld(t, recno, name), want))
        printf("  %s of record %lu\n", name, (unsigned long)recno);
}

static void naud_is(u32 want)
{
    TF_EQ(naud(), want);
}

void check_indexes(int t)
{
    const eng_tdef *td;
    int x;

    td = eng_table_def(t);
    for (x = td->firstx; x >= 0 && x < td->firstx + td->nindex; x++)
        if (!TF_CHECK(te_index_ok(g_e, x)))
            printf("  index %s record %lu\n", eng_index_def(x)->name,
                   (unsigned long)te_index_rec);
}

u32 new_item(u32 id, const char *loc, const char *mfr)
{
    begin_ok(ENG_T_ITEMS, 0);
    TF_EQ(putnum(ENG_T_ITEMS, "ITEM_ID", (i32)id), ENG_OK);
    put_ok("CATEGORY", "sys");
    put_ok("LOC_ID", loc);
    put_ok("MFR", mfr);
    put_ok("MODEL", "5150");
    put_ok("QTY", "1");
    put_ok("PARENT_ID", "0");
    commit_ok(id);
    return dbf_recno(eng_dbf(g_e, ENG_T_ITEMS));
}

/* ---------------------------------------------------------------------
 * SEQ.
 * --------------------------------------------------------------------- */

static void test_seq(void)
{
    u32 v;
    u32 i;
    dbf *d;
    dbf *md;
    u16 f_next;
    u16 f_id;

    if (!fresh())
        return;
    f_next = (u16)eng_field_num(g_e, ENG_T_SEQ, "NEXT_VAL");
    f_id = (u16)eng_field_num(g_e, ENG_T_MAINT, "MAINT_ID");
    TF_EQ(eng_reccount(g_e, ENG_T_SEQ), 6);
    TF_EQ(eng_seq_rows(g_e), ENG_OK);
    TF_EQ(eng_reccount(g_e, ENG_T_SEQ), 6);
    TF_EQ(eng_seq_peek(g_e, ENG_T_ITEMS, &v), ENG_OK);
    TF_EQ(v, 1);
    TF_EQ(eng_seq_next(g_e, ENG_T_ITEMS, 3, &v), ENG_OK);
    TF_EQ(v, 1);
    TF_EQ(eng_seq_next(g_e, ENG_T_ITEMS, 1, &v), ENG_OK);
    TF_EQ(v, 4);
    TF_EQ(eng_seq_peek(g_e, ENG_T_ITEMS, &v), ENG_OK);
    TF_EQ(v, 5);
    TF_EQ(eng_seq_peek(g_e, ENG_T_MAINT, &v), ENG_OK);
    TF_EQ(v, 1);
    TF_EQ(eng_seq_peek(g_e, ENG_T_LOCS, &v), ENG_OK);
    TF_EQ(v, 1);
    TF_EQ(eng_seq_peek(g_e, ENG_T_LTYPES, &v), ENG_EINTERNAL);
    reopen();
    TF_EQ(eng_seq_peek(g_e, ENG_T_ITEMS, &v), ENG_OK);
    TF_EQ(v, 5);

    /* USER_ID is N(4): 9999 is the last ID. */
    TF_EQ(eng_seq_next(g_e, ENG_T_USERS, 9997, &v), ENG_OK);
    TF_EQ(v, 1);
    TF_EQ(eng_seq_next(g_e, ENG_T_USERS, 3, &v), ENG_ECONSTR);
    TF_STREQ(eng_errfield(g_e), "USER_ID");
    TF_EQ(v, 0);
    TF_EQ(eng_seq_peek(g_e, ENG_T_USERS, &v), ENG_OK);
    TF_EQ(v, 9998);
    TF_EQ(eng_seq_next(g_e, ENG_T_USERS, 2, &v), ENG_OK);
    TF_EQ(v, 9998);
    TF_EQ(eng_seq_next(g_e, ENG_T_USERS, 1, &v), ENG_ECONSTR);

    /* Repair: SEQ behind the last record, and behind the ITEMS1 key. */
    TF_EQ(eng_seq_next(g_e, ENG_T_AUDIT, 0, &v), ENG_OK);
    TF_EQ(v, 1);
    new_item(40, "11", "IBM");
    d = eng_dbf(g_e, ENG_T_SEQ);
    for (i = 1; i <= 6; i++) {
        TF_EQ(dbf_read(d, i), DBF_OK);
        TF_EQ(dbf_put_num(d, f_next, 2), DBF_OK);
        TF_EQ(dbf_write(d), DBF_OK);
    }
    TF_EQ(eng_seq_heal(g_e), ENG_OK);
    TF_EQ(eng_seq_peek(g_e, ENG_T_ITEMS, &v), ENG_OK);
    TF_EQ(v, 41);
    TF_EQ(eng_seq_peek(g_e, ENG_T_AUDIT, &v), ENG_OK);
    TF_EQ(v, 2);
    TF_EQ(eng_seq_peek(g_e, ENG_T_USERS, &v), ENG_OK);
    TF_EQ(v, 2);
    /* A later item whose record is not last: the ITEMS1 key counts. */
    new_item(90, "11", "IBM");
    new_item(50, "11", "IBM");
    TF_EQ(eng_seq_heal(g_e), ENG_OK);
    TF_EQ(eng_seq_peek(g_e, ENG_T_ITEMS, &v), ENG_OK);
    TF_EQ(v, 91);
    /* A SEQ ahead of the tables is left alone. */
    TF_EQ(eng_seq_next(g_e, ENG_T_ITEMS, 100, &v), ENG_OK);
    TF_EQ(eng_seq_heal(g_e), ENG_OK);
    TF_EQ(eng_seq_peek(g_e, ENG_T_ITEMS, &v), ENG_OK);
    TF_EQ(v, 191);
    /* A missing SEQ row is a damaged SEQ.DBF until eng_seq_rows. */
    TF_EQ(dbf_read(d, 3), DBF_OK);
    dbf_set_deleted(d, 1);
    TF_EQ(dbf_write(d), DBF_OK);
    TF_EQ(eng_seq_peek(g_e, ENG_T_MAINT, &v), ENG_EIO);
    TF_EQ(eng_seq_rows(g_e), ENG_OK);
    TF_EQ(eng_reccount(g_e, ENG_T_SEQ), 7);
    TF_EQ(eng_seq_peek(g_e, ENG_T_MAINT, &v), ENG_OK);
    TF_EQ(v, 1);

    /* Damaged IDs at the end of a table are passed over, stepping down
       at most ENG_IDSTEP records. */
    md = eng_dbf(g_e, ENG_T_MAINT);
    dbf_blank(md);
    TF_EQ(dbf_put_num(md, f_id, 30), DBF_OK);
    TF_EQ(dbf_append(md), DBF_OK);
    for (i = 0; i < ENG_IDSTEP; i++) {
        dbf_blank(md);
        if (i % 2 == 0)
            memcpy(dbf_recbuf(md) + ID_AT, "12AB    ", 8);
        TF_EQ(dbf_append(md), DBF_OK);
        if (i == ENG_IDSTEP - 2) {
            TF_EQ(eng_seq_heal(g_e), ENG_OK);
            TF_EQ(eng_seq_peek(g_e, ENG_T_MAINT, &v), ENG_OK);
            TF_EQ(v, 31);
        }
    }
    dbf_blank(md);
    TF_EQ(dbf_put_num(md, f_id, 25), DBF_OK);
    TF_EQ(dbf_append(md), DBF_OK);
    TF_EQ(dbf_read(d, 7), DBF_OK);
    TF_EQ(dbf_put_num(d, f_next, 2), DBF_OK);
    TF_EQ(dbf_write(d), DBF_OK);
    TF_EQ(eng_seq_heal(g_e), ENG_OK);
    TF_EQ(eng_seq_peek(g_e, ENG_T_MAINT, &v), ENG_OK);
    TF_EQ(v, 26);
    /* ENG_IDSTEP damaged records still reach the 25 below them; one
       more and none is readable. */
    for (i = 0; i <= ENG_IDSTEP; i++) {
        dbf_blank(md);
        memcpy(dbf_recbuf(md) + ID_AT, "--------", 8);
        TF_EQ(dbf_append(md), DBF_OK);
        TF_EQ(dbf_read(d, 7), DBF_OK);
        TF_EQ(dbf_put_num(d, f_next, 2), DBF_OK);
        TF_EQ(dbf_write(d), DBF_OK);
        TF_EQ(eng_seq_heal(g_e), ENG_OK);
        TF_EQ(eng_seq_peek(g_e, ENG_T_MAINT, &v), ENG_OK);
        TF_EQ(v, i < ENG_IDSTEP ? 26 : 2);
    }
}

/* ---------------------------------------------------------------------
 * Audit rows.
 * --------------------------------------------------------------------- */

static void test_audit(void)
{
    eng_audrow rows[3];
    char longv[60];
    u32 v;

    if (!fresh())
        return;
    memset(longv, 'x', sizeof longv - 1);
    longv[sizeof longv - 1] = '\0';
    longv[ENG_AUDVAL - 1] = 'y';
    rows[0].action = ENG_A_LOGIN;
    rows[0].fld = NULL;
    rows[0].oldval = NULL;
    rows[0].newval = NULL;
    rows[1].action = ENG_A_FAILED;
    rows[1].fld = "USERNAME_TOO_LONG";
    rows[1].oldval = "";
    rows[1].newval = longv;
    TF_EQ(eng_audit_begin(g_e, 3), ENG_OK);
    TF_EQ(eng_audit_add(g_e, ENG_T_USERS, 7, &rows[0]), ENG_OK);
    eng_set_actor(g_e, "A-VERY-LONG-STATION-NAME", "LONGUSERNAME");
    TF_EQ(eng_audit_add(g_e, ENG_T_USERS, 0, &rows[1]), ENG_OK);
    TF_EQ(eng_audit_end(g_e), ENG_OK);
    naud_is(2);
    aud_is(1, "1 USERS 7 L ||");
    fld_is(ENG_T_AUDIT, 1, "STATION", "STA1");
    fld_is(ENG_T_AUDIT, 1, "USERNAME", "TESTER");
    fld_is(ENG_T_AUDIT, 1, "ADATE", "20261008");
    fld_is(ENG_T_AUDIT, 1, "ATIME", "12:34:56");
    fld_is(ENG_T_AUDIT, 2, "STATION", "A-VERY-LONG-STAT");
    fld_is(ENG_T_AUDIT, 2, "USERNAME", "LONGUSER");
    fld_is(ENG_T_AUDIT, 2, "FLD", "USERNAME_T");
    TF_STREQ(fld(ENG_T_AUDIT, 2, "NEWVAL"),
             "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxy");
    /* The third reserved ID was not used: a gap. */
    TF_EQ(eng_seq_peek(g_e, ENG_T_AUDIT, &v), ENG_OK);
    TF_EQ(v, 4);
    TF_EQ(eng_audit_add(g_e, ENG_T_USERS, 0, &rows[0]), ENG_EINTERNAL);
    rows[2] = rows[0];
    rows[2].action = ENG_A_PASSWORD;
    rows[2].fld = "PWHASH";
    eng_set_actor(g_e, "STA2", "U2");
    TF_EQ(eng_audit(g_e, ENG_T_USERS, 3, &rows[1]), ENG_OK);
    TF_EQ(eng_audit(g_e, ENG_T_USERS, 3, &rows[2]), ENG_OK);
    naud_is(4);
    aud_is(3, "4 USERS 3 F USERNAME_T||xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                     "xxxxxxxxxy");
    aud_is(4, "5 USERS 3 W PWHASH||");
    check_indexes(ENG_T_AUDIT);
    /* Each row took one ID. */
    TF_EQ(eng_seq_peek(g_e, ENG_T_AUDIT, &v), ENG_OK);
    TF_EQ(v, 6);
}

#ifdef COMPAT_HOST
/* The host build links with dos_today and dos_time wrapped
   (T_EDIT_HOSTWRAP).  While g_mn_left is not 0 they give a clock at
   23:59:59 on 8 October 2026 that passes midnight once g_mn_left more
   dates have been read; otherwise the real clock. */
static int g_mn_left;           /* dates to read before midnight, -1
                                   past it                              */
static int g_mn_past;           /* the clock has passed midnight        */

void __real_dos_today(u16 *year, u8 *month, u8 *day);
void __real_dos_time(u8 *hour, u8 *min, u8 *sec);
void __wrap_dos_today(u16 *year, u8 *month, u8 *day);
void __wrap_dos_time(u8 *hour, u8 *min, u8 *sec);

void __wrap_dos_today(u16 *year, u8 *month, u8 *day)
{
    if (g_mn_left == 0) {
        __real_dos_today(year, month, day);
        return;
    }
    *year = 2026;
    *month = 10;
    *day = (u8)(g_mn_past ? 9 : 8);
    if (!g_mn_past && --g_mn_left == 0) {
        g_mn_past = 1;
        g_mn_left = -1;
    }
}

void __wrap_dos_time(u8 *hour, u8 *min, u8 *sec)
{
    if (g_mn_left == 0) {
        __real_dos_time(hour, min, sec);
        return;
    }
    *hour = (u8)(g_mn_past ? 0 : 23);
    *min = (u8)(g_mn_past ? 0 : 59);
    *sec = (u8)(g_mn_past ? 0 : 59);
}

/* An audit row written as the DOS clock passes midnight, after each of
   its first dates read in turn, has the date and the time of one
   reading: never the new day with the old day's time. */
static void test_midnight(void)
{
    eng_audrow row;
    char at[ENG_DATELEN + ENG_TIMELEN + 2];
    int k;

    if (!fresh())
        return;
    row.action = ENG_A_LOGIN;
    row.fld = NULL;
    row.oldval = NULL;
    row.newval = NULL;
    eng_set_now(g_e, NULL);
    for (k = 1; k <= 3; k++) {
        g_mn_left = k;
        g_mn_past = 0;
        TF_EQ(eng_audit(g_e, ENG_T_USERS, 1, &row), ENG_OK);
        g_mn_left = 0;
        strcpy(at, fld(ENG_T_AUDIT, naud(), "ADATE"));
        strcat(at, " ");
        strcat(at, fld(ENG_T_AUDIT, naud(), "ATIME"));
        if (!TF_CHECK(strcmp(at, "20261008 23:59:59") == 0 ||
                      strcmp(at, "20261009 00:00:00") == 0))
            printf("  midnight after date %d: %s\n", k, at);
    }
    eng_set_now(g_e, &g_now);
}
#endif

/* ---------------------------------------------------------------------
 * Edits and their audit rows.
 * --------------------------------------------------------------------- */

static void test_edit_audit(void)
{
    static const char d60[] =
        "123456789012345678901234567890123456789012345678901234567890";
    u32 r;
    u32 a;

    if (!fresh())
        return;
    eng_set_actor(g_e, "DESK", "BOB");
    r = new_item(7, "11", "IBM");
    TF_EQ(r, 1);
    naud_is(1);
    aud_is(1, "1 ITEMS 7 I ITEM_ID||7");
    fld_is(ENG_T_AUDIT, 1, "STATION", "DESK");
    fld_is(ENG_T_ITEMS, r, "CATEGORY", "SYS");
    fld_is(ENG_T_ITEMS, r, "LOC_ID", "11");
    check_indexes(ENG_T_ITEMS);

    /* recid 0: the record number. */
    begin_ok(ENG_T_LOCS, 0);
    put_ok("LOC_ID", "12");
    put_ok("LOC_CODE", "b12");
    commit_ok(0);
    aud_is(2, "2 LOCS 1 I LOC_ID||12");

    /* Updates: U rows in field order, M for LOC_ID, P for PARENT_ID,
       values cut, nothing for UPDATED. */
    a = naud();
    begin_ok(ENG_T_ITEMS, r);
    put_ok("MFR", "Compaq");
    put_ok("DESCR", d60);
    put_ok("QTY", "12");
    put_ok("LOC_ID", "12");
    TF_EQ(putnum(ENG_T_ITEMS, "PARENT_ID", 3), ENG_OK);
    put_ok("ACQ_PRICE", "12.5");
    put_ok("UPDATED", "20261009");
    put_ok("MODEL", "5150");
    commit_ok(7);
    naud_is(a + 6);
    aud_is(a + 1, "3 ITEMS 7 U MFR|IBM|Compaq");
    aud_is(a + 2, "4 ITEMS 7 U DESCR||"
                         "1234567890123456789012345678901234567890");
    aud_is(a + 3, "5 ITEMS 7 U QTY|1|12");
    aud_is(a + 4, "6 ITEMS 7 M LOC_ID|11|12");
    aud_is(a + 5, "7 ITEMS 7 P PARENT_ID|0|3");
    aud_is(a + 6, "8 ITEMS 7 U ACQ_PRICE||12.50");
    fld_is(ENG_T_ITEMS, r, "UPDATED", "20261009");
    check_indexes(ENG_T_ITEMS);

    /* Only a NOAUDIT field: written, no row. */
    a = naud();
    begin_ok(ENG_T_ITEMS, r);
    put_ok("UPDATED", "20261010");
    commit_ok(7);
    naud_is(a);
    fld_is(ENG_T_ITEMS, r, "UPDATED", "20261010");

    /* No change at all: nothing written. */
    begin_ok(ENG_T_ITEMS, r);
    put_ok("MFR", "Compaq");
    commit_ok(7);
    naud_is(a);
    TF_EQ(dbf_recno(eng_dbf(g_e, ENG_T_ITEMS)), r);

    /* Delete and undelete: one D row each, with UPDATED unaudited. */
    begin_ok(ENG_T_ITEMS, r);
    eng_edit_delete(g_e, 1);
    put_ok("UPDATED", "20261011");
    commit_ok(7);
    naud_is(a + 1);
    aud_is(a + 1, "9 ITEMS 7 D ||DELETED");
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    TF_CHECK(dbf_is_deleted(eng_dbf(g_e, ENG_T_ITEMS)));
    begin_ok(ENG_T_ITEMS, r);
    eng_edit_delete(g_e, 0);
    put_ok("MFR", "IBM");
    commit_ok(7);
    naud_is(a + 3);
    aud_is(a + 2, "10 ITEMS 7 D |DELETED|");
    aud_is(a + 3, "11 ITEMS 7 U MFR|Compaq|IBM");
    check_indexes(ENG_T_ITEMS);
}

/* Continues test_edit_audit on the same files. */
static void test_hidden(void)
{
    u32 a;
    u32 r2;

    /* HIDDEN and NOAUDIT fields of USERS give no rows. */
    begin_ok(ENG_T_USERS, 0);
    TF_EQ(putnum(ENG_T_USERS, "USER_ID", 1), ENG_OK);
    put_ok("USERNAME", "bob");
    put_ok("LEVEL", "2");
    commit_ok(1);
    r2 = dbf_recno(eng_dbf(g_e, ENG_T_USERS));
    a = naud();
    aud_is(a, "12 USERS 1 I USER_ID||1");
    begin_ok(ENG_T_USERS, r2);
    put_ok("SALT", "1A2B");
    TF_EQ(eng_edit_put(g_e, "PWHASH", "E2932989995E645505E7CCF6BC07E18D"),
          ENG_OK);
    put_ok("MUSTCHG", "t");
    put_ok("LASTLOGIN", "20261008");
    commit_ok(1);
    naud_is(a);
    fld_is(ENG_T_USERS, r2, "MUSTCHG", "T");
    fld_is(ENG_T_USERS, r2, "SALT", "1A2B");
    begin_ok(ENG_T_USERS, r2);
    put_ok("PWHASH", "");
    put_ok("FULLNAME", "Bob");
    put_ok("MUSTCHG", "n");
    commit_ok(1);
    naud_is(a + 1);
    aud_is(a + 1, "13 USERS 1 U FULLNAME||Bob");
    check_indexes(ENG_T_USERS);
    check_indexes(ENG_T_AUDIT);
}

/* ---------------------------------------------------------------------
 * Value checks.
 * --------------------------------------------------------------------- */

static void bad_put(const char *field, const char *val, const char *errf)
{
    if (!TF_EQ(eng_edit_put(g_e, field, val), ENG_EARG))
        printf("  %s=\"%s\"\n", field, val);
    TF_STREQ(eng_errfield(g_e), errf);
}

static void good_put(const char *field, const char *val, const char *want)
{
    char out[ENG_MAXVAL];
    int f;

    if (!TF_EQ(eng_edit_put(g_e, field, val), ENG_OK))
        printf("  %s=\"%s\"\n", field, val);
    f = eng_field_num(g_e, ENG_T_ITEMS, field);
    TF_EQ(eng_rec_text(g_e, ENG_T_ITEMS, (u16)f, out), ENG_OK);
    TF_STREQ(out, want);
}

static void test_values(void)
{
    char c31[32];
    char ctl[4];

    if (!fresh())
        return;
    begin_ok(ENG_T_ITEMS, 0);
    memset(c31, 'M', 31);
    c31[31] = '\0';
    bad_put("MFR", c31, "MFR");
    c31[30] = '\0';
    good_put("MFR", c31, c31);
    ctl[0] = 'a';
    ctl[1] = 0x1F;
    ctl[2] = '\0';
    bad_put("MODEL", ctl, "MODEL");
    ctl[1] = 0x7F;
    bad_put("MODEL", ctl, "MODEL");
    ctl[1] = '\t';
    bad_put("MODEL", ctl, "MODEL");
    ctl[1] = (char)0xE9;
    good_put("MODEL", ctl, ctl);
    good_put("cond", "gd", "GD");
    good_put("MFR", "lower", "lower");
    good_put("CATEGORY", "", "");
    TF_EQ(eng_edit_required(g_e), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "CATEGORY");
    good_put("CATEGORY", "cpu", "CPU");
    TF_EQ(eng_edit_required(g_e), ENG_OK);
    bad_put("NOSUCH", "1", "NOSUCH");

    good_put("QTY", "42", "42");
    good_put("QTY", "", "");
    good_put("QTY", "0", "0");
    good_put("QTY", "00099", "99");
    good_put("QTY", "99999", "99999");
    bad_put("QTY", "100000", "QTY");
    bad_put("QTY", "-1", "QTY");
    good_put("QTY", "-0", "0");
    bad_put("QTY", "1.0", "QTY");
    bad_put("QTY", "1x", "QTY");
    bad_put("QTY", "+1", "QTY");
    bad_put("QTY", " 1", "QTY");
    bad_put("QTY", "-", "QTY");
    bad_put("QTY", "99999999999999999999", "QTY");
    /* The largest i32 is a number, too wide for QTY; one more is none. */
    bad_put("QTY", "2147483647", "QTY");
    TF_STREQ(eng_errmsg(g_e), "bad value");
    bad_put("QTY", "2147483648", "QTY");
    TF_STREQ(eng_errmsg(g_e), "not a number");
    good_put("ACQ_PRICE", "12.5", "12.50");
    good_put("ACQ_PRICE", "0.05", "0.05");
    good_put("ACQ_PRICE", "999999.99", "999999.99");
    bad_put("ACQ_PRICE", "1000000", "ACQ_PRICE");
    bad_put("ACQ_PRICE", "1.234", "ACQ_PRICE");
    bad_put("ACQ_PRICE", "1.", "ACQ_PRICE");
    bad_put("ACQ_PRICE", ".5", "ACQ_PRICE");
    bad_put("ACQ_PRICE", "-2.5", "ACQ_PRICE");
    good_put("ACQ_PRICE", "-0.00", "0.00");
    good_put("PARENT_ID", "-5", "-5");
    good_put("PARENT_ID", "99999999", "99999999");
    bad_put("PARENT_ID", "100000000", "PARENT_ID");
    good_put("PARENT_ID", "-9999999", "-9999999");
    bad_put("PARENT_ID", "-10000000", "PARENT_ID");
    TF_EQ(putnum(ENG_T_ITEMS, "QTY", (i32)100000L), ENG_EARG);
    TF_EQ(putnum(ENG_T_ITEMS, "ACQ_PRICE", 1234), ENG_OK);
    good_put("ACQ_PRICE", "12.34", "12.34");

    good_put("ACQ_DATE", "19840229", "19840229");
    good_put("ACQ_DATE", "", "");
    bad_put("ACQ_DATE", "19850229", "ACQ_DATE");
    bad_put("ACQ_DATE", "1985-1-1", "ACQ_DATE");
    bad_put("ACQ_DATE", "2026100", "ACQ_DATE");
    eng_edit_cancel(g_e);
    begin_ok(ENG_T_USERS, 0);
    put_ok("MUSTCHG", "y");
    put_ok("MUSTCHG", "N");
    put_ok("MUSTCHG", "");
    TF_EQ(eng_edit_put(g_e, "MUSTCHG", "?"), ENG_EARG);
    TF_EQ(eng_edit_put(g_e, "MUSTCHG", "TRUE"), ENG_EARG);
    TF_EQ(eng_edit_required(g_e), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "LEVEL");
    eng_edit_cancel(g_e);
}

/* Continues test_values on the same files. */
static void test_apply(void)
{
    static const eng_pair ok[] = { { "MFR", "Apple" }, { "QTY", "3" } };
    static const eng_pair ro[] = { { "MFR", "Atari" }, { "ITEM_ID", "9" } };
    static const eng_pair unk[] = { { "MFR", "Atari" }, { "COLOUR", "R" } };
    static const eng_pair badv[] = { { "MFR", "Atari" }, { "QTY", "x" } };
    char out[ENG_MAXVAL];
    u32 r;

    /* apply: a denied or unknown key stores nothing. */
    begin_ok(ENG_T_ITEMS, 0);
    TF_EQ(eng_edit_apply(g_e, ro, 2, ENG_F_RO), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "ITEM_ID");
    TF_EQ(eng_edit_apply(g_e, unk, 2, ENG_F_RO), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "COLOUR");
    TF_EQ(eng_rec_text(g_e, ENG_T_ITEMS, g_f_mfr, out), ENG_OK);
    TF_STREQ(out, "");
    TF_EQ(eng_edit_apply(g_e, badv, 2, ENG_F_RO), ENG_EARG);
    TF_STREQ(eng_errfield(g_e), "QTY");
    TF_EQ(eng_edit_apply(g_e, ok, 2, ENG_F_RO), ENG_OK);
    put_ok("CATEGORY", "SYS");
    TF_EQ(putnum(ENG_T_ITEMS, "ITEM_ID", 1), ENG_OK);
    commit_ok(1);
    r = dbf_recno(eng_dbf(g_e, ENG_T_ITEMS));
    fld_is(ENG_T_ITEMS, r, "MFR", "Apple");
    fld_is(ENG_T_ITEMS, r, "QTY", "3");

    /* Cancel restores the record; a second begin is refused and drops
       the stale edit; edit calls without an edit fail. */
    begin_ok(ENG_T_ITEMS, r);
    put_ok("MFR", "Atari");
    eng_edit_cancel(g_e);
    fld_is(ENG_T_ITEMS, r, "MFR", "Apple");
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    begin_ok(ENG_T_ITEMS, r);
    put_ok("MFR", "Atari");
    TF_EQ(eng_edit_begin(g_e, ENG_T_LOCS, 0), ENG_EINTERNAL);
    TF_EQ(eng_rec_text(g_e, ENG_T_ITEMS, g_f_mfr, out), ENG_OK);
    TF_STREQ(out, "Apple");
    TF_EQ(eng_edit_put(g_e, "MFR", "Atari"), ENG_EINTERNAL);
    TF_EQ(eng_edit_commit(g_e, 1), ENG_EINTERNAL);
    begin_ok(ENG_T_ITEMS, r);
    commit_ok(1);
    TF_EQ(eng_edit_begin(g_e, ENG_T_ITEMS, 99), ENG_EIO);
    begin_ok(ENG_T_ITEMS, r);
    eng_edit_cancel(g_e);
}

/* ---------------------------------------------------------------------
 * Memos.
 * --------------------------------------------------------------------- */

static void test_memo(void)
{
    char *big;
    u32 r;
    u32 a;
    u32 b1;
    u32 b2;
    u16 len;
    u16 i;

    /* The longest values are built on the heap, to keep near data small. */
    big = (char *)malloc(ENG_MAXMEMO + 2);
    if (!TF_CHECK(big != NULL) || !fresh()) {
        free(big);
        return;
    }
    begin_ok(ENG_T_ITEMS, 0);
    TF_EQ(putnum(ENG_T_ITEMS, "ITEM_ID", 1), ENG_OK);
    put_ok("CATEGORY", "SYS");
    put_ok("NOTES", "a\nb\r\nc\td\n");
    commit_ok(1);
    r = dbf_recno(eng_dbf(g_e, ENG_T_ITEMS));
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    TF_EQ(eng_memo_read(g_e, ENG_T_ITEMS, g_f_notes, &len), ENG_OK);
    TF_EQ(len, 11);
    TF_STREQ(eng_memobuf(g_e), "a\r\nb\r\nc\td\r\n");
    TF_EQ(dbf_get_memo(eng_dbf(g_e, ENG_T_ITEMS), g_f_notes, &b1), DBF_OK);
    TF_EQ(b1, 1);

    /* Refused text: a lone CR, control bytes, 0x1A. */
    a = naud();
    begin_ok(ENG_T_ITEMS, r);
    bad_put("NOTES", "a\rb", "NOTES");
    bad_put("NOTES", "a\r", "NOTES");
    bad_put("NOTES", "a\001", "NOTES");
    bad_put("NOTES", "a\032", "NOTES");
    /* The limit counts stored bytes: ENG_MAXMEMO - 1 bytes and one LF is
       one byte too many, ENG_MAXMEMO - 2 and an LF fits. */
    for (i = 0; i < ENG_MAXMEMO; i++)
        big[i] = (char)(i % 50 == 49 ? '\n' : 'a' + i % 26);
    big[ENG_MAXMEMO] = '\0';
    bad_put("NOTES", big, "NOTES");
    for (i = 0; i < ENG_MAXMEMO; i++)
        big[i] = 'z';
    big[ENG_MAXMEMO - 1] = '\n';
    bad_put("NOTES", big, "NOTES");
    big[ENG_MAXMEMO - 1] = '\0';
    big[ENG_MAXMEMO - 2] = '\n';
    put_ok("NOTES", big);
    big[ENG_MAXMEMO - 2] = 'z';
    big[ENG_MAXMEMO - 1] = 'z';
    big[ENG_MAXMEMO] = 'z';
    big[ENG_MAXMEMO + 1] = '\0';
    bad_put("NOTES", big, "NOTES");
    big[ENG_MAXMEMO] = '\0';
    put_ok("NOTES", big);
    commit_ok(1);
    naud_is(a + 1);
    aud_is(a + 1, "2 ITEMS 1 U NOTES|MEMO|MEMO");
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    TF_EQ(eng_memo_read(g_e, ENG_T_ITEMS, g_f_notes, &len), ENG_OK);
    TF_EQ(len, ENG_MAXMEMO);
    free(big);
    TF_EQ(dbf_get_memo(eng_dbf(g_e, ENG_T_ITEMS), g_f_notes, &b2), DBF_OK);
    TF_EQ(b2, 2);
}

/* Continues test_memo on the same files: item 1 is record 1. */
static void test_memo2(void)
{
    u32 r;
    u32 a;
    u32 b1;
    u16 len;

    r = 1;
    /* putmemo stores the buffer as it is. */
    begin_ok(ENG_T_ITEMS, r);
    strcpy(eng_memobuf(g_e), "raw\rtext\n");
    TF_EQ(eng_edit_putmemo(g_e, g_f_notes, 9), ENG_OK);
    TF_EQ(eng_edit_putmemo(g_e, g_f_notes, ENG_MAXMEMO + 1), ENG_ECONSTR);
    TF_EQ(eng_edit_putmemo(g_e, g_f_mfr, 1), ENG_EARG);
    TF_EQ(eng_edit_putmemo(g_e, eng_table_def(ENG_T_ITEMS)->nfields, 1),
          ENG_EINTERNAL);
    TF_EQ(eng_edit_putmemo(g_e, g_f_notes, 9), ENG_OK);
    commit_ok(1);
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    TF_EQ(eng_memo_read(g_e, ENG_T_ITEMS, g_f_notes, &len), ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "raw\rtext\n");

    /* An empty memo clears the field; clearing it again is no change. */
    a = naud();
    begin_ok(ENG_T_ITEMS, r);
    put_ok("NOTES", "");
    commit_ok(1);
    naud_is(a + 1);
    aud_is(a + 1, "4 ITEMS 1 U NOTES|MEMO|");
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    TF_EQ(dbf_get_memo(eng_dbf(g_e, ENG_T_ITEMS), g_f_notes, &b1), DBF_OK);
    TF_EQ(b1, 0);
    begin_ok(ENG_T_ITEMS, r);
    put_ok("NOTES", "");
    commit_ok(1);
    naud_is(a + 1);

    /* A memo of a table without memo field cannot exist; LOCS has one. */
    begin_ok(ENG_T_LOCS, 0);
    put_ok("LOC_CODE", "A1");
    put_ok("NOTES", "shelf\n");
    commit_ok(0);
    TF_EQ(eng_rec_read(g_e, ENG_T_LOCS,
                       dbf_recno(eng_dbf(g_e, ENG_T_LOCS))), ENG_OK);
    TF_EQ(eng_memo_read(g_e, ENG_T_LOCS, g_f_lnotes, &len), ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "shelf\r\n");

    /* A memo added where there was none: OLDVAL blank. */
    a = naud();
    begin_ok(ENG_T_ITEMS, r);
    put_ok("NOTES", "back again");
    commit_ok(1);
    naud_is(a + 1);
    aud_is(a + 1, "6 ITEMS 1 U NOTES||MEMO");

    /* A memo whose block holds empty text counts as empty. */
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    TF_EQ(dbt_append(eng_dbt(g_e, ENG_T_ITEMS), "", 0, &b1), DBF_OK);
    TF_EQ(dbt_flush(eng_dbt(g_e, ENG_T_ITEMS)), DBF_OK);
    TF_EQ(dbf_put_memo(eng_dbf(g_e, ENG_T_ITEMS), g_f_notes, b1), DBF_OK);
    TF_EQ(dbf_write(eng_dbf(g_e, ENG_T_ITEMS)), DBF_OK);
    begin_ok(ENG_T_ITEMS, r);
    put_ok("NOTES", "new");
    commit_ok(1);
    naud_is(a + 2);
    aud_is(a + 2, "7 ITEMS 1 U NOTES||MEMO");
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    TF_EQ(dbf_put_memo(eng_dbf(g_e, ENG_T_ITEMS), g_f_notes, b1), DBF_OK);
    TF_EQ(dbf_write(eng_dbf(g_e, ENG_T_ITEMS)), DBF_OK);
    begin_ok(ENG_T_ITEMS, r);
    put_ok("NOTES", "");
    commit_ok(1);
    naud_is(a + 2);
}

/* ---------------------------------------------------------------------
 * Index upkeep, copy and reuse.
 * --------------------------------------------------------------------- */

static void test_indexes(void)
{
    u8 key[ENG_MAXKEYLEN];
    u32 r[6];
    u32 found;
    u16 i;
    int exact;

    if (!fresh())
        return;
    for (i = 0; i < 6; i++)
        r[i] = new_item((u32)(i + 1), i % 2 ? "11" : "22", "IBM");
    check_indexes(ENG_T_ITEMS);

    /* Changing keys moves only the affected entries. */
    begin_ok(ENG_T_ITEMS, r[2]);
    put_ok("LOC_ID", "33");
    put_ok("SERIALNO", "sn-1");
    TF_EQ(putnum(ENG_T_ITEMS, "PARENT_ID", 2), ENG_OK);
    commit_ok(3);
    check_indexes(ENG_T_ITEMS);
    memcpy(key, "      33       3", 16);
    TF_EQ(ndx_seek(eng_ndx(g_e, ENG_X_ITEMS2), key, 16, &found, &exact),
          DBF_OK);
    TF_EQ(found, r[2]);
    TF_EQ(exact, 1);
    memcpy(key, "SN-1", 4);
    TF_EQ(ndx_seek(eng_ndx(g_e, ENG_X_ITEMS5), key, 4, &found, &exact),
          DBF_OK);
    TF_EQ(found, r[2]);
    TF_EQ(exact, 1);
    TF_EQ(eng_seek_live(g_e, ENG_X_ITEMS2, "B2           3", &found),
          ENG_OK);
    TF_EQ(found, 0);
    TF_EQ(eng_seek_live(g_e, ENG_X_ITEMS3, "       2       3", &found),
          ENG_OK);
    TF_EQ(found, r[2]);

    /* Deleting keeps the entries, as dBASE does. */
    begin_ok(ENG_T_ITEMS, r[4]);
    eng_edit_delete(g_e, 1);
    commit_ok(5);
    check_indexes(ENG_T_ITEMS);
    reopen();
    check_indexes(ENG_T_ITEMS);
}

/* Continues test_indexes on the same files: items 1..6 are records
   1..6, item 5 deleted. */
static void test_copy(void)
{
    u32 r[6];
    u32 found;
    u32 a;
    u32 c;
    u16 len;
    u16 i;

    for (i = 0; i < 6; i++)
        r[i] = (u32)(i + 1);
    /* begin_copy: a new record with every field and the memo. */
    begin_ok(ENG_T_ITEMS, r[1]);
    put_ok("NOTES", "line one\nline two");
    put_ok("TAGS", "xt,8088");
    commit_ok(2);
    a = naud();
    TF_EQ(eng_edit_begin_copy(g_e, ENG_T_ITEMS, r[1]), ENG_OK);
    TF_EQ(putnum(ENG_T_ITEMS, "ITEM_ID", 77), ENG_OK);
    put_ok("SERIALNO", "copy");
    commit_ok(77);
    c = dbf_recno(eng_dbf(g_e, ENG_T_ITEMS));
    TF_EQ(c, 7);
    naud_is(a + 1);
    aud_is(a + 1, "13 ITEMS 77 I ITEM_ID||77");
    fld_is(ENG_T_ITEMS, c, "TAGS", "xt,8088");
    fld_is(ENG_T_ITEMS, c, "LOC_ID", "11");
    fld_is(ENG_T_ITEMS, c, "SERIALNO", "copy");
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, c), ENG_OK);
    TF_CHECK(!dbf_is_deleted(eng_dbf(g_e, ENG_T_ITEMS)));
    TF_EQ(eng_memo_read(g_e, ENG_T_ITEMS, g_f_notes, &len), ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "line one\r\nline two");
    TF_EQ(dbf_get_memo(eng_dbf(g_e, ENG_T_ITEMS), g_f_notes, &found), DBF_OK);
    TF_EQ(found, 2);
    fld_is(ENG_T_ITEMS, r[1], "SERIALNO", "");
    TF_EQ(dbf_get_memo(eng_dbf(g_e, ENG_T_ITEMS), g_f_notes, &found), DBF_OK);
    TF_EQ(found, 1);
    check_indexes(ENG_T_ITEMS);
    /* A copy of a deleted record is live; with a memo replaced; and a
       cancelled copy appends nothing. */
    TF_EQ(eng_edit_begin_copy(g_e, ENG_T_ITEMS, r[4]), ENG_OK);
    TF_EQ(putnum(ENG_T_ITEMS, "ITEM_ID", 78), ENG_OK);
    put_ok("NOTES", "fresh");
    commit_ok(78);
    c = dbf_recno(eng_dbf(g_e, ENG_T_ITEMS));
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, c), ENG_OK);
    TF_CHECK(!dbf_is_deleted(eng_dbf(g_e, ENG_T_ITEMS)));
    TF_EQ(eng_memo_read(g_e, ENG_T_ITEMS, g_f_notes, &len), ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "fresh");
    TF_EQ(eng_edit_begin_copy(g_e, ENG_T_ITEMS, r[1]), ENG_OK);
    eng_edit_cancel(g_e);
    TF_EQ(eng_reccount(g_e, ENG_T_ITEMS), 8);
    check_indexes(ENG_T_ITEMS);
}

/* Continues test_copy on the same files. */
static void test_reuse(void)
{
    u32 found;
    u32 a;
    u32 c;

    /* begin_reuse: over a deleted LOCS record, its old key replaced. */
    begin_ok(ENG_T_LOCS, 0);
    put_ok("LOC_ID", "8");
    put_ok("LOC_CODE", "OLD");
    put_ok("LOC_NAME", "attic");
    put_ok("NOTES", "old notes");
    commit_ok(0);
    c = dbf_recno(eng_dbf(g_e, ENG_T_LOCS));
    TF_EQ(eng_edit_begin_reuse(g_e, ENG_T_LOCS, c), ENG_ECONSTR);
    TF_EQ(eng_edit_begin_reuse(g_e, ENG_T_LOCS, 0), ENG_ECONSTR);
    TF_EQ(eng_edit_begin_reuse(g_e, ENG_T_LOCS, 9), ENG_ECONSTR);
    begin_ok(ENG_T_LOCS, c);
    eng_edit_delete(g_e, 1);
    commit_ok(0);
    a = naud();
    TF_EQ(eng_edit_begin_reuse(g_e, ENG_T_LOCS, c), ENG_OK);
    put_ok("LOC_ID", "9");
    put_ok("LOC_CODE", "NEW");
    commit_ok(0);
    TF_EQ(dbf_recno(eng_dbf(g_e, ENG_T_LOCS)), c);
    TF_EQ(eng_reccount(g_e, ENG_T_LOCS), 1);
    naud_is(a + 1);
    aud_is(a + 1, "17 LOCS 1 I LOC_ID||9");
    fld_is(ENG_T_LOCS, c, "LOC_NAME", "");
    TF_EQ(eng_rec_read(g_e, ENG_T_LOCS, c), ENG_OK);
    TF_CHECK(!dbf_is_deleted(eng_dbf(g_e, ENG_T_LOCS)));
    TF_EQ(dbf_get_memo(eng_dbf(g_e, ENG_T_LOCS), g_f_lnotes, &found), DBF_OK);
    TF_EQ(found, 0);
    TF_EQ(eng_seek_live(g_e, ENG_X_LOCS3, "NEW", &found), ENG_OK);
    TF_EQ(found, c);
    TF_EQ(eng_seek_live(g_e, ENG_X_LOCS3, "OLD", &found), ENG_OK);
    TF_EQ(found, 0);
    TF_EQ(eng_seek_live(g_e, ENG_X_LOCS1, "       9", &found), ENG_OK);
    TF_EQ(found, c);
    check_indexes(ENG_T_LOCS);
    /* A cancelled reuse leaves the deleted record as it was. */
    begin_ok(ENG_T_LOCS, c);
    eng_edit_delete(g_e, 1);
    commit_ok(0);
    TF_EQ(eng_edit_begin_reuse(g_e, ENG_T_LOCS, c), ENG_OK);
    put_ok("LOC_CODE", "OTHER");
    eng_edit_cancel(g_e);
    fld_is(ENG_T_LOCS, c, "LOC_CODE", "NEW");
    TF_CHECK(dbf_is_deleted(eng_dbf(g_e, ENG_T_LOCS)));
    /* CATS has no index: reuse writes the record only. */
    begin_ok(ENG_T_CATS, 0);
    put_ok("CAT_CODE", "OLD");
    put_ok("CAT_NAME", "Old");
    eng_edit_delete(g_e, 1);
    commit_ok(0);
    TF_EQ(eng_edit_begin_reuse(g_e, ENG_T_CATS, 1), ENG_OK);
    put_ok("CAT_CODE", "NEW");
    put_ok("CAT_NAME", "New");
    commit_ok(0);
    TF_EQ(eng_reccount(g_e, ENG_T_CATS), 1);
    fld_is(ENG_T_CATS, 1, "CAT_NAME", "New");
    TF_STREQ(aud(naud()), "20 CATS 1 I CAT_CODE||NEW");
    check_indexes(ENG_T_AUDIT);
}

/* ---------------------------------------------------------------------
 * Puts by field number, loan audit rows, quiet commits.
 * --------------------------------------------------------------------- */

/* The last audit row without its AUD_ID. */
static void last_aud_is(const char *want)
{
    if (!TF_STREQ(strchr(aud(naud()), ' ') + 1, want))
        printf("  last audit row\n");
}

/* putf as put, putnumf's N checks, and a field number out of range;
   ON_LOAN changes give O and R, and none when the value stays equal. */
static void test_putf(void)
{
    u16 f;
    u16 nf;

    if (!fresh())
        return;
    new_item(4, "11", "IBM");
    /* Field numbers looked up, not ENG_FIELD: ENGFLD.H would cost this
       file more compiler memory than Watcom C 6.5 has left. */
    f = (u16)eng_field_num(g_e, ENG_T_ITEMS, "ON_LOAN");
    nf = eng_table_def(ENG_T_ITEMS)->nfields;
    /* Blank to 0 leaves the value equal: no row. */
    begin_ok(ENG_T_ITEMS, 1);
    TF_EQ(eng_edit_putnumf(g_e, f, 0), ENG_OK);
    commit_ok(4);
    naud_is(1);
    fld_is(ENG_T_ITEMS, 1, "ON_LOAN", "0");
    begin_ok(ENG_T_ITEMS, 1);
    TF_EQ(eng_edit_putnumf(g_e, f, 2), ENG_OK);
    commit_ok(4);
    last_aud_is("ITEMS 4 O ON_LOAN|0|2");
    begin_ok(ENG_T_ITEMS, 1);
    TF_EQ(eng_edit_putf(g_e, f, "1"), ENG_OK);
    TF_EQ(eng_edit_putf(g_e, g_f_mfr, "kaypro"), ENG_OK);
    TF_EQ(eng_edit_putf(g_e, f, "-1"), ENG_EARG);
    TF_EQ(eng_edit_putnumf(g_e, f, -1), ENG_EARG);
    TF_EQ(eng_edit_putnumf(g_e, g_f_mfr, 1), ENG_EARG);
    TF_STREQ(eng_errmsg(g_e), "not a number field");
    TF_EQ(eng_edit_putf(g_e, nf, "x"), ENG_EINTERNAL);
    TF_EQ(eng_edit_putnumf(g_e, nf, 1), ENG_EINTERNAL);
    commit_ok(4);
    last_aud_is("ITEMS 4 R ON_LOAN|2|1");
    fld_is(ENG_T_ITEMS, 1, "MFR", "kaypro");
    begin_ok(ENG_T_ITEMS, 1);
    TF_EQ(eng_edit_putnumf(g_e, f, 3), ENG_OK);
    commit_ok(4);
    last_aud_is("ITEMS 4 O ON_LOAN|1|3");
    eng_edit_cancel(g_e);
    TF_EQ(eng_edit_putf(g_e, f, "1"), ENG_EINTERNAL);
}

/* A quiet commit writes the record and no audit row; the next commit
   writes its rows again, and without an edit eng_edit_quiet does
   nothing. */
static void test_quiet(void)
{
    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    naud_is(1);
    eng_edit_quiet(g_e);
    begin_ok(ENG_T_ITEMS, 1);
    put_ok("MFR", "Compaq");
    eng_edit_quiet(g_e);
    commit_ok(1);
    naud_is(1);
    fld_is(ENG_T_ITEMS, 1, "MFR", "Compaq");
    begin_ok(ENG_T_ITEMS, 1);
    put_ok("MFR", "IBM");
    commit_ok(1);
    naud_is(2);
    last_aud_is("ITEMS 1 U MFR|Compaq|IBM");
}

int main(int argc, char **argv)
{
    tf_begin("T_EDIT");
    tfs_set_dir(argc > 1 ? argv[1] : "");
    t_idkey();
    test_seq();
    test_audit();
#ifdef COMPAT_HOST
    test_midnight();
#endif
    test_edit_audit();
    test_hidden();
    test_values();
    test_apply();
    test_memo();
    test_memo2();
    t_note();
    t_oldmemo();
    t_stage();
    test_indexes();
    test_copy();
    test_reuse();
    t_marker();
    t_markpad();
    test_putf();
    t_batch();
    test_quiet();
    t_dbferr();
    if (g_e != NULL)
        eng_close(g_e);
    te_remove(tfs_dir(), NULL);
    return tf_done();
}
