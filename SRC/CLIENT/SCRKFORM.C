/*
 * SCRKFORM.C - the forms of the code lists' entries (SCRKFORM.H).
 */
#include <stddef.h>
#include "COMPAT.H"
#include "LINK.H"
#include "TUI.H"
#include "FMODEL.H"
#include "FORM.H"
#include "CODETAB.H"
#include "SCRSHR.H"
#include "SCRKFORM.H"

/* The form's value column and box. */
#define CF_VCOL         8
#define CF_W            (CF_VCOL + LINK_CODENAMELEN + 2)

/* Each pair of messages is the categories' and the location types' in
   kind order, as the screens add the kind to the first. */
extern char me_by_kind[1];
extern char me_by_kind[(ME_LTYPES == ME_CATS + SCR_CK_LTYPE &&
                        ME_NEWLT == ME_NEWCAT + SCR_CK_LTYPE &&
                        ME_FLT == ME_FCAT + SCR_CK_LTYPE &&
                        SCR_CK_CAT == 0) ? 1 : 2];

static const char k_order[] = "SORTORD";

static const form_field cat_fields[SCR_CF_COUNT] = {
    { "CAT_CODE", ME_L_CODE, 0, 1, CF_VCOL, FT_CHAR, LINK_CATLEN, 0,
      FF_KEY | FF_NEWONLY | FF_UPPER | FF_REQ, 0, NULL },
    { "CAT_NAME", ME_L_NAME, 1, 1, CF_VCOL, FT_CHAR, LINK_CODENAMELEN, 0,
      FF_REQ, 0, NULL },
    { k_order, ME_L_ORDER, 2, 1, CF_VCOL, FT_NUM, CT_ORDERW, 0, FF_NONNEG,
      0, NULL }
};

static const form_field ltype_fields[SCR_CF_COUNT] = {
    { "LT_CODE", ME_L_CODE, 0, 1, CF_VCOL, FT_CHAR, LINK_LTYPELEN, 0,
      FF_KEY | FF_NEWONLY | FF_UPPER | FF_REQ, 0, NULL },
    { "LT_NAME", ME_L_NAME, 1, 1, CF_VCOL, FT_CHAR, LINK_CODENAMELEN, 0,
      FF_REQ, 0, NULL },
    { k_order, ME_L_ORDER, 2, 1, CF_VCOL, FT_NUM, CT_ORDERW, 0, FF_NONNEG,
      0, NULL }
};

static const form_def cat_def = {
    ME_NEWCAT, "CATS", SCR_POPTOP, (VID_COLS - CF_W) / 2,
    SCR_CF_COUNT + 2, CF_W, SCR_CF_COUNT, cat_fields
};

static const form_def ltype_def = {
    ME_NEWLT, "LTYPES", SCR_POPTOP, (VID_COLS - CF_W) / 2,
    SCR_CF_COUNT + 2, CF_W, SCR_CF_COUNT, ltype_fields
};

const form_def *const scr_code_defs[] = { &cat_def, &ltype_def };
