/*
 * FORM.H - data-entry forms and the memo editor of the full-screen
 * client (FORM.C, MEMO.C).
 *
 * A form draws the model of FMODEL.H in a box and edits it: each label
 * at its row and column inside the box, each value at its value column
 * in dispw cells (when 0, the width, or FORM_MEMODISP for FT_MEMO), but
 * FT_DATE in FORM_DATEW cells and FT_LOGIC in one.  Labels and the
 * title are message numbers (MSG.H); a form fetches each when it draws
 * it and keeps no msg() result, and every text passed to a function
 * here (form_set_title, memo_edit's title) is copied before the
 * function fetches a message or reads a key.
 *
 * Field types, as edited:
 *   FT_CHAR, FT_NUM, FT_CODE
 *              bytes 20h..FFh, trailing blanks dropped; F4 on an
 *              FT_CODE field calls the form's pick function
 *   FT_DATE    shown and typed as YYYY-MM-DD (the '-' are fixed in the
 *              template); digits only
 *   FT_LOGIC   shown as Y or N; Space toggles
 *   FT_CHOICE  Space steps through the words of choices, and through
 *              blank in a field without FF_REQ, which Ctrl-Y also
 *              clears; F4 picks from a list the form builds from choices
 *              itself
 *   FT_MEMO    not edited in the form: its value is set with
 *              memo_load_count or form_load, and by the memo function's
 *              result, and shown as a line count.  F4 calls the form's
 *              memo function, in view mode too; in a FORM_NEW form,
 *              whose moves pass the memo, it says the memo can be
 *              written once the record has been saved
 *   FT_SECRET  nothing shown, and only bytes 21h..7Eh (tui_input with
 *              TI_NOECHO | TI_PRINT), typed into the field's own slot;
 *              one wider than LINK_PWMAX, a password being set, takes
 *              every byte, so that its check refuses what was typed
 *              rather than a part of it
 *
 * Keys: Tab and Down go to the next field, Shift-Tab and Up to the
 * previous one (wrapping); PgUp and PgDn to the first and last; a click
 * on a field's label or value goes to that field.  The moves and clicks
 * pass every field that cannot be changed but the memo of a saved
 * record, where a character typed says that F4 opens it (MF_MEMOKEY),
 * and in view mode only the fields that show a value (FF_LOCAL | FF_RO).
 * Inside a field the line editor keys of tui_input work, and the value
 * of a field the cursor has just come to starts selected (TI_SELECT; a
 * date's too): the first character typed replaces it, and an arrow,
 * Home, End or a click on the field edits it in place.  Enter or Ctrl-S
 * saves; Esc cancels, asking first when something has changed.  In view
 * mode nothing can be changed, Tab still moves, Enter does nothing, and
 * Esc leaves without asking.  A form opened in another mode, or turned
 * to editing by form_loop, moves a cursor on a field that cannot be
 * changed (a record ID) on to the next field that can.
 *
 * A save first checks every field; the first bad one gets the
 * cursor and its message on the hint row.  The server checks again: a
 * "-ERR 102 ... FIELD=NAME" (or 201 or 202 naming a field) puts the
 * cursor on the field of that name with the server's text, through
 * form_error.
 */
#ifndef FORM_H
#define FORM_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "FMODEL.H"

#define FORM_DATEW      MSG_DATEW /* YYYY-MM-DD                         */
#define FORM_MEMODISP   30      /* cells an FT_MEMO field shows         */

/* F4 on an FT_CODE field (only those: FT_CHOICE lists are the form's
   own): choose a value into val (at most the field's width) and return
   1, or return 0 to keep the value.  Also called with val NULL once
   typing has changed the field's value and the cursor leaves it, so the
   screen may show what the value names (a location's path); the return
   is then not read.  Either call may stop the form (form_stop). */
typedef int (*form_pick_fn)(void *arg, form *f, u16 field, char *val);

/* F4 on an FT_MEMO field of a saved record: edit or show the memo, and
   return its line count after (for the field's display). */
typedef u16 (*form_memo_fn)(void *arg, form *f, u16 field);

/* The body of a form_memo_fn for field (an FT_MEMO field of f) of the
   record id of table, through l: memo_edit with readonly and title,
   then form_stop(f, memo_leave()) when that is not TUI_CMD_NONE.
   Returns the line count for the field: the one the server holds once
   the memo has loaded (also after a save that failed part-way), or the
   field's old count when the memo did not load.  The caller sets its
   own hints after. */
u16 memo_field(form *f, u16 field, link *l, const char *table,
               const char *id, int readonly, const char *title);

/* A form for def in mode (FORM_) with every value empty; pick and memo
   may be NULL.  NULL, keeping nothing, when memory has run out or def has
   more than FORM_MAXFIELDS fields or one wider than FORM_MAXVAL.  At most
   one form with FT_SECRET fields is open at a time: form_wipe closes the
   last one opened. */
form *form_open(const form_def *def, u8 mode, form_pick_fn pick,
                form_memo_fn memo, void *arg);

/* Replace the title (def->title) by a text of at most FORM_TITLEMAX
   characters, copied, e.g. "Item 12" once a new item has its ID. */
#define FORM_TITLEMAX   40
void form_set_title(form *f, const char *title);

/* Draw the box, the labels and the values. */
void form_paint(form *f);

/* Paint and edit until Enter or Ctrl-S with every field valid (returns
   KEY_ENTER for both), Esc (KEY_ESC: in view mode, or nothing changed,
   or the user agreed to lose the changes) or a key the form does not
   use, which is returned: Ctrl-E in view mode; the key of a Record menu
   verb that has none of its own (TUI_VERBKEY to TUI_VERBLAST) in every
   mode, whether or not anything has changed (a screen that leaves the
   form for one checks form_dirty itself and refuses with
   MT_SAVEFIRST); the global keys of tui_isglobal (F3, F10, Ctrl-F,
   Alt-letter, a click on the menu bar) only in view mode or while
   nothing has changed, and otherwise swallowed with the message
   MT_SAVEFIRST.  Returns with the screen as drawn. */
u16 form_run(form *f);

/* End form_run at once: it returns KEY_ESC without asking, for a memo
   or pick function after which the screen must leave with cmd
   (memo_leave), which form_loop then returns. */
void form_stop(form *f, u8 cmd);

/* A form whose values are not a record (a search): Esc and the global
   keys leave at once even with changes, without asking. */
void form_noask(form *f);

/* Report a failed save through tui_fail, after putting the cursor on
   the field a 102, 200, 201 or 202 names, or for a 201 naming none of
   the form's fields on its FF_KEY field; a 200 naming a field, a record
   the field refers to, shows the server's text as a 102 does.  When
   form_save set the model's partial, a message box first says that some
   fields may not have been saved, and partial is cleared.  Returns what
   tui_fail returns. */
u8 form_error(form *f, const rq_res *res);

/* Report a failed save with form_error, which gives *cmd.  Returns 0
   when the form stays open for more editing (*cmd is then TUI_CMD_NONE),
   or -1 when the screen must leave the form and return *cmd: the
   command form_error gave, or TUI_CMD_NONE after an RQ_ENOTFOUND naming
   none of the form's fields (the record is gone). */
int form_fail(form *f, const rq_res *res, u8 *cmd);

/* Report a field that a check of the screen's own refused, as a server's
   field error is reported: the cursor goes to field name (it stays
   where it is when the form has no such field) and text is shown on
   the hint row. */
void form_field_error(form *f, const char *name, const char *text);

/* Save through l with form_save (table, id, idsz and res as there) and
   report a failure.  When the ADD succeeded and a later SET failed,
   the form is first given the new record: its ID goes into field
   idfield (NULL when the record's key is the form's FF_KEY field,
   which already holds it) and its title becomes message title with
   the ID as ^1; form_fail then reports the failed response in res.
   Returns 1 with *cmd TUI_CMD_NONE when saved, else as form_fail. */
int form_store(form *f, link *l, const char *table, char *id, u16 idsz,
               const char *idfield, u16 title, rq_res *res, u8 *cmd);

/* A record screen's part in form_loop; arg is form_loop's.  save saves
   the form on Enter and notes it (lv_note): 0 to go on editing, else the
   form is left with *cmd (TUI_CMD_NONE after a save).  key, or NULL,
   sees each key form_run returns first: 1 when it took the key, 0 to
   leave with *cmd, -1 when the key is not its own.  hint is the
   edit-mode hint message. */
typedef struct {
    int (*save)(void *arg, form *f, u8 *cmd);
    int (*key)(void *arg, form *f, u16 k, u8 *cmd);
    u16  hint;
} form_ops;

/* Run f until it is left; the command to return.  Enter saves through
   ops->save; Esc leaves with TUI_CMD_NONE, or with the command of
   form_stop; Ctrl-E (view mode only) turns the form to FORM_EDIT with the
   hint ops->hint once tui_allow grants TUI_LVL_OPER; any other key not
   taken by ops->key goes to tui_global.  The Record menu holds verbs
   (a set for tui_record of TUI.H) meanwhile and the caller's again
   after.  The caller sets the first hints; they change with the mode
   only, so a message about a failed save stays on the hint row. */
u8 form_loop(form *f, const form_ops *ops, void *arg, u32 verbs);

/* ---------------------------------------------------------------------
 * The memo editor (MEMO.C).
 *
 * Edits the memo of one saved record in a window of MEMO_COLS x
 * MEMO_ROWS cells inside a double frame.  The memo is loaded and saved
 * as MEMOIO.H says, and the editor says, when it opens, what the load
 * made of it (mio_check): one changed on loading is only rewritten if
 * the user saves it, and one that a save would cut short (more than
 * MEMO_MAXLINES lines, truncated by the server, or past LINK_MAXMEMO
 * bytes once loaded) opens read-only, so a save can never leave the
 * server's memo cut short.  Typed text is bytes 20h..7Eh and 80h..FFh.
 *
 * Keys: characters, arrows, Home/End (line), Ctrl-Home/Ctrl-End (memo),
 * PgUp/PgDn (MEMO_ROWS lines), Enter (split the line), Backspace and
 * Del (joining lines at their ends), Ins (insert or overwrite), Ctrl-Y
 * (delete the line), Ctrl-S save, Esc leave (asking when changed).  A key
 * that would pass MEMO_COLS characters in a line, MEMO_MAXLINES lines
 * or LINK_MAXMEMO bytes as stored is refused with a message.  When a
 * save fails part-way, the editor stays open and says how many lines
 * reached the server and that the stored memo is unchanged.
 * --------------------------------------------------------------------- */
#define MEMO_ROWS       10

/* Edit (readonly 0) or show (readonly 1) the memo of record id of
   table ("ITEM", "LOC" or "MAINT"), titled title.  *lines receives the
   line count of the memo as the server now holds it, and is valid
   whenever the load succeeded, a failed save leaving the count loaded;
   after a failed load it is 0.  Returns RQ_OK, or the code of a failed
   load or save (already reported); the screen under the editor is put
   back. */
int memo_edit(link *l, const char *table, const char *id, int readonly,
              const char *title, u16 *lines);

/* Load the line count of the memo of record id of table, as text, into
   the FT_MEMO field named field of form f being opened; a failed count
   is reported (tui_fail, with the server's response) and loads 0.
   Returns 1, or 0 with *cmd set when the screen must leave (tui_fail's
   command). */
int memo_load_count(form *f, const char *field, link *l, const char *table,
                    const char *id, u8 *cmd);

/* The command tui_fail gave when the last memo_edit ended on a failure
   the screen must leave for (TUI_CMD_LOGIN, TUI_CMD_QUIT), else
   TUI_CMD_NONE. */
u8 memo_leave(void);

#endif
