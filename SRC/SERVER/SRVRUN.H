/*
 * SRVRUN.H - the server's run: start-up, loop and shutdown (SRVRUN.C).
 *
 * srv_run is INVSERV.EXE once its command line is read: it starts the
 * server, serves until the shutdown has ended every session, stops,
 * and returns the program's exit status.  It may run again after it
 * has returned, as the tests run it.
 *
 * Start-up, in this order: the critical-error handler; the settings file,
 * read strictly, so that a bad line stops the start naming its number and
 * a missing file means every default; the NetBIOS provider, unless
 * SESSIONS=0, without which the server refuses to start before it touches
 * the data; the message file MSG_SRVFILE; the data directory, whose engine
 * enlarges the handle table to ENG_PROCHANDLES; the event log of LOG=; the
 * connection slots, one for each NetBIOS session, serial port and the
 * mailbox, the failed-login throttle (THROTL.H), and with MAILBOX= the
 * store of login tickets (TICKET.H); BREAK checking off; the transports:
 * NetBIOS, which adds the name, the serial ports of SERIAL=, and the
 * mailbox of MAILBOX=, whose directories must exist; the status screen.  A
 * failure is said on standard error: in fixed English up to the message
 * file, in its texts from then on, which also say every failure of the
 * stop.
 *
 * The loop runs on a tick count of its own, which goes on across
 * midnight where the BIOS count starts again.  Each pass polls the
 * transports, serves one request per slot, closes the idle slots and
 * reads the keys; a pass that did something stirs the tickets' pool
 * with its timing (tkt_stir), and a pass that found nothing to do lets
 * resident programs run (dos_idle).  F3, once confirmed, and ADMIN
 * SHUTDOWN both start CONN's shutdown, which ends the sessions; once
 * every slot is free the loop ends.  A provider that never confirms a
 * cancel would keep a slot from being freed, so 2 * CONN_SHUTWAIT ticks
 * into the shutdown the loop ends all the same, and the stop leaves
 * those commands to nba_close and the wait below.
 *
 * Every way out, a failed start included, undoes what was started: the
 * transports stop (NetBIOS hanging up every session and deleting the
 * name, the serial ports closed and their interrupt handlers removed,
 * the mailbox ending its session), the ticket store is zeroed and
 * freed, the throttle freed, the data files are flushed and closed, the
 * screen restored and the log closed; then, while the provider still
 * owns NCBs it could write into, srv_run waits as nba_close (COMPAT.H)
 * says, reading the key that ended the wait if one did, then stops
 * NetBIOS again, which frees its receive buffers if the provider owns
 * no NCB by then, and only then frees the slots, whose output blocks a
 * SEND left to the provider was sending from.
 * BREAK checking is off and INT 23h returns from before the transports
 * start until then, so neither Ctrl-C nor Ctrl-Break can end the server
 * while the provider owns its NCBs or a port's handler is installed.
 * Nor can a critical error: one (a drive not ready, a sharing or lock
 * violation) is answered Fail, so the call that met it returns its
 * error to the engine, the request gets its response and the server
 * goes on; one DOS does not let fail is answered Retry, so the call
 * waits until the error clears.
 *
 * The event log, when LOG= names one, is created if missing and held
 * open for the run, denying other programs write access; it gets one
 * line per event, appended to what the file holds:
 *
 *   YYYY-MM-DD HH:MM:SS slot EVENT text
 *
 * slot 0 for the server itself, whose events are START (text: its name
 * of NAME=), STACK (text: the stack bytes never used, cstk_unused,
 * written by the stop when the program filled its stack with cstk_fill,
 * as INVSERV.EXE does) and STOP (text: the exit status); a session's
 * slot number from 1, as the screen shows it, with OPEN (its station),
 * LOGIN (the user), REQ (the request's words, never an argument,
 * CONN.H) and CLOSE; and MBOX for a mailbox file that failed (TMBOX.H),
 * on its session's slot or on slot 0, with the text "<CF_E code>
 * <station>".  The log holds no text of the message file: its words are
 * upper-case codes, like the protocol's, for the reader's tools, and are
 * never translated.  The new lines are committed (dos_commit_handle) on
 * the first pass with nothing to do or TMR_SEC ticks after the last
 * commit, whichever comes first.  A log that cannot be written is
 * closed, the server goes on, and the exit status says so.
 */
#ifndef SRVRUN_H
#define SRVRUN_H

#include "COMPAT.H"

/* Exit statuses of INVSERV.EXE. */
#define SRV_EXIT_OK     0       /* shut down by F3 or ADMIN SHUTDOWN    */
#define SRV_EXIT_USAGE  1       /* usage error                          */
#define SRV_EXIT_OPEN   2       /* the data directory could not be
                                   opened                               */
#define SRV_EXIT_DOSVER 3       /* DOS older than 3.1                   */
#define SRV_EXIT_IO     4       /* the event log could not be opened or
                                   written, or the data files could not
                                   be flushed and closed at the end     */
#define SRV_EXIT_NONB   5       /* no NetBIOS provider, SESSIONS= not 0 */
#define SRV_EXIT_NOMEM  6       /* no memory for the slots, the
                                   throttle, the ticket store, a
                                   transport or the screen              */
#define SRV_EXIT_MSG    7       /* the message file missing, unreadable,
                                   malformed or made for another
                                   version, or no memory to load it     */
#define SRV_EXIT_CFG    8       /* the settings file unreadable or not
                                   valid                                */
#define SRV_EXIT_NAME   9       /* the NetBIOS name could not be added
                                   (in use elsewhere, or refused)       */
#define SRV_EXIT_PORT   10      /* a port of SERIAL= missing or open
                                   already                              */
#define SRV_EXIT_MBOX   11      /* the directory of MAILBOX=, or its
                                   REQUEST\ or RESPONSE\, missing       */

/* Bytes of a buffer for srv_run's progdir, NUL included: the longest
   directory to which msg_fopen can still add an 8.3 name within
   DOS_PATHMAX. */
#define SRV_DIRSZ       (DOS_PATHMAX - 12)

/* Run the server with the settings file cfgpath and the message file
   from the directory progdir ("" or ending in a separator) or, when the
   file is not there or progdir is "", the current directory, as
   described above.  Returns SRV_EXIT_OK, or the status of what failed:
   any SRV_EXIT_ code but SRV_EXIT_USAGE and SRV_EXIT_DOSVER, which are
   the caller's.  A failure to close the data files or write the log
   turns SRV_EXIT_OK into SRV_EXIT_IO. */
int srv_run(const char *cfgpath, const char *progdir);

#endif
