/*
 * SCRSFORM.C - the item search's form table and its searches
 * (SCRSFORM.H).
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
#include "FMODEL.H"
#include "FORM.H"
#include "SCR.H"
#include "CODETAB.H"
#include "SCRLFORM.H"
#include "ITEMLS.H"
#include "SCRIFORM.H"
#include "SCRSFORM.H"

#define SF_NWRAP        2       /* fields repeated to close the cycle   */

/* The form's height, and the label and value columns of its fields:
   each is in the first column but the location (second), the status
   (third) and available (fourth), which share the category's row, the
   location's path and "all below", below the location, and the tags and
   the works state, in the third column of the text's and the serial
   number's rows. */
#define SF_H            7
#define SF_C1           1
#define SF_V1           11
#define SF_C2           22
#define SF_V2           32
#define SF_C3           44
#define SF_V3           52
#define SF_C4           58
#define SF_V4           68
#define SF_PATHW        (VID_COLS - 2 - SF_V2)

/* Both search texts fit a form value, each command starts on its field
   of the first turn, with a whole turn after it, and the results'
   titles follow the order of the searches (the build fails
   otherwise). */
extern char sf_fits_form[1];
extern char sf_fits_form[(LINK_SERIALLEN <= FORM_MAXVAL &&
                          LINK_MAXFIND <= FORM_MAXVAL &&
                          SF_V2 + LINK_LOCLEN < SF_C3 &&
                          SF_V2 + SF_PATHW <= VID_COLS - 2 &&
                          SF_V1 + LINK_MAXFIND < SF_C3 &&
                          SF_V1 + LINK_SERIALLEN < SF_C3 &&
                          SF_V3 + LINK_TAGFILT <= VID_COLS - 2 &&
                          SCR_WORKSW <= LINK_NWORKS) ? 1 : 2];
extern char sf_names_reach[1];
extern char sf_names_reach[(MS_C_A > MS_L_STATUS &&
                            MS_C_A - MS_L_STATUS <= 0xFF &&
                            MS_C_WQ > MS_L_WORKS &&
                            MS_C_WQ - MS_L_WORKS <= 0xFF) ? 1 : 2];
extern char sf_starts_fit[1];
extern char sf_starts_fit[(SCMD_SERIAL - SCMD_FIND == SCR_SF_SERIAL &&
                           SCMD_FILTER - SCMD_FIND == SCR_SF_CAT &&
                           SCR_SF_CAT <= SF_NWRAP &&
                           MS_T_SERIAL - MS_T_FIND == SCR_SQ_SERIAL &&
                           MS_T_FILTER - MS_T_FIND == SCR_SQ_FILTER) ?
                          1 : 2];

static const char k_text[] = "TEXT";
static const char k_serial[] = "SERIAL";
static const char k_cat[] = "CAT";
static const char k_loc[] = "LOC";
static const char k_status[] = "STATUS";
static const char k_avail[] = "AVAIL";
static const char k_all[] = "ALL";
static const char k_tag[] = "TAG";

const char scr_works_filt[] = "? Y I N Y?I";

const form_field scr_search_fields[SCR_SF_COUNT + SF_NWRAP] = {
    { k_text,    MS_L_TEXT,   0, SF_C1, SF_V1, FT_CHAR, LINK_MAXFIND, 0,
      FF_LOCAL, 0, NULL },
    { k_serial,  MS_L_SERIAL, 1, SF_C1, SF_V1, FT_CHAR, LINK_SERIALLEN, 0,
      FF_LOCAL, 0, NULL },
    { k_cat,     MS_L_CAT,    2, SF_C1, SF_V1, FT_CODE, LINK_CATLEN, 0,
      FF_LOCAL | FF_UPPER, 0, NULL },
    { k_loc,     MS_L_LOC,    2, SF_C2, SF_V2, FT_CODE, LINK_LOCLEN, 0,
      FF_LOCAL | FF_UPPER, 0, NULL },
    { "WHERE",   MS_L_WHERE,  3, SF_C2, SF_V2, FT_CHAR, SF_PATHW, 0,
      FF_LOCAL | FF_RO, 0, NULL },
    { k_all,     MS_L_ALL,    4, SF_C2, SF_V2, FT_LOGIC, 1, 0, FF_LOCAL, 0,
      NULL },
    { k_status,  MS_L_STATUS, 2, SF_C3, SF_V3, FT_CHOICE, LINK_STATUSLEN,
      MS_C_A - MS_L_STATUS, FF_LOCAL | FF_UPPER, 0, "A D" },
    { k_avail,   MS_L_AVAIL,  2, SF_C4, SF_V4, FT_LOGIC, 1, 0, FF_LOCAL, 0,
      NULL },
    { k_tag,     MS_L_TAG,    0, SF_C3, SF_V3, FT_CODE, LINK_TAGFILT, 0,
      FF_LOCAL, 0, NULL },
    { itl_k_works, MS_L_WORKS, 1, SF_C3, SF_V3, FT_CHOICE, SCR_WORKSW,
      MS_C_WQ - MS_L_WORKS, FF_LOCAL | FF_UPPER, 0, scr_works_filt },
    { k_text,    MS_L_TEXT,   0, SF_C1, SF_V1, FT_CHAR, LINK_MAXFIND, 0,
      FF_LOCAL, 0, NULL },
    { k_serial,  MS_L_SERIAL, 1, SF_C1, SF_V1, FT_CHAR, LINK_SERIALLEN, 0,
      FF_LOCAL, 0, NULL }
};

const form_def scr_search_def = {
    MS_TITLE, "SEARCH", TUI_TOP, 0, SF_H, VID_COLS, SCR_SF_COUNT,
    scr_search_fields
};

const scr_locf scr_search_loc = {
    &scr_search_fields[SCR_SF_LOC], &scr_search_fields[SCR_SF_WHERE],
    MO_NOLOC, 1
};

const char *const scr_search_cmds[SCR_SQ_NONE] = {
    "ITEM FIND", "ITEM SERIAL", "ITEM LIST"
};

void scr_search_form(form_def *def, u8 cmd)
{
    memcpy(def, &scr_search_def, sizeof *def);
    def->fields = &scr_search_fields[cmd - SCMD_FIND];
}

/* 1 when q has a filter. */
static int filtered(const scr_query *q)
{
    return q->cat[0] != '\0' || q->loc[0] != '\0' || q->status[0] != '\0' ||
           q->avail || q->tag[0] != '\0' || q->works[0] != '\0';
}

u8 scr_search_get(scr_query *q, form *f)
{
    form_get_text(f, k_text, q->text, (u16)sizeof q->text);
    form_get_text(f, k_serial, q->serial, (u16)sizeof q->serial);
    form_get_text(f, k_cat, q->cat, (u16)sizeof q->cat);
    form_get_text(f, k_loc, q->loc, (u16)sizeof q->loc);
    form_get_text(f, k_status, q->status, (u16)sizeof q->status);
    form_get_text(f, k_tag, q->tag, (u16)sizeof q->tag);
    ct_tags_bare(q->tag);
    form_get_text(f, itl_k_works, q->works, (u16)sizeof q->works);
    q->avail = (u8)(form_get(f, k_avail)[0] == 'T');
    q->below = (u8)(form_get(f, k_all)[0] == 'T');
    q->kind = (u8)(q->text[0] != '\0' ? SCR_SQ_FIND :
                   q->serial[0] != '\0' ? SCR_SQ_SERIAL :
                   filtered(q) ? SCR_SQ_FILTER : SCR_SQ_NONE);
    if (q->kind != SCR_SQ_SERIAL && q->status[0] != '\0' && q->avail)
        q->kind = SCR_SQ_NONE;
    return q->kind;
}

int scr_search_item(scr_query *q, const char *typed, u8 only)
{
    u16 n;
    u16 i;

    memset(q, 0, sizeof *q);
    while (*typed == ' ')
        typed++;
    txt_copy(q->text, typed, (u16)sizeof q->text);
    for (n = (u16)strlen(q->text); n > 0 && q->text[n - 1] == ' '; n--)
        ;
    q->text[n] = '\0';
    q->avail = (u8)(only == SCR_SI_AVAIL);
    if (only == SCR_SI_ACTIVE)
        strcpy(q->status, "A");
    q->kind = (u8)(n > 0 ? SCR_SQ_FIND : SCR_SQ_FILTER);
    for (i = 0; i < n && TXT_ISDIGIT(q->text[i]); i++)
        ;
    return n > 0 && i == n && n <= LINK_IDLEN;
}

const char *scr_search_id(const scr_query *q)
{
    const char *p;

    for (p = q->text; p[0] == '0' && p[1] != '\0'; p++)
        ;
    return p;
}

/* lv_fmt_fn of scr_search_src: a row of the item list in its first
   order.  arg, the search, is never NULL; the test keeps the parameter,
   which lv_fmt_fn requires, referenced. */
static void search_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    if (arg != NULL)
        itl_row(r, 0, text, key);
}

void scr_search_src(lv_src *src, scr_query *q)
{
    memcpy(src, &itl_src, sizeof *src);
    src->cmd = scr_search_cmds[q->kind];
    src->norders = 0;
    src->empty = (u16)(q->avail ? MI_NOAVAIL : MS_NONE);
    src->args = scr_search_args;
    src->fmt = search_fmt;
    src->arg = q;
}

u16 scr_search_why(const scr_query *q)
{
    return (u16)(q->status[0] != '\0' && q->avail ? MS_BOTHQ : MS_EMPTYQ);
}

void scr_search_args(void *arg, rq *q)
{
    scr_query *s;

    s = (scr_query *)arg;
    if (s->kind == SCR_SQ_SERIAL) {
        rq_word(q, s->serial);
        return;
    }
    if (s->kind == SCR_SQ_FIND)
        rq_word(q, s->text);
    if (s->cat[0] != '\0')
        rq_pair(q, k_cat, s->cat);
    if (s->loc[0] != '\0') {
        rq_pair(q, k_loc, s->loc);
        /* The tree below the location is in tree order; else ITEM
           LIST's location order has the name of the LOC pair, and ITEM
           FIND keeps record order. */
        if (s->below)
            rq_pair(q, k_all, "1");
        else if (s->kind == SCR_SQ_FILTER)
            rq_pair(q, "ORDER", k_loc);
    }
    if (s->status[0] != '\0')
        rq_pair(q, k_status, s->status);
    if (s->avail)
        rq_pair(q, k_avail, "1");
    if (s->tag[0] != '\0')
        rq_pair(q, k_tag, s->tag);
    if (s->works[0] != '\0')
        rq_pair(q, itl_k_works, s->works);
}

const char *scr_search_title(const scr_query *q)
{
    /* The filters' title has no ^1. */
    return msg_sub((u16)(q->kind == SCR_SQ_FIND && filtered(q) ? MS_T_FINDF :
                         MS_T_FIND + q->kind),
                   q->kind == SCR_SQ_FIND ? q->text : q->serial, NULL);
}
