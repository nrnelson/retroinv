/*
 * RPTRUN.C - the requests of a report or label run, laid out as they
 * arrive (RPTRUN.H).
 *
 * A row of the page buffer is its values one after another, each with
 * its NUL, at RPT_ROWMAX bytes a row; nv counts the values of each, so
 * replay can give rpt_line the same values in the same order.  After
 * the rows the far block holds the names of the locations above the
 * one a bin label is for, one for each level below the walk's first,
 * LINK_LOCNAMELEN + 1 bytes each.
 */
#define MSG_PATHS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "PRINT.H"
#include "RPT.H"
#include "CODETAB.H"
#include "SCRLFORM.H"
#include "RPTRUN.H"

/* The page buffer: LINK_PAGEMAX rows of RPT_ROWMAX bytes, then a name
   for each level of the tree. */
#define RU_NAMESZ       (LINK_LOCNAMELEN + 1)
#define RU_NAMES        ((u16)(LINK_PAGEMAX * RPT_ROWMAX))
#define RU_PAGEBYTES    ((u16)(RU_NAMES + LINK_MAXLOCDEPTH * RU_NAMESZ))
extern char ru_page_fits[1];
extern char ru_page_fits[((long)LINK_PAGEMAX * RPT_ROWMAX +
                          LINK_MAXLOCDEPTH * RU_NAMESZ <=
                          (long)CMEM_MAXBLK) ? 1 : 2];

/* A bin label's path, before it is cut: the first location's path and
   the names below it. */
#define RU_PATHMAX      (LINK_LOCPATHLEN + LINK_MAXLOCDEPTH * \
                         (MSG_PATHSEPW + LINK_LOCNAMELEN))

const char rrun_k_cat[] = "CAT";
const char rrun_k_loc[] = "LOC";
const char rrun_k_status[] = "STATUS";
const char rrun_k_tag[] = "TAG";
const char rrun_k_works[] = "WORKS";
const char rrun_k_order[] = "ORDER";
const char rrun_k_overdue[] = "OVERDUE";
const char rrun_k_asof[] = "ASOF";

static const char k_locid[] = "LOC_ID";
static const char k_page[] = "PAGE";
static const char k_after[] = "AFTER";
static const char k_zero[] = "0";

/* The fields the run reads of REPORT ITEMS rows (the location, for the
   groups) and of LOC LIST rows (the bin labels), in u->fi's order. */
static const char *const ru_items[] = { "LOC_ID", "LOC_NAME" };
static const char *const ru_locs[] = {
    "DEPTH", "LOC_NAME", "LOC_CODE", "LTYPE"
};
#define FI_ID           0
#define FI_NAME         1
#define FI_DEPTH        0
#define FI_LNAME        1
#define FI_CODE         2
#define FI_TYPE         3
extern char ru_fi_fits[1];
extern char ru_fi_fits[(sizeof ru_locs / sizeof ru_locs[0] <= RRUN_NFI &&
                        sizeof ru_items / sizeof ru_items[0] <=
                        RRUN_NFI) ? 1 : 2];

/* ---------------------------------------------------------------------
 * Lines.
 * --------------------------------------------------------------------- */

/* The places of the n fields names in the F line ln into u->fi, RRUN_NOF
   for one it lacks. */
static void find_fields(rrun *u, const rq_line *ln,
                        const char *const *names, u16 n)
{
    u16 i;
    u16 j;

    for (j = 0; j < n; j++) {
        u->fi[j] = RRUN_NOF;
        for (i = 0; i < ln->n; i++)
            if (strcmp(ln->v[i], names[j]) == 0)
                u->fi[j] = (u8)i;
    }
}

/* The values of R line ln as the next row of the page buffer. */
static void store(rrun *u, const rq_line *ln)
{
    u16 used;
    u16 n;
    u16 i;

    if (u->nrows >= LINK_PAGEMAX)
        return;
    used = 0;
    for (i = 0; i < ln->n && used < RPT_ROWMAX; i++) {
        n = (u16)strlen(ln->v[i]);
        if (n > RPT_ROWMAX - used - 1)
            n = (u16)(RPT_ROWMAX - used - 1);
        memcpy(u->row + used, ln->v[i], n);
        u->row[used + n] = '\0';
        used = (u16)(used + n + 1);
    }
    cmem_put(u->pg, (u16)(u->nrows * RPT_ROWMAX), u->row, used);
    u->nv[u->nrows++] = (u8)i;
}

/* F and K lines go to the layout, which only notes them (the F line's
   places of the location's ID and name kept for the groups); the values
   of each R line are stored as the next row of the page buffer. */
static int row_line(void *arg, const rq_line *ln)
{
    rrun *u;

    u = (rrun *)arg;
    if (ln->kind == 'R') {
        store(u, ln);
        return 0;
    }
    if (ln->kind == 'F')
        find_fields(u, ln, ru_items,
                    (u16)(sizeof ru_items / sizeof ru_items[0]));
    rpt_line(u->r, ln);
    return 0;
}

/* LOC LIST of the bin labels: the F line's places of the fields of
   ru_locs, and each row stored. */
static int loc_line(void *arg, const rq_line *ln)
{
    rrun *u;

    u = (rrun *)arg;
    if (ln->kind == 'F')
        find_fields(u, ln, ru_locs, (u16)(sizeof ru_locs / sizeof ru_locs[0]));
    else if (ln->kind == 'R')
        store(u, ln);
    return 0;
}

/* Row r of the page buffer into u->row and ln. */
static void fetch(rrun *u, u16 r, rq_line *ln)
{
    u16 i;
    u16 at;

    cmem_get(u->pg, (u16)(r * RPT_ROWMAX), u->row, RPT_ROWMAX);
    ln->kind = 'R';
    ln->over = 0;
    at = 0;
    for (i = 0; i < u->nv[r]; i++) {
        ln->v[i] = u->row + at;
        at = (u16)(at + strlen(u->row + at) + 1);
    }
    ln->n = u->nv[r];
}

/* Value j (of u->fi) of row ln, "" when the row lacks it. */
static const char *field(const rrun *u, const rq_line *ln, u16 j)
{
    return u->fi[j] < ln->n ? ln->v[u->fi[j]] : "";
}

/* Give the layout one line of kind with the n values v. */
static void put_line(rrun *u, char kind, const char *const *v, u16 n)
{
    rq_line ln;
    u16 i;

    ln.kind = kind;
    ln.over = 0;
    ln.n = n;
    for (i = 0; i < n; i++)
        ln.v[i] = (char *)v[i];
    rpt_line(u->r, &ln);
}

/* CAT LIST: the categories of the valuation's totals, code then name. */
static int cat_line(void *arg, const rq_line *ln)
{
    if (ln->kind == 'R' && ln->n >= 2)
        rpt_cat(((rrun *)arg)->r, ln->v[0], ln->v[1]);
    return 0;
}

/* ITEM GET: its K lines go to the layout, and its LOC_ID into u->row. */
static int item_kline(void *arg, const rq_line *ln)
{
    rrun *u;

    u = (rrun *)arg;
    if (ln->kind == 'K' && ln->n >= 2 && strcmp(ln->v[0], k_locid) == 0)
        txt_copy(u->row, ln->v[1], LINK_IDLEN + 1);
    return row_line(arg, ln);
}

/* ---------------------------------------------------------------------
 * Requests.
 * --------------------------------------------------------------------- */

/* 1 when the run is to end: the job failed or the step stopped it. */
static int ended(const rrun *u)
{
    return u->stopped || pr_failed(u->p) != PR_OK;
}

/* One page laid out: the progress, and the step's wish to stop; more is
   1 when another page is to come. */
static void stepped(rrun *u, rrun_fn step, void *arg, int more)
{
    u->pages++;
    if (step(arg, more && pr_failed(u->p) == PR_OK))
        u->stopped = 1;
}

/* 1 when loc names no location: "" (all of them) or "0" (none). */
static int no_loc(const char *loc)
{
    return loc[0] == '\0' || strcmp(loc, k_zero) == 0;
}

/* 1 when the run lists in the tree's order, grouped by location in the
   reports: RPT_VALUE, RPT_ILABEL but for one label, RPT_LIST in
   location order or below. */
static int walks(const rrun *u)
{
    return u->kind != RPT_LOANS && u->kind != RPT_BLABEL && !rrun_one(u) &&
           (u->kind != RPT_LIST || u->below ||
            strcmp(u->order, rrun_k_loc) == 0);
}

/* LOC GET loc into *x (scr_loc_info). */
static int loc_get(link *l, const char *loc, scr_locinfo *x, rq_res *res)
{
    rq q;

    memset(x, 0, sizeof *x);
    rq_begin(&q, "LOC GET");
    rq_word(&q, loc);
    return rq_send(l, &q, scr_loc_info, x, res);
}

/* Lay out the rows of the page buffer, until the job stops; in a
   grouped report each change of location first starts a group, headed
   with its path (LOC GET; its name alone when it has gone since, "" for
   no location). */
static int replay(rrun *u, link *l, rq_res *res)
{
    scr_locinfo x;
    rq_line ln;
    const char *id;
    u16 r;
    int rc;

    for (r = 0; r < u->nrows && pr_failed(u->p) == PR_OK; r++) {
        fetch(u, r, &ln);
        id = field(u, &ln, FI_ID);
        if (u->group && (!u->ingroup || strcmp(id, u->last) != 0)) {
            u->ingroup = 1;
            txt_copy(u->last, id, (u16)sizeof u->last);
            x.path[0] = '\0';
            if (strcmp(id, k_zero) != 0) {
                rc = loc_get(l, id, &x, res);
                if (rc == RQ_ENOTFOUND)
                    txt_copy(x.path, field(u, &ln, FI_NAME),
                             (u16)sizeof x.path);
                else if (rc != RQ_OK)
                    return rc;
                fetch(u, r, &ln);
            }
            rpt_group(u->r, x.path);
        }
        rpt_line(u->r, &ln);
    }
    u->nrows = 0;
    return RQ_OK;
}

/* REPORT ITEMS with the run's filters, page after page: the tree's
   order from location loc ("0": the items of no location) with ALL=1
   when all is 1, else the items at loc ("": all of them), in RPT_LIST's
   order when it is not LOC. */
static int items(rrun *u, link *l, const char *loc, int all, rrun_fn step,
                 void *arg, rq_res *res)
{
    char after[LINK_MAXCURSOR + 1];
    rq q;
    int rc;

    after[0] = '\0';
    do {
        rq_begin(&q, "REPORT ITEMS");
        if (u->cat[0] != '\0')
            rq_pair(&q, rrun_k_cat, u->cat);
        if (loc[0] != '\0')
            rq_pair(&q, rrun_k_loc, loc);
        if (all)
            rq_pair(&q, "ALL", "1");
        if (u->kind == RPT_VALUE)
            rq_pair(&q, rrun_k_status, "A");
        else if (u->kind == RPT_LIST && u->status[0] != '\0')
            rq_pair(&q, rrun_k_status, u->status);
        if (u->tag[0] != '\0')
            rq_pair(&q, rrun_k_tag, u->tag);
        if (u->works[0] != '\0')
            rq_pair(&q, rrun_k_works, u->works);
        if (u->kind == RPT_LIST && !u->group && u->order[0] != '\0')
            rq_pair(&q, rrun_k_order, u->order);
        rq_pairu(&q, k_page, LINK_PAGEMAX);
        if (after[0] != '\0')
            rq_pair(&q, k_after, after);
        rc = rq_send(l, &q, row_line, u, res);
        if (rc != RQ_OK)
            return rc;
        strcpy(after, res->more);
        rc = replay(u, l, res);
        if (rc != RQ_OK)
            return rc;
        stepped(u, step, arg, after[0] != '\0');
    } while (after[0] != '\0' && !ended(u));
    return RQ_OK;
}

/* One bin label: the name, code and path given, the type's name from
   code type. */
static void blabel(rrun *u, const char *name, const char *code,
                   const char *path, const char *type)
{
    static const char *const names[] = {
        "LOC_NAME", "LOC_CODE", "PATH", "LT_NAME"
    };
    char tname[LINK_CODENAMELEN + 1];
    const char *v[sizeof names / sizeof names[0]];

    ct_name(&u->types, type, tname);
    v[0] = name;
    v[1] = code;
    v[2] = path;
    v[3] = tname;
    put_line(u, 'F', names, (u16)(sizeof v / sizeof v[0]));
    put_line(u, 'R', v, (u16)(sizeof v / sizeof v[0]));
}

/* The bin labels of the page buffer's LOC LIST rows, below the walk's
   first location of depth top and path base (top 0, base "" for the
   whole tree): each row's path is base and the names of the rows above
   it, kept by depth after the rows, then its own name. */
static void blabels(rrun *u, u16 top, const char *base)
{
    char path[RU_PATHMAX + 1];
    char name[RU_NAMESZ];
    rq_line ln;
    const char *s;
    u16 r;
    u16 d;
    u16 i;

    for (r = 0; r < u->nrows && !ended(u); r++) {
        fetch(u, r, &ln);
        s = field(u, &ln, FI_DEPTH);
        d = msg_dec(&s, LINK_MAXLOCDEPTH);
        if (d <= top || d > LINK_MAXLOCDEPTH)
            continue;
        txt_copy(name, field(u, &ln, FI_LNAME), RU_NAMESZ);
        cmem_put(u->pg, (u16)(RU_NAMES + (d - 1) * RU_NAMESZ), name,
                 RU_NAMESZ);
        strcpy(path, base);
        for (i = top; i < d; i++) {
            cmem_get(u->pg, (u16)(RU_NAMES + i * RU_NAMESZ), name,
                     RU_NAMESZ);
            if (path[0] != '\0')
                strcat(path, MSG_PATHSEP);
            strcat(path, name);
        }
        blabel(u, field(u, &ln, FI_LNAME), field(u, &ln, FI_CODE), path,
               field(u, &ln, FI_TYPE));
    }
    u->nrows = 0;
}

/* The bin labels: location loc's (LOC GET), then with below, or every
   location when loc names none, those of LOC LIST [UNDER=loc], page by
   page. */
static int blabel_run(rrun *u, link *l, rrun_fn step, void *arg,
                      rq_res *res)
{
    char after[LINK_MAXCURSOR + 1];
    scr_locinfo x;
    rq q;
    int all;
    int rc;

    all = no_loc(u->loc);
    x.path[0] = '\0';
    x.depth = 0;
    if (!all) {
        rc = loc_get(l, u->loc, &x, res);
        if (rc != RQ_OK)
            return rc;
        blabel(u, x.name, x.code, x.path, x.type);
        stepped(u, step, arg, u->below);
        if (!u->below || ended(u))
            return RQ_OK;
    }
    after[0] = '\0';
    do {
        rq_begin(&q, "LOC LIST");
        if (!all)
            rq_pair(&q, "UNDER", u->loc);
        rq_pairu(&q, k_page, LINK_PAGEMAX);
        if (after[0] != '\0')
            rq_pair(&q, k_after, after);
        rc = rq_send(l, &q, loc_line, u, res);
        if (rc != RQ_OK)
            return rc;
        strcpy(after, res->more);
        blabels(u, x.depth, x.path);
        stepped(u, step, arg, after[0] != '\0');
    } while (after[0] != '\0' && !ended(u));
    return RQ_OK;
}

/* The item label of u->item: ITEM GET, then its location's name as a
   K line (none when it has gone since). */
static int item_one(rrun *u, link *l, rrun_fn step, void *arg,
                    rq_res *res)
{
    const char *v[2];
    scr_locinfo x;
    rq q;
    int rc;

    u->row[0] = '\0';
    rq_begin(&q, "ITEM GET");
    rq_word(&q, u->item);
    rc = rq_send(l, &q, item_kline, u, res);
    if (rc != RQ_OK)
        return rc;
    if (!no_loc(u->row)) {
        rc = loc_get(l, u->row, &x, res);
        if (rc == RQ_OK) {
            v[0] = "LOC_NAME";
            v[1] = x.name;
            put_line(u, 'K', v, 2);
        } else if (rc != RQ_ENOTFOUND) {
            return rc;
        }
    }
    rpt_record(u->r);
    stepped(u, step, arg, 0);
    return RQ_OK;
}

/* ---------------------------------------------------------------------
 * Interface.
 * --------------------------------------------------------------------- */

int rrun_open(rrun *u)
{
    u->r = NULL;
    u->nrows = 0;
    u->pages = 0;
    u->stopped = 0;
    u->group = 0;
    u->ingroup = 0;
    u->types.b = NULL;
    u->types.n = 0;
    u->pg = cmem_alloc(RU_PAGEBYTES);
    if (u->pg != NULL && (u->kind != RPT_BLABEL || ct_open(&u->types)))
        return PR_OK;
    cmem_free(u->pg);
    u->pg = NULL;
    return PR_ENOMEM;
}

int rrun_layout(rrun *u, const rpt_geom *g, const char *sub)
{
    u8 opts;

    opts = 0;
    u->group = (u8)(u->kind <= RPT_VALUE && walks(u));
    if (u->group)
        opts = RPT_O_GROUP;
    if (u->kind == RPT_LOANS && u->overdue)
        opts = RPT_O_OVERDUE;
    return rpt_open(&u->r, u->kind, opts, u->p, g, u->date, sub);
}

int rrun_one(const rrun *u)
{
    return (u->kind == RPT_ILABEL && u->item[0] != '\0') ||
           (u->kind == RPT_BLABEL && !no_loc(u->loc) && !u->below);
}

int rrun_go(rrun *u, link *l, rrun_fn step, void *arg, rq_res *res)
{
    rq q;
    int rc;

    if (u->kind == RPT_VALUE) {
        rq_begin(&q, "CAT LIST");
        rc = rq_send(l, &q, cat_line, u, res);
        if (rc != RQ_OK)
            return rc;
    }
    if (u->kind == RPT_BLABEL) {
        rc = ct_load(&u->types, l, "LTYPE", res);
        if (rc != RQ_OK)
            return rc;
        return blabel_run(u, l, step, arg, res);
    }
    if (u->kind == RPT_ILABEL && rrun_one(u))
        return item_one(u, l, step, arg, res);
    if (walks(u))
        return items(u, l, u->loc[0] != '\0' ? u->loc : k_zero,
                     u->loc[0] == '\0' ||
                     (u->below && strcmp(u->loc, k_zero) != 0),
                     step, arg, res);
    if (u->kind != RPT_LOANS)
        return items(u, l, u->loc, 0, step, arg, res);
    res->more[0] = '\0';
    do {
        rq_begin(&q, "REPORT LOANS");
        if (u->overdue)
            rq_pair(&q, rrun_k_overdue, "1");
        rq_pair(&q, rrun_k_asof, u->date);
        rq_pairu(&q, k_page, LINK_PAGEMAX);
        if (res->more[0] != '\0')
            rq_pair(&q, k_after, res->more);
        rc = rq_send(l, &q, row_line, u, res);
        if (rc != RQ_OK)
            return rc;
        rc = replay(u, l, res);
        if (rc != RQ_OK)
            return rc;
        stepped(u, step, arg, res->more[0] != '\0');
    } while (res->more[0] != '\0' && !ended(u));
    return RQ_OK;
}

u32 rrun_close(rrun *u, int rc)
{
    u32 n;

    n = 0;
    if (u->r != NULL) {
        n = rpt_count(u->r);
        if (rc != RQ_OK || u->stopped || pr_failed(u->p) != PR_OK)
            rpt_abort(u->r);
        else
            rpt_close(u->r);
        u->r = NULL;
    }
    cmem_free(u->pg);
    u->pg = NULL;
    ct_close(&u->types);
    return n;
}
