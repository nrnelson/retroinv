/*
 * CSV.C - export of a table to a text file of comma-separated values,
 * and import of such a file into ITEMS or LOCS (ENGCSV.H).
 *
 * The file format is that of dBASE III Plus COPY TO file DELIMITED,
 * whose bytes were taken from dBASE itself: a C value in double quotes
 * with its trailing spaces removed (leading spaces kept), an N value
 * without its leading spaces, a D value as YYYYMMDD; a blank N or D
 * value is written as nothing at all, an empty C value as "".  Each
 * line ends in CR LF, and the file ends with one Ctrl-Z (1Ah) after the
 * last CR LF.  Unlike dBASE, a '"' inside a C value is doubled.
 *
 * Lines are built and read in eng_memobuf.  No export or import path
 * touches a memo: memo fields are never written or read, and the edits
 * of ITEM ADD and LOC ADD without a memo value leave the buffer alone.
 *
 * Export.  One request examines at most ENG_EXPROWS records in record
 * order, through a record-order scan whose read count starts at
 * ENG_MAXREADS - ENG_EXPROWS, so the scan's own limit stops it; the
 * cursor is the scan's.  The lines go to a scratch file (the target's
 * name with the extension ENG_CSVSCRATCH and the hex digit of the
 * export's tag, which every request gives), created by the first
 * request and appended to by the others, one cf_write per line; the
 * request that finds the table exhausted writes the Ctrl-Z, removes an
 * existing target when replacing is asked for, and renames the scratch
 * file to the target.  Exports with different tags (the sessions of a
 * server) never share a scratch file, and a continuation with another
 * tag finds none.  A continuation or last request that fails cuts the
 * scratch file back to the size it had, so a retry with the same cursor
 * writes the same bytes.  A stop leaves the scratch file, which the
 * next export with its tag and base name replaces, and never a partial
 * target.
 *
 * Import.  The file is read through a window in eng_memobuf: a line whose
 * end is not within ENG_CSVLINE bytes is refused as too long and skipped,
 * and one holding a byte below 20h other than TAB as a bad character (a
 * NUL would end its text early).  The first pass checks every line with
 * the checks of ADD (eng_item_check, eng_loc_check); it writes nothing.  A
 * location may have its parent on another line of the file, before or
 * after it, named by its code or, in a file with a LOC_ID column (one
 * without a header always has one), by the LOC_ID the file gives it: there
 * a number names only such a line, so an export imports back with its tree
 * into any data.  Two lines may also clash with each other but with no
 * live location.  So for LOCS a scratch index (IMPORT.&NX in the data
 * directory, removed after the import and before it, in case a stop left
 * one) keeps what the lines will add: a labelling pass first gathers every
 * line's LOC_ID and code; then sweeps over the file take each line whose
 * parent is a live location, the top level or a line already taken, check
 * it with its depth and keep its name under its parent and what became of
 * it, so that a chain of lines in any order is taken one level per sweep
 * at worst; the lines no sweep took have parents in a loop, or below a
 * refused line, or too deep.  The second pass adds the lines sweep by
 * sweep, parents before children, each sweep in file order as the checks
 * took them, so the first pass refuses exactly the lines the second would;
 * it keeps in the index the LOC_ID each numbered line was added as, for
 * the lines that name it.  A refusal is a code below 300; anything from
 * 300 up stops the import.  The second pass adds the lines inside one
 * batch, so the tables' headers are rewritten only at its end: a stop
 * before that leaves the batch's marker, and the next open cuts the
 * records the headers do not count and flags any of their audit rows on
 * the disk.
 */
#include <stddef.h>
#include <string.h>
#define COMPAT_FIND
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGITEM.H"
#include "ENGCODE.H"
#include "ENGLOCS.H"
#include "ENGCSV.H"

#define F_LID       ENG_FIELD(LOCS, LOC_ID)
#define F_LPARENT   ENG_FIELD(LOCS, PARENT_ID)
#define F_LNAME     ENG_FIELD(LOCS, LOC_NAME)
#define F_LCODE     ENG_FIELD(LOCS, LOC_CODE)

/* A line holds every non-memo field of a record (at most ENG_MAXFLIST
   of them, checked at run time) as text: each C byte at most doubled,
   two quotes and a comma per field, then CR LF; the field-name line
   holds each name and a comma, then CR LF. */
extern char csv_line_fits[1];
extern char csv_line_fits[(ENG_CSVLINE >= 2 * ENG_MAXREC +
                           3 * ENG_MAXFLIST + 2) ? 1 : 2];
extern char csv_head_fits[1];
extern char csv_head_fits[(ENG_CSVLINE >= (DBF_NAMELEN + 1) * ENG_MAXFLIST +
                           2) ? 1 : 2];

/* An export request stops through the scan's read limit. */
extern char csv_rows_fit[1];
extern char csv_rows_fit[(ENG_EXPROWS >= 1 && ENG_EXPROWS <= ENG_MAXREADS) ?
                         1 : 2];

/* The exportable tables are 0 .. ENG_T_AUDIT, the importable ones 0 ..
   ENG_T_LOCS; USERS and SEQ come after them. */
extern char csv_tables[1];
extern char csv_tables[(ENG_T_ITEMS == 0 && ENG_T_LOCS == 1 &&
                        ENG_T_AUDIT > ENG_T_LOCS &&
                        ENG_T_USERS > ENG_T_AUDIT &&
                        ENG_T_SEQ > ENG_T_AUDIT) ? 1 : 2];

#define CSV_EOF     0x1A        /* Ctrl-Z: end of the file              */
#define CSV_MAXERR  0xFFFFU     /* cap of refused-line count *errors    */

/* The keys of the scratch index of a LOCS import, each record number
   a line number unless said otherwise.  Of the labelling pass: DUP_ID,
   a line's LOC_ID in the file (ENG_IDLEN, as in LOCS1); DUP_CODE, its
   code (ENG_LOCLEN).  Of the sweeps, after DUP_UP or DUP_STATE a line
   number in hex (ENG_IDLEN digits): DUP_UP, a line whose parent line
   was not decided in the first sweep, that parent; DUP_STATE, a decided
   line, its record ST_REFUSED or the sweep that accepted it times 16
   plus its depth (ST_DEPTH); DUP_LIVE, the LOC_ID of a live parent and
   a name, upper-cased (ENG_LOCNAMELEN), and DUP_LINE, the number of a
   parent line in hex and a name.  Of the adding pass: DUP_NEWID, a
   line's LOC_ID in the file, its record the LOC_ID it was added as. */
#define DUP_CODE    'C'
#define DUP_ID      'I'
#define DUP_UP      'P'
#define DUP_STATE   'S'
#define DUP_LIVE    'N'
#define DUP_LINE    'F'
#define DUP_NEWID   'M'
#define DUP_KEYLEN  (1 + ENG_IDLEN + ENG_LOCNAMELEN)
#define ST_DEPTH    15          /* the depth bits of a DUP_STATE record */
#define ST_REFUSED  15          /* a refused line: no sweep, no depth   */

/* The phases of an import: for LOCS the labelling pass, then sweeps of
   checks, then the adds sweep by sweep; for ITEMS one pass of checks
   and one of adds.  CSV_WAIT: a LOCS line waits for its parent line. */
#define PH_LABEL    0
#define PH_CHECK    1
#define PH_ADD      2
#define CSV_WAIT    (-1)

/* The size of a scratch name: the path, '.', ENG_CSVSCRATCH, the tag's
   digit and NUL. */
#define CSV_SCRMAX  (ENG_CSVPATH + 1 + sizeof ENG_CSVSCRATCH + 1)

static const char k_file[] = "FILE";
static const char k_line[] = "LINE";
static const char k_table[] = "TABLE";
static const char k_import[] = "IMPORT";
static const char k_dupix[] = "IMPORT.&NX";
static const char k_scr[] = ENG_CSVSCRATCH;

/* Record the cf_ code rc of a file call (eng_cferr), naming FILE; a
   file or directory not found is ENG_ENOTFOUND. */
static int file_fail(eng *e, int rc)
{
    int code;

    code = eng_cferr(e, rc, ENG_TX_NONE);
    if (rc == CF_ENOENT || rc == CF_ENOPATH)
        code = ENG_ENOTFOUND;
    return eng_fail(e, code, k_file, eng_errtx(e));
}

/* 1 when the field layout of table t fits a line: at most ENG_MAXFLIST
   non-memo fields and a record of at most ENG_MAXREC bytes. */
static int layout_ok(eng *e, int t)
{
    dbf *d;
    u16 nf;
    u16 f;
    u16 n;

    d = eng_dbf(e, t);
    nf = dbf_nfields(d);
    n = 0;
    for (f = 0; f < nf; f++)
        if (eng_field(e, t, f)->type != 'M')
            n++;
    return n <= ENG_MAXFLIST && dbf_reclen(d) <= ENG_MAXREC;
}

int eng_csv_table(eng *e, const char *name, int import, int *t)
{
    int last;
    int i;

    *t = -1;
    last = import ? ENG_T_LOCS : ENG_T_AUDIT;
    if (name != NULL)
        for (i = 0; i <= last; i++)
            if (txt_ieq(name, eng_table_def(i)->name)) {
                *t = i;
                return ENG_OK;
            }
    return eng_fail(e, ENG_EARG, k_table, ENG_TX_NOCSV);
}

int eng_csv_path(eng *e, const char *path)
{
    if (!txt_outfile_ok(path, ENG_CSVPATH))
        return eng_fail(e, ENG_EARG, k_file, ENG_TX_FNAME);
    return ENG_OK;
}

/* ---------------------------------------------------------------------
 * Export.
 * --------------------------------------------------------------------- */

/* 1 when path names something that exists (or is held by another
   program), 0 when the file or its directory is not found. */
static int exists(const char *path)
{
    int h;
    int rc;

    rc = cf_open(path, CF_READ | CF_DENYNONE, &h);
    if (rc == 0) {
        cf_close(h);
        return 1;
    }
    return rc != CF_ENOENT && rc != CF_ENOPATH;
}

/* Write the len bytes of line b followed by CR LF (b has room). */
static int put_line(int h, char *b, u16 len)
{
    b[len] = '\r';
    b[len + 1] = '\n';
    return cf_write(h, b, (u16)(len + 2));
}

/* The field names of table t (memo fields left out), comma-separated,
   into b; returns the length. */
static u16 head_line(eng *e, int t, char *b)
{
    const dbf_field *fd;
    u16 nf;
    u16 f;
    u16 len;
    u16 k;

    nf = dbf_nfields(eng_dbf(e, t));
    len = 0;
    for (f = 0; f < nf; f++) {
        fd = eng_field(e, t, f);
        if (fd->type == 'M')
            continue;
        if (len > 0)
            b[len++] = ',';
        k = (u16)strlen(fd->name);
        memcpy(b + len, fd->name, (size_t)k);
        len = (u16)(len + k);
    }
    return len;
}

/* The record in table t's buffer as a line into b; returns the length.
   A byte below 20h or 7Fh is written as a space. */
static u16 rec_line(eng *e, int t, char *b)
{
    const dbf_field *fd;
    const u8 *rec;
    const u8 *v;
    u16 nf;
    u16 f;
    u16 s;
    u16 n;
    u16 len;
    int first;
    u8 c;

    rec = dbf_recbuf(eng_dbf(e, t));
    nf = dbf_nfields(eng_dbf(e, t));
    len = 0;
    first = 1;
    for (f = 0; f < nf; f++) {
        fd = eng_field(e, t, f);
        if (fd->type == 'M')
            continue;
        if (!first)
            b[len++] = ',';
        first = 0;
        v = rec + fd->offset;
        n = fd->len;
        while (n > 0 && v[n - 1] == ' ')
            n--;
        s = 0;
        if (fd->type == 'C')
            b[len++] = '"';
        else
            while (s < n && v[s] == ' ')
                s++;
        for (; s < n; s++) {
            c = v[s];
            if (c < 0x20 || c == 0x7F)
                c = ' ';
            else if (c == '"' && fd->type == 'C')
                b[len++] = '"';
            b[len++] = (char)c;
        }
        if (fd->type == 'C')
            b[len++] = '"';
    }
    return len;
}

/* After a failed continuation or last request: cut the scratch file
   back to its size before that request, so a retry with the same cursor
   writes the same bytes.  A failure here leaves the longer file. */
static void cut_back(const char *scr, u32 size)
{
    int h;

    if (cf_open(scr, CF_RDWR | CF_DENYRW, &h) == 0) {
        cf_truncate(h, size);
        cf_close(h);
    }
}

/* The last request: put the scratch file in the target's place. */
static int finish_export(eng *e, const char *path, const char *scr,
                         u8 flags)
{
    int rc;

    if (exists(path)) {
        if (!(flags & ENG_CSV_REPLACE))
            return eng_fail(e, ENG_EDUP, k_file, ENG_TX_EXISTS);
        rc = cf_remove(path);
        if (rc != 0)
            return file_fail(e, rc);
    }
    rc = cf_rename(scr, path);
    if (rc != 0)
        return file_fail(e, rc);
    return ENG_OK;
}

int eng_export(eng *e, int t, const char *path, u8 flags, u8 tag,
               const char *after, u16 *rows, char *cursor)
{
    char scr[CSV_SCRMAX];
    char ext[sizeof ENG_CSVSCRATCH + 1];
    eng_scan s;
    char *b;
    u32 r;
    u32 size;
    int more;
    int h;
    int rc;
    int frc;
    int frc2;
    int first;
    int sized;
    char eof;

    *rows = 0;
    cursor[0] = '\0';
    if (t < 0 || t > ENG_T_AUDIT)
        return eng_fail(e, ENG_EARG, k_table, ENG_TX_NOCSV);
    rc = eng_csv_path(e, path);
    if (rc != ENG_OK)
        return rc;
    if (!layout_ok(e, t) || tag >= ENG_EXPTAGS)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NONE);
    rc = eng_scan_open(e, &s, t, ENG_SCAN_RECORDS, NULL, 0, after);
    if (rc != ENG_OK)
        return rc;
    s.reads = ENG_MAXREADS - ENG_EXPROWS;
    /* The scratch name: path with its extension replaced by, or given,
       k_scr and the tag's digit; it fits, as eng_csv_path bounds path. */
    strcpy(ext, k_scr);
    ext[sizeof ext - 2] = txt_hexdig[tag];
    ext[sizeof ext - 1] = '\0';
    txt_path_ext(scr, (u16)sizeof scr, path, ext, 0);
    b = eng_memobuf(e);
    first = after == NULL || after[0] == '\0';
    size = 0;
    sized = 0;
    if (first) {
        if (!(flags & ENG_CSV_REPLACE) && exists(path))
            return eng_fail(e, ENG_EDUP, k_file, ENG_TX_EXISTS);
        frc = cf_create(scr, &h);
        if (frc != 0)
            return file_fail(e, frc);
        if (flags & ENG_CSV_HEADER)
            frc = put_line(h, b, head_line(e, t, b));
    } else {
        frc = cf_open(scr, CF_RDWR | CF_DENYRW, &h);
        if (frc == CF_ENOENT || frc == CF_ENOPATH)
            return eng_fail(e, ENG_ECURSOR, NULL, ENG_TX_NOEXPORT);
        if (frc != 0)
            return file_fail(e, frc);
        frc = cf_size(h, &size);
        sized = frc == 0;
        if (frc == 0)
            frc = cf_seek(h, size);
    }
    more = 1;
    while (frc == 0) {
        rc = eng_scan_next(e, &s, &r);
        if (rc != ENG_OK || r == 0)
            break;
        frc = put_line(h, b, rec_line(e, t, b));
        if (frc == 0)
            (*rows)++;
    }
    if (frc == 0 && rc == ENG_OK)
        rc = eng_scan_more(e, &s, &more);
    if (frc == 0 && rc == ENG_OK && !more) {
        eof = CSV_EOF;
        frc = cf_write(h, &eof, 1);
    }
    frc2 = cf_close(h);
    if (frc == 0)
        frc = frc2;
    if (rc == ENG_OK && frc != 0)
        rc = file_fail(e, frc);
    if (rc == ENG_OK && more)
        eng_scan_cursor(&s, cursor);
    else if (rc == ENG_OK)
        rc = finish_export(e, path, scr, flags);
    if (rc != ENG_OK && sized)
        cut_back(scr, size);
    return rc;
}

/* ---------------------------------------------------------------------
 * Import.
 * --------------------------------------------------------------------- */

/* The state of an import.  The file is read into the window
   b[start..fill) of eng_memobuf; a line is returned in place, ended by a
   NUL over its LF (or CR LF). */
typedef struct {
    int  h;                     /* the file                             */
    char *b;                    /* eng_memobuf                          */
    u16  start;                 /* first byte not yet returned          */
    u16  fill;                  /* bytes in the window                  */
    u8   eof;                   /* no more bytes: end of file or Ctrl-Z */
    u8   skip;                  /* discarding the rest of a long line   */
    u8   needhdr;               /* the next non-blank line names columns */
    u32  line;                  /* number of the last line returned     */
    u32  lines;                 /* lines the first pass accepted        */
    int  t;                     /* ENG_T_ITEMS or ENG_T_LOCS            */
    u16  ncol;                  /* columns a line may have              */
    u16  col[ENG_MAXFLIST];     /* the field of each column             */
    ndx  *dup;                  /* LOCS: the scratch index              */
    u8   ids;                   /* LOCS: a LOC_ID column numbers lines  */
    u8   phase;                 /* PH_                                  */
    u8   sweep;                 /* LOCS: the sweep, from 1; else 0      */
    u8   sweeps;                /* LOCS: the last sweep that accepted   */
    u8   stop;                  /* the header was refused               */
    u16  depth;                 /* LOCS: the depth of the line checked  */
    u32  waiting;               /* LOCS: lines left waiting by a sweep  */
    eng_csverr_fn err;
    void *arg;
    u16  *errors;
} csv_in;

/* The next line into *ln, or NULL at the end.  *bad is the text a bad
   line is refused with: ENG_TX_LONGLINE for one that did not fit in the
   window (its text is lost), ENG_TX_BADCHAR for one holding a byte below
   20h other than TAB, which no field takes and a NUL would cut short;
   else ENG_TX_NONE.  Returns 0 or the DOS error of a read. */
static int next_line(csv_in *in, char **ln, u16 *bad)
{
    char *b;
    u16 i;
    u16 end;
    u16 next;
    u16 got;
    char c;
    int rc;

    b = in->b;
    *ln = NULL;
    *bad = ENG_TX_NONE;
    for (;;) {
        for (i = in->start; i < in->fill; i++)
            if (b[i] == '\n' || b[i] == CSV_EOF)
                break;
        if (i < in->fill && b[i] == CSV_EOF) {
            in->eof = 1;
            in->fill = i;
        } else if (i < in->fill) {
            end = i;
            next = (u16)(i + 1);
            break;
        }
        if (in->eof) {
            if (in->start == in->fill && !in->skip)
                return 0;
            end = in->fill;
            next = in->fill;
            break;
        }
        if (in->start > 0) {
            memmove(b, b + in->start, (size_t)(in->fill - in->start));
            in->fill = (u16)(in->fill - in->start);
            in->start = 0;
        }
        if (in->fill == ENG_CSVLINE) {
            /* A full window: the line fits only when its end follows,
               so read the byte after it (got 0 at the end of the file). */
            rc = cf_read(in->h, &c, 1, &got);
            if (rc == 0 && got == 1 && c == '\r')
                rc = cf_read(in->h, &c, 1, &got);
            if (rc != 0)
                return rc;
            if (got == 0 || c == CSV_EOF)
                in->eof = 1;
            if (!in->skip && (got == 0 || c == CSV_EOF || c == '\n')) {
                end = in->fill;
                next = in->fill;
                break;
            }
            in->skip = 1;
            in->fill = 0;
            if (got == 1 && c == '\n') {
                end = 0;
                next = 0;
                break;
            }
            continue;
        }
        rc = cf_read(in->h, b + in->fill, (u16)(ENG_CSVLINE - in->fill),
                     &got);
        if (rc != 0)
            return rc;
        if (got == 0)
            in->eof = 1;
        in->fill = (u16)(in->fill + got);
    }
    in->line++;
    if (in->skip) {
        in->skip = 0;
        in->start = next;
        *bad = ENG_TX_LONGLINE;
        *ln = b + end;
        return 0;
    }
    if (end > in->start && b[end - 1] == '\r')
        end--;
    for (i = in->start; i < end; i++)
        if ((u8)b[i] < ' ' && b[i] != '\t')
            *bad = ENG_TX_BADCHAR;
    b[end] = '\0';
    *ln = b + in->start;
    in->start = next;
    return 0;
}

/* Split line s in place into at most max columns, p[i].val receiving
   column i: a bare value trimmed of spaces, or a quoted one as it is
   with "" read as ".  The failures are ENG_EARG naming LINE: too many
   columns, and bad quoting, which names the column's field instead
   when named is 1. */
static int split(eng *e, csv_in *in, char *s, eng_pair *p, u16 max,
                 u16 *nc, int named)
{
    char *o;
    char *q;
    char *t;
    char endc;
    int closed;
    u16 i;

    *nc = 0;
    for (i = 0;; i++) {
        while (*s == ' ')
            s++;
        if (i == max)
            return eng_fail(e, ENG_EARG, k_line, ENG_TX_COLUMNS);
        if (*s == '"') {
            o = ++s;
            p[i].val = o;
            closed = 0;
            while (*s != '\0') {
                if (*s == '"') {
                    s++;
                    if (*s != '"') {
                        closed = 1;
                        break;
                    }
                }
                *o++ = *s++;
            }
            while (*s == ' ')
                s++;
            endc = *s;
            if (!closed || (endc != ',' && endc != '\0'))
                return eng_fail(e, ENG_EARG,
                                named ? eng_field_name(e, in->t, in->col[i])
                                      : k_line,
                                ENG_TX_QUOTE);
            *o = '\0';
        } else {
            p[i].val = s;
            for (q = s; *q != '\0' && *q != ','; q++)
                ;
            endc = *q;
            for (t = q; t > s && t[-1] == ' '; t--)
                ;
            *t = '\0';
            s = q;
        }
        if (endc == '\0')
            break;
        s++;
    }
    *nc = (u16)(i + 1);
    return ENG_OK;
}

/* The field names of a header line: each a non-memo field of the
   table, once. */
static int header(eng *e, csv_in *in, char *ln)
{
    eng_pair p[ENG_MAXFLIST];
    const char *v;
    const char *bad;
    u16 nc;
    u16 i;
    u16 j;
    int f;
    int rc;

    rc = split(e, in, ln, p, ENG_MAXFLIST, &nc, 0);
    if (rc != ENG_OK)
        return rc;
    in->ids = 0;
    for (i = 0; i < nc; i++) {
        v = p[i].val;
        f = eng_field_num(e, in->t, v);
        if (f < 0 || eng_field(e, in->t, (u16)f)->type == 'M') {
            /* Named by its text when that is a plain field name. */
            bad = v;
            for (j = 0; v[j] != '\0'; j++)
                if (!(TXT_ISALPHA(v[j]) || TXT_ISDIGIT(v[j]) ||
                      v[j] == '_'))
                    bad = k_line;
            if (j == 0 || j > DBF_NAMELEN)
                bad = k_line;
            return eng_fail(e, ENG_EARG, bad, ENG_TX_COLUMN);
        }
        for (j = 0; j < i; j++)
            if (in->col[j] == (u16)f)
                return eng_fail(e, ENG_EARG, eng_field_name(e, in->t, (u16)f),
                                ENG_TX_REPCOL);
        in->col[i] = (u16)f;
        if (in->t == ENG_T_LOCS && f == F_LID)
            in->ids = 1;
    }
    in->ncol = nc;
    return ENG_OK;
}

/* The columns of a file without a header: the non-memo fields in file
   order. */
static void positions(eng *e, csv_in *in)
{
    u16 nf;
    u16 f;

    nf = dbf_nfields(eng_dbf(e, in->t));
    in->ncol = 0;
    for (f = 0; f < nf; f++)
        if (eng_field(e, in->t, f)->type != 'M')
            in->col[in->ncol++] = f;
    in->ids = (u8)(in->t == ENG_T_LOCS);
}

/* A key of the scratch index into key (DUP_KEYLEN bytes): kind, the
   ENG_IDLEN bytes at id unless id is NULL, then text upper-cased and
   padded with spaces. */
static void dup_key(u8 *key, char kind, const u8 *id, const char *text)
{
    u16 i;
    u16 at;
    char c;

    memset(key, ' ', DUP_KEYLEN);
    key[0] = (u8)kind;
    at = 1;
    if (id != NULL) {
        memcpy(key + 1, id, ENG_IDLEN);
        at += ENG_IDLEN;
    }
    for (i = 0; text[i] != '\0' && at < DUP_KEYLEN; i++) {
        c = text[i];
        key[at++] = (u8)TXT_UPPER(c);
    }
}

/* The ENG_IDLEN hex digits of line into out. */
static void hex_line(u8 *out, u32 line)
{
    u16 i;

    for (i = ENG_IDLEN; i > 0; i--, line >>= 4)
        out[i - 1] = (u8)txt_hexdig[(int)(line & 15)];
}

/* The scratch key of kind (DUP_ID or DUP_NEWID) for the LOC_ID in the
   file s into key: 0 when s is no LOC_ID from 1 to ENG_MAXID, which
   names no line. */
static int id_key(u8 *key, char kind, const char *s)
{
    u8 id[ENG_IDLEN];
    u32 v;

    if (!eng_loc_idtok(s, &v) || v == 0 || v > ENG_MAXID)
        return 0;
    txt_u32w((char *)id, v, ENG_IDLEN, ' ');
    dup_key(key, kind, id, "");
    return 1;
}

/* The scratch entry whose key starts with the len bytes at key: *rec
   receives its record number, 0 for none. */
static int dup_find(eng *e, csv_in *in, const u8 *key, u16 len, u32 *rec)
{
    u8 cur[DUP_KEYLEN];
    int exact;
    int rc;

    rc = ndx_seek(in->dup, key, len, rec, &exact);
    if (rc == DBF_OK && *rec != 0)
        rc = ndx_key(in->dup, cur);
    if (rc == DBF_OK && *rec != 0 && memcmp(cur, key, len) != 0)
        *rec = 0;
    return eng_dbferr(e, rc, 1);
}

/* The record of the key of kind (DUP_STATE or DUP_UP) for line into
   *rec, 0 when there is none. */
static int line_find(eng *e, csv_in *in, char kind, u32 line, u32 *rec)
{
    u8 hex[ENG_IDLEN];
    u8 key[DUP_KEYLEN];

    hex_line(hex, line);
    dup_key(key, kind, hex, "");
    return dup_find(e, in, key, 1 + ENG_IDLEN, rec);
}

/* Add the key of kind (DUP_STATE or DUP_UP) for line with record rec. */
static int line_put(eng *e, csv_in *in, char kind, u32 line, u32 rec)
{
    u8 hex[ENG_IDLEN];
    u8 key[DUP_KEYLEN];

    hex_line(hex, line);
    dup_key(key, kind, hex, "");
    return eng_dbferr(e, ndx_insert(in->dup, key, rec), 1);
}

/* ENG_EDUP naming field f of LOCS (ENG_TX_FILEDUP) when another line
   has the scratch key whose first len bytes are those at key. */
static int file_dup(eng *e, csv_in *in, const u8 *key, u16 len, u16 f)
{
    u32 rec;
    int rc;

    rc = dup_find(e, in, key, len, &rec);
    if (rc == ENG_OK && rec != 0)
        rc = eng_fail(e, ENG_EDUP, eng_field_name(e, ENG_T_LOCS, f),
                      ENG_TX_FILEDUP);
    return rc;
}

/* ENG_ECONSTR naming PARENT_ID with text tx. */
static int bad_parent(eng *e, u16 tx)
{
    return eng_fail(e, ENG_ECONSTR, eng_field_name(e, ENG_T_LOCS, F_LPARENT),
                    tx);
}

/* The labelling pass's work on LOCS line in->line, whose LOC_ID and
   LOC_CODE values are lid and code: its LOC_ID in the file (from 1 to
   ENG_MAXID, else ENG_EARG, ENG_TX_BADVALUE) and its code, when well
   formed, against those of the other lines so far; when both pass, they
   join them. */
static int loc_label(eng *e, csv_in *in, const char *lid, const char *code)
{
    char norm[ENG_LOCLEN + 1];
    u8 ikey[DUP_KEYLEN];
    u8 ckey[DUP_KEYLEN];
    int rc;

    rc = ENG_OK;
    ikey[0] = '\0';
    ckey[0] = '\0';
    if (lid[0] != '\0') {
        if (id_key(ikey, DUP_ID, lid))
            rc = file_dup(e, in, ikey, 1 + ENG_IDLEN, F_LID);
        else
            rc = eng_fail(e, ENG_EARG, eng_field_name(e, ENG_T_LOCS, F_LID),
                          ENG_TX_BADVALUE);
    }
    if (rc == ENG_OK && eng_code_norm(code, ENG_LOCLEN, 1, norm)) {
        dup_key(ckey, DUP_CODE, NULL, norm);
        rc = file_dup(e, in, ckey, 1 + ENG_LOCLEN, F_LCODE);
    }
    if (rc == ENG_OK && ikey[0] != '\0')
        rc = eng_dbferr(e, ndx_insert(in->dup, ikey, in->line), 1);
    if (rc == ENG_OK && ckey[0] != '\0')
        rc = eng_dbferr(e, ndx_insert(in->dup, ckey, in->line), 1);
    return rc;
}

/* The parent pv of a LOCS line.  In a file with a LOC_ID column a
   number names the line of that LOC_ID in the file, 0 the top level;
   other files, and a code in any file, name a live location (*id its
   LOC_ID, 0 for none) or else, as a well-formed code, the line of that
   code.  *line receives the line named, 0 for none.  ENG_ECONSTR naming
   PARENT_ID (ENG_TX_NOLOC) for a number no line has, ENG_ENOTFOUND for
   any other pv that names nothing. */
static int find_parent(eng *e, csv_in *in, const char *pv, u32 *id,
                       u32 *line)
{
    char norm[ENG_LOCLEN + 1];
    u8 key[DUP_KEYLEN];
    u32 v;
    int rc;

    *id = 0;
    *line = 0;
    if (in->ids && eng_loc_idtok(pv, &v)) {
        rc = ENG_OK;
        if (id_key(key, DUP_ID, pv))
            rc = dup_find(e, in, key, 1 + ENG_IDLEN, line);
        if (rc == ENG_OK && v != 0 && *line == 0)
            rc = bad_parent(e, ENG_TX_NOLOC);
        return rc;
    }
    rc = eng_loc_id(e, pv, id);
    if (rc == ENG_ENOTFOUND && eng_code_norm(pv, ENG_LOCLEN, 1, norm)) {
        dup_key(key, DUP_CODE, NULL, norm);
        rc = dup_find(e, in, key, 1 + ENG_LOCLEN, line);
        if (rc == ENG_OK && *line == 0)
            rc = ENG_ENOTFOUND;
    }
    return rc;
}

/* A sweep's checks of LOCS line in->line, whose pairs are p[0..n-1] and
   whose PARENT_ID and LOC_NAME values are pv and name ("" when missing).
   A parent line that is not decided yet makes the line wait (CSV_WAIT;
   in the first sweep its parent is kept for strands); one refused
   refuses it (ENG_TX_NOLOC); one accepted stands for the location that
   line adds, at its depth, and its pair is left out.  Then
   eng_loc_check, which gives in->depth, and the name under the parent
   against the lines accepted so far, which it then joins. */
static int loc_line(eng *e, csv_in *in, eng_pair *p, u16 n, const char *pv,
                    const char *name)
{
    u8 nkey[DUP_KEYLEN];
    u8 pid[ENG_IDLEN];
    char kind;
    u32 line;
    u32 st;
    u32 id;
    u16 pdepth;
    u16 i;
    int rc;

    kind = DUP_LIVE;
    pdepth = 0;
    rc = find_parent(e, in, pv, &id, &line);
    txt_u32w((char *)pid, id, ENG_IDLEN, ' ');
    if (rc == ENG_OK && line != 0) {
        rc = line_find(e, in, DUP_STATE, line, &st);
        if (rc == ENG_OK && st == 0) {
            if (in->sweep == 1)
                rc = line_put(e, in, DUP_UP, in->line, line);
            return rc == ENG_OK ? CSV_WAIT : rc;
        }
        if (rc == ENG_OK && st == ST_REFUSED)
            rc = bad_parent(e, ENG_TX_NOLOC);
        kind = DUP_LINE;
        pdepth = (u16)(st & ST_DEPTH);
        hex_line(pid, line);
        /* pv is the value of the PARENT_ID pair itself. */
        for (i = 0; i < n; i++)
            if (p[i].val == pv)
                p[i] = p[--n];
    }
    /* A parent that names nothing is eng_loc_check's to refuse. */
    if (rc == ENG_OK || rc == ENG_ENOTFOUND)
        rc = eng_loc_check(e, p, n, pdepth, &in->depth);
    if (rc == ENG_OK) {
        dup_key(nkey, kind, pid, name);
        rc = file_dup(e, in, nkey, DUP_KEYLEN, F_LNAME);
    }
    if (rc == ENG_OK)
        rc = eng_dbferr(e, ndx_insert(in->dup, nkey, in->line), 1);
    return rc;
}

/* The adding pass's work on LOCS line in->line, whose pairs are
   p[0..n-1] and whose PARENT_ID and LOC_ID values are pv and lid: a
   parent that is the LOC_ID of a line in the file becomes the LOC_ID
   that line was added as, written in idtext (TXT_U32LEN + 1 bytes), and
   the line's own LOC_ID in the file is kept with the one it is added
   as. */
static int loc_add(eng *e, csv_in *in, eng_pair *p, u16 n, const char *pv,
                   const char *lid, char *idtext)
{
    u8 key[DUP_KEYLEN];
    u32 id;
    u16 i;
    int rc;

    id = 0;
    rc = ENG_OK;
    /* Without a LOC_ID column no line has a number: no seek for one. */
    if (in->ids && id_key(key, DUP_NEWID, pv))
        rc = dup_find(e, in, key, 1 + ENG_IDLEN, &id);
    if (rc == ENG_OK && id != 0) {
        txt_u32(idtext, id);
        for (i = 0; i < n; i++)
            if (p[i].val == pv)
                p[i].val = idtext;
    }
    if (rc == ENG_OK)
        rc = eng_loc_add(e, p, n, &id);
    if (rc == ENG_OK && id_key(key, DUP_NEWID, lid))
        rc = eng_dbferr(e, ndx_insert(in->dup, key, id), 1);
    return rc;
}

/* The work of the current phase on data line ln. */
static int one_line(eng *e, csv_in *in, char *ln)
{
    eng_pair p[ENG_MAXFLIST];
    char idtext[TXT_U32LEN + 1];
    const char *pv;
    const char *lid;
    const char *code;
    const char *name;
    u32 id;
    u16 nc;
    u16 n;
    u16 i;
    u16 f;
    int rc;

    rc = split(e, in, ln, p, in->ncol, &nc, 1);
    if (rc != ENG_OK)
        return rc;
    pv = "";
    lid = "";
    code = "";
    name = "";
    n = 0;
    for (i = 0; i < nc; i++) {
        f = in->col[i];
        if (in->t == ENG_T_LOCS && f == F_LID)
            lid = p[i].val;
        if (p[i].val[0] == '\0' || (eng_field_flags(in->t, f) & ENG_F_RO))
            continue;
        p[n].key = eng_field_name(e, in->t, f);
        p[n].val = p[i].val;
        if (in->t == ENG_T_LOCS && f == F_LPARENT)
            pv = p[n].val;
        else if (in->t == ENG_T_LOCS && f == F_LCODE)
            code = p[n].val;
        else if (in->t == ENG_T_LOCS && f == F_LNAME)
            name = p[n].val;
        n++;
    }
    if (in->t == ENG_T_ITEMS)
        return in->phase == PH_ADD ? eng_item_add(e, p, n, &id)
                                   : eng_item_check(e, p, n);
    if (in->phase == PH_LABEL)
        return loc_label(e, in, lid, code);
    if (in->phase == PH_ADD)
        return loc_add(e, in, p, n, pv, lid, idtext);
    return loc_line(e, in, p, n, pv, name);
}

/* 1 for a line of nothing but spaces. */
static int blank(const char *s)
{
    while (*s == ' ')
        s++;
    return *s == '\0';
}

/* Report a refused line through the callback, counting it. */
static void refuse(eng *e, csv_in *in, int code)
{
    if (*in->errors < ENG_IMPERRS && in->err != NULL)
        in->err(in->arg, in->line, eng_errfield(e), code, eng_errmsg(e));
    if (*in->errors < CSV_MAXERR)
        (*in->errors)++;
}

/* 1 when a LOCS line of state st is not the current phase's to take:
   in a sweep one already decided, in the adding pass one of another
   sweep. */
static int not_now(const csv_in *in, u32 st)
{
    if (in->phase == PH_CHECK)
        return st != 0;
    return in->phase == PH_ADD && (st >> 4) != in->sweep;
}

/* One pass of the current phase over the file from its start: the
   first one also reads the header (in->stop when it is refused); the
   checking ones report each refused line and, for LOCS, record what
   became of it; the adding one counts the lines in *added and stops at
   the first failure. */
static int pass(eng *e, csv_in *in, u32 *added)
{
    char *ln;
    u32 st;
    u16 bad;
    int hdr;
    int rc;

    for (;;) {
        rc = next_line(in, &ln, &bad);
        if (rc != 0)
            return file_fail(e, rc);
        if (ln == NULL)
            return ENG_OK;
        if (bad == ENG_TX_NONE && blank(ln))
            continue;
        hdr = in->needhdr;
        in->needhdr = 0;
        if (hdr && (in->phase == PH_ADD || in->sweep != 0))
            continue;
        if (in->t == ENG_T_LOCS && in->phase != PH_LABEL) {
            rc = line_find(e, in, DUP_STATE, in->line, &st);
            if (rc != ENG_OK)
                return rc;
            if (not_now(in, st))
                continue;
        }
        if (bad != ENG_TX_NONE)
            rc = eng_fail(e, ENG_EARG, k_line, bad);
        else if (hdr)
            rc = header(e, in, ln);
        else
            rc = one_line(e, in, ln);
        if (rc == CSV_WAIT) {
            in->waiting++;
            continue;
        }
        if (rc == ENG_OK && in->phase == PH_ADD)
            (*added)++;
        if (rc == ENG_OK && in->phase == PH_CHECK && !hdr) {
            in->lines++;
            if (in->t == ENG_T_LOCS) {
                in->sweeps = in->sweep;
                rc = line_put(e, in, DUP_STATE, in->line,
                              ((u32)in->sweep << 4) | in->depth);
            }
        }
        if (rc == ENG_OK)
            continue;
        if (in->phase == PH_ADD || rc >= ENG_EIO)
            return rc;
        refuse(e, in, rc);
        if (hdr) {
            in->stop = 1;
            return ENG_OK;
        }
        if (in->t == ENG_T_LOCS) {
            rc = line_put(e, in, DUP_STATE, in->line, ST_REFUSED);
            if (rc != ENG_OK)
                return rc;
        }
    }
}

/* After the sweeps, refuse each LOCS line still waiting (a DUP_UP key
   without a DUP_STATE one), by what the chain of its parents in the
   file meets within ENG_MAXLOCDEPTH steps: a line met before
   (ENG_TX_CYCLE), a refused line (ENG_TX_NOLOC), else nothing that ends
   in time (ENG_TX_LOCDEEP). */
static int strands(eng *e, csv_in *in)
{
    u32 seen[ENG_MAXLOCDEPTH + 1];
    u8 hex[ENG_IDLEN];
    u8 key[DUP_KEYLEN];
    u32 line;
    u32 up;
    u32 st;
    u16 tx;
    u16 n;
    u16 i;
    int exact;
    int rc;

    line = 0;
    for (;;) {
        hex_line(hex, line + 1);
        dup_key(key, DUP_UP, hex, "");
        rc = eng_dbferr(e, ndx_seek(in->dup, key, 1 + ENG_IDLEN, &up,
                                    &exact), 1);
        if (rc == ENG_OK && up != 0)
            rc = eng_dbferr(e, ndx_key(in->dup, key), 1);
        if (rc != ENG_OK || up == 0 || key[0] != DUP_UP)
            return rc;
        for (line = 0, i = 1; i <= ENG_IDLEN; i++)
            line = (line << 4) | (u32)txt_hexval((char)key[i]);
        rc = line_find(e, in, DUP_STATE, line, &st);
        tx = ENG_TX_LOCDEEP;
        seen[0] = line;
        for (n = 1; rc == ENG_OK && st == 0 && n <= ENG_MAXLOCDEPTH; n++) {
            for (i = 0; i < n && seen[i] != up; i++)
                ;
            if (i < n) {
                tx = ENG_TX_CYCLE;
                break;
            }
            rc = line_find(e, in, DUP_STATE, up, &st);
            if (rc == ENG_OK && st != 0) {
                tx = ENG_TX_NOLOC;
                st = 0;
                break;
            }
            seen[n] = up;
            rc = line_find(e, in, DUP_UP, up, &up);
        }
        if (rc != ENG_OK)
            return rc;
        if (st != 0)
            continue;
        in->line = line;
        refuse(e, in, bad_parent(e, tx));
    }
}

/* ENG_EFULL naming FILE unless the disk of the data directory has room
   for lines new records. */
static int room(eng *e, u32 lines)
{
    const char *d;
    u32 avail;
    u32 need;
    u8 drive;
    int rc;

    d = eng_dir(e);
    drive = 0;
    if (d[0] != '\0' && d[1] == ':' && TXT_ISALPHA(d[0]))
        drive = (u8)(TXT_UPPER(d[0]) - 'A' + 1);
    rc = dos_disk_free(drive, &avail);
    if (rc != 0)
        return file_fail(e, rc);
    if (lines > (0xFFFFFFFFUL - (u32)ENG_IMPSLACK) / (u32)ENG_IMPROWCOST)
        need = 0xFFFFFFFFUL;
    else
        need = (u32)ENG_IMPSLACK + lines * (u32)ENG_IMPROWCOST;
    if (avail < need)
        return eng_fail(e, ENG_EFULL, k_file, ENG_TX_NOSPACE);
    return ENG_OK;
}

/* Rewind the file for a pass. */
static int rewind_in(csv_in *in, u8 flags)
{
    in->start = 0;
    in->fill = 0;
    in->eof = 0;
    in->skip = 0;
    in->line = 0;
    in->needhdr = (u8)((flags & ENG_CSV_HEADER) != 0);
    return cf_seek(in->h, 0);
}

/* Rewind the file and run a pass of phase ph in sweep k. */
static int run(eng *e, csv_in *in, u8 flags, u8 ph, u8 k, u32 *added)
{
    int rc;

    in->phase = ph;
    in->sweep = k;
    rc = rewind_in(in, flags);
    if (rc != 0)
        return file_fail(e, rc);
    return pass(e, in, added);
}

/* The sweeps of a LOCS import's checks: each takes the lines whose
   parent is no line of the file or a line already decided, and they go
   on while lines wait and fewer of them than before, at most
   ENG_MAXLOCDEPTH + 1 times (a chain of lines past that is too deep);
   then the lines still waiting are refused (strands). */
static int sweeps(eng *e, csv_in *in, u8 flags, u32 *added)
{
    u32 before;
    u8 k;
    int rc;

    rc = ENG_OK;
    in->waiting = 0xFFFFFFFFUL;
    for (k = 1; rc == ENG_OK && k <= ENG_MAXLOCDEPTH + 1; k++) {
        before = in->waiting;
        in->waiting = 0;
        rc = run(e, in, flags, PH_CHECK, k, added);
        if (in->waiting == 0 || in->waiting == before)
            break;
    }
    if (rc == ENG_OK && in->waiting != 0)
        rc = strands(e, in);
    return rc;
}

int eng_import(eng *e, int t, const char *path, u8 flags,
               eng_csverr_fn err, void *arg, u16 *errors, u32 *added)
{
    char ix[ENG_MAXPATH];
    eng_audrow row;
    csv_in in;
    u8 k;
    int rc;

    *errors = 0;
    *added = 0;
    if (t != ENG_T_ITEMS && t != ENG_T_LOCS)
        return eng_fail(e, ENG_EARG, k_table, ENG_TX_NOCSV);
    rc = eng_csv_path(e, path);
    if (rc != ENG_OK)
        return rc;
    if (!layout_ok(e, t))
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NONE);
    memset(&in, 0, sizeof in);
    in.b = eng_memobuf(e);
    in.t = t;
    in.err = err;
    in.arg = arg;
    in.errors = errors;
    positions(e, &in);
    rc = cf_open(path, CF_READ | CF_DENYW, &in.h);
    if (rc != 0)
        return file_fail(e, rc);
    if (t == ENG_T_LOCS) {
        strcpy(ix, eng_dir(e));
        strcat(ix, k_dupix);
        cf_remove(ix);
        rc = eng_dbferr(e, ndx_create(ix, k_import, DUP_KEYLEN), 1);
        if (rc == ENG_OK)
            rc = eng_dbferr(e, ndx_open(&in.dup, ix, DBF_RDWR), 1);
    }
    if (rc == ENG_OK)
        rc = run(e, &in, flags, t == ENG_T_LOCS ? PH_LABEL : PH_CHECK, 0,
                 added);
    if (rc == ENG_OK && t == ENG_T_LOCS && !in.stop)
        rc = sweeps(e, &in, flags, added);
    if (rc == ENG_OK && *errors == 0)
        rc = room(e, in.lines);
    if (rc == ENG_OK && *errors == 0) {
        rc = eng_batch_begin(e, t);
        if (rc == ENG_OK) {
            row.action = ENG_A_ADMIN;
            row.fld = k_import;
            row.oldval = NULL;
            row.newval = path;
            rc = eng_audit(e, t, 0, &row);
            for (k = (u8)(t == ENG_T_LOCS); rc == ENG_OK && k <= in.sweeps;
                 k++)
                rc = run(e, &in, flags, PH_ADD, k, added);
            rc = eng_batch_end(e, rc);
        }
    }
    if (in.dup != NULL)
        ndx_close(in.dup);
    if (t == ENG_T_LOCS)
        cf_remove(ix);
    cf_close(in.h);
    if (rc == ENG_OK)
        eng_clear_error(e);
    return rc;
}
