/*
 * SRVSCR.H - the server's status screen (SRVSCR.C).
 *
 * The screen shows the server's name, data directory and uptime, the
 * slots waiting for a call and a table of the open sessions (CONN.H's
 * conn_info: slot, kind, station, user, level, state, idle time), the
 * serial ports served (TSERIAL.H's tserial_info: port, chip and error
 * counts, with the rate of BAUD=) and the mailbox, and one trace line.
 * It redraws itself from CONN (the waiting slots are its CONN_LISTEN
 * ones) and the serial transport once a second or so and on F5, and is
 * drawn through CVIDEO, so CGA snow avoidance and BIOS-only drawing
 * apply.  Its texts come from the server's message file, MSG_SRVFILE,
 * which the server loads (MSG.H) before srvscr_open.
 *
 * Keys, read without waiting: F3 asks to shut the server down, with a
 * confirmation line, also when no session is open (Y confirms, N or
 * Esc cancels, and F3 and F2 do nothing while it shows), during which the
 * server goes on serving; the line goes when a shutdown begins
 * otherwise (ADMIN SHUTDOWN), and during a shutdown F3 does nothing.
 * F5 redraws the whole screen, the confirmation line included, as does
 * Ctrl-Break, after which DOS may have echoed ^C.  F2 turns the trace
 * line on and off (off at each srvscr_open).
 */
#ifndef SRVSCR_H
#define SRVSCR_H

#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"
#include "SRVCFG.H"

/* Results of srvscr_poll. */
#define SRVSCR_NONE     0       /* go on serving                        */
#define SRVSCR_QUIT     1       /* the operator asked for a shutdown    */

/* Open the screen with the vid_open flags vflags and draw it for the
   settings cfg, which are kept by pointer.  Returns 0, or vid_open's
   code with nothing changed. */
int srvscr_open(u8 vflags, const srvcfg *cfg);

/* Read the keys waiting and redraw what is due, at time now (CONN.H's
   tick count).  Returns SRVSCR_QUIT once when F3 has been confirmed,
   else SRVSCR_NONE; also SRVSCR_NONE when the screen is not open. */
int srvscr_poll(u32 now);

/* Show event ev of slot c with its text, as conn_event_fn receives
   them (CONN.H), on the trace line when the trace is on, cut to the
   screen width; else do nothing.  The texts never hold an argument of
   a request. */
void srvscr_trace(u8 c, u8 ev, const char *text);

/* Restore the screen DOS had; does nothing when it is not open. */
void srvscr_close(void);

#endif
