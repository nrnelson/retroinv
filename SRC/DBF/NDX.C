/*
 * NDX.C - dBASE III Plus index files (.NDX).
 *
 * File layout, all integers little-endian, in NDX_PAGE-byte pages:
 *
 *   page 0, the header
 *     0..3    root page number (u32)
 *     4..7    page count, header included: the next free page (u32)
 *     8..11   zero
 *     12..13  key length in bytes (u16)
 *     14..15  keys per page: (NDX_PAGE - 4) / entry size (u16)
 *     16..17  key type: 0 character, 1 numeric (u16)
 *     18..19  entry size: 8 + key length rounded up to a multiple of 4
 *     20..22  zero
 *     23      unique flag (1 when built with SET UNIQUE ON)
 *     24..    the key expression, then a NUL
 *   pages 1.., each
 *     0..1    entry count (u16); dBASE leaves junk in bytes 2..3, so they
 *             are ignored when read and written as zero
 *     4..     entries of the entry size: lower page (u32), record
 *             number (u32), key; bytes past the key length are padding
 *
 * dBASE III Plus leaves memory junk after the expression's NUL, after a
 * page's last entry and in the padding; all of it is ignored when read.
 * Files written here hold zeros there, except that pages changed here
 * keep the junk of a dBASE-written page where no entry is written.
 *
 * The tree is a B+ tree whose leaves hold every key.  A leaf entry has
 * lower page 0 and the record number.  An inner entry has the child page
 * as lower page, record number 0 and the highest key of that child; after
 * the count entries one more slot holds, in its lower-page field only,
 * the rightmost child, which has no key of its own.  An inner page holds
 * up to keys-per-page entries plus that slot.  A page is an inner page
 * when the lower page of its entry 0 is not 0, so an inner page may have
 * a count of 0 and a single child in the slot, as dBASE leaves one after
 * deletes.  Leaves need not all be at the same depth: dBASE's INDEX ON
 * hangs a last, partly filled page directly under an upper level.
 * Pages that no longer belong to the tree may remain in the file.
 * Numeric (type 1) indexes hold IEEE doubles and are refused by ndx_open
 * with DBF_EFORMAT.
 *
 * Invariants kept by insert and delete:
 *   I1  the leaf entries, read left to right, are strictly increasing in
 *       (key, record number) order;
 *   I2  the key of an inner entry equals the highest key in its child;
 *   I3  no page reachable from the root is empty, except a root leaf.
 * dBASE III Plus searches by key alone: it descends into the first child
 * whose key is >= the search key and expects the leaf it reaches to hold
 * the first matching entry, which I2 and I3 guarantee.  Delete therefore
 * does what dBASE's own delete does: when a leaf loses its highest entry
 * the inner keys above it are lowered to the new highest key, and a leaf
 * left empty is unlinked from its parent (the previous child moving into
 * the rightmost slot when needed), which can leave an inner page with a
 * count of 0 and one child, or unlink an inner page left without
 * children.  Pages are never merged and unlinked pages are not reused;
 * REINDEX restores density.  Insert raises the inner keys above a leaf
 * when the new entry becomes its highest.  A page split moves the upper
 * half of a full page to a new page and inserts the highest key of the
 * lower half into the parent; splitting the root adds a new root.
 *
 * Duplicate keys are ordered by record number, but inner entries carry
 * no record number.  When a run of equal keys spans several children,
 * the descent finds the child where (key, record number) belongs by a
 * binary search over the last entries of those children, reached down
 * their rightmost paths.  Reading files that break I2 or I3 still works:
 * a search steps forward through the leaves while the entries are below
 * the target.
 *
 * All open indexes share one pool of NDX_NPOOL page buffers, written
 * back when evicted (least recently used first) and at flush and close,
 * and one scratch buffer for splits, allocated by the first ndx_open and
 * freed by the last ndx_close.  The code never holds more than two page
 * buffers at a time while it fetches another page, so a pool of at least
 * three pages suffices.  Because a slot may hold another index's dirty
 * page, a failed write-back of that page is reported by the operation of
 * whichever index needed the slot, and the page stays dirty in the pool.
 * ndx_close drops the index's pages even when its flush fails: the error
 * is returned and those changes are lost, so callers flush after each
 * request that changes an index.  The header's root and page count are
 * written at ndx_flush and ndx_close, after the pages; a flush with
 * nothing written since the last commit makes no disk call.  DOS records a
 * file's new size in its directory entry only when a handle to it is
 * closed (the duplicate dos_commit_handle closes), while the header
 * sector can reach the disk whenever DOS reuses its buffer, so when
 * pages were added since the last commit the file is committed after
 * the pages and before the header is written, then committed again: the
 * header never names more pages than the recorded size holds.  If the
 * machine stops in between, the header may name fewer pages than the
 * file holds, so ndx_open takes the larger of the header's count and the
 * file size in pages, and new pages never overwrite data already on the
 * disk.  Pages a split changed in place may then be on disk while the
 * header still names the old root, so the tree can miss keys: an index
 * whose flush was cut off needs a reindex (the engine rebuilds every index
 * of a table whose change it marks as unfinished).
 *
 * Malformed files give DBF_EFORMAT: header fields out of range, an entry
 * count above the keys per page, page numbers outside the file or a page
 * naming itself as a child, a path deeper than NDX_MAXDEPTH, a leaf
 * entry with record number 0, or entries out of order while stepping.
 * Every operation may fetch at most a budget of pages proportional to
 * the page count, which bounds the work on files whose pages form loops
 * or shared subtrees.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "DBFINT.H"

#define NDX_TYPE_CHAR   0       /* key type of a character index        */

#define NDX_SCRATCH     (NDX_PAGE + NDX_MAXENT + NDX_PTRSIZE)
#define NDX_MINKPP      2       /* a split needs at least this many     */

#define NDX_NPOOL       6       /* page buffers shared by all indexes   */

/* Pages each operation may fetch: NDX_BUDGET_MUL per page in the file
   plus NDX_BUDGET_ADD. */
#define NDX_BUDGET_MUL  4
#define NDX_BUDGET_ADD  64

/* Cursor states.  NDX_AT, left by ndx_insert and ndx_delete, stands for
   the first entry at or after (curkey, currec), which is located (to
   NDX_ON or NDX_EOF) only when a step or ndx_key needs it. */
#define NDX_BOF         0       /* before the first entry               */
#define NDX_ON          1       /* on the entry stk[] leads to          */
#define NDX_EOF         2       /* past the last entry                  */
#define NDX_AT          3       /* at (curkey, currec), not located     */

typedef struct {
    u32 page;
    u16 idx;            /* entry (leaf) or child (inner, 0..count)      */
} ndx_lvl;

struct ndx {
    int h;              /* DOS handle                                   */
    u32 root;
    u32 npages;         /* next free page                               */
    u32 cpages;         /* npages when the file was last committed      */
    u32 budget;         /* page fetches left in this operation          */
    u32 currec;         /* record of the current entry                  */
    u16 keylen;
    u16 kpp;            /* entries per page                             */
    u16 entsz;
    u16 depth;          /* levels in stk[], root to leaf                */
    ndx_lvl stk[NDX_MAXDEPTH];  /* current position, root to leaf       */
    u8 curkey[NDX_MAXKEY];      /* key of the current entry             */
    char *expr;         /* stored expression, trailing blanks removed   */
    u8 mode;            /* DBF_RDONLY or DBF_RDWR                       */
    u8 hdirty;          /* root or page count changed since the flush   */
    u8 wrote;           /* file written since it was last committed     */
    u8 state;           /* NDX_BOF, NDX_ON, NDX_EOF or NDX_AT           */
    u8 unique;
    u8 exprlen;         /* stored expression length, blanks included    */
};

/* The shared page pool. */
typedef struct {
    ndx *owner;         /* NULL when the slot is free                   */
    u32 page;
    u32 used;           /* tick of the last use                         */
    u8 *buf;
    u8 dirty;
} ndx_slot;

/* The pool, and the state of one operation, which no two indexes use at
   once.  ndx_ins, of NDX_MAXDEPTH levels, holds where ndx_insert puts
   the new entry (ndx_idepth levels, root to leaf), from locate until the
   insert is done. */
static ndx_slot ndx_pool[NDX_NPOOL];
static u8 *ndx_mem;     /* one block: ndx_ins, the pool pages, the rest */
static ndx_lvl *ndx_ins;
static u8 *ndx_scratch; /* NDX_SCRATCH bytes, for splits                */
static u8 *ndx_tmpkey;  /* an entry looked at during a search           */
static u8 *ndx_upkey;   /* key going up into a parent page, or the
                           padded prefix of ndx_seek                    */
static u32 ndx_tmprec;
static u32 ndx_tick;
static u16 ndx_idepth;
static u16 ndx_users;   /* open indexes                                 */

/* ndx_ins comes first in the block, where malloc's alignment holds. */
#define NDX_INSMEM      ((size_t)NDX_MAXDEPTH * sizeof(ndx_lvl))
#define NDX_POOLMEM     (NDX_INSMEM + (size_t)NDX_NPOOL * NDX_PAGE + \
                         NDX_SCRATCH + 2 * NDX_MAXKEY)

static u16 pg_count(const u8 *b)
{
    return get_le16(b);
}

/* Set the entry count of page b, zeroing the ignored bytes 2..3. */
static void pg_setcount(u8 *b, u16 n)
{
    put_le32(b, n);
}

/* Compare (ka, ra) with (kb, rb): key bytes first, then record number. */
static int cmp_pair(ndx *x, const u8 *ka, u32 ra, const u8 *kb, u32 rb)
{
    int c;

    c = memcmp(ka, kb, x->keylen);
    if (c != 0)
        return c;
    if (ra < rb)
        return -1;
    return ra > rb ? 1 : 0;
}

/* ---------------------------------------------------------------------
 * The page pool.
 * --------------------------------------------------------------------- */

static int pool_acquire(void)
{
    int i;

    if (ndx_users == 0) {
        ndx_mem = (u8 *)malloc(NDX_POOLMEM);
        if (ndx_mem == NULL)
            return DBF_ENOMEM;
        ndx_ins = (ndx_lvl *)ndx_mem;
        for (i = 0; i < NDX_NPOOL; i++) {
            ndx_pool[i].owner = NULL;
            ndx_pool[i].dirty = 0;
            ndx_pool[i].buf = ndx_mem + NDX_INSMEM + (size_t)i * NDX_PAGE;
        }
        ndx_scratch = ndx_mem + NDX_INSMEM + (size_t)NDX_NPOOL * NDX_PAGE;
        ndx_tmpkey = ndx_scratch + NDX_SCRATCH;
        ndx_upkey = ndx_tmpkey + NDX_MAXKEY;
    }
    ndx_users++;
    return DBF_OK;
}

/* Drop the slots of x without writing them, and free the pool when x
   was the last open index. */
static void pool_release(ndx *x)
{
    int i;

    for (i = 0; i < NDX_NPOOL; i++) {
        if (ndx_pool[i].owner == x) {
            ndx_pool[i].owner = NULL;
            ndx_pool[i].dirty = 0;
        }
    }
    ndx_users--;
    if (ndx_users == 0) {
        free(ndx_mem);
        ndx_mem = NULL;
    }
}

static int write_slot(ndx_slot *s)
{
    int rc;

    rc = cf_seek(s->owner->h, s->page * (u32)NDX_PAGE);
    if (rc == 0)
        rc = cf_write(s->owner->h, s->buf, NDX_PAGE);
    if (rc != 0)
        return dbf_map_cf(rc);
    s->dirty = 0;
    s->owner->wrote = 1;
    return DBF_OK;
}

/* A slot to load a page into: a free one, else the least recently used,
   written back first if it is dirty. */
static int victim(ndx_slot **out)
{
    ndx_slot *s;
    int i;
    int rc;

    s = &ndx_pool[0];
    for (i = 0; i < NDX_NPOOL; i++) {
        if (ndx_pool[i].owner == NULL) {
            s = &ndx_pool[i];
            break;
        }
        if (ndx_pool[i].used < s->used)
            s = &ndx_pool[i];
    }
    if (s->owner != NULL && s->dirty) {
        rc = write_slot(s);
        if (rc != DBF_OK)
            return rc;
    }
    s->owner = NULL;
    *out = s;
    return DBF_OK;
}

/* Fetch page, a page number of the tree; its count is checked. */
static int page_get(ndx *x, u32 page, u8 **out)
{
    ndx_slot *s;
    int i;
    u16 got;
    int rc;

    *out = NULL;
    if (x->budget == 0 || page == 0 || page >= x->npages)
        return DBF_EFORMAT;
    x->budget--;
    for (i = 0; i < NDX_NPOOL; i++) {
        if (ndx_pool[i].owner == x && ndx_pool[i].page == page) {
            ndx_pool[i].used = ++ndx_tick;
            *out = ndx_pool[i].buf;
            return DBF_OK;
        }
    }
    rc = victim(&s);
    if (rc != DBF_OK)
        return rc;
    rc = cf_seek(x->h, page * (u32)NDX_PAGE);
    if (rc == 0)
        rc = cf_read(x->h, s->buf, NDX_PAGE, &got);
    if (rc != 0)
        return dbf_map_cf(rc);
    if (got != NDX_PAGE || pg_count(s->buf) > x->kpp)
        return DBF_EFORMAT;
    s->owner = x;
    s->page = page;
    s->dirty = 0;
    s->used = ++ndx_tick;
    *out = s->buf;
    return DBF_OK;
}

/* Mark a cached page as changed. */
static void page_dirty(ndx *x, u32 page)
{
    int i;

    for (i = 0; i < NDX_NPOOL; i++)
        if (ndx_pool[i].owner == x && ndx_pool[i].page == page)
            ndx_pool[i].dirty = 1;
}

/* A new zero-filled page at the end of the file, already marked dirty. */
static int page_new(ndx *x, u32 *page, u8 **out)
{
    ndx_slot *s;
    int rc;

    *out = NULL;
    if (x->npages >= NDX_MAXPAGE)
        return DBF_EFULL;
    rc = victim(&s);
    if (rc != DBF_OK)
        return rc;
    memset(s->buf, 0, NDX_PAGE);
    s->owner = x;
    s->page = x->npages;
    s->dirty = 1;
    s->used = ++ndx_tick;
    *page = x->npages;
    x->npages++;
    x->hdirty = 1;
    *out = s->buf;
    return DBF_OK;
}

/* Give the operation starting on x its budget of page fetches. */
static void start_op(ndx *x)
{
    x->budget = x->npages * (u32)NDX_BUDGET_MUL + (u32)NDX_BUDGET_ADD;
}

/* ---------------------------------------------------------------------
 * Searching within a page.
 * --------------------------------------------------------------------- */

/* First entry of leaf b at or after (key, rec), or the count. */
static u16 leaf_lb(ndx *x, const u8 *b, const u8 *key, u32 rec)
{
    const u8 *e;
    u16 lo;
    u16 hi;
    u16 mid;

    lo = 0;
    hi = pg_count(b);
    while (lo < hi) {
        mid = (u16)(lo + (hi - lo) / 2u);
        e = NDX_ENT(x, b, mid);
        if (cmp_pair(x, e + NDX_E_KEY, get_le32(e + NDX_E_RECNO), key,
                     rec) < 0)
            lo = (u16)(mid + 1u);
        else
            hi = mid;
    }
    return lo;
}

/* First entry of page b whose first len key bytes are >= key, or the
   count; on an inner page that is the child to descend into. */
static u16 first_ge(ndx *x, const u8 *b, const u8 *key, u16 len)
{
    u16 lo;
    u16 hi;
    u16 mid;

    lo = 0;
    hi = pg_count(b);
    while (lo < hi) {
        mid = (u16)(lo + (hi - lo) / 2u);
        if (memcmp(NDX_ENT(x, b, mid) + NDX_E_KEY, key, len) < 0)
            lo = (u16)(mid + 1u);
        else
            hi = mid;
    }
    return lo;
}

/* The lower page of entry i of page b. */
static u32 child_at(ndx *x, const u8 *b, u16 i)
{
    return get_le32(NDX_ENT(x, b, i) + NDX_E_LOWER);
}

/* 1 for an inner page: its entry 0, or with a count of 0 its rightmost
   slot, names a child.  Leaf entries have lower page 0. */
static int pg_inner(const u8 *b)
{
    return get_le32(NDX_ENT0(b) + NDX_E_LOWER) != 0;
}

/* Child i of inner page pg (image b), checked not to be pg itself. */
static int child_of(ndx *x, u32 pg, const u8 *b, u16 i, u32 *c)
{
    *c = child_at(x, b, i);
    return *c == pg ? DBF_EFORMAT : DBF_OK;
}

/* Copy the leaf entry at the current position into curkey and currec. */
static int load_cur(ndx *x)
{
    const u8 *e;
    u8 *b;
    int rc;

    rc = page_get(x, x->stk[x->depth - 1].page, &b);
    if (rc != DBF_OK)
        return rc;
    e = NDX_ENT(x, b, x->stk[x->depth - 1].idx);
    memcpy(x->curkey, e + NDX_E_KEY, x->keylen);
    x->currec = get_le32(e + NDX_E_RECNO);
    if (x->currec == 0)
        return DBF_EFORMAT;     /* a leaf entry names a record */
    return DBF_OK;
}

/* ---------------------------------------------------------------------
 * Moving through the leaves.  stk[0..depth-1] holds a path from the root
 * to a leaf; on an inner page idx is the child taken (0..count), on the
 * leaf the entry.
 * --------------------------------------------------------------------- */

/* Extend the path from level d, whose idx is set, down to a leaf; each
   page below is entered at its start (last = 0) or at its end. */
static int descend_from(ndx *x, u16 d, int last)
{
    u32 c;
    u8 *b;
    int rc;

    rc = page_get(x, x->stk[d].page, &b);
    if (rc != DBF_OK)
        return rc;
    while (pg_inner(b)) {
        if (d + 1u >= NDX_MAXDEPTH)
            return DBF_EFORMAT;
        rc = child_of(x, x->stk[d].page, b, x->stk[d].idx, &c);
        if (rc == DBF_OK)
            rc = page_get(x, c, &b);
        if (rc != DBF_OK)
            return rc;
        d++;
        x->stk[d].page = c;
        x->stk[d].idx = (u16)(last ? pg_count(b) : 0u);
    }
    x->depth = (u16)(d + 1u);
    return DBF_OK;
}

/* Make stk[] lead to an entry at or after its position: while the leaf
   position is past the leaf's last entry, go to the next leaf.  *found
   is 0 when there is none (stk[] then is unchanged). */
static int walk_fwd(ndx *x, int *found)
{
    u16 d;
    u8 *b;
    int rc;

    *found = 0;
    for (;;) {
        d = (u16)(x->depth - 1u);
        rc = page_get(x, x->stk[d].page, &b);
        if (rc != DBF_OK)
            return rc;
        if (x->stk[d].idx < pg_count(b)) {
            *found = 1;
            return DBF_OK;
        }
        for (;;) {
            if (d == 0)
                return DBF_OK;
            d--;
            rc = page_get(x, x->stk[d].page, &b);
            if (rc != DBF_OK)
                return rc;
            if (x->stk[d].idx < pg_count(b))
                break;
        }
        x->stk[d].idx++;
        rc = descend_from(x, d, 0);
        if (rc != DBF_OK)
            return rc;
    }
}

/* Move stk[] to the entry before its position, skipping empty leaves;
   *found is 0 when there is none. */
static int walk_back(ndx *x, int *found)
{
    u16 d;
    int rc;

    *found = 0;
    for (;;) {
        d = (u16)(x->depth - 1u);
        if (x->stk[d].idx > 0) {
            x->stk[d].idx--;
            *found = 1;
            return DBF_OK;
        }
        for (;;) {
            if (d == 0)
                return DBF_OK;
            d--;
            if (x->stk[d].idx > 0)
                break;
        }
        x->stk[d].idx--;
        rc = descend_from(x, d, 1);
        if (rc != DBF_OK)
            return rc;
    }
}

/* Settle on the entry stk[] leads to (found) or past the end. */
static int settle(ndx *x, int found, u32 *recno)
{
    int rc;

    if (!found) {
        x->state = NDX_EOF;
        *recno = 0;
        return DBF_OK;
    }
    rc = load_cur(x);
    if (rc != DBF_OK)
        return rc;
    x->state = NDX_ON;
    *recno = x->currec;
    return DBF_OK;
}

/* ndx_first (last = 0) and ndx_last (last = 1): descend from the root
   along the first or last children and step onto the first or last
   entry.  An empty index is left at NDX_BOF. */
static int edge(ndx *x, int last, u32 *recno)
{
    u8 *b;
    int found;
    int rc;

    if (recno != NULL)
        *recno = 0;
    if (x == NULL || recno == NULL)
        return DBF_EARG;
    start_op(x);
    x->state = NDX_BOF;
    found = 0;
    rc = page_get(x, x->root, &b);
    if (rc == DBF_OK) {
        x->stk[0].page = x->root;
        x->stk[0].idx = (u16)(last ? pg_count(b) : 0u);
        rc = descend_from(x, 0, last);
    }
    if (rc == DBF_OK)
        rc = last ? walk_back(x, &found) : walk_fwd(x, &found);
    if (rc == DBF_OK && found)
        rc = settle(x, 1, recno);
    if (rc != DBF_OK) {
        x->state = NDX_BOF;
        *recno = 0;
    }
    return rc;
}

/* Last entry of the subtree under page pg at level d, into ndx_tmpkey and
   ndx_tmprec; *known is 0 when the rightmost leaf is empty. */
static int subtree_last(ndx *x, u32 pg, u16 d, int *known)
{
    const u8 *e;
    u32 c;
    u8 *b;
    u16 n;
    int rc;

    *known = 0;
    for (;;) {
        rc = page_get(x, pg, &b);
        if (rc != DBF_OK)
            return rc;
        n = pg_count(b);
        if (!pg_inner(b))
            break;
        if (++d >= NDX_MAXDEPTH)
            return DBF_EFORMAT;
        rc = child_of(x, pg, b, n, &c);
        if (rc != DBF_OK)
            return rc;
        pg = c;
    }
    if (n == 0)
        return DBF_OK;
    e = NDX_ENT(x, b, n - 1u);
    memcpy(ndx_tmpkey, e + NDX_E_KEY, x->keylen);
    ndx_tmprec = get_le32(e + NDX_E_RECNO);
    *known = 1;
    return DBF_OK;
}

/* Child of inner page pg (level d) to descend into for (key, rec): the
   first whose key is >= key, moved right past children of a run of
   equal keys whose last entry is below (key, rec). */
static int pick_child(ndx *x, u32 pg, u16 d, const u8 *key, u32 rec,
                      u16 *out)
{
    u8 *b;
    u16 n;
    u16 lo;
    u16 hi;
    u16 mid;
    u32 c;
    int known;
    int rc;

    rc = page_get(x, pg, &b);
    if (rc != DBF_OK)
        return rc;
    n = pg_count(b);
    lo = first_ge(x, b, key, x->keylen);
    hi = lo;
    while (hi < n && memcmp(NDX_ENT(x, b, hi) + NDX_E_KEY, key,
                            x->keylen) == 0)
        hi++;
    /* Children lo..hi-1 have the key itself as highest key.  Skipping a
       child whose last entry is below the target is safe whatever the
       other children hold, by I1. */
    while (lo < hi) {
        mid = (u16)(lo + (hi - lo) / 2u);
        rc = page_get(x, pg, &b);
        if (rc == DBF_OK)
            rc = child_of(x, pg, b, mid, &c);
        if (rc == DBF_OK)
            rc = subtree_last(x, c, (u16)(d + 1u), &known);
        if (rc != DBF_OK)
            return rc;
        if (known && cmp_pair(x, ndx_tmpkey, ndx_tmprec, key, rec) < 0)
            lo = (u16)(mid + 1u);
        else
            hi = mid;
    }
    *out = lo;
    return DBF_OK;
}

/* Find the first entry >= (key, rec).  Sets stk[] and the state (NDX_ON
   with curkey and currec, or NDX_EOF), and ndx_ins to the place where
   (key, rec) would be inserted: in the leaf the descent reached, or in
   the last leaf entered because it starts with the same key and a lower
   record number; at the end of that leaf when the entry found lies in a
   later one.  The state is NDX_BOF on failure. */
static int locate(ndx *x, const u8 *key, u32 rec)
{
    const u8 *e;
    u32 pg;
    u16 d;
    u16 c;
    u8 *b;
    int found;
    int rc;

    x->state = NDX_BOF;
    pg = x->root;
    for (d = 0;; d++) {
        if (d >= NDX_MAXDEPTH)
            return DBF_EFORMAT;
        rc = page_get(x, pg, &b);
        if (rc != DBF_OK)
            return rc;
        x->stk[d].page = pg;
        if (!pg_inner(b)) {
            x->stk[d].idx = leaf_lb(x, b, key, rec);
            x->depth = (u16)(d + 1u);
            break;
        }
        rc = pick_child(x, pg, d, key, rec, &c);
        if (rc == DBF_OK)
            rc = page_get(x, pg, &b);
        if (rc != DBF_OK)
            return rc;
        x->stk[d].idx = c;
        rc = child_of(x, pg, b, c, &pg);
        if (rc != DBF_OK)
            return rc;
    }
    memcpy(ndx_ins, x->stk, sizeof x->stk);
    ndx_idepth = x->depth;

    /* b is the leaf at level d, where the descent stopped. */
    while (x->stk[d].idx >= pg_count(b)) {
        rc = walk_fwd(x, &found);
        if (rc != DBF_OK)
            return rc;
        if (!found) {
            x->state = NDX_EOF;
            return DBF_OK;
        }
        d = (u16)(x->depth - 1u);
        rc = page_get(x, x->stk[d].page, &b);
        if (rc != DBF_OK)
            return rc;
        e = NDX_ENT(x, b, x->stk[d].idx);
        if (cmp_pair(x, e + NDX_E_KEY, get_le32(e + NDX_E_RECNO), key,
                     rec) >= 0)
            break;
        /* Equal keys with lower record numbers continue in this leaf. */
        x->stk[d].idx = leaf_lb(x, b, key, rec);
        memcpy(ndx_ins, x->stk, sizeof x->stk);
        ndx_idepth = x->depth;
    }
    rc = load_cur(x);
    if (rc != DBF_OK)
        return rc;
    x->state = NDX_ON;
    return DBF_OK;
}

/* Locate the position NDX_AT stands for; any other is left as it is. */
static int locate_at(ndx *x)
{
    int rc;

    if (x->state != NDX_AT)
        return DBF_OK;
    start_op(x);
    rc = locate(x, x->curkey, x->currec);
    if (rc != DBF_OK)
        x->state = NDX_BOF;
    return rc;
}

/* ---------------------------------------------------------------------
 * Changing the tree.
 * --------------------------------------------------------------------- */

/* The highest key under the page at level n - 1 of path became key:
   store it in the inner entries above it, up to the first level where
   the path does not go through the rightmost slot. */
static int raise_max(ndx *x, ndx_lvl *path, u16 n, const u8 *key)
{
    u16 d;
    u8 *b;
    int rc;

    d = (u16)(n - 1u);
    while (d > 0) {
        d--;
        rc = page_get(x, path[d].page, &b);
        if (rc != DBF_OK)
            return rc;
        if (path[d].idx < pg_count(b)) {
            memcpy(NDX_ENT(x, b, path[d].idx) + NDX_E_KEY, key, x->keylen);
            page_dirty(x, path[d].page);
            return DBF_OK;
        }
    }
    return DBF_OK;
}

/* Store entry {lower, rec, key} at e, padding zeroed. */
static void put_entry(ndx *x, u8 *e, u32 lower, u32 rec, const u8 *key)
{
    memset(e, 0, x->entsz);
    put_le32(e + NDX_E_LOWER, lower);
    put_le32(e + NDX_E_RECNO, rec);
    memcpy(e + NDX_E_KEY, key, x->keylen);
}

/* Fill page b from scratch entries from..from+n-1, followed on an inner
   page by the rightmost-child slot holding child; the rest is zeroed. */
static void fill_page(ndx *x, u8 *b, u16 from, u16 n, int inner,
                      u32 child)
{
    memset(b, 0, NDX_PAGE);
    pg_setcount(b, n);
    memcpy(NDX_ENT0(b), NDX_ENT(x, ndx_scratch, from), (size_t)n * x->entsz);
    if (inner)
        put_le32(NDX_ENT(x, b, n) + NDX_E_LOWER, child);
}

/* Insert {left, 0, ndx_upkey} into the inner page at level d of
   ndx_ins before child ndx_ins[d].idx, which becomes right; split upward
   as needed. */
static int insert_up(ndx *x, int d, u32 left, u32 right)
{
    u32 pg;
    u32 rp;
    u32 last;
    u16 ci;
    u16 n;
    u16 m;
    u8 *b;
    u8 *rb;
    u8 *s;
    int rc;

    for (;;) {
        if (d < 0) {
            /* The root was split: a new root over the two halves. */
            rc = page_new(x, &rp, &rb);
            if (rc != DBF_OK)
                return rc;
            pg_setcount(rb, 1);
            put_entry(x, NDX_ENT0(rb), left, 0, ndx_upkey);
            put_le32(NDX_ENT(x, rb, 1) + NDX_E_LOWER, right);
            x->root = rp;
            x->hdirty = 1;
            return DBF_OK;
        }
        pg = ndx_ins[d].page;
        ci = ndx_ins[d].idx;
        rc = page_get(x, pg, &b);
        if (rc != DBF_OK)
            return rc;
        n = pg_count(b);
        if (n < x->kpp) {
            memmove(NDX_ENT(x, b, ci + 1u), NDX_ENT(x, b, ci),
                    (size_t)(n - ci) * x->entsz + NDX_PTRSIZE);
            put_entry(x, NDX_ENT(x, b, ci), left, 0, ndx_upkey);
            put_le32(NDX_ENT(x, b, ci + 1u) + NDX_E_LOWER, right);
            pg_setcount(b, (u16)(n + 1u));
            page_dirty(x, pg);
            return DBF_OK;
        }

        /* Full: n + 1 keys and n + 2 children in scratch; the lower m
           keys stay, key m goes up, the rest move to a new page. */
        s = ndx_scratch;
        memcpy(NDX_ENT0(s), NDX_ENT0(b), (size_t)ci * x->entsz);
        put_entry(x, NDX_ENT(x, s, ci), left, 0, ndx_upkey);
        memcpy(NDX_ENT(x, s, ci + 1u), NDX_ENT(x, b, ci),
               (size_t)(n - ci) * x->entsz + NDX_PTRSIZE);
        put_le32(NDX_ENT(x, s, ci + 1u) + NDX_E_LOWER, right);
        m = (u16)((n + 1u) / 2u);
        last = get_le32(NDX_ENT(x, s, n + 1u) + NDX_E_LOWER);

        rc = page_new(x, &rp, &rb);
        if (rc != DBF_OK)
            return rc;
        fill_page(x, rb, (u16)(m + 1u), (u16)(n - m), 1, last);
        rc = page_get(x, pg, &b);
        if (rc != DBF_OK)
            return rc;
        fill_page(x, b, 0, m, 1,
                  get_le32(NDX_ENT(x, s, m) + NDX_E_LOWER));
        page_dirty(x, pg);
        memcpy(ndx_upkey, NDX_ENT(x, s, m) + NDX_E_KEY, x->keylen);
        left = pg;
        right = rp;
        d--;
    }
}

/* Insert (key, rec) into the leaf at ndx_ins, splitting if it is full. */
static int insert_leaf(ndx *x, const u8 *key, u32 rec)
{
    u32 pg;
    u32 rp;
    u16 leafd;
    u16 idx;
    u16 n;
    u16 m;
    u8 *b;
    u8 *rb;
    u8 *s;
    int rc;

    leafd = (u16)(ndx_idepth - 1u);
    pg = ndx_ins[leafd].page;
    idx = ndx_ins[leafd].idx;
    rc = page_get(x, pg, &b);
    if (rc != DBF_OK)
        return rc;
    n = pg_count(b);
    if (idx > n)
        return DBF_EFORMAT;
    s = ndx_scratch;
    m = 0;
    rp = 0;
    if (n == x->kpp) {
        /* Full: n + 1 entries in scratch, the lower half stays.  The
           upper half goes first to a new page that no page links to
           yet, so that a failure there leaves the tree as it was. */
        memcpy(NDX_ENT0(s), NDX_ENT0(b), (size_t)idx * x->entsz);
        put_entry(x, NDX_ENT(x, s, idx), 0, rec, key);
        memcpy(NDX_ENT(x, s, idx + 1u), NDX_ENT(x, b, idx),
               (size_t)(n - idx) * x->entsz);
        m = (u16)((n + 1u) / 2u);
        rc = page_new(x, &rp, &rb);
        if (rc != DBF_OK)
            return rc;
        fill_page(x, rb, m, (u16)(n + 1u - m), 0, 0);
    }
    if (idx == n) {
        /* The new entry becomes the leaf's highest: the keys above are
           raised before pg changes.  After a split, insert_up moves the
           raised parent entry of pg, key and all, to the new page. */
        rc = raise_max(x, ndx_ins, ndx_idepth, key);
        if (rc != DBF_OK)
            return rc;
    }
    rc = page_get(x, pg, &b);
    if (rc != DBF_OK)
        return rc;
    if (n < x->kpp) {
        memmove(NDX_ENT(x, b, idx + 1u), NDX_ENT(x, b, idx),
                (size_t)(n - idx) * x->entsz);
        put_entry(x, NDX_ENT(x, b, idx), 0, rec, key);
        pg_setcount(b, (u16)(n + 1u));
        page_dirty(x, pg);
        return DBF_OK;
    }
    fill_page(x, b, 0, m, 0, 0);
    page_dirty(x, pg);
    memcpy(ndx_upkey, NDX_ENT(x, s, m - 1u) + NDX_E_KEY, x->keylen);
    return insert_up(x, (int)leafd - 1, pg, rp);
}

/* DBF_EFULL when an insert could need more pages than the format allows,
   or would grow the path to the insertion point past NDX_MAXDEPTH
   levels; checked before anything is changed.  Trees need about
   log(entries) / log(keys per page) levels, far below the cap. */
static int room_check(ndx *x)
{
    u16 d;
    u8 *b;
    int rc;

    if (NDX_MAXPAGE - x->npages < (u32)ndx_idepth + 1u)
        return DBF_EFULL;
    if (ndx_idepth < NDX_MAXDEPTH)
        return DBF_OK;
    for (d = 0; d < ndx_idepth; d++) {
        rc = page_get(x, ndx_ins[d].page, &b);
        if (rc != DBF_OK)
            return rc;
        if (pg_count(b) < x->kpp)
            return DBF_OK;
    }
    return DBF_EFULL;
}

/* Remove the entry at the leaf of stk[], keeping I2 and I3. */
static int remove_entry(ndx *x)
{
    u32 pg;
    u16 d;
    u16 idx;
    u16 n;
    u8 *b;
    int rc;

    d = (u16)(x->depth - 1u);
    pg = x->stk[d].page;
    idx = x->stk[d].idx;
    rc = page_get(x, pg, &b);
    if (rc != DBF_OK)
        return rc;
    n = pg_count(b);
    memmove(NDX_ENT(x, b, idx), NDX_ENT(x, b, idx + 1u),
            (size_t)(n - idx - 1u) * x->entsz);
    memset(NDX_ENT(x, b, n - 1u), 0, x->entsz);
    n--;
    pg_setcount(b, n);
    page_dirty(x, pg);
    if (n > 0) {
        if (idx < n)
            return DBF_OK;
        /* The highest entry went: lower the keys above. */
        memcpy(ndx_upkey, NDX_ENT(x, b, n - 1u) + NDX_E_KEY, x->keylen);
        return raise_max(x, x->stk, x->depth, ndx_upkey);
    }

    /* The page at level d is empty: unlink it from its parent. */
    while (d > 0) {
        d--;
        pg = x->stk[d].page;
        idx = x->stk[d].idx;
        rc = page_get(x, pg, &b);
        if (rc != DBF_OK)
            return rc;
        n = pg_count(b);
        if (idx < n) {
            /* A keyed child: drop its entry, the highest key stays. */
            memmove(NDX_ENT(x, b, idx), NDX_ENT(x, b, idx + 1u),
                    (size_t)(n - idx - 1u) * x->entsz + NDX_PTRSIZE);
            memset(NDX_ENT(x, b, n - 1u) + NDX_PTRSIZE, 0, x->entsz);
            pg_setcount(b, (u16)(n - 1u));
            page_dirty(x, pg);
            return DBF_OK;
        }
        if (n > 0) {
            /* The rightmost child: the previous one takes the slot, and
               its key becomes the page's highest. */
            memcpy(ndx_upkey, NDX_ENT(x, b, n - 1u) + NDX_E_KEY, x->keylen);
            memset(NDX_ENT(x, b, n - 1u) + NDX_PTRSIZE, 0, x->entsz);
            pg_setcount(b, (u16)(n - 1u));
            page_dirty(x, pg);
            return raise_max(x, x->stk, (u16)(d + 1u), ndx_upkey);
        }
        /* The only child: this page is empty too.  An empty root
           becomes an empty leaf. */
        if (d == 0) {
            memset(b, 0, NDX_PAGE);
            page_dirty(x, pg);
        }
    }
    return DBF_OK;
}

/* ---------------------------------------------------------------------
 * Public functions.
 * --------------------------------------------------------------------- */

int ndx_create(const char *path, const char *expr, u16 keylen)
{
    u8 *buf;
    size_t len;
    u16 entsz;
    int h;
    int rc;
    int rc2;

    if (path == NULL || expr == NULL)
        return DBF_EARG;
    len = strlen(expr);
    if (len < 1 || len > NDX_MAXEXPR || keylen < 1 || keylen > NDX_MAXKEY)
        return DBF_EARG;
    buf = (u8 *)malloc(NDX_PAGE);
    if (buf == NULL)
        return DBF_ENOMEM;
    entsz = (u16)NDX_ENTSZ(keylen);
    memset(buf, 0, NDX_PAGE);
    put_le32(buf + NDX_H_ROOT, 1);
    put_le32(buf + NDX_H_PAGES, 2);
    put_le16(buf + NDX_H_KEYLEN, keylen);
    put_le16(buf + NDX_H_KPP, (u16)NDX_KPP(entsz));
    put_le16(buf + NDX_H_TYPE, NDX_TYPE_CHAR);
    put_le16(buf + NDX_H_ENTSZ, entsz);
    memcpy(buf + NDX_H_EXPR, expr, len);

    rc = cf_create(path, &h);
    if (rc != 0) {
        free(buf);
        return dbf_map_cf(rc);
    }
    rc = cf_write(h, buf, NDX_PAGE);
    if (rc == 0) {
        memset(buf, 0, NDX_PAGE);
        rc = cf_write(h, buf, NDX_PAGE);
    }
    if (rc == 0)
        rc = dos_commit_handle(h);
    rc2 = cf_close(h);
    if (rc == 0)
        rc = rc2;
    free(buf);
    if (rc != 0)
        cf_remove(path);
    return dbf_map_cf(rc);
}

/* Check the header in buf and fill in x from it, the expression
   included; page_get, from ndx_open, refuses a root outside the file's
   page count. */
static int read_header(ndx *x, const u8 *buf)
{
    u16 i;

    x->root = get_le32(buf + NDX_H_ROOT);
    x->npages = get_le32(buf + NDX_H_PAGES);
    x->keylen = get_le16(buf + NDX_H_KEYLEN);
    x->kpp = get_le16(buf + NDX_H_KPP);
    x->entsz = get_le16(buf + NDX_H_ENTSZ);
    x->unique = buf[NDX_H_UNIQUE];
    if (x->root == 0 || x->keylen == 0 || x->keylen > NDX_MAXKEY
        || get_le16(buf + NDX_H_TYPE) != NDX_TYPE_CHAR
        || x->entsz != NDX_ENTSZ(x->keylen)
        || x->kpp < NDX_MINKPP || x->kpp > (u16)NDX_KPP(x->entsz)
        || x->npages < 2 || x->npages > NDX_MAXPAGE)
        return DBF_EFORMAT;
    for (i = 0; i <= NDX_MAXEXPR; i++)
        if (buf[NDX_H_EXPR + i] == 0)
            break;
    if (i > NDX_MAXEXPR)
        return DBF_EFORMAT;
    x->exprlen = (u8)i;
    while (i > 0 && buf[NDX_H_EXPR + i - 1u] == ' ')
        i--;
    x->expr = (char *)malloc((size_t)i + 1u);
    if (x->expr == NULL)
        return DBF_ENOMEM;
    memcpy(x->expr, buf + NDX_H_EXPR, i);
    x->expr[i] = '\0';
    return DBF_OK;
}

int ndx_open(ndx **out, const char *path, int mode)
{
    ndx *x;
    ndx_slot *s;
    u32 size;
    u32 end;
    u8 *b;
    u16 got;
    int rc;

    if (out == NULL)
        return DBF_EARG;
    *out = NULL;
    if (path == NULL)
        return DBF_EARG;

    x = (ndx *)malloc(sizeof(ndx));
    if (x == NULL)
        return DBF_ENOMEM;
    memset(x, 0, sizeof(ndx));
    rc = pool_acquire();
    if (rc != DBF_OK) {
        free(x);
        return rc;
    }
    x->mode = (u8)mode;

    /* The header is read into a pool slot that is left free: read_header
       takes everything it needs from it.  The page count is then the
       larger of the header's and the file size's, and the root page, which
       may reuse the slot, is fetched; page_get refuses one outside. */
    rc = dbf_cf_open(path, mode, &x->h);
    if (rc == DBF_OK) {
        rc = victim(&s);
        if (rc == DBF_OK)
            rc = dbf_map_cf(cf_read(x->h, s->buf, NDX_PAGE, &got));
        if (rc == DBF_OK && got != NDX_PAGE)
            rc = DBF_EFORMAT;
        if (rc == DBF_OK)
            rc = read_header(x, s->buf);
        if (rc == DBF_OK)
            rc = dbf_map_cf(cf_size(x->h, &size));
        if (rc == DBF_OK) {
            end = size / (u32)NDX_PAGE;
            if (size % (u32)NDX_PAGE != 0)
                end++;
            if (end > NDX_MAXPAGE)
                end = NDX_MAXPAGE;
            if (end > x->npages) {
                x->npages = end;
                x->hdirty = (u8)(x->mode == DBF_RDWR);
            }
            x->cpages = x->npages;
            start_op(x);
            rc = page_get(x, x->root, &b);
        }
        if (rc != DBF_OK)
            cf_close(x->h);
    }
    if (rc != DBF_OK) {
        pool_release(x);
        free(x->expr);
        free(x);
        return rc;
    }
    *out = x;
    return DBF_OK;
}

int ndx_flush(ndx *x)
{
    u8 buf[NDX_H_COUNTS];
    int i;
    int rc;

    if (x->mode != DBF_RDWR)
        return DBF_ERDONLY;
    for (i = 0; i < NDX_NPOOL; i++) {
        if (ndx_pool[i].owner == x && ndx_pool[i].dirty) {
            rc = write_slot(&ndx_pool[i]);
            if (rc != DBF_OK)
                return rc;
        }
    }
    /* Nothing written since the last commit: no commit either, so the
       file time stays as it was. */
    if (!x->hdirty && !x->wrote)
        return DBF_OK;
    /* Pages added: their size first, then the header that counts them. */
    if (x->npages > x->cpages) {
        rc = dbf_map_cf(dos_commit_handle(x->h));
        if (rc != DBF_OK)
            return rc;
    }
    if (x->hdirty) {
        put_le32(buf + NDX_H_ROOT, x->root);
        put_le32(buf + NDX_H_PAGES, x->npages);
        rc = cf_seek(x->h, 0);
        if (rc == 0)
            rc = cf_write(x->h, buf, NDX_H_COUNTS);
        if (rc != 0)
            return dbf_map_cf(rc);
        x->hdirty = 0;
        x->wrote = 1;
    }
    rc = dbf_map_cf(dos_commit_handle(x->h));
    if (rc != DBF_OK)
        return rc;
    x->cpages = x->npages;
    x->wrote = 0;
    return DBF_OK;
}

int ndx_close(ndx *x)
{
    int i;
    int dirty;
    int rc;
    int rc2;

    rc = DBF_OK;
    dirty = x->hdirty;
    for (i = 0; i < NDX_NPOOL; i++)
        if (ndx_pool[i].owner == x && ndx_pool[i].dirty)
            dirty = 1;
    if (x->mode == DBF_RDWR && dirty)
        rc = ndx_flush(x);
    rc2 = dbf_map_cf(cf_close(x->h));
    if (rc == DBF_OK)
        rc = rc2;
    pool_release(x);
    free(x->expr);
    free(x);
    return rc;
}

u16 ndx_keylen(ndx *x)
{
    return x->keylen;
}

int ndx_unique(ndx *x)
{
    return x->unique != 0;
}

const char *ndx_expr(ndx *x)
{
    return x->expr;
}

u16 ndx_exprlen(ndx *x)
{
    return x->exprlen;
}

int ndx_insert(ndx *x, const u8 *key, u32 recno)
{
    int rc;

    if (x == NULL || key == NULL || recno == 0)
        return DBF_EARG;
    if (x->mode != DBF_RDWR)
        return DBF_ERDONLY;
    start_op(x);
    rc = locate(x, key, recno);
    if (rc == DBF_OK && x->state == NDX_ON &&
        cmp_pair(x, x->curkey, x->currec, key, recno) == 0)
        rc = DBF_EARG;
    if (rc == DBF_OK)
        rc = room_check(x);
    if (rc == DBF_OK) {
        x->state = NDX_BOF;
        rc = insert_leaf(x, key, recno);
    }
    if (rc != DBF_OK) {
        x->state = NDX_BOF;
        return rc;
    }
    memcpy(x->curkey, key, x->keylen);
    x->currec = recno;
    x->state = NDX_AT;
    return DBF_OK;
}

int ndx_delete(ndx *x, const u8 *key, u32 recno)
{
    int rc;

    if (x == NULL || key == NULL)
        return DBF_EARG;
    if (x->mode != DBF_RDWR)
        return DBF_ERDONLY;
    start_op(x);
    rc = locate(x, key, recno);
    if (rc == DBF_OK && (x->state != NDX_ON ||
        cmp_pair(x, x->curkey, x->currec, key, recno) != 0))
        return DBF_ERANGE;
    if (rc == DBF_OK) {
        x->state = NDX_BOF;
        rc = remove_entry(x);
    }
    /* The entry that followed is the first one above (key, recno), which
       curkey and currec still hold. */
    x->state = (u8)(rc == DBF_OK ? NDX_AT : NDX_BOF);
    return rc;
}

/* Position on the first entry >= (key, rec); when there is one, *recno
   (zeroed by the caller) receives its record number. */
static int seek_pair(ndx *x, const u8 *key, u32 rec, u32 *recno)
{
    int rc;

    start_op(x);
    rc = locate(x, key, rec);
    if (rc == DBF_OK && x->state == NDX_ON)
        *recno = x->currec;
    return rc;
}

int ndx_seek(ndx *x, const u8 *key, u16 len, u32 *recno, int *exact)
{
    int rc;

    if (recno != NULL)
        *recno = 0;
    if (exact != NULL)
        *exact = 0;
    if (x == NULL || recno == NULL || exact == NULL ||
        (key == NULL && len != 0) || len > x->keylen)
        return DBF_EARG;
    /* The first key starting at or above the prefix is the first at or
       above the prefix padded with zero bytes. */
    memset(ndx_upkey, 0, x->keylen);
    if (len != 0)
        memcpy(ndx_upkey, key, len);
    rc = seek_pair(x, ndx_upkey, 0, recno);
    if (*recno != 0 && (len == 0 || memcmp(x->curkey, key, len) == 0))
        *exact = 1;
    return rc;
}

int ndx_first(ndx *x, u32 *recno)
{
    return edge(x, 0, recno);
}

int ndx_last(ndx *x, u32 *recno)
{
    return edge(x, 1, recno);
}

/* Step one entry forward (dir > 0) or back; the new entry must lie
   beyond the current one in that direction. */
static int step(ndx *x, int dir, u32 *recno)
{
    u32 oldrec;
    int found;
    int c;
    int rc;

    if (recno != NULL)
        *recno = 0;
    if (x == NULL || recno == NULL)
        return DBF_EARG;
    rc = locate_at(x);
    if (rc != DBF_OK)
        return rc;
    if (x->state == NDX_BOF)
        return dir > 0 ? edge(x, 0, recno) : DBF_OK;
    if (x->state == NDX_EOF)
        return dir > 0 ? DBF_OK : edge(x, 1, recno);
    start_op(x);
    memcpy(ndx_tmpkey, x->curkey, x->keylen);
    oldrec = x->currec;
    found = 0;
    if (dir > 0) {
        x->stk[x->depth - 1].idx++;
        rc = walk_fwd(x, &found);
    } else {
        rc = walk_back(x, &found);
    }
    if (rc == DBF_OK && !found) {
        x->state = dir > 0 ? NDX_EOF : NDX_BOF;
        return DBF_OK;
    }
    if (rc == DBF_OK)
        rc = settle(x, 1, recno);
    if (rc == DBF_OK) {
        c = cmp_pair(x, x->curkey, x->currec, ndx_tmpkey, oldrec);
        if (dir > 0 ? c <= 0 : c >= 0)
            rc = DBF_EFORMAT;
    }
    if (rc != DBF_OK) {
        x->state = NDX_BOF;
        *recno = 0;
    }
    return rc;
}

int ndx_next(ndx *x, u32 *recno)
{
    return step(x, 1, recno);
}

int ndx_prev(ndx *x, u32 *recno)
{
    return step(x, -1, recno);
}

int ndx_seek_after(ndx *x, const u8 *key, u32 rec, u32 *recno)
{
    int rc;

    if (recno != NULL)
        *recno = 0;
    if (x == NULL || key == NULL || recno == NULL)
        return DBF_EARG;
    rc = seek_pair(x, key, rec, recno);
    if (*recno != 0 && *recno == rec &&
        memcmp(x->curkey, key, x->keylen) == 0)
        rc = ndx_next(x, recno);
    return rc;
}

int ndx_key(ndx *x, u8 *out)
{
    int rc;

    if (x == NULL || out == NULL)
        return DBF_EARG;
    rc = locate_at(x);
    if (rc != DBF_OK)
        return rc;
    if (x->state != NDX_ON)
        return DBF_ERANGE;
    memcpy(out, x->curkey, x->keylen);
    return DBF_OK;
}
