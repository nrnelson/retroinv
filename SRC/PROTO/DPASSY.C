/*
 * DPASSY.C - the ASSY commands (LIST BOM PATH CHILDREN INSTALL REMOVE
 * MOVE).
 *
 * Item IDs are positional, checked by proto_need_idf: CHILD and PARENT
 * for INSTALL, CHILD for REMOVE, ID otherwise; the engine names the same
 * fields for an item that is not live, and for the rules it enforces.
 * LOC= of REMOVE and MOVE, a location token (ENGLOCS.H), goes to the
 * engine as it is (missing, empty or all spaces is its ENG_EARG naming
 * LOC).  INSTALL, REMOVE and MOVE answer "+OK".
 *
 * BOM, CHILDREN and LIST are paged and take FIELDS= (a field list of
 * ITEMS, eng_fieldlist), PAGE= and AFTER=; LIST also takes TOP= (0 or
 * 1).  BOM and LIST are walks (eng_walk), sent through proto_send_rows,
 * BOM with the lead column LEVEL; CHILDREN is one ITEMS3 scan.  PATH is
 * unpaged, with the lead column LEVEL and the fixed ITEMS columns
 * ITEM_ID CATEGORY MFR MODEL LOC_ID LOC_NAME; its at most ENG_MAXDEPTH - 1
 * rows fit an unpaged response (checked in DPCSV.C), and a sink whose
 * room falls below PROTO_ROOMMIN before the last row ends the block with
 * K ERROR= and ENG_EINTERNAL rather than send a shortened path.
 */
#include <stddef.h>
#include "COMPAT.H"
#include "PROTO.H"
#include "DPCMD.H"
#include "ENGFLD.H"
#include "ENGASSY.H"

static const char k_child[] = "CHILD";
static const char k_parent[] = "PARENT";
static const char k_id[] = "ID";
static const char k_loc[] = "LOC";
static const char k_top[] = "TOP";
static const char k_level[] = DP_LEVEL;

/* The pairs of ASSY LIST; BOM and CHILDREN take the list after TOP, and
   its last key is the AFTER= key of all three. */
static const char list_keys[] = "TOP,FIELDS,PAGE,AFTER";
#define PAGE_KEYS   (list_keys + sizeof "TOP," - 1)
#define K_AFTER     (list_keys + sizeof list_keys - sizeof "AFTER")

/* The columns of ASSY PATH after LEVEL. */
static const u16 path_cols[] = { DP_ASSY_PATH(DP_FNUM, DP_XNUM) };

/* The ID named field at position 0, nothing else positional, and only
   the pairs of the comma list keys. */
static int assy_id(proto_call *c, const char *field, const char *keys,
                   u32 *id)
{
    int rc;

    rc = proto_need_idf(c, 0, field, id);
    if (rc == ENG_OK)
        rc = proto_args_end(c, 1, keys);
    return rc;
}

/* ASSY INSTALL child parent. */
int dp_assy_install(proto_call *c)
{
    u32 child;
    u32 parent;
    int rc;

    rc = proto_need_idf(c, 0, k_child, &child);
    if (rc == ENG_OK)
        rc = proto_need_idf(c, 1, k_parent, &parent);
    if (rc == ENG_OK)
        rc = proto_args_end(c, 2, "");
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_assy_install(c->h->e, child, parent));
}

/* ASSY REMOVE child LOC=loc. */
int dp_assy_remove(proto_call *c)
{
    u32 child;
    int rc;

    rc = assy_id(c, k_child, k_loc, &child);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_assy_remove(c->h->e, child,
                                         proto_get(c->r, k_loc)));
}

/* ASSY MOVE id LOC=loc. */
int dp_assy_move(proto_call *c)
{
    u32 id;
    int rc;

    rc = assy_id(c, k_id, k_loc, &id);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_assy_move(c->h->e, id, proto_get(c->r, k_loc)));
}

/* The row source of the walks and of PATH, which use one part of u
   each, as path says.  A walk's rows come from u.w; PATH's from the
   record numbers u.p.recnos[0..u.p.n-1], u.p.i being the next one to send.
   PATH never reports more rows, so its cursor is never asked for. */
typedef struct {
    int path;
    union {
        eng_walk w;
        struct {
            u32 recnos[ENG_MAXDEPTH];
            u16 n;
            u16 i;
        } p;
    } u;
} assy_src;

static int assy_next(proto_call *c, proto_rows *r, u32 *recno)
{
    assy_src *src;
    u16 level;
    int rc;

    src = (assy_src *)r->arg;
    if (!src->path) {
        rc = eng_walk_next(c->h->e, &src->u.w, recno, &level);
        if (rc == ENG_OK && *recno != 0)
            txt_u32(r->leadval, level);
        return rc;
    }
    *recno = 0;
    if (src->u.p.i == src->u.p.n)
        return ENG_OK;
    rc = eng_rec_read(c->h->e, ENG_T_ITEMS, src->u.p.recnos[src->u.p.i]);
    if (rc != ENG_OK)
        return rc;
    *recno = src->u.p.recnos[src->u.p.i];
    src->u.p.i++;
    txt_u32(r->leadval, src->u.p.i);
    return ENG_OK;
}

/* PATH has no cursor: rows it could not send are a failure. */
static int assy_more(proto_call *c, proto_rows *r, int *more)
{
    assy_src *src;

    src = (assy_src *)r->arg;
    *more = 0;
    if (!src->path)
        return eng_walk_more(c->h->e, &src->u.w, more);
    return src->u.p.i < src->u.p.n ? ENG_EINTERNAL : ENG_OK;
}

static void assy_cursor(proto_rows *r, char *out)
{
    eng_walk_cursor(&((const assy_src *)r->arg)->u.w, out);
}

/* Send a page of src: the lead column lead (NULL for none) and the
   ITEMS fields idx[0..n-1]. */
static int assy_send(proto_call *c, assy_src *src, const char *lead,
                     const u16 *idx, u16 n, u16 page)
{
    proto_rows r;

    proto_rows_init(&r, ENG_T_ITEMS, idx, n, assy_next, assy_more,
                    assy_cursor, src);
    r.lead = lead;
    return proto_send_rows(c, &r, page);
}

/* ASSY BOM id [FIELDS=][PAGE=][AFTER=]: the parts, depth first, with
   their LEVEL below id. */
int dp_assy_bom(proto_call *c)
{
    assy_src src;
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    u32 id;
    int rc;

    rc = proto_need_idf(c, 0, k_id, &id);
    if (rc == ENG_OK)
        rc = proto_no_more(c, 1);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_ITEMS, PAGE_KEYS, &page, idx, &n);
    if (rc == ENG_OK)
        rc = eng_assy_bom(c->h->e, &src.u.w, id, proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    src.path = 0;
    return assy_send(c, &src, k_level, idx, n, page);
}

/* ASSY PATH id: the ancestors, the top-level item first with LEVEL 1. */
int dp_assy_path(proto_call *c)
{
    assy_src src;
    u32 id;
    int rc;

    rc = assy_id(c, k_id, "", &id);
    if (rc == ENG_OK)
        rc = eng_assy_path(c->h->e, id, src.u.p.recnos, &src.u.p.n);
    if (rc != ENG_OK)
        return rc;
    src.path = 1;
    src.u.p.i = 0;
    return assy_send(c, &src, k_level, path_cols,
                     (u16)(sizeof path_cols / sizeof path_cols[0]),
                     ENG_MAXDEPTH);
}

/* ASSY CHILDREN id [FIELDS=][PAGE=][AFTER=]: the direct parts in ID
   order. */
int dp_assy_children(proto_call *c)
{
    eng_scan s;
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    u32 id;
    int rc;

    rc = proto_need_idf(c, 0, k_id, &id);
    if (rc == ENG_OK)
        rc = proto_no_more(c, 1);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_ITEMS, PAGE_KEYS, &page, idx, &n);
    if (rc == ENG_OK)
        rc = eng_assy_children(c->h->e, &s, id, proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    return proto_send_page(c, &s, ENG_T_ITEMS, idx, n, page);
}

/* ASSY LIST [TOP=0|1][FIELDS=][PAGE=][AFTER=]: the items that have
   parts, in ID order; TOP=1 only the top-level ones. */
int dp_assy_list(proto_call *c)
{
    assy_src src;
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    int top;
    int rc;

    rc = proto_no_more(c, 0);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_ITEMS, list_keys, &page, idx, &n);
    if (rc == ENG_OK)
        rc = proto_flag(c, k_top, &top);
    if (rc == ENG_OK)
        rc = eng_assy_list(c->h->e, &src.u.w, top, proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    src.path = 0;
    return assy_send(c, &src, NULL, idx, n, page);
}
