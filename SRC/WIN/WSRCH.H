/*
 * WSRCH.H - the search of INVWIN (WSRCH.C): INV's item search, its
 * values asked for in the form dialog (WFORM.H) and its results the
 * source of an item screen of the frame (WSCR.H).
 *
 * The search dialog shows the search form of SCRSFORM.H, captioned
 * MS_TITLE, the focus on the field of the command that opens it and TAB
 * going round the fields from there, its location filter the form's
 * location field (scr_search_loc, WFORM.H).  Its values are no record:
 * OK saves nothing, and Cancel, Esc or Close asks nothing.  OK on
 * values that make no search gives their refusal (scr_search_why:
 * MS_EMPTYQ for none, MS_BOTHQ for a status beside Available) on the
 * dialog's message line, the focus on its first field, and the dialog
 * stays; otherwise it ends, the search they ask for set: ITEM FIND with
 * the text and the filters given, else ITEM SERIAL with the serial
 * number, else ITEM LIST with the filters, in location order when a
 * location is given, AVAIL=1 for Available (scr_search_args).  The
 * frame's Find box sets a search too, of its text alone (wsrch_find).
 * The search set is the last until the next OK or Find, whatever the
 * dialogs between leave.
 */
#ifndef WSRCH_H
#define WSRCH_H

#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "PAGER.H"
#include "FMODEL.H"

/* The source of the results of the search set, a template as itl_src of
   ITEMLS.H is, which the frame copies, giving it fmt: itl_src without
   its orders, with the search's command, args that add its parts
   (scr_search_args), and MS_NONE for no rows. */
extern lv_src wsrch_src;

/* Run the search dialog over owner, the dialog sending nothing but its
   pick lists' and its location's requests, over l for a user of access
   level level: for cmd SCMD_FIND, SCMD_SERIAL or SCMD_FILTER (SCR.H)
   with every value empty and the focus on the field of cmd, or, for
   TUI_CMD_NONE, with the values of the search set and the focus where
   its dialog started.  Returns wf_run's result: WF_OK with the dialog's
   search set; else WF_CANCEL, WF_EFAIL, or WF_LOGIN, WF_QUIT or WF_LOST
   after the failure of one of those (WFORM.H), keeping the search set
   as it was. */
int wsrch_ask(HWND owner, link *l, u8 level, u8 cmd);

/* Make the search set ITEM FIND of text (CP437), its blanks at both
   ends left out, without filters, as the search dialog makes it from
   its Text alone, the dialog to start on Text again: 1, or 0, the set
   unchanged, when no text is left. */
int wsrch_find(const char *text);

/* The title of the results of the search set (scr_search_title). */
const char *wsrch_title(void);

/* A new item's first values, a screen's preset (WSCR.H): those of
   scr_item_preset, the location the filter's when the search set is the
   text's or the filters'. */
void wsrch_preset(form *f);

#endif
