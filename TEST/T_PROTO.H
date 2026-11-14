/*
 * T_PROTO.H - what the source files of the T_PROTO test share: the
 * output sink, the engine, the host and the session under test, the
 * number and escape readers of the script lines, and the directives of
 * T_PROTO2.C and T_PROTO3.C.
 *
 * The test is split in three compilation units, T_PROTO.C (the script
 * runner, the requests and their checks, main), T_PROTO2.C (the
 * directives that set up the data, the files and the host) and
 * T_PROTO3.C (the fake ticket store and its directives), because
 * Watcom C 6.5 runs out of compiler memory on fewer.
 */
#ifndef T_PROTO_H
#define T_PROTO_H

#include "COMPAT.H"
#include "ENGINE.H"
#include "PROTO.H"

#define SCRLINE         640     /* script line, with room for over-long
                                   requests                            */

/* The output of one request: at most a full page, which is "+DATA",
   the F line and PROTO_PAGEMAX R lines of at most PROTO_MAXRESP bytes,
   the C line ("C " and a cursor) and ".", each with CR LF; any other
   response is at most PROTO_MAXUNPAGED bytes. */
#define OUTMAX          (7 + (PROTO_PAGEMAX + 1) * (PROTO_MAXRESP + 2) + \
                         2 + ENG_CURSORLEN + 2 + 3)

/* The output sink: an in-memory buffer of one response. */
typedef struct {
    char *buf;                  /* OUTMAX bytes from malloc             */
    u16  len;                   /* bytes written                        */
    u16  pos;                   /* bytes matched so far                 */
    int  over;                  /* a write did not fit                  */
    int  bad;                   /* a line failed: skip the rest         */
    u16  room;                  /* @ROOM, 0 for unlimited               */
} sink;

extern sink g_snk;
extern proto_out g_out;
extern eng *g_e;
extern eng_time g_now;
extern proto_host g_host;
extern proto_session g_sess;

/* Count one check. */
void count_check(void);

/* Count a failure and report it with the script's name and line: what,
   then the expected text a and the text found b when not NULL, then the
   last request. */
void fail_here(const char *what, const char *a, const char *b);

/* The sink's write function: the len bytes at buf into the output, or
   1, writing nothing, when they do not fit. */
int sink_write(void *arg, const char *buf, u16 len);

/* At most max decimal digits at p into *v; returns how many there
   were. */
u16 scan_digits(const char *p, u16 max, u32 *v);

/* A decimal number of 1 to 9 digits at *p into *v, *p moved past it; 0
   if there is none, or a byte other than a space or the end follows
   it. */
int number(char **p, u32 *v);

/* A decimal number of 0..65535 that is the whole of arg into *v; 0 if
   arg is not one. */
int set_count(char *arg, u16 *v);

/* Decode \\ and \xHH in the text at p in place, ending it with '\0',
   its length into *len; 0 for a bad \x escape and, when strict, for a
   '\' that begins neither escape (otherwise it stands for itself). */
int decode(char *p, u16 *len, int strict);

/* Reset the host and the directives' state for a new script: no
   read-only stations, other sessions, host STATUS lines, peers or stop
   function, and no @FAULT. */
void directives_reset(void);

/* The directive name (with its '@') and its argument arg (NULL for
   none) of T_PROTO2.C; 0 if it is not one of them or not valid. */
int directive(const char *name, char *arg);

/* A "=" line of @WRITE: its text, decoded, and CR LF; 0 outside
   @WRITE or for a bad line. */
int write_line(char *line);

/* End @WRITE's file, if one is open. */
void write_end(void);

/* T_PROTO3.C: empty the fake ticket store and take it from the host,
   whose tick goes back to 0; and its directives @TICKETS, @MBOX,
   @STORE and @TICK, as T_PROTO.C lists them, 0 if name is not one of
   them or not valid. */
void tickets_reset(void);
int tickets_directive(const char *name, char *arg);

#endif
