/*
 * T_CMEM.C - unit test for the far blocks of COMPAT.H (cmem_*).
 *
 * usage: T_CMEM
 *
 * Checks:
 * - before any far block exists, cmem_init is called and a far block
 *   allocated; NEAR_N near blocks of NEAR_SZ bytes, about the
 *   CMEM_NEARHEAP bytes cmem_init promises, must then still be
 *   allocated, as the engine's tables need after the client has taken
 *   far memory;
 * - blocks of 1 byte and CMEM_MAXBLK bytes are allocated with their
 *   sizes, and 0 and sizes above CMEM_MAXBLK are refused;
 * - cmem_put, cmem_get and cmem_set move bytes in and out at both ends
 *   of a full-size block, and a count of 0 does nothing;
 * - cmem_move with overlapping ranges in both directions matches a near
 *   model, and so does cmem_copy between two blocks;
 * - NBIG full-size blocks are held at once, so the far heap is shown to
 *   reach beyond 64 KB;
 * - accesses that pass the end of a block copy nothing under DOS; on
 *   the host they abort, which is caught through SIGABRT and longjmp.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include "COMPAT.H"
#include "TESTFW.H"

#define NMODEL          300
#define NBIG            4       /* full-size blocks held at once        */
#define NEAR_SZ         1024    /* near blocks taken after a far one    */
/* As many as CMEM_NEARHEAP holds, less two for the block descriptors and
   the allocator's headers. */
#define NEAR_N          ((int)(CMEM_NEARHEAP / NEAR_SZ) - 2)

static u8 g_model[NMODEL];
static u8 g_buf[NMODEL];
/* The out-of-bounds cases: the two blocks they use, the jump back from
   the SIGABRT handler and whether it ran. */
static cmem_blk *g_oobb;
static cmem_blk *g_oobc;
static jmp_buf g_oobjmp;
static volatile int g_aborted;

/* A byte pattern that differs at every position of a 256-byte run. */
static u8 pat(u16 i, u8 seed)
{
    return (u8)((i * 7u + seed) & 0xFFu);
}

static void test_near(void)
{
    cmem_blk *b;
    void *p[NEAR_N];
    int n;

    cmem_init();
    /* As large as the screen shadow vid_open takes first: a character
       and an attribute byte per cell. */
    b = cmem_alloc(VID_CELLS * 2);
    TF_CHECK(b != NULL);
    for (n = 0; n < NEAR_N; n++) {
        p[n] = malloc(NEAR_SZ);
        if (p[n] == NULL)
            break;
    }
    TF_EQ(n, NEAR_N);
    while (n > 0)
        free(p[--n]);
    cmem_free(b);
}

static void test_alloc(void)
{
    cmem_blk *b;

    TF_CHECK(cmem_alloc(0) == NULL);
    TF_CHECK(cmem_alloc((u16)(CMEM_MAXBLK + 1u)) == NULL);
    TF_CHECK(cmem_alloc(0xFFFFu) == NULL);
    b = cmem_alloc(1);
    if (TF_CHECK(b != NULL)) {
        TF_EQ(cmem_size(b), 1);
        cmem_put(b, 0, "Q", 1);
        g_buf[0] = 0;
        cmem_get(b, 0, g_buf, 1);
        TF_EQ(g_buf[0], 'Q');
        cmem_free(b);
    }
    b = cmem_alloc(CMEM_MAXBLK);
    if (TF_CHECK(b != NULL)) {
        TF_EQ(cmem_size(b), CMEM_MAXBLK);
        cmem_free(b);
    }
    cmem_free(NULL);
}

static void test_getput(void)
{
    cmem_blk *b;
    u16 i;
    u16 end;

    b = cmem_alloc(CMEM_MAXBLK);
    if (!TF_CHECK(b != NULL))
        return;
    for (i = 0; i < NMODEL; i++)
        g_model[i] = pat(i, 3);
    end = (u16)(CMEM_MAXBLK - NMODEL);
    cmem_put(b, 0, g_model, NMODEL);
    cmem_put(b, end, g_model, NMODEL);
    memset(g_buf, 0, sizeof g_buf);
    cmem_get(b, 0, g_buf, NMODEL);
    TF_CHECK(memcmp(g_buf, g_model, NMODEL) == 0);
    memset(g_buf, 0, sizeof g_buf);
    cmem_get(b, end, g_buf, NMODEL);
    TF_CHECK(memcmp(g_buf, g_model, NMODEL) == 0);

    cmem_set(b, 10, 0xEE, 20);
    cmem_get(b, 0, g_buf, 40);
    for (i = 0; i < 40; i++) {
        if (i >= 10 && i < 30)
            TF_EQ(g_buf[i], 0xEE);
        else
            TF_EQ(g_buf[i], g_model[i]);
    }

    /* A count of 0 does nothing, even at the very end. */
    cmem_put(b, CMEM_MAXBLK, g_model, 0);
    cmem_set(b, CMEM_MAXBLK, 0, 0);
    g_buf[0] = 0x55;
    cmem_get(b, CMEM_MAXBLK, g_buf, 0);
    TF_EQ(g_buf[0], 0x55);

    /* The last byte. */
    cmem_set(b, (u16)(CMEM_MAXBLK - 1u), 0x99, 1);
    cmem_get(b, (u16)(CMEM_MAXBLK - 1u), g_buf, 1);
    TF_EQ(g_buf[0], 0x99);
    cmem_free(b);
}

static void test_move(void)
{
    cmem_blk *b;
    cmem_blk *c;
    u16 i;

    b = cmem_alloc(NMODEL);
    c = cmem_alloc(NMODEL);
    if (!TF_CHECK(b != NULL && c != NULL)) {
        cmem_free(b);
        cmem_free(c);
        return;
    }
    for (i = 0; i < NMODEL; i++)
        g_model[i] = pat(i, 11);
    cmem_put(b, 0, g_model, NMODEL);

    /* Up: destination above an overlapping source. */
    cmem_move(b, 50, 10, 200);
    memmove(g_model + 50, g_model + 10, 200);
    cmem_get(b, 0, g_buf, NMODEL);
    TF_CHECK(memcmp(g_buf, g_model, NMODEL) == 0);

    /* Down. */
    cmem_move(b, 3, 40, 250);
    memmove(g_model + 3, g_model + 40, 250);
    cmem_get(b, 0, g_buf, NMODEL);
    TF_CHECK(memcmp(g_buf, g_model, NMODEL) == 0);

    /* Onto itself, and by one byte. */
    cmem_move(b, 7, 7, 100);
    cmem_move(b, 1, 0, (u16)(NMODEL - 1));
    memmove(g_model + 1, g_model, NMODEL - 1);
    cmem_get(b, 0, g_buf, NMODEL);
    TF_CHECK(memcmp(g_buf, g_model, NMODEL) == 0);

    cmem_set(c, 0, 0, NMODEL);
    cmem_copy(c, 100, b, 5, 150);
    cmem_get(c, 0, g_buf, NMODEL);
    for (i = 0; i < NMODEL; i++) {
        if (i >= 100 && i < 250)
            TF_EQ(g_buf[i], g_model[i - 95]);
        else
            TF_EQ(g_buf[i], 0);
    }
    cmem_free(b);
    cmem_free(c);
}

static void test_many(void)
{
    cmem_blk *b[NBIG];
    int i;
    int n;

    n = 0;
    for (i = 0; i < NBIG; i++) {
        b[i] = cmem_alloc(CMEM_MAXBLK);
        if (b[i] != NULL) {
            cmem_set(b[i], 0, (u8)(0x40 + i), CMEM_MAXBLK);
            n++;
        }
    }
    TF_EQ(n, NBIG);
    for (i = 0; i < NBIG; i++) {
        if (b[i] == NULL)
            continue;
        g_buf[0] = 0;
        g_buf[1] = 0;
        cmem_get(b[i], 0, g_buf, 1);
        cmem_get(b[i], (u16)(CMEM_MAXBLK - 1u), g_buf + 1, 1);
        TF_EQ(g_buf[0], 0x40 + i);
        TF_EQ(g_buf[1], 0x40 + i);
    }
    for (i = 0; i < NBIG; i++)
        cmem_free(b[i]);
}

/* Out-of-bounds accesses.  Each case is run through oob_case(); on the
   host it must abort, under DOS it must leave the blocks and g_buf
   unchanged. */
static void oob_handler(int sig)
{
    signal(sig, oob_handler);
    g_aborted = 1;
    longjmp(g_oobjmp, 1);
}

static void oob_case(int k)
{
    switch (k) {
    case 0:
        cmem_get(g_oobb, 90, g_buf, 11);
        break;
    case 1:
        cmem_put(g_oobb, 100, g_model, 1);
        break;
    case 2:
        cmem_set(g_oobb, 0xFFFFu, 0, 2);
        break;
    case 3:
        cmem_move(g_oobb, 0, 50, 51);
        break;
    case 4:
        cmem_move(g_oobb, 60, 0, 41);
        break;
    case 5:
        cmem_copy(g_oobc, 0, g_oobb, 1, 100);
        break;
    default:
        cmem_copy(g_oobb, 1, g_oobc, 0, 100);
        break;
    }
}

#define NOOB            7

static void test_bounds(void)
{
    u16 i;
    int k;
    int ok;

    g_oobb = cmem_alloc(100);
    g_oobc = cmem_alloc(100);
    if (!TF_CHECK(g_oobb != NULL && g_oobc != NULL))
        return;
    for (i = 0; i < 100; i++)
        g_model[i] = pat(i, 5);
    signal(SIGABRT, oob_handler);
    for (k = 0; k < NOOB; k++) {
        cmem_put(g_oobb, 0, g_model, 100);
        cmem_set(g_oobc, 0, 0x11, 100);
        memset(g_buf, 0x22, 100);
        g_aborted = 0;
        if (setjmp(g_oobjmp) == 0)
            oob_case(k);
#ifdef COMPAT_HOST
        TF_EQ(g_aborted, 1);
#else
        TF_EQ(g_aborted, 0);
#endif
        ok = 1;
        cmem_get(g_oobb, 0, g_buf + 100, 100);
        if (memcmp(g_buf + 100, g_model, 100) != 0)
            ok = 0;
        cmem_get(g_oobc, 0, g_buf + 100, 100);
        for (i = 0; i < 100; i++) {
            if (g_buf[i] != 0x22 || g_buf[100 + i] != 0x11)
                ok = 0;
        }
        if (!TF_CHECK(ok))
            printf("out-of-bounds case %d changed memory\n", k);
    }
    signal(SIGABRT, SIG_DFL);
    cmem_free(g_oobb);
    cmem_free(g_oobc);
}

int main(void)
{
    tf_begin("T_CMEM");
    test_near();
    test_alloc();
    test_getput();
    test_move();
    test_many();
    test_bounds();
    return tf_done();
}
