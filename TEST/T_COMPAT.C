/*
 * T_COMPAT.C - unit test for the portable parts of COMPAT.H.
 *
 * usage: T_COMPAT
 *
 * Checks:
 * - u8, u16, i16, u32 and i32 are 1, 2, 2, 4 and 4 bytes wide;
 * - put_le16 and put_le32 store the low byte first, and get_le16 and
 *   get_le32 read the bytes back in that order;
 * - each boundary value from 0 to 0xFFFFFFFF, passed as a u16 and as a
 *   u32 variable, as an int or unsigned variable and as an int,
 *   unsigned or unsigned long constant, is stored low byte first and
 *   read back unchanged;
 * - nb_present reports no NetBIOS (the host build has none), and
 *   nb_add_name and nb_listen_wait then answer NB_RC_NOT_PRESENT;
 * - nba_open refuses a set of 0 or more than NBA_MAX NCBs with
 *   CF_EINVAL and a failed allocation with CF_ENOMEM, leaving NULL;
 * - the nba_ posts answer NB_RC_ILLCMD for a handle out of range, a
 *   length of 0 or above NB_MSGMAX and a buffer outside its block, and
 *   NB_RC_NOT_PRESENT otherwise; an NCB never taken by the provider
 *   polls NB_OK, has no session, length or caller's name and cancels
 *   with NB_OK; nba_poll and nba_cancel answer NB_RC_ILLCMD for a
 *   handle out of range, and nba_lsn, nba_len and nba_callname give 0,
 *   0 and "" for one; a set opened on reused memory that held pending
 *   completion bytes has every NCB free; nba_close frees the set and
 *   keeps nothing;
 * - uart_open reports no port (the host build has none), and every
 *   other uart_ call, uart_rxerrs included, answers as for a port not
 *   open.
 *
 * Prints each failure, naming the value it was checking, and a summary
 * line; exit status 0 when every check passes, 1 when one fails.
 */
#define COMPAT_NETBIOS
#define COMPAT_UART
#include <stdio.h>
#include "TESTFW.H"

#define NCB_BYTES       64      /* bytes in one NCB, as CNETB.C lays a
                                   set out                              */

/* Check that b holds the low 16 bits of v, low byte first. */
static void check16(const char *what, const u8 *b, u32 v)
{
    int ok;

    ok = TF_EQ(b[0], v & 0xFFu);
    ok &= TF_EQ(b[1], (v >> 8) & 0xFFu);
    ok &= TF_EQ(get_le16(b), v & 0xFFFFu);
    if (!ok)
        printf("  in %s, value %lX\n", what, (unsigned long)v);
}

/* Check that b holds v, low byte first. */
static void check32(const char *what, const u8 *b, u32 v)
{
    int ok;

    ok = TF_EQ(b[0], v & 0xFFu);
    ok &= TF_EQ(b[1], (v >> 8) & 0xFFu);
    ok &= TF_EQ(b[2], (v >> 16) & 0xFFu);
    ok &= TF_EQ(b[3], (v >> 24) & 0xFFu);
    ok &= TF_CHECK(get_le32(b) == v);
    if (!ok)
        printf("  in %s, value %lX\n", what, (unsigned long)v);
}

/* Store a constant c with each macro and check the bytes. */
#define CHK16(c) { put_le16(buf, (c)); check16(#c, buf, (u32)(c)); }
#define CHK32(c) { put_le32(buf, (c)); check32(#c, buf, (u32)(c)); }

static const u32 values[] = {
    0x0UL, 0x1UL, 0x7FUL, 0x80UL, 0xFFUL, 0x100UL, 0x7FFFUL, 0x8000UL,
    0xFFFFUL, 0x10000UL, 0x7FFFFFFFUL, 0x80000000UL, 0xFFFFFFFFUL
};

static void test_sizes(void)
{
    TF_EQ(sizeof(u8), 1);
    TF_EQ(sizeof(u16), 2);
    TF_EQ(sizeof(i16), 2);
    TF_EQ(sizeof(u32), 4);
    TF_EQ(sizeof(i32), 4);
}

static void test_byte_order(void)
{
    u8 buf[4];

    put_le16(buf, 0x1234u);
    TF_EQ(buf[0], 0x34);
    TF_EQ(buf[1], 0x12);
    put_le32(buf, 0x12345678UL);
    TF_EQ(buf[0], 0x78);
    TF_EQ(buf[1], 0x56);
    TF_EQ(buf[2], 0x34);
    TF_EQ(buf[3], 0x12);
    buf[0] = 0xEF;
    buf[1] = 0xBE;
    buf[2] = 0xAD;
    buf[3] = 0xDE;
    TF_EQ(get_le16(buf), 0xBEEFu);
    TF_CHECK(get_le32(buf) == 0xDEADBEEFUL);
}

static void test_values(void)
{
    u8 buf[4];
    int i;
    u32 v32;
    u16 v16;

    for (i = 0; i < (int)(sizeof values / sizeof values[0]); i++) {
        v32 = values[i];
        v16 = (u16)(v32 & 0xFFFFu);
        put_le16(buf, v16);
        check16("u16", buf, (u32)v16);
        put_le32(buf, v32);
        check32("u32", buf, v32);
    }
}

/* The variables are of the plain C types int and unsigned on purpose:
   they are the argument types under test, whose width differs between
   the 16-bit and the host builds. */
static void test_int_vars(void)
{
    u8 buf[4];
    int iv;
    unsigned uv;

    iv = 0x7FFF;
    put_le16(buf, iv);
    check16("int le16", buf, 0x7FFFUL);
    put_le32(buf, iv);
    check32("int le32", buf, 0x7FFFUL);
    iv = 0x80;
    put_le16(buf, iv);
    check16("int le16", buf, 0x80UL);
    put_le32(buf, iv);
    check32("int le32", buf, 0x80UL);
    uv = 0xFFFFu;
    put_le16(buf, uv);
    check16("unsigned le16", buf, 0xFFFFUL);
    put_le32(buf, uv);
    check32("unsigned le32", buf, 0xFFFFUL);
}

static void test_constants(void)
{
    u8 buf[4];

    CHK16(0)
    CHK16(1)
    CHK16(0x7F)
    CHK16(0x80)
    CHK16(0xFF)
    CHK16(0x100)
    CHK16(0x7FFF)
    CHK16(0x8000u)
    CHK16(0xFFFFu)
    CHK16(0x7Fu)
    CHK16(0x100u)

    CHK32(0)
    CHK32(1)
    CHK32(0x7F)
    CHK32(0x80)
    CHK32(0xFF)
    CHK32(0x100)
    CHK32(0x7FFF)
    CHK32(0x8000u)
    CHK32(0xFFFFu)
    CHK32(0x10000UL)
    CHK32(0x7FFFFFFFUL)
    CHK32(0x80000000UL)
    CHK32(0xFFFFFFFFUL)
}

static void test_netbios(void)
{
    u8 num;
    u8 lsn;

    num = 1;
    lsn = 1;
    TF_EQ(nb_present(), 0);
    TF_EQ(nb_add_name("T_COMPAT", &num), NB_RC_NOT_PRESENT);
    TF_EQ(nb_listen_wait("T_COMPAT", "*", &lsn, 0, 0, 1),
          NB_RC_NOT_PRESENT);
    TF_EQ(lsn, 0);
}

/* A cmem_fault hook that fails every allocation. */
static int fail_alloc(u16 size)
{
    return size != 0;
}

static void test_nba_open(void)
{
    nba *s;
    nba *t;

    if (!TF_EQ(nba_open(&t, 1), 0))
        return;
    s = t;
    TF_EQ(nba_open(&s, 0), CF_EINVAL);
    TF_CHECK(s == NULL);
    s = t;
    TF_EQ(nba_open(&s, NBA_MAX + 1), CF_EINVAL);
    TF_CHECK(s == NULL);
    s = t;
    cmem_fault = fail_alloc;
    TF_EQ(nba_open(&s, 1), CF_ENOMEM);
    cmem_fault = 0;
    TF_CHECK(s == NULL);
    TF_EQ(nba_close(t), 0);
    TF_EQ(nba_close(NULL), 0);
}

/* A set whose far block reuses memory that held 0xFF bytes (pending
   completion bytes) still has every NCB free. */
static void test_nba_dirty(void)
{
    cmem_blk *b;
    nba *s;
    u16 h;

    b = cmem_alloc(NBA_MAX * NCB_BYTES);
    if (!TF_CHECK(b != NULL))
        return;
    cmem_set(b, 0, 0xFF, NBA_MAX * NCB_BYTES);
    cmem_free(b);
    if (!TF_EQ(nba_open(&s, NBA_MAX), 0))
        return;
    for (h = 0; h < NBA_MAX; h++)
        TF_EQ(nba_poll(s, h), NB_OK);
    TF_EQ(nba_close(s), 0);
}

static void test_nba_posts(void)
{
    nba *s;
    cmem_blk *b;
    char name[NB_NAMELEN];

    b = cmem_alloc(NB_MSGMAX + 1);
    if (!TF_CHECK(b != NULL) || !TF_EQ(nba_open(&s, NBA_MAX), 0)) {
        cmem_free(b);
        return;
    }
    TF_EQ(nba_listen(s, NBA_MAX, "T_COMPAT", "*", 0, 0), NB_RC_ILLCMD);
    TF_EQ(nba_listen(s, 0, "T_COMPAT", "*", 0, 0), NB_RC_NOT_PRESENT);
    TF_EQ(nba_call(s, NBA_MAX, "T_COMPAT", "INVSERV", 0, 0), NB_RC_ILLCMD);
    TF_EQ(nba_call(s, 1, "T_COMPAT", "INVSERV", 0, 0), NB_RC_NOT_PRESENT);
    TF_EQ(nba_recv(s, NBA_MAX, 1, b, 0, 1), NB_RC_ILLCMD);
    TF_EQ(nba_recv(s, 0, 1, b, 0, 0), NB_RC_ILLCMD);
    TF_EQ(nba_recv(s, 0, 1, b, 0, NB_MSGMAX + 1), NB_RC_ILLCMD);
    TF_EQ(nba_recv(s, 0, 1, b, 2, NB_MSGMAX), NB_RC_ILLCMD);
    TF_EQ(nba_recv(s, 0, 1, b, 1, NB_MSGMAX), NB_RC_NOT_PRESENT);
    TF_EQ(nba_send(s, NBA_MAX, 1, b, 0, 1), NB_RC_ILLCMD);
    TF_EQ(nba_send(s, 0, 1, b, 0, 0), NB_RC_ILLCMD);
    TF_EQ(nba_send(s, 0, 1, b, 0, NB_MSGMAX + 1), NB_RC_ILLCMD);
    TF_EQ(nba_send(s, 0, 1, b, 2, NB_MSGMAX), NB_RC_ILLCMD);
    TF_EQ(nba_send(s, NBA_MAX - 1, 1, b, 0, 1), NB_RC_NOT_PRESENT);

    TF_EQ(nba_poll(s, 0), NB_OK);
    TF_EQ(nba_poll(s, NBA_MAX - 1), NB_OK);
    TF_EQ(nba_poll(s, NBA_MAX), NB_RC_ILLCMD);
    TF_EQ(nba_lsn(s, 0), 0);
    TF_EQ(nba_len(s, 0), 0);
    name[0] = 'X';
    nba_callname(s, 0, name);
    TF_STREQ(name, "");
    TF_EQ(nba_lsn(s, NBA_MAX), 0);
    TF_EQ(nba_len(s, NBA_MAX), 0);
    name[0] = 'X';
    nba_callname(s, NBA_MAX, name);
    TF_STREQ(name, "");
    TF_EQ(nba_cancel(s, 0), NB_OK);
    TF_EQ(nba_cancel(s, NBA_MAX), NB_RC_ILLCMD);
    TF_EQ(nba_close(s), 0);
    TF_EQ(nba_stuck(), 0);
    cmem_free(b);
}

static void test_uart(void)
{
    uart_info i;
    cmem_blk *b;

    TF_EQ(uart_open(1, UART_DEFBAUD, 0), UART_ENOPORT);
    TF_EQ(uart_open(UART_MAXPORT, UART_MAXBAUD, UART_F_LOOP),
          UART_ENOPORT);
    b = cmem_alloc(1);
    if (!TF_CHECK(b != NULL))
        return;
    TF_EQ(uart_read(1, b, 0, 1), 0);
    TF_EQ(uart_write(1, b, 0, 1), 0);
    cmem_free(b);
    TF_EQ(uart_rxcount(1), 0);
    TF_EQ(uart_rxerrs(1), 0);
    TF_EQ(uart_txidle(1), 1);
    uart_rxflush(1);
    uart_close(1);
    i.base = 1;
    i.irq = 1;
    i.lost = 1;
    uart_getinfo(1, &i);
    TF_CHECK(i.base == 0 && i.irq == 0 && i.lost == 0);
}

int main(void)
{
    tf_begin("T_COMPAT");
    test_sizes();
    test_byte_order();
    test_values();
    test_int_vars();
    test_constants();
    test_netbios();
    test_nba_open();
    test_nba_dirty();
    test_nba_posts();
    test_uart();
    return tf_done();
}
