/*
 * WADMIN.C - the commands of INVWIN's Admin menu beside Users (WADMIN.H).
 *
 * A refusal naming DIR and the rows of the sessions are told as INV's
 * SCRADMIN.C tells them: the screens' code stays apart, as sharing it
 * would grow INV, which runs at its memory floor.  The audit log's
 * filter is wa_filter, which the list's requests read (audit_args)
 * while the frame gives them no scope as their arg; beside the users it
 * gives the selected user's name, and they ask for that user's entries.
 */
#define RQERR_CLASS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "RQERR.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "SCR.H"
#include "SCRZFORM.H"
#include "WDLG.H"
#include "WFORM.H"
#include "WCOLS.H"
#include "WROWS.H"
#include "WSCR.H"
#include "WADMIN.H"
#include "INVWIN.H"

/* The directory's value column, after its label's slot (the W= of
   MZ_L_DIR), and the cells it shows, as INV's prompt. */
#define WZ_DIRV         34
#define WZ_DIRW         40
#define WZ_NONE         0xFF    /* a column the F line does not name    */

static const form_field dir_field[] = {
    { scr_adm_k_dir, MZ_L_DIR, 0, 0, WZ_DIRV, FT_CHAR, LINK_MAXDIR, 0,
      FF_REQ | FF_UPPER, WZ_DIRW, NULL }
};

/* Titled by each operation. */
static const form_def dir_def = { 0, NULL, 0, 0, 0, 0, 1, dir_field };

/* The dialogs' part, which saves nothing; the filter's checks REC. */
static const wf_ops ask_ops = { NULL, NULL, 0, ws_form_busy, NULL, NULL };

static int audit_check(void *arg, form *f, u16 *mf);

static const wf_ops filter_ops = {
    NULL, NULL, 0, ws_form_busy, NULL, NULL, audit_check
};

static char wa_filter[SCR_AF_COUNT][SCR_AF_VALSZ];
static const char k_user[] = "USER";    /* AUDIT LIST's filter by user  */

/* The rows of the sessions being read: the place of each column in the
   F line, the list box, and the row of the requester's own, -1 until it
   comes. */
typedef struct {
    u8   at[SCR_SS_NCOLS];
    HWND box;
    int  self;
} wa_sess;

/* t (MSG_MAXLEN + 1 bytes) holding text, CP437, in the Windows set. */
static char *ansi(char *t, const char *text)
{
    txt_copy(t, text, MSG_MAXLEN + 1);
    OemToAnsi(t, t);
    return t;
}

/* Say over w that a dialog could not be made (IW_S_NOFORM), as the
   frame says it of a form. */
static void no_dialog(HWND w)
{
    char t[MSG_MAXLEN + 1];

    LoadString((HINSTANCE)GetWindowWord(w, GWW_HINSTANCE), IW_S_NOFORM, t,
               sizeof t);
    ws_say(w, MB_ICONSTOP, t);
}

/* ---------------------------------------------------------------------
 * The operations on the data files.
 * --------------------------------------------------------------------- */

u8 wadm_op(HWND frame, u8 cmd, int *done)
{
    const scr_adm_op *op;
    char dir[LINK_MAXDIR + 1];
    char num[TXT_U32LEN + 1];
    char t[MSG_MAXLEN + 1];
    form_def def;
    form *f;
    rq q;
    rq_res res;
    int c;
    int r;

    *done = 0;
    op = &scr_adm_ops[cmd - SCMD_BACKUP];
    dir[0] = '\0';
    if (op->title != 0) {
        /* Copied, not assigned: Watcom C 6.5 refuses to assign a const
           struct. */
        memcpy(&def, &dir_def, sizeof def);
        def.title = op->title;
        r = wf_open(&f, &def, FORM_EDIT);
        if (r == 0) {
            r = wf_run(f, frame, NULL, 0, &ask_ops, NULL, NULL, 0);
            if (r == WF_OK)
                form_get_text(f, scr_adm_k_dir, dir, (u16)sizeof dir);
            form_close(f);
        }
        if (r == WF_EFAIL)
            no_dialog(frame);
        if (r != WF_OK)
            return RQE_STAY;
    }
    if (op->ask != 0 &&
        ws_say_oem(frame, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2,
                   msg_sub(op->ask, dir, NULL)) != IDYES)
        return RQE_STAY;
    rq_begin(&q, scr_adm_w_admin);
    rq_word(&q, op->word);
    if (op->title != 0)
        rq_pair(&q, scr_adm_k_dir, dir);
    if (ws_send(frame, &q, NULL, NULL, &res) == RQ_OK) {
        *done = 1;
        rq_okval(&res, "REMOVED", num, (u16)sizeof num);
        ws_say_oem(frame, MB_ICONINFORMATION,
                   msg_sub(op->done, op->title != 0 ? dir : num, NULL));
        return RQE_STAY;
    }
    c = res.code;
    if (res.closed || strcmp(res.field, scr_adm_k_dir) != 0 ||
        (c != RQ_ENOTFOUND && (c < RQ_EIO || c > RQ_EINUSE)))
        return ws_fail(frame, &res);
    ws_say(frame, MB_ICONEXCLAMATION,
           ansi(t, c == RQ_ENOTFOUND ? msg_sub(op->nodir, dir, NULL) :
                                       msg_sub(MZ_DIRERR, dir, rqe_text(c))));
    return RQE_STAY;
}

/* ---------------------------------------------------------------------
 * The audit log.
 * --------------------------------------------------------------------- */

/* The filter's check (wf_ops): REC needs TBL, as INV's form says. */
static int audit_check(void *arg, form *f, u16 *mf)
{
    (void)arg;
    if (form_get(f, scr_audit_k_rec)[0] == '\0' ||
        form_get(f, scr_audit_k_tbl)[0] != '\0')
        return -1;
    *mf = MZ_NEEDTBL;
    return SCR_AF_TBL;
}

/* The list's args: for the scope the frame gives as arg, a user's name
   beside the users, that user's entries; without one, the audit log's
   own list, the filter's values that are not empty. */
static void audit_args(void *arg, rq *q)
{
    int i;

    if (((const char *)arg)[0] != '\0') {
        rq_pair(q, k_user, (const char *)arg);
        return;
    }
    for (i = 0; i < SCR_AF_COUNT; i++)
        if (wa_filter[i][0] != '\0')
            rq_pair(q, scr_audit_fields[i].name, wa_filter[i]);
}

int wadm_filter(HWND owner)
{
    form *f;
    int r;
    int i;

    if (wf_open(&f, &scr_audit_def, FORM_EDIT) != 0)
        return WF_EFAIL;
    r = wf_run(f, owner, NULL, 0, &filter_ops, NULL, NULL, 0);
    if (r == WF_OK)
        for (i = 0; i < SCR_AF_COUNT; i++)
            form_get_text(f, scr_audit_fields[i].name, wa_filter[i],
                          SCR_AF_VALSZ);
    form_close(f);
    return r;
}

/* The audit log's format function (WROWS.H). */
static void audit_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    (void)arg;
    wrow_audit(r, text, key);
}

static const lv_src audit_src = {
    scr_audit_w_list, 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MZ_HDR, MZ_HDR, MZ_HDR, MZ_HDR },
    MZ_EMPTY, scr_audit_k_id, audit_args, audit_fmt, NULL,
    scr_audit_mark
};

const ws_scr wadm_audit = {
    "AUDIT", &audit_src, audit_fmt, &wrow_audit_cols, MZ_T_AUDIT, NULL, NULL,
    NULL, NULL, 0, NULL, NULL, WS_NONEW, NULL, NULL, NULL, NULL, NULL
};

/* ---------------------------------------------------------------------
 * The sessions.
 * --------------------------------------------------------------------- */

/* The value of column i in the R line ln, or "". */
static const char *sess_val(const wa_sess *s, const rq_line *ln, u16 i)
{
    return s->at[i] < ln->n ? ln->v[s->at[i]] : "";
}

/* rq_fn of ADMIN CLIENTS, arg the wa_sess: the columns' places from the
   F line, then each R line's row added to the list box without its
   trailing blanks, as a list window's rows (WLIST.H). */
static int sess_line(void *arg, const rq_line *ln)
{
    wa_sess *s;
    char t[LV_ROWTEXT + 1];
    const char *v;
    char *e;
    u16 at;
    u16 i;
    u16 k;
    int n;

    s = (wa_sess *)arg;
    if (ln->kind == 'F') {
        for (i = 0; i < SCR_SS_NCOLS; i++)
            for (k = 0; k < ln->n; k++)
                if (strcmp(ln->v[k], scr_sess_cols[i].name) == 0)
                    s->at[i] = (u8)k;
        return 0;
    }
    if (ln->kind != 'R')
        return 0;
    t[0] = ' ';
    t[1] = '\0';
    at = 1;
    for (i = 0; i < SCR_SS_SELF; i++) {
        v = sess_val(s, ln, i);
        if (i == SCR_SS_STATE && v[0] >= '0' && v[0] <= '3' && v[1] == '\0')
            v = msg((u16)(MZ_STATE0 + (v[0] - '0')));
        lv_col(t, &at, v, scr_sess_cols[i].w, scr_sess_cols[i].right);
    }
    for (e = t + strlen(t); e > t && e[-1] == ' '; e--)
        ;
    *e = '\0';
    n = (int)SendMessage(s->box, LB_ADDSTRING, 0, (LPARAM)(LPCSTR)t);
    if (sess_val(s, ln, SCR_SS_SELF)[0] == '1')
        s->self = n;
    return 0;
}

/* Fetch the sessions into the list box of the dialog dlg from it, the
   requester's own selected; a failure, shown, ends the dialog with its
   action. */
static void sess_load(HWND dlg)
{
    wa_sess s;
    rq q;
    rq_res res;

    memset(s.at, WZ_NONE, sizeof s.at);
    s.box = GetDlgItem(dlg, IW_C_SROWS);
    s.self = -1;
    SendMessage(s.box, LB_RESETCONTENT, 0, 0L);
    rq_begin(&q, scr_adm_w_admin);
    rq_word(&q, "CLIENTS");
    if (ws_send(dlg, &q, sess_line, &s, &res) == RQ_OK)
        SendMessage(s.box, LB_SETCURSEL, (WPARAM)s.self, 0L);
    else
        EndDialog(dlg, ws_fail(dlg, &res));
}

/* The sessions dialog's controls that wd_fit widens and moves. */
static const int sess_wide[] = { IW_C_SHDR, IW_C_SROWS, 0 };
static const int sess_move[] = { IW_C_REFRESH, IDOK, 0 };

/* The columns of a session's row into *t, over col: scr_sess_cols' but
   SELF, the row's leading blank in the first. */
static void sess_cols(wcol_tab *t, wcol *col)
{
    u8 i;

    for (i = 0; i < SCR_SS_SELF; i++) {
        col[i].cells = scr_sess_cols[i].w;
        col[i].flags = (u8)(scr_sess_cols[i].right ? WCOL_RIGHT : 0);
        col[i].width = 0;
    }
    col[0].cells++;
    t->n = SCR_SS_SELF;
    t->keep = 0;
    t->col = col;
    t->head = 0;
}

/* The sessions dialog: its caption and its header over the rows in
   columns (wd_head, wd_item), fitted to them (wd_fit), and the first
   fetch posted, as INITDIALOG; IW_C_REFRESH, which F5 in the list
   posts, fetches again, IDOK and IDCANCEL end it with RQE_STAY. */
BOOL WCALLBACK wadm_proc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    char t[MSG_MAXLEN + 1];
    wcol col[SCR_SS_SELF];
    wcol_tab tab;

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    if (wm == WM_INITDIALOG) {
        SetWindowText(dlg, ansi(t, msg(MZ_T_SESS)));
        SetDlgItemText(dlg, IW_C_SHDR, msg(MZ_SHDR));
        wd_head(GetDlgItem(dlg, IW_C_SHDR));
        sess_cols(&tab, col);
        wd_fit(dlg, IW_C_SROWS, &tab, sess_wide, sess_move);
        wd_centre(dlg);
        PostMessage(dlg, WM_COMMAND, IW_C_REFRESH, 0L);
        return TRUE;
    }
    if (wm == WM_MEASUREITEM || wm == WM_DRAWITEM) {
        sess_cols(&tab, col);
        wd_item(dlg, wm, lp, &tab, &tab);
        return TRUE;
    }
    if (wm == WM_VKEYTOITEM) {
        if (wp != VK_F5)
            return -1;
        PostMessage(dlg, WM_COMMAND, IW_C_REFRESH, 0L);
        return -2;
    }
    if (wm != WM_COMMAND)
        return FALSE;
    if (wp == IW_C_REFRESH)
        sess_load(dlg);
    else if (wp == IDOK || wp == IDCANCEL)
        EndDialog(dlg, RQE_STAY);
    else
        return FALSE;
    return TRUE;
}

u8 wadm_sessions(HWND frame)
{
    HINSTANCE inst;
    FARPROC proc;
    int r;

    inst = (HINSTANCE)GetWindowWord(frame, GWW_HINSTANCE);
    proc = MakeProcInstance((FARPROC)wadm_proc, inst);
    r = -1;
    if (proc != NULL) {
        r = DialogBoxParam(inst, MAKEINTRESOURCE(IW_R_SESS), frame,
                           (DLGPROC)proc, 0L);
        FreeProcInstance(proc);
    }
    if (r >= 0)
        return (u8)r;
    no_dialog(frame);
    return RQE_STAY;
}
