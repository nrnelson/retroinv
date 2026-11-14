/*
 * CMEMHR.C - the near heap of the network-only client (THIN of
 * SRCS.MK): CMEM_REMHEAP bytes under Turbo C, CMEM_NEARHEAP under Open
 * Watcom and Watcom C 6.5, which grows its near heap to all of DGROUP
 * whatever the module gives (CMEM.C), so that there the size counts
 * only in the DGROUP check of TEST/mapchk.sh.
 *
 * A program that links CMEM.C links exactly one of the near-heap modules
 * CMEM.C names, and a program that links two fails on the duplicate
 * symbols.  CMEM.C says what the size means under each compiler.
 *
 * Without the engine the client's near heap holds only the client's own
 * allocations (DOC/CLIENT.md, Memory).  CMEM_REMHEAP is the Turbo C
 * reservation its deepest screens were measured to fit, with the stack
 * check of TEST/INTEG/scrgate.sh: 8 KB, where 6 KB was outgrown; plus
 * four kilobytes for growth, as COMPAT.H's CMEM_NEARHEAP keeps.  It is
 * measured again whenever the client's near allocations grow;
 * TEST/mapchk.sh reads it here.  Under Open Watcom the client keeps
 * CMEM_NEARHEAP, as a smaller heap was measured to raise its memory
 * floor rather than lower it.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"

#define CMEM_REMHEAP    12288u  /* the remote-only client's heap   */

#ifdef COMPAT_TC
unsigned _heaplen = CMEM_REMHEAP;
#endif
const u16 cmem_heapsz = CMEM_NEARHEAP;
