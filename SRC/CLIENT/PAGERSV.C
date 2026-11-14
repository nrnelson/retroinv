/*
 * PAGERSV.C - the survey of a pager's list and the jump into it
 * (PAGERSV.H).
 *
 * Page k's start cursor sits at k * LV_CURSZ of the block, with its NUL.
 */
#define PAGER_CALLS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "RQ.H"
#include "PAGER.H"
#include "PAGERINT.H"
#include "KEYSET.H"
#include "PAGERSV.H"

/* Page k's start, kept, into cur (LV_CURSZ bytes). */
static void psv_get(psv *s, u16 k, char *cur)
{
    cmem_get(s->cur, (u16)(k * LV_CURSZ), cur, LV_CURSZ);
}

/* No count and no page starts, the block kept. */
static void psv_none(psv *s)
{
    s->rows = 0;
    s->pages = 0;
    s->all = 0;
}

/* The pager's key sink for a survey: key into the set karg. */
static void psv_key(void *karg, const char *key)
{
    ks_add((ks_set *)karg, key);
}

void psv_init(psv *s)
{
    s->cur = NULL;
    psv_none(s);
}

void psv_free(psv *s)
{
    cmem_free(s->cur);
    psv_init(s);
}

int psv_survey(psv *s, pg *p, ks_set *keys)
{
    rq_res res;
    char cur[LV_CURSZ];
    u16 n;
    u8 more;
    int rc;

    psv_none(s);
    if (keys != NULL)
        ks_free(keys);
    p->stopped = 0;
    if (s->cur == NULL)
        s->cur = cmem_alloc(PSV_MAXPAGES * LV_CURSZ);
    if (s->cur == NULL)
        return RQ_OK;
    cur[0] = '\0';
    p->walk = 1;
    if (keys != NULL) {
        p->keyfn = psv_key;
        p->karg = keys;
    }
    for (;;) {
        cmem_put(s->cur, (u16)(s->pages * LV_CURSZ), cur,
                 (u16)(strlen(cur) + 1));
        s->pages++;
        p->walked = s->rows;
        rc = pg_fetch(p, &res, cur, 0, &n, cur, &more);
        s->rows += n;
        if (rc != RQ_OK || p->stopped)
            break;
        if (!more) {
            if (n == 0 && s->pages > 1)
                s->pages--;
            s->all = 1;
            break;
        }
        if (s->pages == PSV_MAXPAGES)
            break;
        if (p->wait(p->arg, 1, s->rows)) {
            p->stopped = 1;
            break;
        }
    }
    p->walk = 0;
    p->keyfn = NULL;
    if (rc != RQ_OK)
        p->fail(p->arg, &res);
    if (rc != RQ_OK || p->stopped) {
        psv_none(s);
        if (keys != NULL)
            ks_free(keys);
    } else if (keys != NULL && !s->all) {
        keys->over = 1;
    }
    return rc;
}

int psv_jump(psv *s, pg *p, u32 r)
{
    rq_res res;
    char cur[LV_CURSZ];
    u32 page;
    u16 k;
    u16 i;
    int rc;

    page = r / p->ps;
    k = page < s->pages ? (u16)page : (u16)(s->pages - 1);
    psv_get(s, k, cur);
    p->stopped = 0;
    rc = pg_load(p, &res, cur, (u32)k * p->ps);
    if (rc == RQ_OK) {
        for (i = k > LV_MAXBACK ? (u16)(k - LV_MAXBACK) : 0; i < k; i++) {
            psv_get(s, i, cur);
            pg_ring_push(p, cur);
        }
        return RQ_OK;
    }
    if (rc == RQ_ECURSOR)
        psv_none(s);
    if (p->fail(p->arg, &res) && rc == RQ_ECURSOR)
        rc = pg_move(p, PG_HOME);
    return rc;
}
