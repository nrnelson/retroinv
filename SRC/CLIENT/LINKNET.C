/*
 * LINKNET.C - the NET backend of the link (LINK.H): an NBCLI session
 * behind the remote calls of LINKREM.H.
 *
 * nbcli_close hangs up unless the session has ended, which ends a live
 * session as a BYE would, so it is both the backend's end and its
 * close.
 */
#define COMPAT_NETBIOS
#include <stddef.h>
#include "COMPAT.H"
#include "LINK.H"
#include "LINKREM.H"
#include "NBCLI.H"

/* NBCLI's limits and results equal the link's.  Each pair declares one
   array twice, the second time with size 2 when that fails: the
   declarations then disagree, which every compiler refuses. */
extern char linknet_resp_ok[1];
extern char linknet_resp_ok[(NBCLI_MAXRESP == LINK_MAXRESP) ? 1 : 2];
extern char linknet_line_ok[1];
extern char linknet_line_ok[(NBCLI_MAXLINE == LINK_MAXLINE) ? 1 : 2];
extern char linknet_wait_ok[1];
extern char linknet_wait_ok[(NBCLI_SENDWAIT == LINK_NETWAIT) ? 1 : 2];
extern char linknet_rc_ok[1];
extern char linknet_rc_ok[(NBCLI_ELOST == LR_ELOST &&
                           NBCLI_EOUTPUT == LR_EOUTPUT) ? 1 : 2];

/* link_open_net gives NBCLI_E open codes the LINK_E codes in the same
   order, LN_OFF higher; a CALL refused (NB_RC_REJECTED) is
   LINK_EREFUSED. */
#define LN_OFF          (LINK_ENONB - NBCLI_ENONB)
extern char linknet_codes_ok[1];
extern char linknet_codes_ok[(LINK_ENAME == NBCLI_ENAME + LN_OFF &&
                              LINK_ECALL == NBCLI_ECALL + LN_OFF &&
                              LINK_ENOMEM == NBCLI_ENOMEM + LN_OFF) ?
                             1 : 2];

static int net_send(void *s, const char *line, u16 len)
{
    return nbcli_send((nbcli *)s, line, len);
}

static int net_response(void *s, link_line_fn fn, void *arg)
{
    return nbcli_response((nbcli *)s, fn, arg, LINK_NETWAIT);
}

static void net_close(void *s)
{
    nbcli_close((nbcli *)s);
}

static const link_rops net_ops = {
    net_send, net_response, net_close, net_close
};

int link_open_net(link **out, const char *server, u8 *nbrc)
{
    nbcli *nb;
    int rc;

    *out = NULL;
    rc = nbcli_open(&nb, server, NULL, nbrc);
    if (rc == NBCLI_ECALL && *nbrc == NB_RC_REJECTED)
        return LINK_EREFUSED;
    if (rc != NBCLI_OK)
        return rc + LN_OFF;
    rc = link_open_remote(out, nb, &net_ops, nbcli_name(nb));
    if (rc != 0)
        nbcli_close(nb);
    return rc;
}
