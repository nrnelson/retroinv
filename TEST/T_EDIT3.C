/*
 * T_EDIT3.C - the third part of the T_EDIT test (see T_EDIT.C): what
 * the edit marker ENG_EDITMARK hands the file layer.
 *
 * DOS writes a whole disk sector even for a short write into a fresh
 * cluster, the bytes past the write taken from whatever its buffer last
 * held (a mailbox request with its password, say), and they stay on the
 * disk once the marker is deleted.  So a marker line that starts a
 * sector is written, then zeros up to the sector's end, then the file is
 * cut back to the line; a line added to a marker already there is one
 * write.  A host file shows no such slack, so these checks pin the calls
 * themselves: the order of the ops the fault hook sees, the bytes on
 * the disk when the cut fails, and, on the host, where the size of a
 * file open elsewhere is up to date, the size before each op.  It also
 * checks the ENG_E code of each DBF_E code.
 *
 * Then memos saved in parts (eng_edit_begin_note with ENG_NOTE_NEW,
 * ENG_NOTE_PART and ENG_NOTE_END): a stage of its own tag, actor, file
 * and record; the memo, the audit and the open edit untouched until the
 * END, which writes the whole memo with one audit row and ends the
 * stage; a stage of another record, tag, station or user, or none,
 * refused; a part refused for its text leaving the stage, a new one
 * replacing it, the length limit; two stations, and two slots of one
 * station and user, interleaved in both orders each writing its whole
 * memo, two stations that share a stage file refused rather than mixed;
 * the disk full while a stage is written removing it, and from each
 * write of the END's commit on leaving the memo, after a reopen, as it
 * was or whole.
 *
 * It is a file of its own only to stay within the compiler memory of
 * Watcom C 6.5; T_EDIT.H holds what the three share.
 */
#define COMPAT_DISK
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGEDIT.H"
#include "ENGNOTE.H"
#define TF_PART
#define TF_FILE "T_EDIT3"
#include "TESTFW.H"
#include "T_FILES.H"
#include "T_EDIT.H"

#define MP_MAXOPS       512     /* ops whose codes watch logs           */

static u8 g_ops[MP_MAXOPS];     /* the ops of the last watched call     */
static u16 g_nops;              /* their number, those not logged too   */
#ifdef COMPAT_HOST
static u32 g_size[MP_MAXOPS];   /* the marker's size before each        */
#endif
static int g_failop;            /* the op whose next call fails (0: none)
                                   with CF_EFULL                        */
static char g_line[ENG_MARKMAX + 1];    /* the lines a marker starts with */
static u16 g_k;                 /* its length                           */
static u8 g_sect[DOS_SECTOR];   /* the marker as read_marker read it    */

/* The fault hook: log every op, closes included, on the host with the
   marker's size, and fail the next call of g_failop. */
static int watch(int op)
{
    if (g_nops < MP_MAXOPS) {
        g_ops[g_nops] = (u8)op;
#ifdef COMPAT_HOST
        cf_fault = NULL;
        g_size[g_nops] = tfs_size(tfs_path(ENG_EDITMARK, ""));
        cf_fault = watch;
#endif
    }
    g_nops++;
    if (op != g_failop)
        return 0;
    g_failop = 0;
    return CF_EFULL;
}

/* Watch from now on, the next call of failop failing (0: none). */
static void arm(int failop)
{
    g_nops = 0;
    g_failop = failop;
    cf_fault = watch;
}

/* The marker's first line (the count line of the AUDIT records now)
   and then the line naming table name, into g_line, its length in
   g_k. */
static void first_line(const char *name)
{
    sprintf(g_line, "%lu\r\n%s\r\n",
            (unsigned long)eng_reccount(g_e, ENG_T_AUDIT), name);
    g_k = (u16)strlen(g_line);
}

/* The marker's size, its first DOS_SECTOR bytes in g_sect; TFS_NOFILE
   when it cannot be read. */
static u32 read_marker(void)
{
    u32 size;
    u16 got;

    size = tfs_size(tfs_path(ENG_EDITMARK, ""));
    if (size == TFS_NOFILE ||
        tfs_read_all(tfs_path(ENG_EDITMARK, ""), g_sect, DOS_SECTOR,
                     &got) != 0)
        return TFS_NOFILE;
    return size;
}

/* Check that the marker is the g_k bytes of g_line, then zeros up to
   the sector's end. */
static void padded(void)
{
    u16 i;

    if (!TF_EQ(read_marker(), DOS_SECTOR))
        return;
    for (i = g_k; i < DOS_SECTOR && g_sect[i] == 0; i++)
        ;
    TF_CHECK(memcmp(g_sect, g_line, g_k) == 0);
    TF_EQ(i, DOS_SECTOR);
}

#ifdef COMPAT_HOST
/* The marker's sizes before the ops from the CREATE at c up to the
   TRUNC at t: each WRITE adds to the end, the line g_k bytes, then the
   zeros (which padded checks) up to DOS_SECTOR exactly, and the cut
   leaves the line. */
static void sizes(u16 c, u16 t)
{
    u16 i;

    TF_EQ(g_size[c + 1], 0);
    TF_EQ(g_size[c + 2], g_k);
    for (i = (u16)(c + 3); i <= t; i++)
        TF_CHECK(g_size[i] > g_size[i - 1]);
    TF_EQ(g_size[t], DOS_SECTOR);
    TF_EQ(g_size[t + 1], g_k);
}
#endif

/* The marker's ops, found by its TRUNC, the first logged (no other
   call these tests make cuts a file): a CREATE, the line's WRITE, at
   least one WRITE of zeros, the TRUNC and a CLOSE.  The CREATE is the
   first op logged when first is 1, and nothing follows the CLOSE when
   whole is 1 (the call was only the marker's). */
static void marker_ops(int first, int whole)
{
    u16 n;
    u16 c;
    u16 t;

    n = g_nops < MP_MAXOPS ? g_nops : MP_MAXOPS;
    for (t = 0; t < n && g_ops[t] != CF_OP_TRUNC; t++)
        ;
    if (!TF_CHECK(t + 1 < n))
        return;
    for (c = t; c > 0 && g_ops[c - 1] == CF_OP_WRITE; c--)
        ;
    if (!TF_CHECK(c > 0) || !TF_EQ(g_ops[--c], CF_OP_CREATE) ||
        !TF_CHECK(t - c >= 3) || !TF_EQ(g_ops[t + 1], CF_OP_CLOSE) ||
        (first && !TF_EQ(c, 0)) || (whole && !TF_EQ(g_nops, t + 2)))
        return;
#ifdef COMPAT_HOST
    sizes(c, t);
#endif
}

/* eng_mark_line on its own: a new marker padded, then cut to its line;
   a cut that fails returns its code and leaves the padded sector; a
   line added to the marker is one write. */
static void test_line(void)
{
    if (!fresh())
        return;
    first_line("ITEMS");
    arm(0);
    TF_EQ(eng_mark_line(g_e, ENG_T_ITEMS, 1), ENG_MARKNEW);
    cf_fault = NULL;
    marker_ops(1, 1);
    TF_EQ(read_marker(), g_k);
    TF_CHECK(memcmp(g_sect, g_line, g_k) == 0);

    strcat(g_line, "LOCS\r\n");
    arm(0);
    TF_EQ(eng_mark_line(g_e, ENG_T_LOCS, 0), 0);
    cf_fault = NULL;
    TF_EQ(g_nops, 2);
    TF_EQ(g_ops[0], CF_OP_WRITE);
    TF_EQ(g_ops[1], CF_OP_CLOSE);
    TF_EQ(read_marker(), strlen(g_line));
    TF_CHECK(memcmp(g_sect, g_line, strlen(g_line)) == 0);

    /* A fresh marker replaces this one, as padded; the cut fails. */
    first_line("CATS");
    arm(CF_OP_TRUNC);
    TF_EQ(eng_mark_line(g_e, ENG_T_CATS, 1), CF_EFULL);
    cf_fault = NULL;
    TF_EQ(g_failop, 0);
    padded();
    TF_EQ(cf_remove(tfs_path(ENG_EDITMARK, "")), 0);
}

/* A commit on a fresh data directory starts with the padded marker; a
   cut that fails refuses the commit as a marker write that fails does,
   the record unchanged and the padded sector left, which the next open
   repairs and removes. */
static void test_commit(void)
{
    u32 rec;
    int rc;

    if (!fresh())
        return;
    first_line("ITEMS");
    arm(0);
    rec = new_item(1, "11", "IBM");
    cf_fault = NULL;
    marker_ops(1, 0);
    TF_CHECK(!tfs_exists(tfs_path(ENG_EDITMARK, "")));

    begin_ok(ENG_T_ITEMS, rec);
    put_ok("MFR", "Compaq");
    arm(CF_OP_WRITE);
    rc = eng_edit_commit(g_e, 1);
    cf_fault = NULL;
    TF_CHECK(rc != ENG_OK);
    TF_STREQ(eng_errmsg(g_e), ENG_EDITMARK);
    reopen();

    first_line("ITEMS");
    begin_ok(ENG_T_ITEMS, rec);
    put_ok("MFR", "Compaq");
    arm(CF_OP_TRUNC);
    TF_EQ(eng_edit_commit(g_e, 1), rc);
    cf_fault = NULL;
    TF_EQ(g_failop, 0);
    TF_STREQ(eng_errmsg(g_e), ENG_EDITMARK);
    padded();
    fld_is(ENG_T_ITEMS, rec, "MFR", "IBM");
    reopen();
    TF_CHECK(!tfs_exists(tfs_path(ENG_EDITMARK, "")));
    fld_is(ENG_T_ITEMS, rec, "MFR", "IBM");
    check_indexes(ENG_T_ITEMS);
}

/* An open that keeps a marker naming ITEMS for the repairs (as the
   reopen after PACK, REINDEX or RESTORE meets one) rewrites it first,
   padded as a new marker is; a cut that fails fails the open and
   leaves the padded sector. */
static void test_reopen(void)
{
    char why[TFS_WHYSZ];
    u16 k;
    int i;

    if (!fresh())
        return;
    first_line("ITEMS");
    k = g_k;
    for (i = 0; i < 2; i++) {
        if (g_e != NULL)
            TF_EQ(eng_close(g_e), ENG_OK);
        g_e = NULL;
        TF_EQ(tfs_write_all(tfs_path(ENG_EDITMARK, ""), g_line, k), 0);
        arm(i == 0 ? 0 : CF_OP_TRUNC);
        TF_EQ(eng_open_files(&g_e, tfs_dir(), NULL, why, (u16)sizeof why) ==
              ENG_OK, i == 0);
        cf_fault = NULL;
        if (i == 0) {
            /* What the open wrote, the line of the checks after. */
            TF_CHECK(eng_marker_kept(g_e));
            g_k = (u16)read_marker();
            if (!TF_CHECK(g_k <= ENG_MARKMAX) ||
                !TF_CHECK(memcmp(g_sect, g_line, k) == 0))
                return;
            memcpy(g_line, g_sect, g_k);
            marker_ops(0, 0);
        } else {
            TF_EQ(g_failop, 0);
            padded();
        }
    }
    /* The padded sector reads as naming every table: repaired, gone. */
    if (g_e != NULL)
        TF_EQ(eng_close(g_e), ENG_OK);
    if (!TF_EQ(eng_open_files(&g_e, tfs_dir(), NULL, why, (u16)sizeof why),
               ENG_OK))
        return;
    eng_mark_adopt(g_e);
    TF_EQ(eng_batch_end(g_e, ENG_OK), ENG_OK);
    TF_CHECK(!tfs_exists(tfs_path(ENG_EDITMARK, "")));
    check_indexes(ENG_T_ITEMS);
}

void t_markpad(void)
{
    test_line();
    test_commit();
    test_reopen();
}

/* ---------------------------------------------------------------------
 * DBF codes.
 * --------------------------------------------------------------------- */

/* eng_dbferr gives each DBF_E code its ENG_E code (that of DBF_EFORMAT
   by whether an index gave it) and records a message; DBF_OK gives
   ENG_OK and records none. */
void t_dbferr(void)
{
    static const int k_map[][3] = {
        { DBF_OK, 0, ENG_OK },
        { DBF_EIO, 0, ENG_EIO },
        { DBF_ENOENT, 0, ENG_EIO },
        { DBF_EINUSE, 0, ENG_EINUSE },
        { DBF_EFORMAT, 0, ENG_EIO },
        { DBF_EFORMAT, 1, ENG_EINDEX },
        { DBF_ENOMEM, 0, ENG_EINTERNAL },
        { DBF_ERANGE, 0, ENG_EIO },
        { DBF_EARG, 0, ENG_EIO },
        { DBF_EFULL, 1, ENG_EFULL },
        { DBF_EHANDLES, 0, ENG_EHANDLES },
        { DBF_ERDONLY, 0, ENG_EIO }
    };
    u16 i;

    if (!fresh())
        return;
    for (i = 0; i < (u16)(sizeof k_map / sizeof k_map[0]); i++) {
        eng_clear_error(g_e);
        if (!TF_EQ(eng_dbferr(g_e, k_map[i][0], k_map[i][1]), k_map[i][2]) ||
            !TF_EQ(eng_errmsg(g_e)[0] != '\0', k_map[i][2] != ENG_OK))
            printf("  DBF code %d\n", k_map[i][0]);
    }
}

/* ---------------------------------------------------------------------
 * Memos saved in parts.
 * --------------------------------------------------------------------- */
#define OLD             "old"
#define LONGW           198     /* a line of g_long                     */

static char g_long[LONGW + 1];

/* A step on item recno: the record read, then eng_edit_begin_note with
   how and, when how writes, the commit the modules make. */
static int note(u32 recno, const char *text, u8 how)
{
    u16 f;
    int rc;

    f = (u16)eng_field_num(g_e, ENG_T_ITEMS, "NOTES");
    rc = eng_rec_read(g_e, ENG_T_ITEMS, recno);
    if (rc == ENG_OK)
        rc = eng_edit_begin_note(g_e, ENG_T_ITEMS, f, recno, text, how);
    if (rc == ENG_OK && ENG_NOTE_WRITES(how))
        rc = eng_edit_commit(g_e, recno);
    return rc;
}

/* Item recno's memo, or "?" when it cannot be read. */
static const char *memo(u32 recno)
{
    u16 len;

    if (eng_rec_read(g_e, ENG_T_ITEMS, recno) != ENG_OK ||
        eng_memo_read(g_e, ENG_T_ITEMS,
                      (u16)eng_field_num(g_e, ENG_T_ITEMS, "NOTES"),
                      &len) != ENG_OK)
        return "?";
    return eng_memobuf(g_e);
}

/* 1 when item recno's memo is want. */
static int memo_is(u32 recno, const char *want)
{
    return TF_STREQ(memo(recno), want);
}

/* The stage file of ITEMS of station sta: ENG_NOTESTAGE and the hex
   digit of sta's hash, as EDIT.C names it. */
static const char *stage(const char *sta)
{
    char ext[sizeof ENG_NOTESTAGE + 1];
    u16 h;

    for (h = 0; *sta != '\0'; sta++)
        h = (u16)(h * 31 + (u8)*sta);
    strcpy(ext, ENG_NOTESTAGE);
    ext[sizeof ext - 2] = txt_hexdig[h & 15];
    ext[sizeof ext - 1] = '\0';
    return tfs_path("ITEMS", ext);
}

/* Make station sta's user usr the actor, as a session's request does. */
static void as(const char *sta, const char *usr)
{
    eng_set_actor(g_e, sta, usr);
}

/* Fresh data with items 1 and 2, item 1's memo OLD, no stage, the
   actor STA1's TESTER. */
static int start(void)
{
    static const char *const stas[] = { "STA1", "STA2", "MB9", "MB10" };
    u16 i;

    for (i = 0; i < sizeof stas / sizeof stas[0]; i++)
        cf_remove(stage(stas[i]));
    if (!fresh())
        return 0;
    new_item(1, "11", "IBM");
    new_item(2, "11", "IBM");
    return TF_EQ(note(1, OLD, ENG_NOTE_ADD), ENG_OK);
}

/* Stage "one\ntwo" and "x" for item 1: all but the END. */
static void stage_two(void)
{
    TF_EQ(note(1, "one\ntwo", ENG_NOTE_NEW), ENG_OK);
    TF_EQ(note(1, "x", ENG_NOTE_PART), ENG_OK);
}

static void test_stage(void)
{
    u32 aud;
    u16 k;

    if (!start())
        return;
    aud = eng_reccount(g_e, ENG_T_AUDIT);
    TF_EQ(note(1, "one\ntwo", ENG_NOTE_NEW), ENG_OK);
    TF_CHECK(tfs_exists(stage("STA1")));
    TF_EQ(tfs_size(stage("STA1")),
          5 + ENG_STALEN + ENG_USERLEN + sizeof "one\r\ntwo" - 1);
    TF_CHECK(!tfs_exists(stage("STA2")));
    TF_EQ(eng_edit_begin(g_e, ENG_T_ITEMS, 1), ENG_OK);  /* none open */
    eng_edit_cancel(g_e);
    TF_EQ(note(2, "x", ENG_NOTE_PART), ENG_ECURSOR);
    TF_STREQ(eng_errfield(g_e), "NOTES");
    TF_STREQ(eng_errmsg(g_e), "no note being saved");
    as("STA1", "OTHER");
    TF_EQ(note(1, "x", ENG_NOTE_PART), ENG_ECURSOR);
    as("STA2", "TESTER");
    TF_EQ(note(1, "x", ENG_NOTE_PART), ENG_ECURSOR);
    as("STA1", "TESTER");
    TF_EQ(note(1, "bad\r", ENG_NOTE_PART), ENG_EARG);
    TF_EQ(note(1, "three", ENG_NOTE_PART), ENG_OK);
    TF_CHECK(memo_is(1, OLD));
    TF_EQ(eng_reccount(g_e, ENG_T_AUDIT), aud);
    TF_EQ(note(1, "four", ENG_NOTE_PART | ENG_NOTE_END), ENG_OK);
    TF_CHECK(memo_is(1, "one\r\ntwo\r\nthree\r\nfour"));
    TF_EQ(eng_reccount(g_e, ENG_T_AUDIT), aud + 1);
    TF_CHECK(!tfs_exists(stage("STA1")));
    TF_EQ(note(1, "x", ENG_NOTE_PART | ENG_NOTE_END), ENG_ECURSOR);

    /* A stage left behind is replaced by the next NEW of its actor. */
    TF_EQ(note(1, "left", ENG_NOTE_NEW), ENG_OK);
    TF_EQ(note(1, "a", ENG_NOTE_NEW), ENG_OK);
    TF_EQ(note(1, "b", ENG_NOTE_PART | ENG_NOTE_END), ENG_OK);
    TF_CHECK(memo_is(1, "a\r\nb"));

    /* The limit holds for a stage as for ADD: ten lines of LONGW bytes
       and their line breaks leave room for one empty line more; a
       refused END ends the stage too. */
    memset(g_long, 'q', LONGW);
    g_long[LONGW] = '\0';
    TF_EQ(note(1, g_long, ENG_NOTE_NEW), ENG_OK);
    for (k = 1; k < 10; k++)
        TF_EQ(note(1, g_long, ENG_NOTE_PART), ENG_OK);
    TF_EQ(note(1, "", ENG_NOTE_PART), ENG_OK);
    TF_EQ(tfs_size(stage("STA1")),
          5 + ENG_STALEN + ENG_USERLEN + ENG_MAXMEMO);
    TF_EQ(note(1, "", ENG_NOTE_PART), ENG_ECONSTR);
    TF_STREQ(eng_errfield(g_e), "NOTES");
    TF_EQ(note(1, "", ENG_NOTE_PART | ENG_NOTE_END), ENG_ECONSTR);
    TF_CHECK(!tfs_exists(stage("STA1")));
    TF_CHECK(memo_is(1, "a\r\nb"));
}

/* Two sessions saving item 1 at once, each request in turn: whatever
   the order of their ENDs, the memo is the whole of one of them.  Two
   stations whose stages share a file (MB9 and MB10, as the stations of
   a mailbox might) never mix: the later NEW takes the file, and the
   other's END is refused, leaving the memo and that stage as they
   were. */
static void test_interleave(void)
{
    if (!start())
        return;
    TF_STREQ(stage("MB9"), stage("MB10"));
    as("STA1", "TESTER");
    TF_EQ(note(1, "A1", ENG_NOTE_NEW), ENG_OK);
    as("STA2", "TESTER");
    TF_EQ(note(1, "B1", ENG_NOTE_NEW), ENG_OK);
    as("STA1", "TESTER");
    TF_EQ(note(1, "A2", ENG_NOTE_PART | ENG_NOTE_END), ENG_OK);
    as("STA2", "TESTER");
    TF_EQ(note(1, "B2", ENG_NOTE_PART | ENG_NOTE_END), ENG_OK);
    TF_CHECK(memo_is(1, "B1\r\nB2"));
    as("STA1", "TESTER");
    TF_EQ(note(1, "A1", ENG_NOTE_NEW), ENG_OK);
    as("STA2", "TESTER");
    TF_EQ(note(1, "B1", ENG_NOTE_NEW), ENG_OK);
    TF_EQ(note(1, "B2", ENG_NOTE_PART | ENG_NOTE_END), ENG_OK);
    as("STA1", "TESTER");
    TF_EQ(note(1, "A2", ENG_NOTE_PART | ENG_NOTE_END), ENG_OK);
    TF_CHECK(memo_is(1, "A1\r\nA2"));

    /* Two sessions of one station and user on slots 1 and 2 (the TCP
       bridge's sessions, or two on one NetBIOS name) keep apart, in
       both orders, and a stage is its slot's alone. */
    TF_EQ(note(1, "S1", ENG_NOTE_NEW | 1), ENG_OK);
    TF_EQ(note(1, "T1", ENG_NOTE_NEW | 2), ENG_OK);
    TF_EQ(note(1, "S2", ENG_NOTE_PART | ENG_NOTE_END | 1), ENG_OK);
    TF_EQ(note(1, "T2", ENG_NOTE_PART | ENG_NOTE_END | 2), ENG_OK);
    TF_CHECK(memo_is(1, "T1\r\nT2"));
    TF_EQ(note(1, "S1", ENG_NOTE_NEW | 1), ENG_OK);
    TF_EQ(note(1, "T1", ENG_NOTE_NEW | 2), ENG_OK);
    TF_EQ(note(1, "T2", ENG_NOTE_PART | ENG_NOTE_END | 2), ENG_OK);
    TF_EQ(note(1, "S2", ENG_NOTE_PART | ENG_NOTE_END | 1), ENG_OK);
    TF_CHECK(memo_is(1, "S1\r\nS2"));
    TF_EQ(note(1, "S1", ENG_NOTE_NEW | 1), ENG_OK);
    TF_EQ(note(1, "x", ENG_NOTE_PART | 2), ENG_ECURSOR);
    TF_EQ(note(1, "x", ENG_NOTE_PART), ENG_ECURSOR);
    TF_EQ(note(1, "S2", ENG_NOTE_PART | ENG_NOTE_END | 1), ENG_OK);

    as("MB9", "TESTER");
    TF_EQ(note(1, "X1", ENG_NOTE_NEW), ENG_OK);
    as("MB10", "TESTER");
    TF_EQ(note(1, "Y1", ENG_NOTE_NEW), ENG_OK);
    as("MB9", "TESTER");
    TF_EQ(note(1, "X2", ENG_NOTE_PART | ENG_NOTE_END), ENG_ECURSOR);
    TF_CHECK(memo_is(1, "S1\r\nS2"));
    as("MB10", "TESTER");
    TF_EQ(note(1, "Y2", ENG_NOTE_PART | ENG_NOTE_END), ENG_OK);
    TF_CHECK(memo_is(1, "Y1\r\nY2"));
}

/* The disk full while a stage is written (each of its two writes) and
   at each write of the END's commit. */
static void test_full(void)
{
    u32 writes;
    u32 k;
    int rc;

    for (k = 1; k <= 2; k++) {
        if (!start())
            return;
        TF_EQ(note(1, "one\ntwo", ENG_NOTE_NEW), ENG_OK);
        tfs_fault(TFS_OP(CF_OP_WRITE), k, 1, 0);
        TF_EQ(note(1, "three", ENG_NOTE_PART), ENG_EFULL);
        tfs_fault_off();
        TF_CHECK(!tfs_exists(stage("STA1")));
        TF_EQ(note(1, "x", ENG_NOTE_PART | ENG_NOTE_END), ENG_ECURSOR);
        TF_CHECK(memo_is(1, OLD));
    }

    if (!start())
        return;
    stage_two();
    tfs_fault(TFS_OP(CF_OP_WRITE), 0, 0, 0);
    TF_EQ(note(1, "three", ENG_NOTE_PART | ENG_NOTE_END), ENG_OK);
    tfs_fault_off();
    writes = tfs_count(CF_OP_WRITE);
    TF_CHECK(writes > 2);
    for (k = 1; k <= writes; k++) {
        if (!start())
            return;
        stage_two();
        tfs_fault(TFS_OP(CF_OP_WRITE), k, 0, 0);
        rc = note(1, "three", ENG_NOTE_PART | ENG_NOTE_END);
        tfs_fault_off();
        TF_CHECK(!tfs_exists(stage("STA1")));
        reopen();
        if (!TF_EQ(rc, ENG_EFULL) ||
            !TF_CHECK(strcmp(memo(1), OLD) == 0 ||
                      strcmp(memo(1), "one\r\ntwo\r\nx\r\nthree") == 0))
            printf("  write %lu\n", (unsigned long)k);
    }
}

void t_stage(void)
{
    test_stage();
    test_interleave();
    test_full();
}
