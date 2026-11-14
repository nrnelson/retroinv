/*
 * WHELP.H - INVWIN's help: Windows Help over the compiled help file, or
 * the help window over the sections of its text, and the topic F1 shows
 * (WHELP.C).
 *
 * Both files come from SRC/WIN/INVWIN.HTX.  The text, WH_TEXT, is
 * INV.HLP's format (TEST/hlpchk.py builds and checks both): an index of
 * sections, then the sections, each a topic of upper-case letters and
 * lines of at most WH_HELPW characters of CP437.  The help file, WH_HLP,
 * is HC.EXE's compilation of the same sections, each topic's context
 * number by WH_CTXMUL (ctx of WHELP.C), and section WH_HELPINDEX its
 * index.  The text is looked for in the program's directory, else in
 * the current one, and read a section with one read when it is shown
 * (msg_section of MSG.H); WH_HLP in the program's directory alone, as
 * Windows Help takes a name without a directory as one in its own.
 *
 * Help is shown in Windows Help: WinHelp with HELP_CONTEXT, always for
 * the frame, so that HELP_QUIT, which wh_done sends once help has been
 * shown there, names the window that asked.  When WH_HLP is not found
 * or WinHelp fails (no WINHELP.EXE), the help window shows the section
 * instead.
 *
 * The topic of the moment is that of the field with the focus in the
 * open form dialog, its form's topic, '_' and the field's name (wf_topic
 * and wf_field of WFORM.H), when WH_TEXT has that section; else the
 * form's; else the one wh_topic set last: the frame sets its screen's
 * (ws_scr's topic of WSCR.H) as it opens the screen's list, and each of
 * its other dialogs' around the dialog.  F1, pressed without SHIFT, CTRL
 * or ALT, shows the topic of the moment over the active window: in the
 * frame's message loop through wh_key, in every dialog box through a
 * message filter of the program's (WH_MSGFILTER), which wh_init sets and
 * wh_done removes.  F1 does nothing while a menu or a message box is
 * open.
 *
 * The help window, dialog IW_R_HELP of INVWIN.RC, is modal: an OEM text
 * control of WEDIT.H in OEM_FIXED_FONT, read-only, WH_HELPW characters
 * wide and multi-line with a vertical scroll bar, filling its client
 * area, which shows the whole section, CR LF between its lines; the
 * keys of an edit control scroll it.  ESC, ENTER and the system menu's
 * Close end it, and F1 inside it shows section WH_HELPKEYS in place of
 * the one shown, as INV's help box does.  A section that cannot be read
 * is said in a message box over the window it was asked over:
 * MT_NOMEM, else MT_NOHELP with the topic.
 */
#ifndef WHELP_H
#define WHELP_H

#include "COMPAT.H"
#include "CWIN.H"

#define WH_TEXT         "INVWIN.HTX"
#define WH_HLP          "INVWIN.HLP"
#define WH_HELPW        70      /* characters of a line of the text     */
#define WH_HELPKEYS     "KEYS"  /* the section of the keys everywhere   */
#define WH_HELPINDEX    "INDEX" /* Windows Help's index, not in WH_TEXT */
/* The context number of a topic: h = h * WH_CTXMUL + c over its
   characters c, modulo 2^32. */
#define WH_CTXMUL       31

/* Set the message filter for F1, inst the program's instance, frame the
   window Windows Help is shown for, dir the program's directory, where
   WH_TEXT is looked for first and WH_HLP alone (ending in a separator),
   which is kept, not copied, until wh_done.  0 when Windows gave no thunk
   for the filter: F1 then works only in the frame. */
int wh_init(HINSTANCE inst, HWND frame, const char *dir);

/* Close Windows Help (HELP_QUIT) if help was shown there, and remove the
   message filter, if set; called while the frame is still a window. */
void wh_done(void);

/* Make topic, a section of WH_TEXT in static storage, the topic of the
   moment when no form dialog gives one; NULL keeps the one set.
   Returns the topic set before, WH_HELPKEYS before the first. */
const char *wh_topic(const char *topic);

/* 1 when m is the press of F1 without SHIFT, CTRL or ALT, the help of
   the moment then shown over the active window, or the keys' section
   in the help window when it is that window; else 0. */
int wh_key(const MSG *m);

/* Show topic, NULL for the topic of the moment, in Windows Help, else
   in the help window over owner until it is closed.  A window of the
   program holding the mouse is first sent WM_CANCELMODE, which ends its
   drag: Windows Help's window comes up over the program without
   disabling any window of it, and a drag left going would move its
   guide across that window. */
void wh_show(HWND owner, const char *topic);

#endif
