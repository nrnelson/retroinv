/*
 * DPLOC.C - the LOC commands (GET ADD SET DEL LIST CONTENTS NOTES) and
 * the commands of the code tables, CAT and LTYPE (LIST ADD SET DEL).
 *
 * A location argument is a token (ENGLOCS.H): all digits a LOC_ID,
 * anything else a code; the engine resolves it.  A code table's code is
 * the first positional argument, its form checked by the engine, which
 * upper-cases it.  The KEY=value pairs of ADD and SET go to the engine
 * as they are; it refuses keys that are not settable fields of the
 * table.  LOC LIST and LOC CONTENTS are walks of the tree (eng_lwalk),
 * sent through proto_send_rows: LIST with the lead column DEPTH and the
 * fixed columns of DP_LOC_LIST, CONTENTS with FIELDS= of ITEMS.DBF.  The
 * level the writing forms of LOC NOTES need is checked by the
 * dispatcher's gate.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "PROTO.H"
#include "DPCMD.H"
#include "ENGFLD.H"
#include "ENGCODE.H"
#include "ENGITEM.H"
#define ENGLOCS_WALKS
#include "ENGLOCS.H"

static const char k_loc[] = "LOC";
static const char k_depth[] = DP_DEPTH;
static const char k_all[] = "ALL";
static const char k_top[] = "TOP";

/* The keys of LOC CONTENTS, whose last one is the AFTER= key of both
   walks, and of LOC LIST, whose last one is UNDER. */
static const char contents_keys[] = "ALL,TOP,FIELDS,PAGE,AFTER";
static const char list_keys[] = "PAGE,AFTER,UNDER";
#define K_AFTER     (contents_keys + sizeof contents_keys - sizeof "AFTER")
#define K_UNDER     (list_keys + sizeof list_keys - sizeof "UNDER")

/* The columns of LOC LIST after DEPTH, and those of CAT LIST and LTYPE
   LIST, at the same places in both code tables (CODES.C checks). */
static const u16 loc_cols[] = { DP_LOC_LIST(DP_FNUM) };
static const u16 code_cols[] = { DP_CAT_LIST(DP_FNUM) };
#define NLOC_COLS   ((u16)(sizeof loc_cols / sizeof loc_cols[0]))
#define NCODE_COLS  ((u16)(sizeof code_cols / sizeof code_cols[0]))

/* The PATH line of LOC GET, "K PATH=" and the path quoted with every
   byte escaped, fits a response line; and a location argument is at
   most ENG_LOCLEN characters, as the request limits of PROTO.H count
   it, a LOC_ID having no more digits. */
extern char fits_path[1];
extern char fits_path[(sizeof "K PATH=" - 1 + 2 + 2 * ENG_LOCPATHLEN <=
                       PROTO_MAXRESP && ENG_IDLEN <= ENG_LOCLEN) ? 1 : 2];

/* LOC_NAME of the item in the ITEMS buffer: that of its location, read
   into the LOCS buffer, or blank; kept in the rows' cache for the next
   row of the same location. */
static int loc_name(proto_call *c, proto_rows *r, char *val)
{
    eng *e;
    u32 id;
    u32 recno;
    int rc;

    e = c->h->e;
    id = (u32)eng_rec_num(e, ENG_T_ITEMS, ENG_FIELD(ITEMS, LOC_ID));
    if (r->xk == (DP_X_LOC_NAME ^ DP_XCOL) && r->xkey == id) {
        txt_copy(val, r->xval, (u16)sizeof r->xval);
        return ENG_OK;
    }
    val[0] = '\0';
    rc = eng_loc_seek(e, id, &recno);
    if (rc == ENG_OK && recno != 0)
        rc = eng_rec_text(e, ENG_T_LOCS, ENG_FIELD(LOCS, LOC_NAME), val);
    if (rc != ENG_OK)
        return rc;
    txt_copy(r->xval, val, (u16)sizeof r->xval);
    r->xk = (u8)(DP_X_LOC_NAME ^ DP_XCOL);
    r->xkey = id;
    return ENG_OK;
}

/* AVAIL of the row's item, in the ITEMS buffer (eng_item_avail). */
static int item_avail(proto_call *c, proto_rows *r, char *val)
{
    val[0] = r->t == ENG_T_ITEMS ? eng_item_avail(c->h->e) : '\0';
    val[1] = '\0';
    return ENG_OK;
}

/* The computed columns, as rows X(table, field, name, text) in the order
   of their DP_X_ numbers: each follows field in the default list, and
   stands in for it while FIELDS= is checked, so its name is not shorter
   (DISPATCH.C); the seen mask there has a bit per column. */
#define XCOLS(X)        X(ITEMS, LOC_ID, LOC_NAME, loc_name) \
                        X(ITEMS, QTY, AVAIL, item_avail)
#define XCOL_ROW(t, f, name, text) \
    { ENG_T_##t, ENG_FIELD(t, f), #name, text },
#define XCOL_FITS(t, f, name, text) && sizeof #name >= sizeof #f

extern char fits_xcols[1];
extern char fits_xcols[(DP_NXCOL <= 16 XCOLS(XCOL_FITS)) ? 1 : 2];

const dp_xcol dp_xcols[DP_NXCOL] = { XCOLS(XCOL_ROW) };

/* The first positional argument into *arg (field names the field a
   missing one is reported as) and the check that nothing else is
   positional; keys is the comma list of the pairs the command accepts,
   or NULL to leave the pairs to the engine or the caller. */
static int first_arg(proto_call *c, const char *field, const char *keys,
                     const char **arg)
{
    int rc;

    rc = proto_need(c, 0, field, arg);
    if (rc == ENG_OK)
        rc = proto_args_end(c, 1, keys);
    return rc;
}

/* LOC GET loc: the record, then K DEPTH and K PATH (eng_loc_path). */
int dp_loc_get(proto_call *c)
{
    char path[ENG_LOCPATHLEN + 1];
    eng *e;
    const char *loc;
    u32 recno;
    u16 depth;
    int rc;

    e = c->h->e;
    rc = first_arg(c, k_loc, "", &loc);
    if (rc == ENG_OK)
        rc = eng_loc_get(e, loc, &recno);
    if (rc != ENG_OK)
        return rc;
    rc = eng_loc_path(e, (u32)eng_rec_num(e, ENG_T_LOCS,
                                          ENG_FIELD(LOCS, LOC_ID)),
                      path, &depth);
    if (rc != ENG_OK)
        return rc;
    proto_data_begin(c->o);
    rc = proto_send_fields(c, ENG_T_LOCS);
    if (rc != ENG_OK)
        return rc;
    proto_k_num(c->o, k_depth, depth);
    proto_k_kv(c->o, "PATH", path);
    proto_data_end(c->o);
    return ENG_OK;
}

/* LOC ADD LOC_NAME= LTYPE= [PARENT_ID=][LOC_CODE=][CAPACITY=][NOTES=]:
   "+OK ID=n". */
int dp_loc_add(proto_call *c)
{
    u32 id;
    int rc;

    rc = proto_no_more(c, 0);
    if (rc == ENG_OK)
        rc = eng_loc_add(c->h->e, c->r->pair, c->r->npairs, &id);
    if (rc != ENG_OK)
        return rc;
    return proto_ok_num(c, "ID", id);
}

/* LOC SET loc k=v... */
int dp_loc_set(proto_call *c)
{
    const char *loc;
    int rc;

    rc = first_arg(c, k_loc, NULL, &loc);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_loc_set(c->h->e, loc, c->r->pair,
                                     c->r->npairs));
}

/* LOC DEL loc. */
int dp_loc_del(proto_call *c)
{
    const char *loc;
    int rc;

    rc = first_arg(c, k_loc, "", &loc);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_loc_del(c->h->e, loc));
}

/* The row source of the walks: the eng_lwalk in arg, and the depth of
   each entry's location in the lead column when there is one. */
static int walk_next(proto_call *c, proto_rows *r, u32 *recno)
{
    u16 depth;
    int rc;

    rc = eng_lwalk_next(c->h->e, (eng_lwalk *)r->arg, recno, &depth);
    if (rc == ENG_OK && r->lead != NULL)
        txt_u32(r->leadval, depth);
    return rc;
}

static int walk_more(proto_call *c, proto_rows *r, int *more)
{
    return eng_lwalk_more(c->h->e, (eng_lwalk *)r->arg, more);
}

static void walk_cursor(proto_rows *r, char *out)
{
    eng_lwalk_cursor((const eng_lwalk *)r->arg, out);
}

/* LOC LIST [UNDER=loc][PAGE=][AFTER=]: the locations below loc, or the
   whole tree, in tree order, each with its DEPTH. */
int dp_loc_list(proto_call *c)
{
    eng_lwalk w;
    proto_rows r;
    u16 page;
    int rc;

    rc = proto_args_end(c, 0, list_keys);
    if (rc == ENG_OK)
        rc = proto_page_size(c, &page);
    if (rc == ENG_OK)
        rc = eng_loc_list(c->h->e, &w, proto_get(c->r, K_UNDER),
                          proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    proto_rows_init(&r, ENG_T_LOCS, loc_cols, NLOC_COLS, walk_next,
                    walk_more, walk_cursor, &w);
    r.lead = k_depth;
    return proto_send_rows(c, &r, page);
}

int dp_item_walk(proto_call *c, const char *loc, int all,
                 const eng_ifilt *f, const u16 *idx, u16 n, u16 page)
{
    eng_lwalk w;
    proto_rows r;
    int rc;

    rc = eng_loc_contents(c->h->e, &w, loc, all, f, proto_get(c->r, K_AFTER));
    if (rc != ENG_OK)
        return rc;
    proto_rows_init(&r, ENG_T_ITEMS, idx, n, walk_next, walk_more,
                    walk_cursor, &w);
    return proto_send_rows(c, &r, page);
}

/* LOC CONTENTS loc [ALL=0|1][TOP=0|1][FIELDS=][PAGE=][AFTER=]: the items
   at loc ("0" for unknown), with ALL=1 also those at each location
   below it, in tree order; installed parts included unless TOP=1. */
int dp_loc_contents(proto_call *c)
{
    eng_ifilt f;
    u16 idx[DP_MAXCOLS];
    u16 n;
    u16 page;
    const char *loc;
    int all;
    int top;
    int rc;

    rc = first_arg(c, k_loc, NULL, &loc);
    if (rc == ENG_OK)
        rc = proto_page_args(c, ENG_T_ITEMS, contents_keys, &page, idx, &n);
    if (rc == ENG_OK)
        rc = proto_flag(c, k_all, &all);
    if (rc == ENG_OK)
        rc = proto_flag(c, k_top, &top);
    if (rc != ENG_OK)
        return rc;
    memset(&f, 0, sizeof f);
    f.flags = (u8)(top ? ENG_IF_TOP : 0);
    return dp_item_walk(c, loc, all, &f, idx, n, page);
}

/* LOC NOTES loc: the memo as L lines; with a pair, a write as ITEM
   NOTES. */
int dp_loc_notes(proto_call *c)
{
    const char *text;
    const char *loc;
    u16 len;
    u8 how;
    int rc;

    rc = first_arg(c, k_loc, NULL, &loc);
    if (rc == ENG_OK)
        rc = proto_note(c, &text, &how);
    if (rc != ENG_OK)
        return rc;
    if (text != NULL)
        return proto_done(c, eng_loc_addnote(c->h->e, loc, text, how));
    rc = eng_loc_notes(c->h->e, loc, &len);
    if (rc != ENG_OK)
        return rc;
    return proto_send_memo(c, len);
}

/* ---------------------------------------------------------------------
 * The code tables, t being ENG_T_CATS or ENG_T_LTYPES.  A missing code
 * is reported naming the table's code field.
 * --------------------------------------------------------------------- */

/* The code, as first_arg. */
static int code_arg(proto_call *c, int t, const char *keys,
                    const char **code)
{
    return first_arg(c, eng_field(c->h->e, t, code_cols[0])->name, keys,
                     code);
}

/* LIST: unpaged, as a code table holds at most ENG_MAXCODES records:
   the F line of code_cols and one R line per live row, in SORTORD then
   code order.  An R line is built in the writer's line buffer and sent
   only when it is ended, so a read that fails, or a line the writer had
   to cut, sends nothing of it: the block ends with "K ERROR=code"
   (proto_err inside +DATA) instead. */
static int code_list(proto_call *c, int t)
{
    u32 recnos[ENG_MAXCODES];
    proto_rows r;
    char val[ENG_MAXVAL];
    eng *e;
    proto_out *o;
    u16 nrec;
    u16 i;
    u16 f;
    int rc;

    e = c->h->e;
    o = c->o;
    rc = proto_args_end(c, 0, "");
    if (rc == ENG_OK)
        rc = eng_code_list(e, t, recnos, &nrec);
    if (rc != ENG_OK)
        return rc;
    proto_rows_init(&r, t, code_cols, NCODE_COLS, NULL, NULL, NULL, NULL);
    proto_data_begin(o);
    proto_send_fline(c, &r);
    for (i = 0; i < nrec; i++) {
        rc = eng_rec_read(e, t, recnos[i]);
        if (rc != ENG_OK)
            break;
        proto_line_begin(o, "R");
        for (f = 0; f < NCODE_COLS && rc == ENG_OK; f++) {
            rc = eng_rec_text(e, t, code_cols[f], val);
            if (rc == ENG_OK)
                proto_line_value(o, val);
        }
        if (rc == ENG_OK && o->cut)
            rc = ENG_EINTERNAL;
        if (rc != ENG_OK)
            break;
        proto_line_end(o);
    }
    if (rc != ENG_OK) {
        proto_err(o, rc, NULL, NULL);
        return rc;
    }
    proto_data_end(o);
    return ENG_OK;
}

/* ADD code name= [SORTORD=] and SET code [name=][SORTORD=]: fn with the
   code and the pairs, which the engine checks. */
static int code_pairs(proto_call *c, int t,
                      int (*fn)(eng *, int, const char *, const eng_pair *,
                                u16))
{
    const char *code;
    int rc;

    rc = code_arg(c, t, NULL, &code);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, fn(c->h->e, t, code, c->r->pair, c->r->npairs));
}

/* DEL code. */
static int code_del(proto_call *c, int t)
{
    const char *code;
    int rc;

    rc = code_arg(c, t, "", &code);
    if (rc != ENG_OK)
        return rc;
    return proto_done(c, eng_code_del(c->h->e, t, code));
}

int dp_cat_list(proto_call *c)
{
    return code_list(c, ENG_T_CATS);
}

int dp_cat_add(proto_call *c)
{
    return code_pairs(c, ENG_T_CATS, eng_code_add);
}

int dp_cat_set(proto_call *c)
{
    return code_pairs(c, ENG_T_CATS, eng_code_set);
}

int dp_cat_del(proto_call *c)
{
    return code_del(c, ENG_T_CATS);
}

int dp_ltype_list(proto_call *c)
{
    return code_list(c, ENG_T_LTYPES);
}

int dp_ltype_add(proto_call *c)
{
    return code_pairs(c, ENG_T_LTYPES, eng_code_add);
}

int dp_ltype_set(proto_call *c)
{
    return code_pairs(c, ENG_T_LTYPES, eng_code_set);
}

int dp_ltype_del(proto_call *c)
{
    return code_del(c, ENG_T_LTYPES);
}
