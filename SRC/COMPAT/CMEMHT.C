/*
 * CMEMHT.C - the near heap of the programs on the TCP stack that link
 * CMEM.C: CMEM_TCPHEAP bytes.
 *
 * A program that links CMEM.C links exactly one of the near-heap modules
 * CMEM.C names, and a program that links two fails on the duplicate
 * symbols.  CMEM.C says what the size means under each compiler.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"

#ifdef COMPAT_TC
unsigned _heaplen = CMEM_TCPHEAP;
#endif
const u16 cmem_heapsz = CMEM_TCPHEAP;
