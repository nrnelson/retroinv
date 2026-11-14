/*
 * SCR.H - the screens of the full-screen client and the commands that
 * reach them.
 *
 * Each screen function takes the command that started it and returns
 * the command to run next (TUI.H): TUI_CMD_NONE to go back to the home
 * screen, a command chosen from the menu while the screen was open, or
 * TUI_CMD_LOGIN / TUI_CMD_QUIT.  The menu table is INVMENU.H's; INV.C
 * maps each command to its screen.  What the screens share with one
 * another is in SCRSHR.H, which INV.C does not read, as Watcom C 6.5's
 * compiler memory holds every declaration a file reads.
 */
#ifndef SCR_H
#define SCR_H

#include "COMPAT.H"
#include "TUI.H"
#include "CFG.H"

/* ---------------------------------------------------------------------
 * Commands.
 * --------------------------------------------------------------------- */
#define SCMD_ITEMS      1       /* item list, sort order from INV.CFG   */
#define SCMD_ITEMS_ID   2       /* item list by ID                      */
#define SCMD_ITEMS_LOC  3       /* ... by location                      */
#define SCMD_ITEMS_MFR  4       /* ... by manufacturer and model        */
#define SCMD_ITEMS_SER  5       /* ... by serial number                 */
#define SCMD_ITEM_NEW   6       /* new item form                        */
#define SCMD_ASSY       7       /* assemblies: items with parts         */
#define SCMD_MAINT      8       /* recent maintenance records           */
#define SCMD_LOANS      9       /* loans and holds, overdue ones marked */
#define SCMD_LOCS       10      /* location list                        */
#define SCMD_LOC_NEW    11      /* new location form                    */
#define SCMD_CATS       12      /* the categories' code list            */
#define SCMD_LTYPES     13      /* the location types' code list        */
#define SCMD_FIND       20      /* text search (Ctrl-F)                 */
#define SCMD_SERIAL     21      /* serial number search                 */
#define SCMD_FILTER     22      /* item list by category, location and
                                   status                               */
#define SCMD_SETTINGS   30      /* the settings form (INV.CFG)          */
#define SCMD_PASSWD     31      /* change own password                  */
#define SCMD_MUSTCHG    32      /* forced change after login            */
#define SCMD_RPT_LIST   40      /* report: item listing                 */
#define SCMD_RPT_VALUE  41      /* report: valuation                    */
#define SCMD_RPT_LOANS  42      /* report: loans out or overdue         */
#define SCMD_LBL_ITEM   43      /* item labels                          */
#define SCMD_LBL_BIN    44      /* bin (location) labels                */
#define SCMD_EXPORT     50      /* export a table to a CSV file         */
#define SCMD_IMPORT     51      /* import a CSV file into ITEMS or LOCS */
#define SCMD_USERS      52      /* the users and their access levels    */
#define SCMD_BACKUP     53      /* copy the data files to a directory   */
#define SCMD_RESTORE    54      /* copy them back from a backup         */
#define SCMD_PACK       55      /* drop the deleted records             */
#define SCMD_REINDEX    56      /* rebuild every index                  */
#define SCMD_AUDIT      57      /* the audit log, filtered              */
#define SCMD_SESSIONS   58      /* the server's open sessions           */

/* ---------------------------------------------------------------------
 * Shared state.
 * --------------------------------------------------------------------- */

/* The settings in effect and the file they came from (INV.C); the
   settings screen changes both the file and these. */
extern cfg scr_cfg;
extern const char *scr_cfgpath;

/* The data directory in use, or under /NET the server's name, shown by
   the login screen, and the vid_open flags (VID_F_) in effect, shown by
   the settings screen (INV.C). */
extern const char *scr_datadir;
extern u8 scr_vidflags;

/* ---------------------------------------------------------------------
 * Items.
 * --------------------------------------------------------------------- */

/* SCRITEM.C: the item lists (SCMD_ITEMS..SCMD_ITEMS_SER) and the new
   item form (SCMD_ITEM_NEW), which shows the note of its save ("Item
   12 added") on the hint row itself, as no list follows it. */
u8 scr_items(u8 cmd);

/* ---------------------------------------------------------------------
 * Locations.
 * --------------------------------------------------------------------- */

/* SCRLOC.C: the location tree (SCMD_LOCS) and the new location form
   (SCMD_LOC_NEW), which shows the note of its save on the hint row
   itself, as no list follows it. */
u8 scr_locs(u8 cmd);

/* ---------------------------------------------------------------------
 * Code lists.
 * --------------------------------------------------------------------- */

/* SCRCODE.C: SCMD_CATS and SCMD_LTYPES, the categories' or the location
   types' list (CAT LIST, LTYPE LIST) in a box: Ins adds an entry,
   Enter or Ctrl-E edits the selected one's name and sort order, Del deletes
   it after asking; those need level 2. */
u8 scr_codes(u8 cmd);

/* ---------------------------------------------------------------------
 * Search.
 * --------------------------------------------------------------------- */

/* SCRSRCH.C: SCMD_FIND, SCMD_SERIAL, SCMD_FILTER. */
u8 scr_search(u8 cmd);

/* ---------------------------------------------------------------------
 * Assemblies.
 * --------------------------------------------------------------------- */

/* SCRASSY.C: SCMD_ASSY, ASSY LIST TOP=1 through scr_item_list. */
u8 scr_assy(u8 cmd);

/* ---------------------------------------------------------------------
 * Maintenance.
 * --------------------------------------------------------------------- */

/* SCRMAINT.C: SCMD_MAINT, MAINT RECENT, newest first: Enter shows a
   record, Ctrl-E edits it, Del deletes it.  The record form edits DETAIL
   with the memo editor ("MAINT"); opened from this list, its view mode
   also shows the record's item (the Record menu's Open item). */
u8 scr_maint(u8 cmd);

/* ---------------------------------------------------------------------
 * Loans.
 * --------------------------------------------------------------------- */

/* SCRLOAN.C: SCMD_LOANS, the loans and holds of every item, first the
   open rows by due date (LOAN LIST OPEN=1), overdue rows drawn
   TA_ALERT.  Ctrl-PgDn goes on to the open rows of each kind, the
   overdue rows (LOAN OVERDUE) and every row, Ctrl-PgUp back; Ins (or
   Ctrl-L, Ctrl-R, Ctrl-U) adds a loan, reservation or use of an item
   asked for among the available ones, Enter shows the selected row,
   Ctrl-E edits it, and the Record menu returns or releases it and shows
   its item. */
u8 scr_loans(u8 cmd);

/* ---------------------------------------------------------------------
 * Reports and files.
 * --------------------------------------------------------------------- */

/* SCRRPT.C: SCMD_RPT_LIST, SCMD_RPT_VALUE, SCMD_RPT_LOANS,
   SCMD_LBL_ITEM, SCMD_LBL_BIN: the report's options form (filters and
   the output: printer, file or screen), then the run, page by page
   with its progress on the status line (Esc between requests stops
   it), printer errors asked as Retry or Cancel, and for the screen a
   viewer of the laid-out text. */
u8 scr_report(u8 cmd);

/* SCRCSV.C: SCMD_EXPORT (table, file, field names line; EXPORT
   repeated with AFTER= until done, its progress on the status line,
   an existing file replaced only after a question) and SCMD_IMPORT
   (table, file, field names line; one IMPORT after a question, then
   the number of records added or the refused lines). */
u8 scr_csv(u8 cmd);

/* INV.C: today as "YYYYMMDD" and a NUL into out (SCR_DATESZ bytes) for
   reports: the date of INV's /NOW: switch when it was given, else
   dos_today. */
#define SCR_DATESZ      9
void scr_date(char *out);

/* ---------------------------------------------------------------------
 * Users.
 * --------------------------------------------------------------------- */

/* SCRUSER.C: SCMD_USERS, level 3 only (tui_allow): USER LIST, disabled
   users drawn TA_ALERT; Enter or Ctrl-E edits the selected user, Ins adds
   one, Del deletes one.  The user form edits the full name, group,
   level and status, and sets a new password (USER RESET) when one is
   typed twice. */
u8 scr_users(u8 cmd);

/* ---------------------------------------------------------------------
 * Administration.
 * --------------------------------------------------------------------- */

/* SCRADMIN.C: SCMD_BACKUP..SCMD_SESSIONS, level 3 only (tui_allow).
   SCMD_BACKUP and SCMD_RESTORE ask for a directory (ADMIN BACKUP DIR=,
   ADMIN RESTORE DIR= after a question), SCMD_PACK asks first (ADMIN
   PACK), SCMD_REINDEX does not (ADMIN REINDEX); each says its success
   or a refusal naming DIR on the hint row, and leaves any other failure
   to tui_fail.  SCMD_AUDIT is a filter form over a list of AUDIT
   LIST, SCMD_SESSIONS the rows of ADMIN CLIENTS, which only a server
   answers. */
u8 scr_admin(u8 cmd);

/* ---------------------------------------------------------------------
 * Settings.
 * --------------------------------------------------------------------- */

/* SCRSET.C: SCMD_SETTINGS. */
u8 scr_settings(u8 cmd);

/* ---------------------------------------------------------------------
 * Login and passwords.
 * --------------------------------------------------------------------- */

/* SCRLOGIN.C: the login screen and the home screen.  The main loop
   calls scr_login with TUI_CMD_LOGIN and scr_home with TUI_CMD_NONE;
   either passes any other command straight on, unchanged.  scr_login
   returns TUI_CMD_NONE once logged in, TUI_CMD_LOGIN after a log-out
   from the forced password change or a failure that asks for the login
   (the next scr_login then offers the same user name with the cursor on
   the password), else TUI_CMD_QUIT.  When LOGIN answers MUSTCHG=1 the
   login screen calls scr_passwd(SCMD_MUSTCHG) itself and goes on from
   its result.  After the third failed login it calls tui_fatal with
   SCR_EXIT_LOGIN, after a refused HELLO with SCR_EXIT_OPEN. */
u8 scr_login(u8 cmd);
u8 scr_home(u8 cmd);

/* INV's exit statuses after the third failed login, and when the
   session could not open: its link (INV.C) or its HELLO (scr_login). */
#define SCR_EXIT_LOGIN  5
#define SCR_EXIT_OPEN   2

/* INV.C: close the NET link, found lost, and call the server again:
   the new link, or NULL when the call failed.  The login screen calls
   it, for a NET link only, and makes the result tui_s.l. */
link *scr_reopen(void);

/* SCRPASS.C: SCMD_PASSWD, and SCMD_MUSTCHG, which cannot be left
   except by changing the password (TUI_CMD_NONE) or logging out
   (TUI_CMD_LOGIN, also when its form finds no memory, MT_NOMEM left on
   the hint row).  A LOGIN that demanded the change sends no full
   name, and none is fetched afterwards: tui_s.name stays "" until the
   next login. */
u8 scr_passwd(u8 cmd);

/* SCRLOGIN.C: overwrite every password a screen holds, the one being
   typed on the login screen and those of the open password form
   (form_wipe).  tui_exit can end the program while one is typed, so
   INV's exit path calls this. */
void scr_wipe(void);

#endif
