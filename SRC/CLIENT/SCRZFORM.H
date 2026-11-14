/*
 * SCRZFORM.H - the tables of the Admin menu's screens (SCRZFORM.C),
 * which each client's show: INV's (SCRUSER.C and SCRADMIN.C) and the
 * Windows client's.
 *
 * The users.  One table of fields serves both clients' user forms, as
 * the loan form's does (SCRNFORM.H): INV draws it in a box, the Windows
 * client maps the fields to the controls of a dialog by index.  The
 * fields, in the table's order: STATUS (A active, D disabled),
 * USERNAME, FULLNAME, GROUP, LEVEL (1 2 3, required), PASSWORD and
 * REPEAT.  USER SET takes the status and USER ADD none, making the user
 * active, so STATUS comes first and a new user's form is the rest of
 * the table.  USERNAME is the FF_KEY field, given once, upper case.
 * USER GET names the group GROUPNAME, while USER ADD and USER SET take
 * it as GROUP=: scr_user_kline loads GROUPNAME into GROUP.  PASSWORD and
 * REPEAT are FT_SECRET, typed without echo; REPEAT is FF_LOCAL, never
 * sent, and a client refuses a form where the two differ.  A new user's
 * password goes in its USER ADD; for a saved user a typed one is sent
 * first as USER RESET (scr_user_reset), which makes the user change it
 * at the next login, then, both wiped, the other changes as USER SET;
 * left empty, the password stays.  The user list is USER LIST in name
 * order, with the command's default fields (it takes no FIELDS=, so the
 * End walk fetches full rows, which are short).  A row holds, after one
 * blank, the user name, the full name, the group, the level, the
 * status, the must-change flag and the date of the last login in the
 * columns of MU_HDR; its key is the user name, and a disabled user's
 * row is marked.  The source's arg is any pointer but NULL.
 *
 * The operations on the data files.  scr_adm_ops holds, from
 * SCMD_BACKUP to SCMD_REINDEX (SCR.H), the ADMIN word each sends, the
 * title of its directory prompt (BACKUP and RESTORE take DIR=), the
 * question asked before it (RESTORE replaces every table, PACK drops
 * the deleted records for good) and the note of its success.  The
 * directory is one on the machine that runs the engine: the server, or
 * the client's in local mode.  A refusal naming DIR is told in the
 * client's words, as the code alone cannot say it: 200 is a directory
 * not found for BACKUP and no complete backup for RESTORE, 300..304 a
 * file of the backup that cannot be read or written; every other
 * failure, 402 (other sessions are open) and 102 (a bad directory name,
 * the data directory itself) included, is reported as any request's.
 *
 * The audit log.  A filter form of one row of local fields, named as
 * AUDIT LIST's keys (TBL, REC, STATION), so that the list's arguments
 * are the form's values that are not empty; REC without TBL is refused
 * in the form, as the server would refuse it.  The list is AUDIT LIST
 * with the command's default fields, newest first (a FIELDS= list
 * would not fit LINK_MAXFSPEC, so the End walk asks for AUD_ID alone):
 * after one blank, the date, the time as HH:MM, the user, the table,
 * the record, the action, the field and the old and the new value, in
 * the columns of MZ_HDR, the values cut to theirs; its key is AUD_ID,
 * and the rows of a failed login (action F) and of a change cut off
 * (X) are marked.
 *
 * The sessions: the rows of one ADMIN CLIENTS, unpaged (at most
 * PROTO_MAXPEERS), each value placed by the names of its F line, in the
 * columns of MZ_SHDR after one blank, the state in words.
 *
 * The texts are messages of SCRUSER.MSG and SCRADMIN.MSG.
 */
#ifndef SCRZFORM_H
#define SCRZFORM_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "PAGER.H"
#include "FMODEL.H"

/* The user list's title and texts, the question before a user is
   deleted, the notes of a user deleted, added and saved, the form's
   titles, the refusal of a repetition that differs and the note of a
   USER SET that failed after its USER RESET. */
#define MU_TITLE        760     /* Users                                */
#define MU_HDR          761     /* column header                        */
#define MU_EMPTY        762     /* No users                             */
#define MU_DELQ         764     /* Delete user ^1?                      */
#define MU_DELETED      765     /* User ^1 deleted                      */
#define MU_ADDED        766     /* User ^1 added                        */
#define MU_SAVED        767     /* User ^1 saved                        */
#define MU_FTITLE       768     /* User ^1                              */
#define MU_NEWTITLE     769     /* New user                             */
#define MU_NOMATCH      771     /* password and repetition differ       */
#define MU_PWONLY       779     /* USER RESET done, the USER SET failed */

/* The user form's labels. */
#define MU_L_STATUS     772
#define MU_L_USER       773
#define MU_L_NAME       774
#define MU_L_GROUP      775
#define MU_L_LEVEL      776
#define MU_L_PW         777
#define MU_L_REPEAT     778

/* The operations' titles, label, questions and notes. */
#define MZ_T_BACKUP     580     /* titles of the directory prompts      */
#define MZ_T_RESTORE    581
#define MZ_L_DIR        582     /* their label                          */
#define MZ_RESTOREQ     583     /* replace the data with ^1's backup?   */
#define MZ_PACKQ        584     /* drop the deleted records?            */
#define MZ_BACKEDUP     586     /* notes of success: ^1 the directory   */
#define MZ_RESTORED     587
#define MZ_PACKED       588     /* ^1 records removed                   */
#define MZ_REINDEXED    589
#define MZ_NODIR        590     /* 200 naming DIR: BACKUP, RESTORE      */
#define MZ_NOBACKUP     591
#define MZ_DIRERR       592     /* 300..304 naming DIR: ^2 rqe_text     */

/* The audit log's title, labels, column header, empty text and the
   refusal of REC without TBL. */
#define MZ_T_AUDIT      593     /* Audit log                            */
#define MZ_L_TBL        594
#define MZ_L_REC        595
#define MZ_L_STA        596
#define MZ_HDR          599
#define MZ_EMPTY        600     /* No audit rows                        */
#define MZ_NEEDTBL      601

/* The sessions' title, column header and states 0..3 in words. */
#define MZ_T_SESS       602     /* Sessions                             */
#define MZ_SHDR         603
#define MZ_STATE0       605
#define MZ_STATE1       606
#define MZ_STATE2       607
#define MZ_STATE3       608

/* A column of the audit list or of the sessions: its field, the cells
   it is drawn in (0: not drawn) and 1 to right-align it. */
typedef struct {
    const char *name;
    u8   w;
    u8   right;
} scr_col;

/* ---------------------------------------------------------------------
 * The users.
 * --------------------------------------------------------------------- */

/* Indexes of fields in scr_user_fields. */
#define SCR_UF_STATUS   0
#define SCR_UF_USER     1
#define SCR_UF_NAME     2
#define SCR_UF_LEVEL    4
#define SCR_UF_PW       5
#define SCR_UF_REPEAT   6
#define SCR_UF_COUNT    7

/* The width of PASSWORD and REPEAT, one more than any password, so that
   the server's check sees a password typed too long (FORM.H, FT_SECRET);
   each field's slot is one byte more. */
#define SCR_UF_PWW      (LINK_PWMAX + 1)

/* Widths of the user list's columns wider than their field (the Level,
   Status and Chg headers). */
#define UC_LEVEL        5
#define UC_STATUS       6
#define UC_MUSTCHG      3

/* USER, the users' table and the form's help topic; GROUPNAME, the
   list's field of the form's GROUP. */
extern const char scr_user_w_user[];
extern const char scr_user_k_group[];

/* The fields, and the form of all SCR_UF_COUNT of them, titled
   MU_NEWTITLE, in INV's box; a new user's form is the same without its
   first field, STATUS. */
extern const form_field scr_user_fields[];
extern const form_def scr_user_def;

/* The list's source, with fmt and mark below. */
extern const lv_src scr_user_src;

/* The source's functions: the row; its mark, 1 for a disabled user. */
void scr_user_fmt(void *arg, const lv_rec *r, char *text, char *key);
int scr_user_mark(void *arg, const lv_rec *r);

/* form_kline for USER GET, the user form arg also given the group
   GROUPNAME names. */
int scr_user_kline(void *arg, const rq_line *ln);

/* USER RESET name PASSWORD=pw through l (rq_send); the user then
   changes the password at the next login. */
int scr_user_reset(link *l, const char *name, const char *pw,
                   rq_res *res);

/* ---------------------------------------------------------------------
 * The operations on the data files.
 * --------------------------------------------------------------------- */

/* One operation: its ADMIN word, the title of its directory prompt (0:
   no directory), its question, ^1 the directory (0: none), the note of
   its success and the text of a 200 naming DIR. */
typedef struct {
    const char *word;
    u16  title;
    u16  ask;
    u16  done;
    u16  nodir;
} scr_adm_op;

/* ADMIN, the command and the operations' help topic; DIR, the key of
   the directory; the operations, from SCMD_BACKUP. */
extern const char scr_adm_w_admin[];
extern const char scr_adm_k_dir[];
extern const scr_adm_op scr_adm_ops[];

/* ---------------------------------------------------------------------
 * The audit log.
 * --------------------------------------------------------------------- */

/* Its fields, TBL, REC and STATION, and the form of all SCR_AF_COUNT of
   them, titled MZ_T_AUDIT, in INV's box of SCR_AF_H rows, one of fields,
   across the work area's top; the bytes of the longest value, the
   station's, and its NUL. */
#define SCR_AF_TBL      0       /* TBL's index                          */
#define SCR_AF_COUNT    3
#define SCR_AF_H        3
#define SCR_AF_VALSZ    (LINK_STALEN + 1)
extern const form_field scr_audit_fields[];
extern const form_def scr_audit_def;

/* The audit list's columns not sized by LINK.H: the time as HH:MM, a
   table name (the longest the engine writes, also the filter's width),
   an old or a new value. */
#define AU_TIMEW        5
#define AU_TBLW         6
#define AU_VALW         10

/* AUDIT LIST; AUD_ID, the name of the audit row's ID and the list's
   key; TBL and REC, the names of the table's and the record's filter;
   STATION, the row's station. */
extern const char scr_audit_w_list[];
extern const char scr_audit_k_id[];
extern const char scr_audit_k_tbl[];
extern const char scr_audit_k_rec[];
extern const char scr_audit_k_sta[];

/* The list's columns after the date, SCR_AU_NCOLS of them: ATIME,
   USERNAME, TBL, REC_ID, ACTION, FLD, OLDVAL and NEWVAL, each one's
   field, its cells and 1 to right-align it. */
#define SCR_AU_NCOLS    8
extern const scr_col scr_audit_cols[];

/* The list's source, a template whose args are scr_audit_args, arg
   NULL: a client copies it and sets arg to the filter form. */
extern const lv_src scr_audit_src;

/* The source's functions: the filter form arg's values that are not
   empty as the request's pairs; the row; its mark.  fmt and mark take
   any arg but NULL. */
void scr_audit_args(void *arg, rq *q);
void scr_audit_fmt(void *arg, const lv_rec *r, char *text, char *key);
int scr_audit_mark(void *arg, const lv_rec *r);

/* ---------------------------------------------------------------------
 * The sessions.
 * --------------------------------------------------------------------- */

/* The columns of ADMIN CLIENTS, as many as SCR_SS_NCOLS, in the order
   of MZ_SHDR: each one's field, the cells it is drawn in and 1 to
   right-align it.  The last, SELF (1 for the requester's own session),
   is not drawn; the STATE before it is drawn in words, from MZ_STATE0
   on. */
#define SCR_SS_NCOLS    8
#define SCR_SS_STATE    5
#define SCR_SS_SELF     7
extern const scr_col scr_sess_cols[];

#endif
