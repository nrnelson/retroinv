/*
 * SRVCFG.H - the server's settings file, INVSERV.CFG (SRVCFG.C).
 *
 * Text lines "KEY=value", ended by CR LF or LF alone; keys are matched
 * without regard to case, spaces around '=' and at the ends of a line
 * are ignored, and a line that is empty or starts with ';' is a comment.
 * A Ctrl-Z (1Ah) ends the file.  Keys:
 *
 *   NAME=name        the server's NetBIOS name, 1..NB_NAMELEN - 1
 *                    characters other than spaces and control bytes,
 *                    not starting with '*'; letters are upper-cased
 *                    (default SRVCFG_DEFNAME)
 *   DATA=dir         the data directory, 1..ENG_MAXDIR characters other
 *                    than control bytes, with the separator that is
 *                    added when dir does not end in '\', '/' or ':' (as
 *                    INV's link_open_local adds it); default "DATA" and
 *                    DOS_DIRSEP
 *   READONLY=list    stations capped at the read level whatever the
 *                    user: 1..SRVCFG_ROMAX names of 1..ENG_STALEN
 *                    characters other than control bytes and ',',
 *                    separated by commas, spaces around each removed;
 *                    letters are upper-cased (default none)
 *   SESSIONS=n       NetBIOS sessions served at once, 0..CONN_NETMAX,
 *                    0 for no NetBIOS (default CONN_NETMAX)
 *   LISTENS=n        LISTENs kept posted, 1..SESSIONS (default
 *                    SRVCFG_DEFLISTENS, or SESSIONS when that is less)
 *   SERIAL=list      serial ports served: COM1, COM2, or both
 *                    separated by a comma, spaces around each removed,
 *                    letters in any case (default none)
 *   BAUD=n           their speed, a rate of COMPAT.H's UART_BAUDOK
 *                    (default UART_DEFBAUD)
 *   RTSCTS=0|1       RTS/CTS flow control on them (default 0)
 *   MAILBOX=dir      the mailbox directory (TMBOX.H), as DATA= (default
 *                    none)
 *   IDLE=n           minutes after which an idle session is closed,
 *                    0..SRVCFG_IDLEMAX, 0 for never (default
 *                    SRVCFG_DEFIDLE)
 *   LOG=path         the event log, 1..SRVCFG_LOGMAX characters other
 *                    than control bytes (default none)
 *   BIOS=0|1         draw the status screen through INT 10h only
 *                    (default 0)
 *   SNOW=0|1         wait for retrace when writing to a CGA (default 0)
 *
 * The file is read strictly: an unknown key, a bad value, a key given
 * twice, a line longer than SRVCFG_MAXLINE, a line holding a NUL byte, a
 * LISTENS= above SESSIONS= (reported at whichever of the two lines comes
 * later; with SESSIONS=0 every LISTENS= is), or a SESSIONS=0 with
 * neither SERIAL= nor MAILBOX= in the file, which would serve nobody
 * (reported at the SESSIONS= line), stops the reading at that line.
 * BAUD= and RTSCTS= without SERIAL= are read and have no effect.  A
 * missing file means every default.
 */
#ifndef SRVCFG_H
#define SRVCFG_H

#include "COMPAT.H"
#include "DBF.H"
#include "ENGINE.H"

#define SRVCFG_NAME     "INVSERV.CFG"
#define SRVCFG_MAXLINE  160     /* bytes of a line, CR LF excluded      */
#define SRVCFG_DEFNAME  "RETROINV"
#define SRVCFG_ROMAX    8       /* stations in READONLY=                */
#define SRVCFG_DEFLISTENS 4     /* LISTENS= when it is not given        */
#define SRVCFG_DEFIDLE  10      /* minutes                              */
#define SRVCFG_IDLEMAX  1440    /* minutes: a day, 1,572,480 ticks, so
                                   the idle timeout in ticks is a u32   */
#define SRVCFG_LOGMAX   64      /* characters of the LOG= path          */

typedef struct {
    char name[NB_NAMELEN];
    char data[ENG_MAXDIR + 1];  /* with its separator, for eng_open     */
    char readonly[SRVCFG_ROMAX * (ENG_STALEN + 1)];
                                /* "STA1,STA2", "" for none             */
    char log[SRVCFG_LOGMAX + 1];    /* "" for none                      */
    char mailbox[ENG_MAXDIR + 1];   /* as data, "" for none             */
    u16  idle;                  /* minutes, 0 for never                 */
    u16  baud;
    u8   sessions;              /* 0 for no NetBIOS                     */
    u8   listens;               /* 0 with no NetBIOS                    */
    u8   serial;                /* bit port - 1 for each port served    */
    u8   rtscts;
    u8   bios;
    u8   snow;
} srvcfg;

/* Every default. */
void srvcfg_default(srvcfg *c);

/* Read path into c, starting from the defaults.  Returns 0 with
   *badline 0 (also when the file does not exist); CF_EINVAL with
   *badline the number (from 1) of the line that stopped the reading;
   or the CF_E code of a read failure with *badline 0.  On failure c
   holds the defaults. */
int srvcfg_load(srvcfg *c, const char *path, u16 *badline);

#endif
