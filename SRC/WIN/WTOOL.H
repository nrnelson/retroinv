/*
 * WTOOL.H - the tool bar of INVWIN's frame: a row of buttons with
 * pictures under the menu, in a child window (WTOOL.C).
 *
 * Windows 3.0 has no tool bar, so wtb_create makes a child window of
 * class "WTOOL" holding a push button of style BS_OWNERDRAW for each
 * command.  The bar's window text says what it holds, in order, each
 * piece after a WTB_SEP but the first: WTB_GAP alone for a gap between
 * groups, WTB_SLOT alone for the slot its owner fills with controls of
 * its own (WTBM_SLOT), and any other piece, the empty one included, for
 * a button, whose window text the piece is
 * ("New|Open|Edit|Delete|-|Move|Install|Lend|-|*|Find").  Only the
 * first WTB_MAXTEXT bytes of the text and its first WTB_MAXBTNS buttons
 * count; the text is set once, by wtb_create.
 *
 * The buttons.  Button k, counting from 0, has the control ID cmds[k]
 * of wtb_create, its command, and shows glyph k of the bitmap resource
 * named as the class, "WTOOL", of the owner's instance, which
 * wtb_create loads once and the bar frees as it is destroyed, locked
 * only while a button is drawn: the picture SRC/WIN/INVTOOL.PY writes,
 * one column WTB_GLYPH pixels wide, glyph k in the rows from
 * 2 * k * WTB_GLYPH down, as it is drawn, and its grayed form the
 * WTB_GLYPH rows below that.  A button past the bitmap's last glyph
 * shows none.  In the bitmap's 16 colours light grey stands for
 * COLOR_BTNFACE as the display shows it (GetNearestColor) and dark grey
 * for wcol_unlike's COLOR_GRAYTEXT, the 14 others for themselves.
 *
 * Drawing.  The bar is COLOR_BTNFACE with a COLOR_WINDOWFRAME line
 * along its bottom.  The pieces stand left to right, each where the
 * one before it ends, the first at WTB_PADX, but a button after a
 * button one pixel sooner, so that the two share their frame line: a
 * button is WTB_BTNW wide, a gap WTB_SPACE, the slot as wide as
 * WTBM_SLOT last reserved, 0 until then.  The buttons and the slot are
 * WTB_BTNH high, WTB_PADY below the bar's top.  Pieces past the bar's
 * right edge are cut there.  A button is drawn as Windows 3.0 draws a
 * push button: a COLOR_WINDOWFRAME frame with its corners left out,
 * its face COLOR_BTNFACE, its glyph centred, a white edge inside its
 * left and top and a two-pixel COLOR_BTNSHADOW one, as wcol_unlike
 * (WCOLS.H) gives it, inside its right and bottom; pressed, one such edge
 * inside its left and top and its glyph a pixel right and down;
 * disabled, the glyph's grayed form.  No GDI object outlives the
 * drawing of a button.
 *
 * The focus.  The bar never takes the focus from its owner's windows:
 * neither it nor a button is a tab stop, nothing here calls SetFocus,
 * and each button's mouse messages (WM_LBUTTONDOWN, WM_LBUTTONDBLCLK,
 * WM_MOUSEMOVE, WM_LBUTTONUP) are taken by the bar's own procedure for
 * its buttons, which the class Button's would answer by taking the
 * focus; every other message goes on to the class's procedure.  The
 * slot's controls are the owner's and behave as their classes do.
 *
 * A click.  A button that is enabled takes the mouse (SetCapture) as it
 * is pressed, a double click's second press as a press, and shows
 * itself pressed (BM_SETSTATE) while the mouse is over it; when the
 * button is let go over it the button shows itself raised again, then,
 * while the button and the bar's parent are both enabled, the bar
 * sends its parent WM_COMMAND, wParam the button's command, lParam
 * MAKELPARAM(the button, BN_CLICKED), as a push button sends its own
 * parent; let go elsewhere, nothing is sent.  A disabled button takes
 * no click, posted or real.  A press is let go, the mouse released and
 * the button raised, without a command, when the button gets
 * WM_CANCELMODE, which Windows 3.0 sends the window holding the mouse as
 * EnableWindow disables it; WM_CANCELMODE sent to the bar goes to the
 * button that holds the mouse, which the owner sends as it is disabled,
 * for a request or a modal box over it, since a press may have lost the
 * mouse to that box.  WM_COMMAND that the bar gets from any of its other
 * children, the slot's, goes on to its parent unchanged.
 *
 * The owner grays a button, or makes it usable again, with
 * EnableWindow(GetDlgItem(bar, cmd), on); the button is drawn again.
 * WTBM_SLOT (wParam a width in pixels, lParam a RECT FAR * to fill)
 * reserves that width for the slot, moves the buttons after it and
 * gives the slot's rectangle in the bar's client coordinates, where the
 * owner puts its controls as children of the bar; it returns 1, or 0,
 * the rectangle left alone, when the bar has no slot.  WTBM_BUTTON
 * (wParam k) returns button k's window as an LRESULT, or 0 when there
 * is no such button.
 */
#ifndef WTOOL_H
#define WTOOL_H

#include "COMPAT.H"
#include "CWIN.H"

#define WTB_SEP         '|'     /* ends each piece of the text but the
                                   last                                 */
#define WTB_GAP         '-'     /* a piece that is a gap                */
#define WTB_SLOT        '*'     /* a piece that is the owner's slot     */
#define WTB_MAXBTNS     10      /* buttons that count                   */
#define WTB_MAXTEXT     64      /* bytes of the window text that count  */
#define WTB_GLYPH       16      /* pixels of a glyph's side             */
#define WTB_BTNW        24      /* a button's width in pixels           */
#define WTB_BTNH        22      /* its height                           */
#define WTB_PADX        4       /* pixels left of the first piece       */
#define WTB_PADY        3       /* pixels above and below the buttons   */
#define WTB_SPACE       8       /* pixels a gap puts between pieces     */
/* The bar's height in pixels: the buttons, the space above and below
   them and the bottom line. */
#define WTB_HEIGHT      (WTB_PADY + WTB_BTNH + WTB_PADY + 1)

#define WTBM_SLOT       (WM_USER + 0)   /* reserve wParam pixels        */
#define WTBM_BUTTON     (WM_USER + 1)   /* button wParam's window       */

/* Make a bar of control ID id in owner, visible, its left, top and
   right edges those of *r in owner's client coordinates; *r's bottom is
   set to its top and WTB_HEIGHT.  Its pieces are string sid of the
   owner's instance (see above), its buttons' commands cmds, one for
   each button, in order.  NULL when Windows made no window, or the
   string or the bitmap is missing. */
HWND wtb_create(HWND owner, RECT *r, int id, UINT sid, const UINT *cmds);

#endif
