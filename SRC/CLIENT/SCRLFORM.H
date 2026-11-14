/*
 * SCRLFORM.H - the location tree's rows and the location form's table
 * (SCRLFORM.C), which the location screens of each client show.
 *
 * Every client's location lists (INV's in SCRPICK.C) show the same rows
 * from LOC LIST, which answers in tree order (depth first, the
 * locations inside one in name order) with the fields DEPTH, LOC_ID,
 * PARENT_ID, LOC_NAME, LOC_CODE, LTYPE and CAPACITY, and has no FIELDS=.
 * A row holds, after one blank, the name indented SCR_LT_INDENT blanks a
 * level below the top and ending in MSG_CUTMARK when cut, the type's
 * name (from the LTYPE LIST the client holds in a ctab, CODETAB.H), the
 * code and the capacity: the capacity right-aligned, the others left,
 * each column as wide as its heading in the column header MO_HDR and
 * one blank after it.  Its key is LOC_ID.
 * No row is marked.  scr_loc_src is a template, as itl_src of ITEMLS.H
 * is: a client copies it and sets fmt to a format function of its own,
 * which calls scr_loc_row with its ctab, and may set args and arg.  A
 * pick list shows the first two columns of the same rows (SCR_LT_PICKW
 * characters), after a lead row (pg_lead of PAGER.H) for no location,
 * MO_NOLOC, the top level, MO_TOP, or every location, MO_ALLLOC, whose
 * key is "0".
 *
 * One table of fields serves both clients' location forms: INV draws it
 * in a box (SCRLOC.C), where row and column place each field, and the
 * Windows client maps the fields to the controls of a dialog by index.
 * The fields, in the table's order: LOC_NAME, LTYPE (a code of the
 * location types' list), PARENT_ID (the location it is inside, its
 * LOC_ID or code as typed, blank at the top level; the server moves it,
 * with everything inside it, when it changes), WHERE (a local text
 * showing PARENT_ID's path, never sent), LOC_CODE, CAPACITY, NOTES,
 * the FT_MEMO field, and LT_NAME (a local text showing LTYPE's name,
 * on LTYPE's row).  LOC ADD answers the new LOC_ID (ID=).
 *
 * A location's contents, the items at it, are an item list (ITEMLS.H)
 * over LOC CONTENTS id, titled with the location's path that LOC GET
 * gives (PATH=), with TOP=1 to show only the items not installed in
 * another and ALL=1 to show the items of every location inside it too,
 * which a location holding others shows first (scr_loc_first).
 * The texts are messages of SCRLOC.MSG.
 */
#ifndef SCRLFORM_H
#define SCRLFORM_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "CODETAB.H"

/* The tree's title, column header and empty text; the question before a
   location is deleted (^1 its name) and the notes of a delete, an add, a
   save and a move (^2 its name); the form's titles (^1 the name); the
   contents' empty text; the titles of the pick lists for a field and for
   a move, and their lead rows, for no location, the top level, and every
   location where a blank field means that (SCRLOC.MSG). */
#define MO_TITLE        520     /* Locations                            */
#define MO_HDR          521     /* column header                        */
#define MO_EMPTY        522     /* No locations                         */
#define MO_DELQ         523     /* Delete location ^1?                  */
#define MO_DELETED      524     /* Location ^2 deleted                  */
#define MO_ADDED        525     /* Location ^2 added                    */
#define MO_SAVED        526     /* Location ^2 saved                    */
#define MO_MOVED        527     /* Location ^2 moved                    */
#define MO_FTITLE       528     /* Location ^1                          */
#define MO_NEWTITLE     529     /* New location                         */
#define MO_NOITEMS      530     /* No items in this location            */
#define MO_PICK         531     /* Choose a location                    */
#define MO_MOVETO       532     /* Move into                            */
#define MO_NOLOC        533     /* (no location)                        */
#define MO_TOP          534     /* (top level)                          */
#define MO_NOSUCH       555     /* No location ^1 (a value typed)       */
#define MO_ALLLOC       556     /* (every location)                     */
#define MO_INSELF       557     /* not inside itself (a pick refused)   */
#define MO_TOODEEP      558     /* ^1 is as deep as a location goes     */
#define MO_PICKQ        559     /* Choose ^1, where the seek stopped?   */

/* The contents' texts, in the order of scr_item_list's tab: the status
   line's text (^1 and ^2 the two states), then its words while all the
   items show and while the top-level ones do, while the location's own
   items show and while everything below it does. */
#define MO_SHOW         540     /* Showing: ^1, ^2                      */
#define MO_S_ALL        541     /* all items                            */
#define MO_S_TOP        542     /* top level                            */
#define MO_S_HERE       543     /* this location                        */
#define MO_S_BELOW      544     /* everything below                     */

/* The form's labels; WHERE has none. */
#define MO_L_NAME       545
#define MO_L_TYPE       546
#define MO_L_INSIDE     547
#define MO_L_WHERE      548
#define MO_L_CODE       549
#define MO_L_CAP        550
#define MO_L_NOTES      551

/* The tree's columns: the blanks of indent per level, the name with its
   indent, the type's name; a pick list's width, the margin and these
   two; the capacity, which ends a row after the code (LINK_LOCLEN). */
#define SCR_LT_INDENT   2
#define SCR_LT_NAMEW    38
#define SCR_LT_TYPEW    18
#define SCR_LT_PICKW    (1 + SCR_LT_NAMEW + 1 + SCR_LT_TYPEW)
#define SCR_LT_CAPW     8

/* Digits of a capacity. */
#define SCR_LF_CAPW     4

/* Characters of the path the form's WHERE shows. */
#define SCR_LF_PATHW    56

/* Indexes of the fields in scr_loc_fields. */
#define SCR_LF_NAME     0
#define SCR_LF_TYPE     1
#define SCR_LF_PARENT   2
#define SCR_LF_WHERE    3
#define SCR_LF_CODE     4
#define SCR_LF_CAP      5
#define SCR_LF_NOTES    6       /* the last row                         */
#define SCR_LF_TNAME    7       /* the type's name, on the type's row   */
#define SCR_LF_COUNT    8

/* The source, fmt, args and arg NULL. */
extern const lv_src scr_loc_src;

/* The rows' key, LOC_ID, and their DEPTH field. */
extern const char scr_loc_k_id[];
extern const char scr_loc_k_depth[];

/* Make the row of r, text and key as an lv_fmt_fn makes them, the type's
   name from types. */
void scr_loc_row(const lv_rec *r, ctab *types, char *text, char *key);

/* The fields, and the form of all SCR_LF_COUNT of them, titled
   MO_NEWTITLE, with the help topic LOC, in a box below the top of the
   work area. */
extern const form_field scr_loc_fields[];
extern const form_def scr_loc_def;

/* A location field of a form: an FT_CODE field holding a location's
   LOC_ID or code as typed, and a local FF_RO text field (FF_LOCAL) that
   shows its path, cut to the field's cells; a location chosen or loaded
   shows its code where it has one, else its LOC_ID.  In a record's form
   (filter 0) blank and "0" mean no location; in a filter (filter 1)
   blank means any location and "0" no location.  For no location the
   path field shows message none, which also names the pick list's lead
   row. */
typedef struct {
    const form_field *loc;
    const form_field *path;
    u16  none;                  /* MO_NOLOC, MO_TOP or MO_ALLLOC        */
    u8   filter;
} scr_locf;

/* The location form's container: PARENT_ID with its path (WHERE), the
   top level, MO_TOP, for none. */
extern const scr_locf scr_loc_inside;

/* What LOC GET says of a location: its ID, name, code, type code, the
   LOC_ID of the location it is inside ("0" at the top level), its depth
   (1 at the top level) and path.  scr_loc_info is an rq_fn filling the
   one arg points at from LOC GET's K lines; the caller clears it
   first. */
typedef struct {
    char id[LINK_IDLEN + 1];
    char name[LINK_LOCNAMELEN + 1];
    char code[LINK_LOCLEN + 1];
    char type[LINK_LTYPELEN + 1];
    char parent[LINK_IDLEN + 1];
    char path[LINK_LOCPATHLEN + 1];
    u16  depth;
} scr_locinfo;

int scr_loc_info(void *arg, const rq_line *ln);

/* The type of the first location inside location id into type
   (LINK_LTYPELEN + 1 bytes), "" when id holds none: the first row of
   LOC LIST UNDER=id over l.  RQ_OK, or the failure's code and res.  A
   new location inside id starts with that type, and the contents of a
   location holding others start with every item below it. */
int scr_loc_first(link *l, const char *id, char *type, rq_res *res);

#endif
