/*
 * T_SRVSCR.H - what the two source files of the T_SRVSCR test share:
 * the arguments, the settings and the stubs of CONN and the serial
 * transport of T_SRVSCR.C, and the snapshots of T_SRVSC2.C.  A file
 * includes it after CONN.H, SRVCFG.H and TESTFW.H, with COMPAT_UART
 * defined before COMPAT.H.
 *
 * The test is split in two compilation units, T_SRVSCR.C (the stubs,
 * the keys, the closed screen, IDLE, TWO, TRACE, CONFIRM, the reopen
 * and main) and T_SRVSC2.C (the snapshots, FULL, the session in slot 5,
 * PORTS, MDA and the long uptime text), because Watcom C 6.5 runs out
 * of compiler memory on it as one file.
 */
#ifndef T_SRVSCR_H
#define T_SRVSCR_H

#define PATHMAX         96

extern char g_dir[PATHMAX];     /* the scratch directory                */
extern char g_gdir[PATHMAX];    /* the goldens' directory               */
extern char g_mdir[PATHMAX];    /* the message file's directory         */
extern int g_update;            /* -u: snap writes the goldens          */
extern srvcfg g_cfg;            /* the settings every screen opens with */

/* The state of the stubs: the sessions conn_info gives, the uptime
   conn_uptime gives, and the ports' facts tserial_info gives. */
extern proto_peer g_peers[CONN_MAX];
extern u16 g_npeers;
extern u32 g_uptime;
extern uart_info g_uinfo[UART_MAXPORT];

/* Set session i (slot slot) with its station, user, level, state and
   idle seconds; the slot then CONN_OPEN. */
void set_peer(u16 i, u8 slot, const char *sta, const char *user,
              u8 level, u8 state, u32 idle);

/* No session, every slot CONN_FREE, uptime 0, no shutdown, no port. */
void clear_conn(void);

/* Two sessions, two LISTENs, slots 3 and 4 free. */
void two_sessions(void);

/* Serve port i as port on chip, with each error count n. */
void set_port(u8 i, u8 port, u8 chip, u16 n);

/* Row r of the screen, trailing spaces removed, into t (VID_COLS + 1
   bytes). */
const char *row_text(u8 r, char *t);

/* Check that the adapter shows the shadow screen, and compare the
   screen with the golden name.SNP, or with -u write it there, when the
   screen is monochrome exactly when mono is 1. */
void snap(const char *name, int mono);

/* The checks of T_SRVSC2.C in their order (FULL, slot 5, PORTS, MDA,
   the long uptime), then the count of the snapshots compared, which
   only main calls, after every other check. */
#ifndef TF_PART
void test_sc2(void);
#endif

#endif
