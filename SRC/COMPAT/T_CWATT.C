/*
 * T_CWATT.C - unit test for CWATT.C, WATTCP's assembly helpers in C,
 * linked with WATTCP's kernel and CPKT.C as INVTCP is.
 *
 * usage: T_CWATT [dir]
 *
 * dir is the scratch directory (default the current one).  The test
 * includes WATTCP's headers, sets the BIOS clock and redirects standard
 * error, which portable code may not, so it lives in SRC/COMPAT/, is
 * built for DOS only and, as CWATT.C is, with WATTCP's packing and char
 * flags.
 *
 * Checks:
 * - the kernel starts without a packet driver: tcp_init returns, and
 *   pkt_error then reports PKT_ENONE;
 * - inchksum gives the sum of the example of RFC 1071, and over a far
 *   block of CW_BLK bytes, all FFh (the most carries) or a pattern, at
 *   lengths 0 to 3, CW_BLK - 1 and CW_BLK, the sum RFC 1071 defines,
 *   computed byte by byte;
 * - intel16 and intel swap the bytes of a word and of a long, and
 *   resolve gives aton of a dotted address and 0 for a host name, there
 *   being no name server;
 * - outs writes to handle 2 exactly the bytes of its text, CR and LF
 *   included, at lengths 0, CW_CHUNK - 1, CW_CHUNK, CW_CHUNK + 1 and
 *   2 * CW_CHUNK, CW_CHUNK being the bytes it writes at once;
 * - set_ttimeout(n) is the time now plus n ticks, and set_timeout(s) now
 *   plus s * TMR_10SEC / 10 ticks, up to the largest n and s;
 * - from CW_EVE ticks before midnight to CW_MORN ticks after it,
 *   set_ttimeout(0) grows by one with each tick, and chk_timeout of it
 *   less one, of it and of it plus one gives 1, 0 and 0: a timeout
 *   expires on the tick after its own, not on it, midnight or not.  Each
 *   of these samples counts only when the tick count did not move while
 *   it was taken, and samples must count on both sides of midnight: the
 *   clock is set, at most CW_TRIES times, until the first sample comes
 *   before midnight.  The clock is then set to the time it had before,
 *   plus the ticks the test took;
 * - wt_isn gives CW_ISNS numbers, a wt_stir before every other one,
 *   each different from the others, their high words not all the same
 *   and the steps between them not all equal, as a mere count would
 *   give, and none the number its mix gives with an empty pool, for
 *   any count of them and the ticks it ran in, so the pool and the
 *   timer chip's count go into each.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails, 2 when the directory name is too long.
 */
#define COMPAT_INTERNAL
#include "COMPAT.H"
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "WATTCP.H"
#include "TESTFW.H"
#include "T_FILES.H"

#define CW_BLK          32767   /* checksum block: the largest int len  */
#define CW_RFCSUM       0xF2DD  /* inchksum of k_rfc                    */
#define CW_CHUNK        32      /* bytes outs writes at once            */
#define CW_TEXTMAX      (2 * CW_CHUNK)
#define CW_STDERR       2       /* DOS handle of standard error         */
#define CW_EVE          3       /* ticks swept before midnight */
#define CW_MORN         3       /* and goes on after it                 */
#define CW_TRIES        3       /* times the clock is set for the sweep */
#define CW_ISNS         8       /* sequence numbers compared            */
#define CW_ROUNDS       4       /* rounds of wt_isn's mix (WT_ROUNDS)   */

/* The example of RFC 1071, section 3: its sum is DDF2h in network
   order. */
static const u8 k_rfc[] = { 0x00, 0x01, 0xF2, 0x03, 0xF4, 0xF5, 0xF6, 0xF7 };

/* ---------------------------------------------------------------------
 * The kernel.
 * --------------------------------------------------------------------- */

static void test_init(void)
{
    tcp_init();
    TF_EQ(pkt_error(), PKT_ENONE);
}

/* ---------------------------------------------------------------------
 * Checksum.
 * --------------------------------------------------------------------- */

/* The sum RFC 1071 defines over the n bytes at p: the 1's complement sum
   of the network-order words, an odd last byte the high byte of a word,
   with its bytes swapped, as inchksum gives it. */
static u16 rfc_sum(FARP(const u8) p, u16 n)
{
    u32 sum;
    u16 i;

    sum = 0;
    for (i = 0; i < n; i++) {
        sum += (i & 1) ? (u32)p[i] : (u32)p[i] << 8;
        if (sum > 0xFFFFUL)
            sum -= 0xFFFFUL;
    }
    return (u16)((sum << 8) | (sum >> 8));
}

static void test_chksum(void)
{
    static const u16 k_lens[] = { 0, 1, 2, 3, CW_BLK - 1, CW_BLK };
    cmem_blk *b;
    FARP(u8) p;
    u16 i;
    u16 k;
    int fill;

    b = cmem_alloc(CW_BLK);
    TF_CHECK(b != NULL);
    if (b == NULL)
        return;
    p = cmem_ptr(b);
    for (i = 0; i < sizeof k_rfc; i++)
        p[i] = k_rfc[i];
    TF_EQ(rfc_sum(p, sizeof k_rfc), CW_RFCSUM);
    TF_EQ(inchksum(p, sizeof k_rfc), CW_RFCSUM);
    for (fill = 0; fill < 2; fill++) {
        for (i = 0; i < CW_BLK; i++)
            p[i] = fill == 0 ? 0xFF : (u8)(i * 7 + 1);
        for (k = 0; k < sizeof k_lens / sizeof k_lens[0]; k++)
            TF_EQ(inchksum(p, (int)k_lens[k]), rfc_sum(p, k_lens[k]));
    }
    cmem_free(b);
}

/* ---------------------------------------------------------------------
 * Byte order and addresses.
 * --------------------------------------------------------------------- */

static void test_order(void)
{
    static char k_dotted[] = "10.0.2.15";
    static char k_host[] = "HOST";

    TF_EQ(intel16(0x1234), 0x3412);
    TF_EQ(intel(0x12345678UL), 0x78563412UL);
    TF_EQ(resolve(k_dotted), aton(k_dotted));
    TF_EQ(resolve(k_host), 0);
}

/* ---------------------------------------------------------------------
 * Console text.
 * --------------------------------------------------------------------- */

/* A new handle for the file of handle h (DOS function 45h), or -1. */
static int dup_handle(int h)
{
    union REGS r;

    r.h.ah = 0x45;
    r.x.bx = (unsigned)h;
    intdos(&r, &r);
    return r.x.cflag ? -1 : (int)r.x.ax;
}

/* Make handle dst refer to the file of handle h (DOS function 46h): 0,
   or 1 when DOS refuses. */
static int force_dup(int h, int dst)
{
    union REGS r;

    r.h.ah = 0x46;
    r.x.bx = (unsigned)h;
    r.x.cx = (unsigned)dst;
    intdos(&r, &r);
    return r.x.cflag != 0;
}

/* outs with handle 2 sent to a scratch file: the file must hold the n
   bytes of text and no more. */
static void outs_len(const char *text, u16 n)
{
    char s[CW_TEXTMAX + 1];
    char back[CW_TEXTMAX + 1];
    const char *path;
    int h;
    int saved;
    int sent;
    u16 got;

    memcpy(s, text, n);
    s[n] = '\0';
    path = tfs_path("OUTS", ".TMP");
    if (!TF_EQ(cf_create(path, &h), 0))
        return;
    sent = 0;
    saved = dup_handle(CW_STDERR);
    if (TF_CHECK(saved >= 0)) {
        sent = TF_EQ(force_dup(h, CW_STDERR), 0);
        if (sent) {
            outs(s);
            TF_EQ(force_dup(saved, CW_STDERR), 0);
        }
        TF_EQ(cf_close(saved), 0);
    }
    TF_EQ(cf_close(h), 0);
    if (sent && TF_EQ(tfs_read_all(path, back, sizeof back, &got), 0) &&
        TF_EQ(got, n))
        TF_CHECK(memcmp(back, s, n) == 0);
    cf_remove(path);
}

static void test_outs(void)
{
    static const u16 k_lens[] = {
        0, CW_CHUNK - 1, CW_CHUNK, CW_CHUNK + 1, 2 * CW_CHUNK
    };
    char text[CW_TEXTMAX];
    u16 i;

    for (i = 0; i < CW_TEXTMAX; i++)
        text[i] = (char)('A' + i % 26);
    text[CW_CHUNK - 2] = '\r';
    text[CW_CHUNK - 1] = '\n';
    for (i = 0; i < sizeof k_lens / sizeof k_lens[0]; i++)
        outs_len(text, k_lens[i]);
}

/* ---------------------------------------------------------------------
 * Timers.
 * --------------------------------------------------------------------- */

/* Set the BIOS tick count to t (INT 1Ah function 01h, CX:DX). */
static void set_ticks(u32 t)
{
    union REGS r;

    r.h.ah = 0x01;
    r.x.cx = (unsigned)(t >> 16);
    r.x.dx = (unsigned)(t & 0xFFFFUL);
    int86(0x1A, &r, &r);
}

static void test_set(void)
{
    u32 p;
    u32 n;
    u32 ticks;
    u32 one;
    u32 ten;
    u32 most;

    do {
        p = tmr_ticks();
        n = set_ttimeout(0);
        ticks = set_ttimeout(0xFFFFu);
        one = set_timeout(1);
        ten = set_timeout(10);
        most = set_timeout(0xFFFFu);
    } while (tmr_ticks() != p);
    TF_EQ(ticks - n, 0xFFFFUL);
    TF_EQ(one - n, TMR_10SEC / 10);
    TF_EQ(ten - n, TMR_10SEC);
    TF_EQ(most - n, 0xFFFFUL * TMR_10SEC / 10);
}

/* Sample, as the header comment says, from tick a, at which
   set_ttimeout(0) gave x, until a sample more than ticks ticks on. */
static void sweep(u32 a, u32 x, u32 ticks)
{
    u32 p;
    u32 e;
    u32 n;
    int late;
    int on;
    int early;
    int eve;
    int morn;

    eve = 0;
    morn = 0;
    for (;;) {
        p = tmr_ticks();
        n = set_ttimeout(0);
        late = chk_timeout(n - 1);
        on = chk_timeout(n);
        early = chk_timeout(n + 1);
        e = tmr_since(a);
        if (tmr_ticks() != p)
            continue;
        if (p >= TMR_DAY - CW_EVE)
            eve = 1;
        else
            morn = 1;
        if (n - x != e || !late || on || early) {
            TF_EQ(n - x, e);
            TF_EQ(late, 1);
            TF_EQ(on, 0);
            TF_EQ(early, 0);
            return;
        }
        if (e > ticks) {
            TF_CHECK(eve && morn);
            return;
        }
    }
}

static void test_midnight(void)
{
    u32 start;
    u32 lost;
    u32 a;
    u32 x;
    int tries;

    start = tmr_ticks();
    lost = 0;
    tries = 0;
    for (;;) {
        set_ticks(TMR_DAY - CW_EVE);
        do {
            a = tmr_ticks();
            x = set_ttimeout(0);
        } while (tmr_ticks() != a);
        if (a >= TMR_DAY - CW_EVE || ++tries == CW_TRIES)
            break;
        lost += CW_EVE + a;
    }
    sweep(a, x, CW_EVE + CW_MORN);
    set_ticks((start + lost + tmr_since(TMR_DAY - CW_EVE)) % TMR_DAY);
}

/* ---------------------------------------------------------------------
 * Initial sequence numbers.
 * --------------------------------------------------------------------- */

/* x turned left by n bits, 1..31. */
static u32 rotl(u32 x, int n)
{
    return (x << n) | (x >> (32 - n));
}

/* The number wt_isn would give for count c at tick t with an empty pool:
   CWATT.C's mix, CW_ROUNDS rounds of it. */
static u32 bare(u32 c, u32 t)
{
    u32 a;
    u32 b;
    int i;

    a = c;
    b = t;
    for (i = 0; i < CW_ROUNDS; i++) {
        a += b;
        b = rotl(b, 13) ^ a;
        a = rotl(a, 16);
    }
    return a ^ b;
}

static void test_isn(void)
{
    u32 n[CW_ISNS];
    u32 t0;
    u32 t1;
    u32 t;
    u32 c;
    int steps;
    int highs;
    int bares;
    int i;
    int j;

    steps = 0;
    highs = 0;
    bares = 0;
    for (i = 0; i < CW_ISNS; i++) {
        if (i % 2 == 0)
            wt_stir();
        t0 = set_ttimeout(0);
        n[i] = wt_isn();
        t1 = set_ttimeout(0);
        for (j = 0; j < i; j++)
            TF_CHECK(n[i] != n[j]);
        if (i >= 2 && n[i] - n[i - 1] != n[1] - n[0])
            steps++;
        if (i >= 1 && (n[i] >> 16) != (n[0] >> 16))
            highs++;
        for (c = 1; c <= CW_ISNS; c++)
            for (t = t0; t <= t1; t++)
                if (n[i] == bare(c, t))
                    bares++;
    }
    TF_CHECK(steps != 0);
    TF_CHECK(highs != 0);
    TF_EQ(bares, 0);
}

int main(int argc, char **argv)
{
    tf_begin("T_CWATT");
    if (argc > 1)
        tfs_set_dir(argv[1]);
    cmem_init();
    test_init();
    test_chksum();
    test_order();
    test_outs();
    test_set();
    test_midnight();
    test_isn();
    return tf_done();
}
