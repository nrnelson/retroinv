/*
 * T_INVTST.H - what the two source files of the T_INVTST test share:
 * the canned server's sessions and answers, the scripts' runs and two
 * of their strings (T_INVTST.C), used by the serial and mailbox checks
 * of T_INVTS2.C.
 *
 * The test is split in two compilation units, T_INVTST.C (the canned
 * NetBIOS server, the NetBIOS checks and main) and T_INVTS2.C (the
 * serial and mailbox checks), because Watcom C 6.5 runs out of
 * compiler memory on one.
 */
#ifndef T_INVTST_H
#define T_INVTST_H

#include "COMPAT.H"

#define NSESS           4       /* server sessions, one LISTEN each     */
#define SV_LINESZ       128     /* a request line, a transcript line    */
#define BIG_LINES       31      /* data lines of the BIG response, which
                                   just passes NB_MSGMAX (T_INVTST.C)   */
#define BIG_XS          60      /* the x's of each                      */
#define BIG_HEAD        5       /* "R nn " before the x's of each       */
#define BIG_ROW         (BIG_HEAD + BIG_XS + 2)  /* a data line, CR LF  */
#define BIG_LEN         (7 + BIG_LINES * BIG_ROW + 3)  /* the response  */
#define SV_OUTSZ        BIG_LEN /* the most output a session queues     */

/* Session states of the canned NetBIOS server. */
#define SV_LISTEN       0
#define SV_OPEN         1
#define SV_DONE         2

/* A session of the canned server: the request line being assembled and
   the output not yet sent. */
typedef struct {
    u8   state;
    u8   lsn;
    u8   rx;                    /* a RECEIVE is posted                  */
    u8   bye;                   /* BYE (or DROP) ended the session      */
    u16  llen;
    u16  opos;
    u16  olen;
    char line[SV_LINESZ];
    char out[SV_OUTSZ];
} srv;

extern srv g_srv[NSESS];        /* the canned server's sessions, each
                                   test's in turn                       */
extern char g_scr[];            /* the script, dir's S.SCR              */
extern char g_out[];            /* the transcript, dir's T.TXT          */
extern const char k_calla[];    /* the script line that calls RINVA     */
extern const char k_badline[];  /* INVSCR.C's reason for a bad line     */

/* Answer the request line of session v, queueing the output: BYE with
   "+OK BYE", LIST and BIG with data responses, HELLO and LOGIN with
   "+OK", ECHO with its argument, PASSWD with an empty line, anything
   else with error 100; DROP ends the session with no reply. */
void answer(srv *v, const char *line);

/* Take the n bytes at piece of session v's input, answering each whole
   line. */
void take(srv *v, const char *piece, u16 n);

/* Write the script with CR LF ends: when longp is not NULL, a line of
   longp and a line too long for INVSCR.C, then the NULL-ended lines. */
void write_script(const char *longp, const char *const *lines);

/* Run the script against the server named server (NULL for none), the
   transcript going to g_out; returns the result, *line the line that
   failed. */
int drive(const char *server, u16 *line);

/* 1 when a line of the transcript is text (whole 1) or holds it (whole
   0), else 0. */
int seen(const char *text, int whole);

/* T_INVTS2.C. */
void test_serial(void);
void test_mbox(void);

#endif
