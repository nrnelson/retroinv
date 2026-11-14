/*
 * TSERIAL.H - the server's serial transport (TSERIAL.C).
 *
 * Each port of SERIAL= carries one session at a time, on a slot claimed
 * from CONN.H, of kind "COM1" or "COM2".  The slot listens (CONN_LISTEN)
 * until a byte arrives; the session then opens with no station of its
 * own (conn_open with NULL), so HELLO names it, and the bytes go on to
 * CONN.  A line that carries only noise thus holds its slot until the
 * idle timeout.  When the session ends (BYE, the last failed LOGIN, an
 * idle timeout, a shutdown), ops->close flushes the port's receive
 * ring at once, so what the caller sent within the session and was not
 * served goes; a byte arriving after that belongs to the next session.
 * The session's last output, already in the transmit ring, is let go:
 * the slot is given back (conn_closed) once the port's transmitter is
 * idle (uart_txidle) or CONN_SHUTWAIT ticks after the close, whichever
 * comes first.  In the second case what the ring still holds is
 * dropped, never sent into the next session: the port is closed and
 * opened again, and one that does not open stays closed.  Then the port
 * claims a slot again and listens, and a byte already waiting opens the
 * next session.  A client waits for the
 * response to its BYE before it sends again.  A port that finds no slot
 * free (a shutdown has begun) tries again on a later pass.
 *
 * Hang-up.  A port whose DSR or DCD (uart_modem) has been up during a
 * session, the far end's DTR on a full null-modem cable, ends the
 * session once both have been down for TSER_HANGUP ticks: the slot is
 * given back at once (conn_closed), the port closed and opened again,
 * which drops what its rings held, and it claims a slot and listens
 * again on the next pass.  A port whose lines never rise (a cable of
 * three wires) keeps its session until one of the ends above.
 *
 * Line errors.  The line has no checksum, so a byte lost or changed
 * shows only in the port's error count (uart_rxerrs).  Once the count
 * has risen since the receive ring was last emptied, what arrives is
 * dropped, and the line it ends, the first LF, ends the session at
 * once, as a hang-up does, but the port is not reopened: only its
 * receive ring is flushed, so the status screen keeps its counts.  That
 * line, whose error may lie anywhere in it, is not carried out and gets
 * no response, and the client's wait for one runs out.  An error with
 * no LF after it changes nothing more: its bytes are dropped, and the
 * session waits for one of the ends above.  While the port listens, an
 * error drops what the ring holds and opens no session, so noise from
 * a cable plugged in or a machine switched on does not cost the next
 * caller its first line.
 *
 * Each pass moves at most TSER_CHUNK bytes per port from its receive
 * ring into the slot's far input buffer of TSER_CHUNK bytes and hands
 * them to conn_input, and the next ones only after ops->rx_done: the
 * ring holds the rest meanwhile, which is the flow control (under
 * UART_F_RTSCTS the port also drops RTS as its ring fills).  Output
 * goes from conn_out_peek into the transmit ring, as much as it has
 * room for (uart_write), on ops->kick and on each pass, each piece
 * reported with conn_out_done.
 *
 * The ports are opened by tserial_start and stay open until
 * tserial_stop.  A file that includes this header defines COMPAT_UART
 * first.  TSERIAL.C checks at compile time that CONN_SERMAX is
 * UART_MAXPORT and that TSER_CHUNK is at most CONN_RXSZ.  The
 * transport never reads the clock; now is CONN.H's tick count.
 */
#ifndef TSERIAL_H
#define TSERIAL_H

#include "COMPAT.H"

#define TSER_CHUNK      256     /* bytes a port hands CONN per pass     */
#define TSER_HANGUP     (TMR_SEC / 2)   /* ticks DSR and DCD stay down
                                   before a caller counts as gone       */

/* Results of tserial_start and tserial_info. */
#define TSER_OK         0
#define TSER_EARG       1       /* ports, baud or flags out of range, or
                                   the transport already started; from
                                   tserial_info, no such port           */
#define TSER_ENOPORT    2       /* a port is missing (UART_ENOPORT)     */
#define TSER_EBUSY      3       /* a port is open already (UART_EBUSY)  */
#define TSER_ENOMEM     4       /* no memory for a port or its buffer   */

/* Start the transport on ports, bit port - 1 set for each port served
   (bit 0 COM1, bit 1 COM2; at least one, no other bit), opening each at
   baud with flags as uart_open takes them.  conn_init must have been
   called with a slot for every port.  Returns TSER_OK, or a TSER_E
   code with every port it opened closed again and nothing else left
   behind; *bad receives the port (1..UART_MAXPORT) whose open failed,
   0 when none did. */
int tserial_start(u8 ports, u16 baud, u8 flags, u8 *bad);

/* One pass at time now, as described above.  Returns the number of
   bytes moved, sessions opened and slots given back or claimed (0 when
   there was nothing to do).  Does nothing before tserial_start or after
   tserial_stop. */
u16 tserial_poll(u32 now);

/* Stop the transport: every slot still held is given back with
   conn_closed and every port closed (uart_close, which drops what its
   transmit ring still held).  Does nothing when the transport is not
   started; every exit path of the server calls it. */
void tserial_stop(void);

/* The i-th port served (from 0, in port order), for the status screen:
   TSER_OK with *port its number and ui filled by uart_getinfo, or
   TSER_EARG, nothing filled, when fewer than i + 1 ports are served. */
int tserial_info(u8 i, u8 *port, uart_info *ui);

#endif
