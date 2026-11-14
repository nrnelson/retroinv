/*
 * WDLG.H - the metrics, the place, the width and the look of the
 * Windows programs' dialogs, their list boxes of columns and the message
 * boxes over them (WDLG.C).
 *
 * Windows 3.0 opens a dialog where its template says, from its owner's
 * client area, and has no DS_CENTER, which came with Windows 4.0, so
 * each dialog procedure calls wd_centre as WM_INITDIALOG, once the
 * dialog has its size and before Windows shows it; the templates say
 * 0, 0.  The preview of WRPT.H alone takes its owner's place instead.
 *
 * The look.  Every dialog procedure of the program begins with
 *
 *     if (WD_LOOKS(wm))
 *         return wd_look(dlg, wm, wp, lp);
 *
 * so that its dialog is grey and its fields sunken, as a 3.0 push button
 * is raised.  The dialog, its statics, check boxes and buttons stand on
 * the stock LTGRAY_BRUSH, in COLOR_BTNTEXT, when COLOR_BTNFACE is that
 * brush's colour and the display shows it solid; otherwise, as on a
 * monochrome display, where the grey would be a dither under the text,
 * they keep Windows' own colours.  Edits, combo boxes and list boxes
 * keep Windows' white, the margin a combo box fills about its text
 * included.  Each well, a visible child with WS_BORDER or of class Edit
 * (whose border is not WS_BORDER), ComboBox or WLIST, has a one-pixel
 * frame around it: a shadow (wcol_unlike of WCOLS.H) along its left and
 * top, white along its right and bottom.  The frames are drawn by the
 * dialog's WM_PAINT alone, so a procedure that hides or moves a well
 * while its dialog shows invalidates the dialog.  No GDI object outlives
 * a message.
 */
#ifndef WDLG_H
#define WDLG_H

#include "COMPAT.H"
#include "CWIN.H"
#include "WCOLS.H"

/* The metrics of Windows 3.0's dialogs, in dialog units: the margin
   inside a dialog's edges, a push button's size and the gap between
   neighbouring buttons and groups of controls. */
#define WD_MARGIN       7
#define WD_BTNW         50
#define WD_BTNH         14
#define WD_GAP          4

/* The window class of an edit control, "Edit". */
extern const char wd_k_edit[];

/* Centre the dialog dlg over its owner's window, or over the screen
   when it has none or its owner is minimized, as an icon's place says
   nothing of where the dialog belongs, and move it, its size kept,
   onto the screen where it passes an edge: the right and bottom first,
   then the left and top, so that a dialog larger than the screen keeps
   its caption on it. */
void wd_centre(HWND dlg);

/* Widen dialog dlg so that its list box box shows a row of the columns
   t (WCOLS.H) as a list window of WLIST.H does (wl_width), as a
   template's dialog units need not match the font: the dialog and the
   controls of wide (box among them) widened by as much, those of move
   moved right by as much, each list ended by 0.  A box wide enough
   already changes nothing. */
void wd_fit(HWND dlg, int box, const wcol_tab *t, const int *wide,
            const int *move);

/* A list box of columns in a dialog: a box of LBS_OWNERDRAWFIXED |
   LBS_HASSTRINGS, its rows CP437 strings of at most LV_ROWTEXT characters,
   under a static control, its header, whose text is set before the dialog,
   as it opens, gives it to wd_head.  The header is then drawn by its
   dialog's procedure, which a static of Windows 3.0 cannot ask for
   (SS_OWNERDRAW came with 3.1): as it paints, it sends its dialog
   WM_DRAWITEM itself, its text and its client area but the box's border
   width on the left, so that its columns stand over the rows'.  The
   dialog's procedure passes WM_MEASUREITEM and WM_DRAWITEM, wm, with their
   lp to wd_item with the rows' columns rows and the header's head
   (WCOLS.H), and answers TRUE: each row is as high as a list window's
   (WLIST.H), and the rows are drawn in ANSI_VAR_FONT in their columns as
   a list window draws its rows, but with no mark (wcol_item of WCOLS.H),
   the header as a list window's flat headings (wcol_heads).  The header
   keeps its text and its class, and is drawn whole should its procedure
   not be replaced. */
void wd_head(HWND hdr);
void wd_item(HWND dlg, UINT wm, LPARAM lp, const wcol_tab *rows,
             const wcol_tab *head);

/* Show text, in the Windows set, in a message box over the window w of
   style st, an MB_ICON value for the OK button alone, or the buttons as
   well, captioned with w's own caption, so that a dialog's question or
   refusal names the dialog; the button chosen. */
int wd_say(HWND w, UINT st, const char *text);

/* The icon of the message box of a failure whose rqe_class show (RQERR.H)
   is s: the stop icon for RQE_BOX, the exclamation for RQE_LINE. */
#define WD_FAILICON(s)  ((s) == RQE_BOX ? MB_ICONSTOP : MB_ICONEXCLAMATION)

/* The messages a dialog procedure hands to wd_look (The look, above). */
#define WD_LOOKS(wm)    ((wm) == WM_CTLCOLOR || (wm) == WM_PAINT)

/* The answer of dialog dlg's procedure to the message wm, wp, lp of the
   look (above): for WM_CTLCOLOR, LTGRAY_BRUSH for the dialog, a static
   or a button of a grey dialog, not an edit, combo box or list window,
   the dc's colours set, else FALSE; for WM_PAINT, the frames painted and
   TRUE; FALSE for any other. */
BOOL wd_look(HWND dlg, UINT wm, WPARAM wp, LPARAM lp);

#endif
