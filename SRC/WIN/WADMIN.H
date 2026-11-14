/*
 * WADMIN.H - the commands of INVWIN's Admin menu beside Users (WADMIN.C),
 * as INV's (SCRADMIN.C), at level 3 only: the operations on the data
 * files, the audit log, a screen of the frame (WSCR.H), and the
 * sessions.  The tables and texts are those of SCRZFORM.H.
 *
 * wadm_op carries out Backup, Restore, Pack or Reindex, an operation of
 * scr_adm_ops.  Backup and Restore first ask for the directory, in the
 * form dialog over one field, DIR (labelled MZ_L_DIR, upper case,
 * required), captioned with the operation's title; Restore and Pack then
 * ask their question in a message box, its default button No.  Cancel
 * or No sends nothing.  The frame then sends the ADMIN command, held
 * busy with the hourglass however long it takes.  Its success is said
 * in a message box, the operation's note; a refusal naming DIR in INV's
 * words, in a message box as a failed request's (MZ_NODIR or
 * MZ_NOBACKUP for 200, MZ_DIRERR for 300..304); any other
 * failure as the frame shows a request's.
 *
 * wadm_filter asks for the audit log's filter, the form dialog over
 * scr_audit_def, captioned MZ_T_AUDIT, every field optional but REC,
 * which needs TBL (MZ_NEEDTBL, the focus on TBL); OK keeps its values
 * for wadm_audit, whose list shows AUDIT LIST with them, newest first,
 * with INV's rows, the failed logins and the changes cut off marked,
 * column header and empty text, titled MZ_T_AUDIT.  It has no record
 * form: ENTER, CTRL+E, INS and DEL do nothing.  Each Audit log asks for a
 * filter afresh, all empty.  Beside the users, as their pane (wpane_user of
 * WPANE.H), the same screen lists what the selected user did: its list then
 * has that user's name as its scope and asks AUDIT LIST USER= with it, the
 * filter left aside.
 *
 * wadm_sessions shows the sessions in dialog IW_R_SESS (INVWIN.H), captioned
 * MZ_T_SESS: the column header MZ_SHDR over a list box of the rows of ADMIN
 * CLIENTS, INV's, in the columns of scr_sess_cols but SELF, the requester's
 * own selected, the dialog widened for a row (wd_head, wd_item and wd_fit of
 * WDLG.H).  Refresh, or F5 in the list, fetches them again; Close or ESC ends
 * the dialog.  The first fetch runs once the dialog shows; a failed fetch, 104
 * in local mode among them, is shown over the dialog as a request's and ends
 * it, as INV leaves its screen.
 */
#ifndef WADMIN_H
#define WADMIN_H

#include "COMPAT.H"
#include "CWIN.H"
#include "WSCR.H"

/* Carry out cmd, SCMD_BACKUP to SCMD_REINDEX (SCR.H), over the frame,
   *done 1 once the server has made it, else 0; the action its failure
   leads to (rqe_class), RQE_STAY but after a failure that leaves. */
u8 wadm_op(HWND frame, u8 cmd, int *done);

/* Ask for the audit log's filter over owner; wf_run's result, WF_OK
   when wadm_audit is to show the list with it. */
int wadm_filter(HWND owner);

extern const ws_scr wadm_audit;

/* Show the sessions over the frame; as wadm_op's. */
u8 wadm_sessions(HWND frame);

#endif
