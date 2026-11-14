/*
 * ASSY.C - assemblies: the PARENT_ID tree of ITEMS and its rules.
 *
 * ITEMS3 (STR(PARENT_ID,8)+STR(ITEM_ID,8)) holds one entry per item,
 * deleted ones included until PACK, so the entries whose first
 * ENG_IDLEN key bytes are an item's ID text are that item's parts in ID
 * order.  Every walk here steps through ITEMS3 by seeking: the first
 * part of item X is the first entry at or above STR(X,8) that still
 * starts with it, and the part after Y under X the first entry above
 * STR(X,8)+STR(Y,8).  A walk therefore keeps no index position between
 * its steps (only IDs), and edits made between steps cannot disturb it.
 * The item IDs come from the keys; records are read only where their
 * fields or deleted mark are needed.
 *
 * A change (INSTALL, REMOVE, MOVE) makes every check and collects the
 * record numbers of the parts to rewrite before its first write; then
 * it writes them in one batch naming ITEMS, one commit per item, the
 * parts first and the item named last.  The counts of INSTALL go down
 * into deleted parts and their parts (the size limit counts entries,
 * deleted ones included); the location upkeep skips deleted parts,
 * which take their parent's location when undeleted (ITEMS.C).
 *
 * A lookup that fails keeps the text ITEMS.C or LOCS.C recorded and is
 * renamed to the argument it concerns.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "ENGFLD.H"
#include "ENGTX.H"
#include "ENGID.H"
#include "ENGEDIT.H"
#include "ENGLOCS.H"
#include "ENGITEM.H"
#include "ENGASSY.H"
#include "ENGLOAN.H"

#define F_ITEMID    ENG_FIELD(ITEMS, ITEM_ID)
#define F_LOCID     ENG_FIELD(ITEMS, LOC_ID)
#define F_PARENT    ENG_FIELD(ITEMS, PARENT_ID)
#define F_UPDATED   ENG_FIELD(ITEMS, UPDATED)

/* Argument names the failures give (one copy each: Turbo C does not
   merge equal string literals). */
static const char k_child[] = "CHILD";
static const char k_parent[] = "PARENT";
static const char k_id[] = "ID";
static const char k_loc[] = "LOC";

/* eng_walk.state besides 0 (not started) and a failed walk's ENG_E
   code (ENG_ESYNTAX or above). */
#define W_LIVE  1               /* on; for BOM the last entry examined
                                   was live (or none was): its parts
                                   come next                            */
#define W_DEAD  2               /* BOM only: the last entry examined was
                                   a deleted part, whose parts are
                                   passed over                          */
#define W_END   3               /* every entry examined                 */

/* par_of's PARENT_ID that names no item. */
#define BADPAR  0xFFFFFFFFUL

/* Outcomes of chain() besides an ENG_E code. */
#define CH_OK       0
#define CH_LONG     1           /* more than max items                  */
#define CH_BROKEN   2           /* an item missing or deleted           */
#define CH_HIT      3           /* the watched ID is in the chain       */

/* ---------------------------------------------------------------------
 * Fields and keys.
 * --------------------------------------------------------------------- */

/* ITEM_ID of the ITEMS buffer, 0 when blank, bad or negative. */
static u32 item_id(eng *e)
{
    i32 v;

    v = eng_rec_num(e, ENG_T_ITEMS, F_ITEMID);
    return v > 0 ? (u32)v : 0;
}

/* PARENT_ID of the ITEMS buffer: 0 for a top-level item (a blank
   field included), else the parent's ID, or BADPAR when the field is
   negative or not a number (written by another program).  BADPAR names
   no item, so a chain through it is broken. */
static u32 par_of(eng *e)
{
    i32 v;
    int blank;

    if (dbf_get_num(eng_dbf(e, ENG_T_ITEMS), F_PARENT, &v, &blank) !=
        DBF_OK || v < 0)
        return BADPAR;
    return (u32)v;
}

/* STR(a,8)+STR(b,8) into k (ENG_ITEMS3LEN bytes; no NUL), a and b at
   most ENG_MAXID; a seek on the first ENG_IDLEN bytes alone ignores b. */
static void mkkey(u32 a, u32 b, u8 *k)
{
    txt_u32w((char *)k, a, ENG_IDLEN, ' ');
    txt_u32w((char *)k + ENG_IDLEN, b, ENG_IDLEN, ' ');
}

/* The LOC_ID of the ITEMS buffer into *loc (blank as 0, unknown); 0
   when it is negative or not a number (written by another program). */
static int top_loc(eng *e, u32 *loc)
{
    i32 v;
    int blank;

    if (dbf_get_num(eng_dbf(e, ENG_T_ITEMS), F_LOCID, &v, &blank) !=
        DBF_OK || v < 0)
        return 0;
    *loc = (u32)v;
    return 1;
}

/* The LOC_ID of the location token tok (ENGLOCS.H) into *loc, 0 for
   unknown; ENG_EARG naming LOC when the token is empty or all spaces (a
   quoted protocol value may be), ENG_ECONSTR naming LOC when it names no
   live location. */
static int get_loc(eng *e, const char *tok, u32 *loc)
{
    const char *c;
    int rc;

    c = tok;
    if (c != NULL)
        while (*c == ' ')
            c++;
    if (c == NULL || *c == '\0')
        return eng_fail(e, ENG_EARG, k_loc, ENG_TX_NONE);
    rc = eng_loc_id(e, tok, loc);
    if (rc == ENG_ENOTFOUND)
        return eng_fail(e, ENG_ECONSTR, k_loc, ENG_TX_NOLOC);
    return rc;
}

/* ---------------------------------------------------------------------
 * ITEMS3 steps.
 * --------------------------------------------------------------------- */

/* Position ITEMS3 on the first entry whose key starts at or above the
   len bytes at k (past 0), or is above them (past 1: entries equal to
   them are stepped over, uncounted; such a run is one entry long while
   IDs are unique).  *r receives its record, 0 for none; cur its key. */
static int ix3_seek(eng *e, const u8 *k, u16 len, int past, u32 *r,
                    u8 *cur)
{
    ndx *x;
    int exact;
    int rc;

    x = eng_ndx(e, ENG_X_ITEMS3);
    rc = ndx_seek(x, k, len, r, &exact);
    while (rc == DBF_OK && *r != 0) {
        rc = ndx_key(x, cur);
        if (rc != DBF_OK || !past || memcmp(cur, k, len) != 0)
            break;
        rc = ndx_next(x, r);
    }
    return eng_dbferr(e, rc, 1);
}

/* The entry of item node's first part (after 0) or of its part after
   item after: *r its record (0 when there is none), *id the part's ID,
   cur its key.  A key whose ID cannot be read is ENG_EIO. */
static int part(eng *e, u32 node, u32 after, u32 *id, u32 *r, u8 *cur)
{
    u8 k[ENG_ITEMS3LEN];
    int rc;

    *id = 0;
    mkkey(node, after, k);
    rc = ix3_seek(e, k,
                  after == 0 ? (u16)ENG_IDLEN : (u16)ENG_ITEMS3LEN,
                  after != 0, r, cur);
    if (rc != ENG_OK || *r == 0)
        return rc;
    if (memcmp(cur, k, ENG_IDLEN) != 0) {
        *r = 0;
        return ENG_OK;
    }
    *id = eng_key_id(cur + ENG_IDLEN, ENG_IDLEN);
    if (*id == 0)
        return eng_fail(e, ENG_EIO, NULL, ENG_TX_NONE);
    return ENG_OK;
}

/* The ITEMS1 entry of item id, live or deleted, loaded into the
   buffer; *r is 0 when there is none. */
static int any_item(eng *e, u32 id, u32 *r)
{
    char k[ENG_IDLEN + 1];
    int exact;
    int rc;

    *r = 0;
    if (!eng_id_key(id, k))
        return ENG_OK;
    rc = eng_dbferr(e, ndx_seek(eng_ndx(e, ENG_X_ITEMS1), (const u8 *)k,
                                ENG_IDLEN, r, &exact), 1);
    if (rc != ENG_OK || *r == 0)
        return rc;
    if (!exact) {
        *r = 0;
        return ENG_OK;
    }
    return eng_rec_read(e, ENG_T_ITEMS, *r);
}

/* Walk up from the live item id through PARENT_ID, reading at most max
   items, id itself included: *n receives the count read, recs (NULL, or
   max entries) their records from id up, and *out CH_OK when the last
   one is top-level (it is then in the buffer and *top is its record),
   CH_LONG when max items left the top unreached (also for a PARENT_ID
   cycle), CH_BROKEN for an item that is missing or deleted or whose
   PARENT_ID is BADPAR, CH_HIT when an item read is watch. */
static int chain(eng *e, u32 id, u16 max, u32 watch, u32 *recs, u16 *n,
                 u32 *top, int *out)
{
    u32 r;
    int rc;

    *n = 0;
    *top = 0;
    *out = CH_LONG;
    while (*n < max) {
        if (id == watch) {
            *out = CH_HIT;
            return ENG_OK;
        }
        rc = eng_item_seek(e, id, &r);
        if (rc != ENG_OK)
            return rc;
        if (r == 0) {
            *out = CH_BROKEN;
            return ENG_OK;
        }
        if (recs != NULL)
            recs[*n] = r;
        (*n)++;
        id = par_of(e);
        if (id == 0) {
            *top = r;
            *out = CH_OK;
            return ENG_OK;
        }
    }
    return ENG_OK;
}

/* Visit every ITEMS3 entry below item top, deleted parts and their own
   parts included, depth first.  *count receives the entries met plus
   one for top, and *tx 0, or ENG_TX_BIG once the count passes limit, or
   ENG_TX_DEEP for a part below level maxh (top being at level 1); the
   walk stops at either, so it reads at most limit entries.  With loc not
   NULL each entry's record is read, and the records of the live parts
   whose LOC_ID differs from *loc go into recs (limit - 1 entries at
   most), *nrec counting them.  An entry naming top again, which only a
   PARENT_ID cycle written by another program makes, is ENG_EIO. */
static int tree(eng *e, u32 top, u16 maxh, u16 limit, const u32 *loc,
                u32 *recs, u16 *nrec, u16 *count, u16 *tx)
{
    u8 cur[ENG_ITEMS3LEN];
    u32 ids[ENG_MAXDEPTH];
    u32 after;
    u32 id;
    u32 r;
    u16 d;
    int rc;

    after = 0;
    d = 0;
    *count = 1;
    *nrec = 0;
    *tx = 0;
    if (limit < 1) {
        *tx = ENG_TX_BIG;
        return ENG_OK;
    }
    for (;;) {
        rc = part(e, d == 0 ? top : ids[d - 1], after, &id, &r, cur);
        if (rc != ENG_OK)
            return rc;
        if (r == 0) {
            if (d == 0)
                return ENG_OK;
            after = ids[--d];
            continue;
        }
        if (id == top)
            return eng_fail(e, ENG_EIO, NULL, ENG_TX_NONE);
        if (++*count > limit) {
            *tx = ENG_TX_BIG;
            return ENG_OK;
        }
        if (d + 2 > maxh) {
            *tx = ENG_TX_DEEP;
            return ENG_OK;
        }
        if (loc != NULL) {
            rc = eng_rec_read(e, ENG_T_ITEMS, r);
            if (rc != ENG_OK)
                return rc;
            if (!dbf_is_deleted(eng_dbf(e, ENG_T_ITEMS)) &&
                (u32)eng_rec_num(e, ENG_T_ITEMS, F_LOCID) != *loc)
                recs[(*nrec)++] = r;
        }
        ids[d++] = id;
        after = 0;
    }
}

/* ---------------------------------------------------------------------
 * Writes.
 * --------------------------------------------------------------------- */

/* One commit of ITEMS record r: LOC_ID loc, PARENT_ID par when setpar
   is 1, UPDATED today (the batch's date).  Unchanged fields give no
   audit row. */
static int put_item(eng *e, u32 r, u32 loc, int setpar, u32 par,
                    const char *today)
{
    u32 id;
    int rc;

    rc = eng_edit_begin(e, ENG_T_ITEMS, r);
    if (rc != ENG_OK)
        return rc;
    id = item_id(e);
    if (setpar)
        rc = eng_edit_putnumf(e, F_PARENT, (i32)par);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_LOCID, (i32)loc);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_UPDATED, today);
    return eng_edit_finish(e, rc, id);
}

/* The repair's commit of ITEMS record r: LOC_ID loc, UPDATED today,
   the pass's date (ENG_DATELEN + 1 bytes), which the first commit of
   the pass reads from the clock while it is "".  Its M row, the old and
   new text as a commit gives them, is written and AUDIT.DBF and its
   indexes flushed first; the commit then writes no row
   (eng_edit_quiet).  So a stop leaves the row without the record, which
   the next open's repair writes again with the same row, but never the
   record without its row. */
static int heal_put(eng *e, u32 r, u32 loc, char *today)
{
    char was[ENG_AUDVAL + 1];
    u32 id;
    int rc;

    rc = eng_edit_begin(e, ENG_T_ITEMS, r);
    if (rc != ENG_OK)
        return rc;
    id = item_id(e);
    if (today[0] == '\0')
        eng_today(e, today);
    eng_rec_str(e, ENG_T_ITEMS, F_LOCID, was, (u16)sizeof was);
    rc = eng_edit_putnumf(e, F_LOCID, (i32)loc);
    if (rc == ENG_OK)
        rc = eng_edit_putf(e, F_UPDATED, today);
    if (rc != ENG_OK)
        return eng_edit_finish(e, rc, id);
    return eng_edit_heal(e, F_LOCID, was, ENG_A_MOVE, id);
}

/* The batch of a change: records recs[0..n-1] get loc, then record last
   (0: none) gets loc and, when setpar is 1, PARENT_ID par; all of them
   one UPDATED date. */
static int write_all(eng *e, const u32 *recs, u16 n, u32 last, u32 loc,
                     int setpar, u32 par)
{
    char today[ENG_DATELEN + 1];
    u16 i;
    int rc;

    rc = eng_batch_begin(e, ENG_T_ITEMS);
    if (rc != ENG_OK)
        return rc;
    eng_today(e, today);
    for (i = 0; i < n && rc == ENG_OK; i++)
        rc = put_item(e, recs[i], loc, 0, 0, today);
    if (rc == ENG_OK && last != 0)
        rc = put_item(e, last, loc, setpar, par, today);
    return eng_batch_end(e, rc);
}

/* ---------------------------------------------------------------------
 * INSTALL, REMOVE, MOVE.
 * --------------------------------------------------------------------- */

int eng_assy_install(eng *e, u32 child, u32 parent)
{
    u32 recs[ENG_MAXASSY];
    u32 loc;
    u32 crec;
    u32 prec;
    u32 root;
    u32 croot;
    u32 oldpar;
    u16 level;
    u16 n;
    u16 nrec;
    u16 ccount;
    u16 rcount;
    u16 tx;
    u16 f;
    int out;
    int rc;

    rc = eng_item_need(e, child, k_child, &crec);
    if (rc != ENG_OK)
        return rc;
    oldpar = par_of(e);
    if (eng_item_held(e) > 0)
        return eng_fail(e, ENG_ECONSTR, k_child, eng_item_hold(e, &f));
    rc = eng_item_need(e, parent, k_parent, &prec);
    if (rc != ENG_OK || oldpar == parent)
        return rc;

    /* The parent's ancestors: no cycle, and room below the parent. */
    rc = chain(e, parent, ENG_MAXDEPTH - 1, child, NULL, &level, &root,
               &out);
    if (rc != ENG_OK)
        return rc;
    if (out == CH_HIT)
        return eng_fail(e, ENG_ECONSTR, k_parent, ENG_TX_CYCLE);
    if (out == CH_LONG)
        return eng_fail(e, ENG_ECONSTR, k_parent, ENG_TX_DEEP);
    if (out == CH_BROKEN || !top_loc(e, &loc))
        return eng_fail(e, ENG_EIO, k_parent, ENG_TX_NONE);
    rc = eng_loan_hosted(e, root, crec, k_parent);
    if (rc != ENG_OK)
        return rc;

    /* The child's top-level item: moving within one assembly leaves its
       size as it is. */
    croot = crec;
    if (oldpar != 0) {
        rc = chain(e, child, ENG_MAXDEPTH, 0, NULL, &n, &croot, &out);
        if (rc != ENG_OK)
            return rc;
        if (out != CH_OK)
            return eng_fail(e, ENG_EIO, k_child, ENG_TX_NONE);
    }

    /* The child's parts: their height below the parent and their count;
       then the parent's assembly with them. */
    rc = tree(e, child, (u16)(ENG_MAXDEPTH - level), ENG_MAXASSY, &loc,
              recs, &nrec, &ccount, &tx);
    if (rc == ENG_OK && tx == 0 && croot != root)
        rc = tree(e, root, ENG_MAXDEPTH, (u16)(ENG_MAXASSY - ccount), NULL,
                  NULL, &n, &rcount, &tx);
    if (rc != ENG_OK)
        return rc;
    if (tx != 0)
        return eng_fail(e, ENG_ECONSTR, k_parent, tx);
    return write_all(e, recs, nrec, crec, loc, 1, parent);
}

int eng_assy_remove(eng *e, u32 child, const char *tok)
{
    u32 recs[ENG_MAXASSY];
    u32 loc;
    u32 crec;
    u16 nrec;
    u16 count;
    u16 tx;
    int rc;

    rc = eng_item_need(e, child, k_child, &crec);
    if (rc != ENG_OK)
        return rc;
    if (par_of(e) == 0)
        return eng_fail(e, ENG_ECONSTR, k_child, ENG_TX_NOTINST);
    rc = get_loc(e, tok, &loc);
    if (rc == ENG_OK)
        rc = tree(e, child, ENG_MAXDEPTH, ENG_MAXASSY, &loc, recs, &nrec,
                  &count, &tx);
    if (rc != ENG_OK)
        return rc;
    if (tx != 0)
        return eng_fail(e, ENG_ECONSTR, k_child, tx);
    return write_all(e, recs, nrec, crec, loc, 1, 0);
}

int eng_assy_move(eng *e, u32 id, const char *tok)
{
    u32 recs[ENG_MAXASSY];
    u32 loc;
    u32 old;
    u32 r;
    u16 nrec;
    u16 count;
    u16 tx;
    int rc;

    rc = eng_item_need(e, id, k_id, &r);
    if (rc != ENG_OK)
        return rc;
    if (par_of(e) != 0)
        return eng_fail(e, ENG_ECONSTR, k_id, ENG_TX_INST);
    old = (u32)eng_rec_num(e, ENG_T_ITEMS, F_LOCID);
    rc = get_loc(e, tok, &loc);
    if (rc == ENG_OK)
        rc = tree(e, id, ENG_MAXDEPTH, ENG_MAXASSY, &loc, recs, &nrec,
                  &count, &tx);
    if (rc != ENG_OK)
        return rc;
    if (tx != 0)
        return eng_fail(e, ENG_ECONSTR, k_id, tx);
    if (old == loc)
        r = 0;
    if (nrec == 0 && r == 0)
        return ENG_OK;
    return write_all(e, recs, nrec, r, loc, 0, 0);
}

/* ---------------------------------------------------------------------
 * PATH, CHILDREN.
 * --------------------------------------------------------------------- */

int eng_assy_path(eng *e, u32 id, u32 *recnos, u16 *n)
{
    u32 recs[ENG_MAXDEPTH];
    u32 r;
    u32 top;
    u16 m;
    u16 i;
    int out;
    int rc;

    *n = 0;
    rc = eng_item_need(e, id, k_id, &r);
    if (rc == ENG_OK)
        rc = chain(e, id, ENG_MAXDEPTH, 0, recs, &m, &top, &out);
    if (rc != ENG_OK)
        return rc;
    if (out != CH_OK)
        return eng_fail(e, ENG_EIO, NULL, ENG_TX_NONE);
    for (i = 1; i < m; i++)
        recnos[i - 1] = recs[m - i];
    *n = (u16)(m - 1);
    return ENG_OK;
}

int eng_assy_children(eng *e, eng_scan *s, u32 id, const char *after)
{
    u8 k[ENG_ITEMS3LEN];
    u32 r;
    int rc;

    rc = eng_item_need(e, id, k_id, &r);
    if (rc != ENG_OK)
        return rc;
    mkkey(id, 0, k);
    return eng_scan_open(e, s, ENG_T_ITEMS, ENG_X_ITEMS3, k, ENG_IDLEN, after);
}

/* ---------------------------------------------------------------------
 * Walks.
 * --------------------------------------------------------------------- */

/* The next BOM candidate after the walk's last entry, without counting:
   that entry's first part when it was live, else the part after it or
   after one of its ancestors.  *d receives the candidate's level, *r its
   record (0: the walk is over), *id its ID and cur its key.  A
   candidate that is the root itself is ENG_EIO (a PARENT_ID cycle). */
static int bom_cand(eng *e, eng_walk *w, u16 *d, u32 *id, u32 *r,
                    u8 *cur)
{
    u16 k;
    int rc;

    k = w->depth;
    rc = ENG_OK;
    *r = 0;
    if (w->state == W_LIVE && k < ENG_MAXDEPTH) {
        rc = part(e, k == 0 ? w->root : w->last[k - 1], 0, id, r, cur);
        *d = (u16)(k + 1);
    }
    for (; rc == ENG_OK && *r == 0 && k > 0; k--) {
        rc = part(e, k == 1 ? w->root : w->last[k - 2], w->last[k - 1],
                  id, r, cur);
        *d = k;
    }
    /* With one parent per item, a walk down from the root meets an item
       twice only through a PARENT_ID cycle, which passes the root. */
    if (rc == ENG_OK && *r != 0 && *id == w->root)
        rc = eng_fail(e, ENG_EIO, NULL, ENG_TX_NONE);
    return rc;
}

/* Position of a list walk: the first entry above the walk's key, or
   from parent 1 up when there is no key yet or it lies below that. */
static int list_cand(eng *e, eng_walk *w, u32 *r, u8 *cur)
{
    u8 k[ENG_ITEMS3LEN];

    mkkey(1, 0, k);
    if (w->recno == 0 || memcmp(w->key, k, ENG_IDLEN) < 0)
        return ix3_seek(e, k, ENG_IDLEN, 0, r, cur);
    return ix3_seek(e, w->key, ENG_ITEMS3LEN, 1, r, cur);
}

/* The walk failed with rc: every later step gives rc again. */
static int walk_fail(eng_walk *w, int rc)
{
    w->state = rc;
    return rc;
}

/* Rebuild a BOM walk at the cursor entry (key, recno): w->last from the
   ancestors of the cursor's item up to the root (read live or deleted),
   and the state from its record: when that is still the cursor's item,
   live and under the key's parent, its parts come next, else the entry
   after it.  ENG_ECURSOR when the item is not a part of root at any
   depth. */
static int bom_resume(eng *e, eng_walk *w, const u8 *key, u32 recno)
{
    u32 c;
    u32 p;
    u32 kp;
    u32 r;
    u32 t;
    u16 n;
    u16 i;
    int rc;

    n = 0;
    p = eng_key_id(key, ENG_IDLEN);
    c = eng_key_id(key + ENG_IDLEN, ENG_IDLEN);
    if (c == 0)
        return eng_fail(e, ENG_ECURSOR, NULL, ENG_TX_NONE);
    kp = p;
    for (;;) {
        if (n >= ENG_MAXDEPTH || p == 0)
            return eng_fail(e, ENG_ECURSOR, NULL, ENG_TX_NONE);
        w->last[n++] = c;
        if (p == w->root)
            break;
        rc = any_item(e, p, &r);
        if (rc != ENG_OK)
            return rc;
        if (r == 0)
            return eng_fail(e, ENG_ECURSOR, NULL, ENG_TX_NONE);
        c = p;
        p = par_of(e);
    }
    for (i = 0; i < n / 2; i++) {
        t = w->last[i];
        w->last[i] = w->last[n - 1 - i];
        w->last[n - 1 - i] = t;
    }
    w->depth = n;
    w->state = W_DEAD;
    if (recno <= eng_reccount(e, ENG_T_ITEMS)) {
        rc = eng_rec_read(e, ENG_T_ITEMS, recno);
        if (rc != ENG_OK)
            return rc;
        if (!dbf_is_deleted(eng_dbf(e, ENG_T_ITEMS)) &&
            item_id(e) == w->last[n - 1] &&
            par_of(e) == kp)
            w->state = W_LIVE;
    }
    return ENG_OK;
}

/* Start walk w, zeroed by the caller, of kind at the cursor text after
   (NULL or "": the beginning).  A list walk takes any ITEMS3 cursor and
   continues after its entry. */
static int walk_open(eng *e, eng_walk *w, int kind, u32 root,
                     const char *after)
{
    u8 key[ENG_ITEMS3LEN];
    u32 recno;
    int rc;

    w->kind = kind;
    w->root = root;
    if (after == NULL || after[0] == '\0') {
        w->state = W_LIVE;
        return ENG_OK;
    }
    rc = eng_cursor_parse(e, ENG_X_ITEMS3, after, key, &recno);
    if (rc != ENG_OK)
        return rc;
    if (kind == ENG_W_BOM) {
        rc = bom_resume(e, w, key, recno);
        if (rc != ENG_OK)
            return rc;
    } else
        w->state = W_LIVE;
    memcpy(w->key, key, ENG_ITEMS3LEN);
    w->recno = recno;
    return ENG_OK;
}

int eng_assy_bom(eng *e, eng_walk *w, u32 id, const char *after)
{
    u32 r;
    int rc;

    memset(w, 0, sizeof *w);
    rc = eng_item_need(e, id, k_id, &r);
    if (rc != ENG_OK)
        return rc;
    return walk_open(e, w, ENG_W_BOM, id, after);
}

int eng_assy_list(eng *e, eng_walk *w, int top, const char *after)
{
    memset(w, 0, sizeof *w);
    return walk_open(e, w, top ? ENG_W_LISTTOP : ENG_W_LIST, 0, after);
}

static int bom_next(eng *e, eng_walk *w, u32 *recno, u16 *level)
{
    u8 cur[ENG_ITEMS3LEN];
    u32 id;
    u32 r;
    u16 d;
    int rc;

    while (w->state == W_LIVE || w->state == W_DEAD) {
        rc = bom_cand(e, w, &d, &id, &r, cur);
        if (rc != ENG_OK)
            return walk_fail(w, rc);
        if (r == 0) {
            w->state = W_END;
            break;
        }
        if (w->reads >= ENG_MAXREADS)
            break;
        rc = eng_rec_read(e, ENG_T_ITEMS, r);
        if (rc != ENG_OK)
            return walk_fail(w, rc);
        w->reads++;
        w->depth = d;
        w->last[d - 1] = id;
        w->recno = r;
        memcpy(w->key, cur, ENG_ITEMS3LEN);
        if (dbf_is_deleted(eng_dbf(e, ENG_T_ITEMS))) {
            w->state = W_DEAD;
            continue;
        }
        w->state = W_LIVE;
        *recno = r;
        *level = d;
        return ENG_OK;
    }
    return ENG_OK;
}

static int list_next(eng *e, eng_walk *w, u32 *recno)
{
    u8 cur[ENG_ITEMS3LEN];
    u32 r;
    u32 p;
    u32 rp;
    int rc;

    while (w->state == W_LIVE) {
        rc = list_cand(e, w, &r, cur);
        if (rc != ENG_OK)
            return walk_fail(w, rc);
        if (r == 0) {
            w->state = W_END;
            break;
        }
        /* Room for the entry and the read of the item it may decide. */
        if (w->reads >= ENG_MAXREADS - 1)
            break;
        w->reads++;
        w->recno = r;
        memcpy(w->key, cur, ENG_ITEMS3LEN);
        /* An entry whose parent is no ID (negative, written by another
           program) belongs to no item: passed over. */
        p = eng_key_id(cur, ENG_IDLEN);
        if (p == 0)
            continue;
        rc = eng_rec_read(e, ENG_T_ITEMS, r);
        if (rc != ENG_OK)
            return walk_fail(w, rc);
        if (dbf_is_deleted(eng_dbf(e, ENG_T_ITEMS)))
            continue;
        /* A live part decides its item p. */
        mkkey(p, ENG_MAXID, w->key);
        rc = eng_item_seek(e, p, &rp);
        if (rc != ENG_OK)
            return walk_fail(w, rc);
        w->reads++;
        if (rp == 0 || (w->kind == ENG_W_LISTTOP && par_of(e) != 0))
            continue;
        *recno = rp;
        return ENG_OK;
    }
    return ENG_OK;
}

/* ENG_OK for a walk that may step on; a failed walk's code, or
   ENG_EINTERNAL for one never opened. */
static int walk_ok(eng *e, eng_walk *w)
{
    if (w->state == 0)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NONE);
    if (w->state >= ENG_ESYNTAX)
        return w->state;
    return ENG_OK;
}

int eng_walk_next(eng *e, eng_walk *w, u32 *recno, u16 *level)
{
    int rc;

    *recno = 0;
    *level = 0;
    rc = walk_ok(e, w);
    if (rc != ENG_OK)
        return rc;
    if (w->kind == ENG_W_BOM)
        return bom_next(e, w, recno, level);
    return list_next(e, w, recno);
}

int eng_walk_more(eng *e, eng_walk *w, int *more)
{
    u8 cur[ENG_ITEMS3LEN];
    u32 id;
    u32 r;
    u16 d;
    int rc;

    *more = 0;
    rc = walk_ok(e, w);
    if (rc != ENG_OK || w->state == W_END)
        return rc;
    if (w->kind == ENG_W_BOM)
        rc = bom_cand(e, w, &d, &id, &r, cur);
    else
        rc = list_cand(e, w, &r, cur);
    if (rc != ENG_OK)
        return walk_fail(w, rc);
    *more = r != 0;
    return ENG_OK;
}

void eng_walk_cursor(const eng_walk *w, char *out)
{
    out[0] = '\0';
    if (w->state == 0 || w->recno == 0)
        return;
    eng_cursor_text(ENG_X_ITEMS3, w->key, w->recno, out);
}

/* ---------------------------------------------------------------------
 * Repair.
 * --------------------------------------------------------------------- */

int eng_assy_heal(eng *e)
{
    char today[ENG_DATELEN + 1];
    u8 k[ENG_ITEMS3LEN];
    u8 cur[ENG_ITEMS3LEN];
    ndx *x;
    u32 r;
    u32 p;
    u32 top;
    u32 group;
    u32 loc;
    u16 n;
    int out;
    int rc;

    x = eng_ndx(e, ENG_X_ITEMS3);
    loc = 0;
    group = 0;
    out = CH_BROKEN;
    today[0] = '\0';
    mkkey(1, 0, k);
    rc = ix3_seek(e, k, ENG_IDLEN, 0, &r, cur);
    while (rc == ENG_OK && r != 0) {
        /* A group whose parent is no ID (negative, written by another
           program) has a broken chain. */
        p = eng_key_id(cur, ENG_IDLEN);
        if (p == 0)
            p = BADPAR;
        if (p != group) {
            group = p;
            out = CH_BROKEN;
            if (p != BADPAR)
                rc = chain(e, p, ENG_MAXDEPTH - 1, 0, NULL, &n, &top, &out);
            if (rc != ENG_OK)
                return rc;
            /* A top-level location that is not a number breaks the
               group too. */
            if (out == CH_OK && !top_loc(e, &loc))
                out = CH_BROKEN;
        }
        if (out == CH_OK) {
            rc = eng_rec_read(e, ENG_T_ITEMS, r);
            if (rc != ENG_OK)
                return rc;
            if (!dbf_is_deleted(eng_dbf(e, ENG_T_ITEMS)) &&
                (u32)eng_rec_num(e, ENG_T_ITEMS, F_LOCID) != loc) {
                /* LOC_ID is in no ITEMS3 key, so the commit leaves the
                   index and its position as they are. */
                rc = heal_put(e, r, loc, today);
                if (rc != ENG_OK)
                    return rc;
            }
        }
        rc = eng_dbferr(e, ndx_next(x, &r), 1);
        if (rc == ENG_OK && r != 0)
            rc = eng_dbferr(e, ndx_key(x, cur), 1);
    }
    return rc;
}
