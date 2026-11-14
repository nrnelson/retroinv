/*
 * T_SCRH.H - the harness of the screen tests T_SCRE and T_SCRR
 * (TEST/T_SCRH.C): key scripts run against screens of the full-screen
 * client, their snapshots compared with goldens.
 *
 * Each program (TEST/T_SCRE.C, TEST/T_SCRR.C) gives its name, its table
 * of scripts and the commands it starts, and links only the screens it
 * runs.  T_SCRH.C has the rest, main included, except the seeding of
 * the data before each script, which T_SCRH2.C does; T_SCRH.C includes
 * TESTFW.H in full, the others with TF_PART.  See T_SCRH.C for what a
 * run does.
 */
#ifndef T_SCRH_H
#define T_SCRH_H

#include "COMPAT.H"
#include "LINK.H"

#define TH_PATHMAX      96      /* a directory prefix or a file path    */
#define TH_SEEDITEMS    45      /* items of the common seed, IDs from 1 */

/* Script flags. */
#define SF_RO           0x01    /* no request may change anything       */
#define SF_MONO         0x02    /* run again on a monochrome screen     */
#define SF_HOME         0x04    /* the home screen waits for a key      */
#define SF_PRN          0x08    /* print jobs go to a capture file      */
#define SF_EXIT         0x10    /* it ends inside a screen (tui_exit)   */
#define SF_FF           0x20    /* the viewer shows a crafted file      */
#define SF_NET          0x40    /* the link counts as NET (tui_s.conn)  */

/* Seed sets added for a script (seed_sets of T_SCRH2.C). */
#define SD_ASSY         0x01    /* assemblies of items 1 and 20         */
#define SD_MAINT        0x02    /* maintenance records                  */
#define SD_LOAN         0x04    /* loans of items 5, 6, 11 and 17       */
#define SD_BOM          0x08    /* item BOM_TOP holding all the others  */
#define SD_GONE         0x10    /* item 27 disposed of                  */
#define BOM_TOP         40      /* below TH_SEEDITEMS - 4 (seed_sets)   */

#define TH_MAXCMDS      5
#define TH_NAMESZ       9       /* a script's name and its NUL          */

/* A script's row; row i of th_scripts is the script named th_name less
   its "T_", then i + 1 (SCRE1, SCRE2, ...), so a row holds no name. */
typedef struct {
    u8   user;                  /* AD, OP or RD: the user it logs in as */
    u8   cmd[TH_MAXCMDS];       /* commands the home screen starts in
                                   turn, ended by 0                     */
    u8   flags;                 /* SF_                                  */
    u8   seed;                  /* SD_: seed sets added for the script  */
    u16  failn;                 /* answer this request (from 1 after the
                                   login) with fail, or 0               */
    const char *fail;           /* NULL: the session ends there instead */
} script;

/* The users of the seeded data, as a script row names them; T_SCRH.C
   holds their names and passwords. */
#define AD              0       /* ADMIN, level 3                       */
#define OP              1       /* OPER, level 2                        */
#define RD              2       /* READER, level 1                      */

/* A script some of whose far allocations fail (cmem_fault), counted
   from 1 after the login; a NULL name ends the table. */
#define TH_MAXFAULTS    2

typedef struct {
    const char *name;
    u16  n[TH_MAXFAULTS];       /* allocations that fail, 0 for none    */
} memfault;

/* Given by each program. */
extern const char th_name[];            /* "T_SCRE"                     */
extern const script th_scripts[];
extern const u16 th_nscripts;
extern const memfault th_memfaults[];

/* The screen of command cmd, started from the home screen; the command
   to go on with, as a tui_app's run function returns it. */
u8 th_run(u8 cmd);

/* Once, before the first script: the settings the screens read
   (scr_cfg) beyond the item order. */
void th_setup(void);

/* Once, before the first script: th_known on every script name in the
   program's own tables beside th_scripts and th_memfaults. */
void th_check_tables(void);

/* Seed requests of the program's own, sent through th_req after the
   common ones; 1 when every one was answered +OK. */
int th_seed(void);

/* Before the screens of the script th_cur run, once its login is done,
   and after them; th_begin returns 0 or the CF_E code of what it could
   not set up. */
int th_begin(void);
void th_login(void);
void th_end(void);

/* Given by T_SCRH2.C. */

/* Send one request line on th_lk (the seeding's, or the login's HELLO);
   1 when it was answered +OK, otherwise the answer is reported. */
int th_req(const char *line);

/* Empty the data directory and seed it on a link of its own, with no
   failing request left armed by the script before (tl_clear); 1 on
   success. */
int th_reseed(void);

/* Given by T_SCRH.C. */

/* printf with the program's name and a colon before it. */
void th_say(const char *fmt, ...);

/* The row of th_scripts of the script named name, counted as a check
   that there is one; NULL, reported, when there is none. */
const script *th_known(const char *name);

/* Compare the file path with the script's golden of extension ext
   (".PRN"), or with -u make it that golden; 1 when they agree. */
int th_check_file(const char *path, const char *ext);

/* Called, when not NULL, by the screens' exit function before a script
   that ended inside a screen returns to the harness, as INV's exit path
   calls scr_wipe; a program that links the login screen sets it in
   th_setup. */
extern void (*th_onexit)(void);

extern char th_ddir[TH_PATHMAX];        /* the data directory prefix    */
extern link_clock th_clk;               /* the engine's fixed clock     */
extern const script *th_cur;            /* the script running           */
extern char th_curname[TH_NAMESZ];      /* ... and its name             */
extern link *th_lk;                     /* the link requests go on      */
extern int th_status;           /* the status a script with SF_EXIT must
                                   end with: 0, unless th_begin sets it */

#endif
