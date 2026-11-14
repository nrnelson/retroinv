/*
 * DBF.H - dBASE III tables (.DBF) and memo files (.DBT).
 *
 * The files written here are byte-compatible with dBASE III Plus 1.1:
 * a 32-byte table header, one 32-byte descriptor per field, a 0x0D
 * terminator, fixed-length records whose first byte is ' ' (live) or '*'
 * (deleted), and a 0x1A end-of-file byte.  The header version byte is
 * 0x03, or 0x83 when the table has a memo (M) field.  Memo text lives in
 * a .DBT file of DBT_BLOCK-byte blocks; block 0 is the memo header.
 *
 * All file access goes through the cf_ functions of COMPAT.H.  Every
 * function returning int returns DBF_OK or a negative DBF_E code.  The
 * dbf and dbt handles are passed without const: Watcom C 6.5 rejects a
 * definition whose const-qualified pointer parameter refers to a struct
 * that was incomplete at the prototype.
 */
#ifndef DBF_H
#define DBF_H

#include "COMPAT.H"

/* Limits of dBASE III Plus. */
#define DBF_MAXFIELDS   128     /* fields per table                     */
#define DBF_NAMELEN     10      /* characters in a field name           */
#define DBF_MAXRECLEN   4000    /* record bytes, delete flag included   */
#define DBF_MAXCHAR     254     /* width of a C field                   */
#define DBF_MAXNUM      19      /* width of an N field                  */

/* Return codes. */
#define DBF_OK          0
#define DBF_EIO         (-1)    /* read, write or seek failed           */
#define DBF_ENOENT      (-2)    /* file or path not found               */
#define DBF_EINUSE      (-3)    /* access denied or sharing violation   */
#define DBF_EFORMAT     (-4)    /* not a dBASE III file or inconsistent */
#define DBF_ENOMEM      (-5)    /* out of memory                        */
#define DBF_ERANGE      (-6)    /* record or field number out of range,
                                   or a value that does not fit         */
#define DBF_EARG        (-7)    /* invalid argument or malformed value  */
#define DBF_EFULL       (-8)    /* disk full                            */
#define DBF_EHANDLES    (-9)    /* no free file handles (FILES=)        */
#define DBF_ERDONLY     (-10)   /* write to a file opened read-only     */

/* The DBF_E code of a cf_ result: 0 is DBF_OK; CF_ENOENT and CF_ENOPATH
   are DBF_ENOENT; CF_EACCES, CF_ESHARE and CF_ELOCK are DBF_EINUSE;
   CF_EMFILE is DBF_EHANDLES, CF_EFULL DBF_EFULL, CF_ENOMEM DBF_ENOMEM,
   and any other code DBF_EIO. */
int dbf_map_cf(int cfrc);

/* Open modes. */
#define DBF_RDONLY      0       /* read only, others may read           */
#define DBF_RDWR        1       /* read and write, others denied        */

/* ---------------------------------------------------------------------
 * Field definitions.
 *
 * type is 'C' (character, 1..DBF_MAXCHAR), 'N' (numeric, 1..DBF_MAXNUM,
 * dec < len - 1 when dec > 0), 'D' (date, DBF_DATELEN, YYYYMMDD), 'L'
 * (logical, 1) or 'M' (memo, DBF_MEMOLEN: block number into the .DBT).
 * name is upper-case, 1..DBF_NAMELEN characters of A-Z, 0-9 and '_',
 * starting with a letter.  offset is the field's byte position in the
 * record (the delete flag is byte 0, so the first field has offset 1);
 * dbf_create and dbf_open fill it in.
 * --------------------------------------------------------------------- */
#define DBF_DATELEN     8       /* width of a D field                   */
#define DBF_MEMOLEN     10      /* width of an M field                  */

typedef struct {
    char name[DBF_NAMELEN + 1];
    char type;
    u8   len;
    u8   dec;
    u16  offset;
} dbf_field;

typedef struct dbf dbf;         /* an open table (opaque)               */
typedef struct dbt dbt;         /* an open memo file (opaque)           */

/* Override the date written to table headers; year 1980..2079, the
   years a two-digit header year can name.  Until it is called, the date
   comes from dos_today().  Used by tests to make files reproducible. */
void dbf_set_today(u16 year, u8 month, u8 day);

/* ---------------------------------------------------------------------
 * Tables.
 * --------------------------------------------------------------------- */

/* Create an empty table with the given fields (validated; offsets are
   filled in).  An existing file is replaced.  The memo file is not
   created; see dbt_create. */
int dbf_create(const char *path, dbf_field *fields, u16 nfields);

/* Open a table.  DBF_RDWR opens deny-read/write, so a second writer gets
   DBF_EINUSE when SHARE is loaded; DBF_RDONLY opens deny-write.  On
   failure *out is NULL. */
int dbf_open(dbf **out, const char *path, int mode);

/* Write the header if the table changed, commit the file and close it.
   The table is freed even when an error is returned. */
int dbf_close(dbf *t);

/* Close the table without writing anything: the file keeps the header
   the last dbf_flush wrote, so records appended since stay uncounted.
   The table is freed even when an error is returned. */
int dbf_drop(dbf *t);

/* Rewrite the header (record count, last-update date), make sure the
   0x1A end-of-file byte follows the last record, and commit the file's
   directory entry, so the file is consistent if the machine stops.  A
   table with nothing to write and no size change since the last commit
   is left alone. */
int dbf_flush(dbf *t);

/* On a table opened DBF_RDWR, cut the file after the last counted record
   when more than one byte follows it (the records of an append that
   stopped before the header counted them), and make sure the 0x1A
   end-of-file byte follows the last record.  The new size reaches the
   directory at the next dbf_flush or dbf_close.  DBF_ERDONLY on a
   read-only table. */
int dbf_trim(dbf *t);

u32 dbf_reccount(dbf *t);

/* Header values: the version byte (0x03, or 0x83 with memo), the header
   length in bytes, and the last-update date as stored in the file. */
u8  dbf_version(dbf *t);
u16 dbf_hdrlen(dbf *t);
void dbf_lastupdate(dbf *t, u16 *year, u8 *month, u8 *day);
u16 dbf_reclen(dbf *t);
u16 dbf_nfields(dbf *t);
int dbf_has_memo(dbf *t);

/* Field i (0-based), or NULL when i is out of range. */
const dbf_field *dbf_field_at(dbf *t, u16 i);

/* Index of the named field (case-insensitive), or -1. */
int dbf_field_index(dbf *t, const char *name);

/* ---------------------------------------------------------------------
 * The record buffer.
 *
 * Each table has one record buffer of dbf_reclen() bytes.  dbf_read
 * loads a record into it, the field functions read and change it, and
 * dbf_write or dbf_append store it.  Record numbers start at 1.
 * --------------------------------------------------------------------- */

/* Fill the buffer with spaces (a live, empty record); dbf_recno becomes 0. */
void dbf_blank(dbf *t);

/* Load record recno into the buffer; dbf_recno() becomes recno.
   DBF_ERANGE, with nothing changed, outside 1..dbf_reccount(); after
   any other failure the buffer holds undefined bytes and dbf_recno() is
   0. */
int dbf_read(dbf *t, u32 recno);

/* Store the buffer over record dbf_recno() (DBF_ERANGE if it is 0). */
int dbf_write(dbf *t);

/* Store the buffer as a new last record; dbf_recno() becomes its number.
   The header is rewritten at the next dbf_flush or dbf_close. */
int dbf_append(dbf *t);

u32 dbf_recno(dbf *t);
int dbf_is_deleted(dbf *t);
void dbf_set_deleted(dbf *t, int deleted);

/* Direct access to the buffer, for code that copies whole records. */
u8 *dbf_recbuf(dbf *t);

/* ---------------------------------------------------------------------
 * Field values in the record buffer (f is the 0-based field index).
 * --------------------------------------------------------------------- */

/* The field's text with trailing spaces removed (N fields: also leading
   spaces).  If it does not fit in outsz bytes including the NUL, the
   truncated text is stored and DBF_ERANGE returned. */
int dbf_get_str(dbf *t, u16 f, char *out, u16 outsz);

/* Store text.  C: left-justified and space-padded; DBF_ERANGE (field
   unchanged) if longer than the field.  D: "YYYYMMDD" with a valid date,
   or "" for a blank date.  L: T or Y stores T, F or N stores F (either
   case), "" stores a blank; anything else, '?' included, is DBF_EARG.
   N and M fields are refused with DBF_EARG; use dbf_put_num and
   dbf_put_memo. */
int dbf_put_str(dbf *t, u16 f, const char *s);

/* Numeric value scaled by 10^dec, e.g. "  12.50" in an N(7,2) field is
   1250.  A blank field gives *scaled = 0 and *blank = 1.  DBF_EARG if
   the field is not an N field or its text is not a number; DBF_ERANGE
   if the value exceeds the i32 range or the field holds dBASE's '*'
   overflow text. */
int dbf_get_num(dbf *t, u16 f, i32 *scaled, int *blank);

/* Store scaled / 10^dec right-justified with dec decimals; DBF_ERANGE
   (field unchanged) if it does not fit the width. */
int dbf_put_num(dbf *t, u16 f, i32 scaled);

/* Store spaces in any field. */
int dbf_put_blank(dbf *t, u16 f);

/* Memo block number of an M field; 0 means no memo. */
int dbf_get_memo(dbf *t, u16 f, u32 *block);
int dbf_put_memo(dbf *t, u16 f, u32 block);

/* ---------------------------------------------------------------------
 * Memo files.
 *
 * dBASE III memos are append-only: changing a memo writes the new text
 * to fresh blocks at the end and the record is pointed at them; the old
 * blocks stay until PACK.  Text ends with 0x1A 0x1A on disk and must not
 * itself contain 0x1A.
 * --------------------------------------------------------------------- */
#define DBT_BLOCK       512
#define DBT_MAXMEMO     65534u  /* longest memo text: a u16 buffer size
                                   including the terminating NUL       */

/* Create an empty memo file (header block only); replaces an existing one. */
int dbt_create(const char *path);

/* Open with the same modes and sharing as dbf_open.  On failure *out
   is NULL. */
int dbt_open(dbt **out, const char *path, int mode);

/* Write the header with dbt_flush if the next free block changed, and
   close the file.  The handle is freed even when an error is returned. */
int dbt_close(dbt *m);

/* Rewrite the header (next free block) and commit the file when an
   append or dbt_open changed it; a clean file is left alone.
   DBF_ERDONLY on a file opened DBF_RDONLY. */
int dbt_flush(dbt *m);

/* Read the memo starting at block into buf as a NUL-terminated string;
   *len receives the number of characters stored.  If the memo does not
   fit, the truncated text is stored and DBF_ERANGE returned.  DBF_ERANGE
   also for block 0 or a block past the end; DBF_EFORMAT when the file
   ends before the memo's terminator; DBF_EARG when bufsz is 0. */
int dbt_read(dbt *m, u32 block, char *buf, u16 bufsz, u16 *len);

/* Append len bytes of text (at most DBT_MAXMEMO) as a new memo; *block
   receives its first block number.  DBF_EARG if the text contains 0x1A,
   DBF_ERANGE if it is too long, DBF_ERDONLY on a file opened DBF_RDONLY,
   DBF_EFULL when the block numbers are exhausted.  The header's
   next-free block number is written by dbt_flush and dbt_close, so call
   dbt_flush before storing the block number in a record.  dbt_open
   never hands out blocks that already hold data, even when the header
   lags behind them. */
int dbt_append(dbt *m, const char *text, u16 len, u32 *block);

/* ---------------------------------------------------------------------
 * Index files (.NDX).
 *
 * The dBASE III index layout: a 512-byte header (root page, page count,
 * key length, keys per page, key type, entry size, key expression) and
 * 512-byte pages, each a count followed by entries of {lower page,
 * record number, key}.  Keys are character strings of exactly the
 * index's key length, compared byte by byte; numeric (type 1) indexes
 * are refused by ndx_open with DBF_EFORMAT.  Equal keys are kept in
 * record-number order.  Each open index has one current position, moved
 * by the seek and step functions.  A newly opened index is positioned
 * before the first entry: ndx_next from there gives the first entry, and
 * ndx_prev from past the last entry gives the last one.  After ndx_insert
 * the position is on the new entry; after ndx_delete it is on the entry
 * that followed the removed one (or past the end).  Deleted records keep
 * their keys; callers skip them.
 * --------------------------------------------------------------------- */
#define NDX_PAGE        512
#define NDX_MAXKEY      100     /* longest key the format allows        */
#define NDX_MAXEXPR     219     /* longest stored key expression        */

typedef struct ndx ndx;         /* an open index (opaque)               */

/* Create an empty index for character keys of keylen bytes (1 ..
   NDX_MAXKEY); expr is the key expression stored in the header as
   written (dBASE re-evaluates it on REINDEX).  Replaces an existing file. */
int ndx_create(const char *path, const char *expr, u16 keylen);

/* Open with the same modes and sharing as dbf_open.  On failure *out
   is NULL. */
int ndx_open(ndx **out, const char *path, int mode);
int ndx_close(ndx *x);

/* Write the header and any changed pages, and commit the file; an index
   with nothing written since its last commit is left alone. */
int ndx_flush(ndx *x);

u16 ndx_keylen(ndx *x);

/* The stored key expression as a NUL-terminated string, trailing
   blanks removed. */
const char *ndx_expr(ndx *x);

/* Add or remove the entry (key, recno); key points at exactly keylen
   bytes.  Inserting an entry whose key and record number are both
   already present, or record number 0, gives DBF_EARG; removing an entry
   that is not present gives DBF_ERANGE. */
int ndx_insert(ndx *x, const u8 *key, u32 recno);
int ndx_delete(ndx *x, const u8 *key, u32 recno);

/* Position on the first entry whose key is >= the len-byte prefix key
   (len <= keylen; a shorter prefix matches any key starting with it).
   *recno receives its record number, or 0 when no such entry exists;
   *exact is 1 when that entry's key starts with the prefix. */
int ndx_seek(ndx *x, const u8 *key, u16 len, u32 *recno, int *exact);

/* Position on the first entry above (key, rec) in key and record-number
   order, whether or not that entry exists; key points at exactly keylen
   bytes.  *recno receives its record number, or 0 when no such entry
   exists. */
int ndx_seek_after(ndx *x, const u8 *key, u32 rec, u32 *recno);

/* Move to the first or last entry, or step from the current one; *recno
   receives the record number, or 0 at either end (the position then
   stays at that end). */
int ndx_first(ndx *x, u32 *recno);
int ndx_last(ndx *x, u32 *recno);
int ndx_next(ndx *x, u32 *recno);
int ndx_prev(ndx *x, u32 *recno);

/* Copy the current entry's key (keylen bytes) to out; DBF_ERANGE when
   there is no current entry. */
int ndx_key(ndx *x, u8 *out);

/* ---------------------------------------------------------------------
 * Index key expressions (KEYEXPR.C).
 *
 * The subset of the dBASE III Plus expression language used by index
 * keys: field names, character constants delimited by "", '' or [],
 * numeric constants, string "+", numeric "+" and "-" (both also unary),
 * STR(n,w[,d]), UPPER(c), RIGHT(c,n), SUBSTR(c,s[,n]), YEAR(d), MONTH(d)
 * and DAY(d).  Function names may be abbreviated to four letters as in
 * dBASE and are case-insensitive.  Results are byte-identical to what
 * dBASE III Plus computes for the same record, so an index built here
 * matches one built by dBASE's INDEX ON.  Arithmetic follows dBASE's:
 * every result is rounded to a 64-bit significand and then to a double,
 * emulated in integers.
 *
 * The expression must have a character result, and only expressions
 * whose key length dBASE derives the same way are accepted, so that the
 * key length is the one dBASE's INDEX ON stores (it pads the keys of
 * other expressions with leftover memory): STR needs a width, counts are
 * integer constants that keep RIGHT and SUBSTR inside their string
 * (SUBSTR without a length only from position 1), and a numeric constant
 * has at most KX_MAXDIG (KEYEXPR.C) significant digits.
 * --------------------------------------------------------------------- */
typedef struct kx kx;           /* a compiled key expression (opaque)   */

/* Compile expr against the fields of table t.  DBF_EARG for a syntax
   error, an unknown field or function, a type mismatch or a numeric
   result; DBF_ERANGE if the key would exceed NDX_MAXKEY bytes.  On
   failure *out is NULL. */
int kx_compile(kx **out, dbf *t, const char *expr);

/* Length in bytes of every key the expression produces. */
u16 kx_len(kx *k);

/* Evaluate on t's record buffer into out (kx_len bytes, no NUL).  Any
   text of an N field gives a value, as in dBASE (KEYEXPR.C), so every
   record has a key.  DBF_EARG if t's record length differs from that
   of the table the expression was compiled against. */
int kx_eval(kx *k, dbf *t, u8 *out);

void kx_free(kx *k);

/* ---------------------------------------------------------------------
 * PACK and REINDEX (PACK.C).
 *
 * dbf_pack removes the records marked deleted from a table and, when the
 * table has memos, rewrites its .DBT so it holds only the memos of the
 * remaining records (with their block numbers updated).  The new files
 * are written beside the old ones and committed before the old ones are
 * replaced, so a machine that stops at any point leaves either the old
 * or the new files intact.  Between the renames the files in place may
 * be a mixture (a new .DBF beside the old .DBT); dbf_pack_recover
 * finishes or rolls back an interrupted pack, so it must run before the
 * table is opened again after a stop (dbf_pack runs it first itself).
 * The table must not be open elsewhere.  Packing renumbers the records,
 * so every index of the table must be rebuilt afterwards with
 * ndx_reindex.  A pack needs free disk space for a complete new copy of
 * the table and of its memo file.
 *
 * ndx_reindex rebuilds an index file from scratch over every record of
 * an open table (deleted ones included, as dBASE does), using the key
 * expression stored in the index, laid out page for page as dBASE's
 * INDEX ON writes it.  It reads the records through the table's record
 * buffer, which is left holding the last record read.  The new index
 * replaces the old one only when it is complete; ndx_reindex_recover
 * finishes or undoes an interrupted replacement and removes a left-over
 * scratch file, and ndx_reindex runs it first itself.  A reindex needs
 * free disk space of about three times the index's size (a scratch
 * index used to sort the keys, and the new file).
 * --------------------------------------------------------------------- */
typedef struct {
    u32 kept;                   /* records remaining                    */
    u32 removed;                /* deleted records dropped              */
    u32 memo_blocks_before;     /* .DBT size in blocks, 0 without memo  */
    u32 memo_blocks_after;
} dbf_packinfo;

int dbf_pack(const char *path, dbf_packinfo *info);

/* Complete or undo a pack of path that was interrupted; DBF_OK when
   there was nothing to do.  DBF_EFORMAT, with every file left as it is,
   when the files present are not a state that an interrupted pack
   leaves. */
int dbf_pack_recover(const char *path);

/* Rebuild the index ndxpath from the open table t, as described above;
   *nkeys, when nkeys is not NULL, receives the number of keys the new
   index holds (0 on failure). */
int ndx_reindex(dbf *t, const char *ndxpath, u32 *nkeys);

/* Complete or undo an interrupted replacement of the index ndxpath, as
   dbf_pack_recover does for a table. */
int ndx_reindex_recover(const char *ndxpath);

#endif
