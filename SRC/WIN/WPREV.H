/*
 * WPREV.H - the preview panel of INVWIN's frame: a record's fields and
 * notes, read-only, in a child window that its owner fills (WPREV.C).
 *
 * A view dialog as a child would cost a control for each label and
 * value in USER's heap, so wpv_create makes one window of class
 * "WPREV", with a border and a vertical scroll bar, which paints what
 * it holds: its lines, or, while it holds none, its window text,
 * centred along its top (wpv_text: nothing to show, a load under way, a
 * load that failed).  The lines are fields, a label and a value each
 * (wpv_add), then the lines of the notes (wpv_note), kept in CP437 in
 * one far block of WPV_BYTES bytes, read line by line as a message
 * needs them and freed with the window; a label keeps WPV_LABELW
 * characters and a value or a line of the notes WPV_VALW.
 *
 * It draws in ANSI_VAR_FONT on COLOR_WINDOW, every text through
 * OemToAnsi: the labels right-aligned in a column as wide as the widest,
 * each value beside its label and each line of the notes from the left
 * edge, both broken at words into as many rows as the panel's width
 * needs, '&' drawn as itself; a focus rectangle inside its border while
 * it has the focus.  The rows are counted for a panel as wide as it is
 * with its scroll bar, which shows while they do not fit its height and
 * scrolls by rows.  No GDI object outlives a message.
 *
 * Keys and mouse.  Up, Down, PgUp, PgDn, Home and End scroll as the
 * bar does.  SHIFT and CTRL alone are dropped.  Every other key is the
 * owner's: it is sent WM_COMMAND, wParam the panel's control ID, lParam
 * MAKELPARAM(the virtual key, WPVN_KEY), during which GetKeyState gives
 * SHIFT and CTRL as they were when the key was typed.  A press of the
 * left button gives the panel the focus, and a double click is
 * VK_RETURN's WPVN_KEY as well.  System keys (ALT and its letters, F10)
 * go on to Windows, so the menus work.
 *
 * WPVM_LINE (wParam i, from 0, lParam a far buffer of WPV_LINESZ bytes)
 * gives line i in CP437, for the test driver (TDRV.H): its label and
 * its value, each with its NUL, one after the other, a line of the
 * notes with an empty label; WPV_FIELD or WPV_NOTE, or 0 when the panel
 * holds no line i.
 */
#ifndef WPREV_H
#define WPREV_H

#include "COMPAT.H"
#include "CWIN.H"

#define WPV_BYTES       9216    /* the lines' block                     */
#define WPV_LABELW      31      /* characters of a label kept           */
#define WPV_VALW        80      /* of a value or a line of the notes    */
#define WPV_LINESZ      (WPV_LABELW + 1 + WPV_VALW + 1)

#define WPVM_LINE       (WM_USER + 0)   /* line wParam into lParam      */
#define WPV_FIELD       1       /* WPVM_LINE: a label and a value       */
#define WPV_NOTE        2       /* a line of the notes                  */

/* The notification code of WM_COMMAND for a key the panel leaves to its
   owner, the key in LOWORD(lParam). */
#define WPVN_KEY        1

/* Make a panel of control ID id in owner, visible, empty and of no size
   at owner's top left corner, for the owner to place; NULL when Windows
   made no window or memory has run out. */
HWND wpv_create(HWND owner, int id);

/* Empty panel w and give it the text of string ids of its instance,
   whose %s, if any, is arg (CP437, at most WPV_VALW characters, or NULL
   without one), or no text for ids 0, painted at once, as no message
   may be dispatched while its owner loads it. */
void wpv_text(HWND w, UINT ids, const char *arg);

/* Add a field to panel w, label and value in CP437, each cut to what a
   line keeps: 1, or 0, with nothing added, when the block is full. */
int wpv_add(HWND w, const char *label, const char *value);

/* Add a line of the notes to panel w, after its last field, as wpv_add
   adds a field. */
int wpv_note(HWND w, const char *line);

/* Show the lines added to panel w since wpv_text, which left it at its
   first row, in place of its text. */
void wpv_done(HWND w);

#endif
