/*
 * PACK.C - PACK of dBASE III tables and REINDEX of their indexes.
 *
 * File names.  Every file made here sits beside the one it replaces and
 * has the same base name with an extension whose first character is
 * one that no file of the system uses:
 *
 *   ITEMS.DBF   the table           ITEMS.DBT   its memo file
 *   ITEMS.$DB   the packed table    ITEMS.$DT   the packed memo file
 *   ITEMS.%DB   the old table       ITEMS.%DT   the old memo file,
 *               while it is being replaced
 *   ITEMS1.NDX  an index            ITEMS1.$NX  the rebuilt index
 *   ITEMS1.%NX  the old index       ITEMS1.#NX  the scratch index
 *               while it is replaced            used to sort the keys
 *
 * The base name is the path without the extension of its last
 * component, so a table given as C:\DATA\ITEMS.DBF has its memo file in
 * C:\DATA\ITEMS.DBT, as in dBASE.  The new extensions are lower-case
 * when the given extension starts with a lower-case letter (or, without
 * one, when the name ends in one), for case-sensitive host file systems.
 *
 * Replacing a file.  The new file is written under its $ name, flushed
 * and committed (dos_commit_handle).  Then the old file is renamed to its
 * % name, the new file renamed into place, and the % file removed.  For a
 * table with memos the steps are
 *
 *   1  write and commit ITEMS.$DB and ITEMS.$DT
 *   2  rename ITEMS.DBF -> ITEMS.%DB        (the commit point)
 *   3  rename ITEMS.$DB -> ITEMS.DBF
 *   4  rename ITEMS.DBT -> ITEMS.%DT
 *   5  rename ITEMS.$DT -> ITEMS.DBT
 *   6  remove ITEMS.%DB
 *   7  remove ITEMS.%DT
 *
 * and a table without memos skips the .DBT steps.  A % file is only ever
 * made after both $ files are complete, so its presence tells which way
 * an interrupted pack goes.  For each of the table and the memo file,
 * dbf_pack_recover notes which of its three names (current, $, %) exist,
 * and checks the whole combination before it changes anything:
 *
 *   no % file           before the commit point: the old files are in
 *                       place and any $ file is partial or left over, so
 *                       it is removed (roll back).  A $ file without its
 *                       current file is refused.
 *   a % file exists     after it (roll forward).  Each file must be in
 *                       one of the states the steps above pass through
 *                       (% and $; % and current; current alone; for the
 *                       memo file before its turn, current and $), in
 *                       an order they allow, and the new table must open
 *                       and, when it has memos, have a new memo file
 *                       exactly as dbf_pack writes it (next-free block
 *                       = file size in blocks, the file ending in a
 *                       memo terminator) that every memo block number
 *                       of the table falls inside.  The remaining
 *                       renames are then made, the files in place
 *                       checked again, and only then the % files
 *                       removed, the table's first.
 *
 * Any other combination is not one an interrupted pack leaves; it is
 * reported as DBF_EFORMAT and every file is left as it is.  An error
 * opening a file that says nothing about its contents (no free handle,
 * a file in use, no memory) is returned as it is.  Recovery judges
 * files by which names exist and whether the new ones are complete;
 * complete files copied over these names by hand (an old table under a
 * $ name, say) cannot be told apart from the ones a pack leaves.  After
 * a stop, dbf_pack_recover must run before the table is opened again
 * (dbf_pack runs it first itself).
 *
 * Each rename and removal is followed by dos_disk_reset, which writes
 * DOS's modified buffers to the disk, and so is the writing of the new
 * files before the commit point.  DOS may otherwise hold a changed
 * directory sector in its buffers and write it later, or out of order
 * with the next step, and the state on the disk after a stop must be one
 * of the states above.  Every rename and removal moves from one of these
 * states to the next, so a machine that stops at any instant, also
 * during dbf_pack_recover itself, leaves a state that recovery
 * completes.  An index is replaced with the same steps for its single
 * file; ndx_reindex_recover is its recovery (it also removes a left-over
 * scratch index), and ndx_reindex runs it first.
 *
 * Disk space.  A pack needs room for a complete new copy of the table
 * and of its memo file beside the old ones.  A reindex needs about three
 * times the index's size: the scratch index, which page splits leave
 * partly empty, and the new file, while the old one is kept.
 *
 * File handles.  A pack of a table with memos holds four files open at
 * once (old and new table, old and new memo file), one without memos
 * two, and ndx_reindex two besides the caller's table (the scratch index
 * and the new file); each commit briefly takes one more handle for its
 * duplicate, so a memo pack needs five free handles and a reindex three.
 * With DOS's default FILES=8, of which five are the standard handles, a
 * memo pack fails with DBF_EHANDLES before anything is changed.
 *
 * Indexes and PACK.  Packing renumbers the records, so every index of
 * the table is wrong afterwards; dbf_pack does not touch them, and the
 * caller rebuilds each with ndx_reindex.
 *
 * Memo copy.  Each memo of a remaining record is copied byte by byte
 * from the old memo file into the new one, starting at the next free
 * block, and the record's block number is changed to it.  Text runs up
 * to the first DBT_TERM, as dBASE reads it, and is written followed by
 * two DBT_TERM bytes, with the layout of DBT.C: the gap before a memo is
 * filled with DBT_FILL bytes and the file ends right after the last
 * terminator.  Memos are streamed through a PACK_BUF-byte buffer, so no
 * memo needs to fit in memory.  Blocks that no remaining record points
 * at are not copied, which is what makes the memo file shrink.
 *
 * Reindex.  Every record, deleted ones included as in dBASE, is read in
 * record order, its key computed with the expression stored in the index
 * and inserted with ndx_insert into a scratch index (the #NX file), which
 * sorts the keys by key and record number in a few pages of memory
 * whatever the table's size.  The scratch index is then walked in order
 * and the final file written in one pass, bottom-up, exactly as dBASE III
 * Plus INDEX ON lays it out:
 *
 *   - leaf pages hold keys-per-page entries; inner pages hold up to
 *     keys-per-page keys plus the rightmost child, i.e. one child more;
 *   - a page is written, with the next page number, as soon as it is
 *     full, and its highest key goes up as an entry of its parent;
 *   - at the end the partial pages are finished from the leaves up: a
 *     partial leaf is written if it has any entry; a partial inner page
 *     with two or more children is written; one with a single child is
 *     not written, and that child goes straight to the parent level, so
 *     leaves can sit at different depths; the topmost page is the root,
 *     which is therefore the last page written (an empty table gives one
 *     empty leaf as root).
 *
 * With the SET UNIQUE ON flag set in the index, only the first record of
 * each key is kept, as dBASE does.  The stored key expression is copied
 * as it was written (dBASE keeps a trailing space).  Bytes that dBASE
 * leaves as stale memory (after the expression, after a page's entries,
 * page bytes 2..3, the rightmost slot past its page number) are zeros.
 * Memory: one page buffer per tree level (NDX_MAXDEPTH at most, two or
 * three in practice).  The scratch index allocates no cache of its own:
 * it uses the NDX page pool that all open indexes share, so its inserts
 * may write back other indexes' dirty pages.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "DBFINT.H"

#define PACK_PATHMAX    128     /* bytes for a path, NUL included       */
#define PACK_BUF        512     /* memo copy buffer                     */

/* Which names of a set exist, as bits. */
#define SET_CUR         1
#define SET_NEW         2
#define SET_BAK         4

/* memo_header writes a whole memo block from the memo copy buffer. */
extern char pack_buf_holds_block[1];
extern char pack_buf_holds_block[(PACK_BUF >= DBT_BLOCK) ? 1 : 2];

/* A page being built: a page, plus room for one more entry, as an inner
   page keeps the whole entry of its rightmost child until the page is
   written, though only the child's page number is part of the page. */
#define PACK_PGBUF      (NDX_PAGE + NDX_MAXENT)

/* The names of one file being replaced. */
typedef struct {
    char cur[PACK_PATHMAX];     /* the file in use                      */
    char nw[PACK_PATHMAX];      /* the complete new file ($)            */
    char bak[PACK_PATHMAX];     /* the old file while replaced (%)      */
} pack_set;

/* The table's names in set[0], its memo file's in set[1]. */
typedef struct {
    pack_set set[2];
} pack_names;

/* Fill s for path, whose extension is replaced (txt_path_ext) by curext
   (or kept when curext is NULL), newext and bakext.  DBF_EARG if a name
   is too long or path has an empty base name. */
static int make_set(pack_set *s, const char *path, const char *curext,
                    const char *newext, const char *bakext)
{
    if (strlen(path) >= PACK_PATHMAX)
        return DBF_EARG;
    if (curext == NULL)
        strcpy(s->cur, path);
    else if (txt_path_ext(s->cur, PACK_PATHMAX, path, curext, 0) != 0)
        return DBF_EARG;
    if (txt_path_ext(s->nw, PACK_PATHMAX, path, newext, 0) != 0 ||
        txt_path_ext(s->bak, PACK_PATHMAX, path, bakext, 0) != 0)
        return DBF_EARG;
    /* An empty base name leaves only the '.' and newext. */
    if (strlen(s->nw) == strlen(newext) + 1)
        return DBF_EARG;
    return DBF_OK;
}

/* 1 if the file exists, 0 if not, or a negative DBF_E code. */
static int exists(const char *path)
{
    int h;
    int rc;

    rc = cf_open(path, (u8)(CF_READ | CF_DENYNONE), &h);
    if (rc == CF_ENOENT)
        return 0;
    if (rc != 0)
        return dbf_map_cf(rc);
    cf_close(h);
    return 1;
}

/* Remove path if it exists, and put the directory change on the disk.
   The probe changes nothing, so a removal (a call that changes the
   disk) is made only for a file present. */
static int remove_if(const char *path)
{
    int rc;

    rc = exists(path);
    if (rc <= 0)
        return rc;
    rc = cf_remove(path);
    if (rc == 0)
        dos_disk_reset();
    return dbf_map_cf(rc);
}

/* Rename, and put the directory change on the disk. */
static int rename_step(const char *from, const char *to)
{
    int rc;

    rc = dbf_map_cf(cf_rename(from, to));
    dos_disk_reset();
    return rc;
}

/* Which names of s exist: *mask receives their SET_ bits. */
static int classify(pack_set *s, int *mask)
{
    int rc;

    *mask = 0;
    rc = exists(s->cur);
    if (rc > 0)
        *mask |= SET_CUR;
    if (rc >= 0) {
        rc = exists(s->nw);
        if (rc > 0)
            *mask |= SET_NEW;
    }
    if (rc >= 0) {
        rc = exists(s->bak);
        if (rc > 0)
            *mask |= SET_BAK;
    }
    return rc < 0 ? rc : DBF_OK;
}

/* A set while rolling forward: the $ file waits beside the % file, or
   is in place with or without the % file still there, or (for a set
   whose turn has not come) still beside the current file. */
static int forward_ok(int mask)
{
    return mask == (SET_BAK | SET_NEW) || mask == (SET_BAK | SET_CUR) ||
           mask == SET_CUR || mask == (SET_CUR | SET_NEW);
}

/* The roll-forward renames of one set whose state is mask. */
static int forward_set(pack_set *s, int mask)
{
    int rc;

    if (!(mask & SET_NEW))
        return DBF_OK;
    if (mask & SET_CUR) {
        rc = rename_step(s->cur, s->bak);
        if (rc != DBF_OK)
            return rc;
    }
    return rename_step(s->nw, s->cur);
}

/* The roll-back of a set: its $ file goes, the current file stays. */
static int rollback_ok(int mask)
{
    return mask == 0 || mask == SET_CUR || mask == (SET_CUR | SET_NEW);
}

/* An error that says nothing about a file's contents (too few
   handles, a file in use, no memory) is passed on; any other failure to
   open a file being checked makes it unusable. */
static int open_error(int rc)
{
    if (rc == DBF_EHANDLES || rc == DBF_EINUSE || rc == DBF_ENOMEM)
        return rc;
    return DBF_EFORMAT;
}

/* Check that path is a memo file exactly as dbf_pack writes it: its
   header's next-free block number is the file size in blocks rounded
   up, and the file is the header block alone (next free 1) or ends in
   the two-byte terminator of its last memo.  *next receives the
   next-free number.  DBF_OK, DBF_EFORMAT or an open_error code. */
static int memo_valid(const char *path, u32 *next)
{
    u8 b[4];
    u32 size;
    u16 got;
    int h;
    int rc;
    int ok;

    *next = 0;
    rc = dbf_cf_open(path, DBF_RDONLY, &h);
    if (rc != DBF_OK)
        return open_error(rc);
    ok = cf_read(h, b, 4, &got) == 0 && got == 4 &&
         cf_size(h, &size) == 0 && size >= DBT_BLOCK;
    if (ok) {
        *next = get_le32(b);
        ok = *next >= 1 && *next <= DBT_MAXBLOCK &&
             *next == (size + (u32)(DBT_BLOCK - 1)) / (u32)DBT_BLOCK;
    }
    if (ok && *next == 1)
        ok = size == DBT_BLOCK;
    else if (ok)
        ok = cf_seek(h, size - 2) == 0 && cf_read(h, b, 2, &got) == 0 &&
             got == 2 && b[0] == DBT_TERM && b[1] == DBT_TERM;
    cf_close(h);
    return ok ? DBF_OK : DBF_EFORMAT;
}

/* The field numbers of t's memo fields into mf (DBF_MAXFIELDS entries);
   returns how many there are. */
static u16 memo_fields(dbf *t, u8 *mf)
{
    u16 f;
    u16 n;

    n = 0;
    for (f = 0; f < dbf_nfields(t); f++)
        if (dbf_field_at(t, f)->type == 'M')
            mf[n++] = (u8)f;
    return n;
}

/* Check the new table of a roll-forward (path): it opens and, when it
   has memos, its memo file (memo, or NULL when the memo set holds no new
   file) is complete and every memo block number of its records is below
   that file's next free block.  *hasmemo receives whether it has memos.
   DBF_OK, DBF_EFORMAT or an open_error code. */
static int new_table_ok(const char *path, const char *memo, int *hasmemo)
{
    u8 mf[DBF_MAXFIELDS];
    dbf *t;
    u32 next;
    u32 r;
    u32 blk;
    u16 nm;
    u16 i;
    int rc;

    *hasmemo = 0;
    rc = dbf_open(&t, path, DBF_RDONLY);
    if (rc != DBF_OK)
        return open_error(rc);
    *hasmemo = dbf_has_memo(t);
    rc = DBF_OK;
    nm = 0;
    if (*hasmemo) {
        if (memo == NULL)
            rc = DBF_EFORMAT;
        else
            rc = memo_valid(memo, &next);
        nm = memo_fields(t, mf);
    }
    for (r = 1; nm > 0 && rc == DBF_OK && r <= dbf_reccount(t); r++) {
        rc = dbf_read(t, r);
        for (i = 0; rc == DBF_OK && i < nm; i++)
            if (dbf_get_memo(t, mf[i], &blk) != DBF_OK || blk >= next)
                rc = DBF_EFORMAT;
    }
    dbf_close(t);
    return rc;
}

/* Recovery of a table (set 0) and its memo file (set 1).  Every check
   is made before anything is changed.  fresh is 1 when dbf_pack has
   just written and committed the $ files itself: they are then checked
   only after the renames. */
static int recover_table(pack_names *pn, int fresh)
{
    pack_set *ts;
    pack_set *ms;
    const char *newmemo;
    int tm;
    int mm;
    int hasmemo;
    int rc;

    ts = &pn->set[0];
    ms = &pn->set[1];
    rc = classify(ts, &tm);
    if (rc == DBF_OK)
        rc = classify(ms, &mm);
    if (rc != DBF_OK)
        return rc;

    if (!((tm | mm) & SET_BAK)) {
        /* Before the commit point: the current files stay. */
        if (!rollback_ok(tm) || !rollback_ok(mm))
            return DBF_EFORMAT;
        rc = remove_if(ts->nw);
        if (rc == DBF_OK)
            rc = remove_if(ms->nw);
        return rc;
    }

    /* After the commit point.  The table is renamed before its memo
       file, and its % file removed first, so a memo set that has begun
       needs a table set that has. */
    if (!forward_ok(tm) || tm == (SET_CUR | SET_NEW))
        return DBF_EFORMAT;
    if ((tm & SET_NEW) && (mm & SET_BAK))
        return DBF_EFORMAT;
    if (mm != 0 && !forward_ok(mm))
        return DBF_EFORMAT;
    if (tm == SET_CUR && mm != (SET_BAK | SET_CUR))
        return DBF_EFORMAT;
    if (mm & SET_NEW)
        newmemo = ms->nw;
    else if (mm == (SET_BAK | SET_CUR))
        newmemo = ms->cur;
    else
        newmemo = NULL;
    if (fresh) {
        /* dbf_pack writes a $ memo file exactly for a table with memos. */
        hasmemo = (mm & SET_NEW) != 0;
    } else {
        rc = new_table_ok((tm & SET_NEW) ? ts->nw : ts->cur, newmemo,
                          &hasmemo);
        if (rc != DBF_OK)
            return rc;
    }
    if (!hasmemo && (mm & (SET_NEW | SET_BAK)))
        return DBF_EFORMAT;

    rc = forward_set(ts, tm);
    if (rc == DBF_OK && hasmemo)
        rc = forward_set(ms, mm);
    if (rc != DBF_OK)
        return rc;
    /* The % files go only once the files in place are complete. */
    rc = new_table_ok(ts->cur, hasmemo ? ms->cur : NULL, &hasmemo);
    if (rc != DBF_OK)
        return rc;
    rc = remove_if(ts->bak);
    if (rc == DBF_OK)
        rc = remove_if(ms->bak);
    return rc;
}

/* DBF_OK if path opens as an index, else DBF_EFORMAT or an open_error
   code. */
static int index_valid(const char *path)
{
    ndx *x;
    int rc;

    rc = ndx_open(&x, path, DBF_RDONLY);
    if (rc != DBF_OK)
        return open_error(rc);
    ndx_close(x);
    return DBF_OK;
}

/* Recovery of an index replacement (set s), and removal of the scratch
   index tmp. */
static int recover_index(pack_set *s, const char *tmp)
{
    int m;
    int rc;

    rc = classify(s, &m);
    if (rc != DBF_OK)
        return rc;
    if (!(m & SET_BAK)) {
        if (!rollback_ok(m))
            return DBF_EFORMAT;
        rc = remove_if(s->nw);
    } else {
        if (m != (SET_BAK | SET_NEW) && m != (SET_BAK | SET_CUR))
            return DBF_EFORMAT;
        rc = index_valid((m & SET_NEW) ? s->nw : s->cur);
        if (rc != DBF_OK)
            return rc;
        rc = forward_set(s, m);
        if (rc == DBF_OK)
            rc = index_valid(s->cur);
        if (rc == DBF_OK)
            rc = remove_if(s->bak);
    }
    if (rc == DBF_OK)
        rc = remove_if(tmp);
    return rc;
}

static int make_names(pack_names *pn, const char *path)
{
    int rc;

    rc = make_set(&pn->set[0], path, NULL, "$DB", "%DB");
    if (rc == DBF_OK)
        rc = make_set(&pn->set[1], path, "DBT", "$DT", "%DT");
    return rc;
}

int dbf_pack_recover(const char *path)
{
    pack_names *pn;
    int rc;

    if (path == NULL)
        return DBF_EARG;
    pn = (pack_names *)malloc(sizeof(pack_names));
    if (pn == NULL)
        return DBF_ENOMEM;
    rc = make_names(pn, path);
    if (rc == DBF_OK)
        rc = recover_table(pn, 0);
    free(pn);
    return rc;
}

/* The names of an index: s, and the scratch index in tmp; hdr is room
   for the new header page, which only ndx_reindex uses. */
typedef struct {
    pack_set s;
    char tmp[PACK_PATHMAX];
    u8 hdr[NDX_PAGE];
} nx_names;

/* Fill the names of nn for the index path; DBF_EARG as make_set. */
static int make_index_names(nx_names *nn, const char *path)
{
    int rc;

    rc = make_set(&nn->s, path, NULL, "$NX", "%NX");
    if (rc == DBF_OK &&
        txt_path_ext(nn->tmp, PACK_PATHMAX, path, "#NX", 0) != 0)
        rc = DBF_EARG;
    return rc;
}

int ndx_reindex_recover(const char *ndxpath)
{
    nx_names *nn;
    int rc;

    if (ndxpath == NULL)
        return DBF_EARG;
    nn = (nx_names *)malloc(sizeof(nx_names));
    if (nn == NULL)
        return DBF_ENOMEM;
    rc = make_index_names(nn, ndxpath);
    if (rc == DBF_OK)
        rc = recover_index(&nn->s, nn->tmp);
    free(nn);
    return rc;
}

/* ---------------------------------------------------------------------
 * PACK.
 * --------------------------------------------------------------------- */

/* The memo files of a pack: the old one read through src, the new one
   written sequentially through dst. */
typedef struct {
    int src;
    int dst;
    u32 srcblocks;              /* old file size in blocks, rounded up  */
    u32 next;                   /* next free block of the new file      */
    u32 pos;                    /* bytes written to the new file        */
    u8 *buf;                    /* PACK_BUF bytes                       */
} memo_copy;

/* Copy the memo at block of the old file to the next free block of the
   new one; *out receives its new block number. */
static int copy_memo(memo_copy *mc, u32 block, u32 *out)
{
    u32 off;
    u32 len;
    u32 nblocks;
    u16 got;
    u16 i;
    u16 n;
    int done;
    int rc;

    if (block >= mc->srcblocks || block > DBT_MAXBLOCK)
        return DBF_EFORMAT;
    if (mc->next > DBT_MAXBLOCK)
        return DBF_EFULL;
    off = mc->next * (u32)DBT_BLOCK;

    /* DBT_FILL bytes up to the memo's first block. */
    rc = 0;
    if (mc->pos < off)
        memset(mc->buf, DBT_FILL, PACK_BUF);
    while (rc == 0 && mc->pos < off) {
        n = (off - mc->pos < (u32)PACK_BUF) ? (u16)(off - mc->pos)
                                            : (u16)PACK_BUF;
        rc = cf_write(mc->dst, mc->buf, n);
        mc->pos += n;
    }
    if (rc != 0)
        return dbf_map_cf(rc);

    rc = cf_seek(mc->src, block * (u32)DBT_BLOCK);
    if (rc != 0)
        return dbf_map_cf(rc);
    len = 0;
    done = 0;
    while (!done) {
        rc = cf_read(mc->src, mc->buf, PACK_BUF, &got);
        if (rc != 0)
            return dbf_map_cf(rc);
        if (got == 0)
            return DBF_EFORMAT;         /* no terminator before the end */
        for (i = 0; i < got && mc->buf[i] != DBT_TERM; i++)
            ;
        done = (i < got);
        if (i > 0) {
            rc = cf_write(mc->dst, mc->buf, i);
            if (rc != 0)
                return dbf_map_cf(rc);
            len += i;
        }
    }
    mc->buf[0] = DBT_TERM;
    mc->buf[1] = DBT_TERM;
    rc = cf_write(mc->dst, mc->buf, 2);
    if (rc != 0)
        return dbf_map_cf(rc);
    len += 2;
    mc->pos += len;
    nblocks = (len + (u32)(DBT_BLOCK - 1)) / (u32)DBT_BLOCK;
    *out = mc->next;
    mc->next += nblocks;
    return DBF_OK;
}

/* Write the new memo file's header block (next free block 1). */
static int memo_header(memo_copy *mc)
{
    int rc;

    memset(mc->buf, DBT_FILL, PACK_BUF);
    put_le32(mc->buf, 1);
    rc = cf_write(mc->dst, mc->buf, DBT_BLOCK);
    mc->next = 1;
    mc->pos = DBT_BLOCK;
    return dbf_map_cf(rc);
}

/* Store the next-free block number in the new header and commit. */
static int memo_finish(memo_copy *mc)
{
    int rc;

    put_le32(mc->buf, mc->next);
    rc = cf_seek(mc->dst, 0);
    if (rc == 0)
        rc = cf_write(mc->dst, mc->buf, 4);
    if (rc == 0)
        rc = dos_commit_handle(mc->dst);
    return dbf_map_cf(rc);
}

/* Open the old memo file and size it: *blocks receives the larger of
   the header's next-free number and the file size in blocks. */
static int memo_open(memo_copy *mc, const char *path, u32 *blocks)
{
    u32 size;
    u16 got;
    u32 next;
    int rc;

    rc = dbf_map_cf(cf_open(path, (u8)(CF_READ | CF_DENYRW), &mc->src));
    if (rc != DBF_OK) {
        mc->src = -1;
        return rc;
    }
    rc = dbf_map_cf(cf_read(mc->src, mc->buf, 4, &got));
    if (rc == DBF_OK && got != 4)
        rc = DBF_EFORMAT;
    if (rc == DBF_OK)
        rc = dbf_map_cf(cf_size(mc->src, &size));
    if (rc != DBF_OK) {
        cf_close(mc->src);
        mc->src = -1;
        return rc;
    }
    next = get_le32(mc->buf);
    mc->srcblocks = size / (u32)DBT_BLOCK;
    if (size % (u32)DBT_BLOCK != 0)
        mc->srcblocks++;
    *blocks = next > mc->srcblocks ? next : mc->srcblocks;
    return DBF_OK;
}

/* Copy the live records of src into the new table dst, and their memos
   through mc when it is not NULL. */
static int copy_records(dbf *src, dbf *dst, memo_copy *mc,
                        dbf_packinfo *info)
{
    u8 mf[DBF_MAXFIELDS];
    u32 r;
    u32 blk;
    u32 nblk;
    u16 nm;
    u16 i;
    int rc;

    nm = mc != NULL ? memo_fields(src, mf) : 0;
    for (r = 1; r <= dbf_reccount(src); r++) {
        rc = dbf_read(src, r);
        if (rc != DBF_OK)
            return rc;
        if (dbf_is_deleted(src)) {
            info->removed++;
            continue;
        }
        memcpy(dbf_recbuf(dst), dbf_recbuf(src), dbf_reclen(src));
        for (i = 0; i < nm; i++) {
            rc = dbf_get_memo(src, mf[i], &blk);
            if (rc != DBF_OK)
                return DBF_EFORMAT;
            if (blk == 0)
                continue;
            rc = copy_memo(mc, blk, &nblk);
            if (rc != DBF_OK)
                return rc;
            rc = dbf_put_memo(dst, mf[i], nblk);
            if (rc != DBF_OK)
                return rc;
        }
        rc = dbf_append(dst);
        if (rc != DBF_OK)
            return rc;
        info->kept++;
    }
    return DBF_OK;
}

/* Write the packed table (and memo file) under the $ names.  On return
   every file is closed; on error the $ files are removed. */
static int write_packed(pack_names *pn, dbf_packinfo *info)
{
    dbf *src;
    dbf *dst;
    dbf_field *fields;
    memo_copy mc;
    u16 nf;
    int i;
    int memo;
    int rc;
    int rc2;

    dst = NULL;
    fields = NULL;
    mc.src = -1;
    mc.dst = -1;
    mc.buf = NULL;
    rc = dbf_open(&src, pn->set[0].cur, DBF_RDWR);
    if (rc != DBF_OK)
        return rc;
    memo = dbf_has_memo(src);
    nf = dbf_nfields(src);
    fields = (dbf_field *)malloc(sizeof(dbf_field) * (size_t)nf);
    mc.buf = (u8 *)malloc(PACK_BUF);
    if (fields == NULL || mc.buf == NULL)
        rc = DBF_ENOMEM;
    for (i = 0; rc == DBF_OK && i < nf; i++)
        memcpy(&fields[i], dbf_field_at(src, (u16)i), sizeof(dbf_field));
    if (rc == DBF_OK && memo)
        rc = memo_open(&mc, pn->set[1].cur, &info->memo_blocks_before);
    if (rc == DBF_OK)
        rc = dbf_create(pn->set[0].nw, fields, nf);
    if (rc == DBF_OK)
        rc = dbf_open(&dst, pn->set[0].nw, DBF_RDWR);
    if (rc == DBF_OK && memo) {
        rc = dbf_map_cf(cf_create(pn->set[1].nw, &mc.dst));
        if (rc == DBF_OK)
            rc = memo_header(&mc);
        else
            mc.dst = -1;
    }
    if (rc == DBF_OK)
        rc = copy_records(src, dst, memo ? &mc : NULL, info);
    if (rc == DBF_OK && memo) {
        rc = memo_finish(&mc);
        info->memo_blocks_after = mc.next;
    }

    if (dst != NULL) {
        rc2 = dbf_close(dst);
        if (rc == DBF_OK)
            rc = rc2;
    }
    if (mc.dst >= 0) {
        rc2 = dbf_map_cf(cf_close(mc.dst));
        if (rc == DBF_OK)
            rc = rc2;
    }
    if (mc.src >= 0)
        cf_close(mc.src);
    rc2 = dbf_close(src);
    if (rc == DBF_OK)
        rc = rc2;
    free(fields);
    free(mc.buf);
    if (rc != DBF_OK) {
        remove_if(pn->set[0].nw);
        remove_if(pn->set[1].nw);
    }
    return rc;
}

int dbf_pack(const char *path, dbf_packinfo *info)
{
    pack_names *pn;
    dbf_packinfo dummy;
    int rc;

    if (path == NULL)
        return DBF_EARG;
    if (info == NULL)
        info = &dummy;
    memset(info, 0, sizeof *info);
    pn = (pack_names *)malloc(sizeof(pack_names));
    if (pn == NULL)
        return DBF_ENOMEM;
    rc = make_names(pn, path);
    if (rc == DBF_OK)
        rc = recover_table(pn, 0);
    if (rc == DBF_OK)
        rc = write_packed(pn, info);
    if (rc == DBF_OK) {
        /* The new files' directory entries reach the disk before the
           commit point, from which recovery rolls forward. */
        dos_disk_reset();
        rc = rename_step(pn->set[0].cur, pn->set[0].bak);
        if (rc == DBF_OK) {
            rc = recover_table(pn, 1);
        } else {
            remove_if(pn->set[0].nw);
            remove_if(pn->set[1].nw);
        }
    }
    free(pn);
    return rc;
}

/* ---------------------------------------------------------------------
 * REINDEX.
 * --------------------------------------------------------------------- */

typedef struct {
    int h;                      /* the new index file                   */
    u16 keylen;
    u16 entsz;
    u16 kpp;
    u16 top;                    /* highest level in use                 */
    u32 npages;                 /* next page number                     */
    u32 root;
    u32 nkeys;                  /* leaf entries written                 */
    u16 n[NDX_MAXDEPTH];        /* entries (leaf) or children (inner)   */
    u8 *pg[NDX_MAXDEPTH];       /* page being filled at each level      */
    u8 unique;                  /* keep only the first record of a key  */
} nx_build;

/* Write the page of level lvl as the next page, at the file position:
   reindex starts it past the header and each page advances it.  *page
   receives its number.  The page buffer is cleared for reuse. */
static int nx_write(nx_build *b, u16 lvl, u32 *page)
{
    u8 *p;
    u16 count;
    int rc;

    p = b->pg[lvl];
    count = b->n[lvl];
    if (lvl > 0) {
        /* The rightmost child keeps only its page number. */
        count--;
        memset(NDX_ENT(b, p, count) + NDX_PTRSIZE, 0,
               (size_t)b->entsz - NDX_PTRSIZE);
    }
    put_le16(p, count);
    if (b->npages >= NDX_MAXPAGE)
        return DBF_EFULL;
    rc = cf_write(b->h, p, NDX_PAGE);
    if (rc != 0)
        return dbf_map_cf(rc);
    *page = b->npages++;
    memset(p, 0, PACK_PGBUF);
    b->n[lvl] = 0;
    return DBF_OK;
}

/* Add child page (with highest key) to the inner page of level lvl,
   writing full pages up the tree. */
static int nx_push(nx_build *b, u16 lvl, u32 child, const u8 *key)
{
    u8 *e;
    int rc;

    for (;;) {
        if (lvl >= NDX_MAXDEPTH)
            return DBF_ERANGE;
        if (b->pg[lvl] == NULL) {
            b->pg[lvl] = (u8 *)malloc(PACK_PGBUF);
            if (b->pg[lvl] == NULL)
                return DBF_ENOMEM;
            memset(b->pg[lvl], 0, PACK_PGBUF);
            b->n[lvl] = 0;
        }
        if (lvl > b->top)
            b->top = lvl;
        e = NDX_ENT(b, b->pg[lvl], b->n[lvl]);
        put_le32(e + NDX_E_LOWER, child);
        put_le32(e + NDX_E_RECNO, 0);
        memcpy(e + NDX_E_KEY, key, b->keylen);
        b->n[lvl]++;
        if (b->n[lvl] <= b->kpp)
            return DBF_OK;
        rc = nx_write(b, lvl, &child);
        if (rc != DBF_OK)
            return rc;
        lvl++;
    }
}

/* Add the leaf entry (key, recno), writing full pages up the tree. */
static int nx_add(nx_build *b, const u8 *key, u32 recno)
{
    u8 *e;
    u32 page;
    int rc;

    e = NDX_ENT(b, b->pg[0], b->n[0]);
    put_le32(e + NDX_E_LOWER, 0);
    put_le32(e + NDX_E_RECNO, recno);
    memcpy(e + NDX_E_KEY, key, b->keylen);
    b->n[0]++;
    b->nkeys++;
    if (b->n[0] < b->kpp)
        return DBF_OK;
    rc = nx_write(b, 0, &page);
    if (rc == DBF_OK)
        rc = nx_push(b, 1, page, key);
    return rc;
}

/* Finish the partial pages from the leaves up and set the root. */
static int nx_finish(nx_build *b)
{
    u8 high[NDX_MAXKEY];
    u32 page;
    int lvl;
    int rc;

    memset(high, 0, sizeof high);
    if (b->n[0] > 0 || b->npages == 1) {
        if (b->n[0] > 0)
            memcpy(high, NDX_ENT(b, b->pg[0], b->n[0] - 1) + NDX_E_KEY,
                   b->keylen);
        rc = nx_write(b, 0, &page);
        if (rc != DBF_OK)
            return rc;
        if (b->top == 0) {
            b->root = page;
            return DBF_OK;
        }
        rc = nx_push(b, 1, page, high);
        if (rc != DBF_OK)
            return rc;
    }
    for (lvl = 1; lvl <= b->top; lvl++) {
        if (b->n[lvl] == 0)
            continue;
        memcpy(high, NDX_ENT(b, b->pg[lvl], b->n[lvl] - 1) + NDX_E_KEY,
               b->keylen);
        if (b->n[lvl] == 1) {
            page = get_le32(NDX_ENT0(b->pg[lvl]) + NDX_E_LOWER);
            b->n[lvl] = 0;
            memset(b->pg[lvl], 0, PACK_PGBUF);
        } else {
            rc = nx_write(b, (u16)lvl, &page);
            if (rc != DBF_OK)
                return rc;
        }
        if (lvl == b->top) {
            b->root = page;
            return DBF_OK;
        }
        rc = nx_push(b, (u16)(lvl + 1), page, high);
        if (rc != DBF_OK)
            return rc;
    }
    return DBF_EFORMAT;         /* not reached: the top has a child     */
}

/* Write the header page and commit the file: hdr holds the stored
   expression at NDX_H_EXPR and zeros elsewhere; the fields are added. */
static int nx_header(nx_build *b, u8 *hdr)
{
    int rc;

    put_le32(hdr + NDX_H_ROOT, b->root);
    put_le32(hdr + NDX_H_PAGES, b->npages);
    put_le16(hdr + NDX_H_KEYLEN, b->keylen);
    put_le16(hdr + NDX_H_KPP, b->kpp);
    put_le16(hdr + NDX_H_ENTSZ, b->entsz);
    hdr[NDX_H_UNIQUE] = b->unique;
    rc = cf_seek(b->h, 0);
    if (rc == 0)
        rc = cf_write(b->h, hdr, NDX_PAGE);
    if (rc == 0)
        rc = dos_commit_handle(b->h);
    return dbf_map_cf(rc);
}

/* Insert every record's key into the scratch index sx. */
static int fill_scratch(ndx *sx, kx *k, dbf *t, u8 *key)
{
    u32 r;
    int rc;

    for (r = 1; r <= dbf_reccount(t); r++) {
        rc = dbf_read(t, r);
        if (rc == DBF_OK)
            rc = kx_eval(k, t, key);
        if (rc == DBF_OK)
            rc = ndx_insert(sx, key, r);
        if (rc != DBF_OK)
            return rc;
    }
    return DBF_OK;
}

/* Walk the scratch index in order into the builder. */
static int build_from(nx_build *b, ndx *sx, u8 *key, u8 *prev)
{
    u32 r;
    int have;
    int rc;

    have = 0;
    rc = ndx_first(sx, &r);
    while (rc == DBF_OK && r != 0) {
        rc = ndx_key(sx, key);
        if (rc != DBF_OK)
            return rc;
        if (!b->unique || !have || memcmp(key, prev, b->keylen) != 0) {
            rc = nx_add(b, key, r);
            if (rc != DBF_OK)
                return rc;
            memcpy(prev, key, b->keylen);
            have = 1;
        }
        rc = ndx_next(sx, &r);
    }
    if (rc == DBF_OK)
        rc = nx_finish(b);
    return rc;
}

/* Everything of ndx_reindex after the recovery; names in s and the
   scratch index name in tmp; *nkeys receives the keys written. */
static int reindex(pack_set *s, const char *tmp, dbf *t, u8 *hdr,
                   u32 *nkeys)
{
    nx_build b;
    const char *expr;
    ndx *old;
    ndx *sx;
    kx *k;
    u8 *key;
    size_t elen;
    int lvl;
    int rc;
    int rc2;

    memset(&b, 0, sizeof b);
    b.h = -1;
    k = NULL;
    sx = NULL;
    key = NULL;
    rc = ndx_open(&old, s->cur, DBF_RDONLY);
    if (rc != DBF_OK)
        return rc;
    b.keylen = ndx_keylen(old);
    b.unique = (u8)ndx_unique(old);
    /* The new header: the expression as stored, trailing blanks and
       all, and zeros elsewhere until nx_header adds the fields. */
    expr = ndx_expr(old);
    elen = strlen(expr);
    memset(hdr, 0, NDX_PAGE);
    memcpy(hdr + NDX_H_EXPR, expr, elen);
    memset(hdr + NDX_H_EXPR + elen, ' ', ndx_exprlen(old) - elen);
    rc = kx_compile(&k, t, expr);
    rc2 = ndx_close(old);
    if (rc == DBF_OK)
        rc = rc2;
    if (rc == DBF_OK && kx_len(k) != b.keylen)
        rc = DBF_EFORMAT;
    if (rc != DBF_OK) {
        kx_free(k);
        return rc;
    }
    b.entsz = (u16)NDX_ENTSZ(b.keylen);
    b.kpp = (u16)NDX_KPP(b.entsz);
    b.npages = 1;

    key = (u8 *)malloc(2 * NDX_MAXKEY);
    b.pg[0] = (u8 *)malloc(PACK_PGBUF);
    if (key == NULL || b.pg[0] == NULL)
        rc = DBF_ENOMEM;
    else
        memset(b.pg[0], 0, PACK_PGBUF);
    if (rc == DBF_OK)
        rc = ndx_create(tmp, (char *)hdr + NDX_H_EXPR, b.keylen);
    if (rc == DBF_OK)
        rc = ndx_open(&sx, tmp, DBF_RDWR);
    if (rc == DBF_OK)
        rc = fill_scratch(sx, k, t, key);
    if (rc == DBF_OK) {
        rc = dbf_map_cf(cf_create(s->nw, &b.h));
        if (rc != DBF_OK)
            b.h = -1;
        else
            rc = dbf_map_cf(cf_seek(b.h, NDX_PAGE));
    }
    if (rc == DBF_OK)
        rc = build_from(&b, sx, key, key + NDX_MAXKEY);
    if (rc == DBF_OK)
        rc = nx_header(&b, hdr);
    *nkeys = b.nkeys;
    if (b.h >= 0) {
        rc2 = dbf_map_cf(cf_close(b.h));
        if (rc == DBF_OK)
            rc = rc2;
    }
    if (sx != NULL)
        ndx_close(sx);
    cf_remove(tmp);
    kx_free(k);
    free(key);
    for (lvl = 0; lvl < NDX_MAXDEPTH; lvl++)
        free(b.pg[lvl]);
    if (rc != DBF_OK)
        remove_if(s->nw);
    return rc;
}

int ndx_reindex(dbf *t, const char *ndxpath, u32 *nkeys)
{
    nx_names *nn;
    u32 n;
    int rc;

    if (nkeys != NULL)
        *nkeys = 0;
    if (t == NULL || ndxpath == NULL)
        return DBF_EARG;
    nn = (nx_names *)malloc(sizeof(nx_names));
    if (nn == NULL)
        return DBF_ENOMEM;
    n = 0;
    rc = make_index_names(nn, ndxpath);
    if (rc == DBF_OK)
        rc = recover_index(&nn->s, nn->tmp);
    if (rc == DBF_OK)
        rc = reindex(&nn->s, nn->tmp, t, nn->hdr, &n);
    if (rc == DBF_OK) {
        /* The commit point, as for a table. */
        dos_disk_reset();
        rc = rename_step(nn->s.cur, nn->s.bak);
        if (rc == DBF_OK)
            rc = recover_index(&nn->s, nn->tmp);
        else
            remove_if(nn->s.nw);
    }
    if (rc == DBF_OK && nkeys != NULL)
        *nkeys = n;
    free(nn);
    return rc;
}
