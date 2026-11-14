/*
 * MBXCLI.H - the mailbox client (MBXCLI.C) of INVTEST and of the link's
 * MBOX backend (LINKMBX.C).
 *
 * A mailbox session (the server's TMBOX.H) is one request file: the
 * client queues request lines, then posts them all at once as
 * REQUEST\<station>.REQ in the server's mailbox directory, written as
 * REQUEST\<station>.TMP, committed and renamed, so the server never
 * reads half a request; the server answers with RESPONSE\<station>.RSP,
 * which holds the response to each request it served, in order, until
 * the session ended.  The file name is the station, so the lines start
 * with HELLO and LOGIN like any session's.  The names of the
 * directories and extensions, and the longest station, are the
 * server's, mirrored below so that a client needs no server header
 * (T_MBXE checks that they agree).
 *
 * The queued lines, which may hold passwords, and the response read are
 * held in far blocks of MBXCLI_QMAX and MBXCLI_RSPMAX bytes, the queue
 * zeroed once written and both, with the line being read, zeroed on
 * close.  The request file is the server's to zero and remove; a
 * request file that could not be posted is zeroed and removed here,
 * and so are, before each post, the one a client of the station left
 * when it stopped in mid-post and a response file left from before.  A
 * response that holds a secret (a login ticket) is zeroed in its file
 * before the file is removed, when the client asks for it
 * (mbxcli_zero_next).
 * Every wait polls the mailbox TMR_SEC ticks apart with the machine
 * idling between (tmr_delay).
 *
 * A session knows only its own posts.  A response a station's earlier
 * session left due (it was closed, or its program ended, before the
 * response came) is the station's .RSP all the same: a new session of
 * the same station that posts before it comes zeroes and removes it if
 * it is there, and else may take it for its own response.  A station is
 * therefore used by one program at a time, and a program that gives up
 * on a response waits for it before the station posts again.
 */
#ifndef MBXCLI_H
#define MBXCLI_H

#include "COMPAT.H"

#define MBXCLI_MAXDIR   64      /* longest dir, its separator included  */
#define MBXCLI_STALEN   8       /* longest station: a DOS base name     */
#define MBXCLI_QMAX     4096    /* bytes of queued lines, CR LF in      */
#define MBXCLI_RSPMAX   CMEM_MAXBLK /* bytes of a response file         */
#define MBXCLI_MAXRESP  512     /* response line, CR LF excluded, equal
                                   to LINK_MAXRESP and NBCLI_MAXRESP
                                   (MBXCLI.C and INVSCR.C check it)     */

/* The mailbox's names: its two directories and the extensions of a
   request, a request being written and a response. */
#define MBXCLI_REQDIR   "REQUEST"
#define MBXCLI_RSPDIR   "RESPONSE"
#define MBXCLI_REQEXT   "REQ"
#define MBXCLI_TMPEXT   "TMP"
#define MBXCLI_RSPEXT   "RSP"

/* Results. */
#define MBXCLI_OK       0
#define MBXCLI_EARG     1       /* dir or station not valid             */
#define MBXCLI_ENOMEM   2       /* no memory for the session            */
#define MBXCLI_EFULL    3       /* the queue or the response would pass
                                   its block                            */
#define MBXCLI_EBUSY    4       /* lines queued while a request of the
                                   station is still out                 */
#define MBXCLI_EIO      5       /* a mailbox file could not be written,
                                   committed, renamed, read or removed  */
#define MBXCLI_EWAIT    6       /* the response has not come yet        */
#define MBXCLI_ENONE    7       /* no response left to deliver          */
#define MBXCLI_EOUTPUT  8       /* the line callback reported a failure */
#define MBXCLI_ECUT     9       /* the response file ends inside a
                                   response                             */

/* The steps of a post, named by the words of MBXCLI_STEPS in order,
   any of which may give MBXCLI_EIO. */
#define MBXCLI_SFIND    0       /* REQUEST\<station>.REQ looked for     */
#define MBXCLI_SCLEAR   1       /* a .RSP left from before removed      */
#define MBXCLI_SCREATE  2       /* the request's .TMP created           */
#define MBXCLI_SWRITE   3       /* written                              */
#define MBXCLI_SCLOSE   4       /* closed                               */
#define MBXCLI_SRENAME  5       /* renamed to .REQ                      */
#define MBXCLI_SOPEN    6       /* RESPONSE\<station>.RSP opened        */
#define MBXCLI_SREAD    7       /* sized and read                       */
#define MBXCLI_SRCLOSE  8       /* closed                               */
#define MBXCLI_SREMOVE  9       /* removed                              */
#define MBXCLI_STEPS    "FIND-REQ CLEAR-RSP CREATE-TMP WRITE-TMP " \
                        "CLOSE-TMP RENAME-TMP OPEN-RSP READ-RSP " \
                        "CLOSE-RSP REMOVE-RSP"
#define MBXCLI_STEPLEN  10      /* longest word of MBXCLI_STEPS         */

typedef struct mbxcli mbxcli;   /* an open session (opaque)             */

/* Receives one response line (len bytes, a NUL after them) without its
   CR LF, data lines still dot-stuffed; a nonzero return means the line
   could not be used. */
typedef int (*mbxcli_line_fn)(void *arg, const char *line, u16 len);

/* Open a session in *out on the mailbox dir (at most MBXCLI_MAXDIR
   characters with the separator that is added when dir does not end in
   '\', '/' or ':', DOS_DIRSEP; "" for the current directory) for
   station (1..MBXCLI_STALEN letters, digits, '-' or '_', upper-cased,
   not a name DOS takes for a device, txt_dosdev); no file is touched.
   Returns MBXCLI_OK, or MBXCLI_EARG or MBXCLI_ENOMEM with *out NULL. */
int mbxcli_open(mbxcli **out, const char *dir, const char *station);

/* Queue line (len bytes, no CR or LF in them) and CR LF for the next
   post.  Returns MBXCLI_OK, or MBXCLI_EFULL with nothing queued when
   the queue would pass MBXCLI_QMAX bytes. */
int mbxcli_queue(mbxcli *m, const char *line, u16 len);

/* Post the queued lines, if any, and wait for the response.  Lines
   queued are refused with MBXCLI_EBUSY while the response to an earlier
   post has not come or REQUEST\<station>.REQ is still there, and stay
   queued, as they do after an MBXCLI_EIO in the writing, whose file is
   zeroed and removed as far as it can be; otherwise a
   RESPONSE\<station>.RSP left from before is zeroed and removed, the
   request written, committed and renamed to .REQ, and the queue zeroed
   and emptied.  A request whose posting fails with nothing of it in
   REQUEST\ is written again once, TMR_SEC ticks later.  Then, while a
   response is due, the call looks for RESPONSE\<station>.RSP every
   TMR_SEC ticks, at most waitsecs + 1 times, reads it whole, closes it
   and removes it: its responses replace those not yet delivered of the
   post before.  A look that cannot open or read it leaves it for the
   next, and so does one that cannot close or remove it, but for a file
   to be zeroed (mbxcli_zero_next).  Returns MBXCLI_OK once the response
   is read, also at once when nothing was queued or due; MBXCLI_EWAIT
   when it has not come, the request staying posted for a later call to
   wait for; MBXCLI_EFULL for a response file longer than MBXCLI_RSPMAX,
   which is removed; MBXCLI_EBUSY; or MBXCLI_EIO, a response then still
   due when the last look failed.

   A basic redirector whose link to the server breaks connects again
   and repeats the call it had in flight, so a removal or rename that
   was done can report failure, and a read on the old handle fails: a
   removal that reports a failure while its file is gone, and a rename
   whose .TMP is gone, count as done.  A failed rename whose .TMP
   cannot be looked for is not done again in that call, as the request
   may be posted already; its lines stay queued with no response due,
   so a later call sends them again and the request may be served
   twice. */
int mbxcli_post(mbxcli *m, u16 waitsecs);

/* Deliver the next response of the last response file read to fn (not
   NULL), line by line, a line longer than MBXCLI_MAXRESP in pieces of
   at most MBXCLI_MAXRESP bytes, with the framing of RESPRD.H.  Returns
   MBXCLI_OK; MBXCLI_ENONE when none is left (the session ended before
   the requests did); MBXCLI_EOUTPUT when fn reported a failure, the
   rest of that response being dropped; or MBXCLI_ECUT when the file
   ends before the response does, the lines already delivered staying
   delivered and the rest dropped.  The server writes a whole file of
   whole lines, so an end inside a line or inside a data response is a
   response cut short (a write that failed), never delivered as
   complete: a last line without its LF delivers at most its whole
   MBXCLI_MAXRESP-byte pieces. */
int mbxcli_response(mbxcli *m, mbxcli_line_fn fn, void *arg);

/* Have the next response file that mbxcli_post reads overwritten with
   zeros before it is closed and removed, as it holds a secret.  Once
   read whole, that file counts as read: its zeroing, close and removal
   go as far as they go, without failing the look, as a file whose
   zeroing has begun cannot be read again; one left behind is zeroed
   and removed by the next post.  The request holds until a response
   file has been read, or removed as too long, so it carries over a post
   that failed and is made again. */
void mbxcli_zero_next(mbxcli *m);

/* Right after mbxcli_post gave MBXCLI_EIO: the step (MBXCLI_S) of the
   first call that failed in its last attempt to post or look for the
   response into *step, and its CF_E code, the DOS error, into *err.
   Every later attempt or look clears the record. */
void mbxcli_ioerr(mbxcli *m, int *step, int *err);

/* Free m, its blocks zeroed first; m may be NULL.  The mailbox's files
   are left as they are. */
void mbxcli_close(mbxcli *m);

/* m's far blocks, the queue into *q and the response into *rsp: an
   observation point for tests, which check that no queued line is left
   once posted. */
void mbxcli_blocks(mbxcli *m, cmem_blk **q, cmem_blk **rsp);

#endif
