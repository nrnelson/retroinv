/*
 * SCRSHR.H - what the screens of the full-screen client share with one
 * another, private to SCR*.C and their tests.
 *
 * The lists, forms and pick lists one screen opens for another, the
 * item form's tabs, the grid of the forms and the server's version
 * text.  INV.C runs the screens through SCR.H alone and does not read
 * this header, as Watcom C 6.5's compiler memory holds every
 * declaration a file reads.
 */
#ifndef SCRSHR_H
#define SCRSHR_H

#include "COMPAT.H"
#include "RQ.H"
#include "TUI.H"
#include "LISTV.H"
#include "FORM.H"
#include "SCRKFORM.H"

/* ---------------------------------------------------------------------
 * Shared state.
 * --------------------------------------------------------------------- */

/* The text of the VER response, fetched once per run by the login screen
   (SCRLOGIN.C) for its title and shown by the settings screen. */
extern char scr_version[RQ_TEXTLEN + 1];

/* ---------------------------------------------------------------------
 * Form layout.
 * --------------------------------------------------------------------- */

/* The three-column grid of the item, loan and service forms: the label
   (SCR_Cn) and value (SCR_Vn) column of each, inside the form's box. */
#define SCR_C1          1
#define SCR_V1          11
#define SCR_C2          25
#define SCR_V2          35
#define SCR_C3          49
#define SCR_V3          59

/* The box of a form that opens centred over a list (a record form, a
   report's or a file's options): its top row, left column and width;
   each screen gives its own height. */
#define SCR_POPTOP      (TUI_TOP + 5)
#define SCR_POPLEFT     5
#define SCR_POPW        71

/* The cells of the item form's Parent field, from SCR_V2 to one before
   the box's right edge: the parent's ID, maker and model. */
#define SCR_PARENTW     (VID_COLS - 2 - SCR_V2)

/* ---------------------------------------------------------------------
 * Items.
 * --------------------------------------------------------------------- */

/* SCRITEM.C: the item form for item id, opened from any list in mode
   FORM_VIEW or FORM_EDIT.  It shows the note of a save ("Item 12
   saved") on the hint row itself, as it has no list to leave it for. */
u8 scr_item_open(const char *id, u8 mode);

/* The cells of an item list's title. */
#define SCR_IL_TITLEW   (VID_COLS - 4)

/* SCRITEM.C: an item list over the paged item command cmd ("ITEM LIST" with
   filters, "LOC CONTENTS", "ITEM FIND", "ITEM SERIAL", "ASSY LIST"), whose own
   positional words and pairs args(arg, q) adds (args may be NULL), with the
   item list's columns, keys and Record menu (scr_assy_move, scr_assy_into
   and scr_assy_out for its assembly verbs), titled title (a text
   of at most SCR_IL_TITLEW characters, copied before anything else, so msg()
   and msg_sub() results may be passed), showing message empty when there are
   no rows; locpreset (or NULL) is the LOC_ID a new item gets, and F1 shows
   help topic topic (a static string, as for tui_topic).  When tab is not 0 (a
   location's contents), the Record menu's Not installed only switches between
   all the items, shown first, and those not installed in another (TOP=1), and
   Everything below between the location's own items and those of every
   location below it too (ALL=1), the latter shown first when below is 1; tab
   is then MO_SHOW, the first of the contents' texts in SCRLFORM.H's order.
   The search, location and assembly screens use it for their results. */
u8 scr_item_list(const char *topic, const char *cmd, lv_args_fn args,
                 void *arg, const char *title, u16 empty,
                 const char *locpreset, u16 tab, u8 below);

/* SCRPICK.C: the request of the Del key of a list, words id ("ITEM
   DEL", "LOC DEL"), with lv_note(note, id) once deleted (none when note
   is 0).  1 when the list must be fetched again (*cmd is then
   TUI_CMD_NONE): the record was deleted, or was already gone
   (reported); 0 when the delete was refused (reported), which changed
   nothing, or with *cmd set when the screen must leave. */
int scr_del(const char *words, const char *id, u16 note, u8 *cmd);

/* SCRITEM.C: the new item form, with LOC_ID preset to loc when loc is
   not NULL.  Below level 2 it is refused (tui_allow) and nothing is
   opened.  Returns the command to run next. */
u8 scr_item_new(const char *loc);

/* After scr_pick_code, scr_pick_loc or scr_pick_item returned 0: the
   command a failed request asked the screen to leave with
   (TUI_CMD_LOGIN, TUI_CMD_QUIT), else TUI_CMD_NONE.  Set by the pick
   functions (defined in SCRPICK.C); the form's pick function then
   passes it to form_stop and its screen returns it. */
extern u8 scr_pickleave;

/* SCRPICK.C: the item that typed names: its ID when that is an item's
   (ITEM GET), else one chosen from the item pick list (scr_pick_item)
   of the items holding the text, digits that name no item included, or
   of every item when typed is blank, or when list is 1 and typed is an
   ID; the list's items those only says (SCR_SI_ of SCRSFORM.H).  1
   with the item's ID in id (LINK_IDLEN + 1 bytes); 0 when the pick list
   was left with Esc or a request failed, *cmd the command a failure
   asks the screen to leave with (else TUI_CMD_NONE). */
int scr_find_item(const char *typed, u8 only, u8 list, char *id, u8 *cmd);

/* SCRPICK.C: ask for an item in a prompt box titled title (a text,
   copied first; msg results may be passed), until scr_find_item finds
   the item typed: 1 with its ID in id (LINK_IDLEN + 1 bytes); 0 when
   the prompt was left with Esc, when the item pick list could not open
   (reported), or when a failed request asks the screen to leave, with
   that command in *cmd (else TUI_CMD_NONE). */
int scr_ask_item(const char *title, u8 only, char *id, u8 *cmd);

/* ---------------------------------------------------------------------
 * Code lists.
 * --------------------------------------------------------------------- */

/* The command word of each code list ("CAT", "LTYPE"; SCRPICK.C), by
   kind (SCR_CK_ of SCRKFORM.H). */
extern const char *const scr_code_tables[];

/* SCRPICK.C: the pick list of code list kind (SCR_CK_), from one LIST of
   it held for tui_pick (the location types of scr_lt_types; the tags
   followed to their end, titled ME_TAGSPART when incomplete, and
   ME_NOTAGS on the hint row instead when there are none), opened on the
   entry of the code in code (on the first when none has it).  1 with
   the chosen code in code, which holds LINK_CATLEN + 1 bytes for a
   category, LINK_LTYPELEN + 1 for a location type and LINK_ITAGLEN + 1
   for a tag; 0 when cancelled or when the request failed (scr_pickleave
   says which). */
int scr_pick_code(u8 kind, char *code);

/* SCRPICK.C: F4 on a code field of kind (SCR_CK_) of form f, for a
   form_pick_fn: the pick list into val, then the form's hints again
   (message hint, through tui_hint_keep); with val NULL (a value typed),
   nothing.  When a failed request ends the screen, the form is stopped
   with its command (form_stop), which is also set in *leave unless
   leave is NULL (for a form run by form_loop, which has it from
   form_stop).  1 when a value was chosen, else 0. */
int scr_pick_field(form *f, u8 kind, char *val, u16 hint, u8 *leave);

/* SCRPICK.C: F4 on a tags field of form f, width characters wide, as
   scr_pick_field: the tag pick list, the tag chosen appended to val
   (ct_tags_add of CODETAB.H), or ME_TAGFULL on the hint row when val
   has no room for it.  1 when a tag was chosen and fits, else 0. */
int scr_pick_tag(form *f, char *val, u16 width, u16 hint, u8 *leave);

/* ---------------------------------------------------------------------
 * Locations.
 * --------------------------------------------------------------------- */

/* SCRLOC.C: the contents of location id, as an item list titled with
   its path, showing the items of every location below it too when it
   holds other locations. */
u8 scr_loc_contents(const char *id);

/* The code lists' loading, the location tree's view, the location pick
   list and the location fields of forms.  Only a file that defines
   SCRSHR_LOCF before it includes this header sees them, with CODETAB.H's
   and SCRLFORM.H's types: the largest tests (T_SCRR.C) would have no
   Watcom C 6.5 compiler memory left if they saw more. */
#ifdef SCRSHR_LOCF
#include "CODETAB.H"
#include "SCRLFORM.H"
#include "SCRSFORM.H"

/* SCRPICK.C: the item pick list, titled MI_PICK (MI_PICKAVL for the available
   items alone), of the items search q lists (SCRSFORM.H; its rows as ITEMLS.H
   makes them), in a box across the work area that puts back what it covered
   and the hints before it.  Enter chooses the selected item: 1 with its ID in
   id (LINK_IDLEN + 1 bytes); 0 after Esc, or when the box could not open
   (reported), or a failed request asks the screen to leave (scr_pickleave says
   which). */
int scr_pick_item(scr_query *q, char *id);

/* SCRPICK.C: the list of code list kind (SCR_CK_) held whole in t, for
   a pick list: t opened and loaded, 1; or 0 with the failure reported
   and *cmd the command to leave with, nothing held.  The tags' load
   shows ME_TAGWAIT on the status line between its pages, and Esc there
   ends it (CODETAB.H's part), the status line put back after.
   scr_code_label is the tui_label_fn of its entries (arg t): the code,
   the name and the sort order. */
int scr_code_load(ctab *t, u8 kind, u8 *cmd);
void scr_code_label(void *arg, u16 i, char *buf);

#ifdef SCRSHR_TREE
/* A list view over the location tree (SCRLFORM.H's rows, the type names
   from scr_lt_types), the tree screen's and the location pick list's,
   seen only by a file that also defines SCRSHR_TREE (SCRLOC.C,
   SCRPICK.C), for the compiler memory of the other screens.
   scr_lt_open opens t's view in the rectangle given (lv_open): 1, or 0 with
   the failure reported (*cmd the command to leave with) and nothing held.
   scr_lt_letter takes key k typed: a letter moves the selection down to the
   next location whose name starts with it, through at most LV_SEEKMAX rows,
   wrapping once, and returns 1.  scr_lt_selrow puts the selected row, whose
   key lv_selected gave, into *x, cleared first: the key as the ID, the name
   (without the indent) and code as the row shows them, and the depth its
   indent tells. */
typedef struct {
    lv  *v;
    lv_src src;
    ctab *types;
} scr_lt;

int scr_lt_open(scr_lt *t, u8 top, u8 left, u8 h, u8 w, u8 *cmd);
void scr_lt_close(scr_lt *t);
int scr_lt_letter(scr_lt *t, u16 k);
void scr_lt_selrow(scr_lt *t, const char *key, scr_locinfo *x);

/* SCRPICK.C: the location types' list (CODETAB.H) that every open view
   of the tree shares: the first scr_lt_types loads it (LTYPE LIST), and
   the scr_lt_types_done matching the last frees it.  NULL, with the
   failure reported and *cmd the command to leave with, when it could
   not be had. */
ctab *scr_lt_types(u8 *cmd);
void scr_lt_types_done(void);
#endif

/* SCRPICK.C: LOC GET loc into *x, cleared first (tui_req). */
int scr_loc_get(const char *loc, scr_locinfo *x, u8 *cmd);

/* SCRPICK.C: 1 when location loc (its LOC_ID or code) is location
   outside (a LOC_ID) or one inside it, by the locations above it (one
   LOC GET each, up to the top); 0 too when a request failed, with *cmd
   as tui_req sets it. */
int scr_loc_within(const char *loc, const char *outside, u8 *cmd);

/* SCRPICK.C: the location pick list, titled by message title, its lead
   row message lead (MO_NOLOC, MO_TOP or MO_ALLLOC of SCRLFORM.H), the
   location cur names (its LOC_ID or code; the lead row when NULL, "" or
   "0") selected first; when the seek stops short of it (LV_SEEKMAX
   rows), Enter on the row it stopped on asks MO_PICKQ first, and No
   keeps the list.  When outside is not NULL, choosing location
   outside or one inside it is refused with MO_INSELF and the list stays
   (one LOC GET for each level above the one chosen).  1 with the chosen
   location's LOC GET in *x (for the lead row its id "0" and the lead's
   text as its name), 0 when cancelled or when a request failed
   (scr_pickleave says which). */
int scr_pick_loc(u16 title, u16 lead, const char *cur, const char *outside,
                 scr_locinfo *x);

/* SCRPICK.C: the path of the location val names into the path field of
   location field d (scr_locf of SCRLFORM.H) in form f, loaded as
   unchanged: none's text, "" (any location), or the path that LOC GET
   gives; a value that names no location is said on the hint row and
   shows "".  With load 1 (val just loaded with the record) a
   location's code, where it has one, replaces val in the location
   field, also as loaded.  1, or 0 when a failed request ends the
   screen, with *cmd its command (else TUI_CMD_NONE). */
int scr_loc_show(form *f, const scr_locf *d, const char *val, int load,
                 u8 *cmd);

/* SCRPICK.C: the form_pick_fn work of location field d of form f: F4
   picks the location from the pick list, opened on val's location and
   refusing outside as scr_pick_loc does, into val (its code, or its ID
   when it has none; blank for the lead row in a record's form) and
   shows its path; a value typed (val NULL) has its path shown.  Then,
   after F4, the form's hints again (message hint).  A failed request
   that ends the screen stops the form as scr_pick_field does.  1 when
   a location was chosen, else 0. */
int scr_loc_field(form *f, const scr_locf *d, const char *outside,
                  char *val, u16 hint, u8 *leave);
#endif

/* ---------------------------------------------------------------------
 * Item form tabs.
 * --------------------------------------------------------------------- */

/* The item form's tabs.  The form takes SCR_ITEMFORM_H rows from TUI_TOP;
   below it a strip (to TUI_BOTTOM) names the tabs.  In view mode (or in edit
   mode while nothing has changed) the Record menu's Parts, Service records,
   Loans and holds, and Used with it run a tab of the item: a list of its
   parts, maintenance records, or loans and holds in the panel from SCR_TAB_TOP
   to TUI_BOTTOM, which covers the form below its first rows, with keys of its
   own, until Esc; then the form is drawn again.  A part chosen in the Parts
   tab is shown in the same form, which keeps the IDs of up to SCR_ITEMBACK
   items it showed before (dropping the oldest), so Esc, or a save, goes back
   through them before it leaves.  Each tab function runs its tab for item id
   and fills in *t. */
#define SCR_ITEMFORM_H  16
#define SCR_ITEMBACK    8       /* items the form goes back through with
                                   Esc after Enter on parts            */
#define SCR_BOMINDENT   8       /* levels the Parts tab indents; deeper
                                   parts are indented as this level    */
#define SCR_TAB_TOP     (TUI_TOP + 6)
#define SCR_TAB_H       (TUI_BOTTOM - SCR_TAB_TOP + 1)

typedef struct {
    u8   cmd;                   /* command to leave with, or TUI_CMD_NONE
                                   to go back to the form               */
    u8   reload;                /* 1: the item itself changed (its held
                                   units): the form loads it again      */
    char open[LV_KEYSZ];        /* an item to show in the form instead
                                   (a part chosen with Enter, a hosted
                                   row's item with Open item), or ""    */
} scr_tab;

/* ---------------------------------------------------------------------
 * Assemblies.
 * --------------------------------------------------------------------- */

/* SCRASSY.C: the Parts tab, ASSY BOM of item id.  Enter shows the
   part, Ins installs an item asked for (scr_ask_item), the Record menu's
   Take out takes the selected part out to a location chosen from the
   location pick list.  It leaves t->reload 0. */
void scr_assy_tab(const char *id, scr_tab *t);

/* SCRASSY.C: the assembly verbs of item lists, for the selected item id.
   scr_assy_move (Move) moves it with its parts to a location from the pick
   list (ASSY MOVE); scr_assy_into (Install in) installs it into an item asked
   for (scr_ask_item, ASSY INSTALL); scr_assy_out (Take out) takes it out of
   its assembly to a location from the pick list (ASSY REMOVE). Each returns 1
   when the list must be fetched again (the change was made, or item id has
   gone), else 0, with *cmd the command to leave with (TUI_CMD_NONE to stay).
   The location pick list leaves its own hints, so the caller draws its hints
   again afterwards (with tui_hint_keep, so that the message the functions
   leave on the hint row after a change, or after a refusal, stays until the
   next key). */
int scr_assy_move(const char *id, u8 *cmd);
int scr_assy_into(const char *id, u8 *cmd);
int scr_assy_out(const char *id, u8 *cmd);

/* ---------------------------------------------------------------------
 * Maintenance.
 * --------------------------------------------------------------------- */

/* SCRMAINT.C: the Service tab, MAINT LIST of item id; Ins adds a
   record.  It leaves t->reload 0. */
void scr_maint_tab(const char *id, scr_tab *t);

/* ---------------------------------------------------------------------
 * Loans.
 * --------------------------------------------------------------------- */

/* SCRLOAN.C: the Loans tab, LOAN LIST ITEM= of item id, and with
   Ctrl-PgDn or Ctrl-PgUp the rows of other items that name it as their
   host (HOST=), which scr_loan_used shows first.  The keys and verbs are
   the list's; a new row is one of this item, or beside the rows it hosts
   one of an item asked for, with this one its host; Open item there sets
   t->open to the selected row's item.  t->reload is 1 after a row was
   added, changed or ended. */
void scr_loan_tab(const char *id, scr_tab *t);
#ifdef TUI_VERBS
void scr_loan_used(const char *id, scr_tab *t);

/* SCRLOAN.C: the form of a new loan (Ctrl-L), reservation (Ctrl-R) or
   use (Ctrl-U), key k, of item, as the list's; the command to leave
   with.  A save leaves its note for the caller's list (lv_note).  Only
   with TUI_VERBS, as the Record menu, for Watcom C 6.5's memory. */
u8 scr_loan_add(const char *item, u16 k);
#endif

#endif
