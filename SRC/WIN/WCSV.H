/*
 * WCSV.H - the export and import of INVWIN (WCSV.C): Export CSV and
 * Import CSV of the Admin menu, the form and requests of SCRCFORM.H in
 * the Windows program's dialogs.
 *
 * The file is on the machine that runs the engine.  When that is this
 * one, the local link's, the file name dialog of WFILE.H comes first,
 * captioned as the form, listing this machine's directories and their
 * files *.CSV, and the path it gives goes into the form's file; over a
 * link to a server, whose directories this machine cannot list, the file
 * is typed in the form as the server names it, as in INV.  Then the form
 * dialog of WFORM.H shows the form of scr_csv_form with the values of
 * scr_csv_preset; its Cancel asks nothing.  OK checks the form and sends
 * from the dialog, held busy (wf_ops' busy):
 *
 *   Export: the export's requests, the records written so far on the
 *   frame's help panel (ws_info of WSCR.H) between them, and an Esc
 *   pressed meanwhile, while the program is active, stops the export
 *   with MX_STOPPED on the message line.  A first answer of 201 naming
 *   FILE asks MT_REPLACE (Yes, No) in a message box over the dialog: Yes
 *   starts the export again, replacing the file, and No goes back to the
 *   file's name.  A 204 in a later answer gives MX_LOST on the message
 *   line.
 *
 *   Import: MX_IMPORTQ (Yes, No) in a message box over the dialog first,
 *   No going back to the file's name, then MX_WAIT on the help panel
 *   while the IMPORT runs.  Refused lines show in a message box over the
 *   dialog: MX_T_REFUSED, the lines the server lists (MX_REFLINE),
 *   MX_REFCOUNT and, when an error followed them, MX_REFERR; nothing was
 *   added, and the dialog stays.
 *
 * A file error naming FILE (scr_csv_ferr) and a 102 show on the message
 * line, the focus on their field; any other failure as the form dialog
 * shows one.  Once records are written, the dialog ends, and a message
 * box over the owner (ws_say of WSCR.H) says how it went:
 * MX_EXPORTED, MX_ADDED, or MX_PARTIAL when adding failed after some
 * records.  The dialog is not held busy while a box over it waits.
 *
 * Both entries need level 3, below which the menu grays them (INVWIN.H);
 * the server checks the level again.
 */
#ifndef WCSV_H
#define WCSV_H

#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"

/* Run Export CSV (cmd SCMD_EXPORT of SCR.H) or Import CSV (SCMD_IMPORT)
   over owner, the frame, on the link l for a user of level (rqe_class);
   local is 1 when the engine runs in the program.  wf_run's result
   (WFORM.H): WF_OK once records were written, WF_CANCEL, WF_LOGIN,
   WF_QUIT or WF_LOST, or WF_EFAIL when a dialog could not be made. */
int wcsv_run(HWND owner, link *l, u8 level, int local, u8 cmd);

#endif
