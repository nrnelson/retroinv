/*
 * INVTCP.H - INVTCP.EXE, the TCP bridge program (INVTCP.C, its main in
 * TCPMAIN.C).
 *
 *   INVTCP [/NAME:local] [/ALLOW:net]... server
 *
 * relays every TCP connection to BR_PORT over its own NetBIOS session to
 * the server named server, CALLing from local or, without /NAME:, from the
 * name BRIDGE.H's br_start chooses.  server and local are NetBIOS names as
 * txt_nbname_ok (COMPAT.H) takes them.  Each /ALLOW: adds a network to the
 * bridge's allow-list (BRIDGE.H), up to BR_ALLOWMAX: net is an address
 * a.b.c.d, decimal without leading zeros, or a network a.b.c.d/bits, the
 * address's first bits (1..32) counting.  INVTCP /? writes the usage and
 * exits with TCP_EXIT_OK.
 *
 * Start-up, in this order: the near heap (cmem_init); DOS 3.1 or later;
 * the command line; the critical-error handler (dos_crit_install),
 * which answers Fail (Retry where DOS allows no Fail); BREAK checking
 * off (dos_break_off); the NetBIOS provider; the TCP stack (ts_init),
 * which finds its settings file WATTCP.CFG itself; the emergency stop;
 * the bridge (br_start).  The emergency stop is an atexit handler,
 * registered after ts_init so that it runs before the stack's own,
 * however the program ends, the library's exit calls included: it stops
 * as F3 does (below), so that the provider never writes into memory
 * DOS has freed.  A failure is said on standard error, in fixed
 * English, and ends the program with its status, after the same stop.
 *
 * The loop polls the stack (ts_poll) and the bridge (br_poll) on a tick
 * count that goes on across midnight, and reads the keys; a pass with
 * nothing to do lets resident programs run (dos_idle).  F3 asks on
 * standard error, with the number of sessions open (br_sessions),
 * whether to stop, and the bridge goes on relaying; Y, N or Esc
 * answers, Y stopping, and no other key counts but F3, which asks
 * again, the event lines scrolling the question away.  The stop is at
 * once:
 * the bridge stops (resetting every connection), the stack is released
 * (ts_term) and, while the provider still owns NCBs it could write
 * into, INVTCP waits as nba_close (COMPAT.H) says; then the BREAK
 * setting and the critical-error handler are put back.  The bridge
 * never stops by itself, the server's absence included.
 *
 * Events go to standard output, one line each:
 *
 *   YYYY-MM-DD HH:MM:SS slot EVENT text
 *
 * slot 0 for INVTCP itself, whose events are START (text: the server's
 * name and BR_PORT) and STOP (text: the exit status); a connection's
 * slot number from 1, with the events of BRIDGE.H: OPEN, CALL, NOCALL,
 * CLOSE and REFUSE.  The words are fixed upper-case codes, never
 * translated, and no line ever holds a byte relayed.
 */
#ifndef INVTCP_H
#define INVTCP_H

/* Exit statuses of INVTCP.EXE; a number shared with INVSERV.EXE means
   the same there. */
#define TCP_EXIT_OK     0       /* stopped by F3, or the usage asked    */
#define TCP_EXIT_USAGE  1       /* usage error                          */
#define TCP_EXIT_DOSVER 3       /* DOS older than 3.1                   */
#define TCP_EXIT_NONB   5       /* no NetBIOS provider                  */
#define TCP_EXIT_NOMEM  6       /* no memory for the NCBs or the
                                   buffers                              */
#define TCP_EXIT_NAME   9       /* the local name could not be added    */
#define TCP_EXIT_NOPKT  10      /* no packet driver                     */
#define TCP_EXIT_STACK  11      /* the stack's settings file missing or
                                   giving no address for this station,
                                   the packet driver or the stack
                                   refused to start, the emergency stop
                                   could not be set up, or a slot could
                                   not listen                           */

/* The program as described above, from the command line argc, argv
   (argv[0] unused); returns its exit status, a TCP_EXIT_ code.  It may
   run again once it has returned, as the tests run it over the
   simulated stack and provider; each run that starts the stack
   registers the emergency stop once more. */
int invtcp_main(int argc, char **argv);

#endif
