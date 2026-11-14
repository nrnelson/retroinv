/*
 * THROTL.C - the server's failed-login throttle (THROTL.H).
 *
 * The far block holds THR_MAX entries side by side, each copied near to
 * be read or changed: the users' entries, then the overflow entry,
 * TH_SPILL.  A free entry is all zeros, a user's then named "".  An
 * entry counts failures while fails is below THR_FAILS and refuses once
 * it has reached it, its tick then the start of the refusal; it is over
 * THR_SPAN ticks after the first failure it counts, or THR_HOLD ticks
 * after the start of its refusal.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "THROTL.H"

#define TH_SPILL        (THR_MAX - 1)   /* the overflow entry          */

/* The u32 member comes first, so that no compiler pads between the
   members. */
typedef struct {
    u32  since;                 /* tick of the count's first failure,
                                   or of the refusal's start            */
    char user[ENG_USERLEN + 1]; /* the stored name, "" for the overflow
                                   entry and a free one                 */
    u8   fails;                 /* failures counted                     */
} entry;

static cmem_blk *thr_blk;       /* the entries; NULL when closed        */

static void get(u8 i, entry *t)
{
    cmem_get(thr_blk, (u16)(i * sizeof *t), t, (u16)sizeof *t);
}

static void put(u8 i, const entry *t)
{
    cmem_put(thr_blk, (u16)(i * sizeof *t), t, (u16)sizeof *t);
}

/* 1 when entry t still counts or refuses at now. */
static int live(const entry *t, u32 now)
{
    return t->fails != 0 &&
           now - t->since < (t->fails >= THR_FAILS ? THR_HOLD : THR_SPAN);
}

/* The index of user's entry, else TH_SPILL, the entry read into *t. */
static u8 find(const char *user, entry *t)
{
    u8 i;

    for (i = 0; i < TH_SPILL; i++) {
        get(i, t);
        if (t->user[0] != '\0' && txt_ieq(t->user, user))
            return i;
    }
    get(TH_SPILL, t);
    return TH_SPILL;
}

int thr_refused(u32 now, const char *user)
{
    entry t;

    if (thr_blk == NULL)
        return 0;
    find(user, &t);
    return t.fails >= THR_FAILS && live(&t, now);
}

/* A failure of a user not in the table takes the first user's entry
   that is free or over, carrying the overflow entry's count with it, so
   that leaving the overflow wins no guess; with none, it counts in the
   overflow entry.  A count that is over starts again. */
void thr_note(u32 now, const char *user, int ok)
{
    entry t;
    u8 i;
    u8 k;

    if (thr_blk == NULL)
        return;
    i = find(user, &t);
    if (ok) {
        if (i < TH_SPILL)
            cmem_set(thr_blk, (u16)(i * sizeof t), 0, (u16)sizeof t);
        return;
    }
    if (i == TH_SPILL) {
        for (k = 0; k < TH_SPILL; k++) {
            get(k, &t);
            if (t.user[0] == '\0' || !live(&t, now))
                break;
        }
        get(TH_SPILL, &t);
        if (k < TH_SPILL) {
            i = k;
            txt_copy(t.user, user, (u16)sizeof t.user);
        }
    }
    if (!live(&t, now)) {
        t.since = now;
        t.fails = 1;
    } else if (t.fails < THR_FAILS && ++t.fails == THR_FAILS) {
        t.since = now;
    }
    put(i, &t);
}

int thr_open(void)
{
    if (thr_blk != NULL)
        return 1;
    thr_blk = cmem_alloc((u16)(THR_MAX * sizeof(entry)));
    if (thr_blk == NULL)
        return 1;
    cmem_set(thr_blk, 0, 0, (u16)(THR_MAX * sizeof(entry)));
    return 0;
}

void thr_close(void)
{
    cmem_free(thr_blk);
    thr_blk = NULL;
}
