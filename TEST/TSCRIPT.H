/*
 * TSCRIPT.H - the request script of TEST/T_INV.SH, for the tests that
 * send it to the server end to end and compare the responses with that
 * script's golden file (TSCRIPT.C): the server's name, ADMIN's password
 * and the fixed clock those tests share, the transcripts they keep and
 * the runs of the script over a link.
 *
 * Kept apart from T_FILES.H so that only those tests link the script's
 * text into their near data.
 */
#ifndef TSCRIPT_H
#define TSCRIPT_H

#include "COMPAT.H"
#include "LINK.H"

#define TSC_SRV         "INVSRV"        /* the server's name            */
#define TSC_PW          "Retro1986"     /* ADMIN's password, once set   */
#define TSC_SIZE        600     /* bytes of the script, NUL included    */
#define TSC_OUTSZ       1024    /* bytes of a transcript                */

/* The fixed clock of the golden file's run, as the members of an
   eng_time or a link_clock: { TSC_CLOCK }. */
#define TSC_CLOCK       2026, 10, 8, 12, 0, 0

/* The responses of a session, each line ended by CR LF as INV /CMD
   writes it, NUL-ended; at most TSC_OUTSZ - 1 bytes in b. */
typedef struct {
    char *b;
    u16 n;
} tn_text;

/* The script into buf of TSC_SIZE bytes, its HELLO naming station (at
   most LINK_STALEN characters): its length.  It sets TSC_PW and logs in
   with it, and holds CR LF line ends but one bare LF, an empty first
   line, an over-long line, a lower-case command, and a request after
   BYE. */
u16 tsc_script(char *buf, const char *station);

/* The golden file's path into out of DOS_PATHMAX bytes: argv[2] when
   given and shorter, else INVCMD.TXT beside the program argv[0]. */
void tsc_golden(char *out, int argc, char **argv);

/* The line callback of the links and clients: append line and CR LF to
   the tn_text at arg; a line that does not fit fails a check and is
   refused (1). */
int tsc_take(void *arg, const char *line, u16 len);

/* Check that a and b hold the same bytes; else report the first byte
   that differs, under what. */
void tsc_same(const tn_text *a, const tn_text *b, const char *what);

/* Feed the n bytes of script to l with link_feed in small pieces, as
   INV /CMD feeds its input, so that lines end inside pieces and across
   them, the responses appended to t: the session must end at BYE
   (LINK_CLOSED), and the request after it get no response. */
void tsc_run(link *l, const char *script, u16 n, tn_text *t);

/* tsc_run over a LOCAL link with the clock TSC_CLOCK on the scratch
   directory, its engine's files removed first. */
void tsc_local(const char *script, u16 n, tn_text *t);

#endif
