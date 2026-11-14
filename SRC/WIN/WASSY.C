/*
 * WASSY.C - the assembly commands and the split of INVWIN (WASSY.H).
 */
#include <stddef.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "ITEMLS.H"
#include "SCRIFORM.H"
#include "SCRAFORM.H"
#include "CODETAB.H"
#include "SCRLFORM.H"
#include "WFORM.H"
#include "WSCR.H"
#include "WASSY.H"
#include "INVWIN.H"

/* The location to go to, and its path. */
static const form_field loc_field[] = {
    { scr_assy_k_loc, MI_L_LOC, 0, 0, WS_ASKV, FT_CODE, LINK_LOCLEN, 0,
      FF_REQ | FF_UPPER, 0, NULL },
    { "WHERE", MI_L_WHERE, 1, 0, WS_ASKV, FT_CHAR, SCR_LF_PATHW, 0,
      FF_LOCAL | FF_RO, 0, NULL }
};

/* That location field: "0" no location, as the pick list's lead row
   gives it; blank, which the field refuses, nothing shown. */
static const scr_locf loc_to = {
    &loc_field[0], &loc_field[1], MO_NOLOC, 1
};

/* The item to install in and the part to install: an ID, or text to
   find it (WFORM.H's owner's pick list). */
static const form_field parent_field[] = {
    { scr_assy_k_parent, MI_L_ITEMID, 0, 0, WS_PROMPTV, FT_CODE,
      LINK_MAXFIND, 0, FF_REQ, 0, NULL }
};

static const form_field child_field[] = {
    { scr_assy_k_child, MI_L_ITEMID, 0, 0, WS_PROMPTV, FT_CODE,
      LINK_MAXFIND, 0, FF_REQ, 0, NULL }
};

static const form_field qty_field[] = {
    { itl_k_qty, MI_L_QTY, 0, 0, WS_ASKV, FT_NUM, LINK_QTYLEN, 0,
      FF_REQ | FF_NONNEG, 0, NULL }
};

static const form_def move_ask = {
    MA_Q_MOVE, ws_k_items, 0, 0, 0, 0, 2, loc_field
};

static const form_def out_ask = {
    MA_Q_OUT, ws_k_parts, 0, 0, 0, 0, 2, loc_field
};

static const form_def into_ask = {
    MA_Q_PARENT, ws_k_parts, 0, 0, 0, 0, 1, parent_field
};

static const form_def part_ask = {
    MA_Q_CHILD, ws_k_parts, 0, 0, 0, 0, 1, child_field
};

static const form_def split_ask = {
    MI_SPLITQ, ws_k_items, 0, 0, 0, 0, 1, qty_field
};

/* ASSY INSTALL of item key in the item asked for (ws_act's send). */
static int install_in(link *l, const char *cmd, const char *key, form *f,
                      rq_res *res)
{
    return scr_assy_send(l, cmd, key, form_get(f, scr_assy_k_parent), NULL,
                         SCR_AT_PARENT, res);
}

/* ASSY INSTALL of the item asked for in item key, the scope. */
static int install_part(link *l, const char *cmd, const char *key, form *f,
                        rq_res *res)
{
    return scr_assy_send(l, cmd, form_get(f, scr_assy_k_child), key, NULL,
                         SCR_AT_CHILD, res);
}

const ws_act wassy_item_acts[] = {
    { WS_K_MOVE, scr_assy_w_move, &move_ask, NULL, WA_KEEP | WA_MULTI,
      MA_MOVED, IW_S_RMOVE, &loc_to },
    { WS_K_INSTALL, scr_assy_w_install, &into_ask, install_in, WA_MULTI,
      MA_INSTALLED, IW_S_RINTO },
    { WS_K_REMOVE, scr_assy_w_remove, &out_ask, NULL, WA_MULTI, MA_REMOVED,
      IW_S_ROUT, &loc_to },
    { WS_K_SPLIT, "ITEM SPLIT", &split_ask, NULL, 0, MI_SPLIT, IW_S_RSPLIT },
    { 0, NULL, NULL, NULL, 0, 0, 0 }
};

const ws_act wassy_part_acts[] = {
    { VK_INSERT, scr_assy_w_install, &part_ask, install_part, WA_SCOPE,
      MA_INSTALLED, IW_S_RPART },
    { WS_K_REMOVE, scr_assy_w_remove, &out_ask, NULL, WA_MULTI, MA_REMOVED,
      IW_S_ROUT, &loc_to },
    { 0, NULL, NULL, NULL, 0, 0, 0 }
};

/* The assemblies' args: TOP=1, the items in no other. */
static void top_args(void *arg, rq *q)
{
    (void)arg;
    rq_pair(q, "TOP", "1");
}

const lv_src wassy_src = {
    "ASSY LIST", 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MI_HDR, MI_HDR, MI_HDR, MI_HDR },
    MA_EMPTY, itl_k_itemid, top_args, NULL, NULL, NULL
};
