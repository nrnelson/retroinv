/*
 * SCRMFORM.C - the service record form's table and the service lists'
 * row source (SCRMFORM.H).
 *
 * Field names are taken from the table wherever a row needs them, so
 * each is kept once.  Compile-time checks here are pairs of
 * declarations of one array, the second of size 2 when the condition
 * fails: they then disagree, which every compiler refuses.
 */
#include <stddef.h>
#include "COMPAT.H"
#include "LINK.H"
#include "TUI.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "FORM.H"
#include "SCRSHR.H"
#include "ITEMLS.H"
#include "SCRMFORM.H"

/* The summary fits a form value. */
extern char maint_fits_form[1];
extern char maint_fits_form[(MAINT_SUMW <= FORM_MAXVAL) ? 1 : 2];

/* INV's form box is VF_H rows high, on the first two columns of the
   form grid; the summary shows VF_SUMDISP cells of it. */
#define VF_H            7
#define VF_SUMDISP      (SCR_POPW - 2 - SCR_V1 - 2)

const char scr_maint_k_id[] = "MAINT_ID";

const form_field scr_maint_fields[] = {
    { scr_maint_k_id, MV_L_ID, 0, SCR_C1, SCR_V1, FT_NUM, LINK_IDLEN, 0,
      FF_RO | FF_ID, 0, NULL },
    { itl_k_itemid, MV_L_ITEM, 0, SCR_C2, SCR_V2, FT_NUM, LINK_IDLEN, 0,
      FF_KEY | FF_RO | FF_ID, 0, NULL },
    { "MDATE",   MV_L_DATE,    1, SCR_C1, SCR_V1, FT_DATE, FORM_DATEW, 0,
      0, 0, NULL },
    { "MTYPE",   MV_L_TYPE,    1, SCR_C2, SCR_V2, FT_CHOICE, MAINT_TYPEW, 0,
      FF_REQ | FF_UPPER, 0, "RP CL RC TS MD IN" },
    { "SUMMARY", MV_L_SUMMARY, 2, SCR_C1, SCR_V1, FT_CHAR, MAINT_SUMW, 0,
      FF_REQ, VF_SUMDISP, NULL },
    { "COST",    MV_L_COST,    3, SCR_C1, SCR_V1, FT_NUM, MAINT_COSTW,
      MAINT_COSTDEC, FF_NONNEG, 0, NULL },
    { "DONE_BY", MV_L_DONEBY,  3, SCR_C2, SCR_V2, FT_CHAR, MAINT_DONEW, 0,
      0, 0, NULL },
    { "NOTES",   MV_L_NOTES,   4, SCR_C1, SCR_V1, FT_MEMO, FORM_MEMOW, 0,
      0, FORM_MEMODISP, NULL }
};

extern char maint_f_count[1];
extern char maint_f_count[(sizeof scr_maint_fields /
                           sizeof scr_maint_fields[0] == SCR_MF_COUNT) ?
                          1 : 2];

const form_def scr_maint_def = {
    MV_NEWTITLE, "SERVREC", SCR_POPTOP, SCR_POPLEFT, VF_H, SCR_POPW,
    SCR_MF_COUNT, scr_maint_fields
};

/* ---------------------------------------------------------------------
 * The lists.
 * --------------------------------------------------------------------- */

const lv_src scr_maint_src = {
    "MAINT RECENT", 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MV_HDR, MV_HDR, MV_HDR, MV_HDR },
    MV_EMPTY, scr_maint_k_id, NULL, NULL, NULL, NULL
};

const lv_src scr_maint_isrc = {
    "MAINT LIST", 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MV_HDRTAB, MV_HDRTAB, MV_HDRTAB, MV_HDRTAB },
    MV_EMPTY, scr_maint_k_id, NULL, NULL, NULL, NULL
};

void scr_maint_row(const lv_rec *r, int withitem, char *text, char *key)
{
    const char *id;
    u16 at;

    id = lv_val(r, scr_maint_k_id);
    while (*id == ' ')
        id++;
    txt_copy(key, id, LV_KEYSZ);
    text[0] = '\0';
    at = 0;
    lv_col(text, &at, id, VC_ID, 1);
    if (withitem)
        lv_col(text, &at, lv_val(r, scr_maint_fields[SCR_MF_ITEM].name),
               VC_ID, 1);
    lv_datecol(text, &at, lv_val(r, scr_maint_fields[SCR_MF_DATE].name));
    lv_col(text, &at, lv_val(r, scr_maint_fields[SCR_MF_TYPE].name),
           MAINT_TYPEW, 0);
    lv_col(text, &at, lv_val(r, scr_maint_fields[SCR_MF_COST].name),
           VC_COST, 1);
    lv_col(text, &at, lv_val(r, scr_maint_fields[SCR_MF_SUM].name),
           (u16)(LV_TEXTLEN - at), 0);
}
