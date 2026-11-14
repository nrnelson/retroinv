/*
 * RQ.H - building protocol requests and reading their responses, for
 * the screens of the full-screen client.
 *
 * A screen builds one request line in an rq: the command words, then
 * positional words, then KEY=value pairs, each value quoted and escaped
 * as the protocol's tokenizer expects when it has to be.  rq_send sends
 * it over a link and turns the response lines into calls of a line
 * callback, with the values already unquoted and unescaped, and fills
 * an rq_res with the outcome.  Nothing here knows any command; the
 * screens do.
 *
 * Quoting.  A value is sent bare unless it is empty or holds a space, a
 * TAB, '"', '\', '=', a byte below 20h or 7Fh; it is then sent in quotes,
 * with '"' as \", '\' as \\, LF as \n and every other byte below 20h,
 * and 7Fh, as \xHH.  Bytes 80h..FFh (CP437) pass unchanged.
 *
 * Decoding.  K, F and R lines are split into values as the protocol's
 * tokenizer splits a request (spaces, quotes, the four escapes); a K
 * line gives the key and the value of its KEY=value.  The text of an L
 * or H line starts after exactly one space following the letter (so
 * leading spaces of memo text are kept; "L" alone is an empty line)
 * and is unescaped (\\ and \xHH) but never split.  Data lines arrive
 * dot-stuffed and are unstuffed first.  A decoded value never holds a
 * NUL: \x00 cannot be sent, so a line that would decode to one is cut
 * there.
 *
 * Passwords travel in request lines: rq_send wipes the line it sent
 * before returning, and callers wipe their own copies.
 */
#ifndef RQ_H
#define RQ_H

#include "COMPAT.H"
#include "LINK.H"

/* ---------------------------------------------------------------------
 * Limits.
 * --------------------------------------------------------------------- */

/* Values kept from one F, R or K line; later values are dropped and
   counted in rq_line.over.  A K line has two values, and an F or R line
   one per field of its FIELDS= list (at most LINK_MAXFSPEC characters,
   RQ.C checks that they fit) or of the command's default list. */
#define RQ_MAXVALS      16

/* Characters of the +OK or -ERR text kept in rq_res.text, FIELD=
   excluded: one screen line.  A longer text is cut; a value cut that
   way (a full name whose control bytes travel as \xHH, for example) is
   not returned by rq_okval. */
#define RQ_TEXTLEN      (VID_COLS - 1)

/* ---------------------------------------------------------------------
 * Result codes.
 * --------------------------------------------------------------------- */

/* The protocol's error numbers (the error table of the protocol
   description), as rq_res.code reports them. */
#define RQ_OK           0
#define RQ_ESYNTAX      100
#define RQ_EUNKNOWN     101
#define RQ_EARG         102     /* rq_res.field names the field         */
#define RQ_ETOOLONG     103     /* also: the request did not fit        */
#define RQ_ENOTAVAIL    104
#define RQ_ENOTFOUND    200
#define RQ_EDUP         201
#define RQ_ECONSTR      202
#define RQ_EPERM        203
#define RQ_ECURSOR      204
#define RQ_EIO          300
#define RQ_EINDEX       301
#define RQ_EFULL        302
#define RQ_EHANDLES     303
#define RQ_EINUSE       304
#define RQ_ENOLOGIN     400
#define RQ_EBADPW       401
#define RQ_EBUSY        402
#define RQ_EDISABLED    403
#define RQ_EMUSTCHG     404
#define RQ_EINTERNAL    500     /* also: a response rq_send cannot read */
#define RQ_ESHUTDOWN    501

/* Codes of the client itself, outside the protocol's range. */
#define RQ_ECLOSED      1       /* the session had ended: nothing was
                                   sent, or the response ended it       */
#define RQ_ESTOP        2       /* the line callback stopped the
                                   response; the rest was dropped       */
#define RQ_ELOST        3       /* the connection to the server was
                                   lost (LINK_ELOST)                    */

/* ---------------------------------------------------------------------
 * Building a request.
 *
 * A part that does not fit in LINK_MAXLINE bytes is left out whole and
 * sets over; rq_send then sends nothing and reports RQ_ETOOLONG.  The
 * rq_try forms instead leave the request unchanged and return 0, so a
 * caller can split a long change over several requests.
 * --------------------------------------------------------------------- */
typedef struct {
    char line[LINK_MAXLINE + 1];    /* the request, NUL-terminated      */
    u16  len;                   /* bytes in line                        */
    u8   over;                  /* a part was left out                  */
} rq;

/* Start a request with words, sent as given (e.g. "ITEM LIST"). */
void rq_begin(rq *q, const char *words);

/* Add a positional word, quoted when it has to be.  Positional words
   must all come before the first pair. */
void rq_word(rq *q, const char *w);

/* Add KEY=value.  rq_pairn takes a value of n bytes that may hold LF
   (memo text); rq_pairu a number in decimal. */
void rq_pair(rq *q, const char *key, const char *val);
void rq_pairn(rq *q, const char *key, const char *val, u16 n);
void rq_pairu(rq *q, const char *key, u32 v);

/* rq_pair and rq_pairn, but 1 if the pair was added and 0, with the
   request unchanged, if it would not fit. */
int rq_trypair(rq *q, const char *key, const char *val);
int rq_trypairn(rq *q, const char *key, const char *val, u16 n);

/* Bytes the n-byte value val takes on the line, quotes included; a
   value that needs quotes and does not fit on a line gives
   LINK_MAXLINE + 1. */
u16 rq_cost(const char *val, u16 n);

/* Bytes still free in the request. */
u16 rq_room(const rq *q);

/* ---------------------------------------------------------------------
 * Reading a response.
 * --------------------------------------------------------------------- */

/* One decoded data line.  kind is the line's letter: 'K' (v[0] the key,
   v[1] the value), 'F' (the field names), 'R' (the values, in the order
   of the F line), 'L' (v[0] the text, n = 1), 'H' (as L), 'P' (n = 0).
   C lines and K ERROR= lines are not passed on; they go into rq_res.
   The strings belong to rq_send and last until the callback returns. */
typedef struct {
    char kind;
    u16  n;                     /* values in v                          */
    u16  over;                  /* values dropped beyond RQ_MAXVALS     */
    char *v[RQ_MAXVALS];
} rq_line;

/* Receives each data line; a nonzero return stops the response
   (RQ_ESTOP). */
typedef int (*rq_fn)(void *arg, const rq_line *ln);

typedef struct {
    int  code;                  /* RQ_OK, a protocol error, RQ_E code   */
    u8   data;                  /* the response was +DATA               */
    u8   closed;                /* the session has ended                */
    char text[RQ_TEXTLEN + 1];  /* text after "+OK " or "-ERR nnn ", from
                                   the response only: never any part of
                                   the request (which may hold a
                                   password)                            */
    char field[LINK_NAMELEN + 1];   /* FIELD= of -ERR, else ""          */
    char more[LINK_MAXCURSOR + 1];  /* cursor of a C line, else ""      */
} rq_res;

/* Send q over l and read its response: data lines go to fn (which may
   be NULL), the outcome to res.  fn must not call rq_send: the decoded
   line and the link are busy until rq_send returns, and such a call
   gets RQ_EINTERNAL without sending anything.  A +DATA block that ends
   with K ERROR=n reports n, after the lines before it have been
   delivered.  A lost link is RQ_ELOST, whatever came before it, and
   sets res->closed as an ended session does.
   Returns res->code.  The request line is wiped before returning. */
int rq_send(link *l, rq *q, rq_fn fn, void *arg, rq_res *res);

/* 1 while rq_send is decoding a response, when it refuses any other
   request: a key wait reached from a line callback (a printer's question
   inside a report) reads it to send nothing.  Set by rq_send only. */
extern u8 rq_busy;

/* The value of KEY=value in the text of a +OK response (ID=, LEVEL=,
   NAME=, MUSTCHG=), unquoted, into buf of size bytes; 1 if found, else
   0 with buf "". */
int rq_okval(const rq_res *res, const char *key, char *buf, u16 size);

#endif
