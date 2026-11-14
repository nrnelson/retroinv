/*
 * TABLES.C - the files of one data directory, the error state, the
 * actor and clock of the audit rows, and record reads.
 *
 * The eng structure lives only here.  It holds one dbf per table, one
 * dbt per table with a memo field, one ndx and one compiled key
 * expression per index, all opened by eng_open_files and closed by
 * eng_close, together with the memo work buffer and the state of the
 * last failure.  The other engine modules reach the files through
 * eng_dbf, eng_dbt, eng_ndx and eng_kx.
 *
 * eng_open_files takes its steps (ENGINE.H) in three passes over the
 * schema of SCHTAB.H, each done for every file before the next starts:
 * recovery (pass_recover) before any file is read, so that a pack or
 * reindex cut short is finished or undone first; creation (pass_create)
 * before any file is opened, so that the marker names every table
 * created, or with an index missing, before anything is written; and
 * opening (pass_open), which opens every table before it builds an
 * index or compiles a key expression against its table.  A failure in
 * any pass closes every file the open has opened.
 *
 * Existence is tested with a read-only, deny-none open: a file that
 * another program holds deny-read/write exists but cannot be opened
 * that way, so it is reported as in use when pass_open opens it.
 */
#define COMPAT_DISK
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGSCH.H"
#include "ENGEDIT.H"
#include "ENGADMIN.H"

static const char k_fields[] = "FIELDS";

/* The extensions and the marker names, each used in several places. */
static const char k_dbf[] = ".DBF";
static const char k_dbt[] = ".DBT";
static const char k_ndx[] = ".NDX";
static const char k_scrdbf[] = ENG_SCRDBF;
static const char k_scrdbt[] = ENG_SCRDBT;
static const char k_scrndx[] = ENG_SCRNDX;
static const char k_editmark[] = ENG_EDITMARK;
static const char k_copymark[] = ENG_COPYMARK;

/* The open's code for a schema row that cannot be copied out of far
   data (eng_schema_field, eng_index_expr): an internal fault, reported
   as ENG_EINTERNAL.  It stands beside the DBF_E codes the open's steps
   return and differs from all of them. */
#define OPEN_ESCHEMA    (-64)

/* The handles the open holds at once to check FILES= (hold_handles):
   the engine's files and the ones it opens for a moment. */
#define NHOLD           (ENG_NHANDLES + ENG_TMPHANDLES)

/* The FILES= the texts ask for is a full handle table and the
   allowance for resident programs. */
extern char files_exact[1];
extern char files_exact[(ENG_FILES == ENG_PROCHANDLES - 2 + ENG_RESFILES) ?
                        1 : 2];

struct eng {
    u8   marked;                /* ENG_EDITMARK present, to be removed  */
    u8   kept;                  /* the marker is left for eng_init_data */
    u8   batch;                 /* a batch is open (AUDIT.C)            */
    u8   fixed;                 /* clock fixed by eng_set_now           */
    u8   pinned;                /* dbf_set_today has been called        */
    u32  audfrom;               /* AUDIT records the marker leaves out  */
    u16  otx;                   /* the open's failure text, if not the
                                   one of its DBF_E code                */
    dbf *t[ENG_NTABLES];
    dbt *m[ENG_NTABLES];        /* NULL for a table without memo        */
    ndx *x[ENG_NINDEX];
    kx  *k[ENG_NINDEX];
    u8   built[ENG_NINDEX];     /* to be built by this open             */
    u8   noted[ENG_NTABLES];    /* named in the marker by this open     */
    u8   named[ENG_NTABLES];    /* named in the marker found, or every
                                   table when it could not be read      */
    char dir[ENG_MAXDIR + 1];
    eng_time now;               /* the fixed clock                      */
    char station[ENG_STALEN + 1];
    char user[ENG_USERLEN + 1];
    char errfield[DBF_NAMELEN + 1];
    u16  errtx;                 /* the text of the last failure         */
    char txmsg[ENG_TXMAX + 1];  /* errtx copied out by eng_errmsg       */
    char path[ENG_MAXPATH];     /* the file the open is working on      */
    char memo[ENG_MAXMEMO + 1];
};

/* ---------------------------------------------------------------------
 * Error state.
 * --------------------------------------------------------------------- */

int eng_fail(eng *e, int code, const char *field, u16 tx)
{
    if (e != NULL) {
        txt_copy(e->errfield, field, sizeof e->errfield);
        e->errtx = tx;
    }
    return code;
}

const char *eng_errfield(eng *e)
{
    return e->errfield;
}

const char *eng_errmsg(eng *e)
{
    eng_tx_copy(e->errtx, e->txmsg);
    return e->txmsg;
}

u16 eng_errtx(eng *e)
{
    return e->errtx;
}

void eng_clear_error(eng *e)
{
    e->errfield[0] = '\0';
    e->errtx = ENG_TX_NONE;
}

/* The ENG_E code and text number of a DBF_E code or OPEN_ESCHEMA;
   index tells whether it came from an index. */
static int map_dbf(int rc, int index, u16 *tx)
{
    switch (rc) {
    case DBF_OK:
        *tx = ENG_TX_NONE;
        return ENG_OK;
    case DBF_EFORMAT:
        if (index) {
            *tx = ENG_TX_IXBAD;
            return ENG_EINDEX;
        }
        *tx = ENG_TX_TBLBAD;
        return ENG_EIO;
    case DBF_EFULL:
        *tx = ENG_TX_DISKFULL;
        return ENG_EFULL;
    case DBF_EHANDLES:
        *tx = ENG_TX_NOHANDLE;
        return ENG_EHANDLES;
    case DBF_EINUSE:
        *tx = ENG_TX_INUSE;
        return ENG_EINUSE;
    case DBF_ENOMEM:
        *tx = ENG_TX_NOMEM;
        return ENG_EINTERNAL;
    case DBF_ENOENT:
        *tx = ENG_TX_NOFILE;
        return ENG_EIO;
    case OPEN_ESCHEMA:
        *tx = ENG_TX_SCHEMA;
        return ENG_EINTERNAL;
    default:
        *tx = ENG_TX_IOERR;
        return ENG_EIO;
    }
}

int eng_dbferr(eng *e, int rc, int index)
{
    u16 tx;
    int code;

    code = map_dbf(rc, index, &tx);
    if (code == ENG_OK)
        return ENG_OK;
    return eng_fail(e, code, NULL, tx);
}

int eng_cferr(eng *e, int cfrc, u16 tx)
{
    u16 dtx;
    int code;

    code = map_dbf(dbf_map_cf(cfrc), 0, &dtx);
    if (code == ENG_OK)
        return ENG_OK;
    return eng_fail(e, code, NULL, tx != ENG_TX_NONE ? tx : dtx);
}

/* ---------------------------------------------------------------------
 * Actor and clock.
 * --------------------------------------------------------------------- */

void eng_set_actor(eng *e, const char *station, const char *user)
{
    txt_copy(e->station, station, sizeof e->station);
    txt_copy(e->user, user, sizeof e->user);
}

void eng_set_user(eng *e, const char *user)
{
    if (user != e->user)
        txt_copy(e->user, user, sizeof e->user);
}

void eng_actor(eng *e, const char **station, const char **user)
{
    *station = e->station;
    *user = e->user;
}

void eng_set_now(eng *e, const eng_time *t)
{
    if (t == NULL) {
        e->fixed = 0;
        return;
    }
    e->fixed = 1;
    memcpy(&e->now, t, sizeof e->now);
    e->pinned = 1;
    dbf_set_today(t->year, t->month, t->day);
}

void eng_now(eng *e, eng_time *t)
{
    u16 y;
    u16 y2;
    u8 mo;
    u8 mo2;
    u8 d;
    u8 d2;
    u8 h;
    u8 mi;
    u8 s;

    if (e->fixed) {
        *t = e->now;
        return;
    }
    /* Read the date on both sides of the time, so a reading across
       midnight does not pair the old date with the new time. */
    dos_today(&y, &mo, &d);
    dos_time(&h, &mi, &s);
    dos_today(&y2, &mo2, &d2);
    if (y2 != y || mo2 != mo || d2 != d) {
        y = y2;
        mo = mo2;
        d = d2;
        dos_time(&h, &mi, &s);
    }
    t->year = y;
    t->month = mo;
    t->day = d;
    t->hour = h;
    t->minute = mi;
    t->second = s;
    /* Once a fixed clock has set the table-header date, DBF.C keeps
       that date; the DOS clock's date replaces it here. */
    if (e->pinned)
        dbf_set_today(y, mo, d);
}

/* The two digits of v into out, no NUL. */
static void two_digits(char *out, u8 v)
{
    txt_u32w(out, v, 2, '0');
}

/* The ENG_DATELEN digits YYYYMMDD of t into out, no NUL. */
static void date_digits(const eng_time *t, char *out)
{
    txt_u32w(out, t->year, 4, '0');
    two_digits(out + 4, t->month);
    two_digits(out + 6, t->day);
}

void eng_today(eng *e, char *out)
{
    eng_time t;

    eng_now(e, &t);
    date_digits(&t, out);
    out[ENG_DATELEN] = '\0';
}

void eng_clock_text(eng *e, char *date, char *tm)
{
    eng_time t;

    eng_now(e, &t);
    date_digits(&t, date);
    date[ENG_DATELEN] = '\0';
    two_digits(tm, t.hour);
    tm[2] = ':';
    two_digits(tm + 3, t.minute);
    tm[5] = ':';
    two_digits(tm + 6, t.second);
    tm[ENG_TIMELEN] = '\0';
}

void eng_stamp(eng *e, char *out)
{
    eng_time t;

    eng_now(e, &t);
    date_digits(&t, out);
    two_digits(out + ENG_DATELEN, t.hour);
    two_digits(out + ENG_DATELEN + 2, t.minute);
    two_digits(out + ENG_DATELEN + 4, t.second);
    out[ENG_STAMPLEN] = '\0';
}

/* ---------------------------------------------------------------------
 * Module support.
 * --------------------------------------------------------------------- */

dbf *eng_dbf(eng *e, int t)
{
    if (t < 0 || t >= ENG_NTABLES)
        return NULL;
    return e->t[t];
}

dbt *eng_dbt(eng *e, int t)
{
    if (t < 0 || t >= ENG_NTABLES)
        return NULL;
    return e->m[t];
}

ndx *eng_ndx(eng *e, int x)
{
    if (x < 0 || x >= ENG_NINDEX)
        return NULL;
    return e->x[x];
}

kx *eng_kx(eng *e, int x)
{
    if (x < 0 || x >= ENG_NINDEX)
        return NULL;
    return e->k[x];
}

const char *eng_dir(eng *e)
{
    return e->dir;
}

char *eng_memobuf(eng *e)
{
    return e->memo;
}

u32 eng_reccount(eng *e, int t)
{
    if (t < 0 || t >= ENG_NTABLES || e->t[t] == NULL)
        return 0;
    return dbf_reccount(e->t[t]);
}

int eng_repaired(eng *e, int t)
{
    if (t < 0 || t >= ENG_NTABLES)
        return 0;
    return e->named[t] != 0;
}

int eng_marker_kept(eng *e)
{
    return e->kept;
}

void eng_marker_done(eng *e)
{
    e->kept = 0;
}

u32 eng_marker_from(eng *e)
{
    return e->audfrom;
}

int eng_batch_on(eng *e)
{
    return e != NULL && e->batch;
}

void eng_set_batch(eng *e, int on)
{
    e->batch = (u8)on;
}

/* ---------------------------------------------------------------------
 * Flushing and closing.
 * --------------------------------------------------------------------- */

/* The order of eng_flush and close_all: SEQ first, as IDs are issued,
   then AUDIT, so that the audit rows of a batch are on the disk before
   the records they describe are counted.  FLUSH_ROWS is the number of
   files in it that come before the tables those rows describe. */
#define FLUSH_ROWS      2
static const int flush_order[ENG_NTABLES] = {
    ENG_T_SEQ, ENG_T_AUDIT, ENG_T_ITEMS, ENG_T_LOCS, ENG_T_CATS, ENG_T_LTYPES,
    ENG_T_MAINT, ENG_T_LOANS, ENG_T_USERS
};

/* A table's header, the count of its records, is not flushed once SEQ or
   AUDIT has failed to flush, so no record is counted while its audit row
   is not; the close (close_all) follows the same rule. */
int eng_flush(eng *e)
{
    const eng_tdef *td;
    int first;
    int firstx;
    int rows;
    int rc;
    int i;
    int t;
    int x;

    first = DBF_OK;
    firstx = 0;
    rows = DBF_OK;
    for (i = 0; i < ENG_NTABLES; i++) {
        t = flush_order[i];
        td = eng_table_def(t);
        if (e->m[t] != NULL) {
            rc = dbt_flush(e->m[t]);
            if (first == DBF_OK)
                first = rc;
        }
        if (e->t[t] != NULL && (i < FLUSH_ROWS || rows == DBF_OK)) {
            rc = dbf_flush(e->t[t]);
            if (first == DBF_OK)
                first = rc;
            if (i < FLUSH_ROWS && rows == DBF_OK)
                rows = rc;
        }
        for (x = td->firstx; x >= 0 && x < td->firstx + td->nindex; x++) {
            if (e->x[x] == NULL)
                continue;
            rc = ndx_flush(e->x[x]);
            if (first == DBF_OK && rc != DBF_OK) {
                first = rc;
                firstx = 1;
            }
        }
    }
    return eng_dbferr(e, first, firstx);
}

/* Close every file of e, the tables in flush_order, keeping the first
   error as an ENG_E code (no text is recorded).  As in eng_flush, once
   SEQ or AUDIT has failed to close, the tables are closed without
   writing their record counts (dbf_drop). */
static int close_all(eng *e)
{
    u16 tx;
    int first;
    int rows;
    int rc;
    int i;
    int t;
    int x;

    first = ENG_OK;
    rows = ENG_OK;
    for (x = 0; x < ENG_NINDEX; x++) {
        if (e->k[x] != NULL) {
            kx_free(e->k[x]);
            e->k[x] = NULL;
        }
        if (e->x[x] != NULL) {
            rc = map_dbf(ndx_close(e->x[x]), 1, &tx);
            e->x[x] = NULL;
            if (first == ENG_OK)
                first = rc;
        }
    }
    for (i = 0; i < ENG_NTABLES; i++) {
        t = flush_order[i];
        if (e->m[t] != NULL) {
            rc = map_dbf(dbt_close(e->m[t]), 0, &tx);
            e->m[t] = NULL;
            if (first == ENG_OK)
                first = rc;
        }
        if (e->t[t] != NULL) {
            rc = map_dbf(i < FLUSH_ROWS || rows == ENG_OK ?
                         dbf_close(e->t[t]) : dbf_drop(e->t[t]), 0, &tx);
            e->t[t] = NULL;
            if (first == ENG_OK)
                first = rc;
            if (i < FLUSH_ROWS && rows == ENG_OK)
                rows = rc;
        }
    }
    return first;
}

int eng_shut(eng *e)
{
    eng_edit_cancel(e);
    eng_audit_drop(e);
    return close_all(e);
}

int eng_close(eng *e)
{
    int rc;

    if (e == NULL)
        return ENG_OK;
    rc = eng_shut(e);
    free(e);
    return rc;
}

int eng_is_open(eng *e)
{
    return e->t[ENG_T_ITEMS] != NULL;
}

/* ---------------------------------------------------------------------
 * Opening.
 * --------------------------------------------------------------------- */

/* e->path = directory prefix + stem + ext. */
static void make_path(eng *e, const char *stem, const char *ext)
{
    size_t n;

    n = strlen(e->dir);
    memcpy(e->path, e->dir, n);
    strcpy(e->path + n, stem);
    strcat(e->path, ext);
}

/* 1 when e->path exists, 0 when it does not, or a negative DBF_E code
   when the directory itself cannot be read. */
static int path_exists(eng *e)
{
    int h;
    int rc;

    rc = cf_open(e->path, CF_READ | CF_DENYNONE, &h);
    if (rc == 0) {
        cf_close(h);
        return 1;
    }
    if (rc == CF_ENOENT)
        return 0;
    if (rc == CF_ENOPATH) {
        e->otx = ENG_TX_NODIR;
        return DBF_ENOENT;
    }
    if (rc == CF_EMFILE)
        return DBF_EHANDLES;
    if (rc == CF_ENOMEM)
        return DBF_ENOMEM;
    return 1;               /* held by another program, or unreadable  */
}

/* Remove e->path; a missing file is not an error. */
static int remove_if(eng *e)
{
    int rc;

    rc = cf_remove(e->path);
    if (rc == CF_ENOENT)
        return DBF_OK;
    return dbf_map_cf(rc);
}

/* Replace the file to with the complete file from: to is removed when
   it exists, from renamed to it, and DOS's buffers written out after
   each step (dos_disk_reset).  0 or a cf_ code. */
static int replace(const char *from, const char *to)
{
    int rc;

    rc = cf_remove(to);
    if (rc != 0 && rc != CF_ENOENT)
        return rc;
    dos_disk_reset();
    rc = cf_rename(from, to);
    dos_disk_reset();
    return rc;
}

/* The copy of file stem+ext of the directory of prefix pre, under the
   scratch name stem+scr: with put 1 renamed into place when it exists
   (replace), else removed when it exists.  0 or a cf_ code. */
static int put_copy(const char *pre, const char *stem, const char *scr,
                    const char *ext, int put)
{
    char from[ENG_MAXPATH];
    char to[ENG_MAXPATH];
    int rc;

    strcpy(from, pre);
    strcat(from, stem);
    strcpy(to, from);
    strcat(from, scr);
    strcat(to, ext);
    if (!put) {
        rc = cf_remove(from);
        return rc == CF_ENOENT ? 0 : rc;
    }
    rc = cf_exists(from);
    if (rc < 0)
        return -rc;
    return rc == 0 ? 0 : replace(from, to);
}

/* put_copy for every table, memo file and index; 0 or a cf_ code. */
static int put_copies(const char *pre, int put)
{
    const eng_tdef *td;
    int rc;
    int i;

    rc = 0;
    for (i = 0; rc == 0 && i < ENG_NTABLES; i++) {
        td = eng_table_def(i);
        rc = put_copy(pre, td->name, k_scrdbt, k_dbt, put);
        if (rc == 0)
            rc = put_copy(pre, td->name, k_scrdbf, k_dbf, put);
    }
    for (i = 0; rc == 0 && i < ENG_NINDEX; i++)
        rc = put_copy(pre, eng_index_def(i)->name, k_scrndx, k_ndx, put);
    return rc;
}

int eng_drop_copies(const char *pre)
{
    return put_copies(pre, 0);
}

int eng_put_copies(const char *pre)
{
    char mark[ENG_MAXPATH];
    int put;
    int rc;

    strcpy(mark, pre);
    strcat(mark, k_copymark);
    put = cf_exists(mark);
    if (put < 0)
        return -put;
    rc = put_copies(pre, put);
    if (rc == 0 && put) {
        rc = cf_remove(mark);
        dos_disk_reset();
    }
    return rc;
}

/* Rename the complete scratch file stem+scratch into place as stem+ext
   (replace); a DBF_E code. */
static int rename_into(eng *e, const char *stem, const char *scratch,
                       const char *ext)
{
    char from[ENG_MAXPATH];

    make_path(e, stem, scratch);
    strcpy(from, e->path);
    make_path(e, stem, ext);
    return dbf_map_cf(replace(from, e->path));
}

/* Create table t (and its memo file) from the schema, under the scratch
   names first: the .DBT is renamed into place before the .DBF, whose
   rename is the commit point.  A .DBT left beside no .DBF belongs to no
   table and is replaced. */
static int create_table(eng *e, int t)
{
    const eng_tdef *td;
    dbf_field *fl;
    u16 i;
    int rc;

    td = eng_table_def(t);
    make_path(e, td->name, k_dbf);
    fl = (dbf_field *)malloc(sizeof(dbf_field) * td->nfields);
    if (fl == NULL)
        return DBF_ENOMEM;
    for (i = 0; i < td->nfields; i++)
        if (eng_schema_field(t, i, &fl[i]) != ENG_OK) {
            free(fl);
            return OPEN_ESCHEMA;
        }
    rc = DBF_OK;
    if (td->memo) {
        make_path(e, td->name, k_scrdbt);
        rc = dbt_create(e->path);
    }
    if (rc == DBF_OK) {
        make_path(e, td->name, k_scrdbf);
        rc = dbf_create(e->path, fl, td->nfields);
    }
    free(fl);
    if (rc == DBF_OK && td->memo)
        rc = rename_into(e, td->name, k_scrdbt, k_dbt);
    if (rc == DBF_OK)
        rc = rename_into(e, td->name, k_scrdbf, k_dbf);
    return rc;
}

/* Build index x afresh: an empty index with the schema's expression
   expr is created under its scratch name and renamed over the old file,
   then, unless the table has no record (a new one: the empty index is
   already the one ndx_reindex would write, and the open spares its
   buffers), filled from its open table by ndx_reindex, which replaces
   the file safely by itself.  The old file is never read, so a damaged
   one cannot stop the rebuild.  e->path is left at the index. */
static int fresh_index(eng *e, int x, const char *expr)
{
    const eng_xdef *xd;
    int rc;

    xd = eng_index_def(x);
    make_path(e, xd->name, k_scrndx);
    rc = ndx_create(e->path, expr, xd->keylen);
    if (rc == DBF_OK)
        rc = rename_into(e, xd->name, k_scrndx, k_ndx);
    make_path(e, xd->name, k_ndx);
    if (rc == DBF_OK && dbf_reccount(e->t[xd->table]) != 0)
        rc = ndx_reindex(e->t[xd->table], e->path, NULL);
    return rc;
}

/* Write the marker's count line for count AUDIT records into line
   (ENG_MARKHEAD bytes); the return is its length. */
static u16 count_line(char *line, u32 count)
{
    u16 n;

    n = txt_u32(line, count);
    line[n++] = '\r';
    line[n++] = '\n';
    return n;
}

/* Follow the k bytes just written to the open marker h at pos, the
   start of a disk sector, with zeros to the sector's end, then cut the
   file back to pos + k, so that the sector holds nothing but the
   marker; a cf_ code.  The first write into a sector past a file's end
   takes a DOS buffer as it is, so the rest of the sector would go to
   the disk with whatever the buffer last held, such as a mailbox
   request and the password in it. */
static int seal(int h, u32 pos, u16 k)
{
    int rc;

    rc = cf_zero(h, (u16)(DOS_SECTOR - k));
    return rc == 0 ? cf_truncate(h, pos + k) : rc;
}

/* Write at position pos of the open marker h the k bytes at line, then
   the table name name (n bytes) and CR LF, sealed when it starts a
   sector, close it and write the directory out; a cf_ code. */
static int put_line(int h, char *line, u16 k, const char *name, u16 n,
                    u32 pos)
{
    int rc;
    int rc2;

    memcpy(line + k, name, n);
    k = (u16)(k + n);
    line[k++] = '\r';
    line[k++] = '\n';
    rc = cf_write(h, line, k);
    if (rc == 0 && (u16)pos % DOS_SECTOR == 0)
        rc = seal(h, pos, k);
    rc2 = cf_close(h);
    if (rc == 0)
        rc = rc2;
    if (rc == 0)
        dos_disk_reset();
    return rc;
}

int eng_mark_line(eng *e, int t, int fresh)
{
    char buf[ENG_MARKMAX];
    char line[ENG_MARKHEAD + ENG_TBLNAMELEN + 2];
    const char *name;
    u32 size;
    u16 got;
    u16 pos;
    u16 n;
    int named;
    int rc;
    int h;

    /* SCHEMA.C checks at compile time that every table name fits line. */
    name = eng_table_def(t)->name;
    n = (u16)strlen(name);
    make_path(e, k_editmark, "");
    rc = fresh ? CF_ENOENT : cf_open(e->path, CF_RDWR | CF_DENYRW, &h);
    if (rc == CF_ENOENT) {
        rc = cf_create(e->path, &h);
        if (rc == 0)
            rc = put_line(h, line,
                          e->t[ENG_T_AUDIT] == NULL ? 0 :
                          count_line(line,
                                     dbf_reccount(e->t[ENG_T_AUDIT])),
                          name, n, 0UL);
        return rc == 0 ? ENG_MARKNEW : rc;
    }
    if (rc != 0)
        return rc;
    got = 0;
    rc = cf_size(h, &size);
    named = rc == 0 && size > ENG_MARKMAX;
    if (rc == 0 && !named)
        rc = cf_read(h, buf, (u16)size, &got);
    for (pos = 0; rc == 0 && !named && pos + n + 2u <= got; pos++)
        named = (pos == 0 || buf[pos - 1] == '\n') &&
                memcmp(buf + pos, name, n) == 0 &&
                buf[pos + n] == '\r' && buf[pos + n + 1] == '\n';
    if (rc == 0 && !named)
        rc = cf_seek(h, size);
    if (rc == 0 && !named)
        return put_line(h, line, 0, name, n, size);
    cf_close(h);
    return rc;
}

/* Name table t in the edit marker (eng_mark_line) unless this open has
   already; the line is on the disk before anything it covers is
   written.  The return is a DBF_E code. */
static int note_table(eng *e, int t)
{
    int rc;

    if (e->noted[t])
        return DBF_OK;
    rc = eng_mark_line(e, t, 0);
    if (rc != 0 && rc != ENG_MARKNEW) {
        e->otx = ENG_TX_NOMARK;
        return dbf_map_cf(rc);
    }
    e->noted[t] = 1;
    e->marked = 1;
    return DBF_OK;
}

int eng_layout(dbf *d, int t)
{
    const eng_tdef *td;
    const dbf_field *f;
    dbf_field want;
    u16 i;

    td = eng_table_def(t);
    if (dbf_nfields(d) != td->nfields)
        return DBF_EFORMAT;
    for (i = 0; i < td->nfields; i++) {
        if (eng_schema_field(t, i, &want) != ENG_OK)
            return OPEN_ESCHEMA;
        f = dbf_field_at(d, i);
        if (f == NULL || !txt_ieq(f->name, want.name) ||
            f->type != want.type || f->len != want.len ||
            f->dec != want.dec)
            return DBF_EFORMAT;
    }
    return DBF_OK;
}

/* The table named by the n bytes at p, or -1. */
static int marked_table(const char *p, u16 n)
{
    const eng_tdef *td;
    int t;

    for (t = 0; t < ENG_NTABLES; t++) {
        td = eng_table_def(t);
        if (strlen(td->name) == n && memcmp(td->name, p, n) == 0)
            return t;
    }
    return -1;
}

/* No count line: the marker covers no audit rows (MARK_NOCOUNT is
   lowered to the record count of AUDIT once it is open). */
#define MARK_NOCOUNT    0xFFFFFFFFUL

/* Mark the indexes named by the edit marker e->path for rebuilding: for
   each table line, a table name and CR LF, the indexes of that table,
   and those of AUDIT, the tables noted in e->named, which the open has
   cleared; e->audfrom receives the count of its count line (one of
   MARK_NOCOUNT, like any count at or above the AUDIT records, covers
   no rows).  An empty or unreadable marker, a line that is not a table
   name and CR LF, a count over MARK_NOCOUNT, or a marker longer than
   ENG_MARKMAX marks every index and gives e->audfrom 0.  The return is
   a DBF_E code. */
static int read_marker(eng *e)
{
    char buf[ENG_MARKMAX];
    u32 size;
    u32 v;
    u16 got;
    u16 pos;
    u16 n;
    int all;
    int rc;
    int h;
    int t;
    int x;

    rc = cf_open(e->path, CF_READ | CF_DENYNONE, &h);
    if (rc == CF_ENOENT)
        return DBF_OK;
    if (rc == CF_EMFILE || rc == CF_ENOPATH) {
        e->otx = rc == CF_EMFILE ? ENG_TX_RDMARK : ENG_TX_NODIR;
        return rc == CF_EMFILE ? DBF_EHANDLES : DBF_ENOENT;
    }
    got = 0;
    if (rc == 0) {
        if (cf_size(h, &size) != 0 || size > ENG_MARKMAX ||
            cf_read(h, buf, (u16)sizeof buf, &got) != 0)
            got = 0;
        cf_close(h);
    }
    e->marked = 1;
    all = got == 0;
    pos = 0;
    v = 0;
    while (!all && pos < got && TXT_ISDIGIT(buf[pos])) {
        n = (u16)(buf[pos++] - '0');
        all = v > (MARK_NOCOUNT - n) / 10;
        v = v * 10 + n;
    }
    if (!all && pos > 0) {
        all = pos + 2u > got || buf[pos] != '\r' || buf[pos + 1] != '\n';
        pos = (u16)(pos + 2u);
        e->audfrom = v;
    }
    while (!all && pos < got) {
        n = 0;
        while (pos + n < got && buf[pos + n] != '\r' && n <= ENG_TBLNAMELEN)
            n++;
        if (n == 0 || n > ENG_TBLNAMELEN || pos + n + 2u > got ||
            buf[pos + n] != '\r' || buf[pos + n + 1] != '\n') {
            all = 1;
            break;
        }
        t = marked_table(buf + pos, n);
        if (t < 0)
            all = 1;
        else
            e->named[t] = 1;
        pos = (u16)(pos + n + 2u);
    }
    /* Every change the marker covers also appends AUDIT rows. */
    e->named[ENG_T_AUDIT] = 1;
    if (all) {
        e->audfrom = 0;
        memset(e->named, 1, sizeof e->named);
    }
    for (x = 0; x < ENG_NINDEX; x++)
        if (e->named[eng_index_def(x)->table])
            e->built[x] = 1;
    return DBF_OK;
}

/* Pass 1: recovery of interrupted packs and reindexes.  The return is a
   DBF_E code; *index tells whether an index failed. */
static int pass_recover(eng *e, int *index)
{
    const eng_tdef *td;
    const eng_xdef *xd;
    int rc;
    int t;
    int x;

    *index = 0;
    for (t = 0; t < ENG_NTABLES; t++) {
        td = eng_table_def(t);
        make_path(e, td->name, k_dbf);
        rc = dbf_pack_recover(e->path);
        if (rc != DBF_OK)
            return rc;
    }
    *index = 1;
    for (x = 0; x < ENG_NINDEX; x++) {
        xd = eng_index_def(x);
        make_path(e, xd->name, k_ndx);
        rc = ndx_reindex_recover(e->path);
        if (rc != DBF_OK)
            return rc;
    }
    /* Files a stopped open was creating, or a RESTORE copying in. */
    *index = 0;
    make_path(e, k_copymark, "");
    rc = dbf_map_cf(eng_put_copies(e->dir));
    if (rc != DBF_OK)
        return rc;
    make_path(e, k_editmark, "");
    return read_marker(e);
}

/* A .DBT of table t whose .DBF is missing may be replaced when the
   marker names the table (a creation that stopped before its .DBF was
   renamed into place) or when it is exactly the empty memo file
   dbt_create writes (next free block 1, the rest of its block zero);
   any other one may hold memos of a table that was lost, and the open
   refuses it.  The return is a DBF_E code. */
static int lone_memo_ok(eng *e, int t)
{
    const eng_tdef *td;
    u8 *blk;
    u32 size;
    u16 got;
    u16 i;
    int ok;
    int rc;
    int h;

    td = eng_table_def(t);
    if (!td->memo || e->named[t])
        return DBF_OK;
    make_path(e, td->name, k_dbt);
    rc = cf_open(e->path, CF_READ | CF_DENYNONE, &h);
    if (rc == CF_ENOENT)
        return DBF_OK;
    if (rc != 0)
        return dbf_map_cf(rc);
    blk = (u8 *)malloc(DBT_BLOCK);
    ok = 0;
    if (blk != NULL && cf_size(h, &size) == 0 && size == DBT_BLOCK &&
        cf_read(h, blk, DBT_BLOCK, &got) == 0 && got == DBT_BLOCK &&
        get_le32(blk) == 1) {
        ok = 1;
        for (i = 4; i < DBT_BLOCK; i++)
            if (blk[i] != 0)
                ok = 0;
    }
    cf_close(h);
    if (blk == NULL)
        return DBF_ENOMEM;
    free(blk);
    if (ok)
        return DBF_OK;
    e->otx = ENG_TX_MEMOORPH;
    return DBF_EFORMAT;
}

/* Pass 2: creation of missing tables, and the choice of the indexes to
   build.  A new table, and an existing table with a missing index, is
   first named in the marker, so all of its indexes are rebuilt by
   whichever open gets through pass 3. */
static int pass_create(eng *e, int *index)
{
    const eng_tdef *td;
    const eng_xdef *xd;
    int rc;
    int t;
    int x;

    *index = 0;
    for (t = 0; t < ENG_NTABLES; t++) {
        td = eng_table_def(t);
        make_path(e, td->name, k_dbf);
        rc = path_exists(e);
        if (rc < 0)
            return rc;
        if (rc == 0) {
            rc = lone_memo_ok(e, t);
            if (rc == DBF_OK)
                rc = note_table(e, t);
            if (rc == DBF_OK)
                rc = create_table(e, t);
            if (rc != DBF_OK)
                return rc;
            for (x = td->firstx; x >= 0 && x < td->firstx + td->nindex;
                 x++)
                e->built[x] = 1;
        } else if (td->memo) {
            make_path(e, td->name, k_dbt);
            rc = path_exists(e);
            if (rc < 0)
                return rc;
            if (rc == 0) {
                e->otx = ENG_TX_MEMOMISS;
                return DBF_ENOENT;
            }
        }
    }
    *index = 1;
    for (x = 0; x < ENG_NINDEX; x++) {
        xd = eng_index_def(x);
        make_path(e, xd->name, k_ndx);
        rc = path_exists(e);
        if (rc < 0)
            return rc;
        if (rc == 0) {
            rc = note_table(e, xd->table);
            if (rc != DBF_OK)
                return rc;
            e->built[x] = 1;
        }
    }
    return DBF_OK;
}

/* Cut the bytes after the last record of the open table t, which an
   append stopped between writing its record and rewriting the header's
   record count leaves (dbf_trim), and commit the file at once, so that
   its directory entry has the new size before this open writes other
   files.  Only a table named in the marker, AUDIT when there is a
   marker (every change it covers appends audit rows), and SEQ, which is
   appended to outside any commit, is cut: there the record was never
   committed (the marker also makes this open rebuild the table's
   indexes, and its ID only leaves a gap).  Any other table keeps its
   tail, as a table another program was appending to may hold records
   there.  The return is a DBF_E code. */
static int trim_table(eng *e, int t)
{
    int rc;

    if (!e->named[t] && !e->noted[t] && t != ENG_T_SEQ)
        return DBF_OK;
    rc = dbf_trim(e->t[t]);
    if (rc == DBF_OK)
        rc = dbf_flush(e->t[t]);
    return rc;
}

/* 1 when the open AUDIT2 has the newest row of the open AUDIT, its
   last record, as the first entry of that row's user, where the key
   puts a user's newest row: one record read and one seek.  It is 0 for
   an index that lacks that row (one of other data or of before the
   table's last rows, or one emptied by a rebuild that stopped) and for
   one with a later row of that user (one of after the table).  Later
   rows of other users alone, and entries below a user's newest, it
   cannot tell.  A table without records has no newest row and gives 0
   too. */
static int audit2_fits(eng *e)
{
    dbf *d;
    u32 n;
    u32 r;
    int exact;

    d = e->t[ENG_T_AUDIT];
    n = dbf_reccount(d);
    return dbf_read(d, n) == DBF_OK &&
           ndx_seek(e->x[ENG_X_AUDIT2],
                    dbf_recbuf(d) +
                    dbf_field_at(d, ENG_FIELD(AUDIT, USERNAME))->offset,
                    ENG_USERLEN, &r, &exact) == DBF_OK &&
           r == n && exact;
}

/* Pass 3: opening and checking every file, and compiling the key
   expressions.  An AUDIT2 that passes the checks but is not its
   table's (audit2_fits) is built afresh too, with no marker: after a
   stop the next open finds it missing, empty or as it was, and builds
   it. */
static int pass_open(eng *e, int *index)
{
    const eng_tdef *td;
    const eng_xdef *xd;
    char expr[NDX_MAXEXPR + 1];
    int rc;
    int t;
    int x;

    *index = 0;
    for (t = 0; t < ENG_NTABLES; t++) {
        td = eng_table_def(t);
        make_path(e, td->name, k_dbf);
        rc = dbf_open(&e->t[t], e->path, DBF_RDWR);
        if (rc == DBF_OK)
            rc = eng_layout(e->t[t], t);
        if (rc == DBF_OK)
            rc = trim_table(e, t);
        if (rc != DBF_OK)
            return rc;
        if (td->memo) {
            make_path(e, td->name, k_dbt);
            rc = dbt_open(&e->m[t], e->path, DBF_RDWR);
            if (rc != DBF_OK)
                return rc;
        }
    }
    *index = 1;
    for (x = 0; x < ENG_NINDEX; x++) {
        xd = eng_index_def(x);
        make_path(e, xd->name, k_ndx);
        if (eng_index_expr(x, expr, (u16)sizeof expr) != ENG_OK)
            return OPEN_ESCHEMA;
        for (;;) {
            if (e->built[x]) {
                rc = fresh_index(e, x, expr);
                if (rc != DBF_OK)
                    return rc;
            }
            rc = ndx_open(&e->x[x], e->path, DBF_RDWR);
            if (rc != DBF_OK)
                return rc;
            /* ndx_expr leaves out the blanks dBASE stores after it. */
            if (!txt_ieq(ndx_expr(e->x[x]), expr) ||
                ndx_keylen(e->x[x]) != xd->keylen)
                return DBF_EFORMAT;
            if (x != ENG_X_AUDIT2 || e->built[x] || audit2_fits(e))
                break;
            ndx_close(e->x[x]);
            e->x[x] = NULL;
            e->built[x] = 1;
        }
        rc = kx_compile(&e->k[x], e->t[xd->table], expr);
        if (rc != DBF_OK)
            return rc;
        if (kx_len(e->k[x]) != xd->keylen)
            return DBF_EFORMAT;
    }
    return DBF_OK;
}

/* Write the kept marker afresh: the count line of the audit rows it
   covered (e->audfrom), so rows not yet flagged stay covered, then one
   line for each table it named (or this open noted), in schema order, at
   most ENG_MARKMAX bytes, so later changes can append to it whatever the
   old file held.  The file is created empty, written and sealed, and DOS
   sets its size at close, so a stop part-way leaves an empty marker,
   which the next open reads as unreadable and so repairs every table and
   flags every audit row.  The return is a DBF_E code. */
static int rewrite_marker(eng *e)
{
    char buf[ENG_MARKMAX];
    const char *name;
    u16 n;
    int rc;
    int h;
    int t;

    n = count_line(buf, e->audfrom);
    for (t = 0; t < ENG_NTABLES; t++) {
        if (!e->named[t] && !e->noted[t])
            continue;
        name = eng_table_def(t)->name;
        while (*name != '\0')
            buf[n++] = *name++;
        buf[n++] = '\r';
        buf[n++] = '\n';
    }
    make_path(e, k_editmark, "");
    rc = cf_create(e->path, &h);
    if (rc == 0) {
        rc = cf_write(h, buf, n);
        if (rc == 0)
            rc = seal(h, 0UL, n);
        if (cf_close(h) != 0 && rc == 0)
            rc = CF_EFULL;
    }
    dos_disk_reset();
    if (rc != 0) {
        e->otx = ENG_TX_NOMARK;
        return dbf_map_cf(rc);
    }
    return DBF_OK;
}

void eng_why(char *why, u16 whysz, const char *what, u16 tx)
{
    char text[ENG_TXMAX + 1];
    const char *msg;
    u16 n;

    if (why == NULL || whysz == 0)
        return;
    eng_tx_copy(tx, text);
    n = 0;
    while (what != NULL && *what != '\0' && n + 1u < whysz)
        why[n++] = *what++;
    if (text[0] != '\0' && n > 0) {
        if (n + 2u >= whysz) {
            text[0] = '\0';
        } else {
            why[n++] = ':';
            why[n++] = ' ';
        }
    }
    for (msg = text; *msg != '\0' && n + 1u < whysz; msg++)
        why[n++] = *msg;
    why[n] = '\0';
}

/* Hold NHOLD handles at once on SEQ.DBF, opened read-only and
   deny-none, then close them, so that a FILES= too small for every file
   the engine keeps open and every one it opens for a moment fails the
   open rather than a request later.  The tables are not open yet:
   SHARE refuses a second open of a file held deny-read/write, even by
   the same program.  The return is a DBF_E code. */
static int hold_handles(eng *e)
{
    int h[NHOLD];
    int n;
    int rc;

    make_path(e, eng_table_def(ENG_T_SEQ)->name, k_dbf);
    n = 0;
    do
        rc = cf_open(e->path, CF_READ | CF_DENYNONE, &h[n]);
    while (rc == 0 && ++n < NHOLD);
    while (n > 0)
        cf_close(h[--n]);
    return dbf_map_cf(rc);
}

/* Open the files of e's directory: the per-open state is cleared, then
   the steps of eng_open_files run.  On failure every file is closed
   again, and the return is the ENG_E code with its text in *tx. */
static int open_all(eng *e, u16 *tx)
{
    int index;
    int code;
    int rc;
    int i;

    for (i = 0; i < ENG_NINDEX; i++)
        e->built[i] = 0;
    for (i = 0; i < ENG_NTABLES; i++) {
        e->noted[i] = 0;
        e->named[i] = 0;
    }
    e->marked = 0;
    e->kept = 0;
    e->audfrom = MARK_NOCOUNT;
    e->otx = ENG_TX_NONE;
    index = 0;
    rc = pass_recover(e, &index);
    if (rc == DBF_OK)
        rc = pass_create(e, &index);
    if (rc == DBF_OK)
        rc = hold_handles(e);
    if (rc == DBF_OK)
        rc = pass_open(e, &index);
    /* A marker naming ITEMS or LOANS (every unreadable one does), or
       covering audit rows, is left for the repairs of eng_init_data,
       which flag those rows and remove the marker once they are done. */
    if (rc == DBF_OK && e->audfrom > dbf_reccount(e->t[ENG_T_AUDIT]))
        e->audfrom = dbf_reccount(e->t[ENG_T_AUDIT]);
    if (rc == DBF_OK && e->marked &&
        (e->named[ENG_T_ITEMS] || e->named[ENG_T_LOANS] ||
         e->audfrom < dbf_reccount(e->t[ENG_T_AUDIT]))) {
        rc = rewrite_marker(e);
        e->kept = (u8)(rc == DBF_OK);
    } else if (rc == DBF_OK && e->marked) {
        make_path(e, k_editmark, "");
        rc = remove_if(e);
        dos_disk_reset();
    }
    if (rc == DBF_OK)
        return ENG_OK;
    code = map_dbf(rc, index, tx);
    if (e->otx != ENG_TX_NONE)
        *tx = e->otx;
    close_all(e);
    return code;
}

int eng_open_files(eng **out, const char *dir, const eng_time *now,
                   char *why, u16 whysz)
{
    eng *e;
    u16 tx;
    int rc;

    *out = NULL;
    eng_why(why, whysz, NULL, ENG_TX_NONE);
    if (dir == NULL || strlen(dir) > ENG_MAXDIR) {
        eng_why(why, whysz, dir, ENG_TX_DIRLONG);
        return ENG_EARG;
    }
    e = (eng *)malloc(sizeof(eng));
    if (e == NULL) {
        eng_why(why, whysz, dir, ENG_TX_NOMEM);
        return ENG_EINTERNAL;
    }
    memset(e, 0, sizeof(eng));
    strcpy(e->dir, dir);
    if (now != NULL)
        eng_set_now(e, now);

    if (dos_set_handles(ENG_PROCHANDLES) == CF_ENOMEM) {
        free(e);
        eng_why(why, whysz, dir, ENG_TX_NOMEM);
        return ENG_EINTERNAL;
    }
    rc = open_all(e, &tx);
    if (rc != ENG_OK) {
        eng_why(why, whysz, e->path, tx);
        free(e);
        return rc;
    }
    *out = e;
    return ENG_OK;
}

int eng_marker_on(eng *e)
{
    make_path(e, k_editmark, "");
    return cf_exists(e->path);
}

int eng_reopen(eng *e)
{
    u16 tx;
    int rc;

    rc = open_all(e, &tx);
    if (rc != ENG_OK)
        return eng_fail(e, rc, NULL, tx);
    return ENG_OK;
}

/* ---------------------------------------------------------------------
 * Record access.
 * --------------------------------------------------------------------- */

/* The table t, or NULL after recording ENG_EINTERNAL. */
static dbf *table_of(eng *e, int t)
{
    if (t < 0 || t >= ENG_NTABLES || e->t[t] == NULL) {
        eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOTABLE);
        return NULL;
    }
    return e->t[t];
}

const dbf_field *eng_field(eng *e, int t, u16 f)
{
    if (t < 0 || t >= ENG_NTABLES || e->t[t] == NULL)
        return NULL;
    return dbf_field_at(e->t[t], f);
}

int eng_field_num(eng *e, int t, const char *name)
{
    if (t < 0 || t >= ENG_NTABLES || e->t[t] == NULL || name == NULL)
        return -1;
    return dbf_field_index(e->t[t], name);
}

const char *eng_field_name(eng *e, int t, u16 f)
{
    const dbf_field *fd;

    fd = eng_field(e, t, f);
    return fd != NULL ? fd->name : "";
}

int eng_rec_read(eng *e, int t, u32 recno)
{
    dbf *d;

    d = table_of(e, t);
    if (d == NULL)
        return ENG_EINTERNAL;
    return eng_dbferr(e, dbf_read(d, recno), 0);
}

int eng_rec_text(eng *e, int t, u16 f, char *out)
{
    const dbf_field *fd;
    dbf *d;
    int rc;

    out[0] = '\0';
    d = table_of(e, t);
    if (d == NULL)
        return ENG_EINTERNAL;
    fd = dbf_field_at(d, f);
    if (fd == NULL)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOFIELD);
    if (fd->type == 'M')
        return ENG_OK;
    rc = dbf_get_str(d, f, out, ENG_MAXVAL);
    if (rc != DBF_OK)
        return eng_dbferr(e, rc, 0);
    if (fd->type == 'L') {
        if (out[0] == 'T' || out[0] == 't' || out[0] == 'Y' ||
            out[0] == 'y')
            strcpy(out, "T");
        else if (out[0] == 'F' || out[0] == 'f' || out[0] == 'N' ||
                 out[0] == 'n')
            strcpy(out, "F");
        else
            out[0] = '\0';
    }
    return ENG_OK;
}

void eng_rec_str(eng *e, int t, u16 f, char *out, u16 sz)
{
    dbf *d;

    d = eng_dbf(e, t);
    if (d == NULL || dbf_get_str(d, f, out, sz) != DBF_OK)
        out[0] = '\0';
}

int eng_rec_is(eng *e, int t, u16 f, const char *want)
{
    char v[ENG_MAXFIND + 1];

    if (want[0] == '\0')
        return 1;
    eng_rec_str(e, t, f, v, (u16)sizeof v);
    return txt_ieq(v, want);
}

int eng_filter_len(eng *e, const char *val, u16 n, const char *key)
{
    if (val != NULL && strlen(val) > n)
        return eng_fail(e, ENG_EARG, key, ENG_TX_FILTLONG);
    return ENG_OK;
}

i32 eng_rec_num(eng *e, int t, u16 f)
{
    dbf *d;
    i32 v;
    int blank;

    d = eng_dbf(e, t);
    if (d == NULL || dbf_get_num(d, f, &v, &blank) != DBF_OK)
        return 0;
    return v;
}

void eng_upper(char *s)
{
    for (; *s != '\0'; s++)
        *s = (char)TXT_UPPER(*s);
}

int eng_seek_live(eng *e, int x, const char *key, u32 *recno)
{
    const eng_xdef *xd;
    u8 want[ENG_MAXKEYLEN];
    u8 got[ENG_MAXKEYLEN];
    size_t n;
    u32 r;
    int exact;
    int rc;
    dbf *d;

    *recno = 0;
    xd = eng_index_def(x);
    if (xd == NULL || e->x[x] == NULL)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOINDEX);
    d = e->t[xd->table];
    n = strlen(key);
    if (n > xd->keylen)
        return ENG_OK;
    memcpy(want, key, n);
    memset(want + n, ' ', xd->keylen - n);
    rc = ndx_seek(e->x[x], want, xd->keylen, &r, &exact);
    if (rc != DBF_OK)
        return eng_dbferr(e, rc, 1);
    while (r != 0 && exact) {
        rc = dbf_read(d, r);
        if (rc != DBF_OK)
            return eng_dbferr(e, rc, 0);
        if (!dbf_is_deleted(d)) {
            *recno = r;
            return ENG_OK;
        }
        rc = ndx_next(e->x[x], &r);
        if (rc == DBF_OK && r != 0)
            rc = ndx_key(e->x[x], got);
        if (rc != DBF_OK)
            return eng_dbferr(e, rc, 1);
        exact = r != 0 && memcmp(got, want, xd->keylen) == 0;
    }
    return ENG_OK;
}

int eng_fieldlist(eng *e, int t, const char *spec, u16 *idx, u16 *n)
{
    const eng_tdef *td;
    const dbf_field *fd;
    char name[DBF_NAMELEN + 2];
    const char *p;
    u16 width;
    u16 len;
    u16 i;
    int f;

    *n = 0;
    if (table_of(e, t) == NULL)
        return ENG_EINTERNAL;
    td = eng_table_def(t);
    if (spec == NULL || spec[0] == '\0') {
        for (i = 0; i < td->nfields && *n < ENG_MAXFLIST; i++)
            if (eng_field_flags(t, i) & ENG_F_LIST)
                idx[(*n)++] = i;
        return ENG_OK;
    }
    if (strlen(spec) > ENG_MAXFSPEC)
        return eng_fail(e, ENG_EARG, k_fields, ENG_TX_FLISTLONG);
    width = 0;
    p = spec;
    for (;;) {
        len = 0;
        while (*p != '\0' && *p != ',') {
            if (len < DBF_NAMELEN + 1u)
                name[len++] = *p;
            p++;
        }
        name[len] = '\0';
        if (len == 0)
            return eng_fail(e, ENG_EARG, k_fields, ENG_TX_FLDEMPTY);
        f = eng_field_num(e, t, name);
        fd = f >= 0 ? eng_field(e, t, (u16)f) : NULL;
        if (fd != NULL && fd->type == 'M')
            return eng_fail(e, ENG_EARG, name, ENG_TX_MEMOLIST);
        if (fd == NULL || (eng_field_flags(t, (u16)f) & ENG_F_HIDDEN))
            return eng_fail(e, ENG_EARG, name, ENG_TX_NOFIELD);
        if (*n >= ENG_MAXFLIST)
            return eng_fail(e, ENG_EARG, k_fields, ENG_TX_MANYFLD);
        width = (u16)(width + fd->len);
        if (width > ENG_MAXRWIDTH)
            return eng_fail(e, ENG_EARG, k_fields, ENG_TX_WIDEFLD);
        idx[(*n)++] = (u16)f;
        if (*p == '\0')
            break;
        p++;
    }
    return ENG_OK;
}

int eng_memo_read(eng *e, int t, u16 f, u16 *len)
{
    const dbf_field *fd;
    u32 block;
    dbf *d;
    int rc;

    *len = 0;
    e->memo[0] = '\0';
    d = table_of(e, t);
    if (d == NULL)
        return ENG_EINTERNAL;
    fd = dbf_field_at(d, f);
    if (fd == NULL || fd->type != 'M' || e->m[t] == NULL)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOTMEMO);
    rc = dbf_get_memo(d, f, &block);
    if (rc != DBF_OK)
        return eng_dbferr(e, rc, 0);
    if (block == 0)
        return ENG_OK;
    rc = dbt_read(e->m[t], block, e->memo, ENG_MAXMEMO + 1, len);
    if (rc == DBF_ERANGE && *len == ENG_MAXMEMO)
        return eng_fail(e, ENG_ECONSTR, fd->name, ENG_TX_MEMOLONG);
    return eng_dbferr(e, rc, 0);
}
