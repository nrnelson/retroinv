/*
 * LZCOPY.C - the copy of a release file, expanded when compressed
 * (lz_copy of LZEXP.H), over the cf_ calls.
 *
 * A module of its own, so that LZEXP.C stays free of file calls and its
 * unit test links it alone.
 */
#include "COMPAT.H"
#include "LZEXP.H"

/* lz_expand's reader and writer over io, the handles of the file read
   and of the file written. */
static int file_read(void *io, u8 *buf, u16 max, u16 *got)
{
    return cf_read(((int *)io)[0], buf, max, got);
}

static int file_write(void *io, const u8 *buf, u16 n)
{
    return cf_write(((int *)io)[1], buf, n);
}

int lz_copy(lz_work *w, int in, int out, int lz)
{
    int h[2];
    u16 got;
    int rc;

    if (lz) {
        h[0] = in;
        h[1] = out;
        return lz_expand(w, file_read, file_write, h);
    }
    do {
        rc = cf_read(in, w->win, LZ_WINSZ, &got);
        if (rc == 0 && got != 0)
            rc = cf_write(out, w->win, got);
    } while (rc == 0 && got == LZ_WINSZ);
    return rc;
}
