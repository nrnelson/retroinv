/*
 * AUDIT.C - audit rows.
 *
 * Each row of AUDIT.DBF is stamped with the engine clock (ADATE, ATIME
 * as HH:MM:SS) and the actor (STATION, USERNAME), names its table (TBL)
 * and record (REC_ID), and gets an entry in AUDIT1 and in AUDIT2.  The
 * IDs of the rows of one change are reserved together by
 * eng_audit_begin, so a change of many fields costs one SEQ write; rows
 * are then built one at a time in the AUDIT record buffer, so the
 * caller needs only one row's text at once.  Rows reach the disk before
 * the records they describe: at eng_audit_end, or inside a batch at
 * eng_batch_end (or earlier, at the flush of a commit that rewrites a
 * record in place).
 *
 * The reservation is held in this module (one engine writes audit rows
 * at a time, as one edit is open at a time).
 *
 * The marker file ENG_EDITMARK brackets every change that writes a table
 * and its audit rows, then its indexes, so that eng_open_files can
 * repair the indexes of a change cut off part-way and flag its audit
 * rows.  It starts with the count of AUDIT records when it was created
 * and then holds one line per table to repair (ENGINE.H, ENG_EDITMARK).
 * A commit marks its table before its first write and unmarks once
 * everything is written; audit rows written outside a commit are marked
 * as AUDIT by eng_audit_begin and unmarked by eng_audit_end.
 *
 * Brackets are counted.  One that fails is never closed, so the marker
 * stays until the next open repairs and deletes it; a later change while
 * it stays adds its own table as another line (eng_mark_line, which
 * rewrites nothing already in the file), and no later unmark removes
 * it.  A remove that fails once the change is on disk is no error: the
 * bracket stays counted.  The count belongs to the engine that made it,
 * and eng_close drops it with the reservation (eng_audit_drop): another
 * engine starts from 0.
 *
 * A batch (eng_batch_begin .. eng_batch_end) is one bracket around a
 * change made of several commits.  While its flag is set the commits
 * and eng_audit_end leave their files unflushed, and the batch's end
 * flushes everything once; the marker stays on disk until then, so a
 * stop inside the batch is repaired by the next open.  eng_flush writes
 * AUDIT.DBF before the tables, so no record a batch appended is counted
 * before its audit row; a commit that rewrites a record in place inside
 * a batch flushes AUDIT.DBF itself before the write.  The flag, not an
 * open bracket, is what skips the flushes: a failed change leaves its
 * bracket open for the rest of the session, and the commits after it
 * still flush.  eng_mark_adopt makes the marker that eng_open_files
 * kept the batch's own, whatever count an earlier engine left.  The
 * flag itself lives in the engine (eng_set_batch), so it ends with
 * eng_close.
 *
 * Cost per change with no bracket open: a create, one write of the count
 * and the name and a close, then a remove, each followed by a disk
 * reset; audit rows written outside a commit first open and close the
 * marker once when a count is left over.  While a bracket is open, a mark
 * of a table the marker is known to name (au_named: this bracket created
 * the marker with it, found it there or appended it) costs nothing; any
 * other mark opens the marker, reads it, and writes and resets only when
 * its line is new, and only such a mark notices a marker that an open in
 * between removed.  That gap is safe: another engine can remove the
 * marker only by opening the same directory, and that open fails with
 * ENG_EINUSE while this engine holds its tables deny-read-write (under
 * SHARE or the network server).  No program runs two engines at once
 * (INVSERV is the only opener, and INV /LOCAL has one engine), so only a
 * second engine in the host build, where sharing is not enforced, could
 * do it.  The unmark only lowers the count.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGID.H"

static eng *au_eng;             /* engine of the open reservation       */
static u32 au_next;             /* next reserved AUD_ID                 */
static u16 au_left;             /* reserved IDs not yet used            */
static u16 au_depth;            /* marks not yet unmarked               */
static eng *au_meng;            /* engine whose marks au_depth counts   */
static u8 au_fresh;             /* eng_mark just ran for this change    */
static u8 au_mark;              /* eng_audit_begin marked AUDIT (its end
                                   unmarks)                             */
static u8 au_fail;              /* a row of the reservation failed      */
static u16 au_named;            /* bit t: the marker names table t      */

/* au_named has one bit per table. */
extern char au_named_ok[1];
extern char au_named_ok[(ENG_NTABLES <= 16) ? 1 : 2];

/* The marker's path in the data directory, into path (ENG_MAXPATH). */
static void mark_path(eng *e, char *path)
{
    strcpy(path, eng_dir(e));
    strcat(path, ENG_EDITMARK);
}

#define MARK_DEPTHMAX   0xFFF0u /* au_depth stops growing here          */

/* No bracket open, so nothing known to be in the marker. */
static void bracket_reset(void)
{
    au_depth = 0;
    au_named = 0;
}

/* 1 if a bracket is open: counted, and its marker still on disk.  A
   count whose marker is gone (an open in between repaired and removed
   it) is reset. */
static int bracket_open(eng *e)
{
    char path[ENG_MAXPATH];
    int h;
    int rc;

    if (au_meng != e)
        bracket_reset();
    if (au_depth == 0)
        return 0;
    mark_path(e, path);
    rc = cf_open(path, CF_READ | CF_DENYNONE, &h);
    if (rc != 0) {
        if (rc == CF_ENOENT)
            bracket_reset();
        return 0;
    }
    cf_close(h);
    return 1;
}

/* No reservation: no engine may add a row (au_left counts only for
   au_eng), and nothing of one is left to end. */
static void reserve_reset(void)
{
    au_eng = NULL;
    au_mark = 0;
    au_fail = 0;
}

int eng_mark(eng *e, int t)
{
    const eng_tdef *td;
    u16 bit;
    int rc;

    td = eng_table_def(t);
    if (td == NULL || strlen(eng_dir(e)) > ENG_MAXDIR)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_MARKTBL);
    bit = (u16)(1u << t);
    if (au_meng != e) {
        au_meng = e;
        bracket_reset();
    }
    /* With no bracket open au_named is 0, and the marker is made afresh. */
    if ((au_named & bit) == 0) {
        rc = eng_mark_line(e, t, au_depth == 0);
        if (rc == ENG_MARKNEW) {
            /* A new marker: none was open, or the old one was gone (an
               open in between repaired and removed it), so this one
               names t alone. */
            bracket_reset();
        } else if (rc != 0) {
            return eng_cferr(e, rc, ENG_TX_MARKFILE);
        }
    }
    au_named |= bit;
    if (au_depth < MARK_DEPTHMAX)
        au_depth++;
    au_fresh = 1;
    return ENG_OK;
}

void eng_unmark(eng *e)
{
    char path[ENG_MAXPATH];
    int rc;

    au_fresh = 0;
    if (au_meng == e && au_depth > 1) {
        au_depth--;
        return;
    }
    mark_path(e, path);
    rc = cf_remove(path);
    if (rc != 0 && rc != CF_ENOENT)
        return;
    dos_disk_reset();
    bracket_reset();
}

int eng_batch_begin(eng *e, int t)
{
    int rc;

    rc = eng_mark(e, t);
    if (rc == ENG_OK) {
        au_fresh = 0;
        eng_set_batch(e, 1);
    }
    return rc;
}

int eng_batch_end(eng *e, int rc)
{
    int rc2;

    rc2 = eng_flush(e);
    eng_set_batch(e, 0);
    if (rc == ENG_OK)
        rc = rc2;
    if (rc == ENG_OK)
        eng_unmark(e);
    return rc;
}

void eng_mark_adopt(eng *e)
{
    au_meng = e;
    bracket_reset();
    au_depth = 1;
    au_fresh = 0;
    eng_set_batch(e, 1);
}

int eng_audit_begin(eng *e, u16 n)
{
    u32 first;
    int inside;
    int rc;

    /* Inside a commit, whose eng_mark has just run, the bracket is
       known to be open; elsewhere it is probed. */
    inside = au_fresh && au_meng == e && au_depth > 0;
    au_fresh = 0;
    reserve_reset();
    if (n == 0)
        return ENG_OK;
    rc = eng_seq_next(e, ENG_T_AUDIT, n, &first);
    if (rc != ENG_OK)
        return rc;
    if (!inside && !bracket_open(e)) {
        rc = eng_mark(e, ENG_T_AUDIT);
        if (rc != ENG_OK)
            return rc;
        au_mark = 1;
    }
    au_eng = e;
    au_next = first;
    au_left = n;
    return ENG_OK;
}

/* Store s, cut to the width of field f (NULL as blank), in field f of
   the AUDIT buffer.  No AUDIT field is wider than ENG_AUDVAL: the open
   checked the widths against the schema, and SCHEMA.C checks the
   schema's at compile time. */
static int put_cut(eng *e, u16 f, const char *s)
{
    char buf[ENG_AUDVAL + 1];
    dbf *d;

    d = eng_dbf(e, ENG_T_AUDIT);
    txt_copy(buf, s, (u16)(dbf_field_at(d, f)->len + 1));
    return eng_dbferr(e, dbf_put_str(d, f, buf), 0);
}

static int put_n(eng *e, u16 f, u32 v)
{
    return eng_dbferr(e, dbf_put_num(eng_dbf(e, ENG_T_AUDIT), f, (i32)v),
                      0);
}

/* The fields of AUDIT, NEWVAL the last. */
#define AU_NFIELDS      (ENG_FIELD(AUDIT, NEWVAL) + 1)

/* Add one row and its entry in each index of AUDIT.  The open checked
   their key lengths against the schema, and SCHEMA.C checks at compile
   time that each fits ENG_MAXKEYLEN, the size of key. */
static int audit_add(eng *e, int tbl, u32 recid, const eng_audrow *row)
{
    const eng_tdef *td;
    const char *v[AU_NFIELDS];  /* the text of each C and D field       */
    char date[ENG_DATELEN + 1];
    char tm[ENG_TIMELEN + 1];
    char act[2];
    u8 key[ENG_MAXKEYLEN];
    dbf *d;
    u32 id;
    u16 f;
    int rc;
    int x;

    td = eng_table_def(tbl);
    if (au_eng != e || au_left == 0 || td == NULL)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOAUDID);
    id = au_next++;
    au_left--;
    d = eng_dbf(e, ENG_T_AUDIT);
    eng_clock_text(e, date, tm);
    act[0] = row->action;
    act[1] = '\0';
    v[ENG_FIELD(AUDIT, ADATE)] = date;
    v[ENG_FIELD(AUDIT, ATIME)] = tm;
    eng_actor(e, &v[ENG_FIELD(AUDIT, STATION)],
              &v[ENG_FIELD(AUDIT, USERNAME)]);
    v[ENG_FIELD(AUDIT, TBL)] = td->name;
    v[ENG_FIELD(AUDIT, ACTION)] = act;
    v[ENG_FIELD(AUDIT, FLD)] = row->fld;
    v[ENG_FIELD(AUDIT, OLDVAL)] = row->oldval;
    v[ENG_FIELD(AUDIT, NEWVAL)] = row->newval;

    dbf_blank(d);
    rc = put_n(e, ENG_FIELD(AUDIT, AUD_ID), id);
    if (rc == ENG_OK)
        rc = put_n(e, ENG_FIELD(AUDIT, REC_ID), recid);
    for (f = ENG_FIELD(AUDIT, AUD_ID) + 1; rc == ENG_OK && f < AU_NFIELDS;
         f++)
        if (f != ENG_FIELD(AUDIT, REC_ID))
            rc = put_cut(e, f, v[f]);
    if (rc != ENG_OK)
        return rc;
    rc = dbf_append(d);
    if (rc != DBF_OK)
        return eng_dbferr(e, rc, 0);
    for (x = ENG_X_AUDIT1; rc == DBF_OK && x <= ENG_X_AUDIT2; x++) {
        rc = kx_eval(eng_kx(e, x), d, key);
        if (rc == DBF_OK)
            rc = ndx_insert(eng_ndx(e, x), key, dbf_recno(d));
    }
    return eng_dbferr(e, rc, 1);
}

int eng_audit_add(eng *e, int tbl, u32 recid, const eng_audrow *row)
{
    int rc;

    rc = audit_add(e, tbl, recid, row);
    if (rc != ENG_OK)
        au_fail = 1;
    return rc;
}

/* Flush AUDIT.DBF, then its indexes; the first failure is returned
   once all have been tried. */
static int audit_flush(eng *e)
{
    int first;
    int rc;
    int x;

    first = eng_dbferr(e, dbf_flush(eng_dbf(e, ENG_T_AUDIT)), 0);
    for (x = ENG_X_AUDIT1; x <= ENG_X_AUDIT2; x++) {
        rc = eng_dbferr(e, ndx_flush(eng_ndx(e, x)), 1);
        if (first == ENG_OK)
            first = rc;
    }
    return first;
}

int eng_audit_end(eng *e)
{
    int rc;

    rc = eng_batch_on(e) ? ENG_OK : audit_flush(e);
    if (rc == ENG_OK && au_mark && !au_fail)
        eng_unmark(e);
    reserve_reset();
    return rc;
}

void eng_audit_drop(eng *e)
{
    if (au_eng == e)
        reserve_reset();
    if (au_meng == e) {
        au_meng = NULL;
        bracket_reset();
        au_fresh = 0;
    }
}

int eng_audit(eng *e, int tbl, u32 recid, const eng_audrow *row)
{
    int rc;
    int rc2;

    rc = eng_audit_begin(e, 1);
    if (rc != ENG_OK)
        return rc;
    rc = eng_audit_add(e, tbl, recid, row);
    rc2 = eng_audit_end(e);
    if (rc == ENG_OK)
        rc = rc2;
    return rc;
}
