/*
 * NBCLI.H - a client's NetBIOS session to the server (NBCLI.C).
 *
 * The client CALLs the server's name, sends request lines and reads
 * whole responses, using the protocol's framing only: a response whose
 * first line is "+DATA" ends with a line holding a lone ".", any other
 * response is its first line.  Lines travel in messages of at most
 * NB_MSGMAX bytes; a message may hold several lines or part of one, and
 * bytes received after the end of a response are kept for the next.
 * Each session has one far block of 2 * NB_MSGMAX bytes: the message
 * being received and the copy of the message being sent.  Every SEND
 * and RECEIVE is a no-wait command, on an NCB of its own, polled with
 * the machine idling (dos_idle) until it completes or its wait runs
 * out; the CALL asks for no session timeouts, so these waits alone
 * bound a command.  A RECEIVE is posted before a request is sent, so
 * that the response finds it even when it arrives as the SEND completes
 * (NBCLI.C), and stays posted while the session is idle: a program ends
 * only after nbcli_close, never through a Ctrl-C (dos_break_off), or the
 * provider may later write into memory DOS has given to another program.
 *
 * Request lines hold passwords: the copy NBCLI makes to send a line is
 * zeroed once it has been sent, and an nbcli is zeroed before it is
 * freed.  Names are upper-cased (ASCII letters) and, as by CNETB.C, cut
 * to NB_NAMELEN - 1 characters.
 *
 * A session has ended when the server hung up (after BYE, or after the
 * last failed LOGIN, once it has sent the response), when it was lost,
 * or when a wait ran out (the RECEIVE or SEND is then cancelled and the
 * session hung up, unless the cancel of a SEND made the provider abort
 * it).  nbcli_send and nbcli_response on an ended session return
 * NBCLI_ELOST at once.
 */
#ifndef NBCLI_H
#define NBCLI_H

#include "COMPAT.H"

#define NBCLI_MAXRESP   512     /* response line, CR LF excluded, equal
                                   to LINK_MAXRESP (LINKNET.C checks
                                   it)                                  */
#define NBCLI_MAXLINE   292     /* request payload, CR LF excluded,
                                   equal to LINK_MAXLINE (LINKNET.C
                                   checks it)                           */
#define NBCLI_CALLTRIES 2       /* CALLs tried while they time out or
                                   are aborted                          */
#define NBCLI_SENDWAIT  60      /* seconds a message's SEND may take,
                                   equal to LINK_NETWAIT (LINKNET.C
                                   checks it)                           */

/* Results. */
#define NBCLI_OK        0
#define NBCLI_ENONB     1       /* no NetBIOS provider                  */
#define NBCLI_ENAME     2       /* the local name could not be added    */
#define NBCLI_ECALL     3       /* the CALL failed                      */
#define NBCLI_ENOMEM    4       /* no memory for the session            */
#define NBCLI_ELOST     5       /* the session has ended                */
#define NBCLI_EOUTPUT   6       /* the line callback reported a failure */

typedef struct nbcli nbcli;     /* an open session (opaque)             */

/* Receives one response line (len bytes, a NUL after them) without its
   CR LF, data lines still dot-stuffed; a nonzero return means the line
   could not be used. */
typedef int (*nbcli_line_fn)(void *arg, const char *line, u16 len);

/* CALL server from a local name and open the session in *out.  The
   local name is set up by nb_local_name (COMPAT.H) from local, with the
   prefix "RINV" and NB_NAMETRIES tries; a name it added is deleted
   again by nbcli_close, or at once when the open fails.  A CALL that
   times out or ends aborted (NB_RC_ABORTED) is made again a second
   later, NBCLI_CALLTRIES times in all: a set-up gone wrong leaves a
   half-open session, which either ends the server's LISTEN or
   completes it and holds the server's slot until its idle timeout
   (DOC/SERVER.md); the server listens again.  Returns NBCLI_OK; or,
   with *out NULL, NBCLI_ENONB (nb_present() found no provider),
   NBCLI_ENAME, NBCLI_ECALL or NBCLI_ENOMEM (also when nb_present()
   found no memory for its own block).
   *nbrc receives the NetBIOS return code of the command that failed
   (the last ADD NAME's for NBCLI_ENAME; NB_RC_NOANSWER when no station
   has the server's name, NB_RC_REJECTED when it has no LISTEN posted,
   ...), NB_OK when none did. */
int nbcli_open(nbcli **out, const char *server, const char *local,
               u8 *nbrc);

/* The local name the session was called from. */
const char *nbcli_name(nbcli *n);

/* Send line (len bytes, no CR or LF in them) and CR LF, in as many
   messages as it takes, each SEND waiting at most NBCLI_SENDWAIT
   seconds.  The length is not checked, so an over-long line reaches
   the server, which answers it with error 103.  A line of at most
   NBCLI_MAXLINE spaces and tabs, or an empty one, is sent too, but the
   server gives it no response, so it does not count as a request.
   At most 2 requests may have their responses unread: the server may
   receive no third until the first response is read, and the SEND
   would wait for it.  Returns NBCLI_OK, or NBCLI_ELOST when the session
   has ended or ends while sending. */
int nbcli_send(nbcli *n, const char *line, u16 len);

/* Read the response to the oldest request sent and not yet read, and
   deliver its lines to fn (not NULL) in order.  A line longer than
   NBCLI_MAXRESP is delivered in pieces of at most NBCLI_MAXRESP bytes.
   waitsecs is the longest time, in seconds (1 or more), to wait for
   each message.  Returns NBCLI_ELOST at once when the session has
   ended; NBCLI_OK, at once when no request waits for its response;
   NBCLI_EOUTPUT when fn reported a failure, the rest of the response
   being read and dropped; or NBCLI_ELOST when the session ended before
   the response was complete, the lines already delivered staying
   delivered. */
int nbcli_response(nbcli *n, nbcli_line_fn fn, void *arg, u16 waitsecs);

/* 1 once a call has found the session ended, else 0. */
int nbcli_closed(nbcli *n);

/* Hang up unless the session has ended, delete a name added by
   nbcli_open, and free n, zeroed first; n may be NULL. */
void nbcli_close(nbcli *n);

#endif
