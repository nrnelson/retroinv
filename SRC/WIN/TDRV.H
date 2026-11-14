/*
 * TDRV.H - the test driver of the Windows programs: key scripts in,
 * snapshots of the window tree out (TDRV.C).
 *
 * A program's /KEYS:file and /SNAP:file switches, which it parses
 * itself, hand td_init a key script and a snapshot file, so that a run
 * gives the same file on every machine and in every mode of Windows.
 * The driver owns a timer, whose messages every message loop dispatches,
 * the program's own and those of DialogBox, MessageBox and an open
 * menu.  Windows makes a timer message only when the program's queue
 * holds nothing else to do, no posted message, input or painting.  A
 * tick is idle when, besides, the program has not said by td_busy that
 * a request is outstanding, a window of the program, or of Windows
 * Help's task while it runs, holds the focus, and the active window, if
 * it is one of theirs, is visible: a dialog box shows itself only when
 * its loop first finds nothing to do, and a tick already due as the loop
 * starts comes first.  A menu that a key
 * opens drops the same way, and an open menu bar with no menu dropped
 * looks no different, so a snapshot after a key is written on the
 * second idle tick, the first having let such a loop do its idle work.
 * On each idle tick the driver carries out the script up to its next
 * key or mouse message: that key is posted to the focus window, and its
 * release on the next tick, so the program has handled each key, and
 * all it caused, before the next one comes; an M line's message is
 * posted alone, a button it presses held until a later M line lets it
 * go, and a snapshot after it waits as one after a key does.  The keys
 * of the P lines right after are typed ahead, posted with that key or
 * message, so that the program finds them waiting while it handles it,
 * as keys typed during a long request are: an Esc that stops it, or
 * the ALT+ESC that switches to another program.  The keys of the A
 * lines right after are typed ahead once the program asks for them
 * (td_ahead), as it starts a request by itself, and an E line after
 * them ends the run there.  Every other line but
 * an O line therefore runs when the program is idle and there is no WAIT
 * line (W sets only how long a wait may last).  So help shown in
 * Windows Help (WHELP.H) takes the keys that follow until it is closed,
 * as it does at a keyboard.  The run ends with the script, and there is
 * no END line either.
 *
 * Keys are typed as a keyboard types them.  A K line's key posts
 * WM_KEYDOWN, then WM_KEYUP, to the focus window, with Windows' key
 * state giving SHIFT, CTRL and ALT down meanwhile (CAPS LOCK always off),
 * and the ALT bit of lParam set with ALT; a key with ALT but not CTRL,
 * and F10, is a system key, WM_SYSKEYDOWN and WM_SYSKEYUP.  So the
 * accelerators of TranslateAccelerator, menu mnemonics and the dialog
 * keys work, and TranslateMessage makes the WM_CHAR or WM_SYSCHAR.  A T
 * line's bytes are posted as WM_CHAR, converted from CP437 by OemToAnsi.
 *
 * Script format: the key scripts of INV's /KEYS: (KEYSCR.H), with the
 * key names it shares.  Text lines ended by LF (a CR before it is
 * dropped), at most TD_MAXLINE bytes, comments included, and without
 * NUL bytes; a Ctrl-Z (1Ah) ends the script, also inside a T line, whose
 * bytes before it are still typed; a word is a run of characters other
 * than space and TAB; command letters and key names are matched without
 * regard to case.
 *
 *   # text         a comment; blank lines are ignored too
 *   T text         type the bytes after "T " (CP437), one per tick
 *   K name ...     the named keys in order, separated by spaces:
 *                  F1..F10 ENTER ESC TAB STAB BS SPACE UP DOWN LEFT
 *                  RIGHT PGUP PGDN HOME END INS DEL CLEFT CRIGHT CHOME
 *                  CEND CPGUP CPGDN, SLASH and BSLASH (the keys of / and
 *                  \ on the US keyboard, the driver's own names, as INV
 *                  takes those characters themselves), a letter A..Z or
 *                  a digit 0..9, each
 *                  after any of the prefixes SHIFT-, CTRL- and ALT-
 *                  (ALT-F, CTRL-SHIFT-F5); INV's BREAK and xHHHH have no
 *                  Windows key
 *   P name ...     keys named as in K lines, typed ahead: each one's
 *                  press and release posted at once to the focus
 *                  window, behind the key or mouse message of the K, T
 *                  or M line before them, or behind the keys of the P
 *                  line before, comments and blank lines aside: the
 *                  window that had the focus then, so a P key reaches a
 *                  dialog that key opens only if the program reads it
 *                  there.
 *                  Windows' key state keeps the modifiers of the key
 *                  before them until its release, so a P key's SHIFT,
 *                  CTRL and ALT are only in its message (the system
 *                  key, the ALT bit).  It may follow no other line; one
 *                  after a line that typed nothing is typed as a K line
 *   A name ...     keys named as in K lines, typed ahead when the
 *                  program asks (the driver's own line): the td_ahead
 *                  that finds them the script's next lines, comments
 *                  and blank lines aside, posts them as a P line's are
 *                  posted, to the focus window, so that a request the
 *                  program starts by itself, as none of the script's
 *                  messages can set it off, finds them waiting, as keys
 *                  typed during it are: the Esc that stops it.  So they
 *                  follow the key that leads to that request, the
 *                  program holding the script from it to the request
 *                  (td_busy); reached while the program is idle, they
 *                  are typed as a K line
 *   E              the script ends here (the driver's own line): the
 *                  lines after it are never carried out.  The td_ahead
 *                  that finds it the script's next line, once it has
 *                  typed the A lines before it, ends the run at once, as
 *                  the script's end does (The end of a run, below), so
 *                  in the middle of what the program asked from: its
 *                  loop is told to end while its request is still to be
 *                  made, which a program must carry through to its end
 *                  as it does Windows' own.  Reached while the program
 *                  is idle, it ends the run as the script's end does
 *   M path event base [dx dy]
 *                  the mouse (the driver's own line): one message, at a
 *                  point of the window path names, control IDs in
 *                  decimal, 0..65535, joined by '.' (208.1), the first
 *                  a child of the program's active window, each next a
 *                  child of the one before.  event is MOVE, DOWN, UP or
 *                  DBL, the left button's WM_MOUSEMOVE, WM_LBUTTONDOWN,
 *                  WM_LBUTTONUP and WM_LBUTTONDBLCLK, or after an R
 *                  (RDOWN, RUP, RDBL) the right button's; DBL is a double
 *                  click's second press, a press when the window's class
 *                  lacks CS_DBLCLKS, as Windows makes it.  event may
 *                  be preceded by any of a K line's prefixes SHIFT-,
 *                  CTRL- and ALT- (SHIFT-DOWN, CTRL-SHIFT-UP), which the
 *                  line sets in Windows' key state as a key's are set,
 *                  putting the others up, until the next K or M line
 *                  sets them again.  base is the point in the window's client
 *                  area, and dx and dy, both or neither, decimal, after
 *                  a '-' when below 0, at most 32767 either way, are
 *                  added to it, the sum held within 32767 either way:
 *                    AT      the client area's top left corner
 *                    MID     its middle
 *                    ROW n   a list box's row n, from 0, at its middle
 *                    HEAD k  a list window's (WLIST.H) heading k, from
 *                            0, at its middle, half the header line down
 *                    DIV k   its divider at the right of column k: the
 *                            slot's right edge, as the COLS line has it
 *                    BTN k   a tool bar's button k (WTBM_BUTTON of
 *                            WTOOL.H), its middle; the button is then
 *                            the window named
 *                  n and k at most 32767.  A window of the program
 *                  holding the mouse (GetCapture) gets the message, the
 *                  point in its client area, wherever it lies, as
 *                  Windows routes it, so a drag across two windows names
 *                  the window under each point; else the window named
 *                  gets it, enabled or not.  wParam holds MK_LBUTTON
 *                  and MK_RBUTTON for the buttons held after it, which
 *                  Windows' key state holds too (GetKeyState), and
 *                  MK_SHIFT and MK_CONTROL for SHIFT- and CTRL-; the
 *                  pointer is not moved and WM_SETCURSOR and the
 *                  non-client messages are not sent.  An M line runs
 *                  as a key does, when the program is idle, so a press
 *                  that keeps td_busy holding until its own release
 *                  fails the run BUSY; while a menu is open its effect
 *                  is not defined
 *   O              open again the program's minimized top-level
 *                  windows (OpenIcon), as a double click on an icon
 *                  does (the driver's own line, as W is): carried out
 *                  as an S line would be, or, when the program is not
 *                  busy, at once on a tick when no window of the
 *                  program holds the focus, as when a key has minimized
 *                  its window and another program's is active then; a
 *                  snapshot after it waits as one after a key does
 *   C              leave the program's active window as another
 *                  program's window lying over it does when the user
 *                  switches to that and back (the driver's own line, as
 *                  W is): the window and every window inside it are to
 *                  be erased and painted again (InvalidateRect), and
 *                  the focus is taken from its window and given back
 *                  while their paint messages still wait; nothing while
 *                  Windows Help's window is the active one.  It is
 *                  carried out as an S line would be; a snapshot after
 *                  it waits as one after a key does, and the first one
 *                  marks the list box rows that the screen does not
 *                  show (BLANK)
 *   S name         write a snapshot titled name (1..TD_NAMELEN of
 *                  upper-case A-Z, 0-9 and _)
 *   W secs         the wait limit from here on, 1..TD_MAXWAIT / 1000
 *                  seconds in place of TD_MAXWAIT ms (the driver's own
 *                  line, not INV's), so that a script made to fail FOCUS
 *                  or BUSY fails soon: an emulator short of host time
 *                  drops clock ticks, and its Windows time runs slow.
 *                  The driver reaches it once the key before it has
 *                  been typed, so it goes before the key whose wait it
 *                  limits
 *   Q              Windows ends its session (the driver's own line, as
 *                  W is), carried out when an S line would be, and
 *                  failing ENDED as a key does once the program's loop
 *                  has ended: each top-level window of the program is
 *                  sent WM_QUERYENDSESSION, then each WM_ENDSESSION,
 *                  its wParam TRUE when every one answered nonzero,
 *                  else FALSE.  When FALSE the script goes on.  When TRUE
 *                  the run ends at once, from the driver's tick, as
 *                  Windows ends a program after WM_ENDSESSION without
 *                  returning to its loop: the driver carries out what
 *                  is left of the script as td_done does (its
 *                  snapshots, of the windows as the program left them,
 *                  but no keys), writes the last line and ends Windows,
 *                  /SNAP: given or not, and the program's code after
 *                  WM_ENDSESSION never runs (unless another program
 *                  refuses to end, ExitWindows then returning to it)
 *
 * td_init reads the whole script once to check it, so a line it does not
 * understand fails the run before anything is typed.
 *
 * Snapshot format: every line ends CR LF.  Each snapshot is committed to
 * the disk once written, so a run that hangs leaves those before it.
 *
 *   =SNAP name
 *   <window>       each top-level window of the program, and of Windows
 *                  Help's task (its class MS_WINHELP) while it runs,
 *                  the active one first, then the others, hidden ones
 *                  included, from the top of the Z-order, each followed
 *                  one space deeper by its drawing (HEAD, COLS), its
 *                  menu bar's items, its list rows, each with its
 *                  drawing (DRAW) one deeper again, or its preview
 *                  lines (PV), and its child
 *                  windows, those in Z-order with theirs again one
 *                  deeper; once a menu has been opened, the window
 *                  Windows shows menus in (class #32768) is one of them
 *   ...
 *   =END           after the last line of the script, or
 *   =FAIL n why    the run failed; n is the last line carried out (for
 *                  SCRIPT the line refused, 0 for none), and as a key's
 *                  release is never held, a failure never waits on it.
 *                  why is SCRIPT for a line that td_init refused or the
 *                  script could not be read, FOCUS when no window of the
 *                  program held the focus, or its active window stayed
 *                  hidden, for the wait limit, BUSY when
 *                  td_busy held a request outstanding that long, ENDED
 *                  when the program ended before the keys or the mouse
 *                  messages of the script, TIMER when Windows had no
 *                  timer to give, TARGET when an M line's path names no
 *                  window or its base none of the window's (ROW of a
 *                  window not a list box or past its rows, HEAD or DIV
 *                  of one not a list window drawing headings or past
 *                  its columns, BTN of one not a tool bar or past its
 *                  buttons),
 *                  PLACE when td_place checks the dialogs' place and
 *                  one was not where td_placed puts it: the script runs
 *                  to its end all the same, and n is the last line
 *                  carried out when the first such dialog was seen
 *   =STACK UNUSED=n  last, the bytes of the program's stack that the run
 *                  never used: td_init fills the stack below its frame
 *                  (cstk_fill of COMPAT.H) and td_done counts them
 *                  (cstk_unused), so a run shows how close it came to
 *                  overflowing into the static data below the stack.
 *                  The comparisons with a golden leave it out and hold
 *                  it to a margin (win_snap of TEST/WIN/winlib.sh)
 *
 * The lines below =SNAP, each after its depth in spaces:
 *
 *   Class "text" st         a top-level window, by its class name
 *   Class id "text" st      a window of style WS_CHILD, with its control
 *                           ID as an unsigned number (-1 as 65535); the
 *                           drop-down list of a combo box is one, and
 *                           comes among the top-level windows
 *   POPUP "text" st         a menu item opening a popup menu, whose
 *                           items follow one deeper
 *   MENU id "text" st       a menu item; a separator is MENU 0 ""
 *                           DISABLED
 *   WLIST id "text" HBAR pos max st   a list window (WLIST.H) whose
 *                           horizontal scroll bar shows (WS_HSCROLL,
 *                           which a range of 0 takes away) with a range
 *                           above 0,
 *                           its thumb at pos and its range 0 to max,
 *                           pixels in decimal; without one, as any
 *                           child window
 *   WTAB id "text" CUR n st a tab strip (WTAB.H): its text, its tabs,
 *                           then n, its current tab (WTM_GETCUR), in
 *                           decimal
 *   WTOOL id "text" st      a tool bar (WTOOL.H): its pieces, each
 *                           after a '|' but the first, a gap as '-',
 *                           the slot as '*' and a button as its own
 *                           window text, after a '~' when the button is
 *                           disabled and a '^' when it is drawn
 *                           pressed (BM_GETSTATE); its buttons have no
 *                           lines of their own, its other children do
 *   WSTAT id "text" st      a status bar (WSTAT.H): its panels' texts,
 *                           each after a '|' but the first
 *   WSPLIT id "text" AT x LIMITS lo hi st  a splitter (WSPLIT.H): x its
 *                           left edge in its parent's client area, lo and
 *                           hi the lowest and highest a drag may give it
 *                           (WSPM_GETLIMITS), pixels in decimal
 *   WPREV id "text" VBAR pos max st   a preview panel (WPREV.H) whose
 *                           lines do not fit its height, pos the row at
 *                           its top and max the last row that can be,
 *                           in decimal; when they fit, or it shows its
 *                           text in their place, as any child window
 *   PV "label" "value"      a field of a preview panel, in CP437 as the
 *                           panel keeps it (WPVM_LINE)
 *   PV "text"               a line of its notes
 *   ROW "text" st           a row of a list box or combo box
 *   ROW st                  a row whose text is left out (LONG, or a box
 *                           that keeps no strings)
 *   HEAD fg/bg piece... st  how a window of class Static or WLIST (a
 *                           list window's header, WLIST.H, or a header
 *                           of WDLG.H) draws its text in columns
 *                           (WCOLS.H), when it does: it is painted once
 *                           more (WM_PAINT, its pending paint done
 *                           first) while WCOLS watches (wcol_watch);
 *                           st is CUT or none
 *   COLS kS@x+w...          right after a HEAD line drawn as headings
 *                           (wcol_heads of WCOLS.H), a list window's or a
 *                           dialog header's (WDLG.H): for each column
 *                           of its table, a space, then k the column (0
 *                           the first), S its heading, F flat,
 *                           B a button, A the button of the order in use
 *                           (the arrow) or P a button held down, x its
 *                           slot's left edge measured as a HEAD piece's
 *                           (negative when scrolled off) and w the slot's
 *                           width, pixels in decimal
 *   DRAW fg/bg piece...     how the owner of an owner-drawn list box
 *                           draws its row in columns, when it does: the
 *                           owner is sent WM_DRAWITEM (ODA_DRAWENTIRE,
 *                           ODS_SELECTED for a selected row, never
 *                           ODS_FOCUS, rcItem the box's client area)
 *                           while WCOLS watches; written for the box's
 *                           first row and each selected row only, as
 *                           every row is drawn with the same columns,
 *                           though CUT is checked on every row
 *
 * In HEAD and DRAW, fg and bg are the text and background colours drawn
 * in, six hex digits each, RRGGBB.  A piece is written for each column
 * whose piece (wcol_piece of WCOLS.H) is not empty, in the columns'
 * order: a space, then kA@x+w"text", k the column (0 the first), A its
 * alignment, R (WCOL_RIGHT), I (WCOL_INDENT) or L, x the piece's left
 * edge in pixels from the left of the box drawn in, less the columns'
 * scroll, so negative when scrolled off (wcol_draw), and w its width
 * (GetTextExtent), both decimal, and its text converted as a
 * window's text is (below) but cut by its column.
 *
 * st is none or more of these words, in this order: DISABLED, HIDDEN (the
 * window lacks WS_VISIBLE), ACTIVE (the active window), FOCUS (the focus
 * window), CAPTURE (a child window holding the mouse, GetCapture, or a tool
 * bar one of whose buttons holds it; a top-level one, as Windows' menus hold
 * it, is left out), CHECKED and MIXED (a check box or radio button that
 * BM_GETCHECK reports checked or indeterminate, a menu item checked), DEFAULT
 * (a default push button), SELECTED (a selected row), DROP (a row of a list
 * window's box marked as a drag's drop target, WL_DDROP of WLIST.H), LONG (a
 * row longer than TD_MAXTEXT, whose text is left out), OVERLAP (a child window
 * reaching, beyond its border line, outside its parent's client area or into a
 * visible sibling, group boxes left out), CUT (a row of a list box that is
 * not owner-drawn wider, in the box's font, than the box; a row of an
 * owner-drawn box, or a HEAD line, drawn with a piece reaching past its slot's
 * right edge (wcol_seen of WCOLS.H: its room's, the rectangle's for the last
 * column, never past the rectangle's) or drawn unclipped; a status bar with a
 * panel's text cut to its room or a panel but the last too narrow for its
 * text, WSBM_CUT of WSTAT.H) and BLANK (in the first snapshot after a C line,
 * a row of a list box lying whole in its client area, under no other window,
 * of which the screen shows one colour all along its middle line, where a row
 * drawn has its text).  A text is the window's, the item's or the
 * row's, converted to CP437 by AnsiToOem, but that of a control whose font is
 * of the OEM character set (OEM_FIXED_FONT), CP437 already, and cut at
 * TD_MAXTEXT characters, but a status bar's, written whole; each byte below
 * 20h is written '?'
 * (the CR LF of a multi-line edit control as "??"), and each character of a
 * password edit control as '*'.  The rows of an owner-drawn box that keeps
 * no strings have no text, so such a box takes LBS_HASSTRINGS
 * (CBS_HASSTRINGS) to have its rows written.  Nothing in a snapshot depends
 * on handles, addresses or the mode of Windows, and pixels, fonts and colours
 * only through OVERLAP, CUT, BLANK, HEAD, COLS, DRAW, HBAR, VBAR, AT and
 * LIMITS:
 * those of the test machine's display, the same in each mode.
 *
 * The end of a run.  When the script ends or fails while the program
 * runs, the driver closes an open menu and ends the program's loop with
 * PostQuitMessage, so that the program leaves as it does when the user
 * ends it; td_done, which the program calls after its loop, writes the
 * last line, =END or =FAIL, and, when /SNAP: was given, calls
 * ExitWindows, so that the harness sees Windows end; a run without
 * /SNAP: leaves Windows running.  Windows 3.0's ExitWindows asks only the
 * other programs: the caller gets neither WM_QUERYENDSESSION nor
 * WM_ENDSESSION and is ended with Windows, and td_done does not return
 * (unless another program refuses to end).  So the program closes its
 * link and files before it calls td_done.
 */
#ifndef TDRV_H
#define TDRV_H

#include "COMPAT.H"
#include "CWIN.H"

#define TD_MAXLINE      128     /* bytes of a script line, CR LF
                                   excluded                             */
#define TD_NAMELEN      8       /* characters of a snapshot name        */
/* Characters of a text written: at least a list's row (LV_ROWTEXT of
   PAGER.H, as TDRV.C checks), so that every row is written whole. */
#define TD_MAXTEXT      160
/* The ms an idle tick may be waited for, unless a W line says less:
   longer than a request's longest wait for its answer, LINK_NETWAIT
   seconds (LINK.H), so that a request left unanswered is not taken for a
   program that hangs. */
#define TD_MAXWAIT      90000ul

/* Run the key script keys, writing the snapshots to snap (which may be
   NULL, for none); keys NULL leaves the driver off.  It is called once,
   from WinMain before the message loop, inst the program's instance,
   and fills the stack below its frame for the =STACK line.
   A failure to open either file, a bad script line or no free timer
   ends the run at once, as td_done does: the =FAIL line is written
   and, when snap was given, ExitWindows called, so that the run of a
   program that drops a quit posted before its loop ends too; without
   snap, or when ExitWindows returns, the program's loop ends as soon
   as it starts. */
void td_init(HINSTANCE inst, const char *keys, const char *snap);

/* Check from here on where each dialog of the program opens, as
   WDLG.H places it, for a program whose dialogs that module places:
   each top-level window of class #32770 of the program's instance (a
   message box is Windows', and Windows centres it on the screen) and
   of fixed size (a sizable one is placed by its owner and its user) is
   checked by td_placed the first time the driver sees it, at a tick or
   at td_busy, so also when it lasts only while the program is busy,
   and a refusal makes the run end with PLACE in place of =END.  It is
   not checked again while it lasts, as its owner may move after it has
   opened; a dialog opened in the message that closes another, between
   two ticks, may be given the closed one's handle, and is then taken
   for it and not checked. */
void td_place(void);

/* 1 when the dialog dlg stands where WDLG.H places it: centred over
   its owner's window, or over the screen when it has none or its owner
   is minimized, its margins there differing by a pixel at most, and on
   the screen, else against the edge of the screen it would have
   passed, or at 0 on an axis where it is longer than the screen. */
int td_placed(HWND dlg);

/* Hold the script while busy is nonzero, when the program has a request
   outstanding whose response a later line needs: the script goes on
   after td_busy(0). */
void td_busy(int busy);

/* Type ahead the keys of the A lines that are the script's next lines,
   if any (Script format, above), and pass them; then end the run when
   an E line is the next.  The program calls it as it starts a request
   that only its own message loop sets off and that a key typed
   meanwhile may stop. */
void td_ahead(void);

/* Called once the program's message loop has ended and it has closed
   its link and files: when the run had not ended, carry out what is
   left of the script, which may hold snapshots, of the windows left,
   but no keys or mouse messages (ENDED when one remains); then write
   =END or =FAIL and the =STACK line, close the files and, when /SNAP:
   was given or a Q line ended the session, call ExitWindows, which does
   not return unless another program refuses to end. */
void td_done(void);

#endif
