/*
 * LINKINT.H - the link itself, private to LINK.C and LINKLOC.C, the
 * LOCAL backend, which opens one.
 *
 * LINK.C reaches the dispatcher of a LOCAL link only through the link's
 * disp, which LINKLOC.C sets, and the engine only to close it, so that a
 * program without them links LINKNOL.C in place of LINKLOC.C and has no
 * engine, dispatcher or DBF layer.  The includer defines COMPAT_NETBIOS
 * before COMPAT.H: a link holds a NetBIOS name.
 */
#ifndef LINKINT_H
#define LINKINT_H

#include "COMPAT.H"
#include "ENGINE.H"
#include "PROTO.H"
#include "LINK.H"
#include "LINKREM.H"

struct link {
    int  end;                   /* LINK_OK, else LINK_CLOSED or
                                   LINK_ELOST: the session has ended    */
    proto_out out;
    proto_acc acc;

    /* The LOCAL backend: disp hands a request to proto_dispatch with
       host, sess and out, its output going to link_sink, and e is the
       engine; both NULL for the remote ones. */
    int (*disp)(struct link *l, const char *line, u16 len);
    eng  *e;
    proto_host host;
    proto_session sess;

    /* The remote backends: rs is NULL for LOCAL and once the session
       has ended. */
    void *rs;
    const link_rops *ro;
    u8   verb;                  /* LV_ of LINK.C: the request being
                                   answered                             */
    u8   fails;                 /* failed LOGINs since the last one that
                                   succeeded                            */
    char name[NB_NAMELEN];      /* NET: the name called from, else ""   */

    /* The response being delivered.  A remote backend hands its lines
       on as its session delivers them and uses resp only to answer an
       over-long line, so the parsed copy of the request it judges shares
       that room. */
    link_line_fn fn;
    void *arg;
    int  fnfail;                /* fn reported a failure                */
    u16  rlen;                  /* bytes in u.resp                      */
    union {
        char resp[LINK_MAXRESP + 2];   /* a line, a held-back CR, a NUL */
        proto_req req;          /* remote: the request being judged     */
    } u;
};

/* LINK.C.  The proto_write_fn of a link (arg) that delivers each line
   of its response to the link's callback. */
int link_sink(void *arg, const char *buf, u16 len);

#endif
