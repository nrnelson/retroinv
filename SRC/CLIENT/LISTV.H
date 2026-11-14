/*
 * LISTV.H - the paged list view of the full-screen client.
 *
 * A list view draws a pager (PAGER.H), which fetches and keeps the rows
 * of one paged command, in a window of rows, one record per row, with
 * the column header of the order in use above them, and moves the
 * selection with the keys.  It gives the pager its two functions:
 *
 *   - waiting: the progress on the status line (MT_WORKING with the
 *     rows of the page so far, ML_WALK with the rows the End walk has
 *     passed), a flush of the screen, and the keys typed meanwhile
 *     (tui_esc_waiting): an Esc among them stops the move, which then
 *     says so on the hint row (ML_STOPPED), PgDn going on from there;
 *     other keys typed are dropped.  A key script has keys waiting only
 *     on its P lines, so other scripted runs always fill whole pages.
 *     When the move ends the status line shows again what the progress
 *     replaced.
 *   - failures: tui_fail reports the failed request, and its command
 *     (lv_leave) tells the screen whether to leave.
 */
#ifndef LISTV_H
#define LISTV_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "PAGER.H"

/* Texts: column headers and the empty-list text are message numbers,
   fetched when drawn; the view keeps no msg() result. */

typedef struct lv lv;           /* a list view (opaque)                 */

/* A view over src on link l, drawn in the rectangle top, left, h rows
   (the header and h - 1 rows of records, h - 1 <= LV_MAXPAGE) by w
   columns (w <= LV_TEXTLEN).  The frame around it is the caller's.
   Nothing is fetched yet.  NULL when memory has run out. */
lv *lv_open(link *l, const lv_src *src, u8 top, u8 left, u8 h, u8 w);

/* Free the view; v may be NULL. */
void lv_close(lv *v);

/* Moves: the pager's move of the same name (PG_HOME, PG_END, ...,
   PG_RELOAD of pg_move, and pg_seek), then a repaint.  Each returns the
   pager's code, a failure being reported through tui_fail.  lv_home is
   also the first load, and after lv_set_order the first in that order,
   its field list and header.  When lv_reload returns RQ_OK the pending
   note (lv_note) is shown; the note is dropped either way. */
int lv_home(lv *v);
int lv_end(lv *v);
int lv_up(lv *v);
int lv_down(lv *v);
int lv_pgup(lv *v);
int lv_pgdn(lv *v);
int lv_seek(lv *v, lv_match_fn match, void *arg, u16 max);
int lv_reload(lv *v);

/* The note a closed form leaves for its list: message m with arguments
   a and b (either may be NULL; a is cut to LINK_IDLEN characters, b,
   which may be a location's name, to LINK_LOCNAMELEN),
   shown by the next lv_reload or lv_note_show.  One note is pending at
   a time; m 0 drops it, which a screen does on entry so that it never
   shows a note left for another list. */
void lv_note(u16 m, const char *a, const char *b);

/* Show the pending note on the hint row (tui_msg), if any, and drop
   it. */
void lv_note_show(void);

/* The end of a form opened where no list follows (a screen that drops
   the note on entry): the pending note is shown when cmd is
   TUI_CMD_NONE (the screen goes back to where it was opened), else
   dropped; returns cmd. */
u8 lv_note_end(u8 cmd);

/* pg_lead of the view's pager: a first row of key and text, which the
   caller keeps while the view is open; set before lv_home. */
void lv_lead(lv *v, const char *key, const char *text);

/* pg_order_name and pg_set_order of the view's pager. */
const char *lv_order_name(lv *v);
int lv_set_order(lv *v, const char *name);

/* Draw the header and the rows; the selected row in TA_SELECT while
   lv_run reads the keys, else TA_BOLD, a marked one in TA_ALERT, and
   the empty text when the list has no rows. */
void lv_paint(lv *v);

/* Paint, then read keys with tui_key and handle Up, Down, PgUp, PgDn,
   Home, End, F5 (lv_reload) and a click on a row, which selects it,
   until any other key, which is returned for the caller to act on:
   Enter (also for the second press of a double click whose first press
   selected the row), Ins, Del, Ctrl-E, a Record menu verb's key, Esc,
   F3, F10, Ctrl-F, Alt-letter, other clicks and so on.  Before it
   returns it draws the selected row TA_BOLD, as it is whenever the view
   does not read the keys: a box the caller opens over the list then
   leaves no ends of the selection bar beside it, while the row's text
   that shows still marks it. */
u16 lv_run(lv *v);

/* After a move that failed in a way that makes the screen leave, the
   command tui_fail gave (TUI_CMD_LOGIN, TUI_CMD_QUIT), for which lv_run
   returns KEY_ESC; after any other move TUI_CMD_NONE. */
u8 lv_leave(lv *v);

/* The order member, pg_selected and pg_rows of the view's pager. */
u8 lv_order_index(lv *v);
int lv_selected(lv *v, char *key);
u16 lv_rows(lv *v);

/* The selected row's display text into text (LV_ROWTEXT + 1 bytes),
   after lv_selected has found a row. */
void lv_seltext(lv *v, char *text);

#endif
