/*
 * LZEXP.C - expansion of a file compressed in Microsoft's COMPRESS
 * format (SZDD).
 *
 * A match is copied a byte at a time, as its source may overlap the
 * bytes it writes, in runs that stop where the source or the destination
 * reaches the window's end; the window goes out between two runs.  The
 * input is refilled only when fewer than LZ_TOKMAX bytes are left, so a
 * token is read without a check per byte, and one that ran past the end
 * of the input is caught before its bytes are copied.  No pointer to a
 * variable of the loop is taken, so the compilers can keep them in
 * registers.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LZEXP.H"

#define LZ_MAGICSZ      8       /* bytes of the magic                   */
#define LZ_HDRSZ        14      /* magic, method, character, length     */
#define LZ_METHOD       'A'
#define LZ_START        (LZ_WINSZ - 16) /* window position of byte 0   */
#define LZ_MINLEN       3       /* bytes of the shortest match          */
#define LZ_WINMASK      (LZ_WINSZ - 1)
#define LZ_EXTLEN       3       /* characters of a whole extension      */

static const u8 lz_magic[LZ_MAGICSZ] = {
    'S', 'Z', 'D', 'D', 0x88, 0xF0, 0x27, 0x33
};

/* Copy n bytes, at least one, from sp to op, forward a byte at a time,
   so a source just behind op repeats its bytes. */
static void copy(u8 *op, const u8 *sp, u16 n)
{
    do {
        *op++ = *sp++;
    } while (--n != 0);
}

/* Move the *n bytes of input left at ip to the start of w->in and read
   after them until LZ_INSZ bytes are there or rd gives none, so a buffer
   left short means the input has ended; their number into *n: 0 or rd's
   code. */
static int fill(lz_work *w, lz_read_fn rd, void *io, const u8 *ip, u16 *n)
{
    u16 got;
    int rc;

    memmove(w->in, ip, *n);
    do {
        rc = rd(io, w->in + *n, (u16)(LZ_INSZ - *n), &got);
        if (rc == LZ_OK)
            *n = (u16)(*n + got);
    } while (rc == LZ_OK && got != 0 && *n < LZ_INSZ);
    return rc;
}

int lz_expand(lz_work *w, lz_read_fn rd, lz_write_fn wr, void *io)
{
    u8 *win;
    u8 *ip;
    u8 *iend;
    u8 *full;
    u32 left;
    u16 flags;
    u16 pos;
    u16 from;
    u16 src;
    u16 n;
    u16 k;
    u16 have;
    int rc;

    have = 0;
    rc = fill(w, rd, io, w->in, &have);
    if (rc != LZ_OK)
        return rc;
    ip = w->in;
    if (have < LZ_HDRSZ || memcmp(ip, lz_magic, LZ_MAGICSZ) != 0 ||
        ip[LZ_MAGICSZ] != LZ_METHOD)
        return LZ_EFORMAT;
    left = get_le32(ip + LZ_HDRSZ - 4);
    iend = ip + have;
    ip += LZ_HDRSZ;
    full = w->in + LZ_INSZ;
    win = w->win;
    memset(win, ' ', LZ_WINSZ);
    pos = LZ_START;
    from = pos;
    flags = 0;
    while (left != 0) {
        if (iend - ip < LZ_TOKMAX && iend == full) {
            have = (u16)(iend - ip);
            rc = fill(w, rd, io, ip, &have);
            if (rc != LZ_OK)
                return rc;
            ip = w->in;
            iend = ip + have;
        }
        flags >>= 1;
        if ((flags & 0x100) == 0)
            flags = (u16)(0xFF00u | *ip++);
        if (flags & 1) {
            win[pos++] = *ip++;
            left--;
            n = 0;
        } else {
            src = (u16)(ip[0] | ((ip[1] & 0xF0) << 4));
            n = (u16)((ip[1] & 0x0F) + LZ_MINLEN);
            ip += 2;
        }
        if (ip > iend || n > left)
            return LZ_EFORMAT;
        left -= n;
        for (;;) {
            if (pos == LZ_WINSZ) {
                rc = wr(io, win + from, (u16)(LZ_WINSZ - from));
                if (rc != LZ_OK)
                    return rc;
                pos = 0;
                from = 0;
            }
            if (n == 0)
                break;
            k = (u16)(LZ_WINSZ - pos);
            if (k > LZ_WINSZ - src)
                k = (u16)(LZ_WINSZ - src);
            if (k > n)
                k = n;
            copy(win + pos, win + src, k);
            n = (u16)(n - k);
            pos = (u16)(pos + k);
            src = (u16)((src + k) & LZ_WINMASK);
        }
    }
    if (ip != iend)
        return LZ_EFORMAT;
    if (iend == full) {
        rc = rd(io, w->in, 1, &have);
        if (rc != LZ_OK)
            return rc;
        if (have != 0)
            return LZ_EFORMAT;
    }
    return pos == from ? LZ_OK : wr(io, win + from, (u16)(pos - from));
}

/* ---------------------------------------------------------------------
 * Names.
 * --------------------------------------------------------------------- */

void lz_name(char *name)
{
    char *dot;

    dot = strchr(name, '.');
    if (dot == NULL)
        strcat(name, "._");
    else if (strlen(dot + 1) < LZ_EXTLEN)
        strcat(name, "_");
    else
        dot[LZ_EXTLEN] = '_';
}
