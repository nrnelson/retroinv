/*
 * CMEMHI.C - the near heap of INV: CMEM_INVHEAP bytes under Turbo C,
 * CMEM_NEARHEAP under Open Watcom and Watcom C 6.5.
 *
 * A program that links CMEM.C links exactly one of the near-heap modules
 * CMEM.C names, and a program that links two fails on the duplicate
 * symbols.  CMEM.C says what the size means under each compiler.
 *
 * Under Turbo C INV's deepest screens set its memory floor with the far
 * heap they need, and INV takes less near heap than INVSERV and the
 * tests, for which COMPAT.H sizes CMEM_NEARHEAP.  CMEM_INVHEAP is the most
 * INV was measured to take (its near heap filled with a pattern over
 * every key script of TEST/T_SCR.SH and over a reindex and a pack of the
 * deep data), under 22 KB, rounded up to whole kilobytes, plus four
 * kilobytes for growth, as CMEM_NEARHEAP keeps.  No tool measures that
 * peak again: the guard is the stack check of TEST/T_SCR.SH, as a heap
 * that outgrows the reservation takes the stack it fills.  It is measured
 * again by hand whenever the client's or the engine's near allocations
 * grow; TEST/mapchk.sh reads it here.  Open Watcom, where a near
 * allocation past the heap fails, keeps CMEM_NEARHEAP.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"

#define CMEM_INVHEAP    26624u  /* INV's heap under Turbo C             */

#ifdef COMPAT_TC
unsigned _heaplen = CMEM_INVHEAP;
#endif
const u16 cmem_heapsz = CMEM_NEARHEAP;
