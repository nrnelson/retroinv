/*
 * TSFAKE.H - a simulated TCP/IP stack for the tests (TSFAKE.C).
 *
 * TSFAKE.C takes the place of SRC/TCP/TSWAT.C in a test's link: it
 * defines every ts_ function of TSOCK.H, with the contracts given
 * there, and simulates the stack and its peers in portable C, with no
 * packet driver and no atexit handler.  The tsf_ functions below play
 * the peers and look inside it.  The fake is never kinder than the
 * stack: what TSOCK.H lets the stack drop, it drops.
 *
 * Peers.  tsf_connect opens a connection to a port; the lowest slot
 * listening on it takes it.  A peer's call takes effect at once, as if
 * ts_poll had run: a test makes it between the program's ts_ calls, or
 * from the pump inside ts_poll, so the program sees it where a stack
 * would show it.  A slot names to the tsf_ calls the connection it took
 * last, until it takes another; each connection's peer is TSF_PEERIP,
 * with a port TSF_PORT0 plus the number of connections taken before it
 * since tsf_reset.
 *
 * Bytes.  Each direction of a connection holds at most TSF_QMAX bytes
 * waiting, as the stack's socket buffers do.  What tsf_send gives waits
 * for ts_read.  What ts_write accepts
 * waits for tsf_recv and counts for ts_unsent until the next ts_poll
 * acknowledges it.  A byte written holds its room until it has been
 * both acknowledged and read by the peer, so ts_room is TSF_QMAX less
 * the bytes holding room, or less when tsf_room caps it, and after
 * tsf_noack every byte written holds its room for good.
 *
 * Ends.  ts_close leaves the slot TS_CLOSING; the next ts_poll ends the
 * close (TS_CLOSED) unless tsf_nofinack or tsf_noack holds it, and the
 * peer still reads every byte written before it.  A held close ends
 * only by the peer's tsf_fin, at the next ts_poll, or by ts_abort or
 * ts_term.  ts_abort of a connection the stack still holds (TS_OPEN or
 * TS_CLOSING), and ts_term, drop every byte waiting either way, as
 * tsf_rst does; tsf_fin drops them too, since the stack has no
 * half-close.  The settings of tsf_room, tsf_noack and tsf_nofinack
 * hold for the connection on the slot and end with it.
 *
 * Memory.  ts_init takes one far block (cmem_alloc) for the bytes
 * waiting of every slot, and returns TS_EINIT when far memory has run
 * out; ts_term and tsf_reset free it.  Unlike the stack, the fake does
 * not zero the bytes ts_read takes or a connection drops: its own
 * memory is not the program's, whose buffers the tests check.  Before
 * ts_init and after ts_term, ts_poll runs no pump.
 */
#ifndef TSFAKE_H
#define TSFAKE_H

#include "COMPAT.H"

#define TSF_QMAX        2048    /* bytes waiting in one direction of a
                                   connection                           */
#define TSF_PORT0       1024    /* the first connection's peer port     */
#define TSF_NOCAP       0xFFFFu /* tsf_room: no cap                     */
/* The peers' address, 10.0.2.2, its first byte the highest. */
#define TSF_PEERIP      0x0A000202UL

/* Results of tsf_connect. */
#define TSF_OK          0
#define TSF_REFUSED     1       /* no slot listens on the port (RST)    */

/* What tsf_recv saw of the connection. */
#define TSF_UP          0       /* open, or closing with bytes to read  */
#define TSF_EOF         1       /* the program closed it and every byte
                                   written has been read                */
#define TSF_GONE        2       /* aborted or reset, closed by the peer
                                   (tsf_fin), or never opened: the bytes
                                   not read are lost                    */

/* Forget every slot, connection and setting: the layer not started,
   the next ts_init returning TS_OK, no pump, no buffer noted, every
   count 0.  A test calls it first. */
void tsf_reset(void);

/* Make the next ts_init return rc (a TS_E code), starting nothing;
   TS_OK for none. */
void tsf_init_rc(int rc);

/* A peer connects to port: TSF_OK with *slot the slot that took the
   connection, now TS_OPEN; or TSF_REFUSED, counted by tsf_refused and
   *slot unchanged, when no slot listens on port. */
int tsf_connect(u16 port, u8 *slot);

/* The peer sends len bytes of buf on the connection of slot: the count
   taken, fewer than len when TSF_QMAX bytes would be waiting (the rest
   is the peer's to send again), 0 when the slot is not TS_OPEN. */
u16 tsf_send(u8 slot, const void *buf, u16 len);

/* The peer reads into buf (size bytes) what the program wrote on the
   connection of slot, in order, *n the count read.  Returns TSF_UP,
   TSF_EOF or TSF_GONE, *n then 0. */
int tsf_recv(u8 slot, void *buf, u16 size, u16 *n);

/* The peer closes (FIN), the slot then TS_CLOSING until the next
   ts_poll, a close held by tsf_nofinack included, or resets (RST), the
   slot then TS_CLOSED; both do nothing to a slot neither TS_OPEN nor
   TS_CLOSING. */
void tsf_fin(u8 slot);
void tsf_rst(u8 slot);

/* Let the program write at most n more bytes in all to the connection
   of slot until the next tsf_room for it; TSF_NOCAP for no cap. */
void tsf_room(u8 slot, u16 n);

/* Acknowledge nothing more on the connection of slot, its end (FIN)
   included: ts_unsent then counts every byte written to it since, read
   by the peer or not, and a close holds as under tsf_nofinack. */
void tsf_noack(u8 slot);

/* Never end a close of the connection of slot by itself: after
   ts_close it stays TS_CLOSING until the peer's tsf_fin, ts_abort or
   ts_term. */
void tsf_nofinack(u8 slot);

/* Run fn(arg) inside every ts_poll, never inside itself; NULL for
   none. */
typedef void (*tsf_pump_fn)(void *arg);
void tsf_pump(tsf_pump_fn fn, void *arg);

/* Counts since tsf_reset: connections refused, and ts_term calls that
   found the layer started. */
u16 tsf_refused(void);
u16 tsf_terms(void);

/* The buffer the last ts_read that read bytes wrote into, and in *len
   the count it read, so that a test can check the caller zeroed them;
   NULL with 0 when there was none since tsf_reset.  The buffer is the
   caller's: it must still exist when the test reads it. */
const u8 *tsf_lastbuf(u16 *len);

#endif
