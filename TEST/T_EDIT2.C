/*
 * T_EDIT2.C - the second half of the T_EDIT test (see T_EDIT.C): the ID
 * keys (eng_id_key and eng_key_id, which need no data directory), note
 * lines (eng_edit_begin_note), the old memo a commit asks about, the
 * ENG_EDITMARK marker of changes
 * stopped or failed at every disk write, and batches.  It is a file of
 * its own only to stay within the compiler memory of Watcom C 6.5;
 * T_EDIT.H holds what both share.
 */
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGID.H"
#include "ENGEDIT.H"
#include "ENGNOTE.H"
#define TF_PART
#define TF_FILE "T_EDIT2"
#include "TESTFW.H"
#include "T_FILES.H"
#include "T_EDIT.H"

static char g_mark[ENG_MARKMAX + 1];    /* marker's result              */
static long g_markn;            /* its count line's value, or -1        */
static u8 g_log[TFS_MAXOPS];    /* the ops of a change run undisturbed  */
static u16 g_reads;             /* reads seen by count_read             */

/* eng_id_key writes an ID right-aligned in ENG_IDLEN characters and a
   NUL, nothing past them, and refuses 0 and IDs over ENG_MAXID with "";
   eng_key_id reads exactly len bytes of such a key back and gives 0 for
   anything else. */
void t_idkey(void)
{
    char k[ENG_IDLEN + 4];
    u8 b[ENG_IDLEN + 2];
    u32 ids[7];
    int i;

    memset(k, '#', sizeof k);
    TF_EQ(eng_id_key(1, k), 1);
    TF_STREQ(k, "       1");
    TF_CHECK(k[ENG_IDLEN + 1] == '#' && k[ENG_IDLEN + 2] == '#' &&
             k[ENG_IDLEN + 3] == '#');
    memset(k, '#', sizeof k);
    TF_EQ(eng_id_key(12, k), 1);
    TF_STREQ(k, "      12");
    TF_CHECK(k[ENG_IDLEN + 1] == '#');
    memset(k, '#', sizeof k);
    TF_EQ(eng_id_key(ENG_MAXID, k), 1);
    TF_STREQ(k, "99999999");
    TF_CHECK(k[ENG_IDLEN + 1] == '#');

    memset(k, '#', sizeof k);
    TF_EQ(eng_id_key(0, k), 0);
    TF_STREQ(k, "");
    TF_CHECK(k[ENG_IDLEN + 1] == '#');
    memset(k, '#', sizeof k);
    TF_EQ(eng_id_key(ENG_MAXID + 1, k), 0);
    TF_STREQ(k, "");
    TF_CHECK(k[ENG_IDLEN + 1] == '#');
    memset(k, '#', sizeof k);
    TF_EQ(eng_id_key(0xFFFFFFFFUL, k), 0);
    TF_STREQ(k, "");
    TF_CHECK(k[ENG_IDLEN + 1] == '#');

    ids[0] = 1;
    ids[1] = 9;
    ids[2] = 10;
    ids[3] = 65535UL;
    ids[4] = 65536UL;
    ids[5] = 12345678UL;
    ids[6] = ENG_MAXID;
    for (i = 0; i < 7; i++) {
        memset(k, '#', sizeof k);
        if (!TF_EQ(eng_id_key(ids[i], k), 1) ||
            !TF_EQ(strlen(k), ENG_IDLEN) ||
            !TF_CHECK(k[ENG_IDLEN + 1] == '#') ||
            !TF_EQ(eng_key_id((const u8 *)k, ENG_IDLEN), ids[i]))
            printf("  id %lu\n", (unsigned long)ids[i]);
    }

    TF_EQ(eng_key_id((const u8 *)"      12", ENG_IDLEN), 12);
    TF_EQ(eng_key_id((const u8 *)"       0", ENG_IDLEN), 0);
    TF_EQ(eng_key_id((const u8 *)"        ", ENG_IDLEN), 0);
    TF_EQ(eng_key_id((const u8 *)"  12 3  ", ENG_IDLEN), 0);
    TF_EQ(eng_key_id((const u8 *)"      1 ", ENG_IDLEN), 0);
    TF_EQ(eng_key_id((const u8 *)" x     1", ENG_IDLEN), 0);
    TF_EQ(eng_key_id((const u8 *)"1234567x", ENG_IDLEN), 0);
    TF_EQ(eng_key_id((const u8 *)"     -12", ENG_IDLEN), 0);
    TF_EQ(eng_key_id((const u8 *)"999999999", 9), 0);
    TF_EQ(eng_key_id((const u8 *)"       1 ", 9), 0);
    TF_EQ(eng_key_id((const u8 *)"", 0), 0);
    TF_EQ(eng_key_id((const u8 *)"    1", 5), 1);
    TF_EQ(eng_key_id((const u8 *)"    1x", 5), 1);

    memcpy(b, "      12", ENG_IDLEN);
    b[ENG_IDLEN] = 'x';
    b[ENG_IDLEN + 1] = '7';
    TF_EQ(eng_key_id(b, ENG_IDLEN), 12);
    memcpy(b, "99999999", ENG_IDLEN);
    b[ENG_IDLEN] = '9';
    TF_EQ(eng_key_id(b, ENG_IDLEN), ENG_MAXID);
}

/* eng_edit_begin_note on item 1: the M rules, one text for every
   refused byte; the CR LF before the line counts toward the limit, and
   the first fault met is reported, so a refused byte at the limit is no
   overflow. */
void t_note(void)
{
    u32 r;
    u16 f;

    if (!fresh())
        return;
    r = new_item(1, "11", "IBM");
    f = (u16)eng_field_num(g_e, ENG_T_ITEMS, "NOTES");
    TF_EQ(eng_edit_begin_note(g_e, ENG_T_ITEMS, f, r, "a\rb", ENG_NOTE_ADD),
          ENG_EARG);
    TF_STREQ(eng_errmsg(g_e), "bad memo text");
    TF_EQ(eng_edit_begin_note(g_e, ENG_T_ITEMS, f, r, "a\001", ENG_NOTE_ADD),
          ENG_EARG);
    TF_STREQ(eng_errmsg(g_e), "bad memo text");
    TF_STREQ(eng_errfield(g_e), "NOTES");
    TF_EQ(eng_edit_begin_note(g_e, ENG_T_ITEMS, f, r, "one\ntwo",
                              ENG_NOTE_ADD), ENG_OK);
    commit_ok(1);
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    TF_EQ(eng_edit_begin_note(g_e, ENG_T_ITEMS, f, r, "three", ENG_NOTE_ADD),
          ENG_OK);
    TF_STREQ(eng_memobuf(g_e), "one\r\ntwo\r\nthree");
    eng_edit_cancel(g_e);

    begin_ok(ENG_T_ITEMS, r);
    memset(eng_memobuf(g_e), 'q', ENG_MAXMEMO);
    TF_EQ(eng_edit_putmemo(g_e, f, ENG_MAXMEMO - 2), ENG_OK);
    commit_ok(1);
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    TF_EQ(eng_edit_begin_note(g_e, ENG_T_ITEMS, f, r, "\001", ENG_NOTE_ADD),
          ENG_EARG);
    TF_EQ(eng_edit_begin_note(g_e, ENG_T_ITEMS, f, r, "x", ENG_NOTE_ADD),
          ENG_ECONSTR);
    TF_STREQ(eng_errmsg(g_e), "memo too long");
    TF_EQ(eng_edit_begin_note(g_e, ENG_T_ITEMS, f, r, "", ENG_NOTE_ADD),
          ENG_OK);
    TF_EQ(strlen(eng_memobuf(g_e)), ENG_MAXMEMO);
    eng_edit_cancel(g_e);

    begin_ok(ENG_T_ITEMS, r);
    memset(eng_memobuf(g_e), 'q', ENG_MAXMEMO);
    TF_EQ(eng_edit_putmemo(g_e, f, ENG_MAXMEMO - 1), ENG_OK);
    commit_ok(1);
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, r), ENG_OK);
    TF_EQ(eng_edit_begin_note(g_e, ENG_T_ITEMS, f, r,
                              "a note well past the end of the memo",
                              ENG_NOTE_ADD),
          ENG_ECONSTR);
}

/* A cf_fault_rd hook that counts the reads in g_reads. */
static int count_read(int op)
{
    if (op == CF_OP_READ)
        g_reads++;
    return 0;
}

/* The reads of commit_ok(1). */
static u16 commit_reads(void)
{
    g_reads = 0;
    cf_fault_rd = count_read;
    commit_ok(1);
    cf_fault_rd = NULL;
    return g_reads;
}

/* OLDVAL and NEWVAL of the last audit row read as oldv and newv. */
static void last_vals(const char *oldv, const char *newv)
{
    u32 n;

    n = eng_reccount(g_e, ENG_T_AUDIT);
    fld_is(ENG_T_AUDIT, n, "OLDVAL", oldv);
    fld_is(ENG_T_AUDIT, n, "NEWVAL", newv);
}

/* Continues t_note on the same files: item 1 is record 1.  An old memo
   block number that cannot be read counts as text; clearing a memo asks
   twice whether the old one holds text and reads it once, two reads
   (its byte and the byte after it) more than a commit of an unindexed
   field. */
void t_oldmemo(void)
{
    dbf *d;
    u16 reads;
    u16 f;

    d = eng_dbf(g_e, ENG_T_ITEMS);
    f = (u16)eng_field_num(g_e, ENG_T_ITEMS, "NOTES");
    TF_EQ(eng_rec_read(g_e, ENG_T_ITEMS, 1), ENG_OK);
    dbf_recbuf(d)[dbf_field_at(d, f)->offset] = 'x';
    TF_EQ(dbf_write(d), DBF_OK);
    begin_ok(ENG_T_ITEMS, 1);
    put_ok("NOTES", "m");
    commit_ok(1);
    last_vals("MEMO", "MEMO");

    begin_ok(ENG_T_ITEMS, 1);
    put_ok("QTY", "2");
    reads = commit_reads();
    begin_ok(ENG_T_ITEMS, 1);
    put_ok("NOTES", "");
    TF_EQ(commit_reads(), reads + 2);
    last_vals("MEMO", "");
}

/* ---------------------------------------------------------------------
 * The edit marker.
 * --------------------------------------------------------------------- */

/* Arm the fault recorder to fail the next disk write, once. */
static void fail_next_write(void)
{
    tfs_fault(TFS_OP(CF_OP_WRITE), 1, 1, 0);
}

/* Arm the fault recorder to fail every remove. */
static void fail_removes(void)
{
    tfs_fault(TFS_OP(CF_OP_REMOVE), 1, 0, CF_EACCES);
}

/* Keep the recorder's log of the change just run, for fail_call. */
static void keep_log(void)
{
    memcpy(g_log, tfs_ops, sizeof g_log);
}

/* Arm the fault recorder to fail call k + 1 of the change whose log
   keep_log kept (once, or with every later call too when stop) with
   the code a disk gives that call's op: CF_EFULL for a write (or a call
   past the log), CF_EACCES for any other. */
static void fail_call(int k, int stop)
{
    tfs_fault(TFS_ALLOPS, (u32)k + 1u, stop ? 0 : 1,
              k < TFS_MAXOPS && g_log[k] != CF_OP_WRITE ? CF_EACCES : 0);
}

/* The marker's table lines, or "" when there is none, in g_mark; the
   value of its count line, if any, in g_markn (else -1). */
static const char *marker(void)
{
    u16 got;
    u16 i;

    if (tfs_read_all(tfs_path(ENG_EDITMARK, ""), g_mark, ENG_MARKMAX,
                     &got) != 0)
        got = 0;
    g_mark[got] = '\0';
    g_markn = 0;
    for (i = 0; TXT_ISDIGIT(g_mark[i]); i++)
        g_markn = g_markn * 10 + (g_mark[i] - '0');
    if (i > 0 && g_mark[i] == '\r' && g_mark[i + 1] == '\n')
        return g_mark + i + 2;
    g_markn = -1;
    return g_mark;
}

/* The commit of the stop test: a changed key and a new memo. */
static int marked_commit(void)
{
    if (eng_edit_begin(g_e, ENG_T_ITEMS, 1) != ENG_OK)
        return ENG_EINTERNAL;
    put_ok("MFR", "Compaq");
    put_ok("NOTES", "portable");
    return eng_edit_commit(g_e, 1);
}

/* The audit rows of the stop test, outside a commit. */
static int marked_audit(void)
{
    eng_audrow row;

    row.action = ENG_A_LOGIN;
    row.fld = NULL;
    row.oldval = NULL;
    row.newval = NULL;
    return eng_audit(g_e, ENG_T_USERS, 1, &row);
}

/* Run change once to count its disk writes and find the marker's
   creation, then stop it at each of them in turn: the marker left names
   want after the count of AUDIT records before the change. */
static void stop_each(int (*change)(void), const char *want)
{
    long aud;
    int total;
    int create;
    int k;
    int rc;

    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    rc = change();
    tfs_fault_off();
    keep_log();
    TF_EQ(rc, ENG_OK);
    TF_STREQ(marker(), "");
    total = tfs_calls() < TFS_MAXOPS ? (int)tfs_calls() : TFS_MAXOPS;
    for (create = 0; create < total && tfs_ops[create] != CF_OP_CREATE;
         create++)
        ;
    TF_CHECK(create < total);
    TF_CHECK(total > create + 3);
    for (k = create + 2; k < total; k++) {
        if (!fresh())
            return;
        new_item(1, "11", "IBM");
        aud = (long)eng_reccount(g_e, ENG_T_AUDIT);
        fail_call(k, 1);
        rc = change();
        tfs_fault_off();
        /* Only the marker's own removal may fail without an error. */
        TF_CHECK(rc != ENG_OK || tfs_ops[k] == CF_OP_REMOVE);
        if (!TF_STREQ(marker(), want) || !TF_EQ(g_markn, aud))
            printf("  stopped at disk write %d of %d\n", k, total);
    }
}

static void test_marker(void)
{
    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    TF_STREQ(marker(), "");
    /* An unchanged commit writes nothing at all. */
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    begin_ok(ENG_T_ITEMS, 1);
    put_ok("MFR", "IBM");
    commit_ok(1);
    tfs_fault_off();
    TF_EQ(tfs_calls(), 0);
    TF_STREQ(marker(), "");
    stop_each(marked_commit, "ITEMS\r\n");
    stop_each(marked_audit, "AUDIT\r\n");
    /* After a stop the marker stays until the next open: later changes
       add their tables to it but never remove it. */
    TF_EQ(marked_audit(), ENG_OK);
    TF_STREQ(marker(), "AUDIT\r\n");
    TF_EQ(marked_commit(), ENG_OK);
    TF_STREQ(marker(), "AUDIT\r\nITEMS\r\n");
    begin_ok(ENG_T_ITEMS, 1);
    put_ok("MFR", "Tandy");
    commit_ok(1);
    TF_STREQ(marker(), "AUDIT\r\nITEMS\r\n");
    TF_EQ(marked_audit(), ENG_OK);
    begin_ok(ENG_T_LOCS, 0);
    put_ok("LOC_CODE", "A1");
    commit_ok(0);
    TF_STREQ(marker(), "AUDIT\r\nITEMS\r\nLOCS\r\n");
    /* The open repairs and removes it; brackets then close normally. */
    reopen();
    TF_STREQ(marker(), "");
    check_indexes(ENG_T_ITEMS);
    check_indexes(ENG_T_AUDIT);
    begin_ok(ENG_T_ITEMS, 1);
    put_ok("MFR", "Kaypro");
    commit_ok(1);
    TF_STREQ(marker(), "");
    TF_EQ(marked_audit(), ENG_OK);
    TF_STREQ(marker(), "");
}

/* Commits of the single-failure sweeps: a key change of ITEMS, then a
   new LOCS record. */
static int commit_a(void)
{
    if (eng_edit_begin(g_e, ENG_T_ITEMS, 1) != ENG_OK)
        return ENG_EINTERNAL;
    put_ok("MFR", "Osborne");
    put_ok("SERIALNO", "OS-1");
    return eng_edit_commit(g_e, 1);
}

static int commit_b(const char *code)
{
    if (eng_edit_begin(g_e, ENG_T_LOCS, 0) != ENG_OK)
        return ENG_EINTERNAL;
    put_ok("LOC_CODE", code);
    return eng_edit_commit(g_e, 0);
}

static void commit_ok_b(const char *code)
{
    TF_EQ(commit_b(code), ENG_OK);
}

/* After a reopen every index matches its table and the marker is gone. */
static void all_consistent(int k)
{
    reopen();
    if (!TF_STREQ(marker(), ""))
        printf("  sweep call %d\n", k);
    check_indexes(ENG_T_ITEMS);
    check_indexes(ENG_T_LOCS);
    check_indexes(ENG_T_AUDIT);
}

/* Fail exactly one disk write of commit A then commit B, each in turn;
   then a clean commit and a reopen must leave every index right.  Then
   the same with A failing at each of its calls and B failing at its
   first write, the marker's (one failure must not make the marker lose
   the table of another). */
static void test_single_failures(void)
{
    int total;
    int k;

    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    commit_a();
    commit_b("B1");
    tfs_fault_off();
    keep_log();
    total = (int)tfs_calls();
    TF_CHECK(total > 10);
    for (k = 0; k < total; k++) {
        if (!fresh())
            return;
        new_item(1, "11", "IBM");
        fail_call(k, 0);
        commit_a();
        commit_b("B1");
        tfs_fault_off();
        commit_ok_b("C1");
        all_consistent(k);
    }
    for (k = 0; k < total; k++) {
        if (!fresh())
            return;
        new_item(1, "11", "IBM");
        fail_call(k, 0);
        if (commit_a() == ENG_OK) {
            tfs_fault_off();
            continue;
        }
        fail_next_write();
        TF_CHECK(commit_b("B1") != ENG_OK);
        tfs_fault_off();
        commit_ok_b("C1");
        if (marker()[0] != '\0')
            TF_CHECK(strstr(g_mark, "ITEMS\r\n") != NULL);
        all_consistent(k);
    }
}

/* A marker that cannot be removed after a durable change is no error. */
static void test_remove_fails(void)
{
    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    fail_removes();
    TF_EQ(commit_a(), ENG_OK);
    TF_EQ(marked_audit(), ENG_OK);
    tfs_fault_off();
    TF_STREQ(marker(), "ITEMS\r\n");
    TF_EQ(commit_b("B1"), ENG_OK);
    TF_STREQ(marker(), "ITEMS\r\nLOCS\r\n");
    TF_EQ(eng_reccount(g_e, ENG_T_LOCS), 1);
    all_consistent(-1);
    TF_EQ(commit_b("B2"), ENG_OK);
    TF_STREQ(marker(), "");
}

/* While a bracket is open, a change adds nothing to a marker longer
   than ENG_MARKMAX, which an open reads as naming every table, and
   succeeds; the next open rebuilds every index and removes it. */
static void test_long_marker(void)
{
    static char junk[ENG_MARKMAX + 1];

    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    fail_removes();
    TF_EQ(commit_a(), ENG_OK);
    tfs_fault_off();
    memset(junk, 'X', sizeof junk);
    TF_EQ(tfs_write_all(tfs_path(ENG_EDITMARK, ""), junk, (u16)sizeof junk),
          0);
    TF_EQ(commit_b("B1"), ENG_OK);
    TF_EQ(tfs_size(tfs_path(ENG_EDITMARK, "")), sizeof junk);
    all_consistent(-1);
}

/* A marker removed while a bracket is open, as an open in between
   repairs and removes it, starts a new bracket: the next change creates
   it naming its own table and removes it when done.  With no bracket
   open, a change replaces a marker it finds with its own. */
static void test_gone_marker(void)
{
    long aud;

    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    fail_removes();
    TF_EQ(commit_a(), ENG_OK);
    tfs_fault_off();
    TF_STREQ(marker(), "ITEMS\r\n");
    TF_EQ(cf_remove(tfs_path(ENG_EDITMARK, "")), 0);
    TF_EQ(commit_b("B1"), ENG_OK);
    TF_STREQ(marker(), "");
    TF_EQ(tfs_write_all(tfs_path(ENG_EDITMARK, ""), "ITEMS\r\n", 7), 0);
    aud = (long)eng_reccount(g_e, ENG_T_AUDIT);
    fail_removes();
    TF_EQ(commit_b("B2"), ENG_OK);
    tfs_fault_off();
    TF_STREQ(marker(), "LOCS\r\n");
    TF_EQ(g_markn, aud);
}

/* A count left over from a failed change of a closed engine does not
   stop a later engine from marking audit rows. */
static void test_stale_count(void)
{
    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    /* Call 1 creates the marker, 2 writes its line, 3 to 10 fill its
       sector with zeros, 11 cuts it back, 12 is the SEQ write. */
    tfs_fault(TFS_ALLOPS, 12, 1, 0);
    TF_CHECK(commit_a() != ENG_OK);
    tfs_fault_off();
    TF_STREQ(marker(), "ITEMS\r\n");
    reopen();
    TF_STREQ(marker(), "");
    fail_removes();
    TF_EQ(marked_audit(), ENG_OK);
    tfs_fault_off();
    TF_STREQ(marker(), "AUDIT\r\n");
    reopen();
    TF_STREQ(marker(), "");
    check_indexes(ENG_T_AUDIT);
}

/* A commit that fails before its record write leaves the buffer as the
   record was. */
static void test_restore(void)
{
    char out[ENG_MAXVAL];

    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    begin_ok(ENG_T_ITEMS, 1);
    put_ok("MFR", "Compaq");
    /* From the first call on, the marker's create. */
    tfs_fault(TFS_ALLOPS, 1, 0, CF_EACCES);
    TF_CHECK(eng_edit_commit(g_e, 1) != ENG_OK);
    tfs_fault_off();
    TF_EQ(eng_rec_text(g_e, ENG_T_ITEMS, g_f_mfr, out), ENG_OK);
    TF_STREQ(out, "IBM");
}

/* A marker whose create is refused as access denied or a sharing
   violation fails the commit with ENG_EINUSE, as on a full disk with
   ENG_EFULL, each naming ENG_EDITMARK; no marker is left. */
static void test_mark_fail(void)
{
    static const int k_code[3][2] = {
        { CF_EACCES, ENG_EINUSE }, { CF_ESHARE, ENG_EINUSE },
        { CF_EFULL, ENG_EFULL }
    };
    int i;

    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    for (i = 0; i < 3; i++) {
        begin_ok(ENG_T_ITEMS, 1);
        put_ok("MFR", "Compaq");
        tfs_fault(TFS_OP(CF_OP_CREATE), 1, 1, k_code[i][0]);
        TF_EQ(eng_edit_commit(g_e, 1), k_code[i][1]);
        tfs_fault_off();
        TF_STREQ(eng_errmsg(g_e), ENG_EDITMARK);
        TF_STREQ(marker(), "");
    }
}

/* eng_close cancels an open edit and drops an open audit reservation,
   so the next engine starts with neither: its begin succeeds on the
   stored record, and an audit row without a reservation of its own is
   refused.  The audit half shows the drop only when the new engine is
   given the old one's address, as it usually is; at another address the
   reservation is refused whether or not it was dropped. */
static void test_close_open(void)
{
    eng_audrow row;

    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    begin_ok(ENG_T_ITEMS, 1);
    put_ok("MFR", "Compaq");
    reopen();
    begin_ok(ENG_T_ITEMS, 1);
    eng_edit_cancel(g_e);
    fld_is(ENG_T_ITEMS, 1, "MFR", "IBM");
    TF_EQ(eng_audit_begin(g_e, 2), ENG_OK);
    reopen();
    row.action = ENG_A_UPDATE;
    row.fld = "MFR";
    row.oldval = NULL;
    row.newval = NULL;
    TF_EQ(eng_audit_add(g_e, ENG_T_ITEMS, 1, &row), ENG_EINTERNAL);
    TF_STREQ(marker(), "");
}

/* The marker tests, in order. */
void t_marker(void)
{
    test_marker();
    test_single_failures();
    test_remove_fails();
    test_long_marker();
    test_gone_marker();
    test_stale_count();
    test_restore();
    test_mark_fail();
    test_close_open();
}

/* ---------------------------------------------------------------------
 * Batches.
 * --------------------------------------------------------------------- */

/* Inside a batch a commit that appends a record flushes only SEQ, as an
   ID reservation does, and one that rewrites a record in place flushes
   AUDIT.DBF too, before the record; the batch's end flushes the rest and
   removes the marker, writing no file the batch left clean.  Outside a
   batch every commit flushes, also after a failed change left its
   bracket open. */
void t_batch(void)
{
    u32 id;
    u16 seq;
    u16 aud;

    if (!fresh())
        return;
    new_item(1, "11", "IBM");
    new_item(2, "11", "IBM");
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    TF_EQ(eng_seq_next(g_e, ENG_T_AUDIT, 1, &id), ENG_OK);
    tfs_fault_off();
    seq = tfs_count(CF_OP_COMMIT);
    TF_CHECK(seq > 0);
    TF_EQ(eng_batch_begin(g_e, ENG_T_ITEMS), ENG_OK);
    TF_EQ(eng_batch_on(g_e), 1);
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    TF_EQ(commit_b("B0"), ENG_OK);
    tfs_fault_off();
    TF_EQ(tfs_count(CF_OP_COMMIT), seq);
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    TF_EQ(commit_a(), ENG_OK);
    tfs_fault_off();
    /* AUDIT.DBF grew, so it is committed before and after its header. */
    aud = (u16)(tfs_count(CF_OP_COMMIT) - seq);
    TF_EQ(aud, 2);
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    begin_ok(ENG_T_ITEMS, 2);
    put_ok("MFR", "Apple");
    commit_ok(2);
    tfs_fault_off();
    TF_EQ(tfs_count(CF_OP_COMMIT), seq + aud);
    TF_STREQ(marker(), "ITEMS\r\nLOCS\r\n");
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    TF_EQ(eng_batch_end(g_e, ENG_OK), ENG_OK);
    tfs_fault_off();
    TF_CHECK(tfs_count(CF_OP_COMMIT) > 2 * seq);
    TF_EQ(eng_batch_on(g_e), 0);
    TF_STREQ(marker(), "");
    reopen();
    check_indexes(ENG_T_ITEMS);
    check_indexes(ENG_T_AUDIT);
    fld_is(ENG_T_ITEMS, 2, "MFR", "Apple");

    /* The end of a batch that changed nothing writes or commits no
       table, memo file or index. */
    TF_EQ(eng_batch_begin(g_e, ENG_T_ITEMS), ENG_OK);
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    TF_EQ(eng_batch_end(g_e, ENG_OK), ENG_OK);
    tfs_fault_off();
    TF_EQ(tfs_count(CF_OP_WRITE), 0);
    TF_EQ(tfs_count(CF_OP_COMMIT), 0);
    TF_STREQ(marker(), "");

    /* A failed step, whose code is returned, or a failed flush keeps
       the marker. */
    TF_EQ(eng_batch_begin(g_e, ENG_T_LOANS), ENG_OK);
    TF_EQ(eng_batch_end(g_e, ENG_EIO), ENG_EIO);
    TF_STREQ(marker(), "LOANS\r\n");
    reopen();
    TF_STREQ(marker(), "");
    TF_EQ(eng_batch_begin(g_e, ENG_T_ITEMS), ENG_OK);
    TF_EQ(commit_b("B1"), ENG_OK);
    fail_next_write();
    TF_CHECK(eng_batch_end(g_e, ENG_OK) != ENG_OK);
    tfs_fault_off();
    TF_EQ(eng_batch_on(g_e), 0);
    TF_STREQ(marker(), "ITEMS\r\nLOCS\r\n");
    all_consistent(-1);

    /* A failed commit leaves its bracket open; the next one flushes. */
    begin_ok(ENG_T_ITEMS, 1);
    put_ok("MFR", "Sharp");
    tfs_fault(TFS_ALLOPS, 12, 1, 0);
    TF_CHECK(eng_edit_commit(g_e, 1) != ENG_OK);
    tfs_fault_off();
    tfs_fault(TFS_ALLOPS, 0, 0, 0);
    begin_ok(ENG_T_ITEMS, 2);
    put_ok("MFR", "Atari");
    commit_ok(2);
    tfs_fault_off();
    TF_CHECK(tfs_count(CF_OP_COMMIT) > seq);
    TF_STREQ(marker(), "ITEMS\r\n");
}
