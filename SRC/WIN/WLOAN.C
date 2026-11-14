/*
 * WLOAN.C - the screens of loans, reservations and uses of INVWIN
 * (WLOAN.H).
 *
 * Every list's rows are SCRNFORM.H's, whose source functions take what
 * a list holds (scr_lq) as their arg: wl_q, which each list's args set
 * before its rows are fetched and formatted, from the view wloan_view
 * chose for the list of every item, from the scope the frame gives as
 * their arg on the scoped screens.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "FORM.H"
#include "ITEMLS.H"
#include "SCRIFORM.H"
#include "SCRNFORM.H"
#include "WFORM.H"
#include "WCOLS.H"
#include "WROWS.H"
#include "WSCR.H"
#include "WPANE.H"
#include "WLOAN.H"
#include "INVWIN.H"

static const char k_topic[] = "LOANS";    /* the screens' and asks' topic */
static const char w_get[] = "LOAN GET";
static const char w_iget[] = "ITEM GET";

u8 wloan_shown;

static scr_lq wl_q;             /* what the list shown holds            */

/* The lists' args, those of wl_q, which they set first: from the view
   shown on the list of every item, which LOAN OVERDUE's gives none, and
   from the frame's arg, the scope's key, on the scoped screens: the
   rows of the item, or the open ones used with it. */
static void list_args(void *arg, rq *q)
{
    lv_src v;

    (void)arg;
    scr_loan_view(&v, &wl_q, wloan_shown);
    scr_loan_args(&wl_q, q);
}

static void item_args(void *arg, rq *q)
{
    memset(&wl_q, 0, sizeof wl_q);
    txt_copy(wl_q.item, (const char *)arg, (u16)sizeof wl_q.item);
    wl_q.kind = SCR_NKINDS;
    scr_loan_args(&wl_q, q);
}

static void host_args(void *arg, rq *q)
{
    memset(&wl_q, 0, sizeof wl_q);
    txt_copy(wl_q.host, (const char *)arg, (u16)sizeof wl_q.host);
    wl_q.kind = SCR_NKINDS;
    wl_q.open = 1;
    scr_loan_args(&wl_q, q);
}

static void fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    (void)arg;
    wrow_loan(&wl_q, r, text, key);
}

const wcol_tab *wloan_view(lv_src *src)
{
    scr_loan_view(src, &wl_q, wloan_shown);
    src->args = list_args;
    return &wrow_loan_cols[0];
}

/* The list's title, its view's. */
static const char *title(void)
{
    return msg((u16)(MH_T_VIEW + wloan_shown));
}

/* After LOAN GET, or a new row's preset, has loaded f (ws_scr's
   loaded): scr_loan_loaded, then the maker and model of its item and of
   its host, if any, read with ITEM GET from the frame; a failed read
   leaves them blank, shown by no one. */
static void loaded(form *f)
{
    const char *host;
    rq q;
    rq_res res;

    scr_loan_loaded(f);
    rq_begin(&q, w_iget);
    rq_word(&q, form_get(f, itl_k_itemid));
    form_load(f, scr_loan_k_what, "");
    ws_send(NULL, &q, scr_loan_kitem, f, &res);
    host = form_get(f, scr_loan_k_host);
    if (host != NULL && host[0] != '\0') {
        rq_begin(&q, w_iget);
        rq_word(&q, host);
        form_load(f, scr_loan_k_hwhat, "");
        ws_send(NULL, &q, scr_loan_khost, f, &res);
    }
}

static const form_field date_field[] = {
    { "DATE", MH_L_RETDATE, 0, 0, WS_PROMPTV, FT_DATE, FORM_DATEW, 0, 0, 0,
      NULL }
};

/* The end of a row, titled MH_Q_END plus its kind. */
static const form_def date_ask = {
    MH_Q_END, k_topic, 0, 0, 0, 0, 1, date_field
};

static const ws_act end_act[] = {
    { WS_K_RETURN, "LOAN RETURN", &date_ask, NULL, WA_KIND | WA_MULTI, 0,
      IW_S_RRET },
    { 0, NULL, NULL, NULL, 0, 0, 0 }
};

/* The notes of a row added and saved; none is deleted. */
static const u16 loan_notes[] = { MH_ADDED, MH_SAVED, 0 };

/* The item of a new row of the list of every item, titled MH_T_NEW plus
   its kind: its ID, or text to find it among the available items (the
   frame's ask, INVWIN.C). */
static const form_field item_field[] = {
    { itl_k_itemid, MI_L_ITEMID, 0, 0, WS_PROMPTV, FT_CODE, LINK_MAXFIND,
      0, FF_REQ, 0, NULL }
};

static const form_def item_ask = {
    MH_T_NEW, k_topic, 0, 0, 0, 0, 1, item_field
};

/* A row's form: saved as LOAN, a new row by its kind's command
   (scr_loan_new), its caption MH_TITLE plus its kind, its host an item
   field of the frame's pick list. */
static const wf_ops loan_ops = {
    scr_loan_w_loan, scr_loan_k_id, MH_TITLE, ws_form_busy, NULL, NULL,
    NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, ws_pick_item
};

/* The notes of a loan returned and of a reservation and a use
   released, by kind. */
static const u16 end_notes[SCR_NKINDS] = {
    MH_ENDED, MH_RELEASED, MH_RELEASED
};

/* The kinds of the rows of every item's lists, which name the row's
   item; of one item's; and of a host's, whose new reservations and uses
   name it. */
static const ws_kinds every_kinds = {
    scr_loan_rowkind, scr_loan_form, scr_loan_new, scr_loan_rowitem,
    end_notes, NULL, 0
};

static const ws_kinds item_kinds = {
    scr_loan_rowkind, scr_loan_form, scr_loan_new, NULL, end_notes, NULL,
    0
};

static const ws_kinds host_kinds = {
    scr_loan_rowkind, scr_loan_form, scr_loan_new, scr_loan_rowitem,
    end_notes, scr_loan_k_host, 1 << SCR_K_RES | 1 << SCR_K_USE
};

static const lv_src item_src = {
    scr_loan_w_list, 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MH_HDRTAB, MH_HDRTAB, MH_HDRTAB, MH_HDRTAB },
    MH_EMPTY, NULL, item_args, NULL, NULL, scr_loan_mark
};

static const lv_src host_src = {
    scr_loan_w_list, 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MH_HDR, MH_HDR, MH_HDR, MH_HDR },
    MH_EMPTYHOST, NULL, host_args, NULL, NULL, scr_loan_mark
};

const ws_scr wloan_list = {
    k_topic, &scr_loan_src, fmt, NULL, 0, NULL, &loan_ops, w_get, NULL, 0,
    NULL, loaded, WS_VIEWS | WS_FILTER, itl_k_itemid, &item_ask, end_act,
    &every_kinds, NULL, loan_notes, title, NULL, &wpane_loan
};

const ws_scr wloan_item = {
    k_topic, &item_src, fmt, &wrow_loan_cols[1], MH_T_ITEM, NULL,
    &loan_ops, w_get, NULL, 0, NULL, loaded, WS_SCOPED, itl_k_itemid, NULL,
    end_act, &item_kinds, NULL, loan_notes
};

const ws_scr wloan_host = {
    k_topic, &host_src, fmt, &wrow_loan_cols[0], MH_T_HOST, NULL,
    &loan_ops, w_get, NULL, 0, NULL, loaded,
    WS_SCOPED | WS_NONEW | WS_FILTER, itl_k_itemid, &item_ask, end_act,
    &host_kinds, NULL, loan_notes
};
