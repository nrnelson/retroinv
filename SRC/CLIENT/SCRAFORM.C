/*
 * SCRAFORM.C - the parts list's row source and the assembly requests
 * (SCRAFORM.H).
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
#include "PAGER.H"
#include "SCRSHR.H"
#include "ITEMLS.H"
#include "SCRIFORM.H"
#include "SCRAFORM.H"

/* The deepest indent leaves room in the name column. */
extern char indent_fits[1];
extern char indent_fits[(AC_INDENT * (SCR_BOMINDENT - 1) < AC_NAME) ?
                        1 : 2];

const char scr_assy_w_install[] = "ASSY INSTALL";
const char scr_assy_w_remove[] = "ASSY REMOVE";
const char scr_assy_w_move[] = "ASSY MOVE";
const char scr_assy_k_child[] = "CHILD";
const char scr_assy_k_parent[] = "PARENT";
const char scr_assy_k_loc[] = "LOC";

const lv_src scr_assy_src = {
    "ASSY BOM", 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MA_HDRBOM, MA_HDRBOM, MA_HDRBOM, MA_HDRBOM },
    MA_EMPTYBOM, itl_k_itemid, scr_assy_args, scr_assy_fmt, NULL, NULL
};

void scr_assy_args(void *arg, rq *q)
{
    rq_word(q, (const char *)arg);
}

/* arg is never NULL: the test keeps the parameter, which lv_fmt_fn
   requires, referenced. */
void scr_assy_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    const char *id;
    const char *lev;
    u16 level;
    u16 ind;
    u16 at;

    if (arg == NULL)
        return;
    id = lv_val(r, itl_k_itemid);
    lev = lv_val(r, "LEVEL");
    level = 0;
    while (*lev == ' ')
        lev++;
    while (*lev >= '0' && *lev <= '9' && level < SCR_BOMINDENT)
        level = (u16)(level * 10 + (u16)(*lev++ - '0'));
    if (level < 1)
        level = 1;
    if (level > SCR_BOMINDENT)
        level = SCR_BOMINDENT;
    ind = (u16)(AC_INDENT * (level - 1));
    while (*id == ' ')
        id++;
    txt_copy(key, id, LV_KEYSZ);
    at = 0;
    lv_col(text, &at, id, AC_ID, 1);
    lv_col(text, &at, lv_val(r, itl_k_cat), LINK_CATLEN, 0);
    memset(text + at, ' ', ind);
    at = (u16)(at + ind);
    lv_col(text, &at, lv_val(r, itl_k_mfr), (u16)(AC_NAME - ind), 0);
    lv_col(text, &at, lv_val(r, itl_k_model), AC_NAME, 0);
    lv_col(text, &at, lv_val(r, itl_k_qty), AC_QTY, 1);
    lv_col(text, &at, lv_val(r, itl_k_loc), AC_LOC, LV_MARK);
    lv_col(text, &at, lv_val(r, itl_k_status), 1, 0);
}

int scr_assy_send(link *l, const char *words, const char *a, const char *b,
                  const char *loc, u8 typed, rq_res *res)
{
    rq q;
    int rc;

    rq_begin(&q, words);
    rq_word(&q, a);
    if (b != NULL)
        rq_word(&q, b);
    if (loc != NULL)
        rq_pair(&q, scr_assy_k_loc, loc);
    rc = rq_send(l, &q, NULL, NULL, res);
    if (rc == RQ_ENOTFOUND && typed != SCR_AT_NONE &&
        strcmp(res->field, typed == SCR_AT_CHILD ? scr_assy_k_child :
                                                   scr_assy_k_parent) == 0) {
        rc = RQ_ECONSTR;
        res->code = rc;
        txt_copy(res->text, msg_sub(MI_NOITEM,
                                    typed == SCR_AT_CHILD ? a : b, NULL),
                 (u16)sizeof res->text);
    }
    return rc;
}
