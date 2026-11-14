/*
 * PAGER.C - paging one list command through the protocol.
 *
 * Rows live in one far block of 2 * ps rows, A at rows 0..ps-1 and B at
 * ps..2ps-1; a row number i counts A's rows first (i < na) and then
 * B's.  Every page fetch for A fills the B half: the new A is moved down
 * (or swapped with the old A, for going up) only once its requests have
 * all succeeded, so a failed request leaves A as it was; B is then
 * dropped and the window kept within A's rows, and B is fetched again
 * when it is needed next.  The page start cursors before A are a ring
 * in a second far block, which the first push allocates and a cursor
 * too long for it replaces with a larger one.
 *
 * Requests are built and sent in pg_fetch() alone, which keeps the
 * request on its own stack; the public moves own one rq_res, which
 * pg_fetch() fills and the move hands to the fail function when a request
 * fails.
 *
 * With PAGER_ABOVE (PAGER.H) the pager also keeps above, pages an
 * unpaged list's response itself, hands a walk's keys to the key sink,
 * and PAGERINT.H gives a module that surveys a list or jumps into it
 * pg_fetch, the page load of PG_HOME (pg_load) and the ring's push.
 * Without it pg_fetch and the push are static, pg_load is not built, and
 * the code is that of a pager without them.
 */
#define PAGER_CALLS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "PAGER.H"
#include "PAGERINT.H"

/* A row key must hold every key a list returns: an item, loan or
   service record ID (all LINK_IDLEN digits: the engine gives the three
   the same width, which LINK.C checks against LINK_IDLEN; a location's
   LOC_ID is one too), a category code, a user name.  The build fails
   otherwise. */
extern char lv_key_holds_id[1];
extern char lv_key_holds_id[(LV_KEYSZ > LINK_IDLEN) ? 1 : 2];
extern char lv_key_holds_cat[1];
extern char lv_key_holds_cat[(LV_KEYSZ > LINK_CATLEN) ? 1 : 2];
extern char lv_key_holds_user[1];
extern char lv_key_holds_user[(LV_KEYSZ > LINK_USERLEN) ? 1 : 2];

/* The rows of the largest window, two pages, fit one far block. */
extern char lv_rows_fit[1];
extern char lv_rows_fit[((u32)2 * LV_MAXPAGE * LV_ROWBYTES <= CMEM_MAXBLK)
                        ? 1 : 2];

/* An empty ring's first block fits any cursor, so a push ends. */
extern char lv_ring_fits[1];
extern char lv_ring_fits[(LV_RINGSZ >= LV_CURSZ) ? 1 : 2];

/* PAGERINT.H's functions: shared with a survey's module when the pager
   keeps above, else the pager's own. */
#ifdef PAGER_ABOVE
#define PG_SHARED
#else
#define PG_SHARED       static
#endif

/* What the response callback of a fill needs. */
typedef struct {
    pg  *p;
    u16  got;                   /* rows of the page so far              */
    u16  cap;                   /* rows the request may still add       */
#ifdef PAGER_ABOVE
    u16  skip;                  /* an unpaged list's rows before the
                                   page, 0 for a paged one              */
    u16  seen;                  /* R lines of the response              */
#endif
} pg_fill;

/* The rows held: A's, and B's when loaded. */
static u16 total(pg *p)
{
    return (u16)(p->na + (p->bload ? p->nb : 0));
}

/* The row of the block that holds row i. */
static u16 slot_of(pg *p, u16 i)
{
    return i < p->na ? i : (u16)(p->ps + (i - p->na));
}

/* The offset of row slot in the block. */
static u16 row_off(u16 slot)
{
    return (u16)(slot * LV_ROWBYTES);
}

#ifdef PAGER_ABOVE
/* above less n rows, not below 0; PG_NOABOVE stays. */
static void less_above(pg *p, u16 n)
{
    if (p->above != PG_NOABOVE)
        p->above = p->above > n ? p->above - n : 0;
}
#endif

const char *lv_val(const lv_rec *r, const char *name)
{
    u16 i;

    for (i = 0; i < r->n; i++)
        if (strcmp(r->names[i], name) == 0)
            return r->vals[i];
    return "";
}

void lv_col(char *t, u16 *at, const char *s, u16 w, u8 flags)
{
    u16 n;
    u16 i;

    while (*s == ' ')
        s++;
    n = (u16)strlen(s);
    if (*at + w > LV_ROWTEXT)
        return;
    for (i = 0; i < w; i++)
        t[*at + i] = ' ';
    if (n > w) {
        memcpy(t + *at, s, w);
        if ((flags & LV_MARK) && w > 0)
            t[*at + w - 1] = MSG_CUTMARK;
    } else {
        memcpy(t + *at + ((flags & LV_RIGHT) ? w - n : 0), s, n);
    }
    *at = (u16)(*at + w);
    if (*at < LV_ROWTEXT)
        t[(*at)++] = ' ';
    t[*at] = '\0';
}

void lv_datecol(char *t, u16 *at, const char *wire)
{
    char date[MSG_DATEW + 1];

    msg_date(date, wire);
    lv_col(t, at, date, MSG_DATEW, 0);
}

/* ---------------------------------------------------------------------
 * The ring of page start cursors: in its block, oldest first, each with
 * its NUL, in the first ringb bytes.
 * --------------------------------------------------------------------- */
/* Drop the oldest cursor, which ends within the block's first LV_CURSZ
   bytes. */
static void ring_drop(pg *p)
{
    char cur[LV_CURSZ];
    u16 n;

    n = p->ringb < LV_CURSZ ? p->ringb : LV_CURSZ;
    cmem_get(p->ring, 0, cur, n);
    n = (u16)(strlen(cur) + 1);
    p->ringb = (u16)(p->ringb - n);
    cmem_move(p->ring, 0, n, p->ringb);
    p->ringn--;
}

/* Push cur on the ring (PAGERINT.H). */
PG_SHARED void pg_ring_push(pg *p, const char *cur)
{
    cmem_blk *b;
    u16 n;

    n = (u16)(strlen(cur) + 1);
    if (p->ring == NULL)
        p->ring = cmem_alloc(LV_RINGSZ);
    if (p->ring == NULL)
        return;
    if (p->ringn == LV_MAXBACK)
        ring_drop(p);
    if (p->ringb + n > cmem_size(p->ring) &&
        cmem_size(p->ring) < LV_RINGMAX) {
        b = cmem_alloc(LV_RINGMAX);
        if (b != NULL) {
            cmem_copy(b, 0, p->ring, 0, p->ringb);
            cmem_free(p->ring);
            p->ring = b;
        }
    }
    while (p->ringb + n > cmem_size(p->ring))
        ring_drop(p);
    cmem_put(p->ring, p->ringb, cur, n);
    p->ringb = (u16)(p->ringb + n);
    p->ringn++;
}

/* 1 when the ring held a cursor, popped into cur (LV_CURSZ bytes). */
static int ring_pop(pg *p, char *cur)
{
    u16 n;
    u16 i;

    if (p->ringn == 0)
        return 0;
    /* The newest cursor ends the bytes in use, within their last
       LV_CURSZ, and starts after the NUL before its own. */
    n = p->ringb < LV_CURSZ ? p->ringb : LV_CURSZ;
    p->ringb = (u16)(p->ringb - n);
    cmem_get(p->ring, p->ringb, cur, n);
    for (i = (u16)(n - 1); i > 0 && cur[i - 1] != '\0'; i--)
        ;
    memmove(cur, cur + i, n - i);
    p->ringb = (u16)(p->ringb + i);
    p->ringn--;
    return 1;
}

/* ---------------------------------------------------------------------
 * Fetching.
 * --------------------------------------------------------------------- */
/* Put a row of key and text (made by the source's format function, or
   the lead row) into slot of the block, text padded to LV_ROWTEXT with
   spaces and followed by mark. */
static void put_row(pg *p, u16 slot, char *key, char *text, int mark)
{
    u16 i;

    text[LV_ROWTEXT] = '\0';
    for (i = (u16)strlen(text); i < LV_ROWTEXT; i++)
        text[i] = ' ';
    key[LV_KEYSZ - 1] = '\0';
    /* The byte after the text holds the row's mark. */
    text[LV_ROWTEXT] = (char)mark;
    i = row_off(slot);
    cmem_put(p->rows, i, key, LV_KEYSZ);
    cmem_put(p->rows, (u16)(i + LV_KEYSZ), text, LV_ROWTEXT + 1);
}

/* rq_fn of pg_fetch: an F line's field names into p->fn, each R line as
   a row after the x->got already in B's half while x->cap allows (only
   counted during a walk, its key handed to the key sink when there is
   one), past the x->skip rows before an unpaged list's page; 0, to go
   on. */
static int fill_line(void *arg, const rq_line *ln)
{
    pg_fill *x;
    pg *p;
    char *vals[RQ_MAXVALS];
    char text[LV_ROWTEXT + 1];
    char key[LV_KEYSZ];
    lv_rec r;
    u16 i;
    u16 n;

    x = (pg_fill *)arg;
    p = x->p;
    if (ln->kind == 'F') {
        n = ln->n < LV_MAXFIELDS ? ln->n : LV_MAXFIELDS;
        for (i = 0; i < n; i++)
            txt_copy(p->fn[i], ln->v[i], (u16)sizeof p->fn[i]);
        p->nf = (u8)n;
        return 0;
    }
#ifdef PAGER_ABOVE
    if (ln->kind == 'R' && x->seen++ < x->skip)
        return 0;
#endif
    if (ln->kind != 'R' || x->cap == 0)
        return 0;
    x->cap--;
#ifdef PAGER_ABOVE
    if (!p->walk || p->keyfn != NULL) {
#else
    if (!p->walk) {
#endif
        n = ln->n < p->nf ? ln->n : p->nf;
        for (i = 0; i < n; i++)
            vals[i] = ln->v[i];
        r.n = n;
        r.names = p->fnp;
        r.vals = vals;
        text[0] = '\0';
        memset(key, 0, sizeof key);
        p->src->fmt(p->src->arg, &r, text, key);
#ifdef PAGER_ABOVE
        if (p->walk) {
            p->keyfn(p->karg, key);
            x->got++;
            return 0;
        }
#endif
        put_row(p, (u16)(p->ps + x->got), key, text,
                p->src->mark != NULL && p->src->mark(p->src->arg, &r));
    }
    x->got++;
    return 0;
}

/* Fill a page (PAGERINT.H). */
PG_SHARED int pg_fetch(pg *p, rq_res *res, const char *start, u16 have,
                       u16 *n, char *next, u8 *more)
{
    const lv_src *s;
    const char *fields;
    char text[LV_ROWTEXT + 1];
    char key[LV_KEYSZ];
    pg_fill x;
    rq q;
    int rc;

    s = p->src;
    fields = p->walk && s->walkfields != NULL ? s->walkfields
                                              : s->fields[p->order];
    x.p = p;
    if (start[0] == '\0' && have == 0 && p->leadtext != NULL &&
        p->ps > 1) {
#ifdef PAGER_ABOVE
        /* A survey's walk counts the lead row, as any row, unstored. */
        if (!p->walk)
#endif
        {
            txt_copy(text, p->leadtext, (u16)sizeof text);
            memset(key, 0, sizeof key);
            txt_copy(key, p->leadkey, (u16)sizeof key);
            put_row(p, p->ps, key, text, 0);
        }
        have = 1;
    }
    x.got = have;
    *n = have;
    if (next != start)
        strcpy(next, start);
    for (;;) {
        rq_begin(&q, s->cmd);
        if (s->args != NULL)
            s->args(s->arg, &q);
        if (s->norders > 0 &&
            strcmp(s->orders[p->order], LV_TREEORDER) == 0) {
            rq_pair(&q, "LOC", "0");
            rq_pair(&q, "ALL", "1");
        } else if (s->norders > 0) {
            rq_pair(&q, "ORDER", s->orders[p->order]);
        }
        if (fields != NULL)
            rq_pair(&q, "FIELDS", fields);
        x.cap = (u16)(p->ps - x.got);
#ifdef PAGER_ABOVE
        x.skip = p->unpaged ? txt_num(next, 0xFFFFu) : 0;
        x.seen = 0;
        if (!p->unpaged)
#endif
        {
            if (x.cap > LINK_PAGEMAX)
                x.cap = LINK_PAGEMAX;
            rq_pairu(&q, "PAGE", (u32)x.cap);
            if (next[0] != '\0')
                rq_pair(&q, "AFTER", next);
        }
        p->nf = 0;
        rc = rq_send(p->l, &q, fill_line, &x, res);
        if (rc != RQ_OK) {
            *n = x.got;
            return rc;
        }
        *more = (u8)(res->more[0] != '\0');
        strcpy(next, res->more);
#ifdef PAGER_ABOVE
        /* An unpaged list's one request asked for all the page misses;
           the next page starts after the rows it skipped and kept. */
        if (p->unpaged) {
            x.skip = (u16)(x.skip + x.got - have);
            *more = (u8)(x.seen > x.skip);
            txt_u32(next, x.skip);
        }
#endif
        if (!*more || x.got >= p->ps)
            break;
        if (p->wait(p->arg, p->walk,
                    p->walk ? p->walked + x.got : (u32)x.got)) {
            p->stopped = 1;
            break;
        }
    }
    *n = x.got;
    return RQ_OK;
}

/* Move n rows of the block from slot from to slot to. */
static void move_rows(pg *p, u16 to, u16 from, u16 n)
{
    if (n > 0)
        cmem_move(p->rows, row_off(to), row_off(from),
                  (u16)(n * LV_ROWBYTES));
}

/* When the window passes the last row held, it ends on that row. */
static void end_window(pg *p)
{
    u16 t;

    t = total(p);
    if (p->first + p->ps > t)
        p->first = t > p->ps ? (u16)(t - p->ps) : 0;
}

/* After B's half was overwritten: the window within A's rows. */
static void keep_a(pg *p)
{
    p->bload = 0;
    p->nb = 0;
    if (p->sel >= p->na)
        p->sel = p->na > 0 ? (u16)(p->na - 1) : 0;
    end_window(p);
    if (p->first > p->sel)
        p->first = p->sel;
}

/* Load A from cursor start (which may be p->cura); B is emptied. */
static int load_a(pg *p, rq_res *res, const char *start)
{
    char next[LV_CURSZ];
    u16 n;
    u8 more;
    int rc;

    rc = pg_fetch(p, res, start, 0, &n, next, &more);
    p->bload = 0;
    p->nb = 0;
    if (rc != RQ_OK) {
        keep_a(p);
        return rc;
    }
    move_rows(p, 0, p->ps, n);
#ifdef PAGER_ABOVE
    /* above: 0 for the first page, kept for A's own start.  From the
       moves, another start is the page before A: above loses its rows
       when it ends where the old A started, else they are not known (a
       stop had left the page short, or the list changed).  PG_END and
       pg_load set above themselves after a load. */
    if (start[0] == '\0') {
        p->above = 0;
    } else if (start != p->cura) {
        if (more && strcmp(next, p->cura) == 0)
            less_above(p, n);
        else
            p->above = PG_NOABOVE;
    }
#endif
    if (start != p->cura)
        strcpy(p->cura, start);
    p->na = n;
    p->amore = more;
    strcpy(p->curb, next);
    p->nb = 0;
    return RQ_OK;
}

/* Fetch B when A has a page after it, or go on filling a B that a stop
   left short. */
static int more_b(pg *p, rq_res *res)
{
    u16 n;
    int rc;

    /* Once the wait function has stopped a fill, the move asks for
       nothing more. */
    if (p->stopped)
        return RQ_OK;
    if (!p->bload) {
        if (!p->amore)
            return RQ_OK;
        rc = pg_fetch(p, res, p->curb, 0, &n, p->curn, &p->bmore);
        if (n > 0 || rc == RQ_OK) {
            p->bload = 1;
            p->nb = n;
        }
        return rc;
    }
    if (!p->bmore || p->nb >= p->ps)
        return RQ_OK;
    rc = pg_fetch(p, res, p->curn, p->nb, &n, p->curn, &p->bmore);
    p->nb = n;
    return rc;
}

/* Once the window lies wholly in B, B becomes A; then fetch what the
   window shows beyond A. */
static int normalize(pg *p, rq_res *res)
{
    if (p->bload && p->first >= p->na) {
#ifdef PAGER_ABOVE
        if (p->above != PG_NOABOVE)
            p->above += p->na;
#endif
        pg_ring_push(p, p->cura);
        strcpy(p->cura, p->curb);
        move_rows(p, 0, p->ps, p->nb);
        p->first = (u16)(p->first - p->na);
        p->sel = (u16)(p->sel - p->na);
        p->na = p->nb;
        p->amore = p->bmore;
        strcpy(p->curb, p->curn);
        p->bload = 0;
        p->nb = 0;
    }
    if (p->first + p->ps > total(p))
        return more_b(p, res);
    return RQ_OK;
}

/* 1 when row i exists, fetching B when needed; *rc receives a
   failure. */
static int avail(pg *p, rq_res *res, u16 i, int *rc)
{
    *rc = RQ_OK;
    if (i < total(p))
        return 1;
    *rc = more_b(p, res);
    return *rc == RQ_OK && i < total(p);
}

/* ---------------------------------------------------------------------
 * The steps of the moves.
 * --------------------------------------------------------------------- */
#ifdef PAGER_ABOVE
int pg_load(pg *p, rq_res *res, const char *start, u32 above)
{
    int rc;

    rc = load_a(p, res, start);
    if (rc == RQ_OK) {
        p->ringn = 0;
        p->ringb = 0;
        p->first = 0;
        p->sel = 0;
        p->above = above;
    }
    return rc;
}

/* The first page into A, the window and selection at its top. */
static int do_home(pg *p, rq_res *res)
{
    return pg_load(p, res, "", 0);
}
#else
/* The first page into A, the window and selection at its top. */
static int do_home(pg *p, rq_res *res)
{
    int rc;

    rc = load_a(p, res, "");
    if (rc == RQ_OK) {
        p->ringn = 0;
        p->ringb = 0;
        p->first = 0;
        p->sel = 0;
    }
    return rc;
}
#endif

/* The page before A into A, the old A becoming B; *moved 1 when the
   window may go on up (first and sel then count the new A's rows). */
static int prev_page(pg *p, rq_res *res, int *moved)
{
    char pc[LV_CURSZ];
    char next[LV_CURSZ];
    u8 ta[LV_ROWBYTES];
    u8 tb[LV_ROWBYTES];
    u16 n;
    u16 i;
    u16 m;
    u8 more;
    int rc;

    *moved = 0;
    if (!ring_pop(p, pc)) {
        if (p->cura[0] == '\0')
            return RQ_OK;
        return do_home(p, res);
    }
    rc = pg_fetch(p, res, pc, 0, &n, next, &more);
    if (rc != RQ_OK) {
        pg_ring_push(p, pc);
        keep_a(p);
        return rc;
    }
    if (more && strcmp(next, p->cura) == 0) {
        m = n > p->na ? n : p->na;
        for (i = 0; i < m; i++) {
            cmem_get(p->rows, row_off(i), ta, LV_ROWBYTES);
            cmem_get(p->rows, row_off((u16)(p->ps + i)), tb, LV_ROWBYTES);
            cmem_put(p->rows, row_off(i), tb, LV_ROWBYTES);
            cmem_put(p->rows, row_off((u16)(p->ps + i)), ta, LV_ROWBYTES);
        }
        p->nb = p->na;
        p->bload = 1;
        p->bmore = p->amore;
        strcpy(p->curn, p->curb);
        strcpy(p->curb, p->cura);
        p->amore = 1;
        p->first = (u16)(p->first + n);
        p->sel = (u16)(p->sel + n);
        *moved = n > 0;
    } else {
        /* The list changed under the pager: show the page found alone. */
        move_rows(p, 0, p->ps, n);
        p->bload = 0;
        p->nb = 0;
        p->amore = more;
        strcpy(p->curb, next);
        p->first = 0;
        p->sel = n > 0 ? (u16)(n - 1) : 0;
    }
#ifdef PAGER_ABOVE
    /* B holds the old A only when the page found ends where A started;
       else the rows above it are not known, but on the first page. */
    if (pc[0] == '\0')
        p->above = 0;
    else if (p->bload)
        less_above(p, n);
    else
        p->above = PG_NOABOVE;
#endif
    strcpy(p->cura, pc);
    p->na = n;
    return RQ_OK;
}

/* The selection one row down, the window scrolling with it. */
static int do_down(pg *p, rq_res *res)
{
    int rc;

    if (total(p) == 0) {
        /* A page that ended without rows (a stop, or nothing found
           yet): the first row found becomes the selection. */
        avail(p, res, 0, &rc);
        return rc;
    }
    if (!avail(p, res, (u16)(p->sel + 1), &rc))
        return rc;
    p->sel++;
    if (p->sel >= p->first + p->ps)
        p->first++;
    return normalize(p, res);
}

/* The selection one row up, fetching the page above A when the window
   is at A's top. */
static int do_up(pg *p, rq_res *res)
{
    int moved;
    int rc;

    if (p->sel > p->first) {
        p->sel--;
        return RQ_OK;
    }
    if (p->first == 0) {
        rc = prev_page(p, res, &moved);
        if (rc != RQ_OK || !moved)
            return rc;
    }
    p->first--;
    p->sel--;
    return RQ_OK;
}

/* The window and selection a page down, the window stopping on the
   last row. */
static int do_pgdn(pg *p, rq_res *res)
{
    u16 k;
    int rc;

    /* The rows the window moves onto are fetched before it moves, so a
       failure leaves it where it was. */
    rc = more_b(p, res);
    if (rc != RQ_OK)
        return rc;
    for (k = 0; k < p->ps; k++) {
        if (!avail(p, res, (u16)(p->sel + 1), &rc))
            break;
        p->sel++;
        p->first++;
        if (pg_at_end(p))
            end_window(p);
        rc = normalize(p, res);
        if (rc != RQ_OK)
            break;
    }
    return rc;
}

/* The window and selection a page up, the selection stopping on the
   first row. */
static int do_pgup(pg *p, rq_res *res)
{
    u16 k;
    int moved;
    int rc;

    for (k = 0; k < p->ps; k++) {
        if (p->first == 0) {
            rc = prev_page(p, res, &moved);
            if (rc != RQ_OK)
                return rc;
            if (!moved) {
                p->sel = p->first;
                break;
            }
        }
        p->first--;
        p->sel--;
    }
    return normalize(p, res);
}

/* The window on the last rows held, the last one selected. */
static void last_window(pg *p)
{
    u16 t;

    t = total(p);
    p->sel = t > 0 ? (u16)(t - 1) : 0;
    p->first = t > p->ps ? (u16)(t - p->ps) : 0;
}

/* The window on the last rows, the last one selected: the End walk
   counts its way to the last page, which is then fetched whole. */
static int do_end(pg *p, rq_res *res)
{
    char cur[LV_CURSZ];
    char next[LV_CURSZ];
    char prev[LV_CURSZ];
#ifdef PAGER_ABOVE
    u32 at;
    u16 back;
    int moved;
#endif
    u16 writes;
    u16 wbytes;
    u16 keepb;
    u16 n;
    u8 keepn;
    u8 more;
    int rc;

    writes = 0;
    wbytes = 0;
    n = 0;
    more = 0;
    if (pg_at_end(p)) {
        /* The rows held end the list: only a short last page alone in
           the pager needs the page before it. */
        if (p->bload || p->na >= p->ps || p->ringn == 0) {
            last_window(p);
            return RQ_OK;
        }
        strcpy(cur, p->cura);
        n = p->na;
    } else {
        strcpy(cur, p->cura);
        strcpy(next, p->curb);
        more = p->amore;
    }
    p->walk = 1;
    p->walked = 0;
    rc = RQ_OK;
    while (more) {
        if (p->wait(p->arg, 1, p->walked)) {
            p->stopped = 1;
            break;
        }
        pg_ring_push(p, cur);
        writes++;
        wbytes = (u16)(wbytes + strlen(cur) + 1);
        strcpy(prev, cur);
        strcpy(cur, next);
        rc = pg_fetch(p, res, cur, 0, &n, next, &more);
        if (rc != RQ_OK)
            break;
        p->walked += n;
        if (p->stopped)
            break;
    }
    p->walk = 0;
    /* What a failure leaves on the ring: the cursors it held before the
       walk, but the oldest, which the walk's pushes dropped, and none
       once they dropped one of the walk's own (wbytes, which wraps only
       after more pushes than the ring holds, is then not used).  A pop
       leaves the bytes of the block as they were, and the walk pushes
       last. */
    keepn = 0;
    keepb = 0;
    if (p->ringn > writes) {
        keepn = (u8)(p->ringn - writes);
        keepb = (u16)(p->ringb - wbytes);
    }
#ifdef PAGER_ABOVE
    /* Once the walk moved (it ran, or was stopped before its first
       request), above for page cur, the walk's last, is the rows before
       A and those from A's start to cur's; back is the rows of the walk's
       page before cur, A's or a full page, as the walk went on after it,
       which is shown instead of cur or before it.  Else the rows held
       ended the list, and load_a counts the page before A. */
    moved = writes != 0 || more;
    back = writes > 1 ? p->ps : p->na;
    at = p->above;
    if (at != PG_NOABOVE && writes != 0)
        at += (u32)p->na + p->walked - n;
    if (at != PG_NOABOVE && !more && n == 0)
        at -= back;
#endif
    if (rc == RQ_OK) {
        /* The cursor before the last entries may lead to a page of none
           (the rest deleted or filtered out): the page before is the
           last, and a full one, its cursor the ring's top, which the
           walk pushed last, or the walk's own copy when the ring had no
           memory.  A stop leaves more set, so never comes here. */
        if (!more && n == 0 &&
            (ring_pop(p, cur) ||
             (writes != 0 && strcpy(cur, prev) != NULL)))
            n = p->ps;
        /* A short last page is shown under the end of the page before
           it, so the window is full: that page into A, the last one
           into B. */
        if (!p->stopped && n < p->ps && ring_pop(p, next)) {
#ifdef PAGER_ABOVE
            if (at != PG_NOABOVE)
                at -= back;
#endif
            rc = load_a(p, res, next);
            if (rc == RQ_OK) {
#ifdef PAGER_ABOVE
                if (moved)
                    p->above = at;
#endif
                rc = more_b(p, res);
                last_window(p);
                return rc;
            }
        } else {
            rc = load_a(p, res, cur);
            if (rc == RQ_OK) {
#ifdef PAGER_ABOVE
                if (moved)
                    p->above = at;
#endif
                last_window(p);
                return RQ_OK;
            }
        }
    }
    p->ringn = keepn;
    p->ringb = keepb;
    return rc;
}

/* Fetch A, and B when it was loaded, again, as pg_reload describes. */
static int do_reload(pg *p, rq_res *res)
{
    char pc[LV_CURSZ];
    u16 sel;
    u16 first;
    int hadb;
    int moved;
    u16 t;
    int rc;

    sel = p->sel;
    first = p->first;
    hadb = p->bload;
    rc = load_a(p, res, p->cura);
    if (rc != RQ_OK)
        return rc;
    if (p->na == 0 && p->cura[0] != '\0') {
        /* The rows of A have gone: show the page before. */
        if (ring_pop(p, pc)) {
            rc = load_a(p, res, pc);
            if (rc != RQ_OK)
                pg_ring_push(p, pc);
        } else {
            rc = load_a(p, res, "");
        }
        if (rc != RQ_OK)
            return rc;
        sel = p->na;
        first = 0;
    }
    rc = hadb ? more_b(p, res) : RQ_OK;
    t = total(p);
    if (sel >= t)
        sel = t > 0 ? (u16)(t - 1) : 0;
    if (first > sel)
        first = sel;
    if (sel >= first + p->ps)
        first = (u16)(sel - p->ps + 1);
    p->sel = sel;
    p->first = first;
    if (rc != RQ_OK)
        return rc;
    rc = normalize(p, res);
    if (rc != RQ_OK)
        return rc;
    /* The rows held end the list.  When they are a page alone, shorter
       than the window, with a page before it, that page goes into A and
       these rows into B, as End shows them, the selection staying on the
       same row: prev_page keeps the rows held, and only a page before
       that no longer ends where they start needs B fetched after it. */
    if (pg_at_end(p) && !p->bload && p->na < p->ps && p->ringn > 0) {
        rc = prev_page(p, res, &moved);
        if (rc == RQ_OK)
            rc = more_b(p, res);
    }
    /* Then the window ends on the last row, as End leaves it. */
    if (pg_at_end(p))
        end_window(p);
    return rc;
}

/* The next sort order, then the first page. */
static int do_order(pg *p, rq_res *res)
{
    if (p->src->norders > 0)
        p->order = (u8)((p->order + 1) % p->src->norders);
    return do_home(p, res);
}

/* ---------------------------------------------------------------------
 * The public moves: run one, report a failure.
 * --------------------------------------------------------------------- */
typedef int (*pg_step)(pg *p, rq_res *res);

/* End a move whose steps returned rc with res: give a failure to the
   fail function, starting again at the first page after a bad cursor
   when it says to stay; a failed move reports no stop.  Returns the
   move's code. */
static int done(pg *p, int rc, rq_res *res)
{
    if (rc != RQ_OK) {
        if (p->fail(p->arg, res) && rc == RQ_ECURSOR) {
            rc = do_home(p, res);
            if (rc != RQ_OK)
                p->fail(p->arg, res);
        }
        p->stopped = 0;
    }
    return rc;
}

/* The steps of the moves, indexed by the PG_ moves, one for each. */
static const pg_step pg_steps[] = {
    do_home, do_end, do_up, do_down, do_pgup, do_pgdn, do_reload, do_order
};
extern char pg_steps_all[1];
extern char pg_steps_all[(sizeof pg_steps / sizeof pg_steps[0] ==
                          PG_ORDER + 1) ? 1 : 2];

int pg_move(pg *p, int m)
{
    rq_res res;

    p->stopped = 0;
    return done(p, pg_steps[m](p, &res), &res);
}

int pg_seek(pg *p, lv_match_fn match, void *arg, u16 max)
{
    rq_res res;
    char text[LV_ROWTEXT + 1];
    char start[LV_KEYSZ];
    char prev[LV_KEYSZ];
    char key[LV_KEYSZ];
    u16 n;
    int rc;

    p->stopped = 0;
    rc = RQ_OK;
    if (pg_selected(p, start)) {
        strcpy(key, start);
        for (n = 0; n < max; n++) {
            strcpy(prev, key);
            rc = do_down(p, &res);
            if (rc == RQ_OK && !p->stopped) {
                pg_selected(p, key);
                if (strcmp(key, prev) == 0) {
                    rc = do_home(p, &res);
                    pg_selected(p, key);
                }
            }
            if (rc != RQ_OK || p->stopped)
                break;
            pg_text(p, p->sel, text);
            if (match(arg, key, text) || strcmp(key, start) == 0)
                break;
        }
    }
    return done(p, rc, &res);
}

/* ---------------------------------------------------------------------
 * Opening and reading.
 * --------------------------------------------------------------------- */
int pg_open(pg *p, link *l, const lv_src *src, u8 ps, pg_wait_fn wait,
            pg_fail_fn fail, void *arg)
{
    u8 i;

    if (ps == 0 || ps > LV_MAXPAGE || src->norders > LV_MAXORDERS)
        return PG_EARG;
    memset(p, 0, sizeof *p);
    p->l = l;
    p->src = src;
    p->wait = wait;
    p->fail = fail;
    p->arg = arg;
    p->ps = ps;
    for (i = 0; i < LV_MAXFIELDS; i++)
        p->fnp[i] = p->fn[i];
    p->rows = cmem_alloc((u16)(2 * ps * LV_ROWBYTES));
    return p->rows != NULL ? PG_OK : PG_ENOMEM;
}

void pg_lead(pg *p, const char *key, const char *text)
{
    p->leadkey = key;
    p->leadtext = text;
}

#ifdef PAGER_ABOVE
void pg_unpaged(pg *p)
{
    p->unpaged = 1;
}
#endif

void pg_close(pg *p)
{
    cmem_free(p->rows);
    cmem_free(p->ring);
}

u16 pg_rows(pg *p)
{
    return total(p);
}

int pg_at_end(pg *p)
{
    return p->bload ? !p->bmore : !p->amore;
}

int pg_text(pg *p, u16 i, char *text)
{
    int mark;

    cmem_get(p->rows, (u16)(row_off(slot_of(p, i)) + LV_KEYSZ), text,
             LV_ROWTEXT + 1);
    mark = text[LV_ROWTEXT];
    text[LV_ROWTEXT] = '\0';
    return mark;
}

int pg_selected(pg *p, char *key)
{
    if (p->sel >= total(p)) {
        key[0] = '\0';
        return 0;
    }
    cmem_get(p->rows, row_off(slot_of(p, p->sel)), key, LV_KEYSZ);
    key[LV_KEYSZ - 1] = '\0';
    return 1;
}

const char *pg_order_name(pg *p)
{
    return p->src->norders > 0 ? p->src->orders[p->order] : "";
}

int pg_set_order(pg *p, const char *name)
{
    u8 i;

    for (i = 0; i < p->src->norders; i++)
        if (strcmp(p->src->orders[i], name) == 0) {
            p->order = i;
            return 1;
        }
    return 0;
}
