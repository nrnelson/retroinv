/*
 * SCRCFORM.C - the form of an export and an import and the CSV requests
 * (SCRCFORM.H).
 *
 * Compile-time checks here are pairs of declarations of one array, the
 * second of size 2 when the condition fails: they then disagree, which
 * every compiler refuses.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "RQERR.H"
#include "FMODEL.H"
#include "SCRSHR.H"
#include "SCRCFORM.H"

#define CX_FORMH        5       /* rows of the form's box               */
#define CX_VCOL         14      /* value column of every field          */
#define CX_FILEW        53      /* cells the file name is shown in      */
#define CX_MAXROWS      0xFFFFu /* K ROWS= of one answer, a u16: the
                                   records one request writes           */

/* The file name fits a form value. */
extern char cx_file_fits[1];
extern char cx_file_fits[(LINK_MAXDIR <= FORM_MAXVAL) ? 1 : 2];

const char scr_csv_k_table[] = "TABLE";
const char scr_csv_k_file[] = "FILE";
const char scr_csv_k_header[] = "HEADER";
const char scr_csv_k_added[] = "ADDED";
const char scr_csv_w_export[] = "EXPORT";
const char scr_csv_w_import[] = "IMPORT";

static const char k_exptables[] = "ITEMS LOCS CATS LTYPES MAINT LOANS AUDIT";
static const char k_imptables[] = "ITEMS LOCS";
static const char k_one[] = "1";
static const char k_rows[] = "ROWS";
static const char k_errors[] = "ERRORS";
static const char k_line[] = "LINE";

/* The fields of both forms; scr_csv_form gives the import form's table
   field the import tables as its choices. */
static const form_field cx_fields[SCR_CX_NFIELDS] = {
    { scr_csv_k_table, MX_L_TABLE, 0, 1, CX_VCOL, FT_CHOICE, SCR_CX_TABLEW,
      0, FF_LOCAL | FF_REQ | FF_UPPER, 0, k_exptables },
    { scr_csv_k_file, MX_L_FILE, 1, 1, CX_VCOL, FT_CHAR, LINK_MAXDIR, 0,
      FF_LOCAL | FF_REQ | FF_UPPER, CX_FILEW, NULL },
    { scr_csv_k_header, MX_L_HEADER, 2, 1, CX_VCOL, FT_LOGIC, 1, 0,
      FF_LOCAL, 0, NULL }
};

/* The export form; scr_csv_form turns its copy into the import form. */
static const form_def cx_def = {
    MX_T_EXPORT, scr_csv_w_export, SCR_POPTOP, SCR_POPLEFT, CX_FORMH, SCR_POPW,
    SCR_CX_NFIELDS, cx_fields
};

void scr_csv_form(form_def *def, form_field *fl, int isexp)
{
    memcpy(fl, cx_fields, sizeof cx_fields);
    memcpy(def, &cx_def, sizeof *def);
    def->fields = fl;
    if (!isexp) {
        def->title = MX_T_IMPORT;
        def->topic = scr_csv_w_import;
        fl[0].choices = k_imptables;
    }
}

void scr_csv_preset(form *f)
{
    form_load(f, scr_csv_k_table, "ITEMS");
    form_load(f, scr_csv_k_header, "F");
}

void scr_csv_get(scr_csv_req *r, form *f)
{
    form_get_text(f, scr_csv_k_table, r->table, (u16)sizeof r->table);
    form_get_text(f, scr_csv_k_file, r->file, (u16)sizeof r->file);
    r->header = (u8)(form_get(f, scr_csv_k_header)[0] == 'T');
    r->replace = 0;
}

int scr_csv_send(link *l, const scr_csv_req *r, int isexp,
                 const char *after, rq_fn fn, void *arg, rq_res *res)
{
    rq q;

    rq_begin(&q, isexp ? scr_csv_w_export : scr_csv_w_import);
    rq_word(&q, r->table);
    rq_pair(&q, scr_csv_k_file, r->file);
    if (r->header)
        rq_pair(&q, scr_csv_k_header, k_one);
    if (r->replace)
        rq_pair(&q, "REPLACE", k_one);
    if (after != NULL)
        rq_pair(&q, "AFTER", after);
    return rq_send(l, &q, fn, arg, res);
}

int scr_csv_rows(void *arg, const rq_line *ln)
{
    if (ln->kind == 'K' && ln->n == 2 && strcmp(ln->v[0], k_rows) == 0)
        *(u32 *)arg += txt_num(ln->v[1], CX_MAXROWS);
    return 0;
}

int scr_csv_imp_line(void *arg, const rq_line *ln)
{
    scr_csv_imp *x;
    char t[SCR_CX_REFW + 1];
    const char *why;

    x = (scr_csv_imp *)arg;
    if (ln->kind == 'R') {
        x->refused = 1;
        x->nr++;
        if (ln->n >= 4 && x->n < LINK_IMPERRS) {
            why = strcmp(ln->v[1], k_line) == 0 ? ln->v[3] :
                  msg_sub(MX_REFFIELD, ln->v[1], ln->v[3]);
            txt_copy(t, msg_sub(MX_REFLINE, ln->v[0], why), (u16)sizeof t);
            cmem_put(x->lines, (u16)(x->n * sizeof t), t, (u16)sizeof t);
            x->n++;
        }
    } else if (ln->kind == 'F') {
        x->refused = 1;
    } else if (ln->kind == 'K' && ln->n == 2) {
        if (strcmp(ln->v[0], k_errors) == 0) {
            x->refused = 1;
            txt_copy(x->errors, ln->v[1], SCR_CX_NUMSZ);
        } else if (strcmp(ln->v[0], scr_csv_k_added) == 0) {
            x->partial = 1;
            txt_copy(x->added, ln->v[1], SCR_CX_NUMSZ);
        }
    }
    return 0;
}

void scr_csv_ferr(rq_res *res)
{
    int c;

    c = res->code;
    if (!res->closed && strcmp(res->field, scr_csv_k_file) == 0 &&
        (c == RQ_ENOTFOUND || c == RQ_EDUP ||
         (c >= RQ_EIO && c <= RQ_EINUSE))) {
        txt_copy(res->text, msg_sub(c == RQ_ENOTFOUND ? MX_NOFILE :
                                    c == RQ_EDUP ? MX_FILEDUP : MX_FILEERR,
                                    rqe_text(c), NULL),
                 (u16)sizeof res->text);
        res->code = RQ_EARG;
    }
}
