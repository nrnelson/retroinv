/*
 * KEYSET.C - a set of row keys in far memory (KEYSET.H).
 *
 * Key i sits at i * KS_KEYW of the block.  CMEM has no reallocation, so
 * a full block is replaced by one of twice its size, the keys copied.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "KEYSET.H"

/* Doubling the first block reaches CMEM_MAXBLK, and every block holds
   whole keys, KS_MAXROWS of them the largest. */
extern char ks_blocks_fit[1];
extern char ks_blocks_fit[(KS_FIRST % KS_KEYW == 0 &&
                           CMEM_MAXBLK % KS_FIRST == 0 &&
                           ((CMEM_MAXBLK / KS_FIRST) &
                            (CMEM_MAXBLK / KS_FIRST - 1)) == 0 &&
                           (u32)KS_MAXROWS * KS_KEYW == CMEM_MAXBLK)
                          ? 1 : 2];

void ks_init(ks_set *s)
{
    s->b = NULL;
    s->n = 0;
    s->over = 0;
}

void ks_free(ks_set *s)
{
    cmem_free(s->b);
    ks_init(s);
}

int ks_add(ks_set *s, const char *key)
{
    char k[KS_KEYW];
    cmem_blk *b;
    u16 size;

    if (s->over)
        return 0;
    size = s->b != NULL ? cmem_size(s->b) : 0;
    if ((u16)(s->n * KS_KEYW) == size) {
        b = size == CMEM_MAXBLK ? NULL :
            cmem_alloc(size == 0 ? KS_FIRST : (u16)(2 * size));
        if (b == NULL) {
            s->over = 1;
            return 0;
        }
        cmem_copy(b, 0, s->b, 0, size);
        cmem_free(s->b);
        s->b = b;
    }
    strncpy(k, key, KS_KEYW);
    cmem_put(s->b, (u16)(s->n * KS_KEYW), k, KS_KEYW);
    s->n++;
    return 1;
}

int ks_get(ks_set *s, u16 i, char *key)
{
    key[0] = '\0';
    if (i >= s->n)
        return 0;
    cmem_get(s->b, (u16)(i * KS_KEYW), key, KS_KEYW);
    key[KS_KEYW] = '\0';
    return 1;
}

u16 ks_find(ks_set *s, const char *key, u16 from)
{
    char k[KS_KEYW];
    char at[KS_KEYW];
    u16 i;
    u16 left;

    strncpy(k, key, KS_KEYW);
    i = from < s->n ? from : 0;
    for (left = s->n; left > 0; left--) {
        cmem_get(s->b, (u16)(i * KS_KEYW), at, KS_KEYW);
        if (memcmp(at, k, KS_KEYW) == 0)
            return i;
        if (++i == s->n)
            i = 0;
    }
    return s->n;
}
