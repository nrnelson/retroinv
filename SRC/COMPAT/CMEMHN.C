/*
 * CMEMHN.C - the near heap of every program but INV, the network-only
 * client and those on the TCP stack: CMEM_NEARHEAP bytes.
 *
 * A program that links CMEM.C links exactly one of the near-heap modules
 * CMEM.C names, and a program that links two fails on the duplicate
 * symbols.  CMEM.C says what the size means under each compiler.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"

#ifdef COMPAT_TC
unsigned _heaplen = CMEM_NEARHEAP;
#endif
const u16 cmem_heapsz = CMEM_NEARHEAP;
