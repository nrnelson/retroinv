/*
 * MEMOIO.H - the notes of a record as a client holds them, loaded and
 * saved over the link (MEMOIO.C), and as one text (MIOTEXT.C).
 *
 * Every client edits the memo of an ITEM, LOC or MAINT record within
 * the same limits and sends it in the same requests; MEMOIO.C does that
 * with no screen and no key, and the client draws and edits the lines
 * (INV's memo editor, MEMO.C of FORM.H).
 *
 * The limits are those of FMODEL.H.  The lines sit in one far block of
 * MEMO_MAXLINES slots of MEMO_COLS bytes.  The mio keeps their lengths,
 * and the bytes the memo takes as stored (its characters plus CR LF
 * between lines), so neither needs a read of the block.  A line holds
 * at most MEMO_COLS bytes, each 20h..FFh but 7Fh; the memo holds at
 * most MEMO_MAXLINES lines and LINK_MAXMEMO bytes as stored.  Loading
 * keeps the bytes and the line length, and mio_check says whether a
 * memo passes the other two; an editing client keeps all four (mio_put
 * and mio_insert take what the caller gives them).
 *
 * Loading: "<table> NOTES <id>" sends the memo as L lines, which
 * rq_send unescapes.  A TAB is expanded to spaces up to the next
 * multiple of MEMO_TAB, any other byte below 20h and 7Fh becomes '?',
 * and a line longer than MEMO_COLS is split into lines of MEMO_COLS;
 * each of these marks the memo changed, so that it is rewritten only
 * when the user saves it.  A text of the client's own (mio_set_text) is
 * taken in the same way.
 *
 * Saving: trailing spaces and trailing empty lines are dropped, and the
 * lines go joined by \n, as many in each request as fit.  A memo that
 * fits one request is sent as "<table> SET <id> NOTES=..." (an empty
 * memo is NOTES="").  A longer one is staged on the server and written
 * whole: "<table> NOTES <id> NEW=..." with the leading lines, then
 * "<table> NOTES <id> PART=..." requests with further lines, the last
 * lines going as END=, which makes the stage the memo.  A line takes at
 * most 2 * MEMO_COLS bytes on the wire (only '"' and '\' need escapes in
 * such text), so every line fits one request on its own; the build
 * fails otherwise.
 *
 * Either way the save is one change on the server, which keeps the
 * memo it had until that change: the memo written to new blocks of the
 * memo file (memo files are append-only, so the file grows by that much
 * until PACK reclaims the old blocks) and one audit row for the field
 * NOTES with the value MEMO.
 */
#ifndef MEMOIO_H
#define MEMOIO_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "FMODEL.H"

#define MEMO_SLOT       (MEMO_COLS + 1) /* a line with its NUL          */

/* Texts of a memo's state (mio_check) and of a save that failed
   part-way, messages of MEMO.MSG. */
#define MM_CHANGED      302     /* tabs, control bytes or long lines    */
#define MM_TRUNC        303     /* the server cut the memo short        */
#define MM_TOOMANY      304     /* more than ^1 (MEMO_MAXLINES) lines   */
#define MM_PARTIAL      307     /* a save stopped after ^1 of ^2 lines,
                                   the stored memo unchanged            */
#define MM_TOOLONG      309     /* longer than LINK_MAXMEMO once loaded */
#define MM_MAYBE        310     /* lost as the memo was written         */
#define MM_OLDSRV       311     /* a server that cannot stage a memo    */

/* A memo.  The client reads n, bytes and len and changes the lines
   only through the functions below; the rest is MEMOIO.C's. */
typedef struct {
    cmem_blk *b;                /* the lines (mio_open)                 */
    u16  n;                     /* lines; after a load also those past
                                   MEMO_MAXLINES, which are not kept    */
    u16  bytes;                 /* bytes as stored, of all n lines      */
    u8   len[MEMO_MAXLINES];
    u8   changed;               /* a TAB, control byte or split         */
    u8   trunc;                 /* K TRUNCATED=1                        */
    char line[MEMO_COLS];       /* the line being loaded                */
    u8   ll;                    /* bytes in line                        */
} mio;

/* Give m its block of lines, holding no line: RQ_OK, or RQ_EINTERNAL
   with nothing held when memory has run out. */
int mio_open(mio *m);

/* Free the block of m. */
void mio_close(mio *m);

/* Load the memo of record id of table ("ITEM", "LOC" or "MAINT") into
   m, open, in place of what it held.  RQ_OK, or the code of rq_send
   with res holding the response; m then holds the lines that came. */
int mio_load(mio *m, link *l, const char *table, const char *id,
             rq_res *res);

/* The line count of a memo, by loading it with nothing kept, with the
   response in res as rq_send fills it; 0 lines on a failure.  RQ_OK or
   a code. */
int mio_count(link *l, const char *table, const char *id, u16 *lines,
              rq_res *res);

/* What a load (or mio_set_text) made of m, as the message to show: the
   first that holds of MM_TOOMANY, more than MEMO_MAXLINES lines came (n
   is then cut to MEMO_MAXLINES), MM_TOOLONG, bytes passes LINK_MAXMEMO,
   MM_TRUNC, the server cut the memo short, and MM_CHANGED, the load
   changed a byte or split a line; else 0.  A client shows a memo of the
   first three read-only, as a save would cut it. */
u16 mio_check(mio *m);

/* Line i (below n) into lb of MEMO_SLOT bytes, with its NUL. */
void mio_get(const mio *m, u16 i, char *lb);

/* Make line i (below n) the len bytes at lb, len at most MEMO_COLS;
   bytes follows. */
void mio_put(mio *m, u16 i, const char *lb, u16 len);

/* Open an empty line at i (at most n, n below MEMO_MAXLINES), the lines
   from i on moving down; bytes counts its line break. */
void mio_insert(mio *m, u16 i);

/* Remove line i (below n), the lines after it moving up; bytes follows. */
void mio_delete(mio *m, u16 i);

/* Make m, open, hold no line. */
void mio_empty(mio *m);

/* Add the n bytes at s to m as one line, as a load takes the server's
   lines. */
void mio_add_line(mio *m, const char *s, u16 n);

/* Send m as the memo of record id of table, of its first MEMO_MAXLINES
   lines at most (the lines it holds): *total receives the number of
   lines worth sending, *done how many reached the server.  RQ_OK, or
   the code of the request that failed, with res holding its response.
   A refused request leaves the server the memo it had.  When the link
   was lost with the request that writes the memo (the SET, or the
   END= of a staged save) sent, the server may have written it, and
   *done is *total.  A save again starts over. */
int mio_save(mio *m, link *l, const char *table, const char *id,
             u16 *done, u16 *total, rq_res *res);

/* The message a client shows of a save that failed with rc
   (mio_save's done, total and res), before the failure itself, or 0
   for none: MM_OLDSRV when the server refused NEW= (an older server,
   which saves only a memo that fits one request; nothing written),
   then shown in the failure's place; MM_MAYBE when the link was lost
   with the request that writes the memo sent; MM_PARTIAL, of done and
   total lines, when lines had reached the server's stage. */
u16 mio_failmsg(int rc, u16 done, u16 total, const rq_res *res);

/* ---------------------------------------------------------------------
 * The memo as one text (MIOTEXT.C), for a client that edits it so.
 * --------------------------------------------------------------------- */

/* The lines of m as one text, of its first MEMO_MAXLINES lines at most,
   joined by CR LF, into out of outsz bytes (at least 1) with a NUL; a
   line or break that does not fit is cut there.  A memo of bytes no
   more than LINK_MAXMEMO takes bytes + 1. */
void mio_text(const mio *m, char *out, u16 outsz);

/* Make text t the lines of m, open, as a load takes the server's lines
   (mio_add_line, trunc cleared); a line ends at CR LF, a lone CR or a
   lone LF, and an empty t is no line.  mio_check then judges it. */
void mio_set_text(mio *m, const char *t);

#endif
