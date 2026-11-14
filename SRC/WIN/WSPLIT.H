/*
 * WSPLIT.H - the splitter between two panes of INVWIN's frame: a thin
 * bar the mouse drags across, in a child window (WSPLIT.C).
 *
 * Windows 3.0 has no splitter, so wsp_create makes a child window of
 * class "WSPLIT", WSP_WIDTH pixels wide, which its owner places
 * between its panes and keeps as high as they are.  Its cursor is
 * IDC_SIZEWE.  It is drawn as a COLOR_WINDOWFRAME line down each long
 * edge with COLOR_BTNFACE between them; no GDI object outlives a
 * message.
 *
 * The drag.  A press of the left button (a double click's second press
 * as a press) on an enabled splitter takes the mouse (SetCapture) and
 * the focus, so that ESC reaches it, and draws the guide: a band as
 * wide and as high as the splitter, inverted (DSTINVERT) on the screen
 * itself, so over the windows of its parent's client area whether or
 * not the parent clips its children (WS_CLIPCHILDREN).  While
 * the button is down the guide follows the pointer, its left edge kept
 * where it was against the pointer and held within the limits
 * (WSPM_LIMITS).  The button let go erases the guide, releases the
 * mouse and gives the focus back to the window that had it, if it is
 * still there; when the guide has left the splitter's place, the
 * splitter then moves there and sends its parent WM_COMMAND, wParam
 * its control ID, lParam MAKELPARAM(the splitter, WSPN_MOVED), from
 * which the owner lays its panes out again.  ESC during the drag, or
 * WM_CANCELMODE, which the owner sends as it is disabled, ends it the
 * same way but sends nothing.  A disabled splitter takes no press, and
 * there is no key to move it: the owner offers its own way to set the
 * place by hand.
 *
 * WSPM_LIMITS (wParam the lowest left edge, LOWORD(lParam) the highest,
 * in the parent's client coordinates) sets the limits; until then they
 * are 0 and the parent's client width less WSP_WIDTH, as it was made.
 * WSPM_GETLIMITS gives them back, MAKELRESULT(lowest, highest).
 */
#ifndef WSPLIT_H
#define WSPLIT_H

#include "COMPAT.H"
#include "CWIN.H"

#define WSP_WIDTH       4       /* the splitter's width in pixels       */

#define WSPM_LIMITS     (WM_USER + 0)   /* wParam lowest, lParam highest */
#define WSPM_GETLIMITS  (WM_USER + 1)   /* the limits, lowest low       */

/* The notification code of WM_COMMAND after a drag that moved it. */
#define WSPN_MOVED      1

/* Make a splitter of control ID id in owner, visible, WSP_WIDTH wide
   and of no height at owner's top left corner, for the owner to place;
   NULL when Windows made no window. */
HWND wsp_create(HWND owner, int id);

#endif
