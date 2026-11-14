/*
 * SERCLI.H - a client's session to the server over a serial line
 * (SERCLI.C).
 *
 * The client opens a port (uart_open of COMPAT.H), sends request lines
 * and reads whole responses, using the protocol's framing only, as
 * NBCLI.H does: a response whose first line is "+DATA" ends with a line
 * holding a lone ".", any other response is its first line, and bytes
 * received after the end of a response are kept for the next.  The
 * line carries no session set-up of its own: the server's slot opens at
 * the first byte it receives, so the first requests are HELLO, whose
 * station the server takes, and LOGIN.  The session ends when the
 * server ends it (after BYE, the last failed LOGIN, its idle timeout or
 * shutdown), which only the responses tell; the server then drops what
 * it has received and not yet served, and takes what arrives after
 * that as the start of the next session, so a client waits for each
 * response before it sends again.  Every wait polls the
 * port with the machine idling (dos_idle) and is bounded: a wait that
 * runs out takes the line as lost.  So does a receive error (an
 * overrun, a framing or parity error, a byte lost to a full queue)
 * that the port counts (uart_rxerrs) while a response is due, as the
 * line carries no checksum that could tell a byte lost or changed.
 *
 * Request lines hold passwords: every copy SERCLI makes of a line is
 * zeroed once it has gone into the port's transmit ring, which the port
 * clears of it once it is sent, and a sercli is zeroed before it is
 * freed.
 */
#ifndef SERCLI_H
#define SERCLI_H

#include "COMPAT.H"

#define SERCLI_MAXRESP  512     /* response line, CR LF excluded, equal
                                   to LINK_MAXRESP and NBCLI_MAXRESP
                                   (LINKSER.C and INVSCR.C check it)    */
#define SERCLI_SENDWAIT 60      /* seconds a line's sending may take,
                                   equal to LINK_NETWAIT (LINKSER.C
                                   checks it)                           */
#define SERCLI_BYEWAIT  2       /* seconds a lost line's closing BYE
                                   may take to go out                   */

/* Results. */
#define SERCLI_OK       0
#define SERCLI_EARG     1       /* port, baud or flags refused          */
#define SERCLI_ENOPORT  2       /* no such port (UART_ENOPORT)          */
#define SERCLI_EBUSY    3       /* the port is open already             */
#define SERCLI_ENOMEM   4       /* no memory for the session or port    */
#define SERCLI_ELOST    5       /* a wait ran out or a byte was lost or
                                   damaged: the line is lost            */
#define SERCLI_EOUTPUT  6       /* the line callback reported a failure */

typedef struct sercli sercli;   /* an open session (opaque)             */

/* Receives one response line (len bytes, a NUL after them) without its
   CR LF, data lines still dot-stuffed; a nonzero return means the line
   could not be used. */
typedef int (*sercli_line_fn)(void *arg, const char *line, u16 len);

/* Open port (1..UART_MAXPORT) at baud with flags (UART_F_RTSCTS or 0),
   as uart_open takes them, for a session in *out; nothing is sent.
   Returns SERCLI_OK; or, with *out NULL and the port closed,
   SERCLI_EARG (an argument uart_open refuses, or any other flag),
   SERCLI_ENOPORT, SERCLI_EBUSY or SERCLI_ENOMEM. */
int sercli_open(sercli **out, u8 port, u16 baud, u8 flags);

/* Send line (len bytes, no CR or LF in them) and CR LF, waiting while
   the transmit ring is full, at most SERCLI_SENDWAIT seconds without
   room for another byte.  The length is not checked, so an over-long
   line reaches the server, which answers it with error 103; a line that
   is no request (rr_request of RESPRD.H) is sent too, but gets no
   response.  When no response is due, the port's error count
   (uart_rxerrs) is noted first, for sercli_response to judge the line
   by, read before anything is dropped; when it changed while nothing
   was due, the bytes waiting in the receive ring are dropped first, as
   noise, so that the error loses no exchange and the noise leads no
   response.
   Returns SERCLI_OK, or SERCLI_ELOST when the line is lost or a wait
   runs out. */
int sercli_send(sercli *s, const char *line, u16 len);

/* Read the response to the oldest request sent and not yet read, and
   deliver its lines to fn (not NULL) in order.  A line longer than
   SERCLI_MAXRESP is delivered in pieces of at most SERCLI_MAXRESP
   bytes.  waitsecs is the longest time, in seconds (1 or more), to wait
   without a byte arriving.  Returns SERCLI_ELOST at once when the line
   is lost; SERCLI_OK, at once when no request waits for its response;
   SERCLI_EOUTPUT when fn reported a failure, the rest of the response
   being read and dropped; or SERCLI_ELOST when the wait ran out before
   the response was complete, or when bytes were read after the port's
   error count had changed from the value sercli_send noted, the line
   then lost and those bytes delivered to no one: the lines delivered
   before stay delivered. */
int sercli_response(sercli *s, sercli_line_fn fn, void *arg,
                    u16 waitsecs);

/* End the session and free s, zeroed first; s may be NULL.  With bye 1,
   unless the line is lost, BYE is sent first and every response still
   due read and dropped, BYE's included, each waited for as by
   sercli_response for SERCLI_SENDWAIT seconds, so that the server has
   ended the session before the port is closed; a caller that knows
   the session has ended already (its BYE answered, its last LOGIN
   failed, a shutdown answered) passes 0, since a BYE would open a new
   session on the server.  On a lost line, whatever bye, CR LF, BYE and
   CR LF are put in the transmit ring as far as it has room, and no
   response is waited for: the CR LF ends a request the server may hold
   unfinished, so that BYE ends a session it may still keep.  The port
   is closed after its transmitter is idle, waited for at most
   SERCLI_SENDWAIT seconds, or SERCLI_BYEWAIT on a lost line. */
void sercli_close(sercli *s, int bye);

#endif
