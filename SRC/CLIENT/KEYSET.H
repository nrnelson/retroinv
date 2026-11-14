/*
 * KEYSET.H - a set of row keys in far memory: every key of a list, for a
 * selection of all its rows, not only those of the pages held.
 *
 * Keys are kept in the order added, each in KS_KEYW bytes, unterminated
 * and NUL-padded, in one far block (CMEM.C) of KS_FIRST bytes that
 * doubles as keys come, up to one of CMEM_MAXBLK holding KS_MAXROWS
 * keys.  A survey of the list collects them (psv_survey of PAGERSV.H).
 */
#ifndef KEYSET_H
#define KEYSET_H

#include "COMPAT.H"
#include "PAGER.H"

#define KS_KEYW         (LV_KEYSZ - 1)          /* bytes kept of a key  */
#define KS_MAXROWS      (CMEM_MAXBLK / KS_KEYW) /* keys a set holds     */
#define KS_FIRST        1024u   /* bytes of the first block             */

/* A set, held by its caller, who reads n and over. */
typedef struct {
    cmem_blk *b;                /* the keys, NULL while there are none  */
    u16  n;                     /* keys held                            */
    u8   over;                  /* 1 once a key was refused: the set
                                   misses keys of its list              */
} ks_set;

/* Make s an empty set, over 0, holding no block. */
void ks_init(ks_set *s);

/* Free the block of s and leave it as ks_init does. */
void ks_free(ks_set *s);

/* Add key (at most KS_KEYW characters are kept) after the keys held: 1,
   or 0 with over 1 when the set holds KS_MAXROWS keys, has no memory to
   grow, or is over already, so a set that refused a key takes no more. */
int ks_add(ks_set *s, const char *key);

/* Key i into key (LV_KEYSZ bytes) and 1, or 0 with key "" when i is not
   below n. */
int ks_get(ks_set *s, u16 i, char *key);

/* The index of key, compared in its first KS_KEYW characters, among the
   keys held, or n when it is none of them.  The search starts at key
   from (the first when from is not below n) and goes round to the one
   before it, so a caller that seeks the keys of a list's rows in the
   list's order finds each at once, from one past the last found. */
u16 ks_find(ks_set *s, const char *key, u16 from);

#endif
