/*
 * SCRMFORM.H - the service record form's table and the service lists'
 * row source (SCRMFORM.C), which the service screens of each client
 * show: INV's (SCRMAINT.C) and the Windows client's.
 *
 * One table of fields serves both clients, as the item form's does
 * (SCRIFORM.H): INV draws it on the grid of its screens, the Windows
 * client maps the fields to the controls of a dialog by index.  The
 * fields, in the table's order: MAINT_ID, ITEM_ID, MDATE, MTYPE,
 * SUMMARY, COST, DONE_BY and NOTES.  MAINT_ID is FF_RO, set by the
 * server; ITEM_ID, the FF_KEY field that MAINT ADD takes as its word, is
 * FF_RO too, preset by the client for a new record; NOTES is the FT_MEMO
 * field, the record's DETAIL memo.
 *
 * Both lists are one paged MAINT command: MAINT RECENT, the newest
 * record added first, through scr_maint_src, or MAINT LIST for one item,
 * the oldest service date first, through scr_maint_isrc.  Each is a
 * template, as itl_src of ITEMLS.H is: a client copies it and sets fmt
 * to a format function of its own, which calls scr_maint_row, and for
 * scr_maint_isrc args to one that adds the item's ID as the command's
 * word.  A row holds the record's ID, for the recent list the item's,
 * then the service date, type, cost and as much of the summary as is
 * left of LV_TEXTLEN, in the columns of MV_HDR (MV_HDRTAB without the
 * item); its key is MAINT_ID, which the End walk fetches alone.  No row
 * is marked.  The labels, titles and texts are messages of SCRMAINT.MSG.
 */
#ifndef SCRMFORM_H
#define SCRMFORM_H

#include "COMPAT.H"
#include "PAGER.H"
#include "FMODEL.H"

/* The lists' texts, the question before a record is deleted and the
   notes of a record added, saved and deleted. */
#define MV_T_RECENT     830     /* Recent service                       */
#define MV_HDR          832     /* column header of the recent list     */
#define MV_HDRTAB       833     /* column header of an item's records   */
#define MV_EMPTY        834     /* No service records                   */
#define MV_DELQ         844     /* Delete service record ^1?            */
#define MV_ADDED        842     /* Service record ^1 added              */
#define MV_SAVED        843     /* Service record ^1 saved              */
#define MV_DELETED      845     /* Service record ^1 deleted            */
#define MV_T_ITEM       855     /* Service records of item ^1           */

/* The form's titles and labels. */
#define MV_TITLE        840     /* Service record ^1                    */
#define MV_NEWTITLE     841     /* New service record                   */
#define MV_L_ID         847
#define MV_L_ITEM       848
#define MV_L_DATE       849
#define MV_L_TYPE       850
#define MV_L_SUMMARY    851
#define MV_L_COST       852
#define MV_L_DONEBY     853
#define MV_L_NOTES      854

/* Indexes of fields in scr_maint_fields. */
#define SCR_MF_ITEM     1
#define SCR_MF_DATE     2
#define SCR_MF_TYPE     3
#define SCR_MF_SUM      4
#define SCR_MF_COST     5
#define SCR_MF_NOTES    7
#define SCR_MF_COUNT    (SCR_MF_NOTES + 1) /* fields: NOTES is the last */

/* Widths of the record's fields (MAINT.DBF). */
#define MAINT_TYPEW     2
#define MAINT_SUMW      60
#define MAINT_COSTW     8
#define MAINT_COSTDEC   2
#define MAINT_DONEW     20

/* Columns of a row (the headers MV_HDR and MV_HDRTAB match them): the
   IDs, the date (MSG_DATEW), the type in MAINT_TYPEW, the cost; the
   summary takes what is left of the row. */
#define VC_ID           8       /* record and item IDs                  */
#define VC_COST         MAINT_COSTW

/* MAINT_ID, the name of the record's ID field and the lists' key. */
extern const char scr_maint_k_id[];

/* The fields, and the form of all SCR_MF_COUNT of them, titled
   MV_NEWTITLE, with the help topic SERVREC, in INV's pop-up box of
   SCR_POPW columns at SCR_POPTOP, SCR_POPLEFT (SCRSHR.H). */
extern const form_field scr_maint_fields[];
extern const form_def scr_maint_def;

/* The sources, fmt, args and arg NULL. */
extern const lv_src scr_maint_src;
extern const lv_src scr_maint_isrc;

/* Make the row of r, text and key as an lv_fmt_fn makes them, with the
   item's ID in its column when withitem is 1 (the recent list). */
void scr_maint_row(const lv_rec *r, int withitem, char *text, char *key);

#endif
