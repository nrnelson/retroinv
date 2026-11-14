/*
 * LOCS.C - locations (LOCS.DBF, its memo file and indexes LOCS1..LOCS3).
 *
 * The rules kept here are those of ENGLOCS.H.  Between eng_edit_begin
 * and the commit the LOCS buffer belongs to the edit, and the commit
 * checks no uniqueness, so every lookup in LOCS (the location's old
 * values, the new parent's chain, a location with the same name under
 * it or with the same code, the locations below a moved one) is made
 * first, from the pairs; only the type is looked up during the edit.
 *
 * The tree is read two ways.  Up through PARENT_ID and LOCS1, one
 * record per level (up): a depth, a path, the cycle check of a move.
 * Down through LOCS2, whose key STR(PARENT_ID,8)+UPPER(LOC_NAME)+
 * STR(LOC_ID,8) holds a location's parent, name and ID: the first child
 * of X is the first entry that starts with STR(X,8), and the sibling
 * after an entry the first entry above its key that starts with the
 * same parent.  A walk keeps the keys of the current location and of
 * those above it, so it climbs without reading (cand); its items come
 * from ITEMS2 the same way, by seeking above the last item's key.  The
 * height check of a move is such a walk.  Both ways stop at
 * ENG_MAXLOCDEPTH.
 *
 * Field numbers are the schema's constants (ENG_FIELD); the keys built
 * here rely on the schema's widths, which every open table has
 * (eng_open_files refuses any other layout).
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
#include "ENGNOTE.H"
#include "ENGCODE.H"
#include "ENGITEM.H"
#define ENGLOCS_WALKS
#include "ENGLOCS.H"

/* Fields used here. */
#define F_LID       ENG_FIELD(LOCS, LOC_ID)
#define F_LPARENT   ENG_FIELD(LOCS, PARENT_ID)
#define F_LNAME     ENG_FIELD(LOCS, LOC_NAME)
#define F_LCODE     ENG_FIELD(LOCS, LOC_CODE)
#define F_LTYPE     ENG_FIELD(LOCS, LTYPE)
#define F_LNOTES    ENG_FIELD(LOCS, NOTES)

/* A LOCS2 key up to its ID: the parent and the name. */
#define NAMEKEY     (ENG_IDLEN + ENG_LOCNAMELEN)

/* eng_lwalk.state besides 0 (not started) and a failed walk's ENG_E
   code (ENG_ESYNTAX or above). */
#define W_ON        1
#define W_END       2

/* eng_lwalk.kind of the height check of a move (room): an ENG_LW_TREE
   walk without the read limit, for which a location too deep is
   ENG_TX_LOCDEEP rather than damage. */
#define LW_MOVE     3

static const char k_parent[] = "PARENT_ID";
static const char k_locname[] = "LOC_NAME";
static const char k_loccode[] = "LOC_CODE";
static const char k_sep[] = " > ";
static const char k_cut[] = "... > ";

/* A path holds a location's own name, after "... > " when cut. */
extern char locs_path_fits[1];
extern char locs_path_fits[(ENG_LOCPATHLEN >=
                            sizeof k_cut - 1 + ENG_LOCNAMELEN) ? 1 : 2];

/* ENG_EIO for a tree another program has broken. */
static int damaged(eng *e)
{
    return eng_fail(e, ENG_EIO, NULL, ENG_TX_LOCTREE);
}

/* ---------------------------------------------------------------------
 * Lookups.
 * --------------------------------------------------------------------- */

int eng_loc_idtok(const char *s, u32 *id)
{
    u16 i;

    *id = 0;
    for (i = 0; TXT_ISDIGIT(s[i]); i++)
        if (*id <= ENG_MAXID)
            *id = *id * 10 + (u32)(s[i] - '0');
    return i > 0 && s[i] == '\0';
}

int eng_loc_seek(eng *e, u32 id, u32 *recno)
{
    char key[ENG_IDLEN + 1];

    *recno = 0;
    if (!eng_id_key(id, key))
        return ENG_OK;
    return eng_seek_live(e, ENG_X_LOCS1, key, recno);
}

int eng_loc_get(eng *e, const char *loc, u32 *recno)
{
    char code[ENG_LOCLEN + 1];
    u32 id;
    int rc;

    *recno = 0;
    rc = ENG_OK;
    if (loc != NULL && eng_loc_idtok(loc, &id))
        rc = eng_loc_seek(e, id, recno);
    else if (eng_code_norm(loc, ENG_LOCLEN, 1, code))
        rc = eng_seek_live(e, ENG_X_LOCS3, code, recno);
    if (rc == ENG_OK && *recno == 0)
        rc = eng_fail(e, ENG_ENOTFOUND, NULL, ENG_TX_NOSUCHLOC);
    return rc;
}

int eng_loc_id(eng *e, const char *loc, u32 *id)
{
    u32 r;
    int rc;

    if (loc == NULL || loc[0] == '\0' ||
        (eng_loc_idtok(loc, id) && *id == 0)) {
        *id = 0;
        return ENG_OK;
    }
    rc = eng_loc_get(e, loc, &r);
    *id = rc == ENG_OK ? (u32)eng_rec_num(e, ENG_T_LOCS, F_LID) : 0;
    return rc;
}

int eng_loc_pairs(eng *e, const eng_pair *p, u16 n, const char *field,
                  eng_pair *q, char *idtext, u32 *id)
{
    u16 i;
    int rc;

    if (n > ENG_MAXPAIRS)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_MANYPAIRS);
    if (n > 0)
        memcpy(q, p, n * sizeof *q);
    for (i = 0; i < n; i++) {
        if (!txt_ieq(p[i].key, field))
            continue;
        rc = eng_loc_id(e, p[i].val, id);
        if (rc == ENG_ENOTFOUND)
            return eng_fail(e, ENG_ECONSTR, field, ENG_TX_NOLOC);
        if (rc != ENG_OK)
            return rc;
        txt_u32(idtext, *id);
        q[i].val = idtext;
    }
    return ENG_OK;
}

/* Load the LOCS1 entry of location id, live or deleted, into the
   buffer; *r is 0 when there is none. */
static int any_loc(eng *e, u32 id, u32 *r)
{
    char k[ENG_IDLEN + 1];
    int exact;
    int rc;

    *r = 0;
    if (!eng_id_key(id, k))
        return ENG_OK;
    rc = eng_dbferr(e, ndx_seek(eng_ndx(e, ENG_X_LOCS1), (const u8 *)k,
                                ENG_IDLEN, r, &exact), 1);
    if (rc != ENG_OK || *r == 0)
        return rc;
    if (!exact) {
        *r = 0;
        return ENG_OK;
    }
    return eng_rec_read(e, ENG_T_LOCS, *r);
}

/* ENG_EDUP naming field (text tx) when a live LOCS record other than
   record self has the len key bytes key on index x (LOCS2 up to the ID:
   the same name under one parent; LOCS3: the same code).  Deleted
   records met count toward no limit, as for eng_seek_live.  Moves the
   LOCS buffer. */
static int unique(eng *e, int x, const u8 *key, u16 len, u32 self,
                  const char *field, u16 tx)
{
    u8 cur[ENG_LOCS2LEN];
    ndx *nx;
    u32 r;
    int exact;
    int rc;

    nx = eng_ndx(e, x);
    rc = ndx_seek(nx, key, len, &r, &exact);
    while (rc == DBF_OK && r != 0) {
        rc = ndx_key(nx, cur);
        if (rc != DBF_OK || memcmp(cur, key, len) != 0)
            break;
        if (r != self) {
            rc = eng_rec_read(e, ENG_T_LOCS, r);
            if (rc != ENG_OK)
                return rc;
            if (!dbf_is_deleted(eng_dbf(e, ENG_T_LOCS)))
                return eng_fail(e, ENG_EDUP, field, tx);
        }
        rc = ndx_next(nx, &r);
    }
    return eng_dbferr(e, rc, 1);
}

/* Walk up from location id through PARENT_ID to the top: *depth
   receives its depth and *first its record.  With path not NULL the
   names met are set into path (ENG_LOCPATHLEN + 1 bytes) as
   eng_loc_path gives them, each to the left of the one before, so the
   nearest ones are kept.  ENG_ENOTFOUND when id is no live location;
   ENG_ECONSTR naming PARENT_ID (ENG_TX_CYCLE) when the walk meets watch
   (0: none), id itself included; ENG_EIO (ENG_TX_LOCTREE) when a parent
   is no live location or the chain passes ENG_MAXLOCDEPTH. */
static int up(eng *e, u32 id, u32 watch, char *path, u16 *depth,
              u32 *first)
{
    char name[ENG_LOCNAMELEN + 1];
    u16 start;                  /* where the names in path start        */
    u16 keep;                   /* the last such start with room for
                                   k_cut before it                      */
    u16 len;
    u16 sep;
    u32 r;
    i32 v;
    int full;
    int blank;
    int rc;

    *depth = 0;
    *first = 0;
    start = ENG_LOCPATHLEN;
    keep = start;
    full = path != NULL;
    if (full)
        path[start] = '\0';
    for (;;) {
        if (id == watch && watch != 0)
            return eng_fail(e, ENG_ECONSTR, k_parent, ENG_TX_CYCLE);
        rc = eng_loc_seek(e, id, &r);
        if (rc != ENG_OK)
            return rc;
        if (r == 0 && *depth == 0)
            return eng_fail(e, ENG_ENOTFOUND, NULL, ENG_TX_NOSUCHLOC);
        if (r == 0 || *depth == ENG_MAXLOCDEPTH)
            return damaged(e);
        if (*depth == 0)
            *first = r;
        (*depth)++;
        if (full) {
            eng_rec_str(e, ENG_T_LOCS, F_LNAME, name, (u16)sizeof name);
            len = (u16)strlen(name);
            sep = start < ENG_LOCPATHLEN ? (u16)(sizeof k_sep - 1) : 0;
            if (len + sep > start) {
                full = 0;
            } else {
                start = (u16)(start - len - sep);
                memcpy(path + start, name, len);
                memcpy(path + start + len, k_sep, sep);
                if (start >= sizeof k_cut - 1)
                    keep = start;
            }
        }
        if (dbf_get_num(eng_dbf(e, ENG_T_LOCS), F_LPARENT, &v, &blank) !=
            DBF_OK || v < 0)
            return damaged(e);
        if (v == 0)
            break;
        id = (u32)v;
    }
    if (path != NULL && !full) {
        start = (u16)(keep - (sizeof k_cut - 1));
        memcpy(path + start, k_cut, sizeof k_cut - 1);
    }
    if (path != NULL)
        memmove(path, path + start, (size_t)(ENG_LOCPATHLEN + 1 - start));
    return ENG_OK;
}

int eng_loc_path(eng *e, u32 id, char *path, u16 *depth)
{
    u32 r;
    int rc;

    rc = up(e, id, 0, path, depth, &r);
    if (rc == ENG_OK)
        return eng_rec_read(e, ENG_T_LOCS, r);
    if (path != NULL)
        path[0] = '\0';
    return rc;
}

/* ---------------------------------------------------------------------
 * Walks.
 * --------------------------------------------------------------------- */

/* 1 for a walk of items. */
static int item_walk(const eng_lwalk *w)
{
    return w->kind == ENG_LW_HERE || w->kind == ENG_LW_ALL;
}

/* The first entry of index x (LOCS2 or ITEMS2) at or above the len
   bytes at k (past 0), or above them (past 1: an entry equal to them,
   which only a whole key can be, is stepped over), whose first
   ENG_IDLEN bytes are those of k, the same parent or location: *r
   receives its record, 0 when there is none, and cur its key. */
static int seek_from(eng *e, int x, const u8 *k, u16 len, int past,
                     u32 *r, u8 *cur)
{
    ndx *nx;
    int exact;
    int rc;

    nx = eng_ndx(e, x);
    rc = ndx_seek(nx, k, len, r, &exact);
    while (rc == DBF_OK && *r != 0) {
        rc = ndx_key(nx, cur);
        if (rc != DBF_OK || !past || memcmp(cur, k, len) != 0)
            break;
        rc = ndx_next(nx, r);
    }
    if (rc == DBF_OK && *r != 0 && memcmp(cur, k, ENG_IDLEN) != 0)
        *r = 0;
    return eng_dbferr(e, rc, 1);
}

/* The walk's next entry after its position, without moving it: an item
   of the current location after w->ikey (item walks, while w->down),
   else the first location in the current one (while w->down, but for
   ENG_LW_HERE), else the location after the current one among its
   siblings, or after one of its ancestors among theirs.  *t receives
   the entry's table, *k a location's level below root, *r the entry's
   record (0: the walk is over) and cur its key; an entry whose ID cannot
   be read is damage. */
static int cand(eng *e, eng_lwalk *w, int *t, u16 *k, u32 *r, u8 *cur)
{
    u8 pre[ENG_IDLEN];
    u32 id;
    u16 d;
    int rc;

    d = w->depth;
    *r = 0;
    *k = 0;
    *t = ENG_T_ITEMS;
    rc = ENG_OK;
    if (w->down && item_walk(w)) {
        rc = seek_from(e, ENG_X_ITEMS2, w->ikey, ENG_ITEMS2LEN, 1, r, cur);
        if (rc != ENG_OK || *r != 0 || w->kind == ENG_LW_HERE)
            return rc;
    }
    *t = ENG_T_LOCS;
    if (w->down) {
        id = d == 0 ? w->root :
             eng_key_id(w->path[d - 1] + NAMEKEY, ENG_IDLEN);
        txt_u32w((char *)pre, id, ENG_IDLEN, ' ');
        rc = seek_from(e, ENG_X_LOCS2, pre, ENG_IDLEN, 0, r, cur);
        *k = (u16)(d + 1);
    }
    for (; rc == ENG_OK && *r == 0 && d > 0; d--) {
        rc = seek_from(e, ENG_X_LOCS2, w->path[d - 1], ENG_LOCS2LEN, 1, r,
                       cur);
        *k = d;
    }
    if (rc == ENG_OK && *r != 0 && eng_key_id(cur + NAMEKEY, ENG_IDLEN) == 0)
        rc = damaged(e);
    return rc;
}

/* The walk failed with rc: every later step gives rc again. */
static int walk_fail(eng_lwalk *w, int rc)
{
    w->state = rc;
    return rc;
}

/* ENG_OK for a walk that may step on; a failed walk's code, or
   ENG_EINTERNAL for one never opened. */
static int walk_ok(eng *e, eng_lwalk *w)
{
    if (w->state == 0)
        return eng_fail(e, ENG_EINTERNAL, NULL, ENG_TX_NOSCAN);
    if (w->state >= ENG_ESYNTAX)
        return w->state;
    return ENG_OK;
}

/* Put the walk on the location whose LOCS2 key is cur, at level k below
   root: its key ends the path, and unless it is deleted its items (from
   ITEM_ID 0 in the item key) and the locations in it come next. */
static void arrive(eng_lwalk *w, u16 k, const u8 *cur, int live)
{
    memcpy(w->path[k - 1], cur, ENG_LOCS2LEN);
    w->depth = k;
    w->down = (u8)live;
    memcpy(w->ikey, cur + NAMEKEY, ENG_IDLEN);
    txt_u32w((char *)w->ikey + ENG_IDLEN, 0, ENG_IDLEN, ' ');
}

int eng_lwalk_next(eng *e, eng_lwalk *w, u32 *recno, u16 *depth)
{
    u8 cur[ENG_LOCS2LEN];
    u32 r;
    u16 k;
    int t;
    int live;
    int rc;

    *recno = 0;
    *depth = 0;
    rc = walk_ok(e, w);
    if (rc != ENG_OK)
        return rc;
    while (w->state == W_ON) {
        rc = cand(e, w, &t, &k, &r, cur);
        if (rc != ENG_OK)
            return walk_fail(w, rc);
        if (r == 0) {
            w->state = W_END;
            break;
        }
        if (w->kind != LW_MOVE && w->reads >= ENG_MAXREADS)
            break;
        rc = eng_rec_read(e, t, r);
        if (rc != ENG_OK)
            return walk_fail(w, rc);
        w->reads++;
        w->recno = r;
        live = !dbf_is_deleted(eng_dbf(e, t));
        if (t == ENG_T_ITEMS) {
            memcpy(w->ikey, cur, ENG_ITEMS2LEN);
            if (!live || !eng_item_keep(e, &w->f))
                continue;
        } else {
            /* A live location below the deepest level: one the move
               would put there, else damage. */
            if (live && w->base + k > ENG_MAXLOCDEPTH)
                return walk_fail(w, w->kind == LW_MOVE ?
                                 eng_fail(e, ENG_ECONSTR, k_parent,
                                          ENG_TX_LOCDEEP) : damaged(e));
            arrive(w, k, cur, live);
            if (!live || item_walk(w))
                continue;
        }
        *recno = r;
        *depth = (u16)(w->base + w->depth);
        return ENG_OK;
    }
    return ENG_OK;
}

int eng_lwalk_more(eng *e, eng_lwalk *w, int *more)
{
    u8 cur[ENG_LOCS2LEN];
    u32 r;
    u16 k;
    int t;
    int rc;

    *more = 0;
    rc = walk_ok(e, w);
    if (rc != ENG_OK || w->state == W_END)
        return rc;
    rc = cand(e, w, &t, &k, &r, cur);
    if (rc != ENG_OK)
        return walk_fail(w, rc);
    *more = r != 0;
    return ENG_OK;
}

void eng_lwalk_cursor(const eng_lwalk *w, char *out)
{
    out[0] = '\0';
    if (w->state == 0 || w->recno == 0)
        return;
    if (item_walk(w))
        eng_cursor_text(ENG_X_ITEMS2, w->ikey, w->recno, out);
    else
        eng_cursor_text(ENG_X_LOCS2, w->path[w->depth - 1], w->recno,
                        out);
}

/* Rebuild the path of walk w down to the location whose LOCS2 key is
   key: the keys of its ancestors are read, live or deleted, up to root.
   ENG_ECURSOR when root is not among them within the levels below it,
   down to one below the deepest (the cursor of another walk, or of a
   location moved out since). */
static int climb(eng *e, eng_lwalk *w, const u8 *key)
{
    u8 pre[ENG_IDLEN];
    u8 tmp[ENG_LOCS2LEN];
    u32 r;
    u16 n;
    u16 i;
    int rc;

    txt_u32w((char *)pre, w->root, ENG_IDLEN, ' ');
    memcpy(w->path[0], key, ENG_LOCS2LEN);
    for (n = 1; memcmp(w->path[n - 1], pre, ENG_IDLEN) != 0; n++) {
        if (n > ENG_MAXLOCDEPTH - w->base)
            return eng_fail(e, ENG_ECURSOR, NULL, ENG_TX_BADCUR);
        rc = any_loc(e, eng_key_id(w->path[n - 1], ENG_IDLEN), &r);
        if (rc == ENG_OK && r == 0)
            rc = eng_fail(e, ENG_ECURSOR, NULL, ENG_TX_BADCUR);
        if (rc == ENG_OK)
            rc = eng_dbferr(e, kx_eval(eng_kx(e, ENG_X_LOCS2),
                                       eng_dbf(e, ENG_T_LOCS), w->path[n]),
                            1);
        if (rc != ENG_OK)
            return rc;
    }
    for (i = 0; i < n / 2; i++) {
        memcpy(tmp, w->path[i], ENG_LOCS2LEN);
        memcpy(w->path[i], w->path[n - 1 - i], ENG_LOCS2LEN);
        memcpy(w->path[n - 1 - i], tmp, ENG_LOCS2LEN);
    }
    w->depth = n;
    return ENG_OK;
}

/* Restore walk w, opened at its start, to the cursor text after: on
   the location the cursor names, for ENG_LW_TREE by the last LOCS2 entry
   examined, for the item walks by its ITEMS2 key, root itself or
   (ENG_LW_ALL) one below it.  Its children come next when it is live and,
   for ENG_LW_TREE, still under the entry's parent; such a one below the
   deepest level is damage. */
static int resume(eng *e, eng_lwalk *w, const char *after)
{
    u8 key[ENG_LOCS2LEN];
    u32 id;
    u32 r;
    int rc;

    if (item_walk(w)) {
        memcpy(key, w->ikey, ENG_IDLEN);
        rc = eng_cursor_parse(e, ENG_X_ITEMS2, after, w->ikey, &w->recno);
        if (rc != ENG_OK || memcmp(w->ikey, key, ENG_IDLEN) == 0)
            return rc;
        id = w->kind == ENG_LW_ALL ? eng_key_id(w->ikey, ENG_IDLEN) : 0;
    } else {
        rc = eng_cursor_parse(e, ENG_X_LOCS2, after, key, &w->recno);
        id = eng_key_id(key + NAMEKEY, ENG_IDLEN);
    }
    r = 0;
    if (rc == ENG_OK)
        rc = any_loc(e, id, &r);
    if (rc == ENG_OK && r == 0)
        rc = eng_fail(e, ENG_ECURSOR, NULL, ENG_TX_BADCUR);
    if (rc == ENG_OK && item_walk(w))
        rc = eng_dbferr(e, kx_eval(eng_kx(e, ENG_X_LOCS2),
                                   eng_dbf(e, ENG_T_LOCS), key), 1);
    if (rc != ENG_OK)
        return rc;
    w->down = (u8)(!dbf_is_deleted(eng_dbf(e, ENG_T_LOCS)) &&
                   eng_rec_num(e, ENG_T_LOCS, F_LPARENT) ==
                   (i32)eng_key_id(key, ENG_IDLEN));
    rc = climb(e, w, key);
    if (rc == ENG_OK && w->down && w->base + w->depth > ENG_MAXLOCDEPTH)
        rc = damaged(e);
    return rc;
}

/* Open walk w of kind on root, the live location loc (0 for the whole
   tree), at the cursor text after (NULL or "": the start). */
static int walk_open(eng *e, eng_lwalk *w, int kind, const char *loc,
                     const char *after)
{
    u32 r;
    int rc;

    memset(w, 0, sizeof *w);
    rc = eng_loc_id(e, loc, &w->root);
    if (rc == ENG_OK && w->root != 0)
        rc = up(e, w->root, 0, NULL, &w->base, &r);
    if (rc != ENG_OK)
        return rc;
    w->kind = (u8)kind;
    w->down = 1;
    txt_u32w((char *)w->ikey, w->root, ENG_IDLEN, ' ');
    txt_u32w((char *)w->ikey + ENG_IDLEN, 0, ENG_IDLEN, ' ');
    w->state = W_ON;
    if (after == NULL || after[0] == '\0')
        return ENG_OK;
    rc = resume(e, w, after);
    if (rc != ENG_OK)
        w->state = 0;
    return rc;
}

int eng_loc_list(eng *e, eng_lwalk *w, const char *under,
                 const char *after)
{
    return walk_open(e, w, ENG_LW_TREE, under, after);
}

int eng_loc_contents(eng *e, eng_lwalk *w, const char *loc, int all,
                     const eng_ifilt *f, const char *after)
{
    int rc;

    rc = walk_open(e, w, all ? ENG_LW_ALL : ENG_LW_HERE, loc, after);
    if (rc == ENG_OK)
        memcpy(&w->f, f, sizeof w->f);
    return rc;
}

/* ENG_ECONSTR naming PARENT_ID (ENG_TX_LOCDEEP) when location id, moved to
   depth d, would leave a live location below it deeper than
   ENG_MAXLOCDEPTH.  Moving to its depth or above deepens nothing; a move
   down walks the locations below it, without the read limit, down to
   the deepest level they may reach. */
static int room(eng *e, u32 id, u16 d)
{
    eng_lwalk w;
    u32 r;
    u16 now;
    int rc;

    rc = up(e, id, 0, NULL, &now, &r);
    if (rc != ENG_OK || d <= now)
        return rc;
    memset(&w, 0, sizeof w);
    w.kind = LW_MOVE;
    w.root = id;
    w.base = d;
    w.down = 1;
    w.state = W_ON;
    do
        rc = eng_lwalk_next(e, &w, &r, &now);
    while (rc == ENG_OK && r != 0);
    return rc;
}

/* ---------------------------------------------------------------------
 * Changes.
 * --------------------------------------------------------------------- */

/* The LOCS2 key up to the ID of a location named name under parent, in
   key (NAMEKEY bytes): 0 when name is blank or longer than the field,
   which an edit refuses anyway. */
static int name_key(u32 parent, const char *name, u8 *key)
{
    u16 i;
    char c;

    if (name[0] == '\0' || strlen(name) > ENG_LOCNAMELEN)
        return 0;
    txt_u32w((char *)key, parent, ENG_IDLEN, ' ');
    memset(key + ENG_IDLEN, ' ', ENG_LOCNAMELEN);
    for (i = 0; name[i] != '\0'; i++) {
        c = name[i];
        key[ENG_IDLEN + i] = (u8)TXT_UPPER(c);
    }
    return 1;
}

/* The place of location record r (0: a new one) whose LOC_ID is self
   and whose parent was old, under parent: a new location or a move is
   checked (parent's chain, which must not meet self, ENG_TX_CYCLE; the
   depth limit; for a move, room below), and *depth receives its depth
   there.  pdepth is that of eng_loc_check. */
static int place(eng *e, u32 r, u32 self, u32 old, u32 parent,
                 u16 pdepth, u16 *depth)
{
    u32 first;
    u16 d;
    int rc;

    *depth = 0;
    if (r != 0 && parent == old)
        return ENG_OK;
    d = pdepth;
    rc = ENG_OK;
    if (pdepth == 0 && parent != 0)
        rc = up(e, parent, self, NULL, &d, &first);
    if (rc == ENG_OK && d >= ENG_MAXLOCDEPTH)
        rc = eng_fail(e, ENG_ECONSTR, k_parent, ENG_TX_LOCDEEP);
    if (rc == ENG_OK && r != 0)
        rc = room(e, self, (u16)(d + 1));
    *depth = (u16)(d + 1);
    return rc;
}

/* The name nv of location record r (0: a new one) among the live
   locations of parent, unless named is 0 (its parent not written yet),
   and its code cv, blank or well formed and among no other live
   location's. */
static int names(eng *e, u32 r, u32 parent, int named, const char *nv,
                 const char *cv)
{
    char code[ENG_LOCLEN + 1];
    u8 key[NAMEKEY];
    int rc;

    rc = ENG_OK;
    if (named && name_key(parent, nv, key))
        rc = unique(e, ENG_X_LOCS2, key, NAMEKEY, r, k_locname,
                    ENG_TX_NAMEDUP);
    if (rc != ENG_OK || cv[0] == '\0')
        return rc;
    if (!eng_code_norm(cv, ENG_LOCLEN, 1, code))
        return eng_fail(e, ENG_EARG, k_loccode, ENG_TX_BADLOC);
    memset(key, ' ', ENG_LOCLEN);
    memcpy(key, code, strlen(code));
    return unique(e, ENG_X_LOCS3, key, ENG_LOCLEN, r, k_loccode,
                  ENG_TX_LOCDUP);
}

/* Start the edit of location record r (0: a new one, at the top unless
   q places it) with the pairs q, then check the required fields and the
   type. */
static int edit_loc(eng *e, u32 r, const eng_pair *q, u16 n)
{
    char lt[ENG_LTYPELEN + 1];
    u32 rr;
    int rc;

    rc = eng_edit_begin(e, ENG_T_LOCS, r);
    if (rc != ENG_OK)
        return rc;
    if (r == 0)
        rc = eng_edit_putnumf(e, F_LPARENT, 0);
    if (rc == ENG_OK)
        rc = eng_edit_apply(e, q, n, ENG_F_RO);
    if (rc == ENG_OK)
        rc = eng_edit_required(e);
    if (rc != ENG_OK)
        return rc;
    eng_rec_str(e, ENG_T_LOCS, F_LTYPE, lt, (u16)sizeof lt);
    rc = eng_code_get(e, ENG_T_LTYPES, lt, &rr);
    if (rc == ENG_ENOTFOUND)
        rc = eng_fail(e, ENG_ECONSTR, "LTYPE", ENG_TX_NOLTYPE);
    return rc;
}

/* Check the pairs p for location record r (0: a new one; else loaded in
   the LOCS buffer) and start its edit: the parent (place), the name
   under it and the code (names), then the edit (edit_loc).
   pdepth is that of eng_loc_check; *depth receives a new location's
   depth.  The edit is open on return when eng_edit_begin was reached
   and succeeded; eng_edit_finish or eng_edit_cancel ends it either
   way. */
static int build(eng *e, u32 r, const eng_pair *p, u16 n, u16 pdepth,
                 u16 *depth)
{
    eng_pair q[ENG_MAXPAIRS];
    char idtext[TXT_U32LEN + 1];
    char name[ENG_LOCNAMELEN + 1];
    char code[ENG_LOCLEN + 1];
    const char *nv;
    const char *cv;
    u32 self;
    u32 old;
    u32 parent;
    u16 i;
    int rc;

    self = 0;
    old = 0;
    name[0] = '\0';
    code[0] = '\0';
    if (r != 0) {
        self = (u32)eng_rec_num(e, ENG_T_LOCS, F_LID);
        old = (u32)eng_rec_num(e, ENG_T_LOCS, F_LPARENT);
        eng_rec_str(e, ENG_T_LOCS, F_LNAME, name, (u16)sizeof name);
        eng_rec_str(e, ENG_T_LOCS, F_LCODE, code, (u16)sizeof code);
    }
    nv = name;
    cv = code;
    for (i = 0; i < n; i++) {
        if (txt_ieq(p[i].key, k_locname))
            nv = p[i].val;
        else if (txt_ieq(p[i].key, k_loccode))
            cv = p[i].val;
    }
    parent = old;
    *depth = 0;
    rc = eng_loc_pairs(e, p, n, k_parent, q, idtext, &parent);
    if (rc == ENG_OK)
        rc = place(e, r, self, old, parent, pdepth, depth);
    if (rc == ENG_OK)
        rc = names(e, r, parent, pdepth == 0, nv, cv);
    if (rc == ENG_OK)
        rc = edit_loc(e, r, q, n);
    return rc;
}

int eng_loc_add(eng *e, const eng_pair *p, u16 n, u32 *id)
{
    u32 nid;
    u16 d;
    int rc;

    *id = 0;
    nid = 0;
    rc = build(e, 0, p, n, 0, &d);
    if (rc == ENG_OK)
        rc = eng_seq_next(e, ENG_T_LOCS, 1, &nid);
    if (rc == ENG_OK)
        rc = eng_edit_putnumf(e, F_LID, (i32)nid);
    rc = eng_edit_finish(e, rc, nid);
    if (rc == ENG_OK)
        *id = nid;
    return rc;
}

int eng_loc_check(eng *e, const eng_pair *p, u16 n, u16 pdepth,
                  u16 *depth)
{
    int rc;

    rc = build(e, 0, p, n, pdepth, depth);
    eng_edit_cancel(e);
    return rc;
}

/* Load the live location loc as eng_loc_get does; *id receives its
   LOC_ID. */
static int get_id(eng *e, const char *loc, u32 *recno, u32 *id)
{
    int rc;

    rc = eng_loc_get(e, loc, recno);
    *id = rc == ENG_OK ? (u32)eng_rec_num(e, ENG_T_LOCS, F_LID) : 0;
    return rc;
}

int eng_loc_set(eng *e, const char *loc, const eng_pair *p, u16 n)
{
    u32 r;
    u32 id;
    u16 d;
    int rc;

    rc = get_id(e, loc, &r, &id);
    if (rc == ENG_OK)
        rc = build(e, r, p, n, 0, &d);
    return eng_edit_finish(e, rc, id);
}

int eng_loc_del(eng *e, const char *loc)
{
    char key[ENG_IDLEN + 1];
    u32 r;
    u32 id;
    int found;
    int rc;

    rc = get_id(e, loc, &r, &id);
    if (rc != ENG_OK)
        return rc;
    if (!eng_id_key(id, key))
        return damaged(e);
    rc = eng_scan_any(e, ENG_T_ITEMS, ENG_X_ITEMS2, (const u8 *)key,
                      ENG_IDLEN, &found);
    if (rc != ENG_OK)
        return rc;
    if (found != 0)
        return eng_fail(e, ENG_ECONSTR, NULL,
                        found == 1 ? ENG_TX_LOCFULL : ENG_TX_LOCSMANY);
    rc = eng_scan_any(e, ENG_T_LOCS, ENG_X_LOCS2, (const u8 *)key,
                      ENG_IDLEN, &found);
    if (rc != ENG_OK)
        return rc;
    if (found != 0)
        return eng_fail(e, ENG_ECONSTR, NULL,
                        found == 1 ? ENG_TX_LOCKIDS : ENG_TX_KIDSMANY);
    rc = eng_edit_begin(e, ENG_T_LOCS, r);
    if (rc != ENG_OK)
        return rc;
    eng_edit_delete(e, 1);
    return eng_edit_finish(e, ENG_OK, id);
}

int eng_loc_notes(eng *e, const char *loc, u16 *len)
{
    u32 r;
    int rc;

    *len = 0;
    rc = eng_loc_get(e, loc, &r);
    if (rc != ENG_OK)
        return rc;
    return eng_memo_read(e, ENG_T_LOCS, F_LNOTES, len);
}

int eng_loc_addnote(eng *e, const char *loc, const char *text, u8 how)
{
    u32 r;
    u32 id;
    int rc;

    rc = get_id(e, loc, &r, &id);
    if (rc == ENG_OK)
        rc = eng_edit_begin_note(e, ENG_T_LOCS, F_LNOTES, r, text, how);
    return ENG_NOTE_WRITES(how) ? eng_edit_finish(e, rc, id) : rc;
}
