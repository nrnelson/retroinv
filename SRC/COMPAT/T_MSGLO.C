/*
 * T_MSGLO.C - unit test for where msg_load of MSG.H leaves its far
 * blocks under DOS: the block of the run where the first one was, with
 * nothing freed under it.
 *
 * usage: T_MSGLO
 *
 * INV.MSG and INVSERV.MSG are loaded from the program's directory, one
 * after the other.  The test reads the far address of each block as it
 * is freed, which portable code may not, so it lives in SRC/COMPAT/ and
 * is built for DOS only.
 *
 * Checks:
 * - for each file, msg_load frees two blocks, the whole file's and the
 *   second pass's, and the block of the run, freed by msg_free, starts
 *   where the whole file's did, whatever way the compiler's far heap
 *   cuts a block from free memory (cmem_replace of COMPAT.H);
 * - for INV.MSG, loaded while the far heap has nothing free but what
 *   lies above its blocks, the second pass's block lay above the block
 *   of the run, and so does a block allocated after the load.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_INTERNAL
#define COMPAT_FIND
#include "COMPAT.H"
#include <dos.h>
#include <stdio.h>
#include "MSG.H"
#include "TESTFW.H"

#define ML_PATHMAX      96
#define ML_FREES        4       /* frees of one file's run              */
#define ML_PROBE        16      /* bytes of the block after the load    */

static u32 g_at[ML_FREES];      /* where each block freed started       */
static u16 g_n;

/* The address of b's first byte, counted in bytes from 0000:0000. */
static u32 linear(cmem_blk *b)
{
    FARP(u8) p;

    p = cmem_ptr(b);
    return ((u32)FP_SEG(p) << 4) + FP_OFF(p);
}

static void freed(cmem_blk *b)
{
    if (g_n < ML_FREES)
        g_at[g_n] = linear(b);
    g_n++;
}

/* Load file of dir; first is 1 for the first load of the run. */
static void test_file(const char *dir, const char *file, int first)
{
    cmem_blk *b;

    g_n = 0;
    cmem_freeing = freed;
    if (TF_EQ(msg_load(dir, file), 0)) {
        TF_EQ(g_n, 2);
        b = cmem_alloc(ML_PROBE);
        if (TF_CHECK(b != NULL))
            cmem_free(b);
        msg_free();
        if (TF_EQ(g_n, ML_FREES)) {
            if (!TF_CHECK(g_at[3] == g_at[0]) ||
                !TF_CHECK(!first || g_at[1] > g_at[3]) ||
                !TF_CHECK(!first || g_at[2] > g_at[3]))
                printf("  %s: file %lX, second pass %lX, after %lX, "
                       "kept %lX\n", file, (unsigned long)g_at[0],
                       (unsigned long)g_at[1], (unsigned long)g_at[2],
                       (unsigned long)g_at[3]);
        }
    }
    cmem_freeing = NULL;
}

int main(int argc, char **argv)
{
    char dir[ML_PATHMAX];

    tf_begin("T_MSGLO");
    TF_CHECK(argc > 0);
    cmem_init();
    tf_progdir(dir, ML_PATHMAX, argv[0]);
    test_file(dir, MSG_FILE, 1);
    test_file(dir, MSG_SRVFILE, 0);
    return tf_done();
}
