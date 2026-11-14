/*
 * WLIST.H - the paged list window of the Windows programs: one list
 * command through a pager, in a child window (WLIST.C).
 *
 * A list window shows a pager (PAGER.H) over one paged command (ITEM
 * LIST, LOC LIST, ...) and moves it with the keys, as INV's list view
 * (LISTV.H) does on the text screen.  It holds only the rows of the
 * pager's window, never the whole table.  It keeps near memory and
 * handles: the rows the pager keeps are in its far blocks (CMEM), and
 * the only far pointers kept from one message to the next are the list
 * box's procedures, its own and the thunk that replaces it, which
 * Windows keeps valid as segments move.
 *
 * Windows.  wl_open makes a child window of class "WLIST" with the
 * control ID the owner gives, whose text is the column header of the
 * order in use, drawn on its first line as headings (wcol_heads of
 * WCOLS.H, Headings below): its table's words (wcol_tab's head) laid
 * out in its columns by wcol_head, or for a table without them message
 * src->header[order].
 * Under the header lies its one child, a list box of control ID
 * WL_BOXID with owner-drawn rows of one height (LBS_OWNERDRAWFIXED)
 * that keeps its strings (LBS_HASSTRINGS), so that a snapshot of the
 * test driver (TDRV.H) writes them; the box has no scroll bar, as it
 * never holds more than the window's rows, but the window has one
 * (WS_VSCROLL; Length and scroll bar, below), and one across (WS_HSCROLL)
 * over the pixels by which the row of the order's columns passes the
 * box: its lines a cell, its pages the box's width, its thumb tracked;
 * the header and the rows scroll together, and the bar is gone while
 * the row fits.  Rows drawn whole (a NULL table) never scroll.
 *
 *   - Row i of the box is the pager's row first + i, for each row of
 *     the pager's window that is held (at most ps of them).  Its string
 *     is the row's display text without its trailing spaces, its item
 *     data the row's mark (1 or 0).
 *   - The box's selection is the pager's selected row, the focus row,
 *     and it has none when no rows are held; a list of several rows
 *     selects more (Several rows, below).  A list that has ended without
 *     rows holds one row instead, the text of message src->empty cut at
 *     LV_ROWTEXT, never selected, its item data WL_WHOLE: it is drawn
 *     whole.  A row the owner marks as a drop target has WL_DDROP in its
 *     item data too (Rows dragged, below).
 *   - Header and rows are CP437, as the data and the message file are,
 *     and the box's strings stay so: a snapshot reads them through
 *     AnsiToOem, which keeps ASCII as it is.  They are drawn in
 *     ANSI_VAR_FONT, a stock font, in the columns of the order in use
 *     (wl_open's cols, laid out and drawn by WCOLS.H), each piece
 *     converted to the Windows set; the header is split at the same
 *     cells as the rows, and the empty text is drawn whole (WCOLS.H's
 *     NULL table).  The selected row is drawn in the system's highlight
 *     colours, a marked row in red, and the focus rectangle around the
 *     focus row while the box has the focus, wherever the box's own caret
 *     is.  A row is WL_ROWPAD
 *     pixels higher than a line of the font, its text centred, so that
 *     the focus rectangle runs through blank pixels.
 *   - The window is made wl_width wide for the first order's table and
 *     wl_height high for ps rows, at the top left corner of the
 *     parent's client area, visible.  The owner moves it where it
 *     belongs and may size it: the box always fills the window under
 *     the header line, and when it is too low for the rows held, it
 *     scrolls to keep the selected one in view (LB_SETCURSEL).
 *   - The parent clips its children (WS_CLIPCHILDREN).  A list box of
 *     Windows 3.0 paints itself as it takes the focus, ahead of the
 *     paint messages waiting, so when the parent is uncovered and
 *     activated, before the parent erases its background: an erase
 *     that took in the box would leave all its rows but the focus row
 *     blank.
 *
 * Keys.  The window gives the focus it gets to the box.  With the focus
 * there:
 *
 *   Up, Down, PgUp, PgDn, Home, End    the pager's moves PG_UP, PG_DOWN,
 *                      PG_PGUP, PG_PGDN, PG_HOME and PG_END, whatever
 *                      SHIFT and CTRL are doing, but for SHIFT with Up
 *                      and Down in a list of several rows (below) and
 *                      CTRL with PgUp and PgDn
 *   Left, Right        scroll the columns a cell, as the horizontal bar
 *                      does
 *   SHIFT, CTRL        alone, dropped
 *   any other key      a WL_KEY note with its virtual key code (Enter,
 *                      Ins, Del, Esc, F1..F9, Tab, Ctrl-PgDn, Ctrl-E,
 *                      ...), during which GetKeyState gives SHIFT and
 *                      CTRL as they were when the key was typed
 *
 * Tab and Shift-Tab are the owner's, as INV's list view leaves them to the
 * screen, so that a list in a dialog lets the focus go on or back.  Characters
 * (WM_CHAR) are dropped, so the box never selects a row by its first
 * letter as Windows' list box would, unless the owner asks for letters
 * (wl_letters): then a character typed, beyond the blank, seeks the
 * next row whose text, its leading blanks skipped, starts with it in
 * either case, going on at the first row after the last, as a move does
 * (pg_seek of PAGER.H, at most LV_SEEKMAX rows).  System keys (ALT and
 * its letters, F10) go on to Windows, so the menus work.  The box asks
 * a dialog for every key (DLGC_WANTALLKEYS), so a list in a dialog gets
 * Tab, Enter and Esc too.  With the mouse, a click selects the row
 * clicked, moving the selection row by row within the window, and a
 * double click is the WL_KEY note of VK_RETURN; the scroll bar's moves
 * are below.  wl_right takes a right press, for the owner's popup menu.
 * Each key, click and scroll gesture is taken in a message the window
 * posts to itself, so it runs from the program's message loop with no
 * list box or scroll bar code under it.
 *
 * Several rows.  A list opened with WL_MULTI, which has no lead row, can
 * choose several rows of its window, as File Manager's lists do.  Its
 * box takes LBS_EXTENDEDSEL, but the list keeps the selection itself and
 * sets the box's selection to show it: Windows 3.0 has no message that
 * sets an extended box's caret or anchor, so the box never handles the
 * left button, whose presses and releases are the list's.  The focus
 * row is the pager's selected row, which every move, wl_selected and
 * wl_text keep acting on; the selected rows are the box's (LB_GETSEL);
 * the anchor is the row SHIFT extends from.  After a quiet move or a
 * click (below), the box's selection follows the focus row:
 *
 *   a plain click, Up, Down, CTRL with Up or Down   the focus row alone,
 *                      the anchor moved to it
 *   SHIFT+click, SHIFT+Up, SHIFT+Down          the rows from the anchor
 *                      to the focus row alone
 *   CTRL+SHIFT+click   those rows added to the selection
 *   CTRL+click         the focus row's selection toggled, the anchor
 *                      moved to it
 *   a right press on a selected row (wl_right) the selection kept
 *
 * A plain press on a selected row while several are chosen keeps them
 * until its release on that row, which then selects that row alone; any
 * other press, a fill and the row's drag out forget it.  A double click's
 * second press is a press, then the WL_KEY note of VK_RETURN.  SHIFT with
 * Up at the list's first row, or with Down at its last, does nothing.  A
 * move that is not quiet fills the box again (Moves, below), which leaves
 * the focus row alone selected, so the selection lives in the window
 * shown: PgUp, PgDn, Home, End, a heading, the scroll bar, wl_move
 * and wl_find drop it, as an Up or Down leaving the window does, but
 * for one with SHIFT while several rows are chosen, which beeps and
 * keeps them, the focus row where it was.  The rows chosen (wl_nchosen,
 * wl_chosen) are the selected rows in the box's order, or the focus row
 * alone when none is selected; none without rows.  A quiet move or a
 * click that changes their number is noted WL_SEL; a busy move's WL_BUSY
 * note stands for it.
 *
 * Add mode, as File Manager's, picks rows scattered over the window from the
 * keyboard.  SHIFT+F8 turns it on and off, and a plain click, or any press
 * that chooses its row alone, turns it off.  In it Up, Down and CTRL with Up
 * or Down move the focus row and keep the selection, SHIFT with Up or Down, or
 * with a click, adds the rows from the anchor to the focus row, and Space
 * toggles the focus row's selection, the anchor moved to it; Space is a WL_KEY
 * note otherwise.  A change of the mode is noted WL_SEL too (wl_adding).
 *
 * All rows.  wl_select_all, which the owner calls for CTRL+/ (WL_VK_ALL, a
 * WL_KEY note as any key the list does not take), chooses every row of the
 * list, not only those of the window: one busy move (Moves, below) that
 * surveys the list as it is then (psv_survey of PAGERSV.H), whatever
 * WL_EXACT says, and keeps the key of each row in a set in far memory
 * (KEYSET.H: KS_KEYW bytes a row, in one block of CMEM_MAXBLK at most), the
 * owner noted WL_COUNT at each of its waits.  The list is counted by it as
 * by any survey.  While all are chosen the add mode is off, each row of
 * the window whose key the set holds is drawn selected, after each fill
 * too, wl_nchosen and wl_chosen give the set's keys, in the list's order as
 * it was surveyed, and the focus row stays the pager's.  PgUp, PgDn, Home,
 * End and the scroll bar's gestures keep them chosen, as a press held on a
 * selected row and a right press on one do (for a drag of them all, and
 * the owner's popup menu).  Every other gesture that moves the focus row
 * or chooses rows leaves the focus row alone chosen: Up and Down, with
 * SHIFT or CTRL or without, a click with or without them, a held press's
 * release, SHIFT+F8 (which then turns the add mode on), a letter's seek, a
 * heading's switch, and each move of the owner's (wl_move, wl_find), as
 * after a command that changed the list.  wl_deselect, which the owner
 * calls for CTRL+\ (WL_VK_NONE, a WL_KEY note too), leaves the focus row
 * alone chosen and ends the add mode, whatever was chosen.  The choice is
 * of the rows as surveyed: a row added since is not in it, and is not
 * drawn selected, and a row deleted since still is, for the owner's
 * command to find gone.  A list of more rows than the set holds
 * (KS_MAXROWS, fewer without memory for its block to grow), or than the
 * survey counts (PSV_MAXPAGES pages of the window's rows), chooses none,
 * WL_EOVER, the survey stopped at its first wait after the set refused a
 * key; an Esc, a failed request and an empty list choose none either.  A
 * wl_select_all that chooses none leaves the rows chosen before it, all of
 * them too, and the add mode as they were, the box not filled again.  A
 * survey that kept no page starts (stopped, failed or without memory)
 * leaves the list the count it had, whole or not, without the page starts
 * of a jump.  Without memory for the survey nothing is chosen and nothing
 * said.
 *
 * Headings.  The header line's columns are headings: a column whose
 * flags name one of src's orders (WCOL_ORDER of WCOLS.H) is a button,
 * the order in use's with the arrow, and every other column flat.  A
 * press of the left button on the header line gives the box the focus.
 * On a button it holds the button down, drawn pressed while the pointer
 * stays on it; released there, it switches the list to the button's
 * order, unless that is the order in use: the order before it selected,
 * then PG_ORDER, posted as a key's move is.
 * A list opened with WL_SIZES whose table keeps widths (wcol_tab's keep),
 * while the program has a widths store (wcol_kept), has dividers: a
 * press within WCOL_GRIP pixels of a column's right edge drags it, the
 * pointer IDC_SIZEWE there, an inverted guide WL_GUIDEW pixels across
 * following the pointer down the list window, on the screen itself,
 * with the column held from WCOL_MINW to WCOL_MAXW average characters
 * wide.  A release elsewhere than at the press keeps the
 * guide's width, rounded to whole average characters, as the column's
 * in the store (wcol_set_width), when it differs from the width it has;
 * the list then lays its columns out again and notes WL_COLS.  Esc (the
 * box has the focus), WM_CANCELMODE, and a capture lost end a press or
 * a drag with nothing done, as destroying the list does.  A button held
 * or a divider dragged holds the capture, so the box's keys are dropped
 * meanwhile (Esc ends it).  A press while a move is busy, or while the
 * window holds the capture already, is ignored; in a list without
 * dividers a press near a column's right edge is on its heading.
 *
 * Rows dragged.  A list opened with WL_DRAGS lets its rows be dragged
 * out, to be dropped where the owner says.  A press of the left button
 * on a row of the box, not the empty text, while no move is busy and
 * nothing holds the capture, goes to the box as a click's press does,
 * and arms the drag.  Until the pointer has moved more than WL_DRAGMIN
 * pixels from the press on either axis, its moves go nowhere, so the
 * selection stays on the row pressed, and a release there is the click
 * (a double click's second press, WM_LBUTTONDBLCLK, never arms).  The
 * first move beyond ends the box's press where it was, as a click, and
 * drags the row out: the box holds the capture, and the owner is noted
 * WL_DRAG, val WL_DR_START, then WL_DR_OVER at that move and at each one
 * after.  The release notes WL_DR_DROP, from a message the box posts the
 * list window, so the owner's requests run from the program's loop;
 * the drag's last point, of the release, is then the one wl_drag_over
 * reads.  Meanwhile the box's keys but Esc, and its characters, go
 * nowhere.  Esc, WM_CANCELMODE to the list window or to its box (which
 * Windows sends the box as a dialog opens), and a capture lost, found at
 * the box's next message, end the drag with WL_DR_END; a drop that comes
 * while a move is busy is noted WL_DR_END too.  They end a row only
 * pressed with no note, its release then the click, the capture a list
 * of several rows took for it given back.  Destroying the list ends a
 * drag with no note.  A drop target is the owner's to mark (wl_target):
 * its row is framed in inverted pixels, WL_DDROP in its item data, apart
 * from the selection.
 * In a list of several rows the press is the list's own (Several rows),
 * not the box's, and the box holds the capture while it is armed: a
 * press held on a selection keeps the rows chosen for the drag, the
 * owner's to carry (wl_chosen), and the drag's start forgets the held
 * press, so that no release after it selects that row alone.
 *
 * Moves.  A key's move, a click's, a scroll's, a letter's seek, a
 * heading's switch, or the owner's own through wl_move or wl_find, is
 * busy unless it only moves the selection within the window (Up and
 * Down not leaving it, a click) while the window is full of rows or the
 * list ends with the rows held, a quiet move, which sends no request:
 *
 *   1. a WL_BUSY note with 1 and the hourglass;
 *   2. pg_move, or the scroll's moves, then the survey when the move
 *      counts the list (below); between two of their requests (PAGER.H's
 *      wait function, wl_wait) the window dispatches the messages
 *      waiting but keyboard and mouse input, so the program repaints
 *      and other programs run.  The keys and clicks, the scroll bar's
 *      included, are dropped; an Esc among them stops the move, which
 *      keeps the rows it has fetched (PAGER.H), and a WM_QUIT stops it
 *      too and is posted again for the main loop;
 *   3. a WL_BUSY note with 0 and the arrow, then the box filled again,
 *      the header changed with the order and the scroll bar set;
 *   4. after a failed request, the client's text for it (rqe_class of
 *      RQERR.H, for the user level the owner gave) in a message box over
 *      the parent: stop icon for RQE_BOX, exclamation for RQE_LINE,
 *      captioned as the parent is.  A move that fails twice, on a bad
 *      cursor and then on the restart at the first page (PAGER.H), shows
 *      the second failure alone;
 *   5. when that failure's action is not RQE_STAY, a WL_LEAVE note with
 *      it (RQE_LOGIN, RQE_QUIT or RQE_LOST of RQERR.H's RQERR_CLASS
 *      section): the owner leaves the list.
 *
 * A move that sends no request sets the scroll bar too: the bar is set
 * after every move, a failed or stopped one included.
 *
 * Length and scroll bar.  The protocol tells no list's length, so the list
 * counts.  With WL_EXACT, which the owner gives for a fast link (LOCAL, NET,
 * TCP: a survey takes milliseconds to a few seconds a thousand rows), a count
 * is pending from wl_open, unless wl_count_set gives one, and again from
 * wl_recount, until a survey ends other than by failing: it counted to the
 * list's end or to a full table, or Esc stopped it.  In step 2, after any busy
 * move that succeeded (a key's, a click's, a scroll's, a letter's, the
 * owner's, or wl_find's before its seek) while a count is pending, and after
 * every PG_ORDER that succeeded, the survey of PAGERSV.H (psv_survey) runs for
 * the order now in use; a move that sends no request surveys nothing, and a
 * move stopped by Esc did not succeed.  A failed survey is the move's failure
 * (steps 4 and 5), its page shown, the count still pending; a survey stopped
 * by Esc leaves no count (the growing range) and none pending; after either,
 * wl_find's seek is still made, so a count that failed never costs the row
 * asked for.  A stop because the list was destroyed or the program is to end
 * ends the move there, as a move's does, wl_find's seek not made.  A failed
 * PG_ORDER surveys nothing, as it loads nothing (PAGER.H).  Without WL_EXACT
 * (COM, MBOX, whose requests take seconds) nothing is surveyed.  With WL_EXACT
 * or without, a move after which the rows held end the list with above known
 * counts the list as a survey to its end would, b + pg_rows (the survey's rows
 * and all, below), so that moving back up keeps T exact; wl_count_add adjusts
 * that count as any whole-list count.  That count and a survey's are dropped,
 * as a failed survey drops them, by a move after which above is PG_NOABOVE
 * (the list changed under the pager), by PG_ORDER before its survey and by
 * wl_recount.  After a move that leaves above PG_NOABOVE, with WL_EXACT, the
 * count is pending again, and the first busy move that succeeds with above
 * known (Home) surveys.
 * Then, in the pager's numbering (PAGER.H: a lead row is row 0), with b the
 * pager's above, taken as 0 while it is PG_NOABOVE:
 *
 *   - the window's top row is t = b + first;
 *   - the list's rows are T = b + pg_rows when the rows held end the
 *     list (pg_at_end) and above is known; else, when the list is
 *     counted whole (all) and above is known, the greater of
 *     the survey's rows and b + pg_rows; T is then exact.
 *     Otherwise T grows as the list is seen: b + pg_rows, plus ps while
 *     a page follows the rows held, and at least the survey's rows when
 *     above is known;
 *   - the bar's range is 0 to M = T - ps (0 when T <= ps, which hides
 *     the bar), in units of u rows: u is 1 while M <= WL_MAXPOS, else
 *     M / WL_MAXPOS + 1, so that range and position fit the int a
 *     Windows 3.0 scroll bar takes.  Its position is min(t, M) / u, and
 *     the range's top position stands for row M, not M / u * u.
 *
 * The bar acts per gesture, not per message.  Windows sends WM_VSCROLL from
 * inside its tracking loop while the mouse button is down, and repeats
 * SB_LINEUP, SB_LINEDOWN, SB_PAGEUP and SB_PAGEDOWN while it is held, so the
 * window only records them: the gesture's code and its count of line or page
 * codes, at most ps for lines and 1 for pages (what one window's move can
 * need), and SB_THUMBPOSITION's last position; SB_THUMBTRACK changes nothing.
 * A gesture starts where Windows starts tracking: WM_NCLBUTTONDOWN or
 * WM_NCLBUTTONDBLCLK on the list window with HTVSCROLL drops any record before
 * DefWindowProc enters the loop, so a gesture whose SB_ENDSCROLL never came is
 * lost, never merged into the next.  A code of another kind than the one
 * recorded likewise drops the record and starts a new gesture.  At
 * SB_ENDSCROLL, which Windows sends as its loop ends, the window posts one
 * move for what is recorded, whatever holds the capture, and forgets it, so
 * nothing is fetched while Windows tracks.  While the list window holds the
 * capture (GetCapture), as during Windows' loop, the list's posted keys,
 * characters and clicks are dropped, as they are while a move is busy; one
 * taken without the capture also drops any record left.  A gesture whose codes
 * come while a move is busy is dropped.  At most one such move is pending: a
 * gesture that ends while one is posted and not yet taken is dropped, and the
 * mark is cleared whenever the posted message is taken, whether its move is
 * made or dropped (a move busy then).  The moves: SB_LINEUP and SB_LINEDOWN,
 * the count's PG_UP or PG_DOWN moves; SB_PAGEUP and SB_PAGEDOWN, PG_PGUP or
 * PG_PGDN; SB_TOP and SB_BOTTOM, PG_HOME and PG_END; SB_THUMBPOSITION, a jump
 * to the top row r, the position times u (M at the range's top), or nothing
 * when the position is the bar's own (the thumb released where it was).  The
 * test driver posts WM_VSCROLL itself, with no WM_NCLBUTTONDOWN or
 * WM_NCLBUTTONDBLCLK: its codes up to their SB_ENDSCROLL form one gesture.
 * That Windows' own loop with a real mouse makes one move per gesture, after
 * the loop, is checked by hand on the test machine.
 *
 * A jump is one busy move.  When r is M and T is exact, it is PG_END.  Else,
 * when the survey kept page starts, the kept page holding r (r / ps), or the
 * last kept when r lies past them, is loaded in one fill (psv_jump), and a
 * failed psv_jump ends the jump; then, or from the window as it is without
 * kept page starts, the window steps: PG_PGDN while r >= t + ps, PG_PGUP while
 * r < t.  Then PG_DOWN moves until t is r, and PG_UP moves bring the selection
 * to the window's top row, so the window shows rows r to r + ps - 1, or ends
 * on the list's last row when fewer follow; these fetch no page beyond the one
 * after the page reached.  The steps stop when a move fails or leaves t and
 * the selection as they were (the list's start or end).  Before every step but
 * the first that may send a request, the window calls the pager's wait
 * function itself, which stops the jump on an Esc, a WM_QUIT or a destroyed
 * list.  A move stopped so, or by a wait of its own (an Esc among a gesture's
 * line moves stops the rest of them), did not succeed and surveys nothing.
 *
 * The texts of headers, the empty list and failures are messages,
 * fetched when shown, so the owner has loaded its message file
 * (msg_load) before the first move.
 */
#ifndef WLIST_H
#define WLIST_H

#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "PAGER.H"
#include "WCOLS.H"

typedef struct wl wl;           /* a list window (opaque)               */

/* The window class of a list window, "WLIST". */
extern const char wl_k_class[];

#define WL_BOXID        1       /* control ID of the list box           */
#define WL_ROWPAD       2       /* pixels a row adds to a line          */
#define WL_WHOLE        2       /* item data of the empty text's row    */
#define WL_GUIDEW       2       /* pixels across a divider's guide      */
/* The highest position of a Windows 3.0 scroll bar, an int. */
#define WL_MAXPOS       32767

/* wl_open's flags. */
#define WL_EXACT        0x01    /* count the list's rows by a survey    */
#define WL_SIZES        0x02    /* its dividers can be dragged
                                   (Headings, above)                    */
#define WL_DRAGS        0x04    /* its rows can be dragged out (Rows
                                   dragged, above)                      */
#define WL_MULTI        0x08    /* it can choose several rows (Several
                                   rows, above)                         */

/* The pixels a press moves, on either axis, before its row is dragged
   out; Windows 3.0 has no metric for it. */
#define WL_DRAGMIN      4
/* A box row's item data bit of the drop target (wl_target). */
#define WL_DDROP        0x04
/* No row (wl_drag_over, wl_target). */
#define WL_NOROW        (-1)

/* Notes to the owner (wl_note_fn's ev). */
#define WL_KEY          0       /* val the virtual key code of a key the
                                   list does not take                   */
#define WL_BUSY         1       /* val 1: a move starts that may send
                                   requests; 0: it has sent its last    */
#define WL_LEAVE        2       /* val the action of a failure,
                                   RQE_LOGIN, RQE_QUIT or RQE_LOST      */
#define WL_COLS         3       /* val the keep of the table whose
                                   column a divider's drag has widened
                                   or narrowed in the store             */
#define WL_DRAG         4       /* val a WL_DR_ step of a row's drag    */
#define WL_SEL          5       /* val the rows chosen (wl_nchosen),
                                   their number or the add mode changed,
                                   or a choice of every row ended, by a
                                   quiet move, a click, a key of a list
                                   of several rows or wl_deselect       */
#define WL_COUNT        6       /* val the rows wl_select_all's survey
                                   has counted so far, during its move  */

/* The virtual keys of / and \ on the US keyboard, which Windows 3.0's
   headers do not name: with CTRL, File Manager's Select All and Deselect
   All (All rows, above). */
#define WL_VK_ALL       0xBF
#define WL_VK_NONE      0xDC

/* The steps of a row's drag (WL_DRAG's val). */
#define WL_DR_START     0       /* dragged out                          */
#define WL_DR_OVER      1       /* the pointer has moved                */
#define WL_DR_DROP      2       /* released, at the last point          */
#define WL_DR_END       3       /* ended with nothing done              */

/* The owner's function, called with the arg of wl_open, the list and a
   note.  A move sends its requests between WL_BUSY 1 and 0, so the owner
   holds its own work meanwhile: its requests (RQ refuses a nested one)
   and the test driver (td_busy).  The move's message box comes after
   WL_BUSY 0, so that a key script can answer it.  WL_COUNT comes inside
   that bracket, for the owner to show and no more.  WL_KEY, WL_LEAVE,
   WL_COLS, WL_SEL and WL_DRAG are the last thing the list does in the
   call that makes them.  The owner may move the list there with wl_move,
   but for WL_DRAG's WL_DR_START, WL_DR_OVER and WL_DR_END, which may come
   from the box's procedure: only WL_DR_DROP comes from the list window's
   own message.

   The owner may destroy the list, or its parent, at any time, in a note
   but those WL_DRAG notes, or during a move too.  A list destroyed
   during a move only notes that it is gone: the move stops at its next
   wait, sends its WL_BUSY 0 note (in which v is not to be used), shows
   no box, makes no WL_LEAVE note and frees the list as it ends. */
typedef void (*wl_note_fn)(void *arg, wl *v, int ev, UINT val);

/* wl_open's result beside PG_OK, PG_EARG and PG_ENOMEM. */
#define WL_EWIN         3       /* Windows refused a window             */

/* Make *out a list window of control ID id in parent, a top-level window
   (a frame or a dialog) that clips its children, over src on link l, its
   rows drawn in the columns of cols, its pager's window ps rows high;
   level is the user's access level (rqe_class), flags any of WL_EXACT,
   WL_SIZES, WL_DRAGS and WL_MULTI, or 0, note the owner's function and
   arg its argument.  cols holds a table for each of src->orders, in their
   order (one without orders), or is NULL for rows drawn whole, as WCOLS.H's
   NULL table.  The first order is in use, unless wl_set_order selects another,
   and nothing is fetched yet: the owner's first move (PG_HOME, or wl_find)
   loads the list.
   Returns PG_OK; PG_EARG or PG_ENOMEM as pg_open does, PG_ENOMEM also
   when the list's near memory or the box's procedure thunk could not be
   had; or WL_EWIN.  After a failure *out is NULL and nothing is held.

   src, cols and what they point to stay unchanged while the list
   lives.  Destroying the window, or its parent, frees the list (at the
   end of a move under way): v is not used after. */
int wl_open(wl **out, HWND parent, UINT id, link *l, const lv_src *src,
            const wcol_tab *cols, u8 ps, u8 level, u8 flags,
            wl_note_fn note, void *arg);

/* The list's window. */
HWND wl_hwnd(wl *v);

/* Select the sort order name, one of src->orders, before the first
   move, without fetching: 1, or 0 when it is none of them, the order in
   use then staying (pg_set_order). */
int wl_set_order(wl *v, const char *name);

/* Make move m, one of the moves of pg_move (PAGER_CALLS), as a key's
   move is made, the focus row alone chosen after it (All rows, above):
   busy, a count started where it starts one (Length and scroll bar,
   above), the box filled again, a failure shown and left by WL_LEAVE.
   Returns pg_move's code, or the survey's when the move succeeded and
   the survey failed.
   Not called while the list is busy (between WL_BUSY 1 and 0); once
   the owner has destroyed the list, in a note of the move or before, v
   is not used again. */
int wl_move(wl *v, int m);

/* Fetch the list again from its first page and select the row of key,
   as a move does: the first row when it is that row, else the row that
   pg_seek of PAGER.H finds or stops on, walking at most LV_SEEKMAX
   rows, after the survey when a count is pending; pg_move's code, the
   survey's when it failed, else pg_seek's.  Not called while the list
   is busy, as wl_move. */
int wl_find(wl *v, const char *key);

/* Give the list a lead row before its first move: key and text, which
   the list keeps, CP437 (pg_lead of PAGER.H). */
void wl_lead(wl *v, const char *key, const char *text);

/* Have the list seek a row by a character typed (Keys, above). */
void wl_letters(wl *v);

/* Have the list page the response of its command itself, before its
   first move: a command that takes no PAGE= or AFTER= and answers with
   its whole list (pg_unpaged of PAGER.H). */
void wl_unpaged(wl *v);

/* The selected row's key into key (LV_KEYSZ bytes) and 1, or 0 with key
   "" when no rows are held (pg_selected). */
int wl_selected(wl *v, char *key);

/* The selected row's text into text (LV_ROWTEXT + 1 bytes) and 1, or 0
   with text "" when no rows are held. */
int wl_text(wl *v, char *text);

/* The row of list t's box under the last point of v's row drag (Rows
   dragged, above), and its key into key (LV_KEYSZ bytes) unless key is
   NULL; or WL_NOROW when the point lies outside t's box, below its last
   row or on its empty text.  t may be v.  Nothing is sent, drawn or
   noted. */
int wl_drag_over(wl *v, wl *t, char *key);

/* Mark row i of the box as the drop target, the row marked before
   unmarked, each drawn again; WL_NOROW marks none.  A move's refill
   marks none. */
void wl_target(wl *v, int i);

/* wl_select_all's result beside the RQ_ codes of RQ.H. */
#define WL_EOVER        4       /* more rows than can be chosen         */

/* Choose every row of the list, one opened with WL_MULTI (All rows,
   above), as a key's move is made: RQ_OK with all chosen, or with what
   was chosen before after an Esc, without memory for the survey or in
   an empty list; WL_EOVER, the same kept, which the owner says; or the
   failed request's code, shown and left as a move's failure.  Not called
   while the list is busy, as wl_move. */
int wl_select_all(wl *v);

/* Leave the focus row alone chosen and end the add mode (All rows,
   above), noted WL_SEL when that changed either or ended a choice of
   every row; nothing in a list without WL_MULTI, which chooses no
   other.  It sends no request.  Not called while the list is busy. */
void wl_deselect(wl *v);

/* 1 while every row of the list is chosen (wl_select_all), else 0. */
int wl_all(wl *v);

/* The rows chosen (Several rows, above): every row of the list while all
   are chosen, else the selected rows, else 1 for the focus row, 0
   without rows; a list without WL_MULTI 1 or 0. */
u16 wl_nchosen(wl *v);

/* 1 while the list is in add mode (Several rows, above), else 0. */
int wl_adding(wl *v);

/* Chosen row i, from 0, in the box's order: key not NULL, its key into
   key (LV_KEYSZ bytes), and text not NULL, its text into text
   (LV_ROWTEXT + 1 bytes), and 1; or 0, key "", when i >= wl_nchosen.
   While all are chosen, row i of the list as it was surveyed, its text
   "", as the list holds the rows of its window alone. */
int wl_chosen(wl *v, u16 i, char *key, char *text);

/* The focus row's row in the box, WL_NOROW without rows. */
int wl_focus(wl *v);

/* A right press at (x, y) in the box's client area, for the owner's popup
   menu: the box focused and the row there the focus row, the selection
   kept when that row is selected in a list of several rows, else that
   row alone, posted as a click is, so it is made before what the owner
   posts after; nothing below the rows. */
void wl_right(wl *v, int x, int y);

/* The index of src->orders in use (0 without orders), as the format
   function needs it to choose its columns. */
u8 wl_order(wl *v);

/* The list's rows, its lead row not counted, into *n and 1 when they
   are exact (T of Length and scroll bar, above), as of the last move or
   of a wl_recount since, which drops them at once; else 0, *n
   unchanged.  wl_count_add and wl_count_set change them at the next
   move, which also sets the bar. */
int wl_count(wl *v, u32 *n);

/* Count n more rows (fewer for n < 0) in a survey that counted the whole list,
   for the records the owner has added or deleted itself, before the move that
   shows the change (PG_RELOAD, or wl_find): never fewer than b + pg_rows (b of
   Length and scroll bar, above), so a count never wraps.  The survey's page
   starts no longer match the rows, so they are dropped and a jump steps
   (Length and scroll bar, above) until the next survey.  Nothing without such
   a survey. */
void wl_count_add(wl *v, int n);

/* Give the list, before its first move and after wl_lead, the exact count n of
   its rows, its lead row not counted, as wl_count gave it for a list of the
   same source, order and arguments that this one replaces (the frame's list
   opened again for a new height): no count is pending, and the count is as a
   survey's to the list's end that kept no page starts, so jumps step.  Page
   starts are kept for a window of ps rows, so they are not carried over. */
void wl_count_set(wl *v, u32 n);

/* Make the count pending again without a move, the survey's rows, page
   starts and count dropped as a failed survey leaves them, for changes the
   owner cannot count itself (an import, a restore, rows leaving a filtered
   list): its next busy move that succeeds surveys (Length and scroll bar,
   above), so the owner calls wl_recount and then wl_move(PG_RELOAD).
   wl_count gives no count from the call on.  Without WL_EXACT the count
   is dropped all the same, and none is pending. */
void wl_recount(wl *v);

/* The geometry of a list window in its font, ANSI_VAR_FONT, read through
   a display context of w (any window of the program), which owners use
   to size the window or the dialog around it: wl_width the width for
   table t (WCOLS.H, NULL as wl_open's cols), x[n] of wcol_layout plus
   the box's border and the scroll bar (SM_CXVSCROLL); wl_height the
   height for n rows, a line for the header and n rows of the font's
   tmHeight + WL_ROWPAD plus the box's border, without the horizontal
   bar, which a window at least wl_width wide never shows; wl_rows the
   most rows of a window h pixels high and cx wide, from 1 to
   LV_MAXPAGE, under the horizontal bar when the row of table t (NULL:
   none) is wider than the window's box. */
int wl_width(HWND w, const wcol_tab *t);
int wl_height(HWND w, u8 n);
u8 wl_rows(HWND w, int h, int cx, const wcol_tab *t);

/* The wait between two requests of a fetch, a list's or another's (a
   pick list's tags): dispatch the messages waiting but keyboard and
   mouse input, which is dropped, leaving the hourglass; 1 when an Esc
   was among it, WL_QUIT when the program is to end, its WM_QUIT posted
   again for the main loop; else 0. */
#define WL_QUIT         2
int wl_wait(void);

#endif
