/*
 * ITEMLS.C - the item list's row source (ITEMLS.H).
 */
#define MSG_LETTER
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "PAGER.H"
#include "MSG.H"
#include "SCR.H"
#include "ITEMLS.H"

#define IL_NORDERS      4       /* sort orders of the source            */

const char itl_k_itemid[] = "ITEM_ID";
const char itl_k_cat[] = "CATEGORY";
const char itl_k_status[] = "STATUS";
const char itl_k_mfr[] = "MFR";
const char itl_k_model[] = "MODEL";
const char itl_k_serial[] = "SERIALNO";
const char itl_k_cond[] = "COND";
const char itl_k_works[] = "WORKS";
const char itl_k_qty[] = "QTY";
const char itl_k_loc[] = "LOC_NAME";
const char itl_k_id[] = "ID";
const char itl_whys[] = ITL_WHYS;
const char itl_works[] = ITL_WORKS;

const lv_src itl_src = {
    "ITEM LIST", IL_NORDERS,
    { itl_k_id, LV_TREEORDER, itl_k_mfr, "SERIAL" },
    { NULL, NULL, NULL, "ITEM_ID,MFR,MODEL,SERIALNO,LOC_NAME" },
    { MI_HDR, MI_HDR, MI_HDR, MI_HDRSER },
    MI_EMPTY, itl_k_itemid, NULL, NULL, NULL, NULL
};
/* The rows of either order fill the width of a row. */
extern char itl_fits[1];
extern char itl_fits[(ITL_IDW + 1 + IC_CAT + 1 + 2 * (ITL_NAMEW + 1) +
                      IC_WORKS + 1 + IC_QTY + 1 + IC_LOC + 1 + IC_WHY ==
                      LV_TEXTLEN &&
                      ITL_IDW + 1 + 2 * (ITL_NAMEW + 1) + IC_SERIAL + 1 +
                      IC_SLOC == LV_TEXTLEN) ? 1 : 2];
extern char orders_fit[1];
extern char orders_fit[(SCMD_ITEMS_SER - SCMD_ITEMS_ID + 1 == IL_NORDERS &&
                        SCMD_ITEMS_SER - SCMD_ITEMS_ID == ITL_ORDSER) ?
                       1 : 2];

void itl_row(const lv_rec *r, u8 order, char *text, char *key)
{
    const char *id;
    const char *v;
    const char *w;
    u16 at;

    id = lv_val(r, itl_k_itemid);
    txt_copy(key, id, LV_KEYSZ);
    at = 0;
    lv_col(text, &at, id, ITL_IDW, 1);
    if (order == ITL_ORDSER) {
        lv_col(text, &at, lv_val(r, itl_k_mfr), ITL_NAMEW, LV_MARK);
        lv_col(text, &at, lv_val(r, itl_k_model), ITL_NAMEW, LV_MARK);
        lv_col(text, &at, lv_val(r, itl_k_serial), IC_SERIAL, 0);
        lv_col(text, &at, lv_val(r, itl_k_loc), IC_SLOC, LV_MARK);
    } else {
        lv_col(text, &at, lv_val(r, itl_k_cat), IC_CAT, 0);
        lv_col(text, &at, lv_val(r, itl_k_mfr), ITL_NAMEW, LV_MARK);
        lv_col(text, &at, lv_val(r, itl_k_model), ITL_NAMEW, LV_MARK);
        v = lv_val(r, itl_k_works);
        w = msg_letter(MT_WK_Q, itl_works, v);
        lv_col(text, &at, w != NULL ? w : v, IC_WORKS, 0);
        lv_col(text, &at, lv_val(r, itl_k_qty), IC_QTY, 1);
        lv_col(text, &at, lv_val(r, itl_k_loc), IC_LOC, LV_MARK);
        w = msg_letter(MH_K_NAME, itl_whys, lv_val(r, "AVAIL"));
        if (w != NULL)
            lv_col(text, &at, w, IC_WHY, 0);
    }
}
