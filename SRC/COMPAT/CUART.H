/*
 * CUART.H - the internals the serial driver's two modules share: CUART.C,
 * the calls every program on a serial line makes, and CUARTSV.C, those
 * a client never makes (uart_rxcount, uart_rxflush, uart_modem,
 * uart_getinfo), which a client does not link.  DOS builds only, included
 * after COMPAT.H with COMPAT_INTERNAL and COMPAT_UART defined; CUART.C
 * tells how the driver works and gives its block's layout.  A Windows
 * program, whose ports belong to the comm driver, links CCOMM.C instead.
 */
#ifndef CUART_H
#define CUART_H

#ifdef COMPAT_WIN16
#error The DOS serial driver is not for Windows: link CCOMM.C
#endif
#include <dos.h>
#ifdef COMPAT_OW
#include <conio.h>
#define CU_IN(port)     inp(port)
#define CU_OUT(port, v) outp((port), (v))
typedef void (__far *cu_fn)(void);
#endif
#ifdef COMPAT_WC6
#include <conio.h>
#define CU_IN(port)     inp(port)
#define CU_OUT(port, v) outp((port), (v))
typedef void (far *cu_fn)(void);
#endif
#ifdef COMPAT_TC
#define CU_IN(port)     inportb(port)
#define CU_OUT(port, v) outportb((port), (u8)(v))
typedef void (far *cu_fn)(void);
#endif

/* The chip's registers, from its base, and their bits. */
#define UR_DATA         0       /* RBR, THR; with LCR_DLAB the divisor's
                                   low byte                             */
#define UR_IER          1       /* with LCR_DLAB the divisor's high byte */
#define UR_IIR          2       /* written: FCR                         */
#define UR_LCR          3
#define UR_MCR          4
#define UR_LSR          5
#define UR_MSR          6
#define UR_SCR          7       /* scratch, not on an 8250              */
#define UR_DIVMIN       96      /* divisor of UART_MINBAUD: 1.8432 MHz
                                   / 16 / 1200                          */
#define IER_RX          0x01
#define IER_TX          0x02
#define IER_LS          0x04    /* line status: errors                  */
#define IER_MS          0x08    /* modem status: CTS                    */
#define IER_ZERO        0xF0    /* bits that read 0                     */
#define IIR_NONE        0x01    /* no interrupt pending                 */
#define IIR_ZERO        0x30
#define IIR_FIFO        0xC0    /* FIFOs on: both on a 16550A only      */
#define FCR_ON          0x01
#define FCR_CLEAR       0x06    /* empty both FIFOs                     */
#define LCR_8N1         0x03
#define LCR_DLAB        0x80
#define MCR_DTR         0x01
#define MCR_RTS         0x02
#define MCR_OUT2        0x08    /* gates the IRQ line on PC boards      */
#define MCR_LOOP        0x10
#define LSR_DR          0x01
#define LSR_OE          0x02
#define LSR_PE          0x04
#define LSR_FE          0x08
#define LSR_THRE        0x20
#define LSR_TEMT        0x40    /* THR and the shift register empty     */
#define MSR_CTS         0x10

/* The ends of the rings, the first words of a port's block, and its
   uart_info after them (CUARTISR.ASM). */
#define UB_RXHEAD       0
#define UB_RXTAIL       2
#define UB_TXHEAD       4
#define UB_TXTAIL       6
#define UB_BASE         8       /* uart_info, from its base on          */

/* The port's variables, at offset 0 of its block (CUART.C checks that
   the layout matches the block's). */
typedef struct {
    u16 rxhead;                 /* the next byte the program takes      */
    u16 rxtail;                 /* the next byte the handler fills      */
    u16 txhead;                 /* the next byte the handler sends      */
    u16 txtail;                 /* the next byte the program fills      */
    uart_info info;             /* base, IRQ, chip and counts           */
    u16 oldoff;                 /* the vector uart_open found           */
    u16 oldseg;
    u8 ier;                     /* IER without IER_TX, and without
                                   IER_RX while the receiver is held    */
    u8 flags;                   /* uart_open's, and UB_SHUT             */
    u16 lsr;                    /* LSR as the last pass read it         */
    u8 mcr;                     /* MCR as uart_open found it            */
    u8 mask;                    /* the PIC's mask bit of the IRQ as
                                   uart_open found it                   */
} cu_vars;

/* A port as the program keeps it: its far block and where in the block
   the layout starts. */
typedef struct {
    cmem_blk *b;                /* the far block, NULL while closed     */
    u16 seg;                    /* the segment the block's layout uses  */
    u16 skew;                   /* the offset in b of that segment's 0  */
} cu_port;

/* The variables of the open port p. */
#define CU_VARS(p)      CFP(volatile cu_vars, (p)->seg, 0)

/* The bytes waiting in the receive ring of the cu_vars v; v is evaluated
   twice. */
#define CU_RXHELD(v)    ((u16)(((v)->rxtail - (v)->rxhead) & \
                               (UART_RXSZ - 1)))

/* The open port numbered port, or NULL; cuart_polled serves it first
   when it is in loopback. */
cu_port *cuart_port(u8 port);
cu_port *cuart_polled(u8 port);

/* Serve p's chip as its handler does, through the block's far
   procedure with interrupts disabled. */
void cuart_serve(const cu_port *p);

/* Take up to n bytes out of p's receive ring into b at off (b NULL drops
   them); then, once UART_RXLOW or fewer are left, let the handler take
   bytes from the chip again (IER_RX) if it held them, and raise RTS
   under UART_F_RTSCTS.  Returns the bytes taken. */
u16 cuart_take(const cu_port *p, cmem_blk *b, u16 off, u16 n);

#endif
