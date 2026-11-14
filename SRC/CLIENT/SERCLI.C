/*
 * SERCLI.C - a client's session to the server over a serial line
 * (SERCLI.H).
 *
 * A session has one far block: a piece of the line being sent at SC_TX
 * and the bytes just read from the receive ring at SC_RX, each
 * SC_CHUNK bytes.  A request goes out piece by piece, each piece copied
 * to SC_TX, handed to the transmit ring as it makes room, and zeroed
 * there once the ring holds all of it.  Received bytes go from the
 * ring through SC_RX into the reader of RESPRD.H, which keeps what
 * follows the end of a response for the next one.  Each read from the
 * ring is followed by a look at the port's error count (uart_rxerrs),
 * which has risen by then if any byte read may be damaged (COMPAT.H),
 * so no such byte reaches the reader.
 */
#define COMPAT_UART
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "RESPRD.H"
#include "SERCLI.H"

#define SC_CHUNK        256     /* bytes of each half of the block      */
#define SC_TX           0       /* a piece of the line being sent       */
#define SC_RX           SC_CHUNK        /* bytes read from the ring     */

/* The open results equal uart_open's, the response results
   rr_response's.  The pair declares one array twice, the second time
   with size 2 when that fails: the declarations then disagree, which
   every compiler refuses. */
extern char sercli_codes_ok[1];
extern char sercli_codes_ok[(SERCLI_EARG == UART_EARG &&
                             SERCLI_ENOPORT == UART_ENOPORT &&
                             SERCLI_EBUSY == UART_EBUSY &&
                             SERCLI_ENOMEM == UART_ENOMEM &&
                             SERCLI_ELOST == RR_ELOST &&
                             SERCLI_EOUTPUT == RR_EOUTPUT) ? 1 : 2];

static const char k_bye[] = "BYE";
static const char k_lostbye[] = "\r\nBYE\r\n";

struct sercli {
    cmem_blk *buf;              /* SC_TX and SC_RX                      */
    u16  npend;                 /* requests whose response is unread    */
    u16  wait;                  /* seconds the response being read may
                                   go without a byte                    */
    u16  errs;                  /* uart_rxerrs as the oldest request
                                   still due was sent                   */
    u8   port;
    u8   closed;                /* the line is lost                     */
    rr_buf rr;                  /* the response being read              */
};

/* 1 when secs seconds have passed since the tick start; otherwise 0,
   after idling once. */
static int expired(u32 start, u16 secs)
{
    if (tmr_since(start) >= (u32)secs * TMR_10SEC / 10)
        return 1;
    dos_idle();
    return 0;
}

/* Hand the n bytes at SC_TX to the transmit ring, waiting while it is
   full, at most SERCLI_SENDWAIT seconds without room for a byte, then
   zero them.  Returns SERCLI_OK, or SERCLI_ELOST, the line then lost,
   when the wait ran out. */
static int put(sercli *s, u16 n)
{
    u32 start;
    u16 off;
    u16 m;

    start = tmr_ticks();
    for (off = 0; off < n; off = (u16)(off + m)) {
        m = uart_write(s->port, s->buf, (u16)(SC_TX + off), (u16)(n - off));
        if (m != 0) {
            start = tmr_ticks();
        } else if (expired(start, SERCLI_SENDWAIT)) {
            s->closed = 1;
            break;
        }
    }
    cmem_set(s->buf, SC_TX, 0, n);
    return s->closed ? SERCLI_ELOST : SERCLI_OK;
}

/* rr_fill_fn: the bytes waiting in the receive ring, up to room, after
   waiting at most s->wait seconds for one; none, the line then lost,
   when the wait runs out or the port counted a receive error since the
   request was sent. */
static u16 fill(void *arg, char *dst, u16 room)
{
    sercli *s;
    u32 start;
    u16 n;

    s = (sercli *)arg;
    if (room > SC_CHUNK)
        room = SC_CHUNK;
    start = tmr_ticks();
    while ((n = uart_read(s->port, s->buf, SC_RX, room)) == 0 &&
           !expired(start, s->wait))
        ;
    if (n == 0 || uart_rxerrs(s->port) != s->errs) {
        s->closed = 1;
        return 0;
    }
    cmem_get(s->buf, SC_RX, dst, n);
    return n;
}

int sercli_open(sercli **out, u8 port, u16 baud, u8 flags)
{
    sercli *s;
    int rc;

    *out = NULL;
    if ((flags & ~UART_F_RTSCTS) != 0)
        return SERCLI_EARG;
    s = (sercli *)malloc(sizeof *s);
    if (s == NULL)
        return SERCLI_ENOMEM;
    memset(s, 0, sizeof *s);
    s->buf = cmem_alloc(2 * SC_CHUNK);
    rc = s->buf == NULL ? SERCLI_ENOMEM : uart_open(port, baud, flags);
    if (rc != UART_OK) {
        cmem_free(s->buf);
        free(s);
        return rc;
    }
    s->port = port;
    *out = s;
    return SERCLI_OK;
}

int sercli_send(sercli *s, const char *line, u16 len)
{
    u16 off;
    u16 m;
    int rc;

    if (s->closed)
        return SERCLI_ELOST;
    if (s->npend == 0) {
        /* The server only answers, so what came while nothing was due
           is noise, or the answer to the last BYE of a client before
           this one: it is dropped, so that it does not lead the next
           response.  The count is read first, so that an error counted
           during the drain, whose byte may stay, loses the exchange. */
        s->errs = uart_rxerrs(s->port);
        while (uart_read(s->port, s->buf, SC_RX, SC_CHUNK) == SC_CHUNK)
            ;
    }
    rc = SERCLI_OK;
    for (off = 0; off < len && rc == SERCLI_OK; off = (u16)(off + m)) {
        m = (u16)(len - off);
        if (m > SC_CHUNK)
            m = SC_CHUNK;
        cmem_put(s->buf, SC_TX, line + off, m);
        rc = put(s, m);
    }
    if (rc == SERCLI_OK) {
        cmem_put(s->buf, SC_TX, "\r\n", 2);
        rc = put(s, 2);
    }
    if (rc == SERCLI_OK && rr_request(line, len))
        s->npend++;
    return rc;
}

int sercli_response(sercli *s, sercli_line_fn fn, void *arg,
                    u16 waitsecs)
{
    int rc;

    if (s->closed)
        return SERCLI_ELOST;
    if (s->npend == 0)
        return SERCLI_OK;
    s->wait = waitsecs;
    rc = rr_response(&s->rr, fill, s, fn, arg);
    if (rc != RR_ELOST)
        s->npend--;
    return rc;
}

void sercli_close(sercli *s, int bye)
{
    u32 start;
    u16 wait;

    if (s == NULL)
        return;
    if (s->closed) {
        cmem_put(s->buf, SC_TX, k_lostbye, sizeof k_lostbye - 1);
        uart_write(s->port, s->buf, SC_TX, sizeof k_lostbye - 1);
    } else if (bye && sercli_send(s, k_bye, sizeof k_bye - 1) ==
               SERCLI_OK) {
        while (s->npend != 0 &&
               sercli_response(s, NULL, NULL, SERCLI_SENDWAIT) !=
               SERCLI_ELOST)
            ;
    }
    wait = s->closed ? SERCLI_BYEWAIT : SERCLI_SENDWAIT;
    start = tmr_ticks();
    while (!uart_txidle(s->port) && !expired(start, wait))
        ;
    uart_close(s->port);
    cmem_free(s->buf);
    txt_wipe(s, (u16)sizeof *s);
    free(s);
}
