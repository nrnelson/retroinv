/*
 * LINKREM.H - the remote backends of the link, private to LINK.C,
 * LINKNET.C, LINKSER.C, LINKTCP.C and LINKMBX.C, and to LINKINT.H, the
 * link itself.
 *
 * A remote backend (NET in LINKNET.C, SERIAL in LINKSER.C, TCP in
 * LINKTCP.C, MBOX in LINKMBX.C) is a session object behind four calls:
 * send a line and read its response, which LINK.C makes for every
 * request, and the two ways to free it.  LINK.C owns everything else,
 * the judging of the session's end included.  Each backend is a file of
 * its own, which keeps each session's interface out of the others and
 * LINK.C within Watcom C 6.5's compiler memory.
 */
#ifndef LINKREM_H
#define LINKREM_H

#include "COMPAT.H"
#include "LINK.H"

/* Results of send and response besides 0, the same numbers as the
   sessions' own (LINKNET.C, LINKSER.C and LINKTCP.C check this). */
#define LR_ELOST        5       /* the session has ended or was lost    */
#define LR_EOUTPUT      6       /* the line callback reported a failure */

typedef struct {
    /* Send line (len bytes, no CR LF): 0 or LR_ELOST.  LINK.C calls
       response for the line before the caller's buffer changes. */
    int (*send)(void *s, const char *line, u16 len);
    /* Deliver the response to the oldest request to fn, waiting as
       LINK_NETWAIT says: 0, LR_ELOST or LR_EOUTPUT. */
    int (*response)(void *s, link_line_fn fn, void *arg);
    /* Free s once the session has ended or been lost. */
    void (*ended)(void *s);
    /* End the live session as the backend ends one, then free s. */
    void (*close)(void *s);
} link_rops;

/* Open a link in *out on the open session s of the backend ops, which
   the link then owns; name is the NetBIOS name a NET session called
   from (link_netname), NULL for none.  Returns 0, or LINK_ENOMEM with
   *out NULL and s left to the caller. */
int link_open_remote(link **out, void *s, const link_rops *ops,
                     const char *name);

/* The error number of a "-ERR nnn" response line of len bytes, else
   0. */
int link_err_code(const char *line, u16 len);

#endif
