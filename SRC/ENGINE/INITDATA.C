/*
 * INITDATA.C - opening a data directory: the files, then the rows every
 * data directory needs.
 *
 * eng_init_data runs with the actor set to ENG_SYSTEM, so the audit rows
 * of what it writes carry station and user SYSTEM:
 *
 *   1  a SEQ.DBF row (NEXT_VAL 1) for every ID-bearing table that has
 *      none (eng_seq_rows);
 *   2  every SEQ row below the table's largest ID + 1 raised
 *      (eng_seq_heal), which repairs SEQ.DBF after a stop between an
 *      append and the next flush, or after a restore of older tables;
 *   3  the seed rows of SCHTAB.H written into each code table
 *      (CATS.DBF, LTYPES.DBF) that is empty and that no audit row shows
 *      its users deleting from, each a new record committed through the
 *      edit functions, so each is audited as one I row naming the code
 *      field, with the record number as REC_ID: a new table is
 *      seeded, and so is one whose seeding a stop cut off, but one its
 *      users emptied, which PACK leaves without records, stays empty;
 *   4  the ADMIN user written into an empty USERS.DBF (eng_user_seed),
 *      likewise audited as one I row.
 *
 * When eng_open_files kept the marker (eng_marker_kept: it named ITEMS
 * or LOANS, could not be read, or covers audit rows), a change was cut
 * off.  Every change writes its audit rows before its records, so its
 * rows may be on disk while its records are not: once SEQ is whole
 * (after step 2), one X row by SYSTEM flags the rows the marker covers,
 * TBL AUDIT, REC_ID the AUD_ID of the first, FLD AUD_ID and NEWVAL the
 * AUD_ID of the last.  The row is appended, never an edit of the rows
 * it flags, so the audit table stays append-only, and one row flags
 * any number.  A stop before the batch's end leaves the marker with the
 * same count line, so the next open flags again, over a range that
 * takes in this row: a flag may repeat, never be lost.  A change of
 * several items may also be half done, and three repairs follow the
 * steps:
 *
 *   5  the item a half-done ITEM SPLIT took units from given its new
 *      QTY (eng_item_heal), then every live installed item given its
 *      top-level item's location (eng_assy_heal), which undoes a
 *      half-done INSTALL, REMOVE or MOVE, then every live item's
 *      counters set to what its open loans, reservations and in-use
 *      rows give (eng_loan_heal), which matches them to the rows a
 *      half-done change of LOANS left.  Each correction's audit row is
 *      written and flushed before the record, so a stop never leaves a
 *      correction without its row.
 *
 * All five steps then run inside a batch that takes the marker over
 * (eng_mark_adopt); the batch's end flushes and removes the marker only
 * when every step succeeded, so a failure or a stop leaves it, and the
 * next open rebuilds the indexes and repairs again.  Without a kept
 * marker, steps 3 and 4 on a new directory run inside a batch of their
 * own, begun on CATS, so the seeding writes the marker once and flushes
 * each file once; a stop during it leaves the marker, and the next open
 * rebuilds the indexes of the tables it names.
 *
 * Every step finds nothing to do on a directory already set up, so
 * reopening changes no data.  The actor is blank afterwards, until the
 * protocol layer sets it.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGSCH.H"
#include "ENGID.H"
#include "ENGEDIT.H"
#include "ENGUSER.H"
#include "ENGITEM.H"
#include "ENGASSY.H"
#include "ENGLOAN.H"

/* Append the X row flagging the AUDIT records after the first
   eng_marker_from(e) of them, when there are any. */
static int flag_cut(eng *e)
{
    char last[TXT_U32LEN + 1];
    eng_audrow row;
    u32 from;
    u32 n;
    u32 first;
    int rc;

    from = eng_marker_from(e);
    n = eng_reccount(e, ENG_T_AUDIT);
    if (n <= from)
        return ENG_OK;
    rc = eng_rec_read(e, ENG_T_AUDIT, n);
    if (rc != ENG_OK)
        return rc;
    txt_u32(last, (u32)eng_rec_num(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, AUD_ID)));
    rc = eng_rec_read(e, ENG_T_AUDIT, from + 1);
    if (rc != ENG_OK)
        return rc;
    first = (u32)eng_rec_num(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, AUD_ID));
    row.action = ENG_A_CUT;
    row.fld = eng_field_name(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, AUD_ID));
    row.oldval = NULL;
    row.newval = last;
    return eng_audit(e, ENG_T_AUDIT, first, &row);
}

/* due[t] = 1 when code table t is to be seeded, as step 3 says: it is
   empty, and none of its audit rows (the AUDIT1 entries of its name)
   deletes or undeletes a record.  Each scan request stops after
   ENG_MAXREADS entries and the next continues after its cursor, so the
   search is unbounded; it runs only on an empty code table. */
static int seed_due(eng *e, int t, u8 *due)
{
    const char *name;
    char key[ENG_TBLNAMELEN];
    char cur[ENG_CURSORMAX];
    char act[2];
    eng_scan s;
    u32 r;
    int more;
    int rc;

    due[t] = (u8)(eng_reccount(e, t) == 0);
    if (!due[t])
        return ENG_OK;
    name = eng_table_def(t)->name;
    memset(key, ' ', ENG_TBLNAMELEN);
    memcpy(key, name, strlen(name));
    cur[0] = '\0';
    do {
        rc = eng_scan_open(e, &s, ENG_T_AUDIT, ENG_X_AUDIT1,
                           (const u8 *)key, ENG_TBLNAMELEN, cur);
        while (rc == ENG_OK && due[t]) {
            rc = eng_scan_next(e, &s, &r);
            if (rc != ENG_OK || r == 0)
                break;
            eng_rec_str(e, ENG_T_AUDIT, ENG_FIELD(AUDIT, ACTION), act,
                        (u16)sizeof act);
            due[t] = (u8)(act[0] != ENG_A_DELETE);
        }
        more = 0;
        if (rc == ENG_OK && due[t])
            rc = eng_scan_more(e, &s, &more);
        eng_scan_cursor(&s, cur);
    } while (more);
    return rc;
}

/* Append its seed rows to each code table t whose due[t] is 1; a seed
   row that cannot be copied out (its code or name empty) stops it with
   ENG_EINTERNAL, so it is never written blank, but the rows appended
   before it stay.  The code tables' fields
   stand at the same places (ENGCODE.H, checked in CODES.C), named here
   by those of CATS. */
static int seed_codes(eng *e, const u8 *due)
{
    char code[ENG_LTYPELEN + 1];
    char name[ENG_CODENAMELEN + 1];
    u16 sortord;
    u16 i;
    int t;
    int rc;

    for (i = 0; i < eng_seed_rows(); i++) {
        eng_seed_row(i, &t, code, name, &sortord);
        if (code[0] == '\0' || name[0] == '\0')
            return ENG_EINTERNAL;
        if (!due[t])
            continue;
        rc = eng_edit_begin(e, t, 0);
        if (rc != ENG_OK)
            return rc;
        rc = eng_edit_putf(e, ENG_FIELD(CATS, CAT_CODE), code);
        if (rc == ENG_OK)
            rc = eng_edit_putf(e, ENG_FIELD(CATS, CAT_NAME), name);
        if (rc == ENG_OK)
            rc = eng_edit_putnumf(e, ENG_FIELD(CATS, SORTORD), (i32)sortord);
        rc = eng_edit_finish(e, rc, 0);
        if (rc != ENG_OK)
            return rc;
    }
    return ENG_OK;
}

int eng_init_data(eng *e)
{
    u8 due[ENG_NTABLES];        /* seed_due's, of CATS and LTYPES       */
    int seeds;
    int batch;
    int kept;
    int rc;

    eng_set_actor(e, ENG_SYSTEM, ENG_SYSTEM);
    kept = eng_marker_kept(e);
    if (kept)
        eng_mark_adopt(e);
    batch = kept;
    rc = eng_seq_rows(e);
    if (rc == ENG_OK)
        rc = eng_seq_heal(e);
    if (kept && rc == ENG_OK)
        rc = flag_cut(e);
    if (rc == ENG_OK)
        rc = seed_due(e, ENG_T_CATS, due);
    if (rc == ENG_OK)
        rc = seed_due(e, ENG_T_LTYPES, due);
    seeds = rc == ENG_OK && (due[ENG_T_CATS] || due[ENG_T_LTYPES]);
    if (rc == ENG_OK && !kept && (seeds ||
                                  eng_reccount(e, ENG_T_USERS) == 0)) {
        rc = eng_batch_begin(e, ENG_T_CATS);
        batch = rc == ENG_OK;
    }
    /* Seeding only an empty code table means a seeding that fails
       partway is not completed later (one stopped before the batch's
       flush leaves the table empty, and the next open seeds it again);
       the missing rows can be added with CAT ADD or LTYPE ADD. */
    if (rc == ENG_OK && seeds)
        rc = seed_codes(e, due);
    if (rc == ENG_OK && eng_reccount(e, ENG_T_USERS) == 0)
        rc = eng_user_seed(e);
    if (kept && rc == ENG_OK)
        rc = eng_item_heal(e);
    if (kept && rc == ENG_OK)
        rc = eng_assy_heal(e);
    if (kept && rc == ENG_OK)
        rc = eng_loan_heal(e);
    if (batch) {
        rc = eng_batch_end(e, rc);
        if (rc == ENG_OK)
            eng_marker_done(e);
    }
    eng_set_actor(e, "", "");
    return rc;
}

int eng_open(eng **out, const char *dir, const eng_time *now,
             char *why, u16 whysz)
{
    char what[ENG_TXMAX + 1];
    eng *e;
    int rc;

    *out = NULL;
    rc = eng_open_files(&e, dir, now, why, whysz);
    if (rc != ENG_OK)
        return rc;
    rc = eng_init_data(e);
    if (rc != ENG_OK) {
        eng_tx_copy(ENG_TX_SETUP, what);
        eng_why(why, whysz, what, eng_errtx(e));
        eng_close(e);
        return rc;
    }
    *out = e;
    return ENG_OK;
}
