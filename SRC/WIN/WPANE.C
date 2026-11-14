/*
 * WPANE.C - the panes of INVWIN's screens (WPANE.H): each one's
 * descriptor and its follow function.
 */
#define RQERR_CLASS
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "CFG.H"
#include "MSG.H"
#include "RQERR.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "MEMOIO.H"
#include "ITEMLS.H"
#include "SCRIFORM.H"
#include "SCRNFORM.H"
#include "CODETAB.H"
#include "WFORM.H"
#include "WCOLS.H"
#include "WLIST.H"
#include "WROWS.H"
#include "WSCR.H"
#include "WLOC.H"
#include "WLOAN.H"
#include "WADMIN.H"
#include "WPREV.H"
#include "WPANE.H"
#include "INVWIN.H"

/* A value of the item form, a category with its name and a line of the
   notes each fit a line of the preview panel whole. */
extern char wpane_fits[1];
extern char wpane_fits[(FORM_MAXVAL <= WPV_VALW && WROW_CATW <= WPV_VALW &&
                        MEMO_COLS <= WPV_VALW) ? 1 : 2];

/* The preview panel's block holds an item whole: a full line for each
   field of the item form, the notes' label being its last one's, for
   the location's path and for the one text that says the notes are
   not whole (mio_check gives a memo one message), and MEMO_MAXLINES
   lines of the notes, each a full line of a memo with its NUL and its
   empty label's. */
extern char wpane_room[1];
extern char wpane_room[((SCR_IF_COUNT + 2L) * WPV_LINESZ +
                        (long)MEMO_MAXLINES * (MEMO_COLS + 2) <=
                        WPV_BYTES) ? 1 : 2];

static const char k_empty[] = "";
static const char w_item[] = "ITEM";
static const char w_get[] = "ITEM GET";

/* The locations' follow (ws_pane): location key set as the one whose
   contents show, or read again when it is that one already, as when the
   tree opens again on it, so that what was last set for its contents,
   not installed only and everything below, stays; the pane's scope is
   the location. */
static int loc_follow(HWND w, const char *key, const char *text,
                      char *scope, HWND pv, u8 *act)
{
    (void)text;
    (void)pv;
    if (!(strcmp(key, wloc_id) == 0 ? wloc_refresh(w, act)
                                    : wloc_enter(w, key, act)))
        return 0;
    txt_copy(scope, key, LV_KEYSZ);
    return 1;
}

const ws_pane wpane_loc = {
    CFG_P_LOC, &ws_contents, &wrow_tpick_cols, loc_follow, IW_S_NHERE,
    wloc_cname
};

/* Add to panel pv the fields of the item form f that the item form shows
   on page pg (wrow_item_pages), each under its label there (wf_label)
   with its value as a view shows it (wf_view_text), the category named
   as the rows name it (wrow_cat), the location's path right under the
   location, where the form's rows put it; none for an empty value that
   has no label. */
static void add_fields(HWND pv, form *f, u8 pg)
{
    char label[MSG_MAXLEN + 1];
    char v[WPV_VALW + 1];
    u16 i;

    for (i = 0; i < SCR_IF_COUNT; i++) {
        if (wrow_item_pages.page[i] != pg || i == SCR_IF_WHERE)
            continue;
        wf_label((HINSTANCE)GetWindowWord(pv, GWW_HINSTANCE),
                 &scr_item_fields[i], label);
        AnsiToOem(label, label);
        wf_view_text(f, i, v);
        if (i == SCR_IF_CAT)
            wrow_cat(ws_cats(), v);
        if (label[0] != '\0' || v[0] != '\0')
            wpv_add(pv, label, v);
        if (i == SCR_IF_LOC)
            wpv_add(pv, k_empty, form_get(f, scr_item_loc.path->name));
    }
}

/* The items' follow (ws_pane): the item of key in panel pv, read into a
   form of its own with ITEM GET as the item form reads it (scr_item_kline,
   scr_item_loaded), its location, when it has one, with LOC GET as the
   form shows it, its code and its path (wf_loc_asks, wf_loc_get), one
   that is gone meanwhile left without a path: the fields above the item
   form's tabs, those of its first page, and then, read with ITEM NOTES,
   its notes under their label, when it has any, and after notes that
   are not whole, more of them than a memo holds or cut short by the
   server, what the item form says of those (mio_check's MM_TOOMANY or
   MM_TRUNC).  Before each request but the first comes the wait of a
   list's move (wl_wait), which an Esc stops (RQ_ESTOP here).  An item
   that is gone (200) is said in the panel (IW_S_PGONE). */
static int item_follow(HWND w, const char *key, const char *text,
                       char *scope, HWND pv, u8 *act)
{
    char t[MSG_MAXLEN + 1];
    char num[TXT_U32LEN + 1];
    scr_locinfo x;
    form *f;
    mio m;
    rq q;
    rq_res res;
    int rc;
    u16 mm;
    u16 i;

    (void)text;
    (void)scope;
    *act = RQE_STAY;
    if (wf_open(&f, &scr_item_def, FORM_VIEW) != 0)
        return 0;
    if (mio_open(&m) != RQ_OK) {
        form_close(f);
        return 0;
    }
    ws_form_busy(NULL, NULL, 1);
    rq_begin(&q, w_get);
    rq_word(&q, key);
    rc = rq_send(ws_link(), &q, scr_item_kline, f, &res);
    if (rc == RQ_OK) {
        scr_item_loaded(f);
        if (wf_loc_asks(f, &scr_item_loc)) {
            rc = wl_wait() ? RQ_ESTOP :
                 wf_loc_get(f, ws_link(), &scr_item_loc, &x, &res);
            if (rc == RQ_OK && x.code[0] != '\0')
                form_load(f, scr_item_loc.loc->name, x.code);
            else if (rc == RQ_ENOTFOUND)
                rc = RQ_OK;
        }
    }
    if (rc == RQ_OK) {
        add_fields(pv, f, WF_PG_TOP);
        add_fields(pv, f, 0);
        rc = wl_wait() ? RQ_ESTOP : mio_load(&m, ws_link(), w_item, key, &res);
    }
    ws_form_busy(NULL, NULL, 0);
    if (rc == RQ_OK) {
        mm = mio_check(&m);
        if (m.n > 0) {
            wf_label((HINSTANCE)GetWindowWord(pv, GWW_HINSTANCE),
                     &scr_item_fields[SCR_IF_NOTES], t);
            AnsiToOem(t, t);
            wpv_add(pv, t, k_empty);
        }
        for (i = 0; i < m.n; i++) {
            mio_get(&m, i, t);
            if (!wpv_note(pv, t))
                break;
        }
        if (mm == MM_TOOMANY || mm == MM_TRUNC) {
            txt_u32(num, MEMO_MAXLINES);
            wpv_note(pv, msg_sub(mm, num, NULL));
        }
        wpv_done(pv);
    } else if (rc == RQ_ENOTFOUND) {
        wpv_text(pv, IW_S_PGONE, NULL);
    } else if (rc != RQ_ESTOP) {
        *act = ws_fail(w, &res);
    }
    mio_close(&m);
    form_close(f);
    return rc == RQ_OK || rc == RQ_ENOTFOUND;
}

const ws_pane wpane_item = {
    CFG_P_ITEM, NULL, wrow_item_cols, item_follow, 0, NULL, IW_S_PLOAD
};

/* The follow of a pane that lists what belongs to the master's record
   (ws_pane): the pane's scope is the row's key, and nothing is sent. */
static int key_follow(HWND w, const char *key, const char *text,
                      char *scope, HWND pv, u8 *act)
{
    (void)w;
    (void)text;
    (void)pv;
    (void)act;
    txt_copy(scope, key, LV_KEYSZ);
    return 1;
}

const ws_pane wpane_assy = {
    CFG_P_ASSY, &ws_parts, wrow_item_cols, key_follow
};

/* The loans' follow (ws_pane): the pane's scope is the item of the row,
   which its text names (scr_loan_rowitem), and nothing is sent. */
static int loan_follow(HWND w, const char *key, const char *text,
                       char *scope, HWND pv, u8 *act)
{
    (void)w;
    (void)key;
    (void)pv;
    *act = RQE_STAY;
    return scr_loan_rowitem(text, scope);
}

const ws_pane wpane_loan = {
    CFG_P_LOAN, &wloan_item, &wrow_loan_cols[0], loan_follow
};

const ws_pane wpane_cat = {
    CFG_P_CAT, &ws_catitems, &wrow_code_cols, key_follow
};

const ws_pane wpane_user = {
    CFG_P_USER, &wadm_audit, &wrow_user_cols, key_follow
};
