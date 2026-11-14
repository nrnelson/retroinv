/*
 * RQERR.H - what the client tells the user about a failed request, and
 * what the screen does next; the refusal of an operation above the
 * user's access level.
 *
 * The policy is the same for every front end: the texts are the
 * client's own, chosen by the code of the rq_res and, for 203, by the
 * user's level.  Only 102 and 202 show the server's reason
 * (rq_res.text), its first letter in upper case, as the code and the
 * field cannot tell those reasons apart.  Nothing here draws, reads a
 * key or sends a request: a front end shows the text the way rqe.show
 * asks and acts as rqe.act says.
 *
 * The texts are messages of TUI.MSG (the MSG_R_TUI range), fetched
 * through MSG.H; their MT_ constants are here, for the screens and
 * tests to name as well.
 *
 * Every screen reads this header through TUI.H, but only TUI.C, the
 * login screen SRC/CLIENT/SCRLOGIN.C, whose refused HELLO ends the
 * program, and the Windows list window SRC/WIN/WLIST.C (which
 * TEST/WLISTT.C tests) need what follows rqe_text: rqe_class, rqe_deny
 * and the messages only they and rqe_text fetch.  A file sees that part
 * only when it defines RQERR_CLASS before the include, as Watcom C 6.5's
 * compiler memory holds every declaration a file reads.
 */
#ifndef RQERR_H
#define RQERR_H

#include "COMPAT.H"
#include "RQ.H"
#include "MSG.H"

/* ---------------------------------------------------------------------
 * Messages (MSG_R_TUI range) and the text of an error code.
 * --------------------------------------------------------------------- */
#define MT_NOTAVAIL     1       /* Not available in local mode          */
#define MT_NOTFOUND     2       /* The record no longer exists          */
#define MT_DUP          3       /* Already exists                       */
#define MT_PERM         4       /* Permission denied: ^1 the user level */
#define MT_RESTART      5       /* The list has changed; starting again
                                   at the top                           */
#define MT_DBERR        6       /* Database error ^1: ^2 (rqe_text)     */
#define MT_BADPW        8       /* Invalid user name or password        */
#define MT_DISABLED     9       /* Account disabled                     */
#define MT_BUSY         10      /* Other sessions are open (402)        */
#define MT_EXPIRED      11      /* Session expired                      */
#define MT_ENDED        13      /* The session has ended                */
#define MT_INTERNAL     14      /* Internal error ^1                    */

/* The client's short text for the code of a failed request, to follow a
   colon: its own for each storage error 300..304, "error n" for any
   other code, n in decimal and 0 for a negative code.  A msg result
   (rotating buffer). */
const char *rqe_text(int code);

/* The client's short text for the DOS error code (CF_E of COMPAT.H) of a
   file it reads or writes itself, to follow a colon or stand in
   parentheses: rqe_text's for too many open files, a file in use and a
   full disk, its own for a file or directory not found, access denied
   and a refused name (CF_EINVAL), "DOS error n" for any other.  A msg
   result (rotating buffer). */
const char *rqe_doserr(int code);

#ifdef RQERR_CLASS
/* ---------------------------------------------------------------------
 * Classifying a failed request.
 * --------------------------------------------------------------------- */
#define MT_REINDEX      7       /* how to rebuild the indexes           */
#define MT_MUSTCHG      12      /* Password change required             */
#define MT_LOST         34      /* Connection lost; a change may stand  */
#define MT_EIO          35      /* rqe_text: MT_EIO..MT_EINUSE in the   */
#define MT_EINDEX       36      /* order of RQ_EIO..RQ_EINUSE, then     */
#define MT_EFULL        37      /* error ^1 for any other code          */
#define MT_EHANDLES     38
#define MT_EINUSE       39
#define MT_EOTHER       40
#define MT_CFNOPATH     43      /* rqe_doserr's own texts               */
#define MT_CFACCESS     44
#define MT_CFNAME       45
#define MT_CFOTHER      46

/* How the text is shown (rqe.show). */
#define RQE_NONE        0       /* nothing: the request succeeded       */
#define RQE_LINE        1       /* one line, until the next key         */
#define RQE_BOX         2       /* a message box, until confirmed       */

/* What the screen does next (rqe.act). */
#define RQE_STAY        0       /* stays                                */
#define RQE_LOGIN       1       /* goes back to the login screen        */
#define RQE_QUIT        2       /* ends the program                     */
#define RQE_LOST        3       /* ends the program once the box is
                                   confirmed: the connection is lost    */

/* Characters rqe.text holds at most: a message, a line break and a
   second message, a longer pair cut. */
#define RQE_TEXTLEN     (2 * MSG_MAXLEN)

typedef struct {
    u8   show;                  /* RQE_NONE, RQE_LINE or RQE_BOX        */
    u8   act;                   /* RQE_STAY .. RQE_LOST                 */
    char text[RQE_TEXTLEN + 1]; /* what to show, '\n' breaking lines    */
} rqe;

/* Classify the outcome res of a request sent for a user of access level
   level (0 before login) into *e.  RQ_OK is RQE_NONE, RQE_STAY with text
   "", whatever res->closed says.  Then, in this order:
     RQ_ELOST                       box MT_LOST, RQE_LOST;
     RQ_ECLOSED, 501, or any other
     code with res->closed set      box MT_ENDED, RQE_QUIT;
     104, 200, 201, 204             line MT_NOTAVAIL, MT_NOTFOUND, MT_DUP,
                                    MT_RESTART;
     102, 202                       line res->text, the server's reason;
     203                            line MT_PERM naming level in
                                    decimal;
     300..304                       box MT_DBERR with the code in
                                    decimal and rqe_text's text; 301
                                    adds a line MT_REINDEX;
     401, 402, 403                  line MT_BADPW, MT_BUSY, MT_DISABLED;
     400, 404                       box MT_EXPIRED, MT_MUSTCHG, RQE_LOGIN
                                    (404: the login screen runs the
                                    forced password change);
     any other code                 box MT_INTERNAL with the code in
                                    decimal.
   Every case not given an action is RQE_STAY.  A negative code reads as
   0 where the text shows it.  The text is cut at RQE_TEXTLEN.  The texts
   are fetched with msg and msg_sub, so no msg result the caller holds
   survives the call. */
void rqe_class(rqe *e, const rq_res *res, u8 level);

/* The refusal of an operation that needs access level need, for a user
   of level level: NULL when level is at least need, else MT_PERM naming
   level in decimal, a msg_sub result (rotating buffer), as rqe_class
   gives for 203.
   Nothing is sent: a front end refuses early, and the server checks the
   level of every request regardless. */
const char *rqe_deny(u8 level, u8 need);
#endif

#endif
