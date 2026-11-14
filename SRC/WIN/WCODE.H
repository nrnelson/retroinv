/*
 * WCODE.H - the code lists of INVWIN, the categories and the location
 * types, each in a dialog of its own (WCODE.C), as INV's code list
 * screens (SCRCODE.C).
 *
 * wcode_run shows the dialog IW_R_CODES (INVWIN.H), captioned ME_CATS or
 * ME_LTYPES (SCRKFORM.H): the list, CAT LIST or LTYPE LIST held whole
 * (CODETAB.H), a row each: the code in LINK_LTYPELEN cells, the name in
 * LINK_CODENAMELEN and the sort order right-aligned in CT_ORDERW, a blank
 * between, drawn in those columns under the column headings ME_HDR, whose own
 * columns end where the rows' do, the dialog widened for a row (wd_head,
 * wd_item and wd_fit of WDLG.H); beside it the buttons Edit (the default, as
 * ENTER, CTRL+E and a double click), New (INS), Delete (DEL) and Close (ESC);
 * F5 fetches the list again, the selection on the row it had.  Edit shows the
 * selected entry in the form dialog of WFORM.H over scr_code_defs of
 * SCRKFORM.H, its name and sort order (SORTORD) to change, captioned ME_FCAT
 * or ME_FLT with the code; New shows a new entry there, titled ME_NEWCAT or
 * ME_NEWLT, its code given once, as the word of ADD, and never changed; each
 * saved as CAT or LTYPE.  A form that cannot be made says IW_S_NOFORM.  Delete
 * sends CAT DEL or LTYPE DEL once a message box asking ME_DELQ, ^1 the entry's
 * name, its default button No, has been answered Yes; the server refuses a
 * code in use.  The list is fetched again after a form or a delete, the
 * selection on the entry changed, or on the row it had.  New, Edit and Delete,
 * and their keys, do nothing below TUI_LVL_OPER, where the buttons are
 * disabled and the list is only shown.  The help topic, CATS or LTYPES, is the
 * dialog's and its form's.  A failed request is shown over the dialog as a
 * list's is (ws_fail); one whose action leaves ends it.
 *
 * Edit, New and Delete are wcode_form and wcode_del, which the frame's
 * list of the categories (INVWIN.H, The categories) carries out on its
 * rows too, so that an entry has one form, one question and one set of
 * rules wherever it is listed.
 */
#ifndef WCODE_H
#define WCODE_H

#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "CODETAB.H"

/* wcode_run's result when its dialog could not be made. */
#define WC_NODIALOG     0xFF

/* Show code list kind, SCR_CK_CAT or SCR_CK_LTYPE of SCRKFORM.H, over
   owner with the link l and the user's level (rqe_class) until it ends:
   the action of a failure that ended it (RQE_LOGIN, RQE_QUIT or
   RQE_LOST), else RQE_STAY, or WC_NODIALOG, said by no one. */
u8 wcode_run(HWND owner, link *l, u8 level, u8 kind);

/* Show entry *r of code list kind, as its list gave it, in the form
   dialog over w with the link l and the user's level, to change its
   name and sort order, or with r->code "" a new entry: the dialog's end
   (wf_run of WFORM.H), WF_EFAIL also when the form could not be made,
   said by no one.  After WF_OK r->code is the code saved, a new entry's
   the one given, and r->name its name. */
int wcode_form(HWND w, link *l, u8 level, u8 kind, ct_row *r);

/* Delete entry *r of code list kind once the question over w, ME_DELQ
   with its name, has been answered Yes: WF_OK once deleted, WF_GONE when
   it had gone already (200), WF_CANCEL after No or a failure, shown
   (ws_fail), that stays, else that failure's action as WF_LOGIN,
   WF_QUIT or WF_LOST. */
int wcode_del(HWND w, u8 kind, const ct_row *r);

#endif
