/*
 * WCODE.C - the code lists of INVWIN (WCODE.H).
 *
 * The dialog's state (wc_dlg) is on wcode_run's stack, and wc_cur points
 * to it while the dialog runs, for the dialog procedure.  The list is
 * fetched as the dialog opens, by a command it posts itself, and again
 * after each change, the selection put back on the entry.  Its Edit, New
 * and Delete are the commands it shares with the frame's list of the
 * categories (wcode_form, wcode_del).
 */
#define RQERR_CLASS
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "RQERR.H"
#include "TUI.H"
#include "FMODEL.H"
#include "CODETAB.H"
#include "SCRKFORM.H"
#include "WDLG.H"
#include "WFORM.H"
#include "WPICK.H"
#include "WSCR.H"
#include "WHELP.H"
#include "INVWIN.H"
#include "WCODE.H"

/* A row: the code, the name and the sort order, a blank between, as
   the heading ME_HDR gives them, and their columns. */
#define WC_ROWW         (LINK_LTYPELEN + 1 + LINK_CODENAMELEN + 1 + \
                         CT_ORDERW)

static const wcol wc_col[] = {
    { LINK_LTYPELEN, 0 }, { LINK_CODENAMELEN, 0 }, { CT_ORDERW, WCOL_RIGHT }
};
static const wcol_tab wc_cols = {
    sizeof wc_col / sizeof wc_col[0], 0, wc_col
};

/* The heading's columns: its "Order", WC_ORDHW cells, wider than the
   order's, takes them from the name's, so that it ends over the
   digits. */
#define WC_ORDHW        5
#define WC_HNAMEW       (LINK_CODENAMELEN - (WC_ORDHW - CT_ORDERW))

static const wcol wc_hcol[] = {
    { LINK_LTYPELEN, 0 }, { WC_HNAMEW, 0 }, { WC_ORDHW, WCOL_RIGHT }
};
static const wcol_tab wc_hcols = {
    sizeof wc_hcol / sizeof wc_hcol[0], 0, wc_hcol
};

/* A row fits a list box's row (WDLG.H), and the heading's columns end
   where the row's do. */
extern char wc_fits[1];
extern char wc_fits[(WC_ROWW <= LV_TEXTLEN &&
                     LINK_LTYPELEN + 1 + WC_HNAMEW + 1 + WC_ORDHW ==
                     WC_ROWW) ? 1 : 2];

/* The command that fetches the list, as the dialog opens and on F5,
   the selection kept on its row: beyond the last control's ID. */
#define WC_LOADID       (IW_C_CHDR + 1)

/* The form dialog's part, its table and title set by kind (edit). */
static const wf_ops wc_ops = { NULL, NULL, 0, ws_form_busy };

/* The controls wd_fit widens and moves. */
static const int wc_wide[] = { IW_C_CHDR, IW_C_CROWS, 0 };
static const int wc_move[] = { IW_C_CEDIT, IW_C_CNEW, IW_C_CDEL, IDCANCEL,
                               0 };

typedef struct {
    ctab t;                     /* the list shown                       */
    link *l;
    HWND dlg;
    u8   kind;
    u8   level;
} wc_dlg;

static wc_dlg *wc_cur;          /* the dialog running, or NULL          */

/* Fetch the list into the box, the row of code selected, else the row
   at sel, else the last; a failure, shown, ends the dialog with its
   action. */
static void load(wc_dlg *c, const char *code, int sel)
{
    char t[WC_ROWW + 1];
    rq_res res;
    ct_row r;
    HWND box;
    u16 i;
    u16 n;
    int rc;

    box = GetDlgItem(c->dlg, IW_C_CROWS);
    ws_form_busy(NULL, c->dlg, 1);
    rc = ct_load(&c->t, c->l, wp_tables[c->kind], &res);
    ws_form_busy(NULL, c->dlg, 0);
    if (rc != RQ_OK) {
        EndDialog(c->dlg, ws_fail(c->dlg, &res));
        return;
    }
    SendMessage(box, LB_RESETCONTENT, 0, 0L);
    for (i = 0; i < c->t.n; i++) {
        ct_get(&c->t, i, &r);
        memset(t, ' ', WC_ROWW);
        memcpy(t, r.code, strlen(r.code));
        memcpy(t + LINK_LTYPELEN + 1, r.name, strlen(r.name));
        n = (u16)strlen(r.order);
        memcpy(t + WC_ROWW - n, r.order, n);
        t[WC_ROWW] = '\0';
        SendMessage(box, LB_ADDSTRING, 0, (LPARAM)(LPCSTR)t);
        if (code != NULL && strcmp(r.code, code) == 0)
            sel = (int)i;
    }
    if (sel >= (int)c->t.n)
        sel = (int)c->t.n - 1;
    SendMessage(box, LB_SETCURSEL, (WPARAM)sel, 0L);
}

/* The selected entry into *r and its row, or -1 when none is. */
static int selected(wc_dlg *c, ct_row *r)
{
    LRESULT i;

    i = SendDlgItemMessage(c->dlg, IW_C_CROWS, LB_GETCURSEL, 0, 0L);
    if (i < 0 || (u16)i >= c->t.n)
        return -1;
    ct_get(&c->t, (u16)i, r);
    return (int)i;
}

/* Say that a form could not be made, over the dialog. */
static void no_form(wc_dlg *c)
{
    char t[MSG_MAXLEN + 1];

    LoadString((HINSTANCE)GetWindowWord(c->dlg, GWW_HINSTANCE), IW_S_NOFORM,
               t, (int)sizeof t);
    ws_say(c->dlg, MB_ICONSTOP, t);
}

int wcode_form(HWND w, link *l, u8 level, u8 kind, ct_row *r)
{
    const form_field *fl;
    wf_ops ops;
    form *f;
    int isnew;
    int rc;

    isnew = r->code[0] == '\0';
    if (wf_open(&f, scr_code_defs[kind],
                (u8)(isnew ? FORM_NEW : FORM_EDIT)) != 0)
        return WF_EFAIL;
    fl = scr_code_defs[kind]->fields;
    if (!isnew) {
        form_load(f, fl[SCR_CF_CODE].name, r->code);
        form_load(f, fl[SCR_CF_NAME].name, r->name);
        form_load(f, fl[SCR_CF_ORDER].name, r->order);
    }
    memcpy(&ops, &wc_ops, sizeof ops);
    ops.table = wp_tables[kind];
    ops.title = (u16)(ME_FCAT + kind);
    rc = wf_run(f, w, l, level, &ops, NULL, r->code, LINK_LTYPELEN + 1);
    if (rc == WF_OK)
        form_get_text(f, fl[SCR_CF_NAME].name, r->name, (u16)sizeof r->name);
    form_close(f);
    return rc;
}

int wcode_del(HWND w, u8 kind, const ct_row *r)
{
    char t[MSG_MAXLEN + 1];
    rq q;
    rq_res res;
    int rc;
    u8 act;

    txt_copy(t, msg_sub(ME_DELQ, r->name, NULL), (u16)sizeof t);
    if (ws_say_oem(w, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, t) != IDYES)
        return WF_CANCEL;
    rq_begin(&q, wp_tables[kind]);
    rq_word(&q, "DEL");
    rq_word(&q, r->code);
    rc = ws_send(w, &q, NULL, NULL, &res);
    if (rc == RQ_OK || rc == RQ_ENOTFOUND)
        return rc == RQ_OK ? WF_OK : WF_GONE;
    act = ws_fail(w, &res);
    return act == RQE_LOGIN ? WF_LOGIN : act == RQE_QUIT ? WF_QUIT :
           act == RQE_LOST ? WF_LOST : WF_CANCEL;
}

/* After a command on an entry ended rc (wcode_form, wcode_del): once it
   changed the entry, or found it gone, the list again, the entry code
   selected, else the row at; a form that could not be made said; a
   failure that leaves ends the dialog with its action. */
static void done(wc_dlg *c, int rc, const char *code, int at)
{
    if (rc == WF_EFAIL)
        no_form(c);
    else if (rc == WF_LOGIN || rc == WF_QUIT || rc == WF_LOST)
        EndDialog(c->dlg, rc == WF_LOGIN ? RQE_LOGIN :
                          rc == WF_QUIT ? RQE_QUIT : RQE_LOST);
    else if (rc == WF_OK || rc == WF_GONE)
        load(c, code, at);
}

/* The selected entry in the form dialog, or a new one when isnew
   (wcode_form). */
static void edit(wc_dlg *c, int isnew)
{
    ct_row r;
    int at;

    at = selected(c, &r);
    if (isnew)
        r.code[0] = '\0';
    else if (at < 0)
        return;
    done(c, wcode_form(c->dlg, c->l, c->level, c->kind, &r), r.code, at);
}

/* The selected entry deleted (wcode_del). */
static void del(wc_dlg *c)
{
    ct_row r;
    int at;

    at = selected(c, &r);
    if (at >= 0)
        done(c, wcode_del(c->dlg, c->kind, &r), NULL, at);
}

/* As the dialog opens: its caption, its heading over the list in
   columns (wd_head), the dialog widened for a row (wd_fit), the buttons
   that change entries disabled below TUI_LVL_OPER, and the fetch
   posted. */
static void init(wc_dlg *c)
{
    char t[MSG_MAXLEN + 1];
    int id;

    msg_get((u16)(ME_CATS + c->kind), t, (u16)sizeof t);
    OemToAnsi(t, t);
    SetWindowText(c->dlg, t);
    SetDlgItemText(c->dlg, IW_C_CHDR, msg(ME_HDR));
    wd_head(GetDlgItem(c->dlg, IW_C_CHDR));
    wd_fit(c->dlg, IW_C_CROWS, &wc_cols, wc_wide, wc_move);
    if (c->level < TUI_LVL_OPER)
        for (id = IW_C_CNEW; id <= IW_C_CDEL; id++)
            EnableWindow(GetDlgItem(c->dlg, id), FALSE);
    wd_centre(c->dlg);
    PostMessage(c->dlg, WM_COMMAND, WC_LOADID, 0L);
}

/* The command that the list box's key vk stands for, as in every list:
   Ins New, Ctrl+E Edit, Del Delete, F5 the list fetched again; else 0.
   Ins and Del with Shift or Ctrl are Windows' clipboard keys, no
   command. */
static int key_button(UINT vk)
{
    int ctrl;
    int plain;

    ctrl = GetKeyState(VK_CONTROL) < 0;
    plain = !ctrl && GetKeyState(VK_SHIFT) >= 0;
    return vk == VK_INSERT && plain ? IW_C_CNEW :
           vk == 'E' && ctrl ? IW_C_CEDIT :
           vk == VK_DELETE && plain ? IW_C_CDEL :
           vk == VK_F5 ? WC_LOADID : 0;
}

BOOL WCALLBACK wcode_proc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    wc_dlg *c;
    int id;
    int up;

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    c = wc_cur;
    if (wm == WM_INITDIALOG) {
        c->dlg = dlg;
        init(c);
        return TRUE;
    }
    if (wm == WM_MEASUREITEM || wm == WM_DRAWITEM) {
        wd_item(dlg, wm, lp, &wc_cols, &wc_hcols);
        return TRUE;
    }
    if (wm == WM_VKEYTOITEM) {
        id = key_button(wp);
        if (id == 0)
            return -1;
        PostMessage(dlg, WM_COMMAND, id, 0L);
        return -2;
    }
    if (wm != WM_COMMAND)
        return FALSE;
    up = c->level >= TUI_LVL_OPER;
    if (wp == WC_LOADID) {
        id = (int)SendDlgItemMessage(dlg, IW_C_CROWS, LB_GETCURSEL, 0, 0L);
        load(c, NULL, id < 0 ? 0 : id);
    } else if (up && (wp == IW_C_CEDIT ||
                      (wp == IW_C_CROWS && HIWORD(lp) == LBN_DBLCLK))) {
        edit(c, 0);
    } else if (up && wp == IW_C_CNEW) {
        edit(c, 1);
    } else if (up && wp == IW_C_CDEL) {
        del(c);
    } else if (wp == IDCANCEL) {
        EndDialog(dlg, RQE_STAY);
    } else {
        return FALSE;
    }
    return TRUE;
}

u8 wcode_run(HWND owner, link *l, u8 level, u8 kind)
{
    const char *old;
    wc_dlg c;
    HINSTANCE inst;
    FARPROC proc;
    int r;

    if (!ct_open(&c.t))
        return WC_NODIALOG;
    c.l = l;
    c.kind = kind;
    c.level = level;
    inst = (HINSTANCE)GetWindowWord(owner, GWW_HINSTANCE);
    proc = MakeProcInstance((FARPROC)wcode_proc, inst);
    r = -1;
    old = wh_topic(scr_code_defs[kind]->topic);
    if (proc != NULL) {
        wc_cur = &c;
        r = DialogBox(inst, MAKEINTRESOURCE(IW_R_CODES), owner,
                      (DLGPROC)proc);
        wc_cur = NULL;
        FreeProcInstance(proc);
    }
    wh_topic(old);
    ct_close(&c.t);
    return r < 0 ? WC_NODIALOG : (u8)r;
}
