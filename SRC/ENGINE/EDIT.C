/*
 * EDIT.C - record edits, index upkeep, field-change audit, memo lines
 * and staged memos.
 *
 * An edit works in the table's own record buffer.  eng_edit_begin saves
 * the record image as it was and the key of every index of the table
 * computed from it; eng_edit_commit then compares the buffer with the
 * image field by field for the audit rows, and each index's new key with
 * the saved one, replacing only the entries whose key changed.
 *
 * A memo value is held in eng_memobuf until the commit appends it to the
 * .DBT (memo files are append-only, so every change takes new blocks).
 * The schema's tables have at most one memo field each, so one pending
 * memo is kept; between a memo put and the commit the module must not
 * use eng_memobuf or read another memo.
 *
 * A commit that writes anything is bracketed by the ENG_EDITMARK marker
 * (eng_mark, eng_unmark).  It writes its audit rows first, so a stop
 * never leaves a record on disk without its rows; an open after a stop
 * part-way rebuilds the table's indexes and flags the rows, whose change
 * may not have landed.  A failed commit leaves the marker; if it failed
 * before the record was written, the buffer is restored from the saved
 * image.  Inside a batch (eng_batch_on) a commit leaves its memo, table
 * and indexes unflushed, and its audit rows too unless it rewrites a
 * record in place: the batch's end flushes them once.
 *
 * A memo too long for one request is staged (eng_edit_begin_note): each
 * step reads the stage file into eng_memobuf, adds its line and writes
 * the file whole, so the stage costs no memory between requests; the
 * last step makes it the memo in one edit instead.
 *
 * A quiet edit (eng_edit_quiet) commits with no audit rows: the open-time
 * repairs write each correction's row themselves, and flush it, before
 * the commit, and ITEM SPLIT writes its rows before its new item.
 *
 * One edit is open at a time, so its state lives in this module.  A
 * begin while an edit is still open is a module error: the stale edit is
 * dropped (its buffer restored when it is e's own) and the begin fails
 * with ENG_EINTERNAL.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGEDIT.H"
#include "ENGNOTE.H"

#define I32_MAX         2147483647UL

static const char k_memo[] = "MEMO";
static const char k_deleted[] = "DELETED";
static const char k_stage[] = ENG_NOTESTAGE;

/* A stage (eng_edit_begin_note): its head (stage_head), then the memo
   so far as stored. */
#define STAGE_HEAD      (5 + ENG_STALEN + ENG_USERLEN)

/* Kinds of edit. */
#define ED_OLD          0       /* an existing record                   */
#define ED_NEW          1       /* a record to append                   */
#define ED_REUSE        2       /* a new record over a deleted one      */

#define ED_UNREAD       2       /* oldmemo before old_memo has read it  */

static struct {
    eng *e;                     /* engine of the open edit, or NULL     */
    int  t;                     /* table of the edit                    */
    u16  reclen;
    int  mfield;                /* field of the pending memo, or -1     */
    u16  mlen;                  /* its length in eng_memobuf            */
    u8   kind;                  /* ED_                                  */
    u8   quiet;                 /* the commit writes no audit rows      */
    u8   oldmemo;               /* old_memo's answer, or ED_UNREAD      */
    u8   img[ENG_MAXREC];       /* the record as it was                 */
    u8   keys[ENG_MAXKEYS];     /* its index keys, in index order       */
    char act[ENG_MAXNFIELDS];   /* each field's audit action, or 0      */
} ed_cur;

static int no_edit(eng *e)
{
    return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOEDIT);
}

/* 1 if an edit is open and e owns it. */
static int edit_of(eng *e)
{
    return e != NULL && ed_cur.e == e;
}

/* Close the edit; restore the buffer from the image when restore. */
static void edit_close(int restore)
{
    if (ed_cur.e != NULL && restore)
        memcpy(dbf_recbuf(eng_dbf(ed_cur.e, ed_cur.t)), ed_cur.img,
               ed_cur.reclen);
    ed_cur.e = NULL;
}

/* Common start of an edit of table t: refuse a second edit and check
   the table.  The open checked the record length against the schema,
   and SCHEMA.C checks at compile time that it fits ENG_MAXREC. */
static int edit_start(eng *e, int t)
{
    if (ed_cur.e != NULL) {
        edit_close(ed_cur.e == e);
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_EDITOPEN);
    }
    if (t < 0 || t >= ENG_NTABLES || eng_dbf(e, t) == NULL)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_BADTABLE);
    ed_cur.t = t;
    ed_cur.reclen = dbf_reclen(eng_dbf(e, t));
    ed_cur.mfield = -1;
    ed_cur.mlen = 0;
    ed_cur.quiet = 0;
    return ENG_OK;
}

/* Save the image of the record in the buffer and its index keys.  The
   open checked the key lengths against the schema, and SCHEMA.C checks
   at compile time that each fits ENG_MAXKEYLEN and that a table's keys
   fit ENG_MAXKEYS. */
static int save_old(eng *e, int t)
{
    const eng_tdef *td;
    dbf *d;
    u16 off;
    int x;
    int rc;

    d = eng_dbf(e, t);
    memcpy(ed_cur.img, dbf_recbuf(d), ed_cur.reclen);
    td = eng_table_def(t);
    off = 0;
    for (x = td->firstx; x >= 0 && x < td->firstx + td->nindex; x++) {
        rc = kx_eval(eng_kx(e, x), d, ed_cur.keys + off);
        if (rc != DBF_OK)
            return eng_dbferr(e, rc, 1);
        off = (u16)(off + kx_len(eng_kx(e, x)));
    }
    return ENG_OK;
}

int eng_edit_begin(eng *e, int t, u32 recno)
{
    int rc;

    rc = edit_start(e, t);
    if (rc != ENG_OK)
        return rc;
    if (recno == 0) {
        dbf_blank(eng_dbf(e, t));
        memcpy(ed_cur.img, dbf_recbuf(eng_dbf(e, t)), ed_cur.reclen);
        ed_cur.kind = ED_NEW;
    } else {
        rc = eng_rec_read(e, t, recno);
        if (rc == ENG_OK)
            rc = save_old(e, t);
        if (rc != ENG_OK)
            return rc;
        ed_cur.kind = ED_OLD;
    }
    ed_cur.e = e;
    return ENG_OK;
}

int eng_edit_begin_copy(eng *e, int t, u32 srcrecno)
{
    const eng_tdef *td;
    dbf *d;
    u32 block;
    u16 f;
    u16 len;
    int rc;

    rc = edit_start(e, t);
    if (rc != ENG_OK)
        return rc;
    d = eng_dbf(e, t);
    rc = eng_rec_read(e, t, srcrecno);
    if (rc != ENG_OK)
        return rc;
    td = eng_table_def(t);
    for (f = 0; f < td->nfields; f++) {
        if (dbf_field_at(d, f)->type != 'M')
            continue;
        rc = dbf_get_memo(d, f, &block);
        if (rc != DBF_OK)
            return eng_dbferr(e, rc, 0);
        if (block == 0)
            continue;
        if (ed_cur.mfield >= 0)
            return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_TWOMEMO);
        rc = eng_memo_read(e, t, f, &len);
        if (rc != ENG_OK)
            return rc;
        ed_cur.mfield = (int)f;
        ed_cur.mlen = len;
    }
    memcpy(ed_cur.img, dbf_recbuf(d), ed_cur.reclen);
    dbf_blank(d);
    memcpy(dbf_recbuf(d) + 1, ed_cur.img + 1, ed_cur.reclen - 1u);
    ed_cur.kind = ED_NEW;
    ed_cur.e = e;
    return ENG_OK;
}

int eng_edit_begin_reuse(eng *e, int t, u32 recno)
{
    dbf *d;
    int rc;

    rc = edit_start(e, t);
    if (rc != ENG_OK)
        return rc;
    d = eng_dbf(e, t);
    if (recno == 0 || recno > dbf_reccount(d))
        return eng_fail(e, ENG_ECONSTR, NULL, ENG_TX_NOTDEL);
    rc = eng_rec_read(e, t, recno);
    if (rc != ENG_OK)
        return rc;
    if (!dbf_is_deleted(d))
        return eng_fail(e, ENG_ECONSTR, NULL, ENG_TX_NOTDEL);
    rc = save_old(e, t);
    if (rc != ENG_OK)
        return rc;
    memset(dbf_recbuf(d), ' ', ed_cur.reclen);
    ed_cur.kind = ED_REUSE;
    ed_cur.e = e;
    return ENG_OK;
}

/* Field number of name in the edited table, or -1 after recording
   ENG_EARG naming it. */
static int field_of(eng *e, const char *name)
{
    int f;

    f = eng_field_num(e, ed_cur.t, name);
    if (f < 0)
        eng_fail(e, ENG_EARG, name, ENG_TX_UNKFIELD);
    return f;
}

/* Append the decimal digit c to *mag; 1 if the result stays within the
   i32 range. */
static int add_digit(u32 *mag, char c)
{
    if (*mag > (I32_MAX - (u32)(c - '0')) / 10)
        return 0;
    *mag = *mag * 10 + (u32)(c - '0');
    return 1;
}

/* Parse an N value: an optional '-', digits, and when dec > 0 an
   optional '.' followed by 1..dec digits; *scaled is the value times
   10^dec.  Returns 1 if the text is such a number within the i32 range. */
static int parse_num(const char *s, u8 dec, i32 *scaled)
{
    u32 mag;
    u16 nd;
    u8 nf;
    int neg;

    neg = *s == '-';
    if (neg)
        s++;
    mag = 0;
    for (nd = 0; TXT_ISDIGIT(*s); nd++, s++)
        if (!add_digit(&mag, *s))
            return 0;
    if (nd == 0)
        return 0;
    nf = 0;
    if (*s == '.') {
        for (s++; TXT_ISDIGIT(*s) && nf < dec; nf++, s++)
            if (!add_digit(&mag, *s))
                return 0;
        if (nf == 0)
            return 0;
    }
    if (*s != '\0')
        return 0;
    for (; nf < dec; nf++)
        if (!add_digit(&mag, '0'))
            return 0;
    *scaled = neg ? -(i32)mag : (i32)mag;
    return 1;
}

/* Convert the memo value val as the M rules of eng_edit_put say into
   out, which holds cap bytes and the NUL after them; *len receives the
   stored length.  Returns 1 if it is acceptable, 0 for a byte the rules
   refuse, -1 if the stored text would pass cap. */
static int memo_conv(char *out, u16 cap, const char *val, u16 *len)
{
    u16 n;
    u8 c;

    n = 0;
    for (; *val != '\0'; val++) {
        c = (u8)*val;
        if (c == '\r') {
            if (val[1] != '\n')
                return 0;
            val++;
            c = '\n';
        }
        if (c == '\n') {
            if (n + 2u > cap)
                return -1;
            out[n++] = '\r';
            out[n++] = '\n';
            continue;
        }
        if (c < 0x20 && c != '\t')
            return 0;
        if (n >= cap)
            return -1;
        out[n++] = (char)c;
    }
    out[n] = '\0';
    *len = n;
    return 1;
}

/* Field number f of the edited table, or -1 after recording
   ENG_EINTERNAL. */
static int field_no(eng *e, u16 f)
{
    if (f >= eng_table_def(ed_cur.t)->nfields) {
        eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOFIELD);
        return -1;
    }
    return (int)f;
}

/* eng_edit_put for field number f (valid) of the open edit. */
static int put_field(eng *e, int f, const char *val)
{
    char buf[ENG_MAXVAL];
    const dbf_field *fd;
    u16 flags;
    dbf *d;
    i32 scaled;
    size_t n;
    size_t i;
    int rc;

    d = eng_dbf(e, ed_cur.t);
    fd = dbf_field_at(d, (u16)f);
    flags = eng_field_flags(ed_cur.t, (u16)f);
    n = strlen(val);
    switch (fd->type) {
    case 'C':
        if (n > fd->len)
            return eng_fail(e, ENG_EARG, fd->name, ENG_TX_VALLONG);
        for (i = 0; i < n; i++) {
            buf[i] = val[i];
            if ((u8)buf[i] < 0x20 || (u8)buf[i] == 0x7F)
                return eng_fail(e, ENG_EARG, fd->name, ENG_TX_BADCHAR);
            if (flags & ENG_F_UPPER)
                buf[i] = (char)TXT_UPPER(buf[i]);
        }
        buf[n] = '\0';
        rc = dbf_put_str(d, (u16)f, buf);
        break;
    case 'N':
        if (n == 0) {
            rc = dbf_put_blank(d, (u16)f);
            break;
        }
        if (!parse_num(val, fd->dec, &scaled))
            return eng_fail(e, ENG_EARG, fd->name, ENG_TX_NOTNUM);
        if (scaled < 0 && (flags & ENG_F_NONNEG))
            return eng_fail(e, ENG_EARG, fd->name, ENG_TX_NEGATIVE);
        rc = dbf_put_num(d, (u16)f, scaled);
        break;
    case 'D':
    case 'L':
        rc = dbf_put_str(d, (u16)f, val);
        break;
    case 'M':
        if (ed_cur.mfield >= 0 && ed_cur.mfield != f)
            return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_TWOMEMO);
        if (memo_conv(eng_memobuf(e), ENG_MAXMEMO, val,
                      &ed_cur.mlen) != 1) {
            ed_cur.mfield = -1;
            return eng_fail(e, ENG_EARG, fd->name, ENG_TX_BADMEMO);
        }
        ed_cur.mfield = f;
        return ENG_OK;
    default:
        return eng_fail(e, ENG_EINTERNAL, fd->name, ENG_TX_BADTYPE);
    }
    if (rc != DBF_OK)
        return eng_fail(e, ENG_EARG, fd->name, ENG_TX_BADVALUE);
    return ENG_OK;
}

int eng_edit_put(eng *e, const char *field, const char *val)
{
    int f;

    if (!edit_of(e))
        return no_edit(e);
    f = field_of(e, field);
    if (f < 0)
        return ENG_EARG;
    return put_field(e, f, val);
}

int eng_edit_putf(eng *e, u16 f, const char *val)
{
    if (!edit_of(e))
        return no_edit(e);
    if (field_no(e, f) < 0)
        return ENG_EINTERNAL;
    return put_field(e, (int)f, val);
}

int eng_edit_putnumf(eng *e, u16 f, i32 scaled)
{
    const dbf_field *fd;
    dbf *d;

    if (!edit_of(e))
        return no_edit(e);
    if (field_no(e, f) < 0)
        return ENG_EINTERNAL;
    d = eng_dbf(e, ed_cur.t);
    fd = dbf_field_at(d, f);
    if (fd->type != 'N')
        return eng_fail(e, ENG_EARG, fd->name, ENG_TX_NOTNUMF);
    if (scaled < 0 && (eng_field_flags(ed_cur.t, f) & ENG_F_NONNEG))
        return eng_fail(e, ENG_EARG, fd->name, ENG_TX_NEGATIVE);
    if (dbf_put_num(d, f, scaled) != DBF_OK)
        return eng_fail(e, ENG_EARG, fd->name, ENG_TX_VALWIDE);
    return ENG_OK;
}

/* eng_edit_putmemo for field number f (valid) of the open edit. */
static int putmemo_field(eng *e, int f, u16 len)
{
    const dbf_field *fd;

    fd = dbf_field_at(eng_dbf(e, ed_cur.t), (u16)f);
    if (fd->type != 'M')
        return eng_fail(e, ENG_EARG, fd->name, ENG_TX_NOTMEMO);
    if (ed_cur.mfield >= 0 && ed_cur.mfield != f)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_TWOMEMO);
    if (len > ENG_MAXMEMO)
        return eng_fail(e, ENG_ECONSTR, fd->name, ENG_TX_MEMOLONG);
    eng_memobuf(e)[len] = '\0';
    ed_cur.mfield = f;
    ed_cur.mlen = len;
    return ENG_OK;
}

int eng_edit_putmemo(eng *e, u16 f, u16 len)
{
    if (!edit_of(e))
        return no_edit(e);
    if (field_no(e, f) < 0)
        return ENG_EINTERNAL;
    return putmemo_field(e, (int)f, len);
}

int eng_edit_apply(eng *e, const eng_pair *p, u16 n, u16 deny)
{
    u8 fnum[ENG_MAXPAIRS];
    u16 i;
    int f;
    int rc;

    if (!edit_of(e))
        return no_edit(e);
    if (n > ENG_MAXPAIRS)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_MANYPAIRS);
    for (i = 0; i < n; i++) {
        f = eng_field_num(e, ed_cur.t, p[i].key);
        if (f < 0 || (eng_field_flags(ed_cur.t, (u16)f) & deny) != 0)
            return eng_fail(e, ENG_EARG, p[i].key, ENG_TX_NOSET);
        fnum[i] = (u8)f;
    }
    for (i = 0; i < n; i++) {
        rc = put_field(e, fnum[i], p[i].val);
        if (rc != ENG_OK)
            return rc;
    }
    return ENG_OK;
}

/* 1 if the n bytes at p are all spaces. */
static int blank(const u8 *p, u16 n)
{
    while (n > 0) {
        if (*p != ' ')
            return 0;
        p++;
        n--;
    }
    return 1;
}

int eng_edit_required(eng *e)
{
    const eng_tdef *td;
    const dbf_field *df;
    dbf *d;
    u16 f;

    if (!edit_of(e))
        return no_edit(e);
    td = eng_table_def(ed_cur.t);
    d = eng_dbf(e, ed_cur.t);
    for (f = 0; f < td->nfields; f++) {
        if (!(eng_field_flags(ed_cur.t, f) & ENG_F_REQ))
            continue;
        df = dbf_field_at(d, f);
        if ((int)f == ed_cur.mfield ? ed_cur.mlen == 0
                                    : blank(dbf_recbuf(d) + df->offset,
                                            df->len))
            return eng_fail(e, ENG_EARG, df->name, ENG_TX_REQUIRED);
    }
    return ENG_OK;
}

void eng_edit_delete(eng *e, int deleted)
{
    if (edit_of(e))
        dbf_set_deleted(eng_dbf(e, ed_cur.t), deleted);
}

/* Swap the bytes of field df between the buffer of d and the saved
   image, so the buffer's field functions read the old value; a second
   swap puts them back. */
static void swap_old(dbf *d, const dbf_field *df)
{
    u8 *p;
    u8 c;
    u16 i;

    p = dbf_recbuf(d) + df->offset;
    for (i = 0; i < df->len; i++) {
        c = p[i];
        p[i] = ed_cur.img[df->offset + i];
        ed_cur.img[df->offset + i] = c;
    }
}

/* The text of field f, as eng_rec_text gives it, cut to ENG_AUDVAL
   characters: from the buffer (old 0) or from the saved image (old 1). */
static void aud_text(eng *e, u16 f, int old, char *out)
{
    const dbf_field *df;
    dbf *d;
    u8 c;

    d = eng_dbf(e, ed_cur.t);
    df = dbf_field_at(d, f);
    if (old)
        swap_old(d, df);
    out[0] = '\0';
    dbf_get_str(d, f, out, ENG_AUDVAL + 1);
    if (df->type == 'L') {
        c = (u8)out[0];
        out[0] = (char)(c == 'T' || c == 't' || c == 'Y' || c == 'y' ? 'T' :
                        c == 'F' || c == 'f' || c == 'N' || c == 'n' ? 'F' :
                        '\0');
        out[1] = '\0';
    }
    if (old)
        swap_old(d, df);
}

/* The value of N field f, blank as 0: from the buffer (old 0) or from
   the saved image (old 1). */
static i32 aud_num(eng *e, u16 f, int old)
{
    const dbf_field *df;
    dbf *d;
    i32 v;
    int empty;

    d = eng_dbf(e, ed_cur.t);
    df = dbf_field_at(d, f);
    if (old)
        swap_old(d, df);
    if (dbf_get_num(d, f, &v, &empty) != DBF_OK || empty)
        v = 0;
    if (old)
        swap_old(d, df);
    return v;
}

/* Audit action of a change of field f of the edited table, or 0 when it
   gives no row.  An ENG_F_AUDLOAN field gives O when its value rose, R
   when it fell, and no row when it is equal (blank to 0). */
static char field_action(eng *e, u16 f)
{
    u16 flags;
    i32 was;
    i32 now;

    flags = eng_field_flags(ed_cur.t, f);
    if (flags & (ENG_F_NOAUDIT | ENG_F_HIDDEN))
        return 0;
    if (flags & ENG_F_AUDMOVE)
        return ENG_A_MOVE;
    if (flags & ENG_F_AUDPAR)
        return ENG_A_PARENT;
    if (flags & ENG_F_AUDLOAN) {
        was = aud_num(e, f, 1);
        now = aud_num(e, f, 0);
        return (char)(now == was ? 0 : now < was ? ENG_A_RETURN
                                                 : ENG_A_LOANOUT);
    }
    return ENG_A_UPDATE;
}

/* 1 if memo field f of the saved image holds text: a block number other
   than 0, in a table whose memo file is open, whose memo is not empty; a
   block number or a memo that cannot be read counts as text.  Only the
   first byte is read, through the memo file's own buffer, so eng_memobuf
   keeps the pending memo.  The answer is kept in ed_cur.oldmemo without f,
   so a commit reads at most once: it asks only about ed_cur.mfield, the one
   memo an edit holds. */
static int old_memo(eng *e, dbf *d, u16 f)
{
    const dbf_field *df;
    char tmp[2];
    u32 block;
    u16 len;
    int rc;

    if (ed_cur.oldmemo != ED_UNREAD)
        return ed_cur.oldmemo;
    df = dbf_field_at(d, f);
    swap_old(d, df);
    rc = dbf_get_memo(d, f, &block);
    swap_old(d, df);
    len = 0;
    if (rc == DBF_OK && block != 0 && eng_dbt(e, ed_cur.t) != NULL)
        rc = dbt_read(eng_dbt(e, ed_cur.t), block, tmp, sizeof tmp, &len);
    ed_cur.oldmemo = (u8)(rc != DBF_OK || len > 0);
    return ed_cur.oldmemo;
}

/* 1 if field f of an existing record changed: its bytes differ from the
   image, or, for a memo, a value was put and either the old or the new
   memo is not empty. */
static int field_changed(eng *e, dbf *d, u16 f)
{
    const dbf_field *df;

    df = dbf_field_at(d, f);
    if (df->type == 'M')
        return (int)f == ed_cur.mfield &&
               (ed_cur.mlen > 0 || old_memo(e, d, f));
    return memcmp(dbf_recbuf(d) + df->offset, ed_cur.img + df->offset,
                  df->len) != 0;
}

/* Add the audit rows of the commit, whose IDs are reserved: the I row of
   a new record, or the D row and the rows of the fields count_rows gave
   an action. */
static int write_rows(eng *e, u32 recid, int deltoggle)
{
    const eng_tdef *td;
    char oldv[ENG_AUDVAL + 1];
    char newv[ENG_AUDVAL + 1];
    eng_audrow row;
    dbf *d;
    u16 f;
    int rc;

    td = eng_table_def(ed_cur.t);
    d = eng_dbf(e, ed_cur.t);
    rc = ENG_OK;
    if (ed_cur.kind != ED_OLD) {
        aud_text(e, 0, 0, newv);
        row.action = ENG_A_INSERT;
        row.fld = dbf_field_at(d, 0)->name;
        row.oldval = NULL;
        row.newval = newv;
        rc = eng_audit_add(e, ed_cur.t, recid, &row);
    } else {
        if (deltoggle) {
            row.action = ENG_A_DELETE;
            row.fld = NULL;
            row.oldval = dbf_is_deleted(d) ? NULL : k_deleted;
            row.newval = dbf_is_deleted(d) ? k_deleted : NULL;
            rc = eng_audit_add(e, ed_cur.t, recid, &row);
        }
        for (f = 0; f < td->nfields && rc == ENG_OK; f++) {
            row.action = ed_cur.act[f];
            if (row.action == 0)
                continue;
            row.fld = dbf_field_at(d, f)->name;
            if (dbf_field_at(d, f)->type == 'M') {
                row.oldval = old_memo(e, d, f) ? k_memo : NULL;
                row.newval = ed_cur.mlen > 0 ? k_memo : NULL;
            } else {
                aud_text(e, f, 1, oldv);
                aud_text(e, f, 0, newv);
                row.oldval = oldv;
                row.newval = newv;
            }
            rc = eng_audit_add(e, ed_cur.t, recid, &row);
        }
    }
    return rc;
}

/* Count the audit rows of the commit (none for a quiet edit); for a
   change, also set ed_cur.act to the action of each field that gives a
   row, 0 for the others. */
static u16 count_rows(eng *e, int deltoggle)
{
    const eng_tdef *td;
    dbf *d;
    u16 f;
    u16 n;

    if (ed_cur.quiet)
        return 0;
    if (ed_cur.kind != ED_OLD)
        return 1;
    td = eng_table_def(ed_cur.t);
    d = eng_dbf(e, ed_cur.t);
    ed_cur.oldmemo = ED_UNREAD;
    n = deltoggle ? 1 : 0;
    for (f = 0; f < td->nfields; f++) {
        ed_cur.act[f] = field_action(e, f);
        if (ed_cur.act[f] != 0 && !field_changed(e, d, f))
            ed_cur.act[f] = 0;
        if (ed_cur.act[f] != 0)
            n++;
    }
    return n;
}

/* Replace the index entries of the written record whose key changed. */
static int update_indexes(eng *e, u32 recno)
{
    const eng_tdef *td;
    u8 key[ENG_MAXKEYLEN];
    dbf *d;
    ndx *nx;
    u16 off;
    u16 len;
    int x;
    int rc;
    int rc2;

    td = eng_table_def(ed_cur.t);
    d = eng_dbf(e, ed_cur.t);
    off = 0;
    rc = ENG_OK;
    for (x = td->firstx; x >= 0 && x < td->firstx + td->nindex; x++) {
        nx = eng_ndx(e, x);
        len = kx_len(eng_kx(e, x));
        rc2 = kx_eval(eng_kx(e, x), d, key);
        if (rc2 != DBF_OK) {
            rc = eng_dbferr(e, rc2, 1);
            break;
        }
        if (ed_cur.kind != ED_NEW &&
            memcmp(key, ed_cur.keys + off, len) == 0) {
            off = (u16)(off + len);
            continue;
        }
        if (ed_cur.kind != ED_NEW) {
            rc2 = ndx_delete(nx, ed_cur.keys + off, recno);
            if (rc2 == DBF_ERANGE && rc == ENG_OK)
                rc = eng_fail(e, ENG_EINDEX, NULL, ENG_TX_IXMISS);
            else if (rc2 != DBF_OK && rc == ENG_OK)
                rc = eng_dbferr(e, rc2, 1);
        }
        rc2 = ndx_insert(nx, key, recno);
        if (rc2 == DBF_OK && !eng_batch_on(e))
            rc2 = ndx_flush(nx);
        if (rc2 != DBF_OK && rc == ENG_OK)
            rc = eng_dbferr(e, rc2, 1);
        off = (u16)(off + len);
    }
    return rc;
}

int eng_edit_commit(eng *e, u32 recid)
{
    dbf *d;
    dbt *m;
    u32 block;
    u32 recno;
    u16 n;
    int deltoggle;
    int written;
    int rc;
    int rc2;

    if (!edit_of(e))
        return no_edit(e);
    d = eng_dbf(e, ed_cur.t);
    deltoggle = ed_cur.kind == ED_OLD &&
                (dbf_recbuf(d)[0] == '*') != (ed_cur.img[0] == '*');
    if (ed_cur.kind == ED_OLD && ed_cur.mfield < 0 &&
        memcmp(dbf_recbuf(d), ed_cur.img, ed_cur.reclen) == 0) {
        edit_close(0);
        return ENG_OK;
    }
    n = count_rows(e, deltoggle);

    /* The marker, SEQ (the audit IDs), the audit rows, the memo, the
       record and its indexes, each flushed in that order, so a record
       never reaches the disk before its rows.  In a batch only SEQ is
       flushed, and the rows written so far before a record rewritten in
       place (which may reach the disk as soon as it is written); the
       rest are flushed at the batch's end, AUDIT first.  The marker goes
       once all of them are on disk. */
    written = 0;
    recno = ed_cur.kind == ED_NEW ? dbf_reccount(d) + 1 : dbf_recno(d);
    rc = eng_mark(e, ed_cur.t);
    if (rc == ENG_OK)
        rc = eng_audit_begin(e, n);
    if (rc == ENG_OK && n > 0)
        rc = write_rows(e, recid != 0 ? recid : recno, deltoggle);
    rc2 = n > 0 ? eng_audit_end(e) : ENG_OK;
    if (rc == ENG_OK)
        rc = rc2;
    if (rc == ENG_OK && ed_cur.kind != ED_NEW && eng_batch_on(e))
        rc = eng_dbferr(e, dbf_flush(eng_dbf(e, ENG_T_AUDIT)), 0);
    if (rc == ENG_OK && ed_cur.mfield >= 0) {
        if (ed_cur.mlen == 0) {
            rc = eng_dbferr(e, dbf_put_blank(d, (u16)ed_cur.mfield), 0);
        } else {
            m = eng_dbt(e, ed_cur.t);
            if (m == NULL) {
                rc = eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NODBT);
            } else {
                rc2 = dbt_append(m, eng_memobuf(e), ed_cur.mlen, &block);
                if (rc2 == DBF_OK && !eng_batch_on(e))
                    rc2 = dbt_flush(m);
                if (rc2 == DBF_OK)
                    rc2 = dbf_put_memo(d, (u16)ed_cur.mfield, block);
                rc = eng_dbferr(e, rc2, 0);
            }
        }
    }
    if (rc == ENG_OK) {
        rc2 = ed_cur.kind == ED_NEW ? dbf_append(d) : dbf_write(d);
        written = rc2 == DBF_OK;
        if (rc2 == DBF_OK && !eng_batch_on(e))
            rc2 = dbf_flush(d);
        rc = eng_dbferr(e, rc2, 0);
    }
    if (rc == ENG_OK)
        rc = update_indexes(e, recno);
    if (rc == ENG_OK)
        eng_unmark(e);
    edit_close(!written);
    return rc;
}

int eng_edit_finish(eng *e, int rc, u32 recid)
{
    if (rc != ENG_OK) {
        eng_edit_cancel(e);
        return rc;
    }
    return eng_edit_commit(e, recid);
}

void eng_edit_quiet(eng *e)
{
    if (edit_of(e))
        ed_cur.quiet = 1;
}

int eng_edit_heal(eng *e, u16 f, const char *was, char action,
                  u32 recid)
{
    char now[ENG_AUDVAL + 1];
    eng_audrow row;
    int rc;
    int x;

    if (!edit_of(e))
        return no_edit(e);
    rc = field_no(e, f) < 0 ? ENG_EINTERNAL : ENG_OK;
    if (rc == ENG_OK) {
        eng_rec_str(e, ed_cur.t, f, now, (u16)sizeof now);
        row.action = action;
        row.fld = eng_field_name(e, ed_cur.t, f);
        row.oldval = was;
        row.newval = now;
        rc = eng_audit(e, ed_cur.t, recid, &row);
    }
    if (rc == ENG_OK)
        rc = eng_dbferr(e, dbf_flush(eng_dbf(e, ENG_T_AUDIT)), 0);
    for (x = ENG_X_AUDIT1; rc == ENG_OK && x <= ENG_X_AUDIT2; x++)
        rc = eng_dbferr(e, ndx_flush(eng_ndx(e, x)), 1);
    if (rc == ENG_OK)
        ed_cur.quiet = 1;
    return eng_edit_finish(e, rc, recid);
}

void eng_edit_cancel(eng *e)
{
    if (edit_of(e))
        edit_close(1);
}

/* ---------------------------------------------------------------------
 * Memo lines.
 * --------------------------------------------------------------------- */

/* Append text as a new line to the len-byte memo held in eng_memobuf,
   as eng_edit_begin_note describes, failures naming field; *len
   receives the new length. */
static int memo_addline(eng *e, const char *field, const char *text,
                        u16 *len)
{
    char *m;
    u16 add;
    u16 n;
    int rc;

    m = eng_memobuf(e);
    n = *len;
    if (n > 0) {
        if (n > ENG_MAXMEMO - 2)
            return eng_fail(e, ENG_ECONSTR, field, ENG_TX_MEMOLONG);
        m[n++] = '\r';
        m[n++] = '\n';
    }
    rc = memo_conv(m + n, (u16)(ENG_MAXMEMO - n), text, &add);
    if (rc == 0)
        return eng_fail(e, ENG_EARG, field, ENG_TX_BADMEMO);
    if (rc < 0)
        return eng_fail(e, ENG_ECONSTR, field, ENG_TX_MEMOLONG);
    *len = (u16)(n + add);
    return ENG_OK;
}

/* Table t's stage of the tag in how, or of the actor for tag 0, into
   path (ENG_MAXPATH): the table's name, ENG_NOTESTAGE, and the tag as a
   hex digit, or for tag 0 one hashed from the actor's station, so that
   the stations of one mailbox, which take turns on one slot, seldom
   share a stage. */
static void stage_name(eng *e, int t, u8 how, char *path)
{
    const char *sta;
    const char *usr;
    size_t n;
    u16 h;

    eng_actor(e, &sta, &usr);
    for (h = 0; *sta != '\0'; sta++)
        h = (u16)(h * 31 + (u8)*sta);
    if (how & ENG_NOTE_TAG)
        h = how;
    strcpy(path, eng_dir(e));
    strcat(path, eng_table_def(t)->name);
    strcat(path, k_stage);
    n = strlen(path);
    path[n] = txt_hexdig[h & 15];
    path[n + 1] = '\0';
}

/* The head of a stage of record recno by the actor under the tag in
   how into head (STAGE_HEAD bytes): the record number (LE32), the tag,
   then the actor's station and user, each NUL-padded to its full
   length. */
static void stage_head(eng *e, u32 recno, u8 how, u8 *head)
{
    const char *sta;
    const char *usr;

    eng_actor(e, &sta, &usr);
    memset(head, 0, STAGE_HEAD);
    put_le32(head, recno);
    head[4] = (u8)(how & ENG_NOTE_TAG);
    memcpy(head + 5, sta, strlen(sta));
    memcpy(head + 5 + ENG_STALEN, usr, strlen(usr));
}

/* The stage at path, when its head is head, into eng_memobuf, *len
   receiving its length; else ENG_ECURSOR naming field. */
static int stage_read(eng *e, const char *path, const u8 *head,
                      const char *field, u16 *len)
{
    u8 was[STAGE_HEAD];
    u16 got;
    int ours;
    int rc;
    int h;

    ours = 0;
    rc = cf_open(path, CF_READ | CF_DENYW, &h);
    if (rc == CF_ENOENT)
        return eng_fail(e, ENG_ECURSOR, field, ENG_TX_NOSTAGE);
    if (rc == 0) {
        rc = cf_read(h, was, STAGE_HEAD, &got);
        ours = rc == 0 && got == STAGE_HEAD &&
               memcmp(was, head, STAGE_HEAD) == 0;
        if (ours)
            rc = cf_read(h, eng_memobuf(e), ENG_MAXMEMO, len);
        cf_close(h);
    }
    if (rc != 0)
        return eng_cferr(e, rc, ENG_TX_NONE);
    if (!ours)
        return eng_fail(e, ENG_ECURSOR, field, ENG_TX_NOSTAGE);
    return ENG_OK;
}

/* Write head and the len bytes of eng_memobuf as the stage at path; a
   stage not written whole is removed. */
static int stage_write(eng *e, const char *path, const u8 *head, u16 len)
{
    int rc;
    int rc2;
    int h;

    rc = cf_create(path, &h);
    if (rc == 0) {
        rc = cf_write(h, head, STAGE_HEAD);
        if (rc == 0 && len > 0)
            rc = cf_write(h, eng_memobuf(e), len);
        rc2 = cf_close(h);
        if (rc == 0)
            rc = rc2;
    }
    if (rc != 0)
        cf_remove(path);
    return eng_cferr(e, rc, ENG_TX_NONE);
}

int eng_edit_begin_note(eng *e, int t, u16 f, u32 recno,
                        const char *text, u8 how)
{
    char path[ENG_MAXPATH];
    u8 head[STAGE_HEAD];
    const char *field;
    u16 len;
    int ours;                   /* PART read the actor's stage          */
    int rc;

    field = eng_field_name(e, t, f);
    stage_name(e, t, how, path);
    stage_head(e, recno, how, head);
    len = 0;
    ours = 0;
    if (how & ENG_NOTE_PART) {
        rc = stage_read(e, path, head, field, &len);
        ours = rc == ENG_OK;
    } else if (how & ENG_NOTE_NEW) {
        rc = ENG_OK;
    } else {
        rc = eng_memo_read(e, t, f, &len);
    }
    if (rc == ENG_OK)
        rc = memo_addline(e, field, text, &len);
    if (!ENG_NOTE_WRITES(how))
        return rc == ENG_OK ? stage_write(e, path, head, len) : rc;
    if (ours)
        cf_remove(path);        /* the stage ends with its END          */
    if (rc == ENG_OK)
        rc = eng_edit_begin(e, t, recno);
    if (rc != ENG_OK)
        return rc;
    rc = putmemo_field(e, (int)f, len);
    if (rc != ENG_OK)
        eng_edit_cancel(e);
    return rc;
}
