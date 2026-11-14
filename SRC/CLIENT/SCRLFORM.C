/*
 * SCRLFORM.C - the location tree's rows and the location form's table
 * (SCRLFORM.H).
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
#include "TUI.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "FORM.H"
#include "CODETAB.H"
#include "SCRLFORM.H"

#define LF_VCOL         14      /* the form's value column              */
#define LF_TNCOL        (LF_VCOL + LINK_LTYPELEN + 2)   /* type's name  */

/* A row fits LV_TEXTLEN, the deepest indent leaves room for a name, and
   the form's widest value fits a form value. */
extern char loc_row_fits[1];
extern char loc_row_fits[(SCR_LT_PICKW + 1 + LINK_LOCLEN + 1 +
                          SCR_LT_CAPW <= LV_TEXTLEN &&
                          SCR_LT_INDENT * (LINK_MAXLOCDEPTH - 1) <
                          SCR_LT_NAMEW &&
                          SCR_LF_PATHW <= FORM_MAXVAL) ? 1 : 2];

const char scr_loc_k_id[] = "LOC_ID";
static const char k_path[] = "PATH";
const char scr_loc_k_depth[] = "DEPTH";

/* The first row's value of one field (scr_loc_first). */
typedef struct {
    char *val;                  /* LINK_LTYPELEN + 1 bytes              */
    u16  at;                    /* its place in the F line, or n        */
} lc_first;

const form_field scr_loc_fields[] = {
    { "LOC_NAME",  MO_L_NAME,   0, 1, LF_VCOL, FT_CHAR, LINK_LOCNAMELEN, 0,
      FF_REQ, 0, NULL },
    { "LTYPE",     MO_L_TYPE,   1, 1, LF_VCOL, FT_CODE, LINK_LTYPELEN, 0,
      FF_REQ | FF_UPPER, 0, NULL },
    { "PARENT_ID", MO_L_INSIDE, 2, 1, LF_VCOL, FT_CODE, LINK_LOCLEN, 0,
      FF_UPPER, 0, NULL },
    { "WHERE",     MO_L_WHERE,  3, 1, LF_VCOL, FT_CHAR, SCR_LF_PATHW, 0,
      FF_LOCAL | FF_RO, 0, NULL },
    { "LOC_CODE",  MO_L_CODE,   4, 1, LF_VCOL, FT_CHAR, LINK_LOCLEN, 0,
      FF_UPPER, 0, NULL },
    { "CAPACITY",  MO_L_CAP,    5, 1, LF_VCOL, FT_NUM,  SCR_LF_CAPW, 0,
      FF_NONNEG, 0, NULL },
    { "NOTES",     MO_L_NOTES,  6, 1, LF_VCOL, FT_MEMO, FORM_MEMOW, 0, 0,
      FORM_MEMODISP, NULL },
    { "LT_NAME",   MO_L_WHERE,  1, LF_TNCOL, LF_TNCOL, FT_CHAR,
      LINK_CODENAMELEN, 0, FF_LOCAL | FF_RO, 0, NULL }
};

extern char loc_f_count[1];
extern char loc_f_count[(sizeof scr_loc_fields / sizeof scr_loc_fields[0] ==
                         SCR_LF_COUNT) ? 1 : 2];

/* Centred, the path reaching the box's right edge. */
const form_def scr_loc_def = {
    MO_NEWTITLE, "LOC", TUI_TOP + 2,
    (VID_COLS - (LF_VCOL + SCR_LF_PATHW + 2)) / 2, SCR_LF_NOTES + 3,
    LF_VCOL + SCR_LF_PATHW + 2, SCR_LF_COUNT, scr_loc_fields
};

const scr_locf scr_loc_inside = {
    &scr_loc_fields[SCR_LF_PARENT], &scr_loc_fields[SCR_LF_WHERE], MO_TOP, 0
};

const lv_src scr_loc_src = {
    "LOC LIST", 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MO_HDR, MO_HDR, MO_HDR, MO_HDR },
    MO_EMPTY, NULL, NULL, NULL, NULL, NULL
};

void scr_loc_row(const lv_rec *r, ctab *types, char *text, char *key)
{
    char tname[LINK_CODENAMELEN + 1];
    const char *s;
    u16 ind;
    u16 at;

    txt_copy(key, lv_val(r, scr_loc_k_id), LV_KEYSZ);
    s = lv_val(r, scr_loc_k_depth);
    ind = msg_dec(&s, LINK_MAXLOCDEPTH);
    if (ind > LINK_MAXLOCDEPTH)
        ind = LINK_MAXLOCDEPTH;
    ind = ind > 1 ? (u16)(SCR_LT_INDENT * (ind - 1)) : 0;
    memset(text, ' ', (size_t)ind + 1);
    at = (u16)(ind + 1);
    lv_col(text, &at, lv_val(r, scr_loc_fields[SCR_LF_NAME].name),
           (u16)(SCR_LT_NAMEW - ind), LV_MARK);
    ct_name(types, lv_val(r, scr_loc_fields[SCR_LF_TYPE].name), tname);
    lv_col(text, &at, tname, SCR_LT_TYPEW, 0);
    lv_col(text, &at, lv_val(r, scr_loc_fields[SCR_LF_CODE].name),
           LINK_LOCLEN, 0);
    lv_col(text, &at, lv_val(r, scr_loc_fields[SCR_LF_CAP].name),
           SCR_LT_CAPW, LV_RIGHT);
}

int scr_loc_info(void *arg, const rq_line *ln)
{
    scr_locinfo *i;
    const char *v;

    i = (scr_locinfo *)arg;
    if (ln->kind != 'K' || ln->n < 2)
        return 0;
    v = ln->v[1];
    if (strcmp(ln->v[0], scr_loc_k_id) == 0)
        txt_copy(i->id, v, (u16)sizeof i->id);
    else if (strcmp(ln->v[0], scr_loc_fields[SCR_LF_NAME].name) == 0)
        txt_copy(i->name, v, (u16)sizeof i->name);
    else if (strcmp(ln->v[0], scr_loc_fields[SCR_LF_CODE].name) == 0)
        txt_copy(i->code, v, (u16)sizeof i->code);
    else if (strcmp(ln->v[0], scr_loc_fields[SCR_LF_TYPE].name) == 0)
        txt_copy(i->type, v, (u16)sizeof i->type);
    else if (strcmp(ln->v[0], scr_loc_fields[SCR_LF_PARENT].name) == 0)
        txt_copy(i->parent, v, (u16)sizeof i->parent);
    else if (strcmp(ln->v[0], k_path) == 0)
        txt_copy(i->path, v, (u16)sizeof i->path);
    else if (strcmp(ln->v[0], scr_loc_k_depth) == 0)
        i->depth = msg_dec(&v, LINK_MAXLOCDEPTH + 1);
    return 0;
}

/* rq_fn of scr_loc_first: the LTYPE of the first R line. */
static int first_type(void *arg, const rq_line *ln)
{
    lc_first *x;
    u16 i;

    x = (lc_first *)arg;
    if (ln->kind == 'F') {
        for (i = 0; i < ln->n; i++)
            if (strcmp(ln->v[i], scr_loc_fields[SCR_LF_TYPE].name) == 0)
                x->at = i;
    } else if (ln->kind == 'R' && x->at < ln->n && x->val[0] == '\0') {
        txt_copy(x->val, ln->v[x->at], LINK_LTYPELEN + 1);
    }
    return 0;
}

int scr_loc_first(link *l, const char *id, char *type, rq_res *res)
{
    rq q;
    lc_first x;

    type[0] = '\0';
    x.val = type;
    x.at = 0xFFFFu;
    rq_begin(&q, scr_loc_src.cmd);
    rq_pair(&q, "UNDER", id);
    rq_pairu(&q, "PAGE", 1);
    return rq_send(l, &q, first_type, &x, res);
}
