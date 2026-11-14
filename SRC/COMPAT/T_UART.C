/*
 * T_UART.C - unit test for the serial ports of COMPAT.H (CUART.C) under
 * DOS, on COM2 in the chip's loopback and on its line.
 *
 * usage: T_UART
 *
 * The machine must have COM2 with a 16550A, as QEMU's is (the runner
 * gives it one, MAKEFILE's <NAME>_UART), and nothing that answers at
 * the standard base of COM4, 2E8h.  The test reads and writes the
 * chip's and the PIC's registers, the BIOS data area and the interrupt
 * vectors itself, which portable code may not, so it lives in
 * SRC/COMPAT/ and is built for DOS only.
 *
 * Checks:
 * - out-of-range ports, rates and flags give UART_EARG, and a port not
 *   open reads, writes and counts nothing, is idle and reports 0s;
 * - a BIOS data area with no base for COM2, or a base where no chip
 *   answers, gives UART_ENOPORT, a failed far allocation UART_ENOMEM,
 *   each leaving the chip's IER and the vector as they were;
 * - in loopback at 9600 and 38400 baud, with the 16550A FIFOs and
 *   without: a second open gives UART_EBUSY; uart_getinfo reports COM2's
 *   base, IRQ 3 and a 16550A; IER, MCR, the divisor latch and the FIFOs
 *   are as asked, the IRQ unmasked at the PIC and the vector taken (the
 *   same chip checks follow each uart_open on the line and with
 *   UART_F_RTSCTS); uart_modem shows CTS, DSR and DCD up, as the chip
 *   loops RTS, DTR and OUT2 back, CTS down once RTS/CTS has dropped RTS,
 *   and nothing once the port is closed; the first write fills the
 *   transmit ring to UART_TXSZ - 1; the bytes 0..255 four times over,
 *   more than a ring holds, come back whole and in order, the port idle
 *   afterwards and every count 0; uart_close leaves IER 0, the FIFOs
 *   off, and MCR, the PIC's mask bit and the vector as they were found,
 *   the port then idle and reporting 0s.  The runs at 38400 baud mask
 *   IRQ 3 at the PIC, as a board's OUT2 gate keeps it off in loopback,
 *   so that the polling alone moves the bytes; QEMU raises it all the
 *   same;
 * - in loopback with IRQ 3 masked and no FIFO, a byte that reaches RBR
 *   before the one there is read counts one overrun, also when RBR is
 *   then read behind the driver's back, so that its pass finds the
 *   overrun with no byte waiting; a full transmit ring and more sent
 *   unread fill the receive ring, the receiver then held (IER_RX off)
 *   and the chip overrunning, counted as overruns, nothing lost; reading
 *   down to UART_RXLOW + 1 keeps it held, one byte more lets it go
 *   (IER_RX on), the bytes read in order; with the FIFOs on,
 *   uart_rxflush also drops bytes still in the chip's FIFO, one byte
 *   more than that FIFO holds counts one overrun, also when the FIFO is
 *   then emptied behind the driver's back, and uart_close turns the
 *   FIFOs off;
 * - wherever the test reads uart_getinfo's counts of an open port,
 *   uart_rxerrs is their sum;
 * - with UART_F_RTSCTS in loopback, where RTS comes back as CTS, the
 *   receive ring fills to UART_RXHIGH (and the byte or two already in
 *   the chip), RTS and CTS drop and the rest of a full transmit ring
 *   waits; reading down to UART_RXLOW + 1 leaves RTS down, one byte more
 *   raises it, and every byte then arrives in order, none lost; with
 *   IRQ 3 masked and the bytes sent one at a time, RTS is still up at
 *   UART_RXHIGH - 1 bytes and down at UART_RXHIGH, the next byte held;
 * - no ring keeps a byte that has left it: a secret written is found in
 *   the port's block exactly once while it is on its way, and not at
 *   all after uart_read, uart_rxflush or uart_close took it (the test
 *   keeps no copy of it in far memory meanwhile);
 * - a uart_read or uart_write never moves more bytes than it was asked
 *   for, though the handler's end of the ring may move between any two
 *   of its instructions: single-stepped on the line, with that end moved
 *   UT_BUMP bytes on at each step in turn;
 * - on the line, where the handler alone serves the chip, 2 KB are sent
 *   at 38400 baud, the port becomes idle and the PIC holds no IRQ 3 in
 *   service;
 * - a port whose vector was taken over after uart_open is closed with
 *   the vector left as it is; the handler it keeps passes an interrupt
 *   on to the vector uart_open found; the port then opens again.
 *
 * QEMU's chip cannot show three things, which are left unchecked.  It
 * sends a byte the moment it reaches THR, so THR and the shift register
 * are empty together and uart_txidle is never seen waiting for LSR_TEMT.
 * It raises the interrupt again for a source that a pass leaves
 * pending, so a handler making one pass only, not passes until IIR
 * reports none, still empties the transmit ring with no call of the
 * driver's to help it.  And it cannot drive CTS, so the restart of a
 * transmitter stopped by CTS, through the modem-status interrupt, is
 * checked only as IER_MS set in IER.
 *
 * Prints each failure and a summary line; exit status 0 when every check
 * passes, 1 when one fails.
 */
#define COMPAT_INTERNAL
#define COMPAT_UART
#include "COMPAT.H"
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "TESTFW.H"
#include "CUART.H"

#define UT_PORT         2       /* COM2                                 */
#define UT_IRQ          3
#define UT_VEC          0x0B    /* IRQ 3's vector                       */
#define UT_BDA          2       /* COM2's base in the BIOS data area    */
#define UT_NOCHIP       0x2E8   /* COM4's base, no chip there           */
#define UT_N            1024u   /* bytes of the pattern                 */
#define UT_LINE         2048u   /* bytes sent on the line               */
#define UT_WAIT         (5 * TMR_SEC)   /* longest wait for the bytes   */
#define UT_SCAN         (UART_RXSZ + UART_TXSZ + 512u)  /* bytes of a
                                   port's block, its handler and variables
                                   with room to spare, looked through    */
#define UT_SECLEN       16      /* bytes of the secret                  */
#define UT_TAIL         8       /* its last bytes, looked for           */
#define UT_BADFLAG      0x08    /* a flag uart_open does not know       */
#define UT_STEPVEC      1       /* the single-step trap's vector        */
#define UT_RACE         16      /* bytes a raced call asks for          */
#define UT_BUMP         300     /* bytes the handler's end moves at once */
#define UT_STEPS        4000u   /* the latest step g_step moves the end at */
#define UT_FIFO         16      /* bytes of a 16550A's receive FIFO     */

/* The chip's registers and bits that the test reads, its own copies:
   CUART.H defines them too, and a value there that differs from these
   does not compile. */
#define UR_IER          1       /* with LCR_DLAB the divisor's high byte */
#define UR_IIR          2
#define UR_LCR          3
#define UR_MCR          4
#define UR_MSR          6
#define UR_DIVMIN       96      /* divisor of UART_MINBAUD              */
#define IER_RX          0x01
#define IER_TX          0x02
#define IER_LS          0x04
#define IER_MS          0x08
#define IIR_FIFO        0xC0    /* the FIFOs are on                     */
#define LCR_DLAB        0x80
#define MCR_DTR         0x01
#define MCR_RTS         0x02
#define MCR_OUT2        0x08
#define MCR_LOOP        0x10
#define MSR_CTS         0x10

/* The PIC's ports and commands that the test uses. */
#define PIC_CMD         0x20
#define PIC_MASK        0x21
#define PIC_READISR     0x0B    /* OCW3: the next read gives ISR        */
#define PIC_READIRR     0x0A    /* OCW3: the next read gives IRR        */

static const char k_secret[UT_SECLEN + 1] = "PASSWORD=SECRET!";

static cmem_blk *g_src;         /* what the test sends                  */
static cmem_blk *g_dst;         /* what it receives                     */
static u16 g_base;              /* COM2's base                          */
static u8 g_hits;               /* calls of g_stub                      */

/* An interrupt handler for the vector uart_open finds: INC BYTE
   [CS:g_hits], IRET, with g_hits's offset filled in. */
static u8 g_stub[] = { 0x2E, 0xFE, 0x06, 0, 0, 0xCF };
static u8 g_iret = 0xCF;        /* a handler that takes a vector over   */

static u16 g_steps;             /* steps until g_step moves the end     */
static u16 g_seg;               /* the port's block                     */
static u16 g_bump = UT_BUMP;    /* for g_step to read                   */

/* A single-step trap handler that, at the g_steps-th step, moves the
   ring end at the block offset in its last word UT_BUMP bytes on, as
   the interrupt handler would with that many bytes: DEC WORD
   [CS:g_steps], JNZ to the IRET, PUSH AX, PUSH DS, MOV DS,[CS:g_seg],
   MOV AX,[CS:g_bump], ADD [end],AX, POP DS, POP AX, IRET; the offsets
   are filled in. */
static u8 g_step[] = {
    0x2E, 0xFF, 0x0E, 0, 0, 0x75, 0x11, 0x50, 0x1E, 0x2E, 0x8E, 0x1E,
    0, 0, 0x2E, 0xA1, 0, 0, 0x01, 0x06, 0, 0, 0x1F, 0x58, 0xCF
};
#define UT_STEPS_AT     3       /* g_step's offsets to fill in          */
#define UT_SEG_AT       12
#define UT_BUMP_AT      16
#define UT_END_AT       20

/* A far procedure that turns the trap flag over, every register kept:
   PUSH AX, PUSHF, POP AX, XOR AH,1, PUSH AX, POPF, POP AX, RETF. */
static u8 g_trap[] = {
    0x50, 0x9C, 0x58, 0x80, 0xF4, 0x01, 0x50, 0x9D, 0x58, 0xCB
};
/* g_trap, called through this variable: Watcom C 6.5 compiles some
   calls through a far pointer kept on the stack as near calls. */
static cu_fn g_flip;

/* A cmem_fault hook that fails every allocation. */
static int fail_alloc(u16 size)
{
    return size != 0;
}

/* IRQ 3's vector, segment high. */
static u32 vector(void)
{
    u16 seg;
    u16 off;

    dos_getvect(UT_VEC, &seg, &off);
    return ((u32)seg << 16) | off;
}

/* Point IRQ 3's vector at x, segment high. */
static void set_vector(u32 x)
{
    dos_setvect(UT_VEC, (u16)(x >> 16), (u16)(x & 0xFFFFu));
}

/* The vector of the data-segment address p, segment high. */
static u32 near_vector(const void *p)
{
    struct SREGS s;

    segread(&s);
    return ((u32)(u16)s.ds << 16) | (u16)(unsigned)p;
}

/* The PIC's mask bit of IRQ 3. */
static u8 pic_bit(void)
{
    return (u8)(CU_IN(PIC_MASK) & (1 << UT_IRQ));
}

/* Mask IRQ 3 at the PIC, as a board's OUT2 gate keeps it off in
   loopback; some emulators raise it all the same. */
static void gate_irq(void)
{
    CU_OUT(PIC_MASK, CU_IN(PIC_MASK) | (1 << UT_IRQ));
}

/* The divisor latch of COM2, read with interrupts disabled. */
static u16 divisor(void)
{
    u16 fl;
    u16 d;
    u8 lcr;

    fl = IRQ_SAVE();
    IRQ_OFF();
    lcr = (u8)CU_IN(g_base + UR_LCR);
    CU_OUT(g_base + UR_LCR, lcr | LCR_DLAB);
    d = (u16)(CU_IN(g_base) | (CU_IN(g_base + UR_IER) << 8));
    CU_OUT(g_base + UR_LCR, lcr);
    if (fl & IRQ_IF)
        IRQ_ON();
    return d;
}

/* Check the chip as uart_open left it at baud with flags: the receive,
   line-status and, under UART_F_RTSCTS, modem-status interrupts on (the
   transmitter's comes and goes), DTR, RTS and OUT2 up and loopback as
   asked, the divisor of baud, and the FIFOs on unless UART_F_NOFIFO. */
static void check_setup(u16 baud, u8 flags)
{
    TF_EQ(CU_IN(g_base + UR_IER) & ~IER_TX,
          IER_RX | IER_LS | (flags & UART_F_RTSCTS ? IER_MS : 0));
    TF_EQ(CU_IN(g_base + UR_MCR),
          MCR_DTR | MCR_RTS | MCR_OUT2 | (flags & UART_F_LOOP ? MCR_LOOP : 0));
    TF_EQ(divisor(), UR_DIVMIN / (baud / UART_MINBAUD));
    TF_EQ(CU_IN(g_base + UR_IIR) & IIR_FIFO,
          flags & UART_F_NOFIFO ? 0 : IIR_FIFO);
}

/* Fill g_src with the pattern: byte i is i mod 256. */
static void fill_pattern(void)
{
    u8 buf[256];
    u16 i;

    for (i = 0; i < 256; i++)
        buf[i] = (u8)i;
    for (i = 0; i < UT_N; i += 256)
        cmem_put(g_src, i, buf, 256);
}

/* 1 when the n bytes of g_dst from off on are the pattern's from off
   on. */
static int is_pattern(u16 off, u16 n)
{
    u8 b;
    u16 i;

    for (i = off; i < off + n; i++) {
        cmem_get(g_dst, i, &b, 1);
        if (b != (u8)i)
            return 0;
    }
    return 1;
}

/* Run g_src through COM2 in loopback until its first n bytes are back
   in g_dst or UT_WAIT ticks pass, sent bytes from on having been
   written and got bytes from on read already; never more bytes on the
   way than the receive ring holds, as nothing holds the sender back.
   Returns the bytes back. */
static u16 transfer(u16 sent, u16 got, u16 n)
{
    u32 t0;
    u16 k;

    t0 = tmr_ticks();
    while (got < n && tmr_since(t0) < UT_WAIT) {
        k = (u16)(UART_RXSZ - 1 - (sent - got));
        if (k > n - sent)
            k = (u16)(n - sent);
        sent = (u16)(sent + uart_write(UT_PORT, g_src, sent, k));
        got = (u16)(got + uart_read(UT_PORT, g_dst, got, (u16)(n - got)));
    }
    return got;
}

/* The receive ring's count once it has not changed for two ticks. */
static u16 settled(void)
{
    u32 t0;
    u16 n;
    u16 was;

    n = uart_rxcount(UT_PORT);
    do {
        was = n;
        t0 = tmr_ticks();
        while (tmr_since(t0) < 2)
            n = uart_rxcount(UT_PORT);
    } while (n != was);
    return n;
}

/* Wait until COM2's receive ring holds n bytes or UT_WAIT ticks pass;
   1 when it does. */
static int wait_rx(u16 n)
{
    u32 t0;

    t0 = tmr_ticks();
    while (uart_rxcount(UT_PORT) < n && tmr_since(t0) < UT_WAIT)
        ;
    return uart_rxcount(UT_PORT) == n;
}

/* How often the last UT_TAIL bytes of the secret appear in the UT_SCAN
   bytes from seg:0, a port's block. */
static int secrets(u16 seg)
{
    FARP(u8) p;
    const char *t;
    u16 i;
    int j;
    int n;

    p = CFP(u8, seg, 0);
    t = k_secret + UT_SECLEN - UT_TAIL;
    n = 0;
    for (i = 0; i <= UT_SCAN - UT_TAIL; i++) {
        for (j = 0; j < UT_TAIL && p[i + j] == (u8)t[j]; j++)
            ;
        if (j == UT_TAIL)
            n++;
    }
    return n;
}

/* Write the secret to COM2 through g_src, which is zeroed again. */
static void write_secret(void)
{
    cmem_put(g_src, 0, k_secret, UT_SECLEN);
    TF_EQ(uart_write(UT_PORT, g_src, 0, UT_SECLEN), UT_SECLEN);
    cmem_set(g_src, 0, 0, UT_SECLEN);
}

/* Read the secret back from COM2 through g_dst, which is zeroed
   again. */
static void read_secret(void)
{
    char buf[UT_SECLEN];

    TF_EQ(uart_read(UT_PORT, g_dst, 0, UT_SECLEN), UT_SECLEN);
    cmem_get(g_dst, 0, buf, UT_SECLEN);
    cmem_set(g_dst, 0, 0, UT_SECLEN);
    TF_CHECK(memcmp(buf, k_secret, UT_SECLEN) == 0);
    memset(buf, 0, UT_SECLEN);
}

static void test_args(void)
{
    static const u16 bauds[] = { 0, 600, 1000, 3600, 7200, 57600u };
    uart_info i;
    int k;

    TF_EQ(uart_open(0, UART_DEFBAUD, 0), UART_EARG);
    TF_EQ(uart_open(UART_MAXPORT + 1, UART_DEFBAUD, 0), UART_EARG);
    for (k = 0; k < (int)(sizeof bauds / sizeof bauds[0]); k++)
        TF_EQ(uart_open(UT_PORT, bauds[k], 0), UART_EARG);
    TF_EQ(uart_open(UT_PORT, UART_DEFBAUD, UT_BADFLAG), UART_EARG);
    uart_close(UT_PORT);
    uart_close(0);
    TF_EQ(uart_read(UT_PORT, g_dst, 0, 1), 0);
    TF_EQ(uart_write(UT_PORT, g_src, 0, 1), 0);
    TF_EQ(uart_rxcount(UT_PORT), 0);
    TF_EQ(uart_txidle(UT_PORT), 1);
    uart_rxflush(UT_PORT);
    i.base = 1;
    i.chip = 1;
    i.lost = 1;
    uart_getinfo(UART_MAXPORT + 1, &i);
    TF_CHECK(i.base == 0 && i.chip == 0 && i.lost == 0);
}

static void test_noport(void)
{
    FARP(u16) bda;
    u32 vec;
    u8 ier;

    bda = CFP(u16, BDA_SEG, UT_BDA);
    vec = vector();
    ier = (u8)CU_IN(g_base + UR_IER);
    *bda = 0;
    TF_EQ(uart_open(UT_PORT, UART_DEFBAUD, 0), UART_ENOPORT);
    *bda = UT_NOCHIP;
    TF_EQ(uart_open(UT_PORT, UART_DEFBAUD, 0), UART_ENOPORT);
    *bda = g_base;
    cmem_fault = fail_alloc;
    TF_EQ(uart_open(UT_PORT, UART_DEFBAUD, 0), UART_ENOMEM);
    cmem_fault = 0;
    TF_CHECK(vector() == vec);
    TF_EQ(CU_IN(g_base + UR_IER), ier);
}

/* uart_rxerrs of COM2 is the sum of the four counts of i, which is
   uart_getinfo's just before. */
static void errs_sum(const uart_info *i)
{
    TF_EQ(uart_rxerrs(UT_PORT),
          (u16)(i->overrun + i->framing + i->parity + i->lost));
}

/* Open COM2 in loopback at baud with the extra flags, run the pattern
   through it, with IRQ 3 masked when gate is 1, and close it. */
static void test_loop(u16 baud, u8 flags, int gate)
{
    uart_info i;
    u32 vec;
    u8 mcr;
    u8 bit;

    vec = vector();
    mcr = (u8)CU_IN(g_base + UR_MCR);
    bit = pic_bit();
    if (!TF_EQ(uart_open(UT_PORT, baud, (u8)(UART_F_LOOP | flags)),
               UART_OK))
        return;
    TF_EQ(uart_open(UT_PORT, baud, UART_F_LOOP), UART_EBUSY);
    uart_getinfo(UT_PORT, &i);
    TF_EQ(i.base, g_base);
    TF_EQ(i.irq, UT_IRQ);
    TF_EQ(i.chip, UART_T_16550A);
    check_setup(baud, (u8)(UART_F_LOOP | flags));
    TF_EQ(pic_bit(), 0);
    TF_CHECK(vector() != vec);
    TF_EQ(uart_modem(UT_PORT), UART_M_CTS | UART_M_DSR | UART_M_DCD);
    if (gate)
        gate_irq();
    fill_pattern();
    TF_EQ(uart_write(UT_PORT, g_src, 0, UT_N), UART_TXSZ - 1);
    TF_EQ(transfer(UART_TXSZ - 1, 0, UT_N), UT_N);
    TF_CHECK(is_pattern(0, UT_N));
    TF_EQ(uart_rxcount(UT_PORT), 0);
    TF_EQ(uart_txidle(UT_PORT), 1);
    uart_getinfo(UT_PORT, &i);
    TF_CHECK(i.overrun == 0 && i.framing == 0 && i.parity == 0 &&
             i.lost == 0);
    errs_sum(&i);
    uart_close(UT_PORT);
    TF_EQ(CU_IN(g_base + UR_IER), 0);
    TF_EQ(CU_IN(g_base + UR_IIR) & IIR_FIFO, 0);
    TF_EQ(CU_IN(g_base + UR_MCR), mcr);
    TF_EQ(pic_bit(), bit);
    TF_CHECK(vector() == vec);
    TF_EQ(uart_rxcount(UT_PORT), 0);
    TF_EQ(uart_modem(UT_PORT), 0);
    TF_EQ(uart_txidle(UT_PORT), 1);
    uart_getinfo(UT_PORT, &i);
    TF_CHECK(i.base == 0 && i.chip == 0 && i.irq == 0);
    cmem_set(g_dst, 0, 0, UT_N);
}

static void test_rtscts(void)
{
    uart_info i;
    u16 n;
    u16 got;

    if (!TF_EQ(uart_open(UT_PORT, UART_MAXBAUD,
                         UART_F_LOOP | UART_F_RTSCTS), UART_OK))
        return;
    check_setup(UART_MAXBAUD, UART_F_LOOP | UART_F_RTSCTS);
    fill_pattern();
    TF_EQ(uart_write(UT_PORT, g_src, 0, UART_TXSZ - 1), UART_TXSZ - 1);
    n = settled();
    TF_CHECK(n >= UART_RXHIGH && n <= UART_RXHIGH + 2);
    TF_EQ(CU_IN(g_base + UR_MCR) & MCR_RTS, 0);
    TF_EQ(CU_IN(g_base + UR_MSR) & MSR_CTS, 0);
    TF_EQ(uart_modem(UT_PORT), UART_M_DSR | UART_M_DCD);
    TF_EQ(uart_txidle(UT_PORT), 0);
    got = uart_read(UT_PORT, g_dst, 0, (u16)(n - UART_RXLOW - 1));
    TF_EQ(CU_IN(g_base + UR_MCR) & MCR_RTS, 0);
    got = (u16)(got + uart_read(UT_PORT, g_dst, got, 1));
    TF_EQ(got, n - UART_RXLOW);
    TF_EQ(CU_IN(g_base + UR_MCR) & MCR_RTS, MCR_RTS);
    TF_EQ(transfer(UART_TXSZ - 1, got, UART_TXSZ - 1), UART_TXSZ - 1);
    TF_CHECK(is_pattern(0, UART_TXSZ - 1));
    uart_getinfo(UT_PORT, &i);
    TF_EQ(i.lost, 0);
    errs_sum(&i);
    uart_close(UT_PORT);
    cmem_set(g_dst, 0, 0, UT_N);
}

/* RTS/CTS's high-water mark, in loopback with IRQ 3 masked: bytes sent
   one at a time leave RTS up while the receive ring holds
   UART_RXHIGH - 1, and the next drops it and holds the one after. */
static void test_rtsmark(void)
{
    u16 n;

    if (!TF_EQ(uart_open(UT_PORT, UART_MAXBAUD,
                         UART_F_LOOP | UART_F_RTSCTS), UART_OK))
        return;
    gate_irq();
    fill_pattern();
    for (n = 0; n < UART_RXHIGH - 1; n++)
        if (uart_write(UT_PORT, g_src, n, 1) != 1 || !wait_rx((u16)(n + 1)))
            break;
    TF_EQ(n, UART_RXHIGH - 1);
    TF_EQ(CU_IN(g_base + UR_MCR) & MCR_RTS, MCR_RTS);
    TF_EQ(uart_write(UT_PORT, g_src, n, 1), 1);
    TF_CHECK(wait_rx(UART_RXHIGH));
    TF_EQ(CU_IN(g_base + UR_MCR) & MCR_RTS, 0);
    TF_EQ(uart_write(UT_PORT, g_src, n + 1, 1), 1);
    TF_EQ(settled(), UART_RXHIGH);
    uart_close(UT_PORT);
}

/* Wait until COM2 is idle or UT_WAIT ticks pass; 1 when it is. */
static int wait_idle(void)
{
    u32 t0;

    t0 = tmr_ticks();
    while (!uart_txidle(UT_PORT) && tmr_since(t0) < UT_WAIT)
        ;
    return uart_txidle(UT_PORT);
}

/* The counts and the held receiver, in loopback with IRQ 3 masked: a
   byte written to THR behind the driver's back while the one before it
   waits unread in RBR is an overrun, counted also when RBR is then read
   behind its back; bytes sent while the receive ring is full hold the
   receiver and overrun the chip, and none is lost. */
static void test_counts(void)
{
    uart_info i;

    if (!TF_EQ(uart_open(UT_PORT, UART_MAXBAUD,
                         UART_F_LOOP | UART_F_NOFIFO), UART_OK))
        return;
    gate_irq();
    CU_OUT(g_base, 'A');
    tmr_delay(2);
    CU_OUT(g_base, 'B');
    tmr_delay(2);
    TF_EQ(uart_rxcount(UT_PORT), 1);
    uart_getinfo(UT_PORT, &i);
    TF_EQ(i.overrun, 1);
    errs_sum(&i);
    uart_rxflush(UT_PORT);
    CU_OUT(g_base, 'C');
    tmr_delay(2);
    CU_OUT(g_base, 'D');
    tmr_delay(2);
    CU_IN(g_base);
    TF_EQ(uart_rxcount(UT_PORT), 0);
    uart_getinfo(UT_PORT, &i);
    TF_EQ(i.overrun, 2);
    errs_sum(&i);
    fill_pattern();
    TF_EQ(uart_write(UT_PORT, g_src, 0, UART_TXSZ - 1), UART_TXSZ - 1);
    TF_CHECK(wait_idle());
    TF_EQ(uart_write(UT_PORT, g_src, 0, UT_SECLEN), UT_SECLEN);
    TF_CHECK(wait_idle());
    TF_EQ(uart_rxcount(UT_PORT), UART_RXSZ - 1);
    TF_EQ(CU_IN(g_base + UR_IER) & IER_RX, 0);
    uart_getinfo(UT_PORT, &i);
    TF_CHECK(i.overrun > 2 && i.framing == 0 && i.parity == 0 &&
             i.lost == 0);
    errs_sum(&i);
    TF_EQ(uart_read(UT_PORT, g_dst, 0, UART_RXSZ - 1 - UART_RXLOW - 1),
          UART_RXSZ - 1 - UART_RXLOW - 1);
    TF_EQ(CU_IN(g_base + UR_IER) & IER_RX, 0);
    TF_EQ(uart_read(UT_PORT, g_dst, UART_RXSZ - 1 - UART_RXLOW - 1, 1), 1);
    TF_EQ(CU_IN(g_base + UR_IER) & IER_RX, IER_RX);
    TF_CHECK(is_pattern(0, UART_RXSZ - 1 - UART_RXLOW));
    uart_close(UT_PORT);
    cmem_set(g_dst, 0, 0, UT_N);
}

/* The chip's FIFOs, in loopback with IRQ 3 masked and bytes written to
   THR behind the driver's back: uart_rxflush drops those still in the
   receive FIFO; one byte more than it holds is an overrun, counted also
   when the FIFO is then emptied behind the driver's back; uart_close
   turns the FIFOs off. */
static void test_flush(void)
{
    uart_info i;
    int k;

    if (!TF_EQ(uart_open(UT_PORT, UART_MAXBAUD, UART_F_LOOP), UART_OK))
        return;
    gate_irq();
    CU_OUT(g_base, 'A');
    CU_OUT(g_base, 'B');
    CU_OUT(g_base, 'C');
    tmr_delay(2);
    uart_rxflush(UT_PORT);
    TF_EQ(uart_rxcount(UT_PORT), 0);
    for (k = 0; k <= UT_FIFO; k++)
        CU_OUT(g_base, 'E');
    tmr_delay(2);
    for (k = 0; k < UT_FIFO; k++)
        CU_IN(g_base);
    TF_EQ(uart_rxcount(UT_PORT), 0);
    uart_getinfo(UT_PORT, &i);
    TF_EQ(i.overrun, 1);
    errs_sum(&i);
    uart_close(UT_PORT);
    TF_EQ(CU_IN(g_base + UR_IIR) & IIR_FIFO, 0);
}

/* The secret's last bytes in the port's block, with interrupts disabled
   while it waits in the transmit ring, so that the handler cannot take
   it on before it is seen there. */
static void test_wipe(void)
{
    u16 seg;
    u16 fl;

    if (!TF_EQ(uart_open(UT_PORT, UART_DEFBAUD, UART_F_LOOP), UART_OK))
        return;
    seg = (u16)(vector() >> 16);
    fl = IRQ_SAVE();
    IRQ_OFF();
    write_secret();
    TF_EQ(secrets(seg), 1);
    if (fl & IRQ_IF)
        IRQ_ON();
    TF_CHECK(wait_rx(UT_SECLEN));
    TF_EQ(secrets(seg), 1);
    read_secret();
    TF_EQ(secrets(seg), 0);

    write_secret();
    TF_CHECK(wait_rx(UT_SECLEN));
    uart_rxflush(UT_PORT);
    TF_EQ(uart_rxcount(UT_PORT), 0);
    TF_EQ(secrets(seg), 0);

    write_secret();
    TF_CHECK(wait_rx(UT_SECLEN));
    IRQ_OFF();
    write_secret();
    uart_close(UT_PORT);
    if (fl & IRQ_IF)
        IRQ_ON();
    TF_EQ(secrets(seg), 0);
}

/* One uart_read (tx 0) or uart_write (tx 1) of UT_RACE bytes on COM2,
   single-stepped, the receive ring empty or the transmit ring full, and
   g_step moving the handler's end of that ring at the steps-th step.
   Returns the call's result. */
static u16 raced(int tx, u16 steps)
{
    u16 fl;
    u16 n;

    fl = IRQ_SAVE();
    IRQ_OFF();
    *CFP(u16, g_seg, UB_RXHEAD) = 0;
    *CFP(u16, g_seg, UB_RXTAIL) = 0;
    *CFP(u16, g_seg, UB_TXHEAD) = (u16)tx;
    *CFP(u16, g_seg, UB_TXTAIL) = 0;
    if (fl & IRQ_IF)
        IRQ_ON();
    g_steps = steps;
    (*g_flip)();
    n = tx ? uart_write(UT_PORT, g_src, 0, UT_RACE) :
             uart_read(UT_PORT, g_dst, 0, UT_RACE);
    (*g_flip)();
    return n;
}

/* A call never moves more than it was asked for, though the handler's
   end of the ring moves between any two of its instructions: on the
   line with IRQ 3 masked, a single-stepped read of an empty ring and
   write to a full one see UT_BUMP bytes arrive or leave at each step in
   turn, until one step comes after the call has looked. */
static void test_race(void)
{
    u16 seg;
    u16 off;
    u16 k;
    u16 n;
    int tx;

    if (!TF_EQ(uart_open(UT_PORT, UART_MAXBAUD, 0), UART_OK))
        return;
    gate_irq();
    g_seg = (u16)(vector() >> 16);
    put_le16(g_step + UT_STEPS_AT, (unsigned)&g_steps);
    put_le16(g_step + UT_SEG_AT, (unsigned)&g_seg);
    put_le16(g_step + UT_BUMP_AT, (unsigned)&g_bump);
    g_flip = (cu_fn)near_vector(g_trap);
    dos_getvect(UT_STEPVEC, &seg, &off);
    dos_setvect(UT_STEPVEC, (u16)(near_vector(g_step) >> 16),
                (u16)(unsigned)g_step);
    for (tx = 0; tx < 2; tx++) {
        put_le16(g_step + UT_END_AT, tx ? UB_TXHEAD : UB_RXTAIL);
        n = UT_RACE;
        for (k = 1; k <= UT_STEPS && n == UT_RACE; k++)
            n = raced(tx, k);
        TF_EQ(n, 0);
    }
    dos_setvect(UT_STEPVEC, seg, off);
    uart_close(UT_PORT);
    cmem_set(g_dst, 0, 0, UT_RACE);
}

static void test_line(void)
{
    u32 t0;
    u16 sent;
    u16 isr;

    if (!TF_EQ(uart_open(UT_PORT, UART_MAXBAUD, 0), UART_OK))
        return;
    check_setup(UART_MAXBAUD, 0);
    fill_pattern();
    t0 = tmr_ticks();
    sent = 0;
    while ((sent < UT_LINE || !uart_txidle(UT_PORT)) &&
           tmr_since(t0) < UT_WAIT)
        if (sent < UT_LINE)
            sent = (u16)(sent + uart_write(UT_PORT, g_src, sent % UT_N,
                                           (u16)(UT_N - sent % UT_N)));
    TF_EQ(sent, UT_LINE);
    TF_EQ(uart_txidle(UT_PORT), 1);
    CU_OUT(PIC_CMD, PIC_READISR);
    isr = (u16)CU_IN(PIC_CMD);
    CU_OUT(PIC_CMD, PIC_READIRR);
    TF_EQ(isr & (1 << UT_IRQ), 0);
    uart_close(UT_PORT);
}

static void test_takeover(void)
{
    union REGS r;
    u32 old;
    u32 ours;
    u8 mcr;

    old = vector();
    mcr = (u8)CU_IN(g_base + UR_MCR);
    put_le16(g_stub + 3, (unsigned)&g_hits);
    set_vector(near_vector(g_stub));
    if (TF_EQ(uart_open(UT_PORT, UART_DEFBAUD, UART_F_LOOP), UART_OK)) {
        ours = vector();
        set_vector(near_vector(&g_iret));
        uart_close(UT_PORT);
        TF_CHECK(vector() == near_vector(&g_iret));
        TF_EQ(CU_IN(g_base + UR_IER), 0);
        TF_EQ(CU_IN(g_base + UR_MCR), mcr);
        set_vector(ours);
        g_hits = 0;
        int86(UT_VEC, &r, &r);
        TF_EQ(g_hits, 1);
    }
    set_vector(old);
    if (TF_EQ(uart_open(UT_PORT, UART_DEFBAUD, UART_F_LOOP), UART_OK)) {
        TF_CHECK(vector() != old);
        uart_close(UT_PORT);
        TF_CHECK(vector() == old);
    }
}

int main(void)
{
    tf_begin("T_UART");
    cmem_init();
    g_base = *CFP(u16, BDA_SEG, UT_BDA);
    g_src = cmem_alloc(UT_N);
    g_dst = cmem_alloc(UT_N);
    if (!TF_CHECK(g_base != 0 && g_src != NULL && g_dst != NULL))
        return tf_done();
    cmem_set(g_dst, 0, 0, UT_N);
    test_args();
    test_noport();
    test_loop(UART_DEFBAUD, 0, 0);
    test_loop(UART_MAXBAUD, 0, 1);
    test_loop(UART_MAXBAUD, UART_F_NOFIFO, 1);
    test_counts();
    test_flush();
    test_rtscts();
    test_rtsmark();
    test_wipe();
    test_race();
    test_line();
    test_takeover();
    return tf_done();
}
