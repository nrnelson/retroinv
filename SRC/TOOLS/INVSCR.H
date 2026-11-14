/*
 * INVSCR.H - the request script interpreter of INVTEST (INVSCR.C).
 *
 * A script drives up to INVSCR_SESSIONS sessions to one server: NetBIOS
 * sessions (NBCLI.H), each called from a local name of its own so that
 * the server sees a station per session, serial lines (SERCLI.H) and
 * mailbox sessions (MBXCLI.H).  Blank lines and lines starting with
 * '#' are ignored; every other line is one of:
 *
 *   @CALL n NAME   open session n (1..INVSCR_SESSIONS) from the local
 *                  name NAME and select it, calling again a second
 *                  apart for up to INVSCR_CALLTIME while the server's
 *                  name is not up or has no LISTEN posted, or after a
 *                  call that timed out or was aborted; as a CALL that
 *                  times out takes about half a minute on NetBEUI and
 *                  each open makes NBCLI_CALLTRIES of them a second
 *                  apart, an @CALL
 *                  may end up to one open (about 61 seconds) after
 *                  INVSCR_CALLTIME
 *   @COM n PORT [BAUD]
 *                  open session n on the serial port PORT (COM1 or
 *                  COM2) at BAUD (a rate of COMPAT.H's UART_BAUDOK,
 *                  default UART_DEFBAUD) and select it
 *   @MBOX n STATION DIR
 *                  open mailbox session n for STATION in the mailbox
 *                  directory DIR, as mbxcli_open takes them, and
 *                  select it
 *   @S n           select session n
 *   > line         send line on the selected session and read its
 *                  oldest unread response (this line's when none
 *                  waits); not on a mailbox session
 *   >& line        send line and leave the response for an @WAIT, at
 *                  most 2 left unread on a NetBIOS session, whose
 *                  server may receive no third request until the
 *                  first response is read; on a mailbox session,
 *                  queue it for the next @POST
 *   @POST n        post the lines queued on mailbox session n as one
 *                  request file and wait up to INVSCR_WAIT seconds for
 *                  the response file, whose responses become the unread
 *                  responses of those lines, in order
 *   @WAIT n        read the oldest unread response of session n
 *   < text         the next line of the response is text
 *   <* text        the next line of the response matches text, in
 *                  which '*' stands for any run of characters
 *   @HANGUP n      hang up session n
 *   @SLEEP t       wait t timer ticks (1..65535)
 *
 * A sigil is followed by one space before its text, or by nothing for
 * empty text; the words of an @ line are separated by one space each.
 * The lines of a response are checked against the < and <* lines that
 * follow the line that read it, in order: a response may have more
 * lines than are checked, but not fewer.  They are compared as they
 * travel, data lines dot-stuffed and the "." that ends a data response
 * included.  A request must hold more than blanks, since the server
 * answers no blank line.  A session is closed as soon as the response
 * to its BYE has been read, as the server ends it then; after the
 * failed LOGIN that makes the server end it, the script closes the
 * session with @HANGUP, which on a serial line sends no BYE and on a
 * mailbox session drops the lines queued.
 *
 * The transcript shows each @ line as read, each request as "n> line"
 * or "n>& line" and each response line as "n< line" ("n<" when empty),
 * n being the session; the arguments of LOGIN and PASSWD and the value
 * of a PASSWORD= pair, quoted or not, are shown as "***".  A failure
 * writes a line saying why.
 */
#ifndef INVSCR_H
#define INVSCR_H

#include <stdio.h>
#include "COMPAT.H"

#define INVSCR_SESSIONS 4       /* sessions a script may open           */
#define INVSCR_WAIT     60      /* seconds to wait for each message of a
                                   response, and for a response file    */
#define INVSCR_CALLTIME (6 * TMR_10SEC)  /* how long @CALL keeps calling,
                                   a minute                             */

/* Results, the exit statuses of INVTEST. */
#define INVSCR_PASS     0
#define INVSCR_FAIL     1       /* a line failed, or no script          */
#define INVSCR_NONB     2       /* no NetBIOS provider                  */
#define INVSCR_CALL     3       /* a NetBIOS session was not opened     */
#define INVSCR_PORT     4       /* a serial port could not be opened    */

/* Run the script file path against the server named server, or with
   server NULL against none, writing the transcript to out, and close
   every session it left open, a serial line with a BYE whose responses
   are read and dropped (sercli_close), so that the server ends its
   session, and a mailbox session dropping the lines it queued.  With
   server NULL an @CALL fails and NetBIOS is not looked for.  Returns
   INVSCR_PASS; INVSCR_FAIL, INVSCR_CALL or INVSCR_PORT with *line the
   number of the script line that failed; or, with *line 0, INVSCR_NONB
   (a server named and nb_present() 0) or INVSCR_FAIL (path cannot be
   opened). */
int invscr_run(FILE *out, const char *server, const char *path,
               u16 *line);

#endif
