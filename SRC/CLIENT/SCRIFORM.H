/*
 * SCRIFORM.H - the item form's table (SCRIFORM.C), which the item
 * screen of each client shows over the form model (FMODEL.H).
 *
 * One table of fields serves both clients: INV draws it on the grid of
 * its screens (SCRITEM.C), where row and column place each field, and
 * the Windows client maps the fields to the controls of a dialog by
 * index.  The labels, the titles and the notes that follow a change
 * are messages of SCRITEM.MSG.  Both clients start a new item and load
 * a saved one as scr_item_preset, scr_item_kline and scr_item_loaded
 * say.
 *
 * The fields, in the table's order: ITEM_ID, CATEGORY, STATUS, ITEMTYPE,
 * COND, MFR, MODEL, SERIALNO, DESCR, WORKS, QTY, HELD, LOC_ID,
 * PARENT_ID, WHERE, ACQ_DATE, ACQ_SRC, ACQ_PRICE, CUR_VALUE, TAGS,
 * CREATED, UPDATED and NOTES; SCR_IF_ gives the index of each one the
 * screens name.  ITEM_ID, PARENT_ID, CREATED and UPDATED are FF_RO, set
 * by the server; NOTES is the FT_MEMO field.  COND, the cosmetic grade,
 * may be blank (not graded); WORKS, the works state, may not.  TAGS is a
 * code field: F4 picks a tag in use to append to it (ct_tags_add of
 * CODETAB.H), and the server keeps it normalized, so a saved item is
 * shown again as it stores it.  HELD, a local FF_RO text without a label,
 * says how many units are lent, reserved and in use (scr_item_loaded).
 * LOC_ID holds the location's LOC_ID or code as typed, blank for none,
 * and WHERE, a local FF_RO text, shows its path (SCRLFORM.H).
 * PARENT_ID is text SCR_PARENTW wide, for the screens to show the
 * parent's maker and model after its ID.
 */
#ifndef SCRIFORM_H
#define SCRIFORM_H

#include "COMPAT.H"
#include "FMODEL.H"
#include "SCRLFORM.H"

/* The form's titles, and the question before an item is deleted
   (SCRITEM.MSG). */
#define MI_DELQ         460     /* Delete item ^1?                      */
#define MI_TITLE        461     /* Item ^1: a saved item, ^1 its ID     */
#define MI_NEWTITLE     462     /* New item                             */

/* The notes of an item added, saved, deleted and copied, ^1 its ID. */
#define MI_ADDED        465     /* Item ^1 added                        */
#define MI_SAVED        466     /* Item ^1 saved                        */
#define MI_DELETED      467     /* Item ^1 deleted                      */
#define MI_COPIED       468     /* Copied as item ^1                    */

/* The text for an item ID that names no item; the item prompt's label;
   the titles of the item pick list (SCRSFORM.H's scr_search_item), and
   of one listing only the available items (a new loan's or hold's), and
   that one's empty text. */
#define MI_NOITEM       496     /* No item ^1                           */
#define MI_L_ITEMID     495     /* the item prompt's label              */
#define MI_PICK         499     /* Choose an item                       */
#define MI_PICKAVL      458     /* Choose an available item             */
#define MI_NOAVAIL      500     /* None available                       */

/* The split's prompt title, ^1 the item's ID, and its note, ^1 the new
   item's ID. */
#define MI_SPLITQ       497     /* Split item ^1                        */
#define MI_SPLIT        498     /* Split off item ^1                    */

/* The form's labels. */
#define MI_L_ID         471
#define MI_L_CAT        472
#define MI_L_STATUS     473
#define MI_L_TYPE       474
#define MI_L_MFR        475
#define MI_L_MODEL      476
#define MI_L_SERIAL     477
#define MI_L_DESCR      478
#define MI_L_COND       479
#define MI_L_QTY        480
#define MI_L_WORKS      481
#define MI_L_LOC        482
#define MI_L_PARENT     483
#define MI_L_WHERE      470
#define MI_L_ACQ        484
#define MI_L_SRC        485
#define MI_L_PRICE      486
#define MI_L_VALUE      487
#define MI_L_TAGS       488
#define MI_L_CREATED    489
#define MI_L_UPDATED    490
#define MI_L_NOTES      491

/* Indexes of fields in scr_item_fields. */
#define SCR_IF_CAT      1
#define SCR_IF_COND     4
#define SCR_IF_SERIAL   7
#define SCR_IF_WORKS    9
#define SCR_IF_HELD     11
#define SCR_IF_LOC      12
#define SCR_IF_PARENT   13
#define SCR_IF_WHERE    14
#define SCR_IF_TAGS     19
#define SCR_IF_NOTES    22
#define SCR_IF_COUNT    (SCR_IF_NOTES + 1) /* fields: NOTES is the last */

/* The fields, and the form of all SCR_IF_COUNT of them, titled
   MI_NEWTITLE, with the help topic ITEM, in a box of SCR_ITEMFORM_H
   rows from TUI_TOP across the screen. */
extern const form_field scr_item_fields[];
extern const form_def scr_item_def;

/* The item form's location: LOC_ID with its path (WHERE), MO_NOLOC for
   none. */
extern const scr_locf scr_item_loc;

/* Load the values a new item's form f starts with, each as unchanged:
   WORKS ?, QTY 1 and STATUS A (COND stays blank). */
void scr_item_preset(form *f);

/* The cells of HELD, from SCR_HELDC, after the quantity, to one before
   the box's right edge. */
#define SCR_HELDC       (SCR_V2 + LINK_QTYLEN + 2)
#define SCR_HELDW       (VID_COLS - 2 - SCR_HELDC)

/* The rq_fn of ITEM GET into form arg: form_kline, the counts ON_LOAN,
   RESERVED, IN_USE and ATTACHED kept for scr_item_loaded, which is to
   follow the request.  Returns 0. */
int scr_item_kline(void *arg, const rq_line *ln);

/* After ITEM GET has loaded f through scr_item_kline: LOC_ID 0 (no
   location) and a top-level item's PARENT_ID 0 loaded as empty values,
   so their fields show blank; a blank LOC_ID sent is no location too,
   and PARENT_ID is FF_RO and never sent, so a save does not see the
   difference.  HELD is worded from the counts: the units on loan,
   reserved and in use, each by its kind's name (MH_K_NAME) when not 0;
   for an active top-level item MH_AVAIL with the units left of QTY,
   when there are any; then MH_HOSTS when other items' rows name it as
   their host; the line cut to SCR_HELDW ends in MSG_CUTMARK.  INVWIN
   writes the counts with its thousands separators (wrow_intl of
   WROWS.H), as it shows QTY. */
void scr_item_loaded(form *f);

/* ASSY PATH's fixed columns: LEVEL ITEM_ID CATEGORY MFR MODEL LOC_ID
   LOC_NAME. */
#define SCR_AP_ID       1
#define SCR_AP_MFR      3
#define SCR_AP_MODEL    4

/* An rq_fn for ASSY PATH of the item in form arg, after ITEM GET and
   scr_item_loaded: the row of its parent, the item whose ID the Parent
   field holds, adds that item's maker and model to the field, cut to
   SCR_PARENTW.  A failed or short response leaves the bare ID, as the
   parent's row comes last.  Returns 0. */
int scr_item_parent(void *arg, const rq_line *ln);

#endif
