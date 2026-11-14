/*
 * RPTRUN.H - the requests of a report or label run, laid out as they
 * arrive (RPTRUN.C).
 *
 * A run sends the requests of its RPT_ kind over a link and gives their
 * lines to a layout (RPT.H) on a pr job (PRINT.H) of the caller's; it
 * shows nothing and reads no keys.  The caller is told of the progress,
 * and may stop the run, through a step function called after each page.
 * Filters left "" are not sent; date is always set, as it is also the
 * date of the page header.
 *
 * The REPORT ITEMS kinds also send TAG= and WORKS= when tag and works
 * are given.
 *
 *   RPT_LIST    REPORT ITEMS with CAT, LOC, STATUS and ORDER=order;
 *               without order, the server's default order, ungrouped;
 *               ORDER=LOC, or below, goes in the tree's order, grouped
 *   RPT_VALUE   CAT LIST, its rows given to rpt_cat; then REPORT ITEMS
 *               with CAT and STATUS=A in the tree's order, grouped
 *   RPT_LOANS   REPORT LOANS with OVERDUE=1 when overdue, and ASOF=date
 *   RPT_ILABEL  ITEM GET item when item is given (rrun_one), then LOC GET
 *               of its location for LOC_NAME; else REPORT ITEMS with CAT
 *               in the tree's order
 *   RPT_BLABEL  LTYPE LIST for the type names, then a label for loc
 *               (LOC GET), and with below, or when loc names no location
 *               ("" or "0"), one for every location of LOC LIST
 *               [UNDER=loc], in the tree's order, its path made of the
 *               names of the rows above it
 *
 * The tree's order is that of REPORT ITEMS LOC=loc ALL=1: the items at
 * loc, then those of each location below it, depth first; loc "" stands
 * for "0", the items of no location first, then the whole tree.  loc
 * "0" with below is the items of no location alone.  Without below a
 * loc given lists the items at loc only.  A grouped report starts a
 * group (rpt_group) at each change of LOC_ID in the rows, headed with
 * the location's path from LOC GET, or with its name when it has gone
 * since (RPT_O_GROUP of RPT_LIST and RPT_VALUE asks for the groups).
 *
 * A list is asked for LINK_PAGEMAX rows at a time (PAGE=), with AFTER=
 * the cursor of the page before, until a page comes without one.  The F
 * and K lines go to the layout as the response is read.  The values of
 * each R line, a NUL after each, go into a far page buffer of
 * LINK_PAGEMAX rows of RPT_ROWMAX bytes, cut where the row's bytes run
 * out, and are laid out (rpt_line) only after rq_send has returned, so
 * a printer that does not take a byte (the job's wait function) is
 * never asked about while a response is being read.  The run ends after
 * the page on which the job failed.
 *
 * A run is rrun_open, rrun_layout, rrun_go and rrun_close, in that
 * order; rrun_close ends any run rrun_open started, with or without a
 * layout, and reads p only while a layout is open.
 */
#ifndef RPTRUN_H
#define RPTRUN_H

#include "COMPAT.H"
#include "LINK.H"
#include "MSG.H"
#include "RQ.H"
#include "PRINT.H"
#include "RPT.H"
#include "CODETAB.H"

#define RRUN_WORDLEN    3       /* longest ORDER or STATUS word         */
#define RRUN_NFI        4       /* fields read by place from a row      */
#define RRUN_NOF        0xFFu   /* ... a field the row lacks            */
#define RRUN_DATESZ     (MSG_DATELEN + 1) /* "YYYYMMDD" and the NUL */

/* The keys of the filters in the requests, which a form of the filters
   takes as its field names; RPT_LIST's location order is ORDER=LOC. */
extern const char rrun_k_cat[];
extern const char rrun_k_loc[];
extern const char rrun_k_status[];
extern const char rrun_k_tag[];
extern const char rrun_k_works[];
extern const char rrun_k_order[];
extern const char rrun_k_overdue[];
extern const char rrun_k_asof[];

/* Called after each page laid out, and after the one record of a GET
   run, so the progress may be shown from the rrun (pages, rpt_count of
   r); more is 1 when another page is to come.  A nonzero return stops
   the run there, with stopped set, and rrun_close then ends it as cut
   short. */
typedef int (*rrun_fn)(void *arg, int more);

/* One run.  The caller sets the filters and p, which RPTRUN.C only reads;
   the rest is the run's, which the caller may read. */
typedef struct {
    u8   kind;                  /* RPT_                                 */
    u8   overdue;               /* RPT_LOANS: the overdue loans only    */
    u8   below;                 /* every location below loc too         */
    u8   stopped;               /* the step function stopped the run    */
    u8   nv[LINK_PAGEMAX];      /* values of each row in pg             */
    char order[RRUN_WORDLEN + 1];   /* RPT_LIST: ORDER=; LOC groups the
                                   rows by location (RPT_O_GROUP)       */
    char status[RRUN_WORDLEN + 1];  /* RPT_LIST: STATUS=, "" for all    */
    char cat[LINK_CATLEN + 1];  /* CAT= of the REPORT ITEMS kinds       */
    char tag[LINK_TAGFILT + 1]; /* TAG= of the REPORT ITEMS kinds       */
    char works[LINK_NWORKS + 1];    /* WORKS= of the REPORT ITEMS kinds */
    char loc[LINK_LOCLEN + 1];  /* the location (its LOC_ID or code) of
                                   the REPORT ITEMS kinds, RPT_BLABEL's
                                   first label, "" for all              */
    char item[LINK_IDLEN + 1];  /* RPT_ILABEL: the one item             */
    char date[RRUN_DATESZ];     /* "YYYYMMDD": RPT_LOANS' ASOF=, and the
                                   date of the page header              */
    pr   *p;                    /* the job, open before rrun_layout and
                                   closed by the caller                 */
    rpt  *r;                    /* the layout, NULL when none is open   */
    cmem_blk *pg;               /* the page buffer, then a name for
                                   each level of the tree               */
    ctab types;                 /* RPT_BLABEL: the location types       */
    u16  nrows;                 /* rows in pg                           */
    u16  pages;                 /* pages laid out, a GET's record one   */
    u8   fi[RRUN_NFI];          /* places of fields in the F line       */
    u8   group;                 /* the rows are grouped by location     */
    u8   ingroup;               /* a group has started: last is its     */
    char last[LINK_IDLEN + 1];  /* LOC_ID of the group being laid out   */
    char row[RPT_ROWMAX];       /* one row on its way to or from pg     */
} rrun;

/* Start run u, its filters set: the page buffer (and RPT_BLABEL's type
   list), no layout, no pages.  PR_OK, or PR_ENOMEM with nothing held. */
int rrun_open(rrun *u);

/* Open u->r, the layout of the kind on job u->p (rpt_open), with
   geometry g, the date u->date and the subtitle sub; with RPT_O_GROUP
   for RPT_VALUE and for an RPT_LIST that walks the locations,
   RPT_O_OVERDUE for RPT_LOANS of the overdue loans.  PR_OK, or the code
   of rpt_open with u->r NULL. */
int rrun_layout(rrun *u, const rpt_geom *g, const char *sub);

/* 1 when the run is of one label: RPT_ILABEL with item, RPT_BLABEL with
   loc and not below. */
int rrun_one(const rrun *u);

/* Send the requests of the run over l, laid out as they arrive, step
   (arg its first argument) called between them.  RQ_OK when every page
   arrived, the job failed or step stopped the run; else the code of the
   failed request, with res holding its response, RQ_ENOTFOUND for the
   item of a GET run or a location loc that does not exist. */
int rrun_go(rrun *u, link *l, rrun_fn step, void *arg, rq_res *res);

/* End run u, rc being what rrun_go returned: rpt_close when rc is RQ_OK,
   the run was not stopped and the job has not failed, else rpt_abort,
   so a run cut short shows no totals; then free the page buffer.  The
   records laid out, 0 when no layout was open.  The job stays open. */
u32 rrun_close(rrun *u, int rc);

#endif
