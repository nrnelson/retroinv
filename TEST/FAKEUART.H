/*
 * FAKEUART.H - simulated serial ports for the tests (FAKEUART.C).
 *
 * FAKEUART.C takes the place of SRC/COMPAT/CUART.C in a test's link: it
 * defines every uart_ function of COMPAT.H, with the results given
 * there, over ports simulated in portable C, so a client, a server, or
 * both in one program, run over serial lines on any build.  It reaches
 * the callers' blocks only through the cmem_ calls.  The fu_ functions
 * below set it up and look inside it.  A file that includes this
 * header defines COMPAT_UART first.
 *
 * Ports.  After fu_reset no port exists; fu_port makes one, with the
 * chip uart_getinfo then reports beside the standard base and IRQ of
 * its number (COM1 3F8h and IRQ 4, COM2 2F8h and IRQ 3).  uart_open
 * takes the port's two rings from one far block (cmem_alloc), so
 * cmem_fault makes it give UART_ENOMEM; it refuses the arguments and a
 * port open already as COMPAT.H says.  The rings behave as CUART.C's:
 * a ring of n bytes holds n - 1, every byte leaving one is zeroed in it
 * (fu_ringblk lets a test see that, the block starting zeroed), and
 * uart_close drops what is left.
 *
 * The line.  A port's line leads to the test, which plays the remote
 * station with fu_peer_send and fu_peer_recv, unless fu_pair connects
 * it to another port; while the port is open with UART_F_LOOP it leads
 * back to the port itself and nothing comes in from the far end.  The
 * line moves bytes in steps: one in every call of uart_read,
 * uart_write, uart_rxcount and uart_txidle, and of fu_peer_send and
 * fu_peer_recv, made by uart_write and fu_peer_send after their bytes
 * are queued and by the others before they answer.
 * A step moves, port by port in number order, up to the port's rate of
 * bytes (fu_rate) out of its transmit ring and up to as many from the
 * test's queue towards it.  A port open with UART_F_LOOP has no
 * interrupts, as on a real board: its bytes move only in the steps of
 * its own calls, which poll it.
 *
 * Where a byte goes.  A byte the port sends reaches the receive ring
 * of the port at the far end, of the same port under UART_F_LOOP, or
 * the queue fu_peer_recv reads, which holds FU_PEERQ bytes; while that
 * queue is full the port's bytes wait in its transmit ring.  A byte
 * sent to a port that is not open, or open with UART_F_LOOP, is gone.
 * A port holds its receiver as CUART.C's handler does: once its
 * receive ring is full nothing more arrives, the bytes waiting at the
 * far end (in its transmit ring, or the test's queue, as an emulator's
 * line waits), until uart_read or uart_rxflush leave UART_RXLOW or
 * fewer.  A byte that fu_inject marks is dropped or counted as it
 * says.
 *
 * Modem lines.  A port's DTR is up while it is open without
 * UART_F_LOOP, and reaches the far end's DSR and DCD as a full
 * null-modem cable carries it: a paired port sees both up while its
 * partner is open on the line, a port on the test's line sees what
 * fu_modem gives (both down at first, as on a cable of three wires),
 * and under UART_F_LOOP a port sees both up, as a chip in loopback
 * shows its own DTR and OUT2.
 * A port's RTS is up while it is open, except that with
 * UART_F_RTSCTS it drops once its receive ring holds UART_RXHIGH bytes
 * and rises again when uart_read or uart_rxflush leave UART_RXLOW or
 * fewer.  Its CTS is the RTS of the far end: of the paired port, its
 * own under UART_F_LOOP, or the test's, which is up unless
 * fu_peer_rts drops it.  A port open with UART_F_RTSCTS sends only
 * while its CTS is up; once it has met CTS down it sends again only
 * when CTS rises (the modem-status interrupt) or uart_write is called,
 * as CUART.C's handler does.  The test, as remote station, obeys the
 * port's RTS: its bytes go towards a port only while the port is open
 * without UART_F_LOOP and its RTS is up, and wait in the test's queue
 * meanwhile.
 *
 * Time.  Nothing happens between the calls.  A program that waits for
 * a byte polls uart_rxcount or uart_read; the pump (fu_pump), run at
 * the start of each uart_read, uart_write, uart_rxcount and
 * uart_txidle, lets a test serve the far end of a pair from those
 * waits.
 */
#ifndef FAKEUART_H
#define FAKEUART_H

#include "COMPAT.H"

#define FU_PEERQ        2048u   /* bytes of each of the test's queues   */

/* Faults fu_inject marks the next bytes arriving at a port with. */
#define FU_OVERRUN      1       /* dropped and counted as an overrun    */
#define FU_FRAMING      2       /* stored and counted as framing errors */
#define FU_PARITY       3       /* stored and counted as parity errors  */
#define FU_LOST         4       /* dropped and counted as lost          */

/* Bits of fu_isrstate: what uart_open set up and uart_close undoes. */
#define FU_S_OPEN       0x01    /* the port is open                     */
#define FU_S_IER        0x02    /* the chip's interrupts are enabled    */
#define FU_S_IRQ        0x04    /* its IRQ is unmasked at the PIC       */
#define FU_S_VECT       0x08    /* the vector points at its handler     */
#define FU_S_RTS        0x10    /* RTS is up                            */

/* Close every port and forget every port, pair, rate, fault, queued
   byte and the pump; a block a port held is freed.  A test calls it
   first. */
void fu_reset(void);

/* Make port (1..UART_MAXPORT) exist with chip (UART_T_), or with chip
   0 remove it, freeing the test's queues of it.  Its queues are taken
   from far memory here: a port made without memory for them aborts the
   program, as does one already open. */
void fu_port(u8 port, u8 chip);

/* Connect the lines of ports p1 and p2 (different, both made by
   fu_port) to each other, until fu_reset or the removal of either by
   fu_port. */
void fu_pair(u8 p1, u8 p2);

/* Move at most n bytes each way on port's line per step; 0 (the
   default) for no limit. */
void fu_rate(u8 port, u16 n);

/* Mark the next n bytes arriving at port with the fault kind (FU_);
   a later call replaces what is left of an earlier one, and n 0 ends
   it. */
void fu_inject(u8 port, u8 kind, u16 n);

/* The test as the remote station on port's line.  fu_peer_send queues
   up to n bytes of buf towards the port and returns how many it took,
   fewer than n when its queue of FU_PEERQ bytes fills.  fu_peer_recv
   copies up to size bytes the port has sent into buf and returns how
   many.  fu_peer_rts raises (on 1) or drops (on 0) the test's RTS, the
   port's CTS.  Each aborts the program for a port fu_port did not
   make. */
u16 fu_peer_send(u8 port, const void *buf, u16 n);
u16 fu_peer_recv(u8 port, void *buf, u16 size);
void fu_peer_rts(u8 port, int on);

/* The test's DSR and DCD (lines, UART_M_DSR and UART_M_DCD bits; the
   others are ignored) on port's line, as uart_modem shows them there.
   Aborts the program for a port fu_port did not make. */
void fu_modem(u8 port, u8 lines);

/* The rate the last uart_open of port gave, 0 when it was not opened
   since fu_reset or fu_port made it, with that open's flags into
   *flags.  Aborts the program for a port fu_port did not make. */
u16 fu_opened(u8 port, u8 *flags);

/* Run fn(arg) at the start of every uart_read, uart_write,
   uart_rxcount and uart_txidle call, never inside itself; NULL for
   none. */
typedef void (*fu_pump_fn)(void *arg);
void fu_pump(fu_pump_fn fn, void *arg);

/* The FU_S_ bits of port now, 0 for a port not open: a test checks
   with it that uart_close undid everything uart_open did. */
u8 fu_isrstate(u8 port);

/* The far block of an open port's rings, NULL for a port not open: its
   receive ring of UART_RXSZ bytes at offset 0 and its transmit ring of
   UART_TXSZ bytes after it; a test reads it to check that no byte that
   left a ring is still there. */
cmem_blk *fu_ringblk(u8 port);

#endif
