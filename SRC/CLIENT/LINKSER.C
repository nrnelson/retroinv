/*
 * LINKSER.C - the SERIAL backend of the link (LINK.H): a SERCLI session
 * behind the remote calls of LINKREM.H.
 *
 * A link closed while its session is live sends BYE and reads the
 * responses still due (sercli_close with bye 1), so that no logged-in
 * session is left on the line; one whose session has ended closes the
 * port without a word, since a BYE would open a new session, unless the
 * line was lost, when SERCLI sends a last BYE without waiting for its
 * response, as the server may still hold the session.
 */
#define COMPAT_UART
#define LINK_SERIAL
#include <stddef.h>
#include "COMPAT.H"
#include "LINK.H"
#include "LINKREM.H"
#include "SERCLI.H"

/* SERCLI's limits and results equal the link's, and sercli_open's
   SERCLI_E codes number 1 up in the order of open_codes.  Each pair
   declares one array twice, the second time with size 2 when that
   fails: the declarations then disagree, which every compiler
   refuses. */
extern char linkser_resp_ok[1];
extern char linkser_resp_ok[(SERCLI_MAXRESP == LINK_MAXRESP) ? 1 : 2];
extern char linkser_wait_ok[1];
extern char linkser_wait_ok[(SERCLI_SENDWAIT == LINK_NETWAIT) ? 1 : 2];
extern char linkser_rc_ok[1];
extern char linkser_rc_ok[(SERCLI_ELOST == LR_ELOST &&
                           SERCLI_EOUTPUT == LR_EOUTPUT) ? 1 : 2];
extern char linkser_open_ok[1];
extern char linkser_open_ok[(SERCLI_EARG == 1 && SERCLI_ENOPORT == 2 &&
                             SERCLI_EBUSY == 3 &&
                             SERCLI_ENOMEM == 4) ? 1 : 2];

/* The LINK_ code of each code of sercli_open, by its value. */
static const u8 open_codes[] = {
    0, LINK_ESETUP, LINK_ENOPORT, LINK_EBUSY, LINK_ENOMEM
};

static int ser_send(void *s, const char *line, u16 len)
{
    return sercli_send((sercli *)s, line, len);
}

static int ser_response(void *s, link_line_fn fn, void *arg)
{
    return sercli_response((sercli *)s, fn, arg, LINK_NETWAIT);
}

static void ser_ended(void *s)
{
    sercli_close((sercli *)s, 0);
}

static void ser_close(void *s)
{
    sercli_close((sercli *)s, 1);
}

static const link_rops ser_ops = {
    ser_send, ser_response, ser_ended, ser_close
};

int link_open_serial(link **out, u8 port, u16 baud, u8 flags)
{
    sercli *s;
    int rc;

    *out = NULL;
    rc = sercli_open(&s, port, baud, flags);
    if (rc != SERCLI_OK)
        return open_codes[rc];
    rc = link_open_remote(out, s, &ser_ops, NULL);
    if (rc != 0)
        sercli_close(s, 0);
    return rc;
}
