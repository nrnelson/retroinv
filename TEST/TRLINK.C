/*
 * TRLINK.C - a recording LOCAL link for the client tests (TRLINK.H).
 */
#define TL_ENDS
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "LINK.H"
#include "TRLINK.H"

struct link {
    eng  *e;
    proto_host host;
    proto_session sess;
    proto_out out;
    link_line_fn fn;
    void *arg;
    int  closed;
    int  lost;
    int  fnfail;                /* fn asked to stop                     */
    const char *name;           /* link_netname's, or NULL              */
    u16  rlen;
    char resp[LINK_MAXRESP + 2];
};

static char tl_buf[TL_LOGMAX];  /* request lines, each ended by '\n'    */
static u16 tl_len;              /* bytes in tl_buf, NUL excluded        */
static u8 tl_over;              /* a request did not fit tl_buf         */
static u8 tl_busy;              /* inside link_send's delivery          */
static u16 tl_n;                /* requests sent                        */
static u16 tl_at;               /* answer request tl_n == tl_at ...     */
static const char *tl_resp;     /* ... with these lines, NULL to close  */
static char *tl_cut;            /* the '\n' tl_req made a NUL, or NULL  */
static const char *tl_name;     /* tl_netname's, for the next opens     */

void (*tl_hook)(const char *line, u16 len);

const char tl_lost[] = "";
const char tl_reset[] = "";
const char tl_gone[] = "";

/* Put back the '\n' that tl_req cut the log at. */
static void uncut(void)
{
    if (tl_cut != NULL)
        *tl_cut = '\n';
    tl_cut = NULL;
}

/* Hand the n bytes in l->resp to the line function, ended by a NUL;
   its callers stop once it has asked to (l->fnfail). */
static void deliver(link *l, u16 n)
{
    l->resp[n] = '\0';
    if (l->fn != NULL && l->fn(l->arg, l->resp, n) != 0)
        l->fnfail = 1;
}

/* Hand the lines of resp, each ended by '\n' but the last, to the line
   function one at a time, as the dispatcher's output comes: without the
   '\n', in the link's response buffer.  Stops when it asks to; a '\n'
   ending resp ends the session after the last line. */
static void tl_answer(link *l, const char *resp)
{
    const char *e;
    size_t n;

    for (;;) {
        e = strchr(resp, '\n');
        n = e != NULL ? (size_t)(e - resp) : strlen(resp);
        if (n > LINK_MAXRESP)
            n = LINK_MAXRESP;
        memcpy(l->resp, resp, n);
        deliver(l, (u16)n);
        if (e != NULL && e[1] == '\0')
            l->closed = 1;
        if (e == NULL || e[1] == '\0' || l->fnfail)
            return;
        resp = e + 1;
    }
}

/* proto_out sink of tl_gone: the response is lost, and its arguments
   only looked at. */
static int drop(void *arg, const char *buf, u16 len)
{
    return arg == NULL && buf == NULL && len > 0;
}

/* proto_out sink: split the bytes into lines at LF, dropping the CR;
   1 (no more output wanted) once the line function has asked to stop. */
static int sink(void *arg, const char *buf, u16 len)
{
    link *l;
    u16 i;

    l = (link *)arg;
    for (i = 0; i < len && !l->fnfail; i++) {
        if (buf[i] == '\n') {
            if (l->rlen > 0 && l->resp[l->rlen - 1] == '\r')
                l->rlen--;
            deliver(l, l->rlen);
            l->rlen = 0;
        } else if (l->rlen < LINK_MAXRESP) {
            l->resp[l->rlen++] = buf[i];
        }
    }
    return l->fnfail;
}

int link_open_local(link **out, const char *dir, const link_clock *now,
                    char *why, u16 whysz)
{
    eng_time t;
    link *l;
    int rc;

    *out = NULL;
    if (dos_set_handles(ENG_PROCHANDLES) != 0)
        return ENG_EINTERNAL;
    l = (link *)malloc(sizeof *l);
    if (l == NULL)
        return ENG_EINTERNAL;
    memset(l, 0, sizeof *l);
    memset(&t, 0, sizeof t);
    if (now != NULL) {
        t.year = now->year;
        t.month = now->month;
        t.day = now->day;
        t.hour = now->hour;
        t.minute = now->minute;
        t.second = now->second;
    }
    rc = eng_open(&l->e, dir, now != NULL ? &t : NULL, why, whysz);
    if (rc != ENG_OK) {
        free(l);
        return rc;
    }
    l->host.e = l->e;
    l->name = tl_name;
    sess_init(&l->sess, NULL);
    *out = l;
    return 0;
}

void tl_netname(const char *name)
{
    tl_name = name;
}

const char *link_netname(link *l)
{
    return l->name;
}

int link_send(link *l, const char *line, u16 len, link_line_fn fn,
              void *arg)
{
    int at;
    int r;

    if ((u32)tl_len + len + 2 <= TL_LOGMAX) {
        memcpy(tl_buf + tl_len, line, len);
        tl_len = (u16)(tl_len + len);
        tl_buf[tl_len++] = '\n';
        tl_buf[tl_len] = '\0';
    } else {
        tl_over = 1;
    }
    tl_n++;
    if (tl_hook != NULL)
        tl_hook(line, len);
    at = tl_at != 0 && tl_n == tl_at;
    if (at && tl_resp == tl_lost)
        l->lost = 1;
    if (l->lost)
        return LINK_ELOST;
    if (l->closed)
        return LINK_CLOSED;
    l->fn = fn;
    l->arg = arg;
    l->fnfail = 0;
    l->rlen = 0;
    tl_busy = 1;
    if (at && tl_resp == tl_reset) {
        sess_init(&l->sess, NULL);
        at = 0;
    }
    if (at && tl_resp == tl_gone) {
        proto_out_init(&l->out, drop, NULL, l);
        proto_dispatch(&l->host, &l->sess, line, len, &l->out);
        l->lost = 1;
        tl_busy = 0;
        return LINK_ELOST;
    }
    if (at) {
        if (tl_resp == NULL)
            l->closed = 1;
        else
            tl_answer(l, tl_resp);
    } else {
        proto_out_init(&l->out, sink, NULL, l);
        r = proto_dispatch(&l->host, &l->sess, line, len, &l->out);
        if (r == PROTO_R_CLOSE)
            l->closed = 1;
    }
    tl_busy = 0;
    if (l->fnfail)
        return LINK_EOUTPUT;
    return l->closed ? LINK_CLOSED : LINK_OK;
}

int link_close(link *l)
{
    int rc;

    if (l == NULL)
        return 0;
    rc = eng_close(l->e);
    free(l);
    return rc;
}

void tl_clear(void)
{
    tl_cut = NULL;
    tl_buf[0] = '\0';
    tl_len = 0;
    tl_over = 0;
    tl_n = 0;
    tl_at = 0;
}

u16 tl_count(void)
{
    return tl_n;
}

int tl_reading(void)
{
    return tl_busy;
}

int tl_full(void)
{
    return tl_over;
}

const char *tl_log(void)
{
    uncut();
    return tl_buf;
}

const char *tl_req(u16 i)
{
    char *p;

    p = tl_buf;
    uncut();
    while (i > 0 && (p = strchr(p, '\n')) != NULL) {
        p++;
        i--;
    }
    if (p == NULL || *p == '\0')
        return "";
    tl_cut = strchr(p, '\n');
    *tl_cut = '\0';
    return p;
}

void tl_fail(u16 n, const char *resp)
{
    tl_at = n;
    tl_resp = resp;
}
