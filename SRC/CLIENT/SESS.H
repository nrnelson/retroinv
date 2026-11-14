/*
 * SESS.H - opening and keeping a client's session (SESS.C): the station,
 * HELLO and VER, LOGIN and its outcome, PASSWD with its local checks,
 * and STATUS's record counts.
 *
 * Every front end runs the same steps and the same policy through these
 * calls; how a step's outcome is shown, and the keys and dialogs around
 * it, are the caller's.  Nothing here draws, reads a key, waits or
 * keeps any state between calls; each call sends its requests over the
 * link it is given, through rq_send, and reports the failure of a
 * request in the caller's rq_res for the caller's own error handling
 * (RQERR.H).
 *
 * The order of a session: sn_station once, when the link has opened, or
 * before the open for a backend opened with the station (MBOX);
 * sn_hello; then sn_login until it gives SN_IN or SN_MUSTCHG, or ends
 * the session; after SN_MUSTCHG nothing but sn_passwd until it gives
 * SN_PW_DONE, or sn_hello to log out (meanwhile the server answers 404
 * to every command but HELLO, PING, VER, BYE, LOGIN and PASSWD);
 * sn_status whenever the counts are wanted.  Logging out is sn_hello
 * again, which ends the login on the session.
 *
 * Passwords.  None is ever kept here: rq_send wipes each request line
 * it sent, and no call copies a password elsewhere.  sn_login wipes the
 * caller's password buffer whole (SN_PWSZ bytes) before it returns,
 * whatever the outcome, so that a front end asks for the password again
 * at each attempt; sn_passwd only reads its three, which the caller
 * keeps for another try after a refusal and wipes (txt_wipe) when its
 * dialog closes, and on the program's way out while one is open.
 */
#ifndef SESS_H
#define SESS_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"

/* ---------------------------------------------------------------------
 * The station and the greeting.
 * --------------------------------------------------------------------- */

/* The station a session names in HELLO, into out (LINK_STALEN + 1
   bytes, the name cut to fit): net when it is not NULL (net is NULL or
   a nonempty name: on a NET link the name it called from,
   link_netname), else cfgsta when it is not "", else the DOS machine
   name when there is a nonempty one (dos_machine_name), else
   CFG_DEFSTATION.  Returns 1 when it fell back to CFG_DEFSTATION, else
   0, also for a name given that equals it. */
int sn_station(char *out, const char *net, const char *cfgsta);

/* Send HELLO station, then, while ver is "", VER, whose +OK text goes
   into ver (RQ_TEXTLEN + 1 bytes), so the version is asked for once per
   run.  The station goes as one word, quoted when it has to be (RQ.H);
   the server refuses one that is empty, starts or ends with a space or
   holds a control byte with 102 FIELD=STATION, and VER is then not
   sent.  A HELLO refused with RQ_EUNKNOWN, as only a garbled line is,
   goes once more.  Returns RQ_OK, or the code of the request that
   failed, its outcome in res, with ver unchanged. */
int sn_hello(link *l, const char *station, char *ver, rq_res *res);

/* ---------------------------------------------------------------------
 * Logging in.
 * --------------------------------------------------------------------- */

/* Bytes of the password buffer sn_login takes, and wipes. */
#define SN_PWSZ         (LINK_PWMAX + 1)

/* The access levels LOGIN gives. */
#define SN_LVLMIN       1
#define SN_LVLMAX       3

/* Outcomes of sn_login. */
#define SN_IN           0       /* logged in                            */
#define SN_MUSTCHG      1       /* logged in, but the password must be
                                   changed first: sn_passwd             */
#define SN_REFUSED      2       /* 401 or 403, the session open: a wrong
                                   user name or password, or a disabled
                                   account, which the server counts as
                                   a failed login                       */
#define SN_FAILED       3       /* any other failure, the session open  */
#define SN_TOOMANY      4       /* 401 or 403 that ended the session:
                                   the LINK_MAXFAIL-th failed login     */
#define SN_ENDED        5       /* the session ended otherwise: it had
                                   ended before, the link was lost, the
                                   server is shutting down, or a storage
                                   or server failure was the
                                   LINK_MAXFAIL-th failed login         */

/* Send LOGIN user pw and take its outcome (SN_).  pw is a buffer of
   SN_PWSZ bytes holding at most LINK_PWMAX characters, wiped whole
   before the call returns, on every outcome.  With SN_IN, *level is the
   level of LEVEL= (SN_LVLMIN when that is not one digit SN_LVLMIN..
   SN_LVLMAX) and name (LINK_FULLNAMELEN + 1 bytes) the full name of
   NAME=, "" without one.  With SN_MUSTCHG, *level is set alike and name
   is "", as such a response holds none.  With any other outcome *level
   and name are left as they were.  res holds the outcome of the
   request whatever it is. */
int sn_login(link *l, const char *user, char *pw, u8 *level, char *name,
             rq_res *res);

/* ---------------------------------------------------------------------
 * Changing the password.
 * --------------------------------------------------------------------- */

/* Outcomes of sn_passwd.  SN_PW_NOMATCH, SN_PW_INVALID and SN_PW_SAME
   are found here, in that order, and send nothing. */
#define SN_PW_DONE      0       /* changed                              */
#define SN_PW_NOMATCH   1       /* the new password and its repetition
                                   differ                               */
#define SN_PW_INVALID   2       /* the new one has fewer than LINK_PWMIN
                                   or more than LINK_PWMAX characters,
                                   or a byte outside 21h..7Eh           */
#define SN_PW_SAME      3       /* the new one equals the old           */
#define SN_PW_OLDWRONG  4       /* 401 with the session open: the old
                                   password is wrong, which the server
                                   does not count as a failed login     */
#define SN_PW_FAILED    5       /* any other failure                    */

/* Check the new password nw against its repetition rep and the old one
   old, and when the checks pass send PASSWD old nw; the outcome (SN_PW_).
   res holds the response when the request was sent (SN_PW_DONE,
   SN_PW_OLDWRONG, SN_PW_FAILED) and is left as it was otherwise.  The
   three passwords are only read. */
int sn_passwd(link *l, const char *old, const char *nw, const char *rep,
              rq_res *res);

/* ---------------------------------------------------------------------
 * The record counts.
 * --------------------------------------------------------------------- */

/* The counts of sn_counts.n, as STATUS names them in its K lines:
   ITEMS=, LOCS= and CATS=. */
#define SN_C_ITEMS      0
#define SN_C_LOCS       1
#define SN_C_CATS       2
#define SN_NCOUNTS      3

typedef struct {
    char n[SN_NCOUNTS][TXT_U32LEN + 1];  /* the count as the server
                                   sends it, cut to TXT_U32LEN
                                   characters; "?" when it sends none   */
} sn_counts;

/* Send STATUS and take its record counts into c, every count "?" before
   the request is sent.  Returns RQ_OK, or the failure, its outcome in
   res, c then holding the counts read before it. */
int sn_status(link *l, sn_counts *c, rq_res *res);

#endif
