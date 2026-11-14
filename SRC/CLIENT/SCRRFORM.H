/*
 * SCRRFORM.H - the report and label forms, the opening of a run's
 * layout and SCREEN job, and the reading of the file a SCREEN run
 * writes (SCRRFORM.C), which the report screens of each client use:
 * INV's (SCRRPT.C) and the Windows client's.
 *
 * One table of local fields serves the forms of all five RPT_ kinds
 * (RPT.H), which are those of the commands SCMD_RPT_LIST to SCMD_LBL_BIN
 * (SCR.H) in the same order: a kind shows the filters of its run
 * (RPTRUN.H), then the output, PRINTER, FILE or SCREEN, and last the
 * file that FILE writes.  INV
 * draws the form in a box, the Windows client maps its fields to the
 * controls of a dialog by index.  The filters are the fields of the
 * same names (rrun_k_ of RPTRUN.H); the status ALL is sent as none, the
 * location (LOC) has its path shown below it (WHERE), BELOW (a logical
 * field) is RPTRUN.H's below, the tags (TAG) and the works state (WORKS)
 * are the search form's filters (SCRSFORM.H), the tags with a pick list
 * on F4, and an item label's item is ITEM_ID.  A run's subtitle names its
 * filters, each after its label, the works state by its name.
 *
 * SCREEN output goes into a scratch file of the current directory,
 * named INVR, four hex digits of a checksum of the station name and
 * the extension $$$, so that stations sharing a directory on a network
 * drive each write their own.  It is created only when it is not there
 * yet (PR_NEWFILE); when it is, as for two stations whose checksums are
 * equal, the next of SCR_RP_SCRTRY names is tried, the checksum XORed
 * with 1, 2 and so on, so two stations never share one.
 *
 * The viewer of a SCREEN run reads the file through a scr_rview: the
 * offsets of its lines, up to SCR_RV_MAXLINES, in a far block of that
 * size, found in one pass over the file (when memory is short half as
 * many at a time, down to SCR_RV_MINLINES, the first lines only); then
 * the lines a window shows, read in one stream from the first of them.
 * A line starts at the beginning of the file and after each LF, except
 * at its end, and an FF is a line of its own, a page end.  The texts
 * are messages of SCRRPT.MSG.
 */
#ifndef SCRRFORM_H
#define SCRRFORM_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "FMODEL.H"
#include "TUI.H"
#include "PRINT.H"
#include "RPT.H"
#include "RPTRUN.H"
#include "CODETAB.H"
#include "SCRLFORM.H"

/* The forms' titles but those of RPT.H, their labels, also the words
   of the subtitle, and the texts both clients give for a run. */
#define MQ_T_LOANS      1002    /* form titles (MR_T_LIST, MR_T_VALUE)  */
#define MQ_T_ILABEL     1003
#define MQ_T_BLABEL     1004
#define MQ_L_ORDER      1007
#define MQ_L_CAT        1008
#define MQ_L_LOC        1009
#define MQ_L_STATUS     1010
#define MQ_L_OUT        1011
#define MQ_L_FILE       1012
#define MQ_L_OVERDUE    1013
#define MQ_L_ASOF       1014
#define MQ_L_ITEM       1015
#define MQ_L_BELOW      1016
#define MQ_L_WHERE      1019
#define MQ_L_TAG        1030
#define MQ_L_WORKS      1031
#define MQ_C_WQ         1032    /* the Works filter's choices' names,
                                   in the order of scr_works_filt       */
#define MQ_C_WY         1033
#define MQ_C_WI         1034
#define MQ_C_WN         1035
#define MQ_C_WK         1036
#define MQ_ANDBELOW     1000    /* ^1 and below (the subtitle)          */
#define MQ_PROGRESS     1017    /* ^1 pages asked for, ^2 records       */
#define MQ_NEEDFILE     1018    /* FILE output without a file name      */
#define MQ_CANCELLED    1023
#define MQ_STOPPED      1024
#define MQ_FILEERR      1025    /* ^1 the file, ^2 the DOS error        */
#define MQ_SCRATCH      1026    /* ^1 the scratch file                  */
#define MQ_DONE         1027    /* ^1 records                           */
#define MQ_NOTFOUND     1028    /* ITEM GET or LOC GET answered 200     */
#define MQ_VSHOWN       1029    /* ^1 the lines shown                   */
#define MQ_BADFILE      1037    /* a name FILE output may not write     */

#define SCR_RP_MAXF     10              /* fields of the largest form   */
#define SCR_RP_WHEREW   33              /* characters of a location's
                                           path in the form             */
#define SCR_RP_FILEW    LINK_MAXDIR     /* characters of a file name    */
#define SCR_RP_SCRSZ    13              /* "INVRhhhh.$$$" and the NUL   */
#define SCR_RP_SCRTRY   16              /* scratch names tried: the
                                           checksum XOR 0..15 changes
                                           only its last hex digit      */

/* The fields of the output, of its file and of the location's path. */
extern const char scr_rpt_k_out[];
extern const char scr_rpt_k_file[];
extern const char scr_rpt_k_where[];

/* The location field of the forms (a location field of SCRLFORM.H):
   scr_rpt_loc a filter of the report kinds, where blank means every
   location and "0" none; scr_rpt_bloc the first bin label's location,
   blank for every one. */
extern const scr_locf scr_rpt_loc;
extern const scr_locf scr_rpt_bloc;

/* REPORTS and LABELS, the help topics of the report and label forms. */
extern const char scr_rpt_k_reports[];
extern const char scr_rpt_k_labels[];

/* The form of kind (RPT_) into *def, its fields into fields
   (SCR_RP_MAXF): titled by the kind, in INV's pop-up box of SCR_POPW
   columns, as high as its rows need, at SCR_POPTOP, SCR_POPLEFT
   (SCRSHR.H), with the help topic REPORTS, or LABELS for the labels. */
void scr_rpt_form(form_def *def, form_field *fields, u8 kind);

/* The first values of a form of scr_rpt_form: the location order, the
   status ALL, the output SCREEN, not only the overdue loans. */
void scr_rpt_defaults(form *f);

/* The filters of f, a form of u->kind, into u, and its file into file
   (SCR_RP_FILEW + 1 bytes); u->date stays "" when no date is typed. */
void scr_rpt_values(rrun *u, form *f, char *file);

/* What the file name of FILE output, the form's file, gets: MQ_NEEDFILE
   when it is "", MQ_BADFILE when txt_outfile_ok refuses it (a data or
   scratch file, a device, a name DOS refuses), else 0. */
u16 scr_rpt_file_bad(const char *file);

/* Open the layout of the run (rrun_layout) with the geometry g and the
   filters as the subtitle, its location named by path (its full path;
   "" for none), cut from the left to the room the other filters leave:
   rrun_layout's code. */
int scr_rpt_layout(rrun *u, const rpt_geom *g, const char *path);

/* Open u->p, the job of SCREEN output, in the first scratch name of the
   station's that is not taken, which goes into scratch (SCR_RP_SCRSZ
   bytes): pr_open's code and *cferr. */
int scr_rpt_screen(rrun *u, char *scratch, const char *station,
                   int *cferr);

/* The failure res of a run that rrun_go ended with rc: a 200 becomes a
   102 for the field that named what was not found, its text
   MQ_NOTFOUND: ITEM_ID for the item of one label (rrun_one), else LOC
   when it names a location; any other stays as it is. */
void scr_rpt_failed(const rrun *u, int rc, rq_res *res);

/* ---------------------------------------------------------------------
 * The viewer's file.
 * --------------------------------------------------------------------- */

/* Line offsets (u32) in one far block of at most SCR_RV_MAXLINES and,
   when memory is short, at least SCR_RV_MINLINES of them (a work
   area's height of INV's); the bytes read from the file at a time; the
   lines a viewer shows at once, INV's work area below a title row. */
#define SCR_RV_MAXLINES 8000
#define SCR_RV_MINLINES TUI_WORKROWS
#define SCR_RV_CHUNK    128
#define SCR_RV_ROWS     (TUI_WORKROWS - 1)

/* scr_rv_line's kinds of line. */
#define SCR_RV_END      0       /* past the lines held: none            */
#define SCR_RV_TEXT     1       /* a line of text                       */
#define SCR_RV_RULE     2       /* a page end                           */

/* An open file of a SCREEN run.  n and cut are the caller's to read. */
typedef struct {
    int  h;                     /* the file                             */
    cmem_blk *b;                /* the offsets of its lines             */
    u16  n;                     /* lines held                           */
    u16  next;                  /* the line the file is read at         */
    u16  at;                    /* next byte of buf                     */
    u16  got;                   /* bytes in buf                         */
    u8   cut;                   /* 1 when the file has more lines       */
    char buf[SCR_RV_CHUNK];
} scr_rview;

/* Open the file path into v, lines being the count of its lines
   (pr_lines of the job that wrote it): the offsets of all of them, up
   to SCR_RV_MAXLINES, or when memory is short of the first.  0, a CF_E
   code of the open or read, or CF_ENOMEM when not even SCR_RV_MINLINES
   (or all the lines, when fewer) fit; nothing stays open then.
   scr_rv_close closes what it opened. */
int scr_rv_open(scr_rview *v, const char *path, u32 lines);

/* Line i, its characters before any CR, at most RPT_WIDTH of them, into
   line (RPT_WIDTH + 1 bytes): a page end as a rule of RPT_WIDTH TF_SH
   (TUI.H), "" past the lines held and when the file cannot be read.  Its
   kind, SCR_RV_.  The lines that follow line i are read on from it,
   others from their offset. */
int scr_rv_line(scr_rview *v, u16 i, char *line);

/* Close the file and free the offsets. */
void scr_rv_close(scr_rview *v);

#endif
