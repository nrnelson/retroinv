/*
 * RESPRD.H - whole protocol responses read from a client's byte stream
 * (RESPRD.C), for the NetBIOS and serial sessions (NBCLI.H, SERCLI.H),
 * the TCP backend of the link (LINKTCP.C) and the mailbox client of
 * INVTEST and the MBOX link (MBXCLI.H).
 *
 * The framing is the protocol's only: a response whose first line is
 * "+DATA" ends with a line holding a lone ".", any other response is
 * its first line.  Lines end with LF, a CR before it dropped.  The
 * stream's bytes come from the session through a fill callback into the
 * reader's line buffer; what follows the end of a response stays there
 * for the next one.
 */
#ifndef RESPRD_H
#define RESPRD_H

#include "COMPAT.H"
#include "LINK.H"

/* Results of rr_response, the values of the NBCLI_, SERCLI_ and LR_
   codes of the same meaning, so that those sessions and LINKTCP.C pass
   them on as their own. */
#define RR_OK           0
#define RR_ELOST        5       /* the stream ended: fill gave 0        */
#define RR_EOUTPUT      6       /* the line callback reported a failure */

/* A reader; zeroed (memset) it is empty and ready. */
typedef struct {
    u16  lpos;                  /* next byte of line not yet delivered  */
    u16  llen;                  /* bytes in line                        */
    u8   cont;                  /* line starts inside a delivered line  */
    char line[LINK_MAXRESP + 1];   /* lines received, a held-back CR    */
} rr_buf;

/* Puts 1 to room bytes of the stream at dst and returns how many, or 0
   when the stream is lost. */
typedef u16 (*rr_fill_fn)(void *arg, char *dst, u16 room);

/* Receives one line (len bytes, a NUL after them) without its CR LF,
   data lines still dot-stuffed; a nonzero return means the line could
   not be used. */
typedef int (*rr_line_fn)(void *arg, const char *line, u16 len);

/* Read one response from r, filled by fill(farg), and deliver its lines
   to fn(arg) in order, or drop them when fn is NULL; a line longer than
   LINK_MAXRESP is delivered in pieces of at most LINK_MAXRESP bytes.
   Returns RR_OK; RR_EOUTPUT when
   fn reported a failure, the rest of the response being read and
   dropped; or RR_ELOST when fill gave 0 before the response was
   complete, the lines already delivered staying delivered. */
int rr_response(rr_buf *r, rr_fill_fn fill, void *farg, rr_line_fn fn,
                void *arg);

/* 1 when the request line of len bytes gets a response, else 0: a line
   of at most LINK_MAXLINE spaces and tabs, or an empty one, is no
   request to the server, which answers a longer one with error 103. */
int rr_request(const char *line, u16 len);

#endif
