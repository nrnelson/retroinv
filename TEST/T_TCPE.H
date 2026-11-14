/*
 * T_TCPE.H - what the three source files of the T_TCPE test share: the
 * server and the bridge, the TCP clients, and the phases of T_TCPE3.C
 * and T_TCPE2.C.
 *
 * One program is every part of a TCP client's path to the server: the
 * clients are the simulated stack's peers (TSFAKE.H), the bridge
 * (BRIDGE.H) relays each of their connections over the simulated
 * provider (FAKENB.H) to the server (TNETB.H, CONN.H and the engine on
 * the scratch data directory).  Before each pass of the stack, the
 * bridge and the server, each client sends at most FEEDSZ bytes
 * of its requests and lets the bridge write at most FEEDSZ bytes to
 * it, and after the pass it reads them: the clients' streams are
 * interleaved, every line reaches the server in pieces, and the
 * responses back up behind slow clients.  Client k always holds the
 * bridge's slot k.  The clock never moves, so no session is ever idle.
 *
 * The test is split in three compilation units, T_TCPE.C (the server
 * and main), T_TCPE2.C (sessions at once, capped or not, ADMIN SHUTDOWN
 * and the server stopped) and T_TCPE3.C (the stack, the bridge, the
 * clients, and the same bytes over the bridge as over LOCAL), because
 * Watcom C 6.5 runs out of compiler memory on fewer; T_TCPE3.C needs no
 * engine, protocol or server header, T_TCPE.C no bridge header.
 */
#ifndef T_TCPE_H
#define T_TCPE_H

#define BRNAME          "INVTCP"        /* the bridge's, every session's
                                           station                      */
#define FEEDSZ          13      /* bytes a client sends, and takes, a
                                   pass: lines end inside pieces and
                                   across                               */
#define OUTSZ           768     /* bytes of each buffer above, NUL
                                   included                             */
#define EVSZ            160     /* bytes of the bridge's event log      */
#define NOW             1000UL  /* the clock, never moved               */

extern char *g_rx[TS_SLOTS];    /* what each client received, NUL-ended */
extern char *g_want[TS_SLOTS];  /* what each client must receive        */
extern char *g_script;          /* the request script                   */
extern char g_ev[EVSZ];         /* "k EVENT text;" for each bridge event
                                   since begin, OPEN without its text   */

/* Start a phase on the data in the scratch directory (a fresh one is
   made when there is none): a fresh provider and stack, the server
   (srv_begin) and the bridge.  Returns 1, else 0 with the server not
   started. */
int begin(const char *readonly);

/* Start the server on the data in the scratch directory, with the
   stations of the comma list readonly (kept by pointer) capped: 1, else
   0 with nothing started. */
int srv_begin(const char *readonly);

/* One pass of the server's loop, when it runs. */
void srv_run(void);

/* Stop the server, when it runs: the clients' sessions end, the bridge
   goes on. */
void srv_stop(void);

/* Stop the server if it runs and the bridge, close the stack, and
   check that nothing is left: every session that opened closed, no
   command, set, name or session end, no hang-up of a session the
   provider had removed. */
void finish(void);

/* A client connects, taken by slot k, and passes run until the bridge
   holds its session. */
void cli_open(u8 k);

/* Each client k with a non-NULL want[k] sends req[k] (NULL for
   nothing) in pieces, and passes run until it has
   received as many bytes as want[k] holds and, with end, its
   connection has ended and its slot listens again.  It must then have
   received exactly want[k], and its connection must have ended and its
   slot listen (end) or the connection still be open. */
void talk(const char *const *req, const char *const *want, int end);

/* talk for client k alone. */
void ask(u8 k, const char *req, const char *want, int end);

/* 1 when the first len bytes of slot k's areas (its transmit area,
   then its receive area) and the bounce buffer hold nothing but
   zeros. */
int wiped(u8 k, u16 len);

/* The phases of T_TCPE3.C and T_TCPE2.C. */
void test_same(const char *golden);
void test_four(void);
void test_shutdown(void);

#endif
