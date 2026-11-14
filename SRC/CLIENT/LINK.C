/*
 * LINK.C - the client's link to the inventory system (LINK.H).
 *
 * With its backends this is the only client code that sees the protocol
 * layer.  The LOCAL backend (LINKLOC.C) owns an open engine and one
 * protocol session on it; a request goes to proto_dispatch, whose output
 * sink here (link_sink) splits the bytes into lines at LF, drops the CR
 * before it
 * and hands each line to the caller's callback.  The dispatcher writes a
 * whole line per sink call, but the sink does not rely on that.  This
 * file calls the dispatcher only through the link's disp (LINKINT.H)
 * and the engine only to close it, so a program without them links
 * LINKNOL.C in place of LINKLOC.C.
 *
 * The remote backends own a session, an NBCLI one for NET (LINKNET.C), a
 * SERCLI one for SERIAL (LINKSER.C), a connection to the TCP bridge for
 * TCP (LINKTCP.C) and a mailbox for MBOX (LINKMBX.C), behind the calls
 * of LINKREM.H; each sends a request and delivers the lines of its
 * response.  The server closes the session after +OK to BYE, after the
 * LINK_MAXFAIL-th failed LOGIN and after -ERR 501 (shutting down) to any
 * request; the link judges each from the command word of the request and
 * the first line of its response, as the server's session does, and
 * closes its end at once: on NET the provider may give the session
 * number to another session, and on SERIAL anything more sent would open
 * a new session.  An over-long line is answered here with the bytes of
 * the server's 103, through the sink, so no response writer is linked
 * for it.
 *
 * Stream input (link_feed) is assembled into lines by the protocol's own
 * line assembler, which also swallows an over-long line up to its LF so
 * that it is answered with 103 and the stream stays in step, exactly as
 * on every server transport.  The assembled line is wiped as soon as its
 * request has been dispatched, the parsed copy of a remote request as
 * soon as it has been judged, and the whole link before it is freed, so
 * no request line (and no password in one) stays in memory.
 */
#define COMPAT_NETBIOS
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#define ENGFLD_WIDTHS
#include "ENGFLD.H"
#include "ENGLOCS.H"
#define ENGTX_TXTONLY
#include "ENGTX.H"
#include "LINKINT.H"

/* The server's answer to an over-long request line, as proto_err writes
   it: ENG_ETOOLONG and its standard text. */
static const char k_toolong[] = "-ERR 103 " ENG_TXT_LONGLINE "\r\n";
extern char toolong_ok[1];
extern char toolong_ok[(ENG_ETOOLONG == 103) ? 1 : 2];

/* LINK.H repeats these limits for the screens, which do not include
   PROTO.H or ENGINE.H.  Each pair below declares one array twice, the
   second time with size 2 when the limits differ: the declarations then
   disagree, which every compiler refuses (a negative array size is
   accepted by some period compilers). */
extern char link_maxline_ok[1];
extern char link_maxline_ok[(LINK_MAXLINE == PROTO_MAXLINE) ? 1 : 2];
extern char link_maxresp_ok[1];
extern char link_maxresp_ok[(LINK_MAXRESP == PROTO_MAXRESP) ? 1 : 2];
extern char link_maxdir_ok[1];
extern char link_maxdir_ok[(LINK_MAXDIR == ENG_MAXDIR) ? 1 : 2];
extern char link_maxcursor_ok[1];
extern char link_maxcursor_ok[(LINK_MAXCURSOR == ENG_CURSORLEN) ? 1 : 2];
extern char link_maxmemo_ok[1];
extern char link_maxmemo_ok[(LINK_MAXMEMO == ENG_MAXMEMO) ? 1 : 2];
extern char link_pagemax_ok[1];
extern char link_pagemax_ok[(LINK_PAGEMAX == PROTO_PAGEMAX) ? 1 : 2];
extern char link_userlen_ok[1];
extern char link_userlen_ok[(LINK_USERLEN == ENG_USERLEN) ? 1 : 2];
extern char link_pwmin_ok[1];
extern char link_pwmin_ok[(LINK_PWMIN == ENG_PWMIN) ? 1 : 2];
extern char link_pwmax_ok[1];
extern char link_pwmax_ok[(LINK_PWMAX == ENG_PWMAX) ? 1 : 2];
extern char link_stalen_ok[1];
extern char link_stalen_ok[(LINK_STALEN == ENG_STALEN) ? 1 : 2];
extern char link_maxfind_ok[1];
extern char link_maxfind_ok[(LINK_MAXFIND == ENG_MAXFIND) ? 1 : 2];
extern char link_maxfspec_ok[1];
extern char link_maxfspec_ok[(LINK_MAXFSPEC == ENG_MAXFSPEC) ? 1 : 2];
extern char link_namelen_ok[1];
extern char link_namelen_ok[(LINK_NAMELEN == DBF_NAMELEN) ? 1 : 2];
extern char link_maxcodes_ok[1];
extern char link_maxcodes_ok[(LINK_MAXCODES == ENG_MAXCODES) ? 1 : 2];
extern char link_catlen_ok[1];
extern char link_catlen_ok[(LINK_CATLEN == ENG_CATLEN) ? 1 : 2];
extern char link_ltypelen_ok[1];
extern char link_ltypelen_ok[(LINK_LTYPELEN == ENG_LTYPELEN) ? 1 : 2];
extern char link_codenamelen_ok[1];
extern char link_codenamelen_ok[(LINK_CODENAMELEN == ENG_CODENAMELEN) ? 1 :
                                2];
extern char link_fullnamelen_ok[1];
extern char link_fullnamelen_ok[(LINK_FULLNAMELEN == ENG_FULLNAMELEN) ? 1 : 2];
extern char link_idlen_ok[1];
extern char link_idlen_ok[(LINK_IDLEN == ENG_IDLEN) ? 1 : 2];
extern char link_loclen_ok[1];
extern char link_loclen_ok[(LINK_LOCLEN == ENG_LOCLEN) ? 1 : 2];
extern char link_locnamelen_ok[1];
extern char link_locnamelen_ok[(LINK_LOCNAMELEN == ENG_LOCNAMELEN) ? 1 : 2];
extern char link_locpathlen_ok[1];
extern char link_locpathlen_ok[(LINK_LOCPATHLEN == ENG_LOCPATHLEN) ? 1 : 2];
extern char link_maxlocdepth_ok[1];
extern char link_maxlocdepth_ok[(LINK_MAXLOCDEPTH == ENG_MAXLOCDEPTH) ? 1 :
                                2];
extern char link_imperrs_ok[1];
extern char link_imperrs_ok[(LINK_IMPERRS == PROTO_IMPERRS) ? 1 : 2];
extern char link_why_ok[1];
extern char link_why_ok[(LINK_WHYSZ == ENG_WHYSZ) ? 1 : 2];
extern char link_maxfail_ok[1];
extern char link_maxfail_ok[(LINK_MAXFAIL == PROTO_MAXFAIL) ? 1 : 2];
extern char link_tktlen_ok[1];
extern char link_tktlen_ok[(LINK_TKTLEN == ENG_HASHLEN) ? 1 : 2];

/* The field widths LINK.H repeats, from the schema rows (ENGFLD.H). */
extern char link_typelen_ok[1];
extern char link_typelen_ok[(LINK_TYPELEN == ENG_FW_ITEMS_ITEMTYPE) ? 1 : 2];
extern char link_mfrlen_ok[1];
extern char link_mfrlen_ok[(LINK_MFRLEN == ENG_FW_ITEMS_MFR &&
                            LINK_MFRLEN == ENG_FW_ITEMS_MODEL) ? 1 : 2];
extern char link_seriallen_ok[1];
extern char link_seriallen_ok[(LINK_SERIALLEN == ENG_FW_ITEMS_SERIALNO) ?
                              1 : 2];
extern char link_descrlen_ok[1];
extern char link_descrlen_ok[(LINK_DESCRLEN == ENG_FW_ITEMS_DESCR &&
                              LINK_DESCRLEN == ENG_FW_ITEMS_TAGS) ? 1 : 2];
extern char link_condlen_ok[1];
extern char link_condlen_ok[(LINK_CONDLEN == ENG_FW_ITEMS_COND) ? 1 : 2];
extern char link_works_ok[1];
extern char link_works_ok[(LINK_WORKSLEN == ENG_FW_ITEMS_WORKS &&
                           LINK_NWORKS == ENG_NWORKS) ? 1 : 2];
extern char link_itaglen_ok[1];
extern char link_itaglen_ok[(LINK_ITAGLEN == ENG_ITAGLEN &&
                             LINK_TAGFILT == ENG_TAGFILT) ? 1 : 2];
extern char link_qtylen_ok[1];
extern char link_qtylen_ok[(LINK_QTYLEN == ENG_FW_ITEMS_QTY &&
                            LINK_QTYLEN == ENG_FW_ITEMS_ON_LOAN &&
                            ENG_FD_ITEMS_QTY == 0 &&
                            ENG_FD_ITEMS_ON_LOAN == 0) ? 1 : 2];
extern char link_acqsrclen_ok[1];
extern char link_acqsrclen_ok[(LINK_ACQSRCLEN == ENG_FW_ITEMS_ACQ_SRC) ?
                              1 : 2];
extern char link_pricelen_ok[1];
extern char link_pricelen_ok[(LINK_PRICELEN == ENG_FW_ITEMS_ACQ_PRICE &&
                              LINK_PRICELEN == ENG_FW_ITEMS_CUR_VALUE &&
                              LINK_PRICEDEC == ENG_FD_ITEMS_ACQ_PRICE &&
                              LINK_PRICEDEC == ENG_FD_ITEMS_CUR_VALUE) ?
                             1 : 2];
extern char link_statuslen_ok[1];
extern char link_statuslen_ok[(LINK_STATUSLEN == ENG_FW_ITEMS_STATUS &&
                               LINK_STATUSLEN == ENG_FW_USERS_STATUS) ?
                              1 : 2];
extern char link_grouplen_ok[1];
extern char link_grouplen_ok[(LINK_GROUPLEN == ENG_FW_USERS_GROUPNAME) ?
                             1 : 2];
extern char link_levellen_ok[1];
extern char link_levellen_ok[(LINK_LEVELLEN == ENG_FW_USERS_LEVEL &&
                              ENG_FD_USERS_LEVEL == 0) ? 1 : 2];

/* The kinds of request a remote backend judges a response by. */
#define LV_OTHER        0
#define LV_BYE          1
#define LV_LOGIN        2
#define LV_NONE         3       /* no request: the server answers none  */

/* Hand the first n bytes of resp to the callback and move the rest, if
   any, to the front. */
static void deliver(link *l, u16 n)
{
    char c;

    c = l->u.resp[n];
    l->u.resp[n] = '\0';
    if (!l->fnfail && l->fn != NULL && l->fn(l->arg, l->u.resp, n) != 0)
        l->fnfail = 1;
    l->u.resp[n] = c;
    memmove(l->u.resp, l->u.resp + n, (size_t)(l->rlen - n));
    l->rlen = (u16)(l->rlen - n);
}

/* proto_out sink: split the bytes into lines at LF, dropping the CR
   before it.  resp holds one byte more than a line, so a CR can wait to
   see whether an LF follows.  A line longer than LINK_MAXRESP (which the
   protocol's limits rule out) is delivered in pieces of at most
   LINK_MAXRESP bytes. */
int link_sink(void *arg, const char *buf, u16 len)
{
    link *l;
    u16 i;

    l = (link *)arg;
    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            if (l->rlen > 0 && l->u.resp[l->rlen - 1] == '\r')
                l->rlen--;
            if (l->rlen > LINK_MAXRESP)
                deliver(l, LINK_MAXRESP);
            deliver(l, l->rlen);
        } else {
            if (l->rlen > LINK_MAXRESP)
                deliver(l, LINK_MAXRESP);
            l->u.resp[l->rlen++] = buf[i];
        }
        if (l->fnfail)
            return 1;
    }
    return 0;
}

/* The LV_ kind of the request line of len bytes, as the protocol's
   tokenizer reads it into r: LV_NONE for a line of no tokens (empty, or
   only spaces and TABs), else from its command word.  The parsed copy
   is wiped: it may hold a password. */
static u8 req_verb(proto_req *r, const char *line, u16 len)
{
    u8 v;

    v = LV_OTHER;
    if (proto_parse(r, line, len) == ENG_OK) {
        if (r->nargs > 0) {
            if (txt_ieq(r->arg[0], "BYE"))
                v = LV_BYE;
            else if (txt_ieq(r->arg[0], "LOGIN"))
                v = LV_LOGIN;
        } else if (r->npairs == 0) {
            v = LV_NONE;
        }
    }
    txt_wipe(r->buf, (u16)sizeof r->buf);
    return v;
}

int link_err_code(const char *line, u16 len)
{
    if (len < 8 || memcmp(line, "-ERR ", 5) != 0)
        return 0;
    return line[5] * 100 + line[6] * 10 + line[7] -
           ('0' * 100 + '0' * 10 + '0');
}

/* link_line_fn of a remote session: judge the line as the server does,
   then hand it to the caller's callback.  The server ends the session
   after ENG_ESHUTDOWN to any request, after +OK to BYE, and after the
   LINK_MAXFAIL-th failed LOGIN, a failure it counts being ENG_EBADPW,
   ENG_EDISABLED, the storage class (3xx) or ENG_EINTERNAL.  Only the
   first line of a response can begin with +OK or -ERR, so every line
   may be judged. */
static int remote_line(void *arg, const char *line, u16 len)
{
    link *l;
    int ok;
    int code;

    l = (link *)arg;
    ok = len >= 3 && memcmp(line, "+OK", 3) == 0;
    code = link_err_code(line, len);
    if (code == ENG_ESHUTDOWN || (l->verb == LV_BYE && ok))
        l->end = LINK_CLOSED;
    else if (l->verb == LV_LOGIN && ok)
        l->fails = 0;
    else if (l->verb == LV_LOGIN &&
             (code / 100 == ENG_EIO / 100 || code == ENG_EBADPW ||
              code == ENG_EDISABLED || code == ENG_EINTERNAL) &&
             ++l->fails >= LINK_MAXFAIL)
        l->end = LINK_CLOSED;
    return l->fn(l->arg, line, len);
}

/* Send one request over the remote session and deliver its response.
   A line of no tokens is not sent: the server would answer nothing. */
static int remote_send(link *l, const char *line, u16 len)
{
    int rc;

    l->verb = req_verb(&l->u.req, line, len);
    if (l->verb == LV_NONE)
        return LINK_OK;
    rc = l->ro->send(l->rs, line, len);
    if (rc == 0)
        rc = l->ro->response(l->rs, remote_line, l);
    if (rc == LR_ELOST)
        l->end = LINK_ELOST;
    if (l->end != LINK_OK) {
        l->ro->ended(l->rs);
        l->rs = NULL;
    }
    return rc == LR_EOUTPUT ? LINK_EOUTPUT : l->end;
}

/* Send one request (or answer an over-long one with 103) and deliver
   its response. */
static int dispatch(link *l, const char *line, u16 len, int toolong,
                    link_line_fn fn, void *arg)
{
    int r;

    if (l->end != LINK_OK)
        return l->end;
    l->fn = fn;
    l->arg = arg;
    l->fnfail = 0;
    l->rlen = 0;
    if (l->rs != NULL && !toolong)
        return remote_send(l, line, len);
    if (toolong) {
        link_sink(l, k_toolong, (u16)(sizeof k_toolong - 1));
        r = PROTO_R_OK;
    } else {
        r = l->disp(l, line, len);
    }
    if (l->rlen > LINK_MAXRESP)
        deliver(l, LINK_MAXRESP);
    if (l->rlen > 0)
        deliver(l, l->rlen);
    if (r == PROTO_R_CLOSE)
        l->end = LINK_CLOSED;
    return l->fnfail ? LINK_EOUTPUT : l->end;
}

int link_open_remote(link **out, void *s, const link_rops *ops,
                     const char *name)
{
    link *l;

    *out = NULL;
    l = (link *)malloc(sizeof *l);
    if (l == NULL)
        return LINK_ENOMEM;
    memset(l, 0, sizeof *l);
    l->rs = s;
    l->ro = ops;
    if (name != NULL)
        strcpy(l->name, name);
    proto_acc_init(&l->acc);
    *out = l;
    return 0;
}

const char *link_netname(link *l)
{
    return l->name[0] != '\0' ? l->name : NULL;
}

int link_send(link *l, const char *line, u16 len, link_line_fn fn,
              void *arg)
{
    return dispatch(l, line, len, len > LINK_MAXLINE, fn, arg);
}

int link_feed(link *l, const char *buf, u16 n, link_line_fn fn, void *arg)
{
    u16 i;
    int a;
    int r;

    if (l->end != LINK_OK)
        return l->end;
    for (i = 0; i < n; i++) {
        a = proto_acc_feed(&l->acc, (int)(u8)buf[i]);
        if (a == PROTO_ACC_NONE)
            continue;
        r = dispatch(l, l->acc.buf, l->acc.len, a == PROTO_ACC_LONG,
                     fn, arg);
        proto_acc_wipe(&l->acc);
        if (r != LINK_OK)
            return r;
    }
    return LINK_OK;
}

int link_close(link *l)
{
    int rc;

    if (l == NULL)
        return 0;
    if (l->rs != NULL)
        l->ro->close(l->rs);
    rc = eng_close(l->e);
    txt_wipe(l, (u16)sizeof *l);
    free(l);
    return rc;
}

const void *link_bytes(link *l, u16 *size)
{
    *size = (u16)sizeof *l;
    return l;
}
