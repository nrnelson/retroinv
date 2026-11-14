/*
 * SCRNFORM.C - the forms and list rows of loans and holds (SCRNFORM.H).
 *
 * Field names are taken from the table wherever a row needs them, so
 * each is kept once.  Compile-time checks here are pairs of
 * declarations of one array, the second of size 2 when the condition
 * fails: they then disagree, which every compiler refuses.
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
#include "SCRSHR.H"
#include "ITEMLS.H"
#include "SCRNFORM.H"

/* The notes and the host's ID or text fit a form value. */
extern char loan_fits_form[1];
extern char loan_fits_form[(LOAN_NOTESW <= FORM_MAXVAL &&
                            LINK_MAXFIND <= FORM_MAXVAL) ? 1 : 2];
/* The rows of every item and those of one item fit a row's text. */
extern char loan_row_fits[1];
extern char loan_row_fits[(LC_ITEMAT + LC_ID + 1 + LC_QTY + 1 + LC_HOLDER +
                           1 + MSG_DATEW + 1 + LC_NAME <= LV_TEXTLEN &&
                           LC_ITEMAT + LC_QTY + 1 + LOAN_NAMEW + 1 +
                           MSG_DATEW + 1 + MSG_DATEW <= LV_TEXTLEN) ?
                          1 : 2];

/* The form's box, BF_H rows high for a kind with a host, a row fewer for
   a loan, which has none on row BF_HOSTROW; its three columns of label
   (BF_Cn) and value (BF_Vn), the first label wider than SCRSHR.H's
   grid gives, for "Reserved for"; WHAT from BF_V1 to the box's edge,
   HOSTWHAT from BF_HWC, after the host's ID or text; the notes show
   BF_NOTESDISP cells. */
#define BF_H            9
#define BF_HOSTROW      3
#define BF_C1           1
#define BF_V1           15
#define BF_C2           27
#define BF_V2           37
#define BF_C3           49
#define BF_V3           59
#define BF_IN           (SCR_POPW - 2)  /* cells inside the box         */
#define BF_WHATW        (BF_IN - BF_V1)
#define BF_HWC          (BF_V1 + LINK_MAXFIND + 1)
#define BF_HWHATW       (BF_IN - BF_HWC)
#define BF_NOTESDISP    (BF_IN - BF_V1 - 2)
/* The dates of a row fit their columns and the box, and the host's
   maker and model have cells after its ID or text. */
extern char loan_fits_box[1];
extern char loan_fits_box[(BF_V1 + FORM_DATEW < BF_C2 &&
                           BF_V2 + FORM_DATEW < BF_C3 &&
                           BF_V3 + FORM_DATEW <= BF_IN &&
                           BF_HWHATW > 0) ? 1 : 2];

/* Indexes into scr_loan_fields beside SCRNFORM.H's. */
#define F_HOST          5
#define F_HWHAT         6
#define F_OUT           8

const char scr_loan_w_loan[] = "LOAN";
const char scr_loan_k_id[] = "LOAN_ID";
const char scr_loan_k_item[] = "ITEM";
const char scr_loan_k_host[] = "HOST_ID";
const char scr_loan_k_what[] = "WHAT";
const char scr_loan_k_hwhat[] = "HOSTWHAT";
const char scr_loan_w_list[] = "LOAN LIST";
const char scr_loan_w_over[] = "LOAN OVERDUE";
const char scr_loan_k_kind[] = "KIND";
static const char k_one[] = "1";
static const char k_zero[] = "0";

/* Every kind's fields, a loan's labels. */
const form_field scr_loan_fields[SCR_NF_COUNT] = {
    { scr_loan_k_id, MH_L_ID,     0, BF_C1, BF_V1, FT_NUM, LINK_IDLEN, 0,
      FF_RO | FF_ID, 0, NULL },
    { itl_k_itemid, MH_L_ITEM,    0, BF_C2, BF_V2, FT_NUM, LINK_IDLEN, 0,
      FF_KEY | FF_RO | FF_ID, 0, NULL },
    { itl_k_qty,  MH_L_QTY,       0, BF_C3, BF_V3, FT_NUM, LOAN_QTYW, 0,
      FF_NONNEG | FF_NEWONLY, 0, NULL },
    { scr_loan_k_what, MH_L_NONE, 1, BF_V1, BF_V1, FT_CHAR, BF_WHATW, 0,
      FF_LOCAL | FF_RO, 0, NULL },
    { "HOLDER",   MH_L_HOLDER,    2, BF_C1, BF_V1, FT_CHAR, LOAN_NAMEW, 0,
      FF_REQ, 0, NULL },
    { scr_loan_k_host, MH_L_HOST, BF_HOSTROW, BF_C1, BF_V1, FT_CODE,
      LINK_MAXFIND, 0, 0, 0, NULL },
    { scr_loan_k_hwhat, MH_L_NONE, BF_HOSTROW, BF_HWC, BF_HWC, FT_CHAR,
      BF_HWHATW, 0, FF_LOCAL | FF_RO, 0, NULL },
    { "CONTACT",  MH_L_CONTACT,   4, BF_C1, BF_V1, FT_CHAR, LOAN_NAMEW, 0,
      0, 0, NULL },
    { "OUT_DATE", MH_L_OUT,       5, BF_C1, BF_V1, FT_DATE, FORM_DATEW, 0,
      FF_NEWONLY, 0, NULL },
    { "DUE_DATE", MH_L_DUE,       5, BF_C2, BF_V2, FT_DATE, FORM_DATEW, 0,
      FF_REQ, 0, NULL },
    { "RET_DATE", MH_L_RET,       5, BF_C3, BF_V3, FT_DATE, FORM_DATEW, 0,
      FF_RO, 0, NULL },
    { "NOTES",    MH_L_NOTES,     6, BF_C1, BF_V1, FT_CHAR, LOAN_NOTESW, 0,
      0, BF_NOTESDISP, NULL }
};

static const form_def loan_def = {
    MH_T_NEW, scr_loan_w_loan, SCR_POPTOP, SCR_POPLEFT, BF_H, SCR_POPW,
    SCR_NF_COUNT, scr_loan_fields
};

/* The kinds' command words of a new row, by SCR_K_. */
static const char *const loan_adds[SCR_NKINDS] = { "OUT", "RESERVE", "USE" };

u8 scr_loan_kind(const char *v)
{
    const char *p;

    p = v[0] != '\0' ? strchr(itl_whys, v[0]) : NULL;
    return (u8)(p != NULL && p < itl_whys + SCR_NKINDS ? p - itl_whys :
                SCR_K_LOAN);
}

void scr_loan_form(u8 kind, form_def *def, form_field *fields)
{
    form_field *d;
    u16 i;
    u16 n;
    int loan;

    loan = kind == SCR_K_LOAN;
    n = 0;
    for (i = 0; i < SCR_NF_COUNT; i++) {
        if (loan ? i == F_HOST || i == F_HWHAT :
                   kind == SCR_K_USE && i == SCR_NF_DUE)
            continue;
        d = &fields[n++];
        memcpy(d, &scr_loan_fields[i], sizeof *d);
        if (loan && d->row > BF_HOSTROW)
            d->row--;
        if (i == SCR_NF_HOLDER || i == F_OUT || i == SCR_NF_DUE)
            d->label = (u16)(d->label + kind);
        else if (i == F_HOST)
            d->label = (u16)(d->label + kind - SCR_K_RES);
        else if (i == SCR_NF_RET && !loan)
            d->label = MH_L_REL;
        if (!loan)
            d->flags &= (u8)~FF_REQ;
    }
    memcpy(def, &loan_def, sizeof *def);
    def->title = (u16)(MH_T_NEW + kind);
    def->h = (u8)(BF_H - loan);
    def->nfields = (u8)n;
    def->fields = fields;
}

void scr_loan_new(form *f, u8 kind)
{
    form_load(f, itl_k_qty, k_one);
    form_set_add(f, loan_adds[kind]);
}

void scr_loan_loaded(form *f)
{
    const char *v;

    v = form_get(f, scr_loan_k_host);
    if (v != NULL && strcmp(v, k_zero) == 0)
        form_load(f, scr_loan_k_host, "");
}

/* The work of scr_loan_kitem and scr_loan_khost: ITEM GET's MFR or
   MODEL into field of f. */
static int name_line(form *f, const char *field, const rq_line *ln)
{
    char v[BF_WHATW + 1];
    u16 n;

    if (ln->kind != 'K' || ln->n < 2)
        return 0;
    if (strcmp(ln->v[0], itl_k_mfr) == 0) {
        form_load(f, field, ln->v[1]);
    } else if (strcmp(ln->v[0], itl_k_model) == 0) {
        form_get_text(f, field, v, (u16)sizeof v);
        n = (u16)strlen(v);
        if (n > 0 && ln->v[1][0] != '\0' && n + 1 < (u16)sizeof v)
            v[n++] = ' ';
        txt_copy(v + n, ln->v[1], (u16)(sizeof v - n));
        form_load(f, field, v);
    }
    return 0;
}

int scr_loan_kitem(void *arg, const rq_line *ln)
{
    return name_line((form *)arg, scr_loan_k_what, ln);
}

int scr_loan_khost(void *arg, const rq_line *ln)
{
    return name_line((form *)arg, scr_loan_k_hwhat, ln);
}

/* ---------------------------------------------------------------------
 * The lists.
 * --------------------------------------------------------------------- */

const lv_src scr_loan_src = {
    scr_loan_w_list, 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MH_HDR, MH_HDR, MH_HDR, MH_HDR },
    MH_EMPTY, NULL, scr_loan_args, scr_loan_fmt, NULL, scr_loan_mark
};

void scr_loan_view(lv_src *src, scr_lq *q, u8 v)
{
    memset(q, 0, sizeof *q);
    q->kind = (u8)(v >= SCR_LV_KIND && v < SCR_LV_OVER ? v - SCR_LV_KIND :
                   SCR_NKINDS);
    q->open = (u8)(v < SCR_LV_OVER);
    q->ended = (u8)(v == SCR_LV_ALL);
    src->header[0] = (u16)(q->ended ? MH_HDREND : MH_HDR);
    src->cmd = v == SCR_LV_OVER ? scr_loan_w_over : scr_loan_w_list;
    src->args = v == SCR_LV_OVER ? NULL : scr_loan_args;
    src->empty = (u16)(v == SCR_LV_OVER ? MH_EMPTYOVER : MH_EMPTY);
    src->arg = q;
}

void scr_loan_args(void *arg, rq *q)
{
    const scr_lq *s;
    char k[2];

    s = (const scr_lq *)arg;
    if (s->item[0] != '\0')
        rq_pair(q, scr_loan_k_item, s->item);
    if (s->host[0] != '\0')
        rq_pair(q, "HOST", s->host);
    if (s->kind < SCR_NKINDS) {
        k[0] = itl_whys[s->kind];
        k[1] = '\0';
        rq_pair(q, scr_loan_k_kind, k);
    }
    if (s->open)
        rq_pair(q, "OPEN", k_one);
}

/* arg is never NULL: the test keeps the parameter, which lv_mark_fn
   requires, referenced. */
int scr_loan_mark(void *arg, const lv_rec *r)
{
    return arg != NULL && strcmp(lv_val(r, "LATE"), k_one) == 0;
}

/* Value v of a row with its leading blanks left out. */
static const char *trimmed(const char *v)
{
    while (*v == ' ')
        v++;
    return v;
}

void scr_loan_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    char t[2 * LINK_MFRLEN + 2];
    const scr_lq *q;
    const char *id;
    const char *v;
    u16 at;
    u16 n;
    u8 kind;
    int every;

    q = (const scr_lq *)arg;
    every = q->item[0] == '\0';
    id = trimmed(lv_val(r, scr_loan_k_id));
    txt_copy(key, id, LV_KEYSZ);
    kind = scr_loan_kind(lv_val(r, scr_loan_k_kind));
    at = 0;
    lv_col(text, &at, scr_loan_mark(arg, r) ? "!" : "", 1, 0);
    lv_col(text, &at, id, LC_ID, LV_RIGHT);
    lv_col(text, &at, msg((u16)(MH_K_NAME + kind)), LC_KIND, 0);
    if (every)
        lv_col(text, &at, lv_val(r, itl_k_itemid), LC_ID, LV_RIGHT);
    lv_col(text, &at, lv_val(r, itl_k_qty), LC_QTY, LV_RIGHT);
    v = lv_val(r, scr_loan_fields[SCR_NF_HOLDER].name);
    id = trimmed(lv_val(r, scr_loan_k_host));
    if (v[0] == '\0' && q->host[0] == '\0') {
        n = msg_get((u16)(kind == SCR_K_USE ? MH_L_HOSTUSE : MH_L_HOST), t,
                    (u16)(sizeof t - LINK_IDLEN - 1));
        t[n++] = ' ';
        txt_copy(t + n, id, (u16)(sizeof t - n));
        v = t;
    }
    lv_col(text, &at, v, (u16)(!every ? LOAN_NAMEW :
                                q->ended ? LC_HOLDEND : LC_HOLDER), LV_MARK);
    lv_datecol(text, &at, lv_val(r, scr_loan_fields[SCR_NF_DUE].name));
    if (!every || q->ended) {
        lv_datecol(text, &at, lv_val(r, scr_loan_fields[SCR_NF_RET].name));
        return;
    }
    txt_copy(t, lv_val(r, itl_k_mfr), (u16)sizeof t);
    n = (u16)strlen(t);
    v = lv_val(r, itl_k_model);
    if (n > 0 && v[0] != '\0')
        t[n++] = ' ';
    txt_copy(t + n, v, (u16)(sizeof t - n));
    lv_col(text, &at, t, LC_NAME, LV_MARK);
}

u8 scr_loan_rowkind(const char *text)
{
    const char *m;
    u16 n;
    u8 k;

    for (k = 0; k < SCR_NKINDS; k++) {
        m = msg((u16)(MH_K_NAME + k));
        n = (u16)strlen(m);
        if (n > LC_KIND)
            n = LC_KIND;
        if (n > 0 && strncmp(text + LC_KINDAT, m, n) == 0 &&
            (n == LC_KIND || text[LC_KINDAT + n] == ' '))
            return k;
    }
    return SCR_K_LOAN;
}

int scr_loan_rowitem(const char *text, char *id)
{
    u16 i;
    u16 n;
    char c;

    n = 0;
    for (i = LC_ITEMAT; i < LC_ITEMAT + LC_ID; i++) {
        c = text[i];
        if (TXT_ISDIGIT(c))
            id[n++] = c;
    }
    id[n] = '\0';
    return n > 0;
}
