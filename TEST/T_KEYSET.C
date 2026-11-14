/*
 * T_KEYSET.C - the set of row keys (SRC/CLIENT/KEYSET.C).
 *
 * usage: T_KEYSET
 *
 * Written from the contract in KEYSET.H.  Covered:
 * - ks_init: no block, no key, over 0;
 * - growth: the first key takes a block of KS_FIRST, each block full
 *   doubles it, the old one freed, up to one of CMEM_MAXBLK holding
 *   KS_MAXROWS keys;
 * - every key read back in order after each growth, keys of one to
 *   KS_KEYW characters and one longer, cut, each into a buffer filled
 *   with 'X', so ks_get must end it;
 * - KS_MAXROWS: the next key refused with over 1 and the keys kept; a
 *   set over refuses every later key;
 * - no memory for the first block or for a larger one: over 1, the keys
 *   held kept, and no later key taken once memory is back;
 * - ks_get past n: 0 and "";
 * - ks_find: each key's index from every start, the search going round
 *   and a start past n being the first; the first of two equal keys from
 *   the start; a longer key found by its first KS_KEYW characters and a
 *   shorter one not by a prefix; n for a key not held and in an empty
 *   set;
 * - ks_free: the block freed, the set as ks_init leaves it.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
/* cmem_freeing is declared under COMPAT_FIND, and only the host builds
   this test. */
#define COMPAT_FIND
#include <stdio.h>
#include <string.h>
#include "COMPAT.H"
#include "PAGER.H"
#include "KEYSET.H"
#include "TESTFW.H"

static u16 g_nalloc;            /* cmem_alloc calls                     */
static u16 g_nfree;             /* cmem_free calls with a block         */
static int g_nomem;             /* fail every allocation                */

static int fault_fn(u16 size)
{
    TF_CHECK(size > 0);
    g_nalloc++;
    return g_nomem;
}

static void freeing_fn(cmem_blk *b)
{
    TF_CHECK(b != NULL);
    g_nfree++;
}

/* Key i, of 1 to KS_KEYW characters as i grows. */
static void key_of(u16 i, char *key)
{
    sprintf(key, "%u", (unsigned)i);
}

/* Key i of s into key, filled with 'X' first so that only ks_get can
   end it. */
static int get_x(ks_set *s, u16 i, char *key)
{
    memset(key, 'X', LV_KEYSZ);
    return ks_get(s, i, key);
}

/* 1 when the set holds keys 0 to n - 1 in order. */
static int holds(ks_set *s, u16 n)
{
    char key[LV_KEYSZ];
    char want[LV_KEYSZ];
    u16 i;

    if (s->n != n)
        return 0;
    for (i = 0; i < n; i++) {
        key_of(i, want);
        if (!get_x(s, i, key) || strcmp(key, want) != 0)
            return 0;
    }
    return 1;
}

static void test_init(void)
{
    ks_set s;

    memset(&s, 0xA5, sizeof s);
    ks_init(&s);
    TF_CHECK(s.b == NULL);
    TF_EQ(s.n, 0);
    TF_EQ(s.over, 0);
}

/* Growth from the first block to the largest, KS_MAXROWS and past it. */
static void test_growth(void)
{
    ks_set s;
    char key[LV_KEYSZ];
    u16 size;
    u16 nblk;
    u16 i;

    ks_init(&s);
    g_nalloc = 0;
    g_nfree = 0;
    nblk = 0;
    size = 0;
    for (i = 0; i < KS_MAXROWS; i++) {
        key_of(i, key);
        if (!TF_EQ(ks_add(&s, key), 1))
            break;
        if (cmem_size(s.b) != size) {
            TF_EQ(cmem_size(s.b), size == 0 ? KS_FIRST : 2 * size);
            TF_EQ(i, size / KS_KEYW);
            size = cmem_size(s.b);
            nblk++;
            TF_CHECK(holds(&s, (u16)(i + 1)));
        }
    }
    TF_EQ(size, CMEM_MAXBLK);
    TF_EQ(g_nalloc, nblk);
    TF_EQ(g_nfree, nblk - 1);
    TF_EQ(s.over, 0);
    TF_CHECK(holds(&s, KS_MAXROWS));
    TF_EQ(ks_add(&s, "1"), 0);
    TF_EQ(s.over, 1);
    TF_EQ(s.n, KS_MAXROWS);
    TF_EQ(g_nalloc, nblk);
    TF_EQ(ks_add(&s, "2"), 0);
    TF_CHECK(holds(&s, KS_MAXROWS));
    ks_free(&s);
    TF_EQ(g_nfree, nblk);
    TF_CHECK(s.b == NULL);
    TF_EQ(s.n, 0);
    TF_EQ(s.over, 0);
}

/* Keys of every length, one longer cut, and ks_get past n. */
static void test_keys(void)
{
    static const char *const keys[] = {
        "7", "AB", "CARD", "1234567", "12345678", "ABCDEFGHI"
    };
    ks_set s;
    char key[LV_KEYSZ];
    u16 i;

    ks_init(&s);
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++)
        TF_EQ(ks_add(&s, keys[i]), 1);
    for (i = 0; i < sizeof keys / sizeof keys[0] - 1; i++) {
        TF_EQ(get_x(&s, i, key), 1);
        TF_STREQ(key, keys[i]);
    }
    TF_EQ(get_x(&s, i, key), 1);
    TF_STREQ(key, "ABCDEFGH");
    TF_EQ(get_x(&s, (u16)(i + 1), key), 0);
    TF_STREQ(key, "");
    TF_EQ(get_x(&s, 0xFFFFu, key), 0);
    TF_STREQ(key, "");
    ks_free(&s);
    TF_EQ(ks_get(&s, 0, key), 0);
    TF_STREQ(key, "");
}

/* No memory for the first block, then for the second. */
static void test_nomem(void)
{
    ks_set s;
    char key[LV_KEYSZ];
    u16 i;

    ks_init(&s);
    g_nomem = 1;
    TF_EQ(ks_add(&s, "1"), 0);
    TF_EQ(s.over, 1);
    TF_EQ(s.n, 0);
    TF_CHECK(s.b == NULL);
    g_nomem = 0;
    TF_EQ(ks_add(&s, "1"), 0);
    TF_EQ(s.n, 0);
    ks_free(&s);

    g_nfree = 0;
    for (i = 0; i < KS_FIRST / KS_KEYW; i++) {
        key_of(i, key);
        TF_EQ(ks_add(&s, key), 1);
    }
    g_nomem = 1;
    TF_EQ(ks_add(&s, "X"), 0);
    TF_EQ(s.over, 1);
    TF_EQ(g_nfree, 0);
    TF_CHECK(holds(&s, KS_FIRST / KS_KEYW));
    g_nomem = 0;
    TF_EQ(ks_add(&s, "X"), 0);
    TF_CHECK(holds(&s, KS_FIRST / KS_KEYW));
    ks_free(&s);
    TF_EQ(g_nfree, 1);
}

/* ks_find from every start, round the end, and what it does not find. */
static void test_find(void)
{
    static const char *const keys[] = {
        "7", "AB", "12345678", "AB", "1234567"
    };
    static const u8 first[][5] = {
        { 0, 1, 2, 1, 4 }, { 0, 1, 2, 1, 4 }, { 0, 3, 2, 3, 4 },
        { 0, 3, 2, 3, 4 }, { 0, 1, 2, 1, 4 }, { 0, 1, 2, 1, 4 },
        { 0, 1, 2, 1, 4 }
    };
    ks_set s;
    u16 from;
    u16 i;

    ks_init(&s);
    TF_EQ(ks_find(&s, "7", 0), 0);
    TF_EQ(ks_find(&s, "7", 3), 0);
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++)
        TF_EQ(ks_add(&s, keys[i]), 1);
    for (from = 0; from < sizeof first / sizeof first[0]; from++)
        for (i = 0; i < s.n; i++)
            TF_EQ(ks_find(&s, keys[i], from), first[from][i]);
    TF_EQ(ks_find(&s, keys[1], 0xFFFFu), 1);
    TF_EQ(ks_find(&s, "123456789", 3), 2);
    TF_EQ(ks_find(&s, "123456", 0), s.n);
    TF_EQ(ks_find(&s, "A", 1), s.n);
    TF_EQ(ks_find(&s, "", 0), s.n);
    TF_EQ(ks_find(&s, "8", 2), s.n);
    ks_free(&s);
}

int main(void)
{
    tf_begin("T_KEYSET");
    cmem_fault = fault_fn;
    cmem_freeing = freeing_fn;
    test_init();
    test_growth();
    test_keys();
    test_nomem();
    test_find();
    cmem_fault = NULL;
    cmem_freeing = NULL;
    return tf_done();
}
