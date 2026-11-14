/*
 * WTAB.H - the tab strip of the Windows programs' dialogs: a row of
 * tabs over the page they choose, in a child window (WTAB.C).
 *
 * Windows 3.0 has no tab control, so wt_create makes a child window of
 * class "WTAB" drawn with the GDI alone.  Its window text is its tabs:
 * their texts in the Windows set, in order, each after a WT_SEP but
 * the first ("Overview|Technical|Acquisition|Notes"), so that the texts live
 * in the window's own memory, outside DGROUP, and a snapshot of the test
 * driver (TDRV.H) writes them as any window's text.  SetWindowText gives
 * the strip new tabs; it is then drawn again, and a current tab past
 * the last becomes the last, without a notification.  An empty text is
 * one empty tab; only the first WT_MAXTEXT bytes of the text and its
 * first WT_MAXTABS tabs count.
 *
 * Drawing.  The tabs stand left to right from the strip's left edge,
 * each as wide as its text and WT_PADX pixels on either side, its text
 * WT_PADY pixels below its top, the current one WT_LIFT pixels taller
 * and wider on either side than the others; tabs past the strip's right
 * edge are cut there.  Each is filled with COLOR_BTNFACE, outlined in
 * COLOR_WINDOWFRAME with its top corners cut, with a COLOR_WINDOW edge
 * inside its left and top and a COLOR_BTNSHADOW one, as wcol_unlike of
 * WCOLS.H gives it, inside its right (so WCOLS.C is linked too).  A
 * COLOR_WINDOWFRAME line runs along the strip's bottom but under the
 * current tab, which so stands joined to the page below.  The rest of
 * the strip is painted with the brush its owner answers to WM_CTLCOLOR
 * for a static (CTLCOLOR_STATIC), as the dialog behind it.  Texts are
 * drawn in the strip's font in COLOR_BTNTEXT, each with its mnemonic
 * underlined: the character after a single '&' in its text (the '&' not
 * drawn), else its first character; "&&" is a '&' shown.  While the
 * strip has the focus the current tab's text has a focus rectangle.  No
 * GDI object outlives a message but the font, which the owner owns.
 *
 * The current tab.  Tabs count from 0.  The strip opens on tab 0;
 * WTM_GETCUR returns the current tab, WTM_SETCUR (wParam the tab) makes
 * a tab current and returns it, or returns -1 and changes nothing when
 * there is no such tab.  Neither notifies the owner.  When the user
 * makes another tab current, the strip is drawn again, then sends its
 * owner WM_COMMAND, wParam its control ID, lParam MAKELPARAM(the strip,
 * WTN_CHANGE), so that the owner shows the page of WTM_GETCUR; the
 * user's choice of the tab already current sends nothing.  WTM_GETRECT
 * (wParam the tab, lParam a RECT FAR * to fill) gives a tab's rectangle
 * in the strip's client coordinates, as drawn, the current one's lift
 * included; it returns 0, the rectangle left alone, when there is no
 * such tab, else 1.
 *
 * The user chooses a tab:
 *
 *   a click        on a tab (WM_LBUTTONDOWN) makes it current and
 *                  gives the strip the focus; one outside every tab
 *                  only the focus
 *   Left, Right    with the focus on the strip, the tab before or after
 *                  the current one, none past the first or the last
 *                  (WM_GETDLGCODE asks for the arrows, DLGC_WANTARROWS)
 *   Ctrl+PgDn,     the next tab, and from the last the first, or the
 *   Ctrl+PgUp      one before, and from the first the last, wherever
 *                  the focus is in the strip's dialog, as Cardfile
 *                  turns its cards; Ctrl+Tab and Ctrl+Shift+Tab, which
 *                  the dialog manager would take for Tab, do the same
 *   Alt+letter     the tab whose mnemonic it is, regardless of case
 *                  (AnsiUpper), as is the letter alone while the focus
 *                  is on a window that takes no characters (its
 *                  WM_GETDLGCODE without DLGC_WANTCHARS), the strip's
 *                  own included, as the dialog manager takes a letter
 *
 * The strip is a tab stop of its dialog (WS_TABSTOP).  The last two
 * kinds of key come to the dialog's other controls, or to the dialog
 * manager, which takes Ctrl+Tab for Tab: the owner hands each key
 * message of its dialog to wt_key first, in its message filter
 * (WH_MSGFILTER, MSGF_DIALOGBOX) for a dialog box, in its message loop
 * for a window, and drops the message when wt_key returns 1, so that
 * the dialog manager, which knows nothing of the tabs, never sees a
 * tab's mnemonic.  A tab's mnemonic is so one of its dialog's access
 * letters, which no other control's may repeat.
 */
#ifndef WTAB_H
#define WTAB_H

#include "COMPAT.H"
#include "CWIN.H"

#define WT_SEP          '|'     /* ends each tab's text but the last    */
#define WT_MAXTABS      8       /* tabs that count                      */
#define WT_MAXTEXT      64      /* bytes of the window text that count  */
#define WT_PADX         6       /* pixels each side of a tab's text     */
#define WT_PADY         3       /* pixels above and below it            */
#define WT_LIFT         2       /* pixels the current tab stands out    */

#define WTN_CHANGE      1       /* WM_COMMAND's code: a tab chosen      */

#define WTM_GETCUR      (WM_USER + 0)   /* the current tab              */
#define WTM_SETCUR      (WM_USER + 1)   /* make tab wParam current      */
#define WTM_GETRECT     (WM_USER + 2)   /* tab wParam's rectangle       */

/* Make a strip of control ID id in owner, visible, its left, top and
   right edges those of *r in owner's client coordinates; *r's bottom is
   set to where the strip ends, as high as its font needs for the
   current tab's lift, its text, the focus rectangle and the bottom
   line.  Its tabs are string sid of the owner's instance (see above),
   its font font, NULL for ANSI_VAR_FONT, kept, never deleted, while
   the strip lasts.  NULL when Windows made no window or the string is
   missing. */
HWND wt_create(HWND owner, RECT *r, int id, UINT sid, HFONT font);

/* 1 when wm and wp, a key message of the strip w's dialog, are a press
   of Ctrl+PgDn, Ctrl+PgUp, Ctrl+Tab or Ctrl+Shift+Tab (WM_KEYDOWN), or of
   a tab's mnemonic with Alt (WM_SYSCHAR) or alone on a window that takes
   no characters (WM_CHAR): the tab it chooses is made current, the owner
   notified as for a click when it was not.  Else 0, nothing done. */
int wt_key(HWND w, UINT wm, WPARAM wp);

#endif
