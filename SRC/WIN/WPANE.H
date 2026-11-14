/*
 * WPANE.H - the panes of INVWIN's screens (WPANE.C): what each screen
 * that has one shows beside its list (ws_pane of WSCR.H), which the
 * frame opens, loads and lays out as INVWIN.H says (The split view).
 *
 * The locations' pane, WINPANE.LOC of the settings, is the selected
 * location's contents, the frame's screen ws_contents: following a
 * location sets it as the one whose contents show (wloc_enter of
 * WLOC.H: LOC GET, and the type of the first location inside it), or
 * reads it again, what is set for it kept (wloc_refresh), when it is
 * that one already, as the tree opens again on it.  Without a width in
 * the settings the tree is as wide as its Name and Type columns, the
 * location pick list's (wrow_tpick_cols of WROWS.H), and the pane's
 * count is IW_S_NHERE with the location's name (wloc_cname).
 *
 * The items' pane, WINPANE.ITEM, beside the item list and a search's
 * results, is a preview panel (WPREV.H) of the
 * selected item, IW_S_PLOAD with its ID while it loads: its ID, maker
 * and model, the fields above the item form's tabs, then those of the
 * form's Overview page (wrow_item_pages of WROWS.H), each under the
 * form's label with its value as the form shows it in FORM_VIEW
 * (wf_view_text of WFORM.H), the category with its name, the held
 * units without a label, the location as the form's location field
 * shows it, its code, or its ID without one, and under it, without a
 * label, its path, or the form's text for no location, a Parent as its
 * ID alone; then, when the item has notes, their label and their lines,
 * as a load makes them (MEMOIO.H), and after the MEMO_MAXLINES lines of
 * notes that have more, or the lines of notes that the server cut
 * short, the item form's text for those, MM_TOOMANY or MM_TRUNC.  The
 * panel's block holds all of that at its longest (WPANE.C checks
 * WPV_BYTES against it), so no line is left out unsaid.
 * ITEM GET reads the fields, LOC GET the location of an item that has
 * one and ITEM NOTES the notes, the frame held busy over them all; an
 * Esc typed meanwhile stops the load before its next request, and an
 * item deleted meanwhile shows IW_S_PGONE in the panel.
 * Without a width in the settings the list is as wide as its columns in
 * the ID order (wrow_item_cols).
 *
 * The assemblies' pane, WINPANE.ASSY, is the selected assembly's parts,
 * the frame's screen ws_parts with its commands, its scope the
 * assembly, which following sets with no request; the list is as wide
 * as beside the items' pane, and the pane's count is the parts' own, as
 * when they show alone.
 *
 * The loans' pane, WINPANE.LOAN, beside every view of the loans and
 * holds, is the history of the selected row's item, its loans and holds
 * open and ended, the screen wloan_item of WLOAN.H with its commands, a
 * new one being that item's; its scope is the item the row names, which
 * following reads from the row's text with no request, so rows of one
 * item give it the same scope.  The list is as wide as its columns, at
 * most half the frame, and the pane's count is its screen's.
 *
 * The categories' pane, WINPANE.CAT, is the selected category's items,
 * the frame's screen ws_catitems with the item list's commands and
 * orders, a new item taking the category; its scope is the category's
 * code, which following sets with no request.  The categories are as
 * wide as their columns (wrow_code_cols of WROWS.H), at most half the
 * frame, and the pane's count is its screen's, of items.
 *
 * The users' pane, WINPANE.USER, is what the selected user did: the
 * audit log's screen, wadm_audit of WADMIN.H, which has no commands,
 * its scope the user's name, set with no request, for which its list
 * asks AUDIT LIST USER= in place of the audit log's filter.  The users
 * are as wide as their columns, at most half the frame.  Both need
 * TUI_LVL_ADMIN, the users' screen by its menu entry and AUDIT LIST by
 * the server, so the pane is never offered to a user it would be
 * refused.  The server finds a user's entries through its index of the
 * audit log by user, so the pane is counted and fetched again as the
 * others are.
 */
#ifndef WPANE_H
#define WPANE_H

#include "COMPAT.H"
#include "CWIN.H"
#include "WSCR.H"

extern const ws_pane wpane_loc;
extern const ws_pane wpane_item;
extern const ws_pane wpane_assy;
extern const ws_pane wpane_loan;
extern const ws_pane wpane_cat;
extern const ws_pane wpane_user;

#endif
