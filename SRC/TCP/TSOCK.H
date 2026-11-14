/*
 * TSOCK.H - the TCP sockets of the bridge, a thin layer over a TCP/IP
 * stack (TSWAT.C on WATTCP and the packet driver; TEST/TSFAKE.C in the
 * tests).
 *
 * The layer has TS_SLOTS sockets, named by their slot numbers
 * 0..TS_SLOTS - 1, and shows no type of the stack beneath it: data
 * moves through ordinary near buffers, the only kind the stack takes in
 * the medium model.  The stack does its work (taking the frames the
 * packet driver has received, answering, sending, retransmitting,
 * timing out) only inside ts_poll, which the program calls on every
 * pass of its loop, apart from ts_write, which starts sending what it
 * accepts at once.  Every call returns at once, but for one case of
 * ts_poll: an ICMP echo request or redirect from a station not in the
 * stack's ARP cache makes it resolve that station's address before it
 * answers, which can take about five seconds when the station does not
 * reply.  A slot's state changes only inside ts_poll and the program's
 * own ts_listen, ts_close and ts_abort.  Every call but ts_listen takes
 * a slot out of range, and any slot before ts_init or after ts_term,
 * for a slot TS_FREE.
 *
 * A slot's states:
 *
 *   TS_FREE     never listened since ts_init;
 *   TS_LISTEN   waiting for a connection on its port (a connection that
 *               is being set up still counts as listening);
 *   TS_OPEN     connected: bytes move both ways;
 *   TS_CLOSING  the connection is ending and the stack still holds it;
 *   TS_CLOSED   the stack holds nothing of it: the slot may listen
 *               again.
 *
 * Several slots may listen on one port; a connection goes to one of
 * them, the stack's choice, and one that finds none listening is
 * refused (RST).  A connection that fails while it is being set up
 * leaves its slot TS_LISTEN or TS_CLOSED.
 *
 * There is no half-close: the stack cannot go on sending once the peer
 * has closed its side.  When the peer closes (FIN) or resets (RST) the
 * connection, its slot leaves TS_OPEN at once, both ways, for
 * TS_CLOSING or TS_CLOSED: the bytes received and not yet read are
 * dropped; the bytes written that the peer has not acknowledged may
 * still be sent, or are dropped; the slot may stay TS_CLOSING for some
 * seconds.
 *
 * Received bytes may hold passwords: the layer keeps no copy of one
 * once ts_read has taken it or the slot has dropped it.  The bytes are
 * zeroed in the socket's receive buffer as they go, and every received
 * frame's buffer as soon as ts_poll has handled it.  Written bytes are
 * not wiped, so the caller writes nothing secret.
 */
#ifndef TSOCK_H
#define TSOCK_H

#include "COMPAT.H"

#define TS_SLOTS        4       /* sockets                              */
#define TS_IPLEN        4       /* bytes of an IPv4 address             */

/* Results. */
#define TS_OK           0
#define TS_EARG         1       /* an argument out of range, a slot in
                                   the wrong state, the layer not
                                   started, or ts_init called before    */
#define TS_ENOPKT       2       /* no packet driver                     */
#define TS_ECFG         3       /* the stack's settings file (WATTCP.CFG)
                                   missing, or giving no address for
                                   this station                         */
#define TS_EINIT        4       /* the packet driver or the stack
                                   refused to start                     */

/* Slot states, as above. */
#define TS_FREE         0
#define TS_LISTEN       1
#define TS_OPEN         2
#define TS_CLOSING      3
#define TS_CLOSED       4

/* Start the stack: find the packet driver, take its handle for IP and
   ARP, read the settings file and set every slot TS_FREE.  Called once
   in a run: any later call, after a success or a failure, returns
   TS_EARG.  Returns TS_OK, or a TS_E code with nothing left behind
   (the driver's handle released).  On success it has registered an
   atexit handler that does what ts_term does, so the handle is
   released however the program ends, the stack's own fatal errors
   included; a handler the program registers after this call runs
   before it. */
int ts_init(void);

/* Abort every connection (RST), stop every listen and release the
   packet driver's handle.  Does nothing before ts_init or when it has
   already run. */
void ts_term(void);

/* Listen on port (1..65535) on a slot TS_FREE or TS_CLOSED, which is
   then TS_LISTEN.  Returns TS_OK, or TS_EARG for a slot out of range
   or in another state, port 0, or a call before ts_init or after
   ts_term. */
int ts_listen(u8 slot, u16 port);

/* Let the stack work, as above; nothing before ts_init or after
   ts_term. */
void ts_poll(void);

/* The state of slot. */
u8 ts_state(u8 slot);

/* Read into buf up to max bytes that the peer sent, in order: the
   count read, 0 when none waits or the slot is not TS_OPEN.  The bytes
   read are zeroed in the stack. */
u16 ts_read(u8 slot, void *buf, u16 max);

/* The bytes ts_write would accept now; 0 when the slot is not
   TS_OPEN. */
u16 ts_room(u8 slot);

/* Write len bytes of buf to the peer: the count accepted, at most
   ts_room's (0 when the slot is not TS_OPEN), the rest left to the
   caller.  The stack starts sending what it accepted at once, as fast
   as the peer's window allows, without waiting for more. */
u16 ts_write(u8 slot, const void *buf, u16 len);

/* The bytes written to the slot that the peer has not acknowledged; 0
   when the slot is neither TS_OPEN nor TS_CLOSING. */
u16 ts_unsent(u8 slot);

/* Close the connection of a slot TS_OPEN: the stack sends its end (FIN)
   after the bytes not yet sent, and the slot is TS_CLOSING until the
   peer has closed too and the stack has let the connection go, then
   TS_CLOSED.  From this call on, the bytes received are dropped, those
   waiting and those that arrive later.  A peer that never closes may
   keep the slot TS_CLOSING: the caller bounds the wait and then
   aborts.  Does nothing to a slot in another state. */
void ts_close(u8 slot);

/* Abort the connection (RST) or stop the listen of a slot in any state
   but TS_FREE, which is then TS_CLOSED at once, every byte held in
   either direction dropped. */
void ts_abort(u8 slot);

/* The peer of the slot's connection, from when it opened until the
   slot listens again: its address in ip (TS_IPLEN bytes, in the order
   of the dotted form) and its port in *port; zeros when the slot has
   had no connection. */
void ts_peer(u8 slot, u8 *ip, u16 *port);

#endif
