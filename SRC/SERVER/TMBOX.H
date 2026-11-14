/*
 * TMBOX.H - the server's mailbox transport (TMBOX.C).
 *
 * A mailbox is a directory, shared over the network or not, holding two
 * directories: REQUEST\, where a client puts a request file
 * <station>.REQ, and RESPONSE\, where the server leaves the response
 * <station>.RSP.  A client writes its file under another name and
 * renames it to .REQ once complete (MBXCLI.H), so the server never
 * reads half a request; the server writes RESPONSE\<station>.TMP and
 * renames it to .RSP the same way.  A session that a failed write ended
 * still puts in place what was written, its last response possibly cut,
 * which a client reads as cut short (MBXCLI.H).  The names, below, are
 * the protocol's, never translated.
 *
 * One request file is one session, on a slot claimed from CONN.H of
 * kind "MBOX", and one session runs at a time.  Every TMBOX_POLL ticks
 * while no session runs, the transport reads up to TMBOX_SCANMAX names
 * of REQUEST\*.REQ and takes the first after the last one served in
 * name order, wrapping round, so that a station that posts again and
 * again cannot keep another waiting.  The session opens with the file's
 * base name as its station (conn_open), which READONLY= caps as any
 * other; HELLO is still required.  The file, opened denying others
 * write access, is read whole (cf_readblk), CR LF added after its last
 * byte when it was not LF, then overwritten with zeros in the file and
 * committed, so that no DOS buffer backed by the file still holds any
 * of its text once the session acts on it; only then is it handed to
 * CONN, in pieces of up to CONN_RXSZ bytes (conn_input), the next once
 * CONN gives the last back.  A request of at most CONN_RXSZ less 2
 * bytes (room for that CR LF) is read into a block the transport keeps;
 * a longer one takes a far block of its own for its session. When there
 * is no memory for that block, or the request is longer than
 * CMEM_MAXBLK less the CR LF (both CF_ENOMEM), or the read, zeroing or
 * commit fails, the session ends with nothing handed to CONN, its
 * response empty, and the failure is reported to the watch function.
 * The output goes from conn_out_peek to the .TMP (cf_writeblk) and is
 * reported with conn_out_done.  The session ends once the last piece
 * has been given back with no output left, or when CONN closes it (BYE,
 * the last failed LOGIN, an idle timeout, a shutdown); the rest of the
 * request is then ignored.
 *
 * At the end of a session, whatever ended it: once the output CONN
 * still holds is written, the rest of the .TMP's last disk sector is
 * zeroed and the file cut back (not after a failed write); the .TMP is
 * committed and closed; the request file, unless already zeros, is
 * overwritten with zeros and committed, then removed, so a request
 * never runs twice and no password stays on the disk; a .RSP left from
 * before is removed and the .TMP renamed to it; then the slot is given
 * back (conn_closed).  A .RSP that cannot be removed (a client still
 * reading it) leaves the .TMP in place, the request gone all the same:
 * the rename is tried again on every look, and that station's next
 * request waits until it succeeds.  A request that cannot be removed
 * keeps the .TMP back in the same way, so that it is never answered
 * twice: each look tries the removal again while the request there
 * starts with a zero (or holds nothing), and puts the .TMP in place
 * once it is gone; while the request cannot be opened or read, the
 * .TMP waits too.  When both the zeroing and the removal fail, a
 * request whose session ended before it was zeroed keeps its text, so
 * the next look takes it for a new one: it is served again, its .RSP
 * replacing the first.  A write, commit, rename or removal that fails
 * otherwise (a full disk) ends the session as far as it can, a .TMP
 * whose write failed put in place as written, and is reported to the
 * watch function; a request that cannot be opened, or whose .TMP
 * cannot be made, is left for a later look, and a failure of it, or of
 * a rename tried again, is reported once until its station is served
 * or its file is gone.  A name DOS takes for a device (txt_dosdev),
 * which a client on another system can give a file, is passed over by
 * every look and at start, so no file of that name is ever opened,
 * renamed or removed: DOS would open the device, whatever the directory
 * or extension, and a read of CON waits for a key at the console.
 *
 * At start every RESPONSE\*.TMP is the end of a session of a run that
 * stopped: its request, if still there, is overwritten with zeros and
 * removed, and the .TMP then put in place as the .RSP as at the end of
 * a session, so that a request never runs twice, even across a crash.
 *
 * As a session lasts one file, the transport offers its sessions login
 * tickets (conn_tops.tickets, TICKET.H).  A ticket's response cannot be
 * wiped as a request is, since its client must read it; the client
 * zeroes the file once read (mbxcli_zero_next of MBXCLI.H).  So when the
 * mailbox is on the server's own disk, from the server's write of that
 * response until a later write through DOS overwrites the buffer that
 * held its sector (the client's zeroing, when the share's server writes
 * through the same DOS), a write that starts a new sector past the end
 * of another file (a data file, a memo or index page, the log) may take
 * that buffer and leave the rest of it, part of the ticket, in that
 * file's slack on the disk.  DOS 3.1 has no call that clears its
 * buffers.  The responses are spared, as each ends in a sector of its
 * own; a ticket so left ends as any ticket does, and is read only from
 * the raw disk.  A server that reaches the mailbox through a redirector
 * holds no response in its DOS buffers.
 *
 * The transport never reads the clock; now is CONN.H's tick count.
 */
#ifndef TMBOX_H
#define TMBOX_H

#include "COMPAT.H"

#define TMBOX_POLL      TMR_SEC /* ticks between looks for a request    */
#define TMBOX_SCANMAX   64      /* names one look reads at most         */
#define TMBOX_STALEN    8       /* longest station: a DOS base name     */

/* The mailbox's names: its two directories and the extensions of a
   request, a response being written and a response. */
#define TMBOX_REQDIR    "REQUEST"
#define TMBOX_RSPDIR    "RESPONSE"
#define TMBOX_REQEXT    "REQ"
#define TMBOX_TMPEXT    "TMP"
#define TMBOX_RSPEXT    "RSP"

/* Results of tmbox_start. */
#define TMBOX_OK        0
#define TMBOX_EARG      1       /* dir too long, or the transport
                                   already started                      */
#define TMBOX_ENODIR    2       /* dir, its REQUEST\ or its RESPONSE\
                                   does not exist                       */
#define TMBOX_ENOMEM    3       /* no memory for the input buffer       */

/* Start the transport on the mailbox dir, 1..ENG_MAXDIR characters
   ending in a separator ('\', '/' or ':') as SRVCFG.H gives it, which
   is kept by pointer.  conn_init must have been called with a slot for
   it.  Ends the sessions of the leftover RESPONSE\*.TMP as described
   above.  Returns TMBOX_OK, or a TMBOX_E code with nothing left
   behind. */
int tmbox_start(const char *dir);

/* One pass at time now, as described above.  Returns the number of
   pieces moved, files looked for, opened, renamed or removed, and
   slots given back or claimed (0 when there was nothing to do).  Does
   nothing before tmbox_start or after tmbox_stop. */
u16 tmbox_poll(u32 now);

/* Stop the transport: a session still running ends as described above
   and its slot is given back.  Does nothing when the transport is not
   started; every exit path of the server calls it. */
void tmbox_stop(void);

/* The mailbox directory given to tmbox_start, "" when the transport is
   not started. */
const char *tmbox_dir(void);

/* Receives each failure of a mailbox file, for the event log: slot,
   the number from 1 of the slot whose session the file is of, or 0
   when no session holds it (a rename tried again, the removals at
   start); the station whose request or response it is ("" for none);
   and the CF_E code. */
typedef void (*tmbox_fail_fn)(u8 slot, const char *station, int err);

/* Send failures to fn from now on; NULL for none (the start). */
void tmbox_watch(tmbox_fail_fn fn);

#endif
