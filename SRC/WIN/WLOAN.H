/*
 * WLOAN.H - the screens of loans, reservations and uses of INVWIN
 * (WLOAN.C), screens of the frame (WSCR.H) over the rows and forms of
 * SCRNFORM.H, each row of its kind.
 *
 * wloan_list, Items > Loans and holds, shows a view of every item's rows
 * (scr_loan_view), titled MH_T_VIEW plus the view, wloan_shown: Items > Loans
 * and holds starts on the open rows of every kind (SCR_LV_OPEN), and CTRL+PGDN
 * goes on to the next view, CTRL+PGUP back to the one before: the open loans,
 * reservations and uses, the overdue loans and reservations (LOAN OVERDUE),
 * every row, and the open rows again.  wloan_item, a scoped screen opened from
 * a record of the screens of items (its Loans and holds, WS_K_LOANS of
 * WSCR.H), shows the item's rows, open and ended, LOAN LIST ITEM=, in their
 * order, titled MH_T_ITEM; wloan_host (Used with it, WS_K_USED) the open rows
 * reserved for the item or in use with it, LOAN LIST HOST=, titled MH_T_HOST.
 * Each has INV's rows, the late ones marked, column headers and empty texts.
 * Beside wloan_list, in the split view, stands its pane, wloan_item for the
 * selected row's item (wpane_loan of WPANE.H).
 *
 * A row shows in the form of its kind (scr_loan_form), read with LOAN GET and
 * saved as LOAN, the maker and model of its item and of its host read with
 * ITEM GET after it (scr_loan_kitem, scr_loan_khost), its host an item field
 * of the frame's pick list (ws_pick_item); a saved row's caption is MH_TITLE
 * plus its kind.  A new row, which INS adds as a loan and CTRL+L, CTRL+R and
 * CTRL+U as a loan, a reservation and a use, as on the screens of items,
 * starts with scr_loan_new's values for its kind and the maker and model of
 * its item, and is sent with its kind's command.  It takes the scoped screen's
 * item; on wloan_list the item is asked for first, in a dialog titled MH_T_NEW
 * plus the kind of one field, ITEM_ID, labelled MI_L_ITEMID: the item's ID, or
 * text to find it in the pick list of the available items, which digits that
 * name no item also search for (the frame's ask, INVWIN.C).  On wloan_host
 * CTRL+R and CTRL+U add a reservation or a use for the host, its HOST_ID the
 * host's as a change and its item asked for the same way; INS and CTRL+L add
 * nothing there.  DEL does nothing, as no row is deleted.  Return or release
 * (WS_K_RETURN) ends the selected row, or each row chosen (WA_MULTI of
 * WSCR.H), with LOAN RETURN, on the date asked for in a dialog of one date
 * field, DATE, labelled MH_L_RETDATE and titled MH_Q_END plus the row's kind
 * with the row's ID; empty, the server takes today.  Open item (WS_K_ITEM) on
 * wloan_list and wloan_host shows the row's item in the item form.  A row
 * added or saved says so on the status bar, MH_ADDED or MH_SAVED, a loan
 * returned MH_ENDED and a reservation or a use released MH_RELEASED, each with
 * the row's caption.
 */
#ifndef WLOAN_H
#define WLOAN_H

#include "COMPAT.H"
#include "PAGER.H"
#include "WCOLS.H"
#include "WSCR.H"

extern const ws_scr wloan_list;
extern const ws_scr wloan_item;
extern const ws_scr wloan_host;

/* The view wloan_list shows, SCR_LV_ of SCRNFORM.H. */
extern u8 wloan_shown;

/* Make src, the frame's copy of wloan_list's source, show view
   wloan_shown; the columns of its rows (WCOLS.H). */
const wcol_tab *wloan_view(lv_src *src);

#endif
