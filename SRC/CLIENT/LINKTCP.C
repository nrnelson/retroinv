/*
 * LINKTCP.C - the TCP backend of the link (LINK.H): a connection to the
 * TCP bridge INVTCP (COMPAT.H, TCP connections) behind the remote calls
 * of LINKREM.H.
 *
 * The bridge relays the connection's bytes over a NetBIOS session of
 * its own to the server, never reading them, so the connection carries
 * the protocol's lines as a NetBIOS session does, and the responses are
 * read with RESPRD.H.  The server ends its session after BYE, the last
 * failed LOGIN or a shutdown, and the bridge then closes the
 * connection; a connection closed while the session is live makes the
 * bridge hang the session up, which ends it as a BYE would.  So the
 * backend's end and its close are the same call.
 *
 * A request goes out in one write, its line and CR LF copied together
 * into tx, which is zeroed as soon as the write has returned, so no copy
 * of a password outlives its sending in the session.
 */
#define COMPAT_SOCKET
#define LINK_TCP
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "LINKREM.H"
#include "RESPRD.H"

/* RESPRD's results are the remote calls', and sk_open's SK_E codes
   number 1 up in the order of open_codes.  Each pair declares one array
   twice, the second time with size 2 when that fails: the declarations
   then disagree, which every compiler refuses. */
extern char linktcp_rc_ok[1];
extern char linktcp_rc_ok[(RR_ELOST == LR_ELOST &&
                           RR_EOUTPUT == LR_EOUTPUT) ? 1 : 2];
extern char linktcp_sk_ok[1];
extern char linktcp_sk_ok[(SK_ENOSTACK == 1 && SK_ENOMEM == 2 &&
                           SK_EREFUSED == 3 && SK_ECONN == 4 &&
                           SK_ECANCEL == 5) ? 1 : 2];

/* The LINK_ code of each code of sk_open, by its value. */
static const u8 open_codes[] = {
    0, LINK_ENOTCP, LINK_ENOMEM, LINK_EREFUSED, LINK_ECALL, LINK_ECANCEL
};

typedef struct {
    sk  *k;
    char tx[LINK_MAXLINE + 2];  /* the request being sent and its CR LF;
                                   zero between requests                */
    rr_buf rr;                  /* the response being read              */
} tl_sess;

/* LINK.C sends no line longer than LINK_MAXLINE: it answers one with
   103 itself. */
static int tl_send(void *s, const char *line, u16 len)
{
    tl_sess *t;
    int rc;

    t = (tl_sess *)s;
    memcpy(t->tx, line, len);
    t->tx[len] = '\r';
    t->tx[len + 1] = '\n';
    rc = sk_send(t->k, t->tx, (u16)(len + 2), LINK_NETWAIT);
    txt_wipe(t->tx, (u16)(len + 2));
    return rc == SK_OK ? 0 : LR_ELOST;
}

/* rr_fill_fn: the bytes the connection of the session arg holds, up to
   room, after waiting at most LINK_NETWAIT seconds for them; none once
   it has closed or is lost. */
static u16 fill(void *arg, char *dst, u16 room)
{
    return sk_recv(((tl_sess *)arg)->k, dst, room, LINK_NETWAIT);
}

static int tl_response(void *s, link_line_fn fn, void *arg)
{
    return rr_response(&((tl_sess *)s)->rr, fill, s, fn, arg);
}

static void tl_close(void *s)
{
    tl_sess *t;

    t = (tl_sess *)s;
    sk_close(t->k);
    txt_wipe(t, (u16)sizeof *t);
    free(t);
}

static const link_rops tcp_ops = {
    tl_send, tl_response, tl_close, tl_close
};

int link_open_tcp(link **out, const u8 *ip, u16 port, u16 *err)
{
    tl_sess *t;
    int rc;

    *out = NULL;
    *err = 0;
    t = (tl_sess *)malloc(sizeof *t);
    if (t == NULL)
        return LINK_ENOMEM;
    memset(t, 0, sizeof *t);
    rc = sk_open(&t->k, ip, port != 0 ? port : LINK_TCPPORT, LINK_NETWAIT,
                 err);
    if (rc != SK_OK) {
        free(t);
        return open_codes[rc];
    }
    rc = link_open_remote(out, t, &tcp_ops, NULL);
    if (rc != 0)
        tl_close(t);
    return rc;
}
