/*
 * CMEM.C - far memory blocks behind opaque handles.
 *
 * A block is allocated from the compiler's far heap (_fmalloc under the
 * Watcom compilers, farmalloc under Turbo C) and described by a small
 * near descriptor holding its far address and size; on the host both
 * come from malloc.  Every access is checked against the size: under DOS
 * an access outside the block copies nothing, on the host it calls
 * abort(), so tests catch it.  Copies go through CM_FCOPY: the
 * library's movedata under DOS (Turbo C 2.01, Watcom C 6.5 and Open
 * Watcom all have it, a forward string move by words), memmove on the
 * host.  cmem_set and the backward copy of cmem_move are plain loops
 * over far pointers.  cf_readblk and cf_writeblk are here, beside the
 * descriptor, and leave the DOS call to cf_readfar and cf_writefar
 * (CDOS.C), so a program that reads into or writes from far blocks
 * links both modules.
 *
 * Under Windows a block is a handle from GlobalAlloc, which every call
 * locks once on entry (CM_LOCK) and unlocks before it returns (CM_DONE,
 * or CM_DONERC around the call's result), but a block of
 * cmem_alloc_perm, locked from its allocation to its free, and one of
 * cmem_alloc_dos, which is fixed; the descriptor's far address is valid
 * only while the block is locked.
 * The copies are inline string moves, so that nothing is called
 * between making a far pointer into DGROUP and using it: a call that
 * loads a discarded code segment may move DGROUP in real mode.
 */
#define COMPAT_INTERNAL
#define COMPAT_FIND
#define COMPAT_REPLACE
#define COMPAT_HEAP
#include "COMPAT.H"
#include <stdlib.h>
#include <string.h>
#ifndef COMPAT_HOST
#include <dos.h>
#endif
/* CM_FALLOC(b, n) gives the descriptor b a block of n bytes, CM_NONE(b)
   is 1 when there was none to give, and CM_FFREE(b) gives it back. */
#ifdef COMPAT_WIN16
#include "CWIN.H"
#define CM_FALLOC(b, n) blk_get(b, n)
#define CM_NONE(b) ((b)->h == NULL)
#define CM_FFREE(b) blk_put(b)
#else
#define CM_NONE(b) ((b)->p == NULL)
#ifdef COMPAT_OW
#include <malloc.h>
#define CM_FALLOC(b, n) ((b)->p = (FARP(u8))_fmalloc((size_t)(n)))
#define CM_FFREE(b) _ffree((b)->p)
#endif
#endif
#ifdef COMPAT_WC6
#include <malloc.h>
#define CM_FALLOC(b, n) ((b)->p = (FARP(u8))_fmalloc((size_t)(n)))
#define CM_FFREE(b) _ffree((b)->p)
#endif
#ifdef COMPAT_TC
#include <alloc.h>
#define CM_FALLOC(b, n) ((b)->p = (FARP(u8))farmalloc((unsigned long)(n)))
#define CM_FFREE(b) farfree((b)->p)
#endif
#ifdef COMPAT_HOST
#define CM_FALLOC(b, n) ((b)->p = (FARP(u8))malloc((size_t)(n)))
#define CM_FFREE(b) free((b)->p)
#define CM_FCOPY(d, s, n) memmove(d, s, (size_t)(n))
#else
#ifdef COMPAT_WIN16
/* Copy n bytes from the far pointer s to the far pointer d, first byte
   first, so that an overlapping copy to a lower address is safe, by a
   string move expanded in place: words, then the odd byte, whose count
   ADC takes from the carry SHR left, as REP MOVSW changes no flag. */
void cm_fcopy(FARP(u8) d, FARP(const u8) s, u16 n);
#pragma aux cm_fcopy = \
    "push ds" \
    "mov ds,dx" \
    "shr cx,1" \
    "rep movsw" \
    "adc cx,cx" \
    "rep movsb" \
    "pop ds" \
    parm [es di] [dx si] [cx] \
    modify [si di cx];
#define CM_FCOPY(d, s, n) cm_fcopy(d, s, n)
#else
/* Copy n bytes from the far pointer s to the far pointer d, first byte
   first, so that an overlapping copy to a lower address is safe. */
#define CM_FCOPY(d, s, n) \
    movedata(FP_SEG(s), FP_OFF(s), FP_SEG(d), FP_OFF(d), n)
#endif
#endif

/* ---------------------------------------------------------------------
 * The near heap.
 *
 * Under Turbo C and Open Watcom a DOS program that uses far blocks
 * keeps its near heap to a fixed size, so that the part of DGROUP's 64
 * KB that the static data, the stack and that heap leave goes to the
 * far heap instead.  The size comes from the one near-heap module the
 * program links beside this one: CMEMHN.C (CMEM_NEARHEAP), for a
 * program on the TCP stack CMEMHT.C (CMEM_TCPHEAP), for INV CMEMHI.C
 * and for the network-only client CMEMHR.C (each its own smaller heap
 * under Turbo C, CMEM_INVHEAP and CMEM_REMHEAP).  The module defines
 * Turbo C's _heaplen and, in every build, cmem_heapsz, which only Open
 * Watcom's DOS cmem_init reads, so that such a link without a module
 * fails; TEST/mapchk.sh fails the DOS map of any build that links this
 * module without exactly one.  A Windows program links none, its far
 * blocks coming from the global heap (cmem_init, COMPAT.H).
 *
 * Turbo C's start-up code sizes the near heap of the small and medium
 * models from _heaplen (0, its default, gives the heap all of DGROUP the
 * static data and the stack leave), puts the stack right above it and
 * starts the far heap above DGROUP.  When the extent of the static data
 * (from DGROUP's start to its end, the padding between modules
 * included), _heaplen and _stklen come to more than 64 KB it ends the
 * program before main (status 3), so TEST/mapchk.sh checks that sum for
 * each program.  The reservation is not a limit: Turbo C's allocator
 * refuses only when the heap would come near the stack pointer, so a
 * heap that outgrows its reservation takes the bottom of the stack,
 * which cstk_unused (CDOS.C) then reports as used.
 *
 * The Watcom near heap lies at the top of DGROUP, above the stack, and
 * grows by enlarging the program's DOS memory block.  A far block
 * allocated first sits right after that DOS block and stops the growth,
 * so cmem_init grows the near heap before any far block is taken: it
 * allocates near blocks of CM_GROWBLK bytes, CM_GROWN of them or until
 * malloc fails, and frees them again, which leaves the space in the
 * near heap.  Open Watcom grows it by cmem_heapsz bytes, and its far
 * blocks then start right above.  Watcom C 6.5 grows it to the whole of
 * DGROUP (CM_GROWN blocks cover 64 KB) whatever the near-heap module
 * gives, as its _fmalloc takes far blocks out of free near heap space
 * once DOS has no memory left, so none of DGROUP is lost to the far
 * blocks; holding its near heap to CMEM_NEARHEAP was measured to leave
 * less memory for the screens, not more.  Under Watcom C 6.5 the
 * module's size counts only in the DGROUP check of TEST/mapchk.sh,
 * which every build must pass.
 *
 * Watcom C 6.5's _fmalloc grows a far heap segment in multiples of
 * _amblksiz bytes, which its start-up code sets to 8 KB, so a block
 * that would fit in what DOS has left beside the segment still fails
 * when the rounded growth does not.  Under Watcom C 6.5, cmem_init sets
 * _amblksiz to CM_AMBLK, one paragraph, the unit DOS allocates in, once
 * the near heap is grown.
 *
 * Under Watcom C 6.5 cmem_init first calls dos_heap_top, which grows
 * the near heap to the top of DGROUP in one step, as the library's own
 * last step to the top can hang its malloc (CDOS.C); the blocks then
 * grow it only when DOS had not the memory for that step, as far as DOS
 * allows.
 * --------------------------------------------------------------------- */
#define CM_GROWBLK      1024

#ifdef COMPAT_OW
#ifndef COMPAT_WIN16
#define CM_GROWN        ((int)((cmem_heapsz + CM_GROWBLK - 1u) / CM_GROWBLK))
#endif
#endif
#ifdef COMPAT_WC6
#define CM_GROWN        64
#define CM_AMBLK        16
#endif

#ifdef CM_GROWN
/* Each block holds the address of the one allocated before it, so they
   are freed newest first without an array of their addresses. */
void cmem_init(void)
{
    void **b;
    void **last;
    int n;

    dos_heap_top();
    last = NULL;
    for (n = 0; n < CM_GROWN; n++) {
        b = (void **)malloc(CM_GROWBLK);
        if (b == NULL)
            break;
        *b = (void *)last;
        last = b;
    }
    while (last != NULL) {
        b = (void **)*last;
        free(last);
        last = b;
    }
#ifdef CM_AMBLK
    _amblksiz = CM_AMBLK;
#endif
}
#else
void cmem_init(void)
{
}
#endif

/* ---------------------------------------------------------------------
 * Far blocks.
 * --------------------------------------------------------------------- */

cmem_fault_fn cmem_fault = NULL;

void (*cmem_freeing)(cmem_blk *b) = NULL;

#ifdef COMPAT_WIN16

/* The kinds of block: of cmem_alloc, cmem_alloc_perm and
   cmem_alloc_dos. */
#define CM_MOVE         0       /* moveable, locked inside each call    */
#define CM_PERM         1       /* locked for the run                   */
#define CM_DOS          2       /* GlobalDosAlloc's, fixed              */

struct cmem_blk {
    HGLOBAL h;                  /* the global memory block; for CM_DOS
                                   its selector                         */
    FARP(u8) p;                 /* its first byte while locked, else
                                   NULL                                 */
    u16 size;                   /* bytes, 1..CMEM_MAXBLK                */
    u8 kind;                    /* CM_MOVE, CM_PERM or CM_DOS           */
};

/* The kind of block cmem_alloc makes, CM_MOVE but while
   cmem_alloc_perm's or cmem_alloc_dos's call of it runs. */
static u8 cm_kind;

/* Allocate the global block of b, size bytes, of the kind cm_kind:
   moveable, but fixed for a block of cmem_alloc_perm in real mode,
   which is locked at once, and fixed in the first megabyte for one of
   cmem_alloc_dos, whose selector the low word of GlobalDosAlloc's
   result gives. */
static void blk_get(cmem_blk *b, u16 size)
{
    UINT flags;
    DWORD dos;

    b->kind = cm_kind;
    b->p = NULL;
    if (b->kind == CM_DOS) {
        dos = GlobalDosAlloc((DWORD)size);
        b->h = (HGLOBAL)LOWORD(dos);
        if (dos != 0)
            b->p = (FARP(u8))MAKELP(LOWORD(dos), 0);
        return;
    }
    flags = GMEM_MOVEABLE;
    if (b->kind == CM_PERM && !(GetWinFlags() & WF_PMODE))
        flags = GMEM_FIXED;
    b->h = GlobalAlloc(flags, (DWORD)size);
    if (b->kind == CM_PERM && b->h != NULL)
        b->p = (FARP(u8))GlobalLock(b->h);
}

static void blk_put(cmem_blk *b)
{
    if (b->kind == CM_DOS) {
        GlobalDosFree((UINT)b->h);
        return;
    }
    if (b->kind == CM_PERM)
        GlobalUnlock(b->h);
    GlobalFree(b->h);
}

/* Lock b for one call, which sets b->p; a block for the run or a fixed
   one stays as it is.  A moveable block that is not discardable always
   locks. */
static void blk_lock(cmem_blk *b)
{
    if (b->kind == CM_MOVE)
        b->p = (FARP(u8))GlobalLock(b->h);
}

static void blk_unlock(cmem_blk *b)
{
    if (b->kind == CM_MOVE) {
        GlobalUnlock(b->h);
        b->p = NULL;
    }
}

/* rc, once b is unlocked. */
static int blk_unlock_rc(cmem_blk *b, int rc)
{
    blk_unlock(b);
    return rc;
}

#define CM_LOCK(b)      blk_lock(b)
#define CM_DONE(b)      blk_unlock(b)
#define CM_DONERC(b, rc) blk_unlock_rc(b, rc)

#else

struct cmem_blk {
    FARP(u8) p;                 /* first byte of the block              */
    u16 size;                   /* bytes, 1..CMEM_MAXBLK                */
};

/* The start and the end of a call's use of b, the second with the
   call's result rc: a far block of DOS stays where it is. */
#define CM_LOCK(b)
#define CM_DONE(b)
#define CM_DONERC(b, rc) (rc)

#endif

cmem_blk *cmem_alloc(u16 size)
{
    cmem_blk *b;

    if (size == 0 || size > CMEM_MAXBLK)
        return NULL;
    if (cmem_fault != NULL && (*cmem_fault)(size) != 0)
        return NULL;
    b = (cmem_blk *)malloc(sizeof(cmem_blk));
    if (b == NULL)
        return NULL;
    CM_FALLOC(b, size);
    if (CM_NONE(b)) {
        free(b);
        return NULL;
    }
    b->size = size;
    return b;
}

void cmem_free(cmem_blk *b)
{
    if (b == NULL)
        return;
    if (cmem_freeing != NULL)
        (*cmem_freeing)(b);
    CM_FFREE(b);
    free(b);
}

#ifdef COMPAT_WC6
/* Watcom C 6.5's far heap cuts a block from the end of a free piece, so
   a block allocated where a freed one of was bytes lay would end where
   that one did and leave the rest of its place free under it.  The call
   first takes that rest from the piece's end as a block of its own,
   hold, so that the new block gets the piece's start, and then frees
   hold, which joins the free memory above the new block.  The heap
   gives a block an even size and CM_TAG bytes before it; when the place
   has less than CM_HOLDMIN bytes more than the new block, that block is
   allocated as it comes. */
#define CM_TAG          2
#define CM_HOLDMIN      16

cmem_blk *cmem_replace(u16 size, u16 was)
{
    cmem_blk *b;
    FARP(u8) hold;
    u16 need;

    was = (u16)((was + 1) & ~1u);
    need = (u16)((size + 1) & ~1u);
    hold = NULL;
    if (was >= need + CM_HOLDMIN)
        hold = (FARP(u8))_fmalloc((size_t)(was - need - CM_TAG));
    b = cmem_alloc(size);
    if (hold != NULL)
        _ffree(hold);
    return b;
}
#endif

#ifdef COMPAT_WIN16
/* cmem_alloc of a block of the kind given. */
static cmem_blk *alloc_kind(u16 size, u8 kind)
{
    cmem_blk *b;

    cm_kind = kind;
    b = cmem_alloc(size);
    cm_kind = CM_MOVE;
    return b;
}

cmem_blk *cmem_alloc_perm(u16 size)
{
    return alloc_kind(size, CM_PERM);
}

cmem_blk *cmem_alloc_dos(u16 size)
{
    return alloc_kind(size, CM_DOS);
}

/* The selector of a fixed block is its handle. */
u16 cmem_locks(cmem_blk *b, int *fixed)
{
    *fixed = (UINT)b->h == SELECTOROF(GlobalLock(b->h));
    GlobalUnlock(b->h);
    return (u16)(GlobalFlags(b->h) & GMEM_LOCKCOUNT);
}
#endif

u16 cmem_size(cmem_blk *b)
{
    return b->size;
}

FARP(u8) cmem_ptr(cmem_blk *b)
{
    return b->p;
}

/* 1 when n bytes at off lie inside b.  The host aborts instead of
   returning 0.  The sum is formed in 32 bits, so it cannot wrap. */
static int cm_inside(cmem_blk *b, u16 off, u16 n)
{
    if ((u32)off + (u32)n <= (u32)b->size)
        return 1;
#ifdef COMPAT_HOST
    abort();
#endif
    return 0;
}

void cmem_get(cmem_blk *b, u16 off, void *dst, u16 n)
{
    FARP(u8) s;
    FARP(u8) d;

    if (n == 0 || !cm_inside(b, off, n))
        return;
    CM_LOCK(b);
    s = b->p + off;
    d = (FARP(u8))dst;
    CM_FCOPY(d, s, n);
    CM_DONE(b);
}

void cmem_put(cmem_blk *b, u16 off, const void *src, u16 n)
{
    FARP(u8) d;
    FARP(const u8) s;

    if (n == 0 || !cm_inside(b, off, n))
        return;
    CM_LOCK(b);
    d = b->p + off;
    s = (FARP(const u8))src;
    CM_FCOPY(d, s, n);
    CM_DONE(b);
}

void cmem_set(cmem_blk *b, u16 off, u8 v, u16 n)
{
    FARP(u8) d;

    if (n == 0 || !cm_inside(b, off, n))
        return;
    CM_LOCK(b);
    d = b->p + off;
    while (n-- != 0)
        *d++ = v;
    CM_DONE(b);
}

/* Overlapping ranges are copied from the end when the destination lies
   above the source, so no byte is overwritten before it is read. */
void cmem_move(cmem_blk *b, u16 to, u16 from, u16 n)
{
    FARP(u8) d;
    FARP(u8) s;

    if (n == 0 || !cm_inside(b, to, n) || !cm_inside(b, from, n))
        return;
    if (to == from)
        return;
    CM_LOCK(b);
    if (to < from) {
        d = b->p + to;
        s = b->p + from;
        CM_FCOPY(d, s, n);
    } else {
        d = b->p + to + n;
        s = b->p + from + n;
        while (n-- != 0)
            *--d = *--s;
    }
    CM_DONE(b);
}

void cmem_copy(cmem_blk *dst, u16 doff, cmem_blk *src, u16 soff, u16 n)
{
    FARP(u8) d;
    FARP(u8) s;

    if (n == 0 || !cm_inside(dst, doff, n) || !cm_inside(src, soff, n))
        return;
    CM_LOCK(dst);
    CM_LOCK(src);
    d = dst->p + doff;
    s = src->p + soff;
    CM_FCOPY(d, s, n);
    CM_DONE(src);
    CM_DONE(dst);
}

int cf_readblk(int h, cmem_blk *b, u16 off, u16 n, u16 *got)
{
    *got = 0;
    if (!cm_inside(b, off, n))
        return CF_EINVAL;
    CM_LOCK(b);
    return CM_DONERC(b, cf_readfar(h, b->p + off, n, got));
}

int cf_writeblk(int h, cmem_blk *b, u16 off, u16 n)
{
    if (!cm_inside(b, off, n))
        return CF_EINVAL;
    CM_LOCK(b);
    return CM_DONERC(b, cf_writefar(h, b->p + off, n));
}
