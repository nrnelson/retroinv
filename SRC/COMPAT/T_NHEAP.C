/*
 * T_NHEAP.C - unit test for the near heap under DOS as cmem_init and
 * dos_heap_top of COMPAT.H leave it: whole, also from the break where
 * the steps of Watcom C 6.5's malloc would damage it, and when DOS has
 * not the memory for all of DGROUP.
 *
 * usage: T_NHEAP
 *
 * Built in the medium model the program is T_NHEAP; built in the small
 * model it is T_NHEAPG, which stands for a program without cmem_init
 * and calls dos_heap_top alone; T_NHEAP calls it after cmem_init, which
 * leaves it nothing to do.
 *
 * Under Watcom C 6.5 the test takes DOS memory blocks and moves the
 * near heap's break itself, which portable code may not, so it lives
 * in SRC/COMPAT/ and is built for DOS only.  T_NHEAP first leaves DOS
 * NH_GAP paragraphs behind the program and no other memory, which is
 * more than one of the library's steps and less than DGROUP lacks, and
 * calls cmem_init.  With that memory given back, it sets the library's
 * step again, moves the break to where such steps lead to NH_BAD, the
 * break from which the library's step to the top of DGROUP is faulty
 * (CDOS.C), and calls cmem_init again.  T_NHEAPG only moves the break
 * so and calls dos_heap_top.
 *
 * Checks:
 * - under Watcom C 6.5 with DOS short of memory, cmem_init raises the
 *   break, to NH_SAFE at most, and as many near blocks of NH_BLK bytes
 *   are then taken until malloc refuses as once more after they are
 *   freed;
 * - under Watcom C 6.5 the break is moved as said, and at NH_TOP, the
 *   top of DGROUP, after cmem_init or, in T_NHEAPG, dos_heap_top;
 * - near blocks are then taken until malloc refuses, NH_MIN of them or
 *   more, and as many again once they are freed; under the other
 *   compilers that is the whole test, after cmem_init or, in
 *   T_NHEAPG, after dos_heap_top, which is nothing there.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_HEAP
#include "COMPAT.H"
#ifdef COMPAT_WC6
#include <dos.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include "TESTFW.H"

#define NH_BLK          1024
/* As many as CMEM_NEARHEAP holds, less two for the allocator's headers
   and the C library's stream buffer. */
#define NH_MIN          ((u16)(CMEM_NEARHEAP / NH_BLK) - 2)
/* 1 in the small model, where a function's address is a near one. */
#define NH_SMALL        (sizeof(void (*)(void)) == sizeof(void *))
#ifdef COMPAT_WC6
#define NH_TOP          0xFFFEu /* the highest break                    */
#define NH_BAD          (NH_TOP - 6)
#define NH_SAFE         0xFFF0u /* the highest break that needs less
                                   memory than NH_TOP                   */
#define NH_STEP         0x2000u /* _amblksiz when main starts           */
#define NH_GAP          0x0400u
#define NH_BREAK()      ((unsigned)sbrk(0))
/* Bytes from the break to the next one that leads to NH_BAD. */
#define NH_AIM()        ((NH_BAD - NH_BREAK()) % _amblksiz)
#endif

/* Take near blocks of NH_BLK bytes until malloc refuses, then free
   them, newest first; how many there were.  Each block holds the
   address of the one taken before it. */
static u16 fill(void)
{
    void **b;
    void **last;
    u16 n;

    last = NULL;
    n = 0;
    while ((b = (void **)malloc(NH_BLK)) != NULL) {
        *b = (void *)last;
        last = b;
        n++;
    }
    while (last != NULL) {
        b = (void **)*last;
        free(last);
        last = b;
    }
    return n;
}

#ifdef COMPAT_WC6
/* A DOS memory block of n paragraphs (INT 21h function 48h): its
   segment, or 0 with *most the paragraphs of the largest there is. */
static unsigned dos_alloc(unsigned n, unsigned *most)
{
    union REGS r;

    r.h.ah = 0x48;
    r.x.bx = n;
    intdos(&r, &r);
    *most = r.x.bx;
    return r.x.cflag ? 0 : r.x.ax;
}

/* Give the DOS memory block at seg back (function 49h). */
static void dos_free(unsigned seg)
{
    union REGS r;
    struct SREGS s;

    segread(&s);
    s.es = seg;
    r.h.ah = 0x49;
    intdosx(&r, &r, &s);
}

/* cmem_init with DOS short of memory: a block of NH_GAP paragraphs
   behind the program, a block of all the rest, and the first given
   back for the heap to grow into. */
static void test_short(void)
{
    unsigned gap;
    unsigned rest;
    unsigned most;
    unsigned brk;
    u16 n;

    gap = dos_alloc(NH_GAP, &most);
    dos_alloc(0xFFFFu, &most);
    rest = dos_alloc(most, &most);
    TF_CHECK(gap != 0 && rest != 0);
    dos_free(gap);
    brk = NH_BREAK();
    cmem_init();
    TF_CHECK(NH_BREAK() > brk);
    n = fill();
    TF_EQ(fill(), n);
    TF_CHECK(NH_BREAK() <= NH_SAFE);
    dos_free(rest);
}
#endif

int main(void)
{
    u16 n;
    int only;

    only = NH_SMALL;
    tf_begin(only ? "T_NHEAPG" : "T_NHEAP");
#ifdef COMPAT_WC6
    if (!only)
        test_short();
    _amblksiz = NH_STEP;
    sbrk((int)NH_AIM());
    TF_EQ(NH_AIM(), 0);
#endif
    if (!only)
        cmem_init();
    dos_heap_top();
#ifdef COMPAT_WC6
    TF_EQ(NH_BREAK(), NH_TOP);
#endif
    n = fill();
    TF_CHECK(n >= NH_MIN);
    TF_EQ(fill(), n);
    return tf_done();
}
