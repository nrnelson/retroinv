/*
 * IDGEN.C - ID numbers from SEQ.DBF.
 *
 * SEQ.DBF holds one row per ID-bearing table (eng_tdef.seq names it in
 * SEQ_NAME) and nothing else; NEXT_VAL is the next ID to issue.  SEQ has
 * no index, so a row is first found by reading the table's records in
 * order.  Its record number is then kept for the rest of the open
 * (seq_rec, cleared by eng_seq_rows, which every open runs first) and
 * read directly by later issues.  SEQ rows do not move while the engine
 * is open; the check that the record read is live and holds that row
 * only guards against a damaged file, and a failed check scans again.
 *
 * The ID of an ID-bearing table is its first field.  An issue that would
 * pass the width of that field, or of NEXT_VAL, is refused with
 * ENG_ECONSTR naming the ID field, leaving SEQ unchanged.
 *
 * As IDs are appended in increasing order, a record is found by its ID
 * with a binary search over the record numbers (eng_id_find), for the
 * tables that have no index on their ID.
 *
 * An ID is ENG_IDLEN digits in its keys, STR(id,8) right-aligned with
 * spaces (eng_id_key, eng_key_id).
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGID.H"

#define SEQ_NAMEMAX     DBF_NAMELEN     /* width of SEQ_NAME            */

/* The SEQ record of each table's row, or 0 (also for a record number
   above 0xFFFF, which a SEQ of a few rows never has). */
static u16 seq_rec[ENG_NTABLES];

/* STR(id,8) holds ENG_MAXID, the largest number of ENG_IDLEN digits. */
extern char eng_maxid_ok[1];
extern char eng_maxid_ok[(ENG_IDLEN == 8 && ENG_MAXID == 99999999UL) ?
                         1 : 2];

/* Largest value an N field of width len without decimals holds (len is
   at most 9 here, so the value fits a u32). */
static u32 num_max(u8 len)
{
    u32 m;
    int i;

    m = 1;
    for (i = 0; i < len && i < 9; i++)
        m *= 10;
    return m - 1;
}

/* Read record r of SEQ into the SEQ buffer; *hit is 1 when it is live
   and its SEQ_NAME is name. */
static int seq_holds(eng *e, u32 r, const char *name, int *hit)
{
    char got[SEQ_NAMEMAX + 1];
    dbf *d;
    int rc;

    *hit = 0;
    rc = eng_rec_read(e, ENG_T_SEQ, r);
    if (rc != ENG_OK)
        return rc;
    d = eng_dbf(e, ENG_T_SEQ);
    if (!dbf_is_deleted(d) &&
        dbf_get_str(d, ENG_FIELD(SEQ, SEQ_NAME), got, sizeof got) ==
        DBF_OK && strcmp(got, name) == 0)
        *hit = 1;
    return ENG_OK;
}

/* Load the live SEQ row of table t into the SEQ buffer; *recno receives
   its number, or 0 when there is no such row.  ENG_EINTERNAL for a
   table without ID. */
static int seq_lookup(eng *e, int t, u32 *recno)
{
    const eng_tdef *td;
    u32 n;
    u32 r;
    int hit;
    int rc;

    *recno = 0;
    td = eng_table_def(t);
    if (td == NULL || td->seq == NULL)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOID);
    n = dbf_reccount(eng_dbf(e, ENG_T_SEQ));
    r = seq_rec[t];
    if (r != 0 && r <= n) {
        rc = seq_holds(e, r, td->seq, &hit);
        if (rc != ENG_OK)
            return rc;
        if (hit) {
            *recno = r;
            return ENG_OK;
        }
    }
    for (r = 1; r <= n; r++) {
        rc = seq_holds(e, r, td->seq, &hit);
        if (rc != ENG_OK)
            return rc;
        if (hit) {
            seq_rec[t] = r <= 0xFFFFu ? (u16)r : 0;
            *recno = r;
            return ENG_OK;
        }
    }
    return ENG_OK;
}

/* seq_lookup, with a missing row as ENG_EIO (a damaged SEQ.DBF). */
static int seq_find(eng *e, int t, u32 *recno)
{
    int rc;

    rc = seq_lookup(e, t, recno);
    if (rc == ENG_OK && *recno == 0)
        return eng_fail(e, ENG_EIO, "SEQ_NAME", ENG_TX_NOSEQ);
    return rc;
}

/* NEXT_VAL of the row in the SEQ buffer; a blank or non-positive value
   reads as 1. */
static int seq_value(eng *e, u32 *next)
{
    i32 v;
    int blank;
    int rc;

    *next = 1;
    rc = dbf_get_num(eng_dbf(e, ENG_T_SEQ), ENG_FIELD(SEQ, NEXT_VAL), &v,
                     &blank);
    if (rc != DBF_OK)
        return eng_dbferr(e, rc, 0);
    if (!blank && v > 0)
        *next = (u32)v;
    return ENG_OK;
}

/* Store next as NEXT_VAL of the row in the SEQ buffer, write and flush. */
static int seq_store(eng *e, u32 next)
{
    dbf *d;
    int rc;

    d = eng_dbf(e, ENG_T_SEQ);
    rc = dbf_put_num(d, ENG_FIELD(SEQ, NEXT_VAL), (i32)next);
    if (rc == DBF_OK)
        rc = dbf_write(d);
    if (rc == DBF_OK)
        rc = dbf_flush(d);
    return eng_dbferr(e, rc, 0);
}

int eng_seq_next(eng *e, int t, u16 count, u32 *first)
{
    u32 recno;
    u32 next;
    u32 idmax;
    u32 seqmax;
    const dbf_field *idf;
    int rc;

    *first = 0;
    rc = seq_find(e, t, &recno);
    if (rc == ENG_OK)
        rc = seq_value(e, &next);
    if (rc != ENG_OK)
        return rc;
    *first = next;
    if (count == 0)
        return ENG_OK;
    idf = eng_field(e, t, 0);
    idmax = num_max(idf->len);
    seqmax = num_max(eng_field(e, ENG_T_SEQ, ENG_FIELD(SEQ, NEXT_VAL))->len);
    if (next > idmax || (u32)count - 1u > idmax - next ||
        next >= seqmax || (u32)count > seqmax - next) {
        *first = 0;
        return eng_fail(e, ENG_ECONSTR, idf->name, ENG_TX_IDSOUT);
    }
    rc = seq_store(e, next + count);
    if (rc != ENG_OK)
        *first = 0;
    return rc;
}

int eng_seq_peek(eng *e, int t, u32 *next)
{
    u32 recno;
    int rc;

    *next = 0;
    rc = seq_find(e, t, &recno);
    if (rc == ENG_OK)
        rc = seq_value(e, next);
    return rc;
}

int eng_seq_rows(eng *e)
{
    const eng_tdef *td;
    dbf *d;
    u32 recno;
    int added;
    int t;
    int rc;

    memset(seq_rec, 0, sizeof seq_rec);
    d = eng_dbf(e, ENG_T_SEQ);
    added = 0;
    for (t = 0; t < ENG_NTABLES; t++) {
        td = eng_table_def(t);
        if (td->seq == NULL)
            continue;
        rc = seq_lookup(e, t, &recno);
        if (rc != ENG_OK)
            return rc;
        if (recno != 0)
            continue;
        dbf_blank(d);
        rc = dbf_put_str(d, ENG_FIELD(SEQ, SEQ_NAME), td->seq);
        if (rc == DBF_OK)
            rc = dbf_put_num(d, ENG_FIELD(SEQ, NEXT_VAL), 1);
        if (rc == DBF_OK)
            rc = dbf_append(d);
        if (rc != DBF_OK)
            return eng_dbferr(e, rc, 0);
        added = 1;
    }
    if (added)
        return eng_dbferr(e, dbf_flush(d), 0);
    return ENG_OK;
}

int eng_id_key(u32 id, char *key)
{
    key[0] = '\0';
    if (id == 0 || id > ENG_MAXID)
        return 0;
    txt_u32w(key, id, ENG_IDLEN, ' ');
    key[ENG_IDLEN] = '\0';
    return 1;
}

u32 eng_key_id(const u8 *key, u16 len)
{
    u32 v;
    u16 i;

    v = 0;
    for (i = 0; i < len && key[i] == ' '; i++)
        ;
    for (; i < len; i++) {
        if (!TXT_ISDIGIT(key[i]))
            return 0;
        v = v * 10 + (u32)(key[i] - '0');
        if (v > ENG_MAXID)
            return 0;
    }
    return v;
}

/* The ID of the record nearest at or below r whose ID can be read (a
   positive number), stepping down at most ENG_IDSTEP records: *at is
   its number, left in the buffer, and *id its ID; *at is 0 when there
   is none. */
static int readable_below(eng *e, int t, u32 r, u32 *at, u32 *id)
{
    i32 v;
    u16 k;
    int blank;
    int rc;

    *at = 0;
    *id = 0;
    for (k = 0; k <= ENG_IDSTEP && r > 0; k++, r--) {
        rc = eng_rec_read(e, t, r);
        if (rc != ENG_OK)
            return rc;
        rc = dbf_get_num(eng_dbf(e, t), 0, &v, &blank);
        if (rc == DBF_OK && !blank && v > 0) {
            *at = r;
            *id = (u32)v;
            return ENG_OK;
        }
    }
    return ENG_OK;
}

/* Largest ID of table t: that of its last record, and for a table whose
   first index is a unique index on its ID (ITEMS1, LOCS1) also the last
   key of that index.  A last record whose ID is blank, not a number,
   zero or negative (a damaged record) is passed over for the one before
   it, stepping down at most ENG_IDSTEP records (readable_below); if none
   of them has an ID, the records give 0.  The open checked that the
   index's keys are ENG_IDLEN bytes. */
static int largest_id(eng *e, int t, u32 *largest)
{
    const eng_xdef *xd;
    u8 key[ENG_IDLEN];
    ndx *x;
    u32 at;
    u32 r;
    u32 v;
    int rc;

    rc = readable_below(e, t, eng_reccount(e, t), &at, largest);
    if (rc != ENG_OK)
        return rc;
    xd = eng_index_def(eng_table_def(t)->firstx);
    if (xd != NULL && xd->unique == 1) {
        x = eng_ndx(e, eng_table_def(t)->firstx);
        rc = ndx_last(x, &r);
        if (rc == DBF_OK && r != 0)
            rc = ndx_key(x, key);
        if (rc != DBF_OK)
            return eng_dbferr(e, rc, 1);
        if (r != 0) {
            v = eng_key_id(key, ndx_keylen(x));
            if (v > *largest)
                *largest = v;
        }
    }
    return ENG_OK;
}

int eng_seq_heal(eng *e)
{
    const eng_tdef *td;
    u32 largest;
    u32 next;
    u32 recno;
    int t;
    int rc;

    for (t = 0; t < ENG_NTABLES; t++) {
        td = eng_table_def(t);
        if (td->seq == NULL)
            continue;
        rc = largest_id(e, t, &largest);
        if (rc == ENG_OK)
            rc = seq_find(e, t, &recno);
        if (rc == ENG_OK)
            rc = seq_value(e, &next);
        if (rc != ENG_OK)
            return rc;
        if (next <= largest) {
            rc = seq_store(e, largest + 1);
            if (rc != ENG_OK)
                return rc;
        }
    }
    return ENG_OK;
}

int eng_id_find(eng *e, int t, u32 id, u32 *recno)
{
    const eng_tdef *td;
    u32 lo;
    u32 hi;
    u32 mid;
    u32 at;
    u32 v;
    int rc;

    *recno = 0;
    td = eng_table_def(t);
    if (td == NULL || td->seq == NULL || eng_dbf(e, t) == NULL)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOID);
    lo = 1;
    hi = eng_reccount(e, t);
    while (lo <= hi) {
        mid = lo + (hi - lo) / 2;
        rc = readable_below(e, t, mid, &at, &v);
        if (rc != ENG_OK)
            return rc;
        if (at == 0)
            return eng_dbferr(e, DBF_EFORMAT, 0);
        if (v == id) {
            if (!dbf_is_deleted(eng_dbf(e, t)))
                *recno = at;
            return ENG_OK;
        }
        if (v < id)
            lo = mid + 1;
        else
            hi = at - 1;
    }
    return ENG_OK;
}
