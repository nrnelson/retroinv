/*
 * RPT.H - the layout of reports and labels (RPT.C).
 *
 * A report is a column layout of one paged list command, laid out in
 * pages of the page length with a header on every page; a label run
 * puts each record on one label of a continuous strip.  RPT.C receives
 * the list's F and R lines (or the K lines of one record) as the
 * response decoder gives them and writes through a pr job (PRINT.H);
 * it sends no request and shows nothing on the screen.
 *
 *   RPT_LIST    REPORT ITEMS: ID, category, maker, model, serial,
 *               works state, quantity, location's name; with RPT_O_GROUP
 *               in groups by location (rpt_group), counts per group and
 *               in all
 *   RPT_VALUE   REPORT ITEMS: ID, category, maker, model, quantity,
 *               price paid and value; grouped as RPT_LIST with subtotals,
 *               the total, and totals per category (rpt_cat) on the last
 *               page; ACQ_PRICE and CUR_VALUE are each item's own
 *               amounts, added as stored (not multiplied by QTY)
 *   RPT_LOANS   REPORT LOANS: late mark, loan, item, kind, quantity,
 *               holder, contact, due date, maker and model; the count
 *               and the late count; RPT_O_OVERDUE for the overdue title
 *   RPT_ILABEL  item labels from REPORT ITEMS rows, or ITEM GET with the
 *               K line LOC_NAME of its location
 *   RPT_BLABEL  bin labels from rows of LOC_NAME, LOC_CODE, PATH and
 *               LT_NAME (the type's name)
 *
 * Page: RPT_TOP lines of header (program and title with the date and
 * page number, the subtitle, a blank line, the column heading, a rule),
 * then rows, totals and group lines, then RPT_BOTTOM blank lines left
 * to the page end (pr_ff), whose length the job's form length matches.
 * Every line holds at most RPT_WIDTH characters; longer values are cut,
 * a location's name and a label's line ending in MSG_CUTMARK.
 * A group heading never starts on the last RPT_KEEP lines of a page.
 * Amounts are read as text with two decimals and added as scaled
 * integers; a total that does not fit 32 bits prints as '*' marks.
 *
 * Label: at most labelh - 1 text lines of at most labelw characters,
 * padded with empty lines to labelh (no page ends: the strip moves
 * labelh lines per label).  Item label: bold ID and category, maker and
 * model, serial number, location's name.  Bin label: the location's
 * name bold and double width, its code (a line only when it has one),
 * its path (cut to keep the nearest names, msg_path) and its type.
 */
#ifndef RPT_H
#define RPT_H

#include "COMPAT.H"
#include "RQ.H"
#include "PRINT.H"

/* The titles of two reports, which their forms share. */
#define MR_T_LIST       951
#define MR_T_VALUE      952

#define RPT_LIST        1
#define RPT_VALUE       2
#define RPT_LOANS       3
#define RPT_ILABEL      4
#define RPT_BLABEL      5

#define RPT_O_GROUP     0x01    /* RPT_LIST, RPT_VALUE: rpt_group       */
#define RPT_O_OVERDUE   0x02    /* RPT_LOANS of REPORT LOANS OVERDUE=1  */

#define RPT_WIDTH       79      /* characters of a report line          */
#define RPT_TOP         5       /* header lines of a page               */
#define RPT_BOTTOM      6       /* blank lines at the foot of a page    */
#define RPT_KEEP        3       /* lines a group heading needs below it */
#define RPT_MINPAGE     20      /* page length range, lines             */
#define RPT_MAXPAGE     99
#define RPT_MINLABELW   20      /* label width range, characters        */
#define RPT_MAXLABELW   79
#define RPT_MINLABELH   3       /* label pitch range, lines             */
#define RPT_MAXLABELH   20
#define RPT_SUBLEN      RPT_WIDTH   /* characters of a subtitle         */

/* Bytes one row's values take in a caller's page buffer, a NUL after
   each: the widest row of REPORT ITEMS, REPORT LOANS and a bin label
   fits. */
#define RPT_ROWMAX      192

typedef struct {
    u8   pagelen;               /* RPT_MINPAGE..RPT_MAXPAGE             */
    u8   labelw;                /* RPT_MINLABELW..RPT_MAXLABELW         */
    u8   labelh;                /* RPT_MINLABELH..RPT_MAXLABELH         */
} rpt_geom;

typedef struct rpt rpt;         /* a report being laid out (opaque)     */

/* rpt_open's code for a kind that is not one of the RPT_ kinds (after
   the PR_ codes, which the other calls return). */
#define RPT_EKIND       (PR_ENOMEM + 1)

/* Start kind (RPT_) with options (RPT_O_) on job p, geometry g (values
   out of range are taken as the nearest in range), date "YYYYMMDD" for
   the page header and sub the subtitle (copied; NULL or "" for none).
   Reports set the job's form length to g->pagelen.  Returns PR_OK, or
   PR_ENOMEM when memory runs out or RPT_EKIND for a kind out of range,
   with *out NULL. */
int rpt_open(rpt **out, u8 kind, u8 opts, pr *p, const rpt_geom *g,
             const char *date, const char *sub);

/* RPT_VALUE: one category of CAT LIST (code and name), before the first
   row; at most LINK_MAXCODES.  Totals of rows whose category was not
   given are added under the code alone.  0, or PR_ENOMEM. */
int rpt_cat(rpt *r, const char *code, const char *name);

/* One decoded data line: F names the columns (values are found by field
   name, so their order does not matter), R is one record, K one field
   of a single record (ITEM GET, LOC GET), which rpt_record then
   prints.  Other kinds are ignored.  Returns pr_failed of the job. */
int rpt_line(rpt *r, const rq_line *ln);
int rpt_record(rpt *r);

/* With RPT_O_GROUP (else nothing): end the group laid out, if any, with
   its totals, and begin the group of the rows that follow, headed by
   location path path (as PATH= gives it; "" for the items of no
   location), cut to fit with its nearest names kept. */
void rpt_group(rpt *r, const char *path);

/* Records laid out so far (for progress). */
u32 rpt_count(rpt *r);

/* Group and grand totals, the category totals (RPT_VALUE), the last
   page end; frees r (the job stays open).  Returns pr_failed of the
   job.  r may be NULL. */
int rpt_close(rpt *r);

/* End a run that was stopped, or whose request or printing failed,
   without any totals (no group or grand total, no category table), so
   that nothing on paper looks complete: a report gets one line saying
   it stopped, after the last row laid out, and its last page end; a
   label run just ends.  Frees r as rpt_close does and returns pr_failed
   of the job.  r may be NULL. */
int rpt_abort(rpt *r);

#endif
