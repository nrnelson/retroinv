/*
 * CODETAB.C - a code list held whole (CODETAB.H).
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "CODETAB.H"

/* The rows of the longest list fit one far block, a code list's row is
   the start of a ct_row, and a tag's no longer. */
extern char ct_rows_fit[1];
extern char ct_rows_fit[(LINK_MAXCODES * (long)CT_ROWSZ <=
                         (long)CMEM_MAXBLK &&
                         sizeof(ct_row) - LINK_ITAGLEN + LINK_LTYPELEN ==
                         CT_ROWSZ && CT_TAGSZ <= CT_ROWSZ) ? 1 : 2];

static const char k_itemtags[] = "ITEM TAGS";

int ct_open(ctab *t)
{
    t->n = 0;
    t->last = 0;
    t->part = 0;
    t->rowsz = CT_ROWSZ;
    t->b = cmem_alloc((u16)(LINK_MAXCODES * CT_ROWSZ));
    return t->b != NULL;
}

void ct_close(ctab *t)
{
    cmem_free(t->b);
    t->b = NULL;
    t->n = 0;
}

/* rq_fn of ct_load: each R line of three values as the next row. */
static int ct_line(void *arg, const rq_line *ln)
{
    ctab *t;
    ct_row r;

    t = (ctab *)arg;
    if (ln->kind != 'R' || ln->n < 3 || t->n >= LINK_MAXCODES)
        return 0;
    txt_copy(r.code, ln->v[0], LINK_LTYPELEN + 1);
    txt_copy(r.name, ln->v[1], (u16)sizeof r.name);
    txt_copy(r.order, ln->v[2], (u16)sizeof r.order);
    cmem_put(t->b, (u16)(t->n * CT_ROWSZ), &r, CT_ROWSZ);
    t->n++;
    return 0;
}

int ct_load(ctab *t, link *l, const char *table, rq_res *res)
{
    rq q;
    int rc;

    t->n = 0;
    t->last = 0;
    t->rowsz = CT_ROWSZ;
    rq_begin(&q, table);
    rq_word(&q, "LIST");
    rc = rq_send(l, &q, ct_line, t, res);
    if (rc != RQ_OK)
        t->n = 0;
    return rc;
}

/* a against b in ITEM TAGS' order, A-Z folded: below 0, 0 or above 0,
   as strcmp. */
static int tag_cmp(const char *a, const char *b)
{
    int x;
    int y;
    char c;

    do {
        c = *a++;
        x = TXT_UPPER(c);
        c = *b++;
        y = TXT_UPPER(c);
    } while (x == y && x != '\0');
    return x - y;
}

/* rq_fn of ct_load_tags: the tag of an R line into its place among the
   rows, by a binary search over their codes, unless one of them is the
   tag already; when the rows are full the last goes, or the tag itself
   when it would be last, and t->part is set. */
static int ct_tag(void *arg, const rq_line *ln)
{
    ctab *t;
    char code[CT_TAGSZ];
    u16 lo;
    u16 hi;
    u16 mid;
    int c;

    t = (ctab *)arg;
    if (ln->kind != 'R' || ln->n < 1)
        return 0;
    lo = 0;
    hi = t->n;
    while (lo < hi) {
        mid = (u16)((lo + hi) / 2);
        cmem_get(t->b, (u16)(mid * CT_TAGSZ), code, CT_TAGSZ);
        c = tag_cmp(ln->v[0], code);
        if (c == 0)
            return 0;
        if (c < 0)
            hi = mid;
        else
            lo = (u16)(mid + 1);
    }
    if (t->n == LINK_MAXCODES) {
        t->part = 1;
        if (lo == LINK_MAXCODES)
            return 0;
        t->n--;
    }
    cmem_move(t->b, (u16)((lo + 1) * CT_TAGSZ), (u16)(lo * CT_TAGSZ),
              (u16)((t->n - lo) * CT_TAGSZ));
    memset(code, 0, sizeof code);
    txt_copy(code, ln->v[0], (u16)sizeof code);
    cmem_put(t->b, (u16)(lo * CT_TAGSZ), code, CT_TAGSZ);
    t->n++;
    return 0;
}

int ct_load_tags(ctab *t, link *l, ct_wait_fn wait, void *arg,
                 rq_res *res)
{
    rq q;
    int rc;

    t->n = 0;
    t->last = 0;
    t->part = 0;
    t->rowsz = CT_TAGSZ;
    rq_begin(&q, k_itemtags);
    for (;;) {
        rc = rq_send(l, &q, ct_tag, t, res);
        if (rc != RQ_OK) {
            t->n = 0;
            t->part = 0;
            return rc;
        }
        if (res->more[0] == '\0')
            return RQ_OK;
        if (wait(arg)) {
            t->part = 1;
            return RQ_OK;
        }
        rq_begin(&q, k_itemtags);
        rq_pair(&q, "AFTER", res->more);
    }
}

int ct_tags_add(char *t, u16 width, const char *tag)
{
    u16 len;
    u16 i;
    u16 j;

    len = (u16)strlen(tag);
    for (i = 0; t[i] != '\0'; i = j) {
        while (t[i] == ' ' || t[i] == ',')
            i++;
        for (j = i; t[j] != '\0' && t[j] != ' ' && t[j] != ','; j++)
            ;
        if (j - i == len && txt_ieqn(t + i, tag, len))
            return 1;
    }
    while (i > 0 && (t[i - 1] == ' ' || t[i - 1] == ','))
        i--;
    if (i + (i > 0) + len > width)
        return 0;
    if (i > 0)
        t[i++] = ',';
    strcpy(t + i, tag);
    return 1;
}

void ct_tags_bare(char *t)
{
    char *end;

    for (end = t; *t != '\0'; t++) {
        if (*t == ' ')
            *t = ',';
        else if (*t != ',')
            end = t + 1;
    }
    *end = '\0';
}

void ct_get(ctab *t, u16 i, ct_row *r)
{
    if (t->rowsz == CT_TAGSZ) {
        r->name[0] = '\0';
        r->order[0] = '\0';
        cmem_get(t->b, (u16)(i * CT_TAGSZ), r->code, CT_TAGSZ);
    } else {
        cmem_get(t->b, (u16)(i * CT_ROWSZ), r, CT_ROWSZ);
    }
}

void ct_name(ctab *t, const char *code, char *name)
{
    ct_row r;
    u16 i;

    for (i = 0; i < t->n; i++) {
        ct_get(t, (u16)((t->last + i) % t->n), &r);
        if (strcmp(r.code, code) == 0) {
            t->last = (u16)((t->last + i) % t->n);
            txt_copy(name, r.name, LINK_CODENAMELEN + 1);
            return;
        }
    }
    txt_copy(name, code, LINK_CODENAMELEN + 1);
}
