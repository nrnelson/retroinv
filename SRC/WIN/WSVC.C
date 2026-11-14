/*
 * WSVC.C - the service screens of INVWIN (WSVC.H).
 */
#include <stddef.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "MSG.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "MEMOIO.H"
#include "ITEMLS.H"
#include "SCRMFORM.H"
#include "WFORM.H"
#include "WCOLS.H"
#include "WROWS.H"
#include "WSCR.H"
#include "WSVC.H"

static const char k_topic[] = "SERVICE";  /* both screens' help topic */
static const char w_get[] = "MAINT GET";
static const char w_del[] = "MAINT DEL";

static void recent_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    (void)arg;
    wrow_svc(r, 1, text, key);
}

static void item_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    (void)arg;
    wrow_svc(r, 0, text, key);
}

static const u16 svc_notes[] = { MV_ADDED, MV_SAVED, MV_DELETED };

/* The field naming a service record after its ID (wf_ops' names). */
static const char k_summary[] = "SUMMARY";
static const char *const svc_names[] = { k_summary, NULL };

static const wf_ops svc_ops = {
    "MAINT", scr_maint_k_id, MV_TITLE, ws_form_busy, mio_load, mio_save,
    NULL, NULL, NULL, 0, 0, svc_names
};

const ws_scr wsvc_recent = {
    k_topic, &scr_maint_src, recent_fmt, &wrow_svc_cols[0], MV_T_RECENT,
    &scr_maint_def, &svc_ops, w_get, w_del, MV_DELQ, NULL, NULL, WS_NONEW,
    NULL, NULL, NULL, NULL, NULL, svc_notes
};

const ws_scr wsvc_item = {
    k_topic, &scr_maint_isrc, item_fmt, &wrow_svc_cols[1], MV_T_ITEM,
    &scr_maint_def, &svc_ops, w_get, w_del, MV_DELQ, NULL, NULL, WS_SCOPED,
    itl_k_itemid, NULL, NULL, NULL, NULL, svc_notes
};
