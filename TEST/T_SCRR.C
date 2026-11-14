/*
 * T_SCRR.C - the report and CSV screens (SRC/CLIENT/SCRRPT.C, SCRCSV.C),
 * the user, administration and password screens (SCRUSER.C, SCRADMIN.C,
 * SCRPASS.C), the settings screen (SCRSET.C) and the login screen
 * (SCRLOGIN.C) run under the key scripts TEST/KEYS/SCRR*.KEY, their
 * screens compared with golden snapshots, by the harness of
 * TEST/T_SCRH.C: the program's name, its scripts, the commands it
 * starts, a seed of its own, the printer settings and the faults of its
 * scripts.
 *
 * usage: T_SCRR [options] dir [file...], as TEST/T_SCRH.C gives them.
 *
 * The seed adds to the common one a current value to every third item
 * and marks items 9, 18 and 27 disposed (STATUS D), so the valuation
 * has values and the listing's status filter picks something.  The
 * screens print on PRN_LPT2, which no test machine has, so a script
 * sees the printer questions of an absent printer; with SF_PRN the jobs
 * go to the capture file T_SCRR.CAP of the data directory instead
 * (prn_capture), and the capture must equal TEST/GOLDEN/PRT/NAME.PRN
 * (th_check_file); one that does is removed.  The date of the reports
 * is that of the engine's fixed clock (scr_date), their pages PAGELEN
 * lines long.
 *
 * File and SCREEN output and the CSV files are named without a
 * directory, so they lie in the current directory, which is the same
 * text on every machine: the scratch drive under DOSBox-X, the boot
 * floppy on MS-DOS 3.10, the program's scratch directory on the host.
 * Each file a script has in outfiles is removed before the script, and
 * after it unless it was compared with TEST/GOLDEN/PRT/NAME plus the
 * extension given there and differed.  The answers that a real engine
 * would give only after a fault (a full disk, a lost scratch file, a
 * refused line, an import that stopped halfway) are the test's own
 * (tl_fail).  A script listed in faults has one file operation of its
 * kind fail (COMPAT.H's cf_fault), counted from 1 after the login: a
 * write of the printer's capture file or of a report file, or the
 * creation of the viewer's scratch file or of the settings file's
 * scratch file (the settings file is T_SCRH.C's scr_cfgpath, in the
 * current directory).  Every name in outfiles and faults, and SCR_HELD,
 * SCR_TAKEN and SCR_OLDDATE, must name a script (th_check_tables).  With
 * SF_FF the viewer's scratch file, once the report has written it, is
 * replaced as the viewer opens it (COMPAT.H's cf_fault_rd) by lines
 * with a form feed in the middle of one, which no report writes.  On
 * the host, the settings screen's adapter, which the goldens mask as it
 * differs between machines, must read as a CGA written directly when
 * SCRR28's failing creation comes.
 *
 * No script may leave a SCREEN output's scratch file (INVR*.$$$)
 * behind, nor change one another station made.  SCRR52 runs as station
 * TET4, whose name sums to the same checksum as TEST's, while TEST's job
 * holds their first scratch name open: the run must create the second
 * name.  In SCRR53 all SCR_TRY names are there: the run must try each
 * once and fail with the scratch file's message.  In SCRR54 the
 * valuation's layout finds no memory once its scratch file is made, which
 * must be removed.  The held files keep their bytes.
 *
 * The login screen runs as a command of its own (TC_LOGIN), after the
 * harness's login; it sends HELLO and LOGIN again, and VER only for its
 * first run in the program: a script that opens it twice must send one
 * VER.  The version text it fetches is dropped before every script, so
 * the settings screen shows none.  INV's home screen, which sends
 * STATUS, runs as a command too (TC_HOME); in SCRR29 the date is
 * 1980-01-01, that of a PC whose clock was never set (SCR_OLDDATE).
 *
 * The user and administration screens' scripts must send the requests
 * of reqs exactly, so the form's group goes as GROUP= and a password
 * typed for a saved user as USER RESET before the USER SET of the other
 * changes, and the audit filters as the request's pairs; they must not
 * send those of unsent, which a question answered N, or a refusal
 * before any request, holds back; no script may send the form's
 * repetition of a password; and on the host the user, administration
 * and password screens must free every near block they took by the
 * time they return (held_run), and so must INV's exit wipe (scr_wipe,
 * which closes the open password form) when a script ends inside one
 * (SF_EXIT, exit_wipe); a script that ends with a password half typed,
 * in the password form or on the login screen, must see the host's
 * exit wipe overwrite it, and SCRR49 a password it left by Up wiped
 * then.  The administration scripts back up to and restore from a
 * directory that is not there, as the screens run on the test's data
 * directory, which has none: the success of BACKUP and RESTORE is the
 * test's answer, while PACK and REINDEX run on the engine.  ADMIN
 * CLIENTS answers 104 in local mode; its rows are the test's answer.
 * On the host, every other ADMIN request must find the hint row saying
 * that the operation is running (TS_WAIT) already on the screen as it
 * is sent (TRLINK's tl_hook): a flush then writes nothing.  With
 * SF_HOME, TC_KEY runs nothing, so the key the home screen reads
 * before it shows what the last command left on the hint row.  No
 * seeded user has a password change pending, so a login that asked for
 * one would show the forced change in a snapshot.
 */
#define TF_TERSE
#define TF_PART
#define TF_MALLOC
#define TF_FILE "T_SCRR"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define COMPAT_FIND
#define TL_ENDS
#include "COMPAT.H"
#include "PRINT.H"
#include "SCR.H"
#include "SCRSHR.H"
#include "TRLINK.H"
#include "T_SCRH.H"
#include "TESTFW.H"

#define PAGELEN         30      /* report page lines: a few pages       */
#define LABELW          35      /* label stock, characters by lines     */
#define LABELH          6
#define TC_LOGIN        60      /* the login screen, a command no SCMD_
                                   names                                */
#define TC_KEY          61      /* nothing, after a key with SF_HOME    */
#define TC_HOME         62      /* INV's home screen (scr_home)         */
#define TC_PWMEM        63      /* the forced password change without
                                   memory for its form, on the host     */
#define NEWCALL         "RINV0002"      /* scr_reopen's new NetBIOS name */

const char th_name[] = "T_SCRR";

/* What the settings screen shows of the run: no adapter flags; and the
   data directory the login screen shows. */
u8 scr_vidflags;
const char *scr_datadir = "TDATA";

/* Answers two scripts give in the engine's place, each kept once. */
static const char k_exists[] = "-ERR 201 file exists FIELD=FILE";
static const char k_busy[] = "-ERR 402 server busy";
static const char k_full[] = "-ERR 302 disk full";

/* The requests of a saved user's new password and full name. */
static const char k_reset[] = "USER RESET OPER PASSWORD=pass99\n"
    "USER SET OPER FULLNAME=\"Otto Opstein Sr\"\n";

/* An ADMIN CLIENTS answer with more rows than the sessions' box holds:
   the requester's, one before HELLO, one logged in with a password
   change pending, then rows of a slot number alone. */
static const char k_clients[] = "+DATA\n"
    "F SLOT TYPE STATION USER LEVEL STATE IDLE SELF\n"
    "R 1 NETB PC1 ADMIN 3 3 0 1\nR 2 NETB LAB2 \"\" 0 1 75 0\n"
    "R 3 NETB LAB3 OPER 2 2 4 0\nR 4\nR 5\nR 6\nR 7\nR 8\nR 9\nR 10\n"
    "R 11\nR 12\nR 13\nR 14\nR 15\nR 16\nR 17\nR 18\nR 19\nR 20\n"
    "R 21\n.";

/* An import answer refusing more lines than the engine lists (it lists
   ENG_IMPERRS of them, and K ERRORS counts them all), then giving the
   error that stopped the reading. */
#define REFMANY "+DATA\nF LINE FIELD CODE TEXT\nR 2 QTY 102 bad\n" \
    "R 3 QTY 102 bad\nR 4 QTY 102 bad\nR 5 QTY 102 bad\nR 6 QTY 102 bad\n" \
    "R 7 QTY 102 bad\nR 8 QTY 102 bad\nR 9 QTY 102 bad\nR 10 QTY 102 bad\n" \
    "R 11 QTY 102 bad\nK ERRORS=15\nK ERROR=303\n."

/* An import answer refusing one line, then giving the error that
   stopped the reading; and one refusing two lines without K ERRORS. */
#define REFONE "+DATA\nF LINE FIELD CODE TEXT\nR 3 CATEGORY 202 " \
    "\"unknown category\"\nK ERRORS=1\nK ERROR=300\n."
#define REFNOK "+DATA\nF LINE FIELD CODE TEXT\nR 3 LINE 102 " \
    "\"bad quoting\"\nR 4 QTY 102 \"not a number\"\n."

/* An audit page with a table name and values longer than their
   columns, a failed login (an empty user) and the row of a change cut
   off, which the list marks. */
#define AUDROWS "+DATA\nF AUD_ID ADATE ATIME USERNAME TBL REC_ID ACTION " \
    "FLD OLDVAL NEWVAL\nR 9 20261008 12:01:00 ADMIN LONGNAME 12345678 U " \
    "DESCR OLD_VALUE_12 NEW_VALUE_12\n" \
    "R 8 20261008 12:00:30 \"\" USERS 0 F USERNAME \"\" ?\n" \
    "R 7 20261008 12:00:00 SYSTEM AUDIT 5 X AUD_ID \"\" 7\n."

const script th_scripts[] = {
    { AD, { SCMD_RPT_LIST },
      SF_MONO, 0, 0, NULL },                      /* 1 listing, viewer */
    { AD, { SCMD_RPT_VALUE, SCMD_RPT_LOANS },
      0, SD_LOAN, 0, NULL },                      /* 2 valuation, loans */
    { AD, { SCMD_LBL_ITEM, SCMD_LBL_BIN },
      0, 0, 0, NULL },                            /* 3 labels          */
    { AD, { SCMD_RPT_LIST },
      0, 0, 1, "-ERR 102 bad value FIELD=LOC" },  /* 4 no printer, 102 */
    { AD, { SCMD_RPT_LIST, SCMD_LBL_ITEM },
      SF_PRN, 0, 0, NULL },                       /* 5 printed         */
    { AD, { SCMD_EXPORT },
      0, 0, 1, k_exists },                        /* 6 export, 201     */
    { AD, { SCMD_IMPORT },
      0, 0, 1, k_busy },                          /* 7 import, 402     */
    { OP, { SCMD_EXPORT, SCMD_IMPORT, SCMD_USERS, SCMD_AUDIT,
      SCMD_RPT_LIST },
      SF_RO | SF_HOME, 0, 0, NULL },              /* 8 level 2         */
    { AD, { SCMD_RPT_LIST },
      0, 0, 0, NULL },                            /* 9 orders, status  */
    { AD, { SCMD_RPT_LIST },
      SF_PRN, 0, 2, k_busy },                     /* 10 stopped: 402   */
    { AD, { SCMD_RPT_LIST },
      SF_PRN, 0, 0, NULL },                       /* 11 write fails: retry */
    { AD, { SCMD_RPT_LIST },
      SF_PRN, 0, 0, NULL },                       /* 12 write fail: cancel */
    { AD, { SCMD_RPT_LIST },
      0, 0, 0, NULL },                            /* 13 report file error */
    { AD, { SCMD_RPT_LIST },
      0, 0, 0, NULL },                            /* 14 no scratch, no mem */
    { AD, { SCMD_EXPORT },
      0, 0, 0, NULL },                            /* 15 no dir; paged  */
    { AD, { SCMD_EXPORT }, 0, 0, 2,
      "-ERR 204 no export in progress" },         /* 16 204 later      */
    { AD, { SCMD_EXPORT },
      0, 0, 2, k_exists },                        /* 17 201 later      */
    { AD, { SCMD_EXPORT },
      0, 0, 1, "-ERR 302 disk full FIELD=FILE" }, /* 18 302 disk full  */
    { AD, { SCMD_IMPORT }, 0, 0, 1,
      "+DATA\nK ADDED=7\nK ERROR=302\n." },       /* 19 partial, 302   */
    { AD, { SCMD_IMPORT },
      0, 0, 1, REFONE },                          /* 20 refused, 300   */
    { AD, { SCMD_IMPORT },
      0, 0, 1, REFMANY },                         /* 21 15 refused     */
    { AD, { SCMD_IMPORT },
      0, 0, 1, REFNOK },                          /* 22 no K ERRORS    */
    { AD, { SCMD_IMPORT }, 0, 0, 1,
      "+DATA\nK ADDED=3\nK ERROR=304\n." },       /* 23 partial, 304   */
    { AD, { SCMD_IMPORT }, 0, 0, 1,
      "+DATA\nK ADDED=1\nK ERROR=305\n." },       /* 24 partial, other */
    { AD, { SCMD_IMPORT },
      0, 0, 1, NULL },                            /* 25 no mem; ends   */
    { AD, { SCMD_RPT_LIST },
      0, 0, 0, NULL },                            /* 26 Esc during a run */
    { AD, { SCMD_EXPORT },
      0, 0, 0, NULL },                            /* 27 Ctrl-Break, export */
    { AD, { SCMD_SETTINGS },
      0, 0, 0, NULL },                            /* 28 settings: no write */
    { AD, { TC_LOGIN, TC_LOGIN, TC_HOME },
      0, 0, 0, NULL },                            /* 29 VER once; 1980 */
    { AD, { SCMD_USERS },
      0, 0, 0, NULL },                            /* 30 user added     */
    { AD, { SCMD_USERS, SCMD_PASSWD },
      SF_HOME, 0, 0, NULL },                      /* 31 user changed   */
    { AD, { SCMD_BACKUP, SCMD_BACKUP, SCMD_BACKUP, SCMD_BACKUP,
      TC_KEY }, SF_HOME, 0, 3, "+OK" },           /* 32 backup         */
    { AD, { SCMD_RESTORE, SCMD_RESTORE, SCMD_RESTORE, SCMD_RESTORE,
      TC_KEY }, SF_HOME, 0, 2, "+OK" },           /* 33 restore        */
    { AD, { SCMD_USERS, SCMD_PACK, SCMD_AUDIT },
      SF_HOME, 0, 0, NULL },                      /* 34 pack           */
    { AD, { SCMD_REINDEX, SCMD_PACK, SCMD_REINDEX, TC_KEY },
      SF_HOME, 0, 1, k_busy },                    /* 35 402, reindex   */
    { AD, { SCMD_AUDIT },
      0, 0, 0, NULL },                            /* 36 audit log      */
    { AD, { SCMD_SESSIONS, TC_KEY },
      SF_HOME, 0, 1, k_clients },                 /* 37 sessions, 104  */
    { AD, { SCMD_AUDIT, TC_KEY },
      SF_HOME, 0, 0, NULL },                      /* 38 audit: no memory */
    { AD, { SCMD_RESTORE, TC_KEY },
      SF_HOME, 0, 1, "-ERR 300 x FIELD=DIR" },    /* 39 damaged backup */
    { AD, { SCMD_AUDIT },
      0, 0, 1, AUDROWS },                         /* 40 audit: marked rows */
    { AD, { SCMD_SESSIONS },
      0, 0, 1, k_clients },                       /* 41 sessions: Esc  */
    { AD, { SCMD_PACK },
      0, 0, 1, k_full },                          /* 42 302, no DIR    */
    { AD, { SCMD_BACKUP },
      0, 0, 1, k_full },                          /* 43 302, no DIR    */
    { AD, { SCMD_BACKUP, TC_KEY }, SF_HOME, 0, 1,
      "-ERR 304 data files in use FIELD=DIR" },   /* 44 304 DIR        */
    { AD, { SCMD_BACKUP, SCMD_BACKUP },
      0, 0, 1, "-ERR 300 x FIELD=DIR\n" },        /* 45 300 DIR, closed */
    { AD, { SCMD_USERS, SCMD_PASSWD }, 0, 0, 4,
      "-ERR 102 bad value FIELD=FULLNAME" },      /* 46 RESET, SET refused */
    { AD, { TC_HOME, SCMD_PASSWD }, 0, 0, 1,
      "-ERR 400 HELLO or LOGIN required" },       /* 47 400: home, passwd */
    { AD, { SCMD_PASSWD },
      SF_EXIT, 0, 0, NULL },                      /* 48 ends in the form */
    { AD, { TC_LOGIN },
      SF_EXIT, 0, 0, NULL },                      /* 49 ends at the login */
    { OP, { SCMD_BACKUP, SCMD_RESTORE, SCMD_PACK, SCMD_REINDEX,
      SCMD_SESSIONS },
      SF_RO | SF_HOME, 0, 0, NULL },              /* 50 level 2: admin */
    { AD, { SCMD_RPT_LIST },
      SF_FF, 0, 0, NULL },                        /* 51 viewer: form feed */
    { AD, { SCMD_RPT_LIST },
      0, 0, 0, NULL },                            /* 52 scratch name held */
    { AD, { SCMD_RPT_LIST },
      0, 0, 0, NULL },                            /* 53 every name taken */
    { AD, { SCMD_RPT_VALUE },
      0, 0, 0, NULL },                            /* 54 no layout memory */
    { AD, { SCMD_RPT_LIST },
      0, 0, 0, NULL },                            /* 55 and below      */
    { AD, { SCMD_LBL_ITEM },
      0, 0, 1, "-ERR 200 gone" },                 /* 56 200, not LOC   */
    { AD, { SCMD_RPT_LIST, SCMD_RPT_VALUE },
      0, 0, 0, NULL },                            /* 57 tags, works    */
    { AD, { SCMD_USERS },
      0, 0, 0, NULL },                            /* 58 status: A, D   */
    { AD, { SCMD_RPT_LIST },
      0, 0, 0, NULL },                            /* 59 file: ITEMS.DBF */
    { AD, { TC_LOGIN, TC_LOGIN },
      0, 0, 3, tl_reset },                        /* 60 LOGIN: new session */
    { AD, { TC_LOGIN },
      SF_NET, 0, 3, tl_lost },                    /* 61 LOGIN: NET lost */
    { AD, { TC_LOGIN }, SF_EXIT, 0, 1,
      "-ERR 102 bad station FIELD=STATION" },     /* 62 HELLO refused  */
    { AD, { TC_LOGIN }, 0, 0, 3,
      "-ERR 102 bad user name FIELD=USER" },      /* 63 LOGIN: 102     */
    { AD, { TC_PWMEM },
      0, 0, 0, NULL },                            /* 64 change: no mem */
    { AD, { TC_LOGIN },
      SF_EXIT, 0, 3, tl_lost }                    /* 65 LOGIN: COM lost */
};
const u16 th_nscripts = (u16)(sizeof th_scripts / sizeof th_scripts[0]);

/* SCRR25's first far allocation after the login is the form's; its
   second the refused lines' block, before the import is sent. */
const memfault th_memfaults[] = {
    { "SCRR25", { 2, 0 } },
    { "SCRR38", { 1, 0 } },             /* the audit list's rows        */
    { "SCRR14", { 3, 4 } },             /* the viewer's offsets, twice  */
    { "SCRR54", { 2, 0 } },             /* the valuation's categories   */
    { NULL, { 0, 0 } }
};

/* A file a script names in its current directory, and the extension of
   its golden, or NULL when it is only removed.  The names differ from
   the goldens', which on MS-DOS 3.10 lie in that same directory.
   EXP16.&C0, EXP17.&C0 and EXP27.&C0 are the engine's scratch files of
   the exports to EXP16.CSV, EXP17.CSV and EXP27.CSV (the extension
   ENG_CSVSCRATCH of ENGCSV.H and the tag 0 of local mode): the first
   request writes one, and it stays behind because the test answers the
   later request in the engine's place, or because the export was
   stopped.  EXP16.CSV, EXP17.CSV and EXP27.CSV are removed too, as a
   run that failed to stop (at the refused page, or at Ctrl-Break) would
   leave them, and TEST.CFG, as a settings save that failed to fail
   would, so that a failed run cannot break the next one. */
typedef struct {
    const char *name;
    const char *file;
    const char *ext;
} outfile;

static const outfile outfiles[] = {
    { "SCRR13", "R13.TXT", NULL },
    { "SCRR15", "EXP15.CSV", ".CSV" },
    { "SCRR16", "EXP16.CSV", NULL },
    { "SCRR16", "EXP16.&C0", NULL },
    { "SCRR17", "EXP17.CSV", NULL },
    { "SCRR17", "EXP17.&C0", NULL },
    { "SCRR26", "RPT26.TXT", NULL },
    { "SCRR27", "EXP27.CSV", NULL },
    { "SCRR27", "EXP27.&C0", NULL },
    { "SCRR28", "TEST.CFG", NULL },
    { NULL, NULL, NULL }
};

/* A file operation (CF_OP_) that fails once: the n-th of its kind
   after the login. */
typedef struct {
    const char *name;
    int  op;
    u16  n;
} fault;

static const fault faults[] = {
    { "SCRR11", CF_OP_WRITE, 3 },       /* within the first page        */
    { "SCRR12", CF_OP_WRITE, 180 },     /* after the first page         */
    { "SCRR13", CF_OP_WRITE, 5 },
    { "SCRR14", CF_OP_CREATE, 1 },
    { "SCRR28", CF_OP_CREATE, 1 },      /* the settings' scratch file   */
    { NULL, 0, 0 }
};

/* A request a script must send, as the log holds it. */
typedef struct {
    const char *name;
    const char *line;
} sent;

static const sent reqs[] = {
    { "SCRR30", "USER ADD BOB LEVEL=2 FULLNAME=\"Bob Smith\" GROUP=OPS "
      "PASSWORD=abc1\n" },
    { "SCRR31", k_reset },
    { "SCRR32", "ADMIN BACKUP DIR=NOSUCH\nADMIN BACKUP DIR=A*B\n"
      "ADMIN BACKUP DIR=BAK\n" },
    { "SCRR33", "ADMIN RESTORE DIR=NOSUCH\nADMIN RESTORE DIR=BAK\n" },
    { "SCRR36", "AUDIT LIST TBL=ITEMS REC=3 PAGE=" },
    { "SCRR46", k_reset },
    { "SCRR60", "HELLO TEST\nLOGIN ADMIN secret1\n" },
    { "SCRR61", "LOGIN ADMIN secret1\nHELLO TEST\nHELLO " NEWCALL "\n"
      "LOGIN ADMIN secret1\n" },
    { NULL, NULL }
};

/* A text no request of a script may hold. */
static const sent unsent[] = {
    { "SCRR33", "DIR=OLD" },            /* the restore answered N       */
    { "SCRR35", "PACK" },               /* the pack answered N          */
    { "SCRR36", "REC=7" },              /* REC without TBL              */
    { "SCRR50", "ADMIN" },              /* level 2: no admin request    */
    { "SCRR59", "REPORT" },             /* no file name taken           */
    { NULL, NULL }
};

/* The exit status of a script that ends inside a screen (SF_EXIT) other
   than 0. */
typedef struct {
    const char *name;
    int  status;
} exitst;

static const exitst exits[] = {
    { "SCRR62", SCR_EXIT_OPEN },        /* the refused HELLO            */
    { "SCRR65", TUI_EXIT_LOST },        /* a lost link not made again   */
    { NULL, 0 }
};

static const fault *g_curf;     /* the script's fault, or NULL          */
static u16 g_nops;              /* operations of its kind so far        */

/* The capture file's path, which prn_capture keeps using. */
static char g_cap[TH_PATHMAX + 16];

/* The near blocks held as held_run started its screen, and 1 while
   that screen runs. */
static u16 g_held;
static u8 g_inheld;
static u8 g_wipes;              /* exit_wipe's runs in the script       */

/* Run the user, an administration or the password screen: on the
   host, every near block it takes is freed by the time it returns. */
static u8 held_run(u8 cmd)
{
    g_held = tm_held();
    g_inheld = 1;
    if (cmd == SCMD_USERS)
        cmd = scr_users(cmd);
    else if (cmd == SCMD_PASSWD)
        cmd = scr_passwd(cmd);
    else
        cmd = scr_admin(cmd);
    g_inheld = 0;
    TM_HELD(g_held);
    return cmd;
}

#ifdef COMPAT_HOST
/* The host build links with txt_wipe wrapped (T_SCRR_HOSTWRAP): the
   wipes of a buffer that starts with k_half are counted. */
static const char k_half[] = "newp";    /* the SF_EXIT scripts' typing  */
static u8 g_nhalf;

void __real_txt_wipe(void *p, u16 n);
void __wrap_txt_wipe(void *p, u16 n);

void __wrap_txt_wipe(void *p, u16 n)
{
    if (n > sizeof k_half - 1 &&
        strncmp((const char *)p, k_half, sizeof k_half - 1) == 0)
        g_nhalf++;
    __real_txt_wipe(p, n);
}

/* SCR_PWUP's login screen leaves a k_half typed as the password by Up,
   which wipes it, before it types k_half again. */
#define SCR_PWUP        "SCRR49"
#endif

/* th_onexit: INV's exit wipe, after which a held_run screen that the
   script ended inside holds no near block either; on the host, a
   script with SF_EXIT that ends with its keys (status 0), k_half half
   typed as a password, must see it wiped, and SCR_PWUP must have seen
   its first k_half wiped before. */
static void exit_wipe(void)
{
#ifdef COMPAT_HOST
    u8 n;

    n = g_nhalf;
#endif
    g_wipes++;
    scr_wipe();
#ifdef COMPAT_HOST
    if ((th_cur->flags & SF_EXIT) && th_status == 0)
        TF_CHECK(g_nhalf != n);
    if (strcmp(th_curname, SCR_PWUP) == 0)
        TF_EQ(n, 1);
#endif
    if (g_inheld)
        TM_HELD(g_held);
    g_inheld = 0;
}

#ifdef COMPAT_HOST
static void row_text(u8 row, char *t);
#endif

/* TC_PWMEM: on the host, whose mallocs can fail, the forced password
   change finds no memory for its form: it logs out (TUI_CMD_LOGIN) with
   the message on the hint row. */
static void pw_nomem(void)
{
#ifdef COMPAT_HOST
    char t[VID_COLS + 1];

    tm_fail(1);
    TF_EQ(scr_passwd(SCMD_MUSTCHG), TUI_CMD_LOGIN);
    row_text(TUI_HINTROW, t);
    TF_CHECK(strstr(t, "Not enough memory") != NULL);
#endif
}

/* scr_reopen of INV.C: the link closed and opened again on the data
   directory, as INV calls the server again, the new link calling from
   a NetBIOS name of its own, as one made up from the timer would.  On
   the host the new link opens before the old one closes, so that it
   never takes the old one's block and a screen left on the old one is
   seen (th_end). */
link *scr_reopen(void)
{
    char why[LINK_WHYSZ];
    link *old;

    old = th_lk;
#ifndef COMPAT_HOST
    link_close(old);
#endif
    tl_netname(NEWCALL);
    link_open_local(&th_lk, th_ddir, &th_clk, why, (u16)sizeof why);
    tl_netname(NULL);
#ifdef COMPAT_HOST
    link_close(old);
#endif
    return th_lk;
}

u8 th_run(u8 cmd)
{
    if (cmd >= SCMD_RPT_LIST && cmd <= SCMD_LBL_BIN)
        return scr_report(cmd);
    if (cmd == SCMD_EXPORT || cmd == SCMD_IMPORT)
        return scr_csv(cmd);
    if (cmd == SCMD_SETTINGS)
        return scr_settings(cmd);
    if (cmd == SCMD_USERS || cmd == SCMD_PASSWD ||
        (cmd >= SCMD_BACKUP && cmd <= SCMD_SESSIONS))
        return held_run(cmd);
    if (cmd == TC_HOME)
        return scr_home(TUI_CMD_NONE);
    if (cmd == TC_PWMEM)
        pw_nomem();
    if (cmd == TC_LOGIN) {
        /* As tui_run opens the login screen: logged out. */
        tui_s.level = 0;
        tui_s.user[0] = '\0';
        tui_s.name[0] = '\0';
        tui_frame();
        return scr_login(TUI_CMD_LOGIN);
    }
    return TUI_CMD_NONE;
}

/* The date of the reports and the home screen: the engine's fixed
   clock's, but in SCR_OLDDATE that of a PC whose clock was never set. */
#define SCR_OLDDATE     "SCRR29"
void scr_date(char *out)
{
    if (strcmp(th_curname, SCR_OLDDATE) == 0)
        strcpy(out, "19800101");
    else
        sprintf(out, "%04u%02u%02u", (unsigned)th_clk.year,
                (unsigned)th_clk.month, (unsigned)th_clk.day);
}

void th_setup(void)
{
    th_onexit = exit_wipe;
    scr_cfg.prport = PRN_LPT2;
    scr_cfg.prtype = PR_EPSON;
    scr_cfg.pagelen = PAGELEN;
    scr_cfg.labelw = LABELW;
    scr_cfg.labelh = LABELH;
}

int th_seed(void)
{
    char line[40];
    int ok;
    int i;

    ok = 1;
    for (i = 3; i <= TH_SEEDITEMS; i += 3) {
        sprintf(line, "ITEM SET %d CUR_VALUE=%d.25", i, i * 4);
        ok &= th_req(line);
    }
    for (i = 9; i <= 27; i += 9) {
        sprintf(line, "ITEM SET %d STATUS=D", i);
        ok &= th_req(line);
    }
    return ok;
}

/* The SCREEN output's first scratch name of the stations TEST and TET4
   (scratch_name of SCRRPT.C), the others ending in the other hex
   digits; and SF_FF's lines, also the bytes of the scratch files
   SCRR52 and SCRR53 hold. */
static const char k_scr[] = "INVR1EDF.$$$";
static const char k_ff[] = "AB\fCD\n";
#define SCR_TRY         16      /* scratch names a run tries            */
#define SCR_HELD        "SCRR52"        /* holds one scratch name       */
#define SCR_TAKEN       "SCRR53"        /* holds SCR_TRY of them        */

void th_check_tables(void)
{
    u16 i;

    for (i = 0; outfiles[i].name != NULL; i++)
        th_known(outfiles[i].name);
    for (i = 0; faults[i].name != NULL; i++)
        th_known(faults[i].name);
    for (i = 0; reqs[i].name != NULL; i++)
        th_known(reqs[i].name);
    for (i = 0; unsent[i].name != NULL; i++)
        th_known(unsent[i].name);
    for (i = 0; exits[i].name != NULL; i++)
        th_known(exits[i].name);
    th_known(SCR_HELD);
    th_known(SCR_TAKEN);
    th_known(SCR_OLDDATE);
}

static int g_scrh;              /* the first scratch name held, or -1   */
static u8 g_scrn;               /* scratch names held                   */
static u16 g_ncreate;           /* creates since the login              */

/* For SCRR52 and SCRR53, as station TET4: hold g_scrn scratch names,
   from k_scr on, the first open; 0 or the CF_E code of a failure. */
static int scr_hold(void)
{
    char name[sizeof k_scr];
    u8 i;
    int h;
    int rc;

    g_scrh = -1;
    g_scrn = (u8)(strcmp(th_curname, SCR_HELD) == 0 ? 1 :
                  strcmp(th_curname, SCR_TAKEN) == 0 ? SCR_TRY : 0);
    if (g_scrn > 0)
        strcpy(tui_s.station, "TET4");
    strcpy(name, k_scr);
    for (i = 0; i < g_scrn; i++) {
        name[7] = txt_hexdig[15 - i];
        rc = cf_create_new(name, &h);
        if (rc != 0)
            return rc;
        rc = cf_write(h, k_ff, (u16)(sizeof k_ff - 1));
        if (i == 0)
            g_scrh = h;
        else
            cf_close(h);
        if (rc != 0)
            return rc;
    }
    return 0;
}

int th_begin(void)
{
    const outfile *o;
    const exitst *x;
    int rc;

    rc = scr_hold();
    if (rc != 0)
        return rc;
    scr_version[0] = '\0';
    g_wipes = 0;
#ifdef COMPAT_HOST
    g_nhalf = 0;
#endif
    for (o = outfiles; o->name != NULL; o++)
        if (strcmp(o->name, th_curname) == 0)
            cf_remove(o->file);
    for (x = exits; x->name != NULL; x++)
        if (strcmp(x->name, th_curname) == 0)
            th_status = x->status;
    if (!(th_cur->flags & SF_PRN))
        return 0;
    strcpy(g_cap, th_ddir);
    strcat(g_cap, "T_SCRR.CAP");
    return prn_capture(g_cap);
}

#ifdef COMPAT_HOST
/* The adapter's cells of the settings screen, which SCRR28.KEY masks,
   and what they show on the host. */
#define ADP_ROW         8
#define ADP_COL         19

static const char k_adp[] = "CGA, direct video ";

/* The text of screen row row into t (VID_COLS + 1 bytes). */
static void row_text(u8 row, char *t)
{
    u8 cells[VID_ROWBYTES];
    u16 i;

    vid_getrow(row, cells);
    for (i = 0; i < VID_COLS; i++)
        t[i] = (char)cells[2 * i];
    t[VID_COLS] = '\0';
}

/* MZ_WAIT, which SCRADMIN.C keeps private as every screen keeps its
   message numbers; a renumbering shows as a wrong count of g_nwait. */
#define TS_WAIT         585

static const char k_admin[] = "ADMIN ";
static const char k_admcl[] = "ADMIN CLIENTS";

static u16 g_nadmin;            /* ADMIN requests but CLIENTS sent      */
static u16 g_nwait;             /* ... of them with TS_WAIT on the hint
                                   row                                  */

/* 1 when the request line of len bytes is an ADMIN request other than
   CLIENTS. */
static int admin_op(const char *line, u16 len)
{
    return len >= sizeof k_admin - 1 &&
           memcmp(line, k_admin, sizeof k_admin - 1) == 0 &&
           !(len >= sizeof k_admcl - 1 &&
             memcmp(line, k_admcl, sizeof k_admcl - 1) == 0);
}

/* tl_hook: count the ADMIN requests other than CLIENTS, and those sent
   while the screen showed TS_WAIT on the hint row: the shadow holds it
   and a flush finds nothing left to write. */
static void admin_sent(const char *line, u16 len)
{
    char t[VID_COLS + 1];

    if (!admin_op(line, len))
        return;
    g_nadmin++;
    row_text(TUI_HINTROW, t);
    vid_flush();
    if (strstr(t, msg(TS_WAIT)) != NULL && vid_flushed() == 0)
        g_nwait++;
}

/* The ADMIN requests other than CLIENTS in the log, counted apart from
   admin_sent so that a hook that saw none of them shows. */
static u16 admin_logged(void)
{
    const char *p;
    const char *e;
    u16 n;

    n = 0;
    for (p = tl_log(); *p != '\0'; p = e + 1) {
        e = strchr(p, '\n');
        n = (u16)(n + admin_op(p, (u16)(e - p)));
    }
    return n;
}
#endif

/* cf_fault: count the creates and fail the script's fault; on the
   host, first check the adapter as SCRR28's settings screen creates its
   scratch file. */
static int fail_op(int op)
{
#ifdef COMPAT_HOST
    char t[VID_COLS + 1];

    if (op == CF_OP_CREATE && th_cur->cmd[0] == SCMD_SETTINGS) {
        row_text(ADP_ROW, t);
        TF_CHECK(strncmp(t + ADP_COL, k_adp, sizeof k_adp - 1) == 0);
    }
#endif
    g_ncreate = (u16)(g_ncreate + (op == CF_OP_CREATE));
    if (g_curf != NULL && op == g_curf->op && ++g_nops == g_curf->n)
        return op == CF_OP_CREATE ? CF_EACCES : CF_EFULL;
    return 0;
}

/* cf_fault_rd with SF_FF: the first open once the scratch file exists
   is the viewer's, so it finds k_ff; later opens (a message page while
   the viewer is up) are left alone. */
static int ff_open(int op)
{
    int h;

    if (op == CF_OP_OPEN && cf_exists(k_scr) == 1 &&
        TF_EQ(cf_create(k_scr, &h), 0)) {
        TF_EQ(cf_write(h, k_ff, (u16)(sizeof k_ff - 1)), 0);
        TF_EQ(cf_close(h), 0);
        cf_fault_rd = NULL;
    }
    return 0;
}

void th_login(void)
{
    u16 i;

#ifdef COMPAT_HOST
    g_nadmin = 0;
    g_nwait = 0;
    tl_hook = admin_sent;
#endif
    g_curf = NULL;
    g_nops = 0;
    for (i = 0; faults[i].name != NULL; i++)
        if (strcmp(faults[i].name, th_curname) == 0)
            g_curf = &faults[i];
    g_ncreate = 0;
    if (g_curf != NULL || g_scrn > 0)
        cf_fault = fail_op;
    if (th_cur->flags & SF_FF)
        cf_fault_rd = ff_open;
}

/* The requests logged since the login that are VER. */
static u16 vers(void)
{
    u16 i;
    u16 n;

    n = 0;
    for (i = 0; i < tl_count(); i++)
        if (strcmp(tl_req(i), "VER") == 0)
            n++;
    return n;
}

/* The scratch files the script leaves: only the g_scrn it held, with
   their bytes, after exactly as many creates as names the run had to
   try. */
static void scr_left(void)
{
    char name[DOS_FINDNAME];
    char got[sizeof k_ff];
    dos_find f;
    u16 n;
    u8 found;
    int h;
    int rc;

    if (g_scrh >= 0) {
        TF_EQ(g_ncreate, g_scrn < SCR_TRY ? g_scrn + 1 : SCR_TRY);
        cf_close(g_scrh);
    }
    found = 0;
    for (rc = dos_find_first(&f, "INVR*.$$$", name); rc == 0;
         rc = dos_find_next(&f, name)) {
        n = 0;
        if (g_scrn > 0 && cf_open(name, CF_READ | CF_DENYNONE, &h) == 0) {
            cf_read(h, got, (u16)sizeof got, &n);
            cf_close(h);
        }
        TF_CHECK(n == sizeof k_ff - 1 && memcmp(got, k_ff, n) == 0);
        found++;
        cf_remove(name);
    }
    TF_EQ(found, g_scrn);
}

void th_end(void)
{
    const outfile *o;
    const sent *r;

    for (r = reqs; r->name != NULL; r++)
        if (strcmp(r->name, th_curname) == 0 &&
            !TF_CHECK(strstr(tl_log(), r->line) != NULL))
            th_say("%s: not sent: %s\n", r->name, r->line);
    for (r = unsent; r->name != NULL; r++)
        if (strcmp(r->name, th_curname) == 0 &&
            !TF_CHECK(strstr(tl_log(), r->line) == NULL))
            th_say("%s: sent: %s\n", r->name, r->line);
    TF_CHECK(strstr(tl_log(), "REPEAT") == NULL);
    /* An SF_EXIT script left through the exit wipe once, any other never. */
    TF_EQ(g_wipes, (th_cur->flags & SF_EXIT) != 0);
    /* The screens' link is the harness's, after scr_reopen too. */
    TF_CHECK(tui_s.l == th_lk);
#ifdef COMPAT_HOST
    tl_hook = NULL;
    TF_EQ(g_nadmin, admin_logged());
    TF_EQ(g_nwait, g_nadmin);
#endif
    scr_left();
    cf_fault = NULL;
    cf_fault_rd = NULL;
    g_curf = NULL;
    prn_capture(NULL);
    /* One VER, but none logged after a login asked for again (the log
       starts there) or before a refused HELLO. */
    if (th_cur->cmd[0] == TC_LOGIN)
        TF_EQ(vers(), th_cur->fail != tl_reset &&
                      th_status != SCR_EXIT_OPEN);
    if ((th_cur->flags & SF_PRN) && th_check_file(g_cap, ".PRN"))
        cf_remove(g_cap);
    for (o = outfiles; o->name != NULL; o++)
        if (strcmp(o->name, th_curname) == 0 &&
            (o->ext == NULL || th_check_file(o->file, o->ext)))
            cf_remove(o->file);
}
