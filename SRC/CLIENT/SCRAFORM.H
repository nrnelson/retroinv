/*
 * SCRAFORM.H - the parts list's row source and the assembly requests
 * (SCRAFORM.C), which the assembly screens of each client use: INV's
 * (SCRASSY.C) and the Windows client's.
 *
 * The parts list is ASSY BOM for one item, paged: every part below the
 * item, depth first, each part before its own parts.  scr_assy_src is a
 * template, as itl_src of ITEMLS.H is: a client copies it and sets its
 * arg to the item's ID, never NULL, which scr_assy_args gives as the
 * command's word.  A row holds the part's ID, category, maker, model,
 * quantity, location and status in the columns of MA_HDRBOM, the maker
 * indented by the part's LEVEL, which the response gives first (1 for a
 * part of the item itself), so the tree's shape shows; its key is
 * ITEM_ID, which the End walk fetches alone (LEVEL always comes with
 * it).  No row is marked.
 *
 * The changes are ASSY INSTALL child parent, ASSY REMOVE child LOC= and
 * ASSY MOVE id LOC=, sent by scr_assy_send.  The server checks each one
 * (cycles, depth, size, loans, the level) and names the field it
 * refuses: CHILD, PARENT, LOC, or ID for MOVE.  A 200 naming an ID the
 * user typed is no record that has gone but an ID that names no item;
 * scr_assy_send gives it as a 202 for that field, so that a client
 * shows it as it shows the server's other refusals of the field.
 *
 * The assemblies, the top-level items with parts, are an item list
 * (ITEMLS.H) over ASSY LIST TOP=1, titled MA_T_LIST.  The texts are
 * messages of SCRASSY.MSG.
 */
#ifndef SCRAFORM_H
#define SCRAFORM_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "PAGER.H"

/* The assemblies' title and empty text, the parts list's texts, the
   questions of an install, the notes of an install, a removal and a
   move, and the Windows client's title of an item's parts and the
   questions of a move and of a removal. */
#define MA_T_LIST       800     /* Assemblies                           */
#define MA_EMPTY        801     /* No assemblies                        */
#define MA_HDRBOM       803     /* column header of the parts list      */
#define MA_EMPTYBOM     804     /* No installed parts                   */
#define MA_Q_CHILD      806     /* Install an item in item ^1           */
#define MA_Q_PARENT     807     /* Install item ^1 in                   */
#define MA_INSTALLED    809     /* Item ^1 installed in item ^2         */
#define MA_REMOVED      810     /* Item ^1 taken out to ^2              */
#define MA_MOVED        811     /* Item ^1 moved to ^2                  */
#define MA_T_ITEM       812     /* Parts of item ^1                     */
#define MA_Q_MOVE       813     /* Move item ^1 to                      */
#define MA_Q_OUT        814     /* Take item ^1 out to                  */

/* Columns of a part's row (the header MA_HDRBOM matches them). */
#define AC_ID           8
#define AC_NAME         20      /* maker (with the indent), model       */
#define AC_QTY          5
#define AC_LOC          13      /* the location's name                  */
#define AC_INDENT       2       /* columns of indent per level          */

/* scr_assy_send's typed: which of its IDs the user typed. */
#define SCR_AT_NONE     0
#define SCR_AT_CHILD    1       /* the first, the part                  */
#define SCR_AT_PARENT   2       /* the second, the assembly             */

/* The commands; CHILD and PARENT, the fields an install names; LOC, the
   pair of a removal and a move, and the field naming a location the
   server does not know. */
extern const char scr_assy_w_install[];
extern const char scr_assy_w_remove[];
extern const char scr_assy_w_move[];
extern const char scr_assy_k_child[];
extern const char scr_assy_k_parent[];
extern const char scr_assy_k_loc[];

/* The parts list's source, with args and fmt below and arg NULL. */
extern const lv_src scr_assy_src;

/* The source's functions, arg the item's ID: ASSY BOM's word; the
   row. */
void scr_assy_args(void *arg, rq *q);
void scr_assy_fmt(void *arg, const lv_rec *r, char *text, char *key);

/* Send "words a [b] [LOC=loc]" over l, b and loc left out when NULL;
   RQ_OK, or the failure's code and res.  typed (SCR_AT_) names the ID
   the user typed, a or b: a 200 naming its field CHILD or PARENT comes
   back as a 202 for that field, its text MI_NOITEM with the ID. */
int scr_assy_send(link *l, const char *words, const char *a, const char *b,
                  const char *loc, u8 typed, rq_res *res);

#endif
