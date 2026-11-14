/*
 * CUART.C - the serial ports COM1 and COM2 on an 8250, 16450 or 16550
 * family chip.
 *
 * Each open port has one far block, used from its first paragraph on,
 * so that it starts at offset 0 of a segment of its own: the port's
 * variables (cu_vars), the interrupt handler, then the receive and the
 * transmit ring.  The handler is machine code, the bytes of CUARTISR.ASM
 * (make lint checks cuart_isr against it), copied into the block by
 * uart_open; it addresses everything in the block by offsets from its
 * own segment, so nothing is patched at run time.  CUARTISR.ASM tells
 * what the handler does and gives the block's layout, whose offsets are
 * the UB_ constants (the ends of the rings in CUART.H, the rest below).
 *
 * The program fills the transmit ring at its tail and empties the
 * receive ring at its head, the handler the other two ends, each end a
 * word written in one instruction, so the bytes move without a lock.
 * A call sizes its move from one read of the handler's end, which may
 * move between two reads.  What both sides change, MCR's RTS bit, IER_RX
 * of the chip and of the block (the handler clears it to hold the
 * receiver, the program sets it again) and the counts, the program
 * touches with interrupts disabled.  Once the port is open, only the
 * handler's code moves bytes between the rings and the chip and reads
 * LSR, whose read clears its error bits: besides the interrupt, the
 * block's far procedure UB_POLL makes one pass of it, with interrupts
 * disabled.  uart_write calls it to start the transmitter: the handler
 * keeps IER_TX off while the transmit ring is empty, so nothing
 * interrupts until a pass loads the first byte.  uart_txidle calls it
 * for the LSR it leaves in the block, uart_rxflush for the bytes still
 * in the chip, and in loopback every call that polls (COMPAT.H) does.
 *
 * The calls a client never makes, uart_rxcount, uart_rxflush,
 * uart_modem and uart_getinfo, are in CUARTSV.C, so that a client does
 * not link them;
 * CUART.H holds what the two modules share: the chip's registers, the
 * ends of the rings, the port's variables and the calls on a port that
 * both use.  The test T_UART.C reads the chip and the block by the same
 * names.
 */
#define COMPAT_INTERNAL
#define COMPAT_UART
#include "COMPAT.H"

#ifdef COMPAT_HOST

int uart_open(u8 port, u16 baud, u8 flags)
{
    (void)port;
    (void)baud;
    (void)flags;
    return UART_ENOPORT;
}

void uart_close(u8 port)
{
    (void)port;
}

u16 uart_read(u8 port, cmem_blk *b, u16 off, u16 n)
{
    (void)port;
    (void)b;
    (void)off;
    (void)n;
    return 0;
}

u16 uart_write(u8 port, cmem_blk *b, u16 off, u16 n)
{
    (void)port;
    (void)b;
    (void)off;
    (void)n;
    return 0;
}

int uart_txidle(u8 port)
{
    (void)port;
    return 1;
}

u16 uart_rxerrs(u8 port)
{
    (void)port;
    return 0;
}

#else

#include <stdlib.h>
#include <string.h>
#include "CUART.H"

/* The rest of the block, as in CUARTISR.ASM. */
#define UB_OVR          12
#define UB_FRM          14
#define UB_PAR          16
#define UB_OLDVEC       20
#define UB_IER          24
#define UB_FLAGS        25
#define UB_LSR          26
#define UB_ISR          30      /* the handler                          */
#define UB_POLL         50      /* its far procedure                    */
#define UB_RXBUF        265
#define UB_TXBUF        1289
#define UB_SIZE         (UB_TXBUF + UART_TXSZ)
#define UB_ALLOC        (UB_SIZE + 15)  /* UB_SIZE from a paragraph on  */
#define UB_SHUT         0x80    /* UB_FLAGS: the port is closed         */
#define UB_RTSCTS       0x01    /* UART_F_RTSCTS                        */
#define UB_RXMASK       1023    /* UART_RXSZ - 1                        */
#define UB_TXMASK       1023    /* UART_TXSZ - 1                        */
#define UB_RXHIGH       768     /* UART_RXHIGH                          */

#define PIC_CMD         0x20    /* the 8259's ports                     */
#define PIC_MASK        0x21
#define PIC_EOI         0x20    /* non-specific end of interrupt        */
#define CU_IRQVEC       8       /* the vector of IRQ 0                  */

/* Each member is a word, or one of two bytes that make up a word, so
   no compiler pads them, and they lie at the offsets above when
   uart_info and cu_vars (CUART.H) have the sizes of their parts of the
   block (the build fails otherwise). */
extern char cuart_layout[1];
extern char cuart_layout[(sizeof(uart_info) == UB_OLDVEC - UB_BASE &&
                          sizeof(cu_vars) == UB_ISR &&
                          UB_TXBUF - UB_RXBUF == UART_RXSZ &&
                          UB_ALLOC <= CMEM_MAXBLK &&
                          UB_RXMASK + 1 == UART_RXSZ &&
                          UB_TXMASK + 1 == UART_TXSZ &&
                          (UART_RXSZ & UB_RXMASK) == 0 &&
                          (UART_TXSZ & UB_TXMASK) == 0 &&
                          UB_RXHIGH == UART_RXHIGH &&
                          UB_RTSCTS == UART_F_RTSCTS &&
                          (UB_SHUT & (UART_F_RTSCTS | UART_F_LOOP |
                                      UART_F_NOFIFO)) == 0) ? 1 : 2];

/* The bytes of CUARTISR.ASM, one instruction to a row. */
static const u8 cuart_isr[] = {
    0x2E, 0xF6, 0x06, UB_FLAGS & 0xFF, UB_FLAGS >> 8, UB_SHUT,
    0x74, 0x05,
    0x2E, 0xFF, 0x2E, UB_OLDVEC & 0xFF, UB_OLDVEC >> 8,
    0x50,
    0xB0, PIC_EOI,
    0xE6, PIC_CMD,
    0xEB, 0x09,
    0x9C,
    0x0E,
    0xE8, 0x01, 0x00,
    0xCB,
    0x50,
    0xB0, 0x00,
    0x53,
    0x51,
    0x52,
    0x56,
    0x57,
    0x1E,
    0x0E,
    0x1F,
    0x88, 0xC1,
    0x8B, 0x3E, UB_BASE & 0xFF, UB_BASE >> 8,
    0x8D, 0x55, UR_MSR,
    0xEC,
    0x88, 0xC4,
    0x8D, 0x55, UR_LSR,
    0xEC,
    0xA8, LSR_OE,
    0x74, 0x04,
    0xFF, 0x06, UB_OVR & 0xFF, UB_OVR >> 8,
    0xA8, LSR_DR,
    0x74, 0x58,
    0xA8, LSR_PE,
    0x74, 0x04,
    0xFF, 0x06, UB_PAR & 0xFF, UB_PAR >> 8,
    0xA8, LSR_FE,
    0x74, 0x04,
    0xFF, 0x06, UB_FRM & 0xFF, UB_FRM >> 8,
    0xF6, 0x06, UB_IER & 0xFF, UB_IER >> 8, IER_RX,
    0x74, 0x41,
    0x8B, 0x1E, UB_RXTAIL & 0xFF, UB_RXTAIL >> 8,
    0x8D, 0x77, 0x01,
    0x81, 0xE6, UB_RXMASK & 0xFF, UB_RXMASK >> 8,
    0x3B, 0x36, UB_RXHEAD & 0xFF, UB_RXHEAD >> 8,
    0x75, 0x07,
    0x80, 0x26, UB_IER & 0xFF, UB_IER >> 8, ~IER_RX & 0xFF,
    0xEB, 0x29,
    0x89, 0xFA,
    0xEC,
    0x88, 0x87, UB_RXBUF & 0xFF, UB_RXBUF >> 8,
    0x89, 0x36, UB_RXTAIL & 0xFF, UB_RXTAIL >> 8,
    0xF6, 0x06, UB_FLAGS & 0xFF, UB_FLAGS >> 8, UB_RTSCTS,
    0x74, 0xAF,
    0x2B, 0x36, UB_RXHEAD & 0xFF, UB_RXHEAD >> 8,
    0x81, 0xE6, UB_RXMASK & 0xFF, UB_RXMASK >> 8,
    0x81, 0xFE, UB_RXHIGH & 0xFF, UB_RXHIGH >> 8,
    0x72, 0xA1,
    0x8D, 0x55, UR_MCR,
    0xEC,
    0x24, ~MCR_RTS & 0xFF,
    0xEE,
    0xEB, 0x98,
    0xA2, UB_LSR & 0xFF, UB_LSR >> 8,
    0x8B, 0x1E, UB_TXHEAD & 0xFF, UB_TXHEAD >> 8,
    0x3B, 0x1E, UB_TXTAIL & 0xFF, UB_TXTAIL >> 8,
    0x74, 0x26,
    0xF6, 0x06, UB_FLAGS & 0xFF, UB_FLAGS >> 8, UB_RTSCTS,
    0x74, 0x05,
    0xF6, 0xC4, MSR_CTS,
    0x74, 0x1A,
    0xA8, LSR_THRE,
    0x74, 0x12,
    0xB0, 0x00,
    0x86, 0x87, UB_TXBUF & 0xFF, UB_TXBUF >> 8,
    0x89, 0xFA,
    0xEE,
    0x43,
    0x81, 0xE3, UB_TXMASK & 0xFF, UB_TXMASK >> 8,
    0x89, 0x1E, UB_TXHEAD & 0xFF, UB_TXHEAD >> 8,
    0xB0, IER_TX,
    0xEB, 0x02,
    0xB0, 0x00,
    0x0A, 0x06, UB_IER & 0xFF, UB_IER >> 8,
    0x8D, 0x55, UR_IER,
    0xEE,
    0x84, 0xC9,
    0x74, 0x09,
    0x42,
    0xEC,
    0xA8, IIR_NONE,
    0x75, 0x03,
    0xE9, 0x48, 0xFF,
    0x1F,
    0x5F,
    0x5E,
    0x5A,
    0x59,
    0x5B,
    0x58,
    0xCF
};

extern char cuart_isr_fits[1];
extern char cuart_isr_fits[(UB_ISR + sizeof cuart_isr == UB_RXBUF) ? 1 : 2];

/* COM1 interrupts on IRQ 4, COM2 on IRQ 3. */
static const u8 cuart_irqs[UART_MAXPORT] = { 4, 3 };

static cu_port cuart_ports[UART_MAXPORT];
static int cuart_exitset;       /* 1 once close_all is registered       */

cu_port *cuart_port(u8 port)
{
    if (port < 1 || port > UART_MAXPORT || cuart_ports[port - 1].b == NULL)
        return NULL;
    return &cuart_ports[port - 1];
}

void cuart_serve(const cu_port *p)
{
    u16 fl;

    fl = IRQ_SAVE();
    IRQ_OFF();
    (*(cu_fn)(((u32)p->seg << 16) | UB_POLL))();
    if (fl & IRQ_IF)
        IRQ_ON();
}

cu_port *cuart_polled(u8 port)
{
    cu_port *p;

    p = cuart_port(port);
    if (p != NULL && (CU_VARS(p)->flags & UART_F_LOOP))
        cuart_serve(p);
    return p;
}

/* Move n bytes between b at off and p's transmit ring (tx 1, into it)
   or receive ring (tx 0, out of it, zeroing them there; b NULL drops
   them), from ring index i on.  Returns the index after them. */
static u16 ring_move(const cu_port *p, int tx, u16 i, cmem_blk *b, u16 off,
                     u16 n)
{
    u16 at;
    u16 size;
    u16 k;

    at = (u16)(p->skew + (tx ? UB_TXBUF : UB_RXBUF));
    size = tx ? UART_TXSZ : UART_RXSZ;
    while (n > 0) {
        k = (u16)(size - i);
        if (k > n)
            k = n;
        if (tx) {
            cmem_copy(p->b, (u16)(at + i), b, off, k);
        } else {
            if (b != NULL)
                cmem_copy(b, off, p->b, (u16)(at + i), k);
            cmem_set(p->b, (u16)(at + i), 0, k);
        }
        i = (u16)((i + k) & (size - 1));
        off = (u16)(off + k);
        n = (u16)(n - k);
    }
    return i;
}

u16 cuart_take(const cu_port *p, cmem_blk *b, u16 off, u16 n)
{
    FARP(volatile cu_vars) v;
    u16 held;
    u16 head;
    u16 fl;
    u16 r;
    u8 m;

    v = CU_VARS(p);
    held = CU_RXHELD(v);
    if (n > held)
        n = held;
    head = ring_move(p, 0, v->rxhead, b, off, n);
    fl = IRQ_SAVE();
    IRQ_OFF();
    v->rxhead = head;
    if (CU_RXHELD(v) <= UART_RXLOW && !(v->ier & IER_RX)) {
        v->ier = (u8)(v->ier | IER_RX);
        r = (u16)(v->info.base + UR_IER);
        CU_OUT(r, CU_IN(r) | IER_RX);
    }
    if ((v->flags & UART_F_RTSCTS) && CU_RXHELD(v) <= UART_RXLOW) {
        r = (u16)(v->info.base + UR_MCR);
        m = (u8)CU_IN(r);
        if (!(m & MCR_RTS))
            CU_OUT(r, m | MCR_RTS);
    }
    if (fl & IRQ_IF)
        IRQ_ON();
    return n;
}

/* 1 when a chip answers at base: the bits of IIR and IER that every
   chip of the family reads as 0 are 0. */
static int answers(u16 base)
{
    return (CU_IN(base + UR_IIR) & IIR_ZERO) == 0 &&
           (CU_IN(base + UR_IER) & IER_ZERO) == 0;
}

/* The UART_T_ chip at base: an 8250 has no scratch register, and the
   FIFOs, when turned on, show in IIR as both bits of IIR_FIFO on a
   16550A, one on a 16550 and none on a 16450.  The FIFOs are left
   off. */
static u8 chip_type(u16 base)
{
    u8 fifo;

    CU_OUT(base + UR_SCR, 0x55);
    if (CU_IN(base + UR_SCR) != 0x55)
        return UART_T_8250;
    CU_OUT(base + UR_SCR, 0xAA);
    if (CU_IN(base + UR_SCR) != 0xAA)
        return UART_T_8250;
    CU_OUT(base + UR_IIR, FCR_ON);
    fifo = (u8)(CU_IN(base + UR_IIR) & IIR_FIFO);
    CU_OUT(base + UR_IIR, 0);
    if (fifo == IIR_FIFO)
        return UART_T_16550A;
    return fifo != 0 ? UART_T_16550 : UART_T_16450;
}

/* Close every port, at the program's exit. */
static void close_all(void)
{
    u8 port;

    for (port = 1; port <= UART_MAXPORT; port++)
        uart_close(port);
}

int uart_open(u8 port, u16 baud, u8 flags)
{
    cu_port *p;
    cu_vars v;
    FARP(u8) a;
    cmem_blk *b;
    u16 base;
    u16 fl;
    u8 bit;

    if (port < 1 || port > UART_MAXPORT || !UART_BAUDOK(baud) ||
        (flags & ~(UART_F_RTSCTS | UART_F_LOOP | UART_F_NOFIFO)) != 0)
        return UART_EARG;
    p = &cuart_ports[port - 1];
    if (p->b != NULL)
        return UART_EBUSY;
    base = *CFP(u16, BDA_SEG, (port - 1) * 2);
    if (base == 0 || !answers(base))
        return UART_ENOPORT;
    b = cmem_alloc(UB_ALLOC);
    if (b == NULL)
        return UART_ENOMEM;
    memset(&v, 0, sizeof v);
    v.info.base = base;
    v.info.irq = cuart_irqs[port - 1];
    v.info.chip = chip_type(base);
    v.ier = (u8)(IER_RX | IER_LS | (flags & UART_F_RTSCTS ? IER_MS : 0));
    v.flags = flags;
    bit = (u8)(1 << v.info.irq);

    CU_OUT(base + UR_IER, 0);
    v.mcr = (u8)CU_IN(base + UR_MCR);
    CU_OUT(base + UR_LCR, LCR_DLAB);
    CU_OUT(base + UR_DATA, UR_DIVMIN / (baud / UART_MINBAUD));
    CU_OUT(base + UR_IER, 0);
    CU_OUT(base + UR_LCR, LCR_8N1);
    if (v.info.chip == UART_T_16550A && !(flags & UART_F_NOFIFO))
        CU_OUT(base + UR_IIR, FCR_ON | FCR_CLEAR);
    CU_OUT(base + UR_MCR, MCR_DTR | MCR_RTS | MCR_OUT2 |
                          (flags & UART_F_LOOP ? MCR_LOOP : 0));
    CU_IN(base + UR_LSR);
    CU_IN(base + UR_DATA);
    CU_IN(base + UR_MSR);
    v.mask = (u8)(CU_IN(PIC_MASK) & bit);
    dos_getvect((u8)(CU_IRQVEC + v.info.irq), &v.oldseg, &v.oldoff);

    a = cmem_ptr(b);
    p->b = b;
    p->skew = (u16)((16 - (FP_OFF(a) & 15)) & 15);
    p->seg = (u16)(FP_SEG(a) + (FP_OFF(a) >> 4) + (p->skew != 0));
    cmem_set(b, 0, 0, UB_ALLOC);
    cmem_put(b, p->skew, &v, sizeof v);
    cmem_put(b, (u16)(p->skew + UB_ISR), cuart_isr, sizeof cuart_isr);
    dos_setvect((u8)(CU_IRQVEC + v.info.irq), p->seg, UB_ISR);
    fl = IRQ_SAVE();
    IRQ_OFF();
    CU_OUT(base + UR_IER, v.ier);
    CU_OUT(PIC_MASK, CU_IN(PIC_MASK) & ~bit);
    if (fl & IRQ_IF)
        IRQ_ON();
    if (!cuart_exitset && atexit(close_all) == 0)
        cuart_exitset = 1;
    return UART_OK;
}

void uart_close(u8 port)
{
    cu_port *p;
    cu_vars v;
    u16 seg;
    u16 off;
    u16 fl;

    p = cuart_port(port);
    if (p == NULL)
        return;
    fl = IRQ_SAVE();
    IRQ_OFF();
    CU_VARS(p)->flags |= UB_SHUT;
    cmem_get(p->b, p->skew, &v, sizeof v);
    CU_OUT(v.info.base + UR_IER, 0);
    CU_OUT(PIC_MASK, (CU_IN(PIC_MASK) & ~(1 << v.info.irq)) | v.mask);
    if (fl & IRQ_IF)
        IRQ_ON();
    if (v.info.chip == UART_T_16550A && !(v.flags & UART_F_NOFIFO))
        CU_OUT(v.info.base + UR_IIR, 0);
    CU_OUT(v.info.base + UR_MCR, v.mcr);
    cmem_set(p->b, (u16)(p->skew + UB_RXBUF), 0, UART_RXSZ + UART_TXSZ);
    dos_getvect((u8)(CU_IRQVEC + v.info.irq), &seg, &off);
    if (seg == p->seg && off == UB_ISR) {
        dos_setvect((u8)(CU_IRQVEC + v.info.irq), v.oldseg, v.oldoff);
        cmem_free(p->b);
    }
    p->b = NULL;
}

u16 uart_read(u8 port, cmem_blk *b, u16 off, u16 n)
{
    cu_port *p;

    p = cuart_polled(port);
    return p != NULL ? cuart_take(p, b, off, n) : 0;
}

u16 uart_write(u8 port, cmem_blk *b, u16 off, u16 n)
{
    cu_port *p;
    FARP(volatile cu_vars) v;
    u16 room;

    p = cuart_port(port);
    if (p == NULL)
        return 0;
    v = CU_VARS(p);
    room = (u16)((v->txhead - v->txtail - 1) & (UART_TXSZ - 1));
    if (n > room)
        n = room;
    v->txtail = ring_move(p, 1, v->txtail, b, off, n);
    cuart_serve(p);
    return n;
}

int uart_txidle(u8 port)
{
    cu_port *p;
    FARP(volatile cu_vars) v;

    p = cuart_port(port);
    if (p == NULL)
        return 1;
    cuart_serve(p);
    v = CU_VARS(p);
    return v->txhead == v->txtail && (v->lsr & LSR_TEMT) != 0;
}

/* The handler never counts a byte lost to a full ring, so the sum
   leaves that count out.  Each count is read in one instruction, which
   the handler cannot split. */
u16 uart_rxerrs(u8 port)
{
    cu_port *p;
    FARP(volatile cu_vars) v;

    p = cuart_port(port);
    if (p == NULL)
        return 0;
    v = CU_VARS(p);
    return (u16)(v->info.overrun + v->info.framing + v->info.parity);
}

#endif
