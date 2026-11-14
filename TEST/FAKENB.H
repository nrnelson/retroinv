/*
 * FAKENB.H - a simulated NetBIOS provider for the tests (FAKENB.C).
 *
 * FAKENB.C takes the place of SRC/COMPAT/CNETB.C in a test's link: it
 * defines every nb_ and nba_ function of COMPAT.H, with the return
 * codes given there, and simulates the provider and a network in
 * portable C, so a client, a server, or both in one program, run over
 * NetBIOS on any build.  It reaches the callers' buffers only through
 * cmem_get and cmem_put.  The fnb_ functions below set it up and
 * look inside it.
 *
 * The network.  Names in the local name table (FNB_NAMES at most)
 * belong to the program (nb_add_name, nb_delete_name); names given to
 * fnb_peer_call and fnb_dupname belong to other stations.  ADD NAME
 * into a full table gives NB_RC_NAMEFULL.  DELETE NAME of a name not
 * in the table gives NB_RC_NONAME; of a name that open ends use,
 * NB_RC_DELPEND, the name then staying in the table, unusable, until
 * the last of them closes.  Either way the LISTENs posted for it,
 * stalled ones included unless held (fnb_hold), complete with
 * NB_RC_NAMEDEL.  A CALL or a fnb_peer_call to a local name meets the
 * oldest LISTEN posted for that name whose remote name is "*" or the
 * caller's, which completes.  A CALL gets, in this order: NB_RC_NONAME
 * when its own local name is not usable in the table, NB_RC_NOANSWER
 * when the called name is not, NB_RC_REJECTED when that name has no
 * such LISTEN, NB_RC_SESSFULL when fewer than two ends are free.  A
 * no-wait CALL (nba_call) completes inside its post, with one of these
 * codes or NB_OK, unless fnb_call_hold holds it, so a cancel finds it
 * complete and one that met its LISTEN leaves its session open, as
 * COMPAT.H says.  A LISTEN whose
 * local name is not usable gives NB_RC_NONAME, one posted while no end
 * is free NB_RC_SESSFULL; at most FNB_MAXEND ends are open at once.
 * Each end has its own LSN, the lowest from 1 that no open end uses;
 * the LISTEN's end takes its LSN first, so the lower one.
 *
 * An end stays open, keeping its LSN and counted by fnb_sessions, until
 * it closes: when its own side hangs it up, or, for an end of the
 * program whose session the other side or fnb_abort has ended, when the
 * fake reports the end to the program, as a real provider then removes
 * the session: the first time commands on the end complete with
 * NB_RC_SESSCLOSED or NB_RC_ABORTED (those in progress at the end, else
 * the next one posted), the end closes.  A hang-up of an end whose
 * ended session was not yet reported returns NB_RC_SESSCLOSED.  A
 * remote station's end closes only by fnb_peer_hangup, which a test
 * calls before it expects fnb_sessions() to be 0.  A command, by the
 * program or by a remote station, on an LSN that none of its own open
 * ends uses gives NB_RC_BADLSN (a no-wait post is refused).
 *
 * Messages.  A SEND completes once the other end has received the whole
 * message; until then the bytes stay in the sender's buffer.  A RECEIVE
 * takes the oldest message waiting, or the rest of it after
 * NB_RC_INCOMPLETE.  A remote station's messages (fnb_peer_send) are
 * copied and wait in the fake, FNB_QMAX bytes per session at most.  An
 * end that closes completes its commands in progress, stalled ones
 * included unless held, with NB_RC_SESSCLOSED, or NB_RC_ABORTED when
 * an abort closed it (its SENDs' messages are dropped).  After a
 * hang-up the other end still receives every message that waited
 * before it, then NB_RC_SESSCLOSED, as does any other command on an end
 * of a session that has ended.
 *
 * Time.  The fake does its work inside the calls: a no-wait command
 * completes in the call that meets its condition (a post, a poll, a
 * command or fnb_peer_ call of the other end), then shows NB_RC_PENDING
 * to fnb_latency more nba_poll calls on its NCB; until it shows its
 * final code it counts as in progress for fnb_ncbmax and fnb_pending.
 * The session's timeouts, rto for a RECEIVE and sto for a SEND (as
 * given to the CALL or LISTEN that opened the end; a remote station's
 * ends have none), count tries, one per unit.  A no-wait RECEIVE or
 * SEND that is still in progress at its rto-th or sto-th nba_poll
 * completes with NB_RC_TIMEOUT; a timeout of 0 never passes.  A
 * wait-form command that cannot complete at once calls the pump
 * (fnb_pump) and tries again, FNB_SPINS times or fewer when its timeout
 * is nonzero and lower (for nb_listen_wait, its seconds), then gives
 * NB_RC_TIMEOUT, so a test never hangs.  A RECEIVE that times out
 * leaves the session open; a SEND that times out aborts it.  A CALL
 * refused with NB_RC_NOANSWER or NB_RC_REJECTED, or timed out by
 * fnb_call_timeouts, runs the pump once before nb_call or the post of
 * nba_call returns, for the time a provider spends on the call, so that
 * a test can post the LISTEN between a program's tries.
 * nb_listen_wait otherwise behaves as a wait-form nb_listen, but
 * cancels its LISTEN on expiry: a stalled cancel (fnb_stall) makes it
 * and every later nb_listen_wait return NB_RC_PENDING until the LISTEN
 * completes, and nb_last_cancel_rc give NB_RC_NOCANCEL.  Only
 * nb_listen_wait uses a stall; nb_listen never returns NB_RC_PENDING.
 *
 * nb_present() returns 1, or what fnb_present sets; every nb_ command
 * and nba_ post gives NB_RC_NOT_PRESENT until it has returned 1 after
 * the last fnb_reset, as with CNETB.C.  nba_cancel takes effect at
 * once: the command completes, cancelled, and nba_poll shows it
 * without latency, unless fnb_stall keeps it in progress.  The first
 * cancel of a SEND aborts its session, stalled or not.  A call on a set
 * after its nba_close aborts the test program.
 */
#ifndef FAKENB_H
#define FAKENB_H

#include "COMPAT.H"

#define FNB_MAXEND      32      /* session ends open at once            */
#define FNB_QMAX        4096    /* bytes of remote messages waiting per
                                   session                              */
#define FNB_SPINS       1000    /* pump calls a wait-form command makes */
#define FNB_NAMES       16      /* names in the local name table        */
#define FNB_OTHERS      8       /* names fnb_dupname may give away      */

/* Forget every name, session, command and set (whatever the program
   still holds becomes invalid) and every setting below: latency 0, no
   command limit, no stalled cancels, holds or refused posts, no LISTEN
   code, no timed-out or held CALLs, no remote names, no buffer noted,
   no pump, no HANG UP counted by fnb_badhangups, no RECEIVE counted by
   fnb_late, nb_present returning 1.  A test calls it first. */
void fnb_reset(void);

/* Make nb_present return rc from now on: 1 finds the provider, 0 finds
   none, NB_PRES_NOMEM finds no memory; a result other than 1 leaves no
   provider found, as CNETB.C's does. */
void fnb_present(int rc);

/* Delay the visible completion of every no-wait command from now on by
   polls further nba_poll calls on its NCB; 0 for none. */
void fnb_latency(u16 polls);

/* Allow at most n no-wait commands in progress at once over every set;
   a post beyond that returns NB_RC_TOOMANY.  0 for no limit. */
void fnb_ncbmax(u16 n);

/* Make the next n cancels (by nba_cancel, inside nba_close, or by
   nba_stuck of a held command) leave their command in progress, as a
   provider that does not confirm a cancel: nba_cancel returns
   NB_RC_PENDING, nba_close counts the NCB.  Only a cancel that finds
   its command not yet complete uses one of the n.  Such a command takes
   part in nothing (it gets no message and meets no CALL, nor times out)
   and completes only by a later cancel, by the closing of its end, or,
   for a LISTEN, by the deletion of its name, unless fnb_hold keeps it;
   one whose set was closed stays in progress, counted by fnb_pending
   and nba_stuck, until then.  The cancel of nb_listen_wait on expiry
   uses one too. */
void fnb_stall(u16 n);

/* Make the next n stalled commands that the closing of their end or the
   deletion of their name would complete stay in progress instead, as
   a provider that ends them only on a cancel: each is then held, takes
   part in nothing more, and completes only by a cancel, by nba_cancel
   or, once its set is closed, by the cancel each nba_stuck call makes
   of it; such a cancel uses a stall as any other.  0 for none. */
void fnb_hold(u16 n);

/* Refuse the next n no-wait posts that pass the checks of their NCB
   and arguments with rc, the NCB left as it was: NB_RC_TOOMANY
   (fnb_toomany) as a provider short of room for a while, any other code
   as a provider that rejects the command itself; rc is never NB_OK.
   0 for none. */
void fnb_refuse(u16 n, u8 rc);
#define fnb_toomany(n)  fnb_refuse(n, NB_RC_TOOMANY)

/* Make the next LISTEN posted (no-wait or wait-form) whose name is
   usable and for which an end is free complete at once with rc in
   place of meeting a call.  NB_OK for none. */
void fnb_listen_rc(u8 rc);

/* Make the next n CALLs of the program (nb_call or nba_call) complete
   with rc, whatever they would have met: NB_RC_TIMEOUT
   (fnb_call_timeouts) as a CALL whose session the called station
   dropped as it opened, NB_RC_ABORTED as one the network broke as it
   opened; the LSN (*lsn, nba_lsn) is 0 and nothing else changes.  0
   for none. */
void fnb_call_fail(u16 n, u8 rc);
#define fnb_call_timeouts(n) fnb_call_fail(n, NB_RC_TIMEOUT)

/* Make the next n no-wait CALLs of the program (nba_call) stay in
   progress, as CALLs the provider is still making, before
   fnb_call_timeouts or anything else applies to them: each meets
   nothing and completes only by a cancel, with NB_RC_CANCELLED, unless
   a stall (fnb_stall) keeps it in progress.  0 for none. */
void fnb_call_hold(u16 n);

/* The far block holding the buffer of the last no-wait SEND (send 1)
   or RECEIVE (send 0) that the program posted and the fake took, *off
   and *len as given to the post, so that a test can read the buffer
   after the command or the program's use of it has ended; NULL with 0
   and 0 when there was none since fnb_reset.  The block may have been
   freed since. */
cmem_blk *fnb_lastbuf(int send, u16 *off, u16 *len);

/* Give name to another station: nb_add_name of it then gives
   NB_RC_NAMEINUSE.  More than FNB_OTHERS names abort the program. */
void fnb_dupname(const char *name);

/* End the session of which lsn is one end abnormally, as a broken
   network does: the commands in progress on both ends complete with
   NB_RC_ABORTED, and an end of the program that had one closes, later
   commands on its LSN getting NB_RC_BADLSN; on an end that had none,
   the next command completes with NB_RC_SESSCLOSED.  Does nothing when
   no open end uses lsn. */
void fnb_abort(u8 lsn);

/* Complete the oldest RECEIVE or SEND of the program in progress with
   rc, as a provider's failure of that one command, its session left
   open (a SEND's message goes).  Does nothing when none is in
   progress. */
void fnb_fail(u8 rc);

/* A remote station, driven by the test.  Its end of a session is named
   by the LSN fnb_peer_call gave.  None of these calls wait. */

/* Call the local name to from the remote name from.  Returns NB_OK with
   *lsn set, or NB_RC_REJECTED, NB_RC_NOANSWER or NB_RC_SESSFULL as a
   CALL would, *lsn 0. */
u8 fnb_peer_call(const char *from, const char *to, u8 *lsn);

/* Send len bytes (0..NB_MSGMAX) from the remote end lsn as one message;
   a RECEIVE takes a zero-length one whole, with 0 bytes.  Returns NB_OK,
   NB_RC_TOOMANY when the session's FNB_QMAX bytes would be passed, or
   three times as many with 2 counted for each message, or no far
   memory is left for them (nothing sent), NB_RC_ILLCMD for a len above
   NB_MSGMAX, NB_RC_SESSCLOSED, or NB_RC_BADLSN. */
u8 fnb_peer_send(u8 lsn, const void *buf, u16 len);

/* Receive at the remote end lsn into buf (size bytes): NB_OK with the
   next whole message, or NB_RC_INCOMPLETE with its first size bytes
   and the rest left for the next call, *len the bytes copied; the
   sending end's SEND completes with the message's last byte.
   NB_RC_PENDING when no message waits, NB_RC_SESSCLOSED when none ever
   will, NB_RC_BADLSN; *len 0 then. */
u8 fnb_peer_recv(u8 lsn, void *buf, u16 size, u16 *len);

/* Hang up the remote end lsn.  Returns NB_OK, NB_RC_SESSCLOSED when
   the session had ended, or NB_RC_BADLSN. */
u8 fnb_peer_hangup(u8 lsn);

/* Run fn(arg) inside every nba_poll call and every try of a wait-form
   command, never inside itself; NULL for none.  A test that runs a
   client and a server in one program pumps the server's loop from the
   client's waits, and calls the server only from the pump while one is
   set. */
typedef void (*fnb_pump_fn)(void *arg);
void fnb_pump(fnb_pump_fn fn, void *arg);

/* Counts for the checks at the end of a test: no-wait commands in
   progress, sets open (nba_open not yet matched by nba_close), names in
   the local name table, and session ends open on either side (remote
   ends included). */
u16 fnb_pending(void);
u16 fnb_sets(void);
u16 fnb_names(void);
u16 fnb_sessions(void);

/* HANG UPs of the program since fnb_reset on an LSN none of its open
   ends uses, refused with NB_RC_BADLSN.  A program that keeps to the
   provider's rules makes none: a session a command reported ended is
   gone, and its LSN may belong to another session by then. */
u16 fnb_badhangups(void);

/* RECEIVEs of the program since fnb_reset that were posted with the
   start of a message already waiting for them.  A real provider keeps
   no message that arrives while no RECEIVE is posted for it: NetBEUI
   has the sender send it again once one is, so each counts a message
   sent twice on the wire. */
u16 fnb_late(void);

#endif
