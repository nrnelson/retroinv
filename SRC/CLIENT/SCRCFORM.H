/*
 * SCRCFORM.H - the form of an export and an import and the CSV requests
 * (SCRCFORM.C), which the export and import of each client run: INV's
 * (SCRCSV.C) and the Windows client's.
 *
 * One form of local fields serves both commands: the table (a choice, of
 * the exportable tables for an export and of ITEMS and LOCS for an
 * import), the file, and whether the file's first line holds the field
 * names.  The field names are those the server names in a refusal
 * (TABLE, FILE, HEADER), so that a client shows a 102 on its field.  The
 * file is one on the machine that runs the engine: the server, or the
 * client's own in local mode.  The values are not a record.  Both
 * commands need level 3.
 *
 * Export.  scr_csv_send sends "EXPORT table FILE=file [HEADER=1]
 * [REPLACE=1] [AFTER=cursor]".  Each answer gives the records it wrote
 * (K ROWS=, which scr_csv_rows adds up) and, while records remain, a C
 * line, after which the client sends the request again with AFTER= that
 * cursor and the same file and flags.  The server writes the export under
 * a scratch name and gives the file its name only in the last answer, so
 * an export stopped between requests leaves the file as it was.  A 201
 * naming FILE in the first answer means that the file exists, and
 * REPLACE=1 on every request of a new start replaces it; a 204 in a later
 * answer, that the server's scratch file has gone.
 *
 * Import.  One "IMPORT table FILE=file [HEADER=1]", which adds every line
 * or none: +OK ADDED=n; or +DATA with R lines for the refused lines (as
 * many as the server lists, LINK_IMPERRS at most) and K ERRORS= counting
 * all of them; or +DATA with K ADDED= and K ERROR=, adding having failed
 * after that many records.  scr_csv_imp_line keeps what such an answer
 * says in an scr_csv_imp.
 *
 * A file error naming FILE (200, 201 in a later answer, 300..304) is told
 * in the client's text for its code, on the file's field (scr_csv_ferr).
 * The texts are messages of SCRCSV.MSG.
 */
#ifndef SCRCFORM_H
#define SCRCFORM_H

#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "FMODEL.H"

/* The forms' titles and labels, the status line's texts, and the
   outcomes of an export and an import. */
#define MX_T_EXPORT     1040    /* form titles                          */
#define MX_T_IMPORT     1041
#define MX_L_TABLE      1042    /* field labels                         */
#define MX_L_FILE       1043
#define MX_L_HEADER     1044
#define MX_PROGRESS     1047    /* status info: ^1 records written      */
#define MX_STOPPED      1049    /* Esc between requests: ^1 the file    */
#define MX_EXPORTED     1050    /* ^1 records written to file ^2        */
#define MX_LOST         1051    /* 204 during an export                 */
#define MX_IMPORTQ      1052    /* add the records of ^1 to ^2?         */
#define MX_WAIT         1053    /* the import is running                */
#define MX_ADDED        1054    /* ^1 records added to ^2               */
#define MX_T_REFUSED    1055    /* title of the refused lines           */
#define MX_REFLINE      1056    /* Line ^1  ^2                          */
#define MX_REFFIELD     1057    /* ^1: ^2 (field and reason)            */
#define MX_REFCOUNT     1058    /* ^1 lines refused in all              */
#define MX_REFERR       1059    /* ^1 the error that stopped reading    */
#define MX_PARTIAL      1060    /* ^1 records added, ^2 the error       */
#define MX_NOFILE       1062    /* texts of a file error on FILE: 200,  */
#define MX_FILEDUP      1063    /* 201, and 300..304 (^1 rqe_text)      */
#define MX_FILEERR      1064

#define SCR_CX_NFIELDS  3       /* fields of the form                   */
#define SCR_CX_TABLEW   6       /* characters of the longest table name */
/* Characters of a refused line as kept, those INV's box of them shows
   inside its frame and margins. */
#define SCR_CX_REFW     (VID_COLS - 10)
#define SCR_CX_NUMSZ    (TXT_U32LEN + 1)    /* a u32 in decimal and NUL */

/* The field names, which are also the keys of the requests; ADDED, the
   key of an import's +OK ADDED= and K ADDED=. */
extern const char scr_csv_k_table[];
extern const char scr_csv_k_file[];
extern const char scr_csv_k_header[];
extern const char scr_csv_k_added[];

/* EXPORT and IMPORT, the commands and their forms' help topics. */
extern const char scr_csv_w_export[];
extern const char scr_csv_w_import[];

/* What one export or import sends. */
typedef struct {
    char table[SCR_CX_TABLEW + 1];
    char file[LINK_MAXDIR + 1];
    u8   header;                /* the first line holds the field names */
    u8   replace;               /* export: an existing file is replaced */
} scr_csv_req;

/* What an import's answer said beyond its code (scr_csv_imp_line). */
typedef struct {
    cmem_blk *lines;            /* the kept lines, SCR_CX_REFW + 1 bytes
                                   each, LINK_IMPERRS at most: the
                                   caller's far block                   */
    u16  n;                     /* refused lines kept                   */
    u16  nr;                    /* R lines received                     */
    u8   refused;               /* lines were refused: nothing added    */
    u8   partial;               /* K ADDED= came: adding failed         */
    char errors[SCR_CX_NUMSZ];  /* K ERRORS=: every refused line        */
    char added[SCR_CX_NUMSZ];   /* K ADDED=                             */
} scr_csv_imp;

/* The form of an export (isexp 1) or an import into def, its fields
   copied into fl (SCR_CX_NFIELDS of them): titled MX_T_EXPORT or
   MX_T_IMPORT, with the help topic EXPORT or IMPORT, in a box over a
   list (SCRSHR.H's SCR_POP). */
void scr_csv_form(form_def *def, form_field *fl, int isexp);

/* The first values of a form of scr_csv_form: the table ITEMS, no line
   of field names. */
void scr_csv_preset(form *f);

/* The values of f, a form of scr_csv_form, into r, which replaces
   nothing. */
void scr_csv_get(scr_csv_req *r, form *f);

/* Send one EXPORT (isexp 1) or IMPORT for r over l, with AFTER=after
   when after is not NULL, its data lines to fn with arg (rq_send):
   RQ_OK, or the failure's code and res. */
int scr_csv_send(link *l, const scr_csv_req *r, int isexp,
                 const char *after, rq_fn fn, void *arg, rq_res *res);

/* An export's rq_fn: K ROWS= of its answer added to the u32 at arg. */
int scr_csv_rows(void *arg, const rq_line *ln);

/* An import's rq_fn: one line of its answer into the scr_csv_imp at arg,
   zeroed but for its lines before the request.  A refused line is kept
   as MX_REFLINE with the line's number and the server's reason, after
   MX_REFFIELD's field name when it names one, cut to SCR_CX_REFW
   characters. */
int scr_csv_imp_line(void *arg, const rq_line *ln);

/* Give the failure res, when it is a file error naming FILE (200, 201,
   300..304) of a session still open, the client's text for its code
   and the code 102, so that a client shows it on the file's field as it
   shows the server's other refusals; any other failure stays as it is. */
void scr_csv_ferr(rq_res *res);

#endif
