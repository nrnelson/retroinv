/*
 * ITEMLS.H - the item list's row source, which every client shows its
 * item lists through (ITEMLS.C).
 *
 * Every client's item lists (INV's in SCRITEM.C) show the same rows
 * from the same requests: ITEM LIST with ORDER=ID, the tree's order
 * (LOC=0 ALL=1, LV_TREEORDER of PAGER.H: the items of no location, then
 * those of each location depth first), ORDER=MFR or ORDER=SERIAL, the
 * command's default fields for the first three and
 * FIELDS=ITEM_ID,MFR,MODEL,SERIALNO,LOC_NAME for SERIAL, the End walk
 * asking for ITEM_ID alone.  A row holds the ID, then the category,
 * maker, model, works state (the word of its WORKS letter, below),
 * quantity, location and why the item is not available (the name of its
 * AVAIL letter, below; blank when it is available), or for SERIAL the
 * maker, model, serial number and location, the location being the name
 * of the one the item is at (LOC_NAME): the ID and the quantity
 * right-aligned, the others left, the maker, the model and the location
 * ending in MSG_CUTMARK when cut, each column as wide as its heading in
 * the column header MI_HDR or MI_HDRSER and one blank after it.
 * Its key is ITEM_ID as the server sends it, which, being a number, carries no
 * blanks (eng_rec_text of ENGINE.H).  No row is marked.
 *
 * itl_src is a template, constant near data: a client copies it
 * (memcpy, as Watcom C 6.5 cannot assign from a const object) and sets
 * fmt to a format function of its own, which calls itl_row with the
 * index of the order its list has in use, as only the list knows it.
 * The client may set args and arg too, and for the results of another
 * item command (ITEM FIND, LOC CONTENTS, ...) change cmd and empty and
 * set norders to 0: no ORDER=, the default fields.
 */
#ifndef ITEMLS_H
#define ITEMLS_H

#include "COMPAT.H"
#include "PAGER.H"

/* The field and key names of the source and its rows, which the item
   form shares, the service record and loan forms ITEM_ID and the loan
   lists' rows ITEM_ID, MFR and MODEL; itl_k_loc is LOC_NAME, the name of
   the item's location; itl_k_id is the ID order's ORDER= value, which
   also names the new item in ITEM COPY's +OK. */
extern const char itl_k_itemid[];
extern const char itl_k_cat[];
extern const char itl_k_status[];
extern const char itl_k_mfr[];
extern const char itl_k_model[];
extern const char itl_k_serial[];
extern const char itl_k_cond[];
extern const char itl_k_works[];
extern const char itl_k_qty[];
extern const char itl_k_loc[];
extern const char itl_k_id[];

/* Texts of the source, messages of SCRITEM.MSG; a list in the order
   itl_src.orders[i] is titled MI_T_ID + i. */
#define MI_T_ID         450     /* Items by ID (then LOC, MFR, SERIAL)  */
#define MI_T_LOC        451
#define MI_T_MFR        452
#define MI_T_SER        453
#define MI_HDR          454     /* column header, default field list    */
#define MI_HDRSER       455     /* column header, serial number order   */
#define MI_EMPTY        456     /* No items                             */

/* A row's first columns, for a client reading a row back: the ID, then
   (but in the serial number order, index ITL_ORDSER of itl_src.orders)
   the category, LINK_CATLEN wide, then the maker and the model, each
   with one blank after it. */
#define ITL_IDW         8
#define ITL_NAMEW       15
#define ITL_ORDSER      3

/* The other columns of a row (the headers MI_HDR and MI_HDRSER match
   them): the category, works state, quantity, location and why the item
   is not available; in the serial number order the serial number and
   the location. */
#define IC_CAT          LINK_CATLEN
#define IC_WORKS        5
#define IC_QTY          5
#define IC_LOC          10
#define IC_WHY          9
#define IC_SERIAL       20
#define IC_SLOC         16

/* Why an item is not available: the letters of its AVAIL column, the
   first three, ITL_KINDS, also the kinds of a loan or hold (SCRNFORM.H),
   and their names, messages of SCRNFORM.MSG from MH_K_NAME in the same
   order: on loan, reserved, in use, installed, disposed of, no units. */
#define ITL_KINDS       "LRU"
#define ITL_WHYS        ITL_KINDS "IDN"
#define MH_K_NAME       908     /* On loan                              */
#define MH_K_RES        909
#define MH_K_USE        910
#define MH_AV_I         911
#define MH_AV_D         912
#define MH_AV_N         913

/* An item's works states, the letters of its WORKS column (not tested,
   works, intermittently, does not work), and their words in the rows,
   messages of TUI.MSG from MT_WK_Q in the same order, never paged out;
   a letter that is none of them shows as it is. */
#define ITL_WORKS       "?YIN"
#define MT_WK_Q         47      /* ?                                    */
#define MT_WK_Y         48
#define MT_WK_I         49
#define MT_WK_N         50

/* ITL_WHYS as one array for the item rows and the kinds of SCRNFORM.H: a
   letter's number is its distance into it.  RPT.C, whose tests link no
   item list, keeps its own copy of the kinds' letters, ITL_KINDS. */
extern const char itl_whys[];

/* ITL_WORKS as one array, for the item rows' works words. */
extern const char itl_works[];

/* The source, fmt NULL.  orders[i] is the order of the browse command
   SCMD_ITEMS_ID + i of SCR.H, up to SCMD_ITEMS_SER (the build fails
   unless the commands and orders agree). */
extern const lv_src itl_src;

/* Make the row of r for index order of itl_src.orders, text and key as
   an lv_fmt_fn makes them. */
void itl_row(const lv_rec *r, u8 order, char *text, char *key);

#endif
