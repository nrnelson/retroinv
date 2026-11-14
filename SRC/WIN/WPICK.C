/*
 * WPICK.C - the pick lists of the Windows client (WPICK.H).
 *
 * The dialog's state (wp_dlg) is on the stack of wp_pick or wp_list,
 * and wp_cur points to it while the dialog runs, for the
 * dialog procedure.  The dialog posts itself the command that fetches
 * its list as it opens, so that it shows before the requests.  The
 * template's list box takes a code list, which comes whole in one
 * response; the locations and a caller's source are paged, so a list
 * window takes the box's place for them.
 */
#define PAGER_CALLS
#define RQERR_CLASS
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "RQERR.H"
#include "TUI.H"
#include "PAGER.H"
#include "CODETAB.H"
#include "SCRKFORM.H"
#include "SCRLFORM.H"
#include "WDLG.H"
#include "WLIST.H"
#include "WCOLS.H"
#include "WROWS.H"
#include "WFSTR.H"
#include "WPICK.H"

/* The characters of a code list's row: the code, a blank and the
   name. */
#define WP_CODEW        (LINK_LTYPELEN + 1 + LINK_CODENAMELEN)

/* The command that fetches the list: no control's ID. */
#define WP_LOADID       (WF_C_PICK + 1)

/* The kind beside those of WPICK.H: a caller's paged source (wp_list).
   The kinds up to WP_LOCS load a ctab, WP_LOCS and WP_LIST are paged. */
#define WP_LIST         4

extern char wp_code_fits[1];
extern char wp_code_fits[(LV_KEYSZ <= WP_CODESZ &&
                          LINK_ITAGLEN <= WP_CODEW) ? 1 : 2];

const char *const wp_tables[] = { "CAT", "LTYPE" };
const char wp_w_locget[] = "LOC GET";
u8 wp_flags;

/* The list of each kind's ctab: its codes, or for WP_LOCS the location
   types. */
static const u8 wp_lists[] = {
    SCR_CK_CAT, SCR_CK_LTYPE, SCR_CK_TAG, SCR_CK_LTYPE
};

typedef struct {
    link *l;
    void (*busy)(void *arg, HWND dlg, int on);
    void *arg;
    char *code;
    const char *at;             /* the row to select                    */
    char miss[LV_KEYSZ];        /* the location row where the seek of at
                                   gave up, or ""                       */
    const char *outside;        /* the location no choice may be in, or
                                   NULL                                 */
    rq_res *res;
    ctab t;                     /* the codes, or the location types     */
    lv_src src;                 /* the paged list's source              */
    char lead[LV_TEXTLEN + 1];  /* and the locations' lead row          */
    wl  *v;                     /* the paged list's window, or NULL     */
    HWND dlg;
    HWND list;                  /* the list box or the list window      */
    char title[MSG_MAXLEN + 1]; /* the caption, in the Windows set      */
    const wcol_tab *cols;       /* the paged list's columns             */
    u8   kind;
    u8   level;
    u8   act;                   /* RQE_STAY, or WL_LEAVE's action       */
} wp_dlg;

static wp_dlg *wp_cur;          /* the dialog running, or NULL          */

/* The rows of the location list (lv_fmt_fn), arg the dialog. */
static void loc_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    wrow_tree(r, &((wp_dlg *)arg)->t, text, key);
}

/* Row i of the list box: its code into code (WP_CODESZ bytes), and, t
   not NULL, its text into t (WP_CODEW + 1 bytes): a tag alone; else the
   code in LINK_LTYPELEN cells, a blank and its name. */
static void row(wp_dlg *c, u16 i, char *code, char *t)
{
    ct_row r;

    ct_get(&c->t, i, &r);
    strcpy(code, r.code);
    if (t != NULL && c->kind == SCR_CK_TAG) {
        strcpy(t, code);
    } else if (t != NULL) {
        memset(t, ' ', LINK_LTYPELEN + 1);
        memcpy(t, code, strlen(code));
        strcpy(t + LINK_LTYPELEN + 1, r.name);
    }
}

/* ct_wait_fn of the tags: wl_wait. */
static int tag_wait(void *arg)
{
    (void)arg;
    return wl_wait();
}

/* Fetch the list: the code list, the tags captioned as whole or not,
   or the location types and then the locations, or a caller's source,
   the caller's row selected, or a code list without rows its one row
   MT_NOCHOICE, unselected; a failed code list ends the dialog with
   WP_FAIL. */
static void load(wp_dlg *c)
{
    char t[WP_CODEW + 1];
    char code[WP_CODESZ];
    u16 sel;
    u16 i;
    int rc;

    if (c->kind <= WP_LOCS) {
        c->busy(c->arg, c->dlg, 1);
        rc = c->kind == SCR_CK_TAG ?
             ct_load_tags(&c->t, c->l, tag_wait, NULL, c->res) :
             ct_load(&c->t, c->l, wp_tables[wp_lists[c->kind]], c->res);
        c->busy(c->arg, c->dlg, 0);
        if (rc != RQ_OK) {
            EndDialog(c->dlg, WP_FAIL);
            return;
        }
    }
    if (c->kind == SCR_CK_TAG) {
        msg_get(c->t.part ? ME_TAGSPART : c->t.n == 0 ? ME_NOTAGS : ME_TAGS,
                c->title, (u16)sizeof c->title);
        OemToAnsi(c->title, c->title);
        SetWindowText(c->dlg, c->title);
    }
    if (c->v != NULL) {
        if (c->at[0] == '\0') {
            wl_move(c->v, PG_HOME);
        } else {
            wl_find(c->v, c->at);
            if (c->kind == WP_LOCS && wl_selected(c->v, c->miss) &&
                strcmp(c->miss, c->at) == 0)
                c->miss[0] = '\0';
        }
        return;
    }
    sel = 0;
    for (i = 0; i < c->t.n; i++) {
        row(c, i, code, t);
        SendMessage(c->list, LB_ADDSTRING, 0, (LPARAM)(LPCSTR)t);
        if (strcmp(code, c->at) == 0)
            sel = i;
    }
    if (c->t.n == 0)
        SendMessage(c->list, LB_ADDSTRING, 0,
                    (LPARAM)(LPCSTR)msg(MT_NOCHOICE));
    else
        SendMessage(c->list, LB_SETCURSEL, sel, 0L);
}

/* 1 with the selected row's code or key in code, or 0 when no row
   is selected. */
static int chosen(wp_dlg *c)
{
    LRESULT i;

    if (c->v != NULL)
        return wl_selected(c->v, c->code);
    i = SendMessage(c->list, LB_GETCURSEL, 0, 0L);
    if (i < 0 || (u16)i >= c->t.n)
        return 0;
    row(c, (u16)i, c->code, NULL);
    return 1;
}

/* The location list's notes (wl_note_fn): its requests in the owner's
   busy bracket; Enter and Esc as OK and Cancel, Tab and Shift-Tab to
   the next and the previous control; a failure that leaves ends the
   dialog. */
static void note(void *arg, wl *v, int ev, UINT val)
{
    wp_dlg *c;

    (void)v;
    c = (wp_dlg *)arg;
    if (ev == WL_BUSY) {
        c->busy(c->arg, c->dlg, (int)val);
    } else if (ev == WL_LEAVE) {
        c->act = (u8)val;
        EndDialog(c->dlg, WP_NONE);
    } else if (val == VK_RETURN || val == VK_ESCAPE) {
        PostMessage(c->dlg, WM_COMMAND, val == VK_RETURN ? IDOK : IDCANCEL,
                    0L);
    } else if (val == VK_TAB) {
        PostMessage(c->dlg, WM_NEXTDLGCTL, GetKeyState(VK_SHIFT) < 0, 0L);
    }
}

/* Put the list w at x, y, cx by cy pixels, OK and Cancel under it
   against its right edge, y below it and x apart, and size the dialog
   dlg around them with margins of x and y. */
static void fit(HWND dlg, HWND w, int x, int y, int cx, int cy)
{
    RECT b;
    RECT d;
    RECT k;
    int bw;
    int bh;
    int bx;
    int by;

    MoveWindow(w, x, y, cx, cy, FALSE);
    GetWindowRect(GetDlgItem(dlg, IDOK), &b);
    bw = b.right - b.left;
    bh = b.bottom - b.top;
    bx = x + cx - bw;
    by = y + cy + y;
    MoveWindow(GetDlgItem(dlg, IDCANCEL), bx, by, bw, bh, FALSE);
    MoveWindow(GetDlgItem(dlg, IDOK), bx - x - bw, by, bw, bh, FALSE);
    GetWindowRect(dlg, &d);
    GetClientRect(dlg, &k);
    SetWindowPos(dlg, NULL, 0, 0, d.right - d.left - k.right + cx + 2 * x,
                 d.bottom - d.top - k.bottom + by + bh + y,
                 SWP_NOMOVE | SWP_NOZORDER);
}

/* As the dialog opens: its caption, its list box in OEM_FIXED_FONT, or
   the paged list made in the box's place, the locations' with their
   lead row, fitted at the box's corner and given the focus, and the
   fetch posted; FALSE, or TRUE once a paged list that could not be made
   has ended the dialog. */
static BOOL init(wp_dlg *c)
{
    TEXTMETRIC tm;
    RECT r;
    POINT p;
    HFONT font;
    HDC dc;
    HWND w;
    int cx;
    int cy;

    SetWindowText(c->dlg, c->title);
    w = GetDlgItem(c->dlg, WF_C_PICK);
    GetWindowRect(w, &r);
    p.x = r.left;
    p.y = r.top;
    ScreenToClient(c->dlg, &p);
    if (c->kind != WP_LOCS && c->kind != WP_LIST) {
        font = (HFONT)GetStockObject(OEM_FIXED_FONT);
        dc = GetDC(c->dlg);
        SelectObject(dc, font);
        GetTextMetrics(dc, &tm);
        ReleaseDC(c->dlg, dc);
        SendMessage(w, WM_SETFONT, (WPARAM)font, FALSE);
        cx = 2 * GetSystemMetrics(SM_CXBORDER) +
             WP_CODEW * tm.tmAveCharWidth + GetSystemMetrics(SM_CXVSCROLL);
        cy = 2 * GetSystemMetrics(SM_CYBORDER) + WP_ROWS * tm.tmHeight;
    } else {
        DestroyWindow(w);
        if (wl_open(&c->v, c->dlg, WF_C_PICK, c->l, &c->src, c->cols,
                    WP_ROWS, c->level, wp_flags, note, c) != PG_OK) {
            EndDialog(c->dlg, WP_ENOMEM);
            return TRUE;
        }
        if (c->kind == WP_LOCS) {
            wl_lead(c->v, "0", c->lead);
            wl_letters(c->v);
        }
        w = wl_hwnd(c->v);
        SetWindowLong(w, GWL_STYLE, GetWindowLong(w, GWL_STYLE) | WS_TABSTOP);
        cx = wl_width(c->dlg, c->cols);
        cy = wl_height(c->dlg, WP_ROWS);
    }
    c->list = w;
    fit(c->dlg, w, p.x, p.y, cx, cy);
    wd_centre(c->dlg);
    PostMessage(c->dlg, WM_COMMAND, WP_LOADID, 0L);
    SetFocus(w);
    return FALSE;
}

int wp_within(link *l, const char *id, const char *outside,
              void (*busy)(void *arg, HWND dlg, int on), void *arg,
              HWND dlg)
{
    char at[LINK_IDLEN + 1];
    scr_locinfo x;
    rq q;
    rq_res res;
    int rc;
    int n;

    txt_copy(at, id, (u16)sizeof at);
    for (n = 0; n <= LINK_MAXLOCDEPTH && strcmp(at, "0") != 0; n++) {
        if (strcmp(at, outside) == 0)
            return 1;
        memset(&x, 0, sizeof x);
        rq_begin(&q, wp_w_locget);
        rq_word(&q, at);
        busy(arg, dlg, 1);
        rc = rq_send(l, &q, scr_loc_info, &x, &res);
        busy(arg, dlg, 0);
        if (rc != RQ_OK)
            return 0;
        txt_copy(at, x.parent, (u16)sizeof at);
    }
    return 0;
}

/* 1 when the user, asked MO_PICKQ over the dialog, No the default,
   chooses the selected location row, named from its name column. */
static int sure(wp_dlg *c)
{
    char t[LV_ROWTEXT + 1];
    char q[MSG_MAXLEN + 1];
    u16 a;
    u16 e;

    wl_chosen(c->v, 0, NULL, t);
    t[WROW_TREEW] = '\0';
    for (a = 0; t[a] == ' '; a++)
        ;
    for (e = (u16)strlen(t); e > a && t[e - 1] == ' '; e--)
        ;
    t[e] = '\0';
    txt_copy(q, msg_sub(MO_PICKQ, t + a, NULL), (u16)sizeof q);
    OemToAnsi(q, q);
    return wd_say(c->dlg, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, q) ==
           IDYES;
}

/* OK or a double click: the choice made, but a location inside the one
   it may not be in (wp_within), MO_INSELF then said over the dialog,
   which stays, and the row where a seek gave up, which only a Yes to
   MO_PICKQ (sure) chooses. */
static void ok(wp_dlg *c)
{
    char t[MSG_MAXLEN + 1];

    if (!chosen(c)) {
        EndDialog(c->dlg, WP_NONE);
    } else if (c->outside != NULL &&
               wp_within(c->l, c->code, c->outside, c->busy, c->arg,
                         c->dlg)) {
        msg_get(MO_INSELF, t, (u16)sizeof t);
        OemToAnsi(t, t);
        wd_say(c->dlg, MB_ICONEXCLAMATION, t);
    } else if (strcmp(c->code, c->miss) != 0 || sure(c)) {
        EndDialog(c->dlg, WP_OK);
    }
}

BOOL WCALLBACK wp_proc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    wp_dlg *c;

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    c = wp_cur;
    if (wm == WM_INITDIALOG) {
        c->dlg = dlg;
        return init(c);
    }
    if (wm != WM_COMMAND)
        return FALSE;
    if (wp == WP_LOADID)
        load(c);
    else if (wp == IDOK || (wp == WF_C_PICK && HIWORD(lp) == LBN_DBLCLK))
        ok(c);
    else if (wp == IDCANCEL)
        EndDialog(dlg, WP_NONE);
    else
        return FALSE;
    return TRUE;
}

/* Start c for kind, every other part empty (RQE_STAY, no codes, no
   list window), captioned with title (wp_pick's), choosing into code. */
static void start(wp_dlg *c, u8 kind, const char *title, char *code)
{
    memset(c, 0, sizeof *c);
    txt_copy(c->title, title, (u16)sizeof c->title);
    OemToAnsi(c->title, c->title);
    c->kind = kind;
    c->code = code;
    c->at = "";
}

/* Run the dialog of c over owner and free its codes; its end, WP_, with
   *act the action of a failure that left a paged list (else
   RQE_STAY). */
static int run(wp_dlg *c, HWND owner, u8 *act)
{
    HINSTANCE inst;
    FARPROC proc;
    int r;

    inst = (HINSTANCE)GetWindowWord(owner, GWW_HINSTANCE);
    proc = MakeProcInstance((FARPROC)wp_proc, inst);
    r = -1;
    if (proc != NULL) {
        wp_cur = c;
        r = DialogBox(inst, MAKEINTRESOURCE(WF_R_PICK), owner,
                      (DLGPROC)proc);
        wp_cur = NULL;
        FreeProcInstance(proc);
    }
    ct_close(&c->t);
    *act = c->act;
    return r < 0 ? WP_ENOMEM : r;
}

int wp_pick(HWND owner, link *l, u8 level, u8 kind, const char *title,
            u16 lead, const char *at, const char *outside,
            void (*busy)(void *arg, HWND dlg, int on), void *arg,
            char *code, rq_res *res, u8 *act)
{
    wp_dlg c;

    *act = RQE_STAY;
    start(&c, kind, title, code);
    if (!ct_open(&c.t))
        return WP_ENOMEM;
    memcpy(&c.src, &scr_loc_src, sizeof c.src);
    c.src.fmt = loc_fmt;
    c.src.arg = &c;
    c.lead[0] = '\0';
    if (kind == WP_LOCS)
        msg_get(lead, c.lead, (u16)sizeof c.lead);
    c.l = l;
    c.busy = busy;
    c.arg = arg;
    c.at = at;
    c.outside = outside;
    c.res = res;
    c.cols = &wrow_tpick_cols;
    c.level = level;
    return run(&c, owner, act);
}

int wp_list(HWND owner, link *l, u8 level, const char *title,
            const lv_src *src, const wcol_tab *cols, const char *at,
            void (*busy)(void *arg, HWND dlg, int on), void *arg,
            char *code, u8 *act)
{
    wp_dlg c;

    start(&c, WP_LIST, title, code);
    memcpy(&c.src, src, sizeof c.src);
    c.l = l;
    c.busy = busy;
    c.arg = arg;
    c.at = at;
    c.cols = cols;
    c.level = level;
    return run(&c, owner, act);
}
