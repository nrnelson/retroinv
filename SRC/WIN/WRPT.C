/*
 * WRPT.C - the reports and labels of INVWIN (WRPT.H).
 *
 * A run is the form dialog's send: it opens the job and the layout, runs
 * the requests with the run's dialog shown, and leaves in the context
 * how it ended, for wrpt_run to show once the form dialog has ended.
 * The run's dialog is modeless and owned by the form dialog, which the
 * frame holds disabled while the run is under way (ws_form_busy); the
 * frame's yield gives it its keys (ws_modeless), and it is destroyed
 * only once the form dialog takes input again, so that the activation
 * goes back to the form dialog and not to another program.
 *
 * The preview holds in its list box only the rows it shows, read from
 * the file through scr_rv_line, and fills it again at each move; it
 * draws them from the column on its left edge.  The file is a plain
 * file job's, without the printer's codes, so a bin label's code, which
 * prints twice as wide, is known by its place, the first of each
 * label's labelh lines (RPT.H).  Its scroll bars are the dialog's own:
 * setting one's range may show or hide it and size the dialog's client
 * area again, so its layout is done again until it stays as it is, at
 * most WV_FITS times.
 */
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "FMODEL.H"
#include "SCR.H"
#include "PRINT.H"
#include "RPT.H"
#include "RPTRUN.H"
#include "SCRRFORM.H"
#include "WDLG.H"
#include "WFORM.H"
#include "WPICK.H"
#include "WSCR.H"
#include "WRPT.H"

#define WV_FITS         3       /* layouts of the preview for one size  */

/* A run's state and its end, for the form dialog's functions. */
typedef struct {
    rrun u;
    const form_def *def;
    const rpt_geom *g;
    const char *station;
    char path[LINK_LOCPATHLEN + 1];     /* the location's path, for the
                                   subtitle (run_path)                  */
    HWND owner;                 /* the frame                            */
    HWND dlg;                   /* the form dialog, once it is busy     */
    HWND run;                   /* the run's dialog, or NULL            */
    FARPROC thunk;              /* its procedure's instance thunk       */
    u32  lines;                 /* the lines of the SCREEN output       */
    UINT icon;                  /* the note's MB_ICON value             */
    u8   prn;                   /* the PRN_ code a printer stopped on   */
    u8   cancel;                /* the run's Cancel was chosen          */
    u8   view;                  /* the run ends in the preview          */
    char out;                   /* the output: 'P', 'F' or 'S'          */
    char file[SCR_RP_FILEW + 1];
    char scratch[SCR_RP_SCRSZ]; /* the SCREEN output's file             */
    char note[MSG_MAXLEN + 1];  /* how the run ended, in the Windows set,
                                   "" for nothing to say                */
} wr_ctx;

/* The preview's state. */
typedef struct {
    scr_rview v;
    const char *title;          /* in the Windows set                   */
    HWND owner;
    int  lineh;                 /* pixels of a row                      */
    int  cw;                    /* pixels of a character                */
    u16  first;                 /* the line on the first row            */
    u16  left;                  /* the column on the left edge          */
    u16  rows;                  /* rows shown                           */
    u16  cols;                  /* columns shown                        */
    u16  width;                 /* columns of the output's lines        */
    u8   pitch;                 /* lines of a bin label, whose code shows
                                   twice as wide; 0 for other runs      */
    u8   ready;                 /* the list box exists                  */
    u8   filling;               /* fill is setting the scroll bars      */
    u8   resized;               /* the client area changed meanwhile    */
} wr_view;

static wr_ctx *wr_cur;          /* the run under way, for its dialog    */
static wr_view *wv_cur;         /* the preview open                     */

/* ---------------------------------------------------------------------
 * The run.
 * --------------------------------------------------------------------- */

/* Today as "YYYYMMDD" into d (RRUN_DATESZ bytes). */
static void today(char *d)
{
    u16 y;
    u8 m;
    u8 day;

    dos_today(&y, &m, &day);
    txt_u32w(d, y, 4, '0');
    txt_u32w(d + 4, m, 2, '0');
    txt_u32w(d + 6, day, 2, '0');
    d[RRUN_DATESZ - 1] = '\0';
}

/* The report's title, in the Windows set, into t (MSG_MAXLEN + 1
   bytes). */
static void title(const wr_ctx *c, char *t)
{
    msg_get(c->def->title, t, MSG_MAXLEN + 1);
    OemToAnsi(t, t);
}

/* The run ends with text (CP437) in a box with icon. */
static void note(wr_ctx *c, const char *text, UINT icon)
{
    txt_copy(c->note, text, (u16)sizeof c->note);
    OemToAnsi(c->note, c->note);
    c->icon = icon;
}

/* The run ends with the message m naming a file and its DOS error. */
static void note_file(wr_ctx *c, u16 m, const char *path, int cferr)
{
    char num[TXT_U32LEN + 1];

    txt_u32(num, (u32)cferr);
    note(c, msg_sub(m, path, num), MB_ICONEXCLAMATION);
}

/* The run ends with the string ids of the program's (WRSTR.H). */
static void note_str(wr_ctx *c, UINT ids)
{
    LoadString((HINSTANCE)GetWindowWord(c->owner, GWW_HINSTANCE), ids,
               c->note, (int)sizeof c->note);
    c->icon = MB_ICONEXCLAMATION;
}

/* The end of a print job that did not print: cancelled by the user, or
   Windows' printer missing, its driver not loading (PRN_EOFFLINE,
   CPRNW.C) or failed. */
static void note_prn(wr_ctx *c)
{
    if (c->cancel)
        note(c, msg(MQ_CANCELLED), MB_ICONEXCLAMATION);
    else
        note_str(c, c->prn == PRN_ENODEV ? WR_S_NOPRN :
                    c->prn == PRN_EOFFLINE ? WR_S_NODRV : WR_S_PRNERR);
}

/* The printer is missing or has failed (pr_wait_fn): Windows has no
   job to send the bytes again to, so it is cancelled, the status kept
   for note_prn. */
static int wait(void *arg, int status)
{
    ((wr_ctx *)arg)->prn = (u8)status;
    return 0;
}

/* The pages asked for and the records laid out, on the run's dialog. */
static void progress(wr_ctx *c)
{
    char a[TXT_U32LEN + 1];
    char b[TXT_U32LEN + 1];
    char t[MSG_MAXLEN + 1];

    txt_u32(a, c->u.pages);
    txt_u32(b, rpt_count(c->u.r));
    txt_copy(t, msg_sub(MQ_PROGRESS, a, b), (u16)sizeof t);
    OemToAnsi(t, t);
    if (c->run != NULL)
        SetDlgItemText(c->run, WR_C_PROGRESS, t);
}

/* The step of the run: the progress, then the messages waiting, among
   them the run dialog's keys and clicks; while more pages are to come
   its Cancel stops the run. */
static int step(void *arg, int more)
{
    wr_ctx *c;

    c = (wr_ctx *)arg;
    progress(c);
    dos_idle();
    return more && c->cancel;
}

/* The run's dialog: captioned with the report's title; Cancel, or Esc,
   notes the cancel and cancels a print job. */
BOOL WCALLBACK wr_runproc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    char t[MSG_MAXLEN + 1];

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    if (wm == WM_INITDIALOG) {
        wd_centre(dlg);
        title(wr_cur, t);
        SetWindowText(dlg, t);
        return TRUE;
    }
    if (wm == WM_COMMAND && wp == IDCANCEL) {
        wr_cur->cancel = 1;
        prn_cancel();
        return TRUE;
    }
    return FALSE;
}

/* Show the run's dialog over the form dialog; its end is end_run's. */
static void start_run(wr_ctx *c)
{
    HINSTANCE inst;

    inst = (HINSTANCE)GetWindowWord(c->owner, GWW_HINSTANCE);
    c->thunk = MakeProcInstance((FARPROC)wr_runproc, inst);
    if (c->thunk != NULL)
        c->run = CreateDialog(inst, MAKEINTRESOURCE(WR_R_RUN), c->dlg,
                              (DLGPROC)c->thunk);
    ws_modeless = c->run;
}

/* Destroy the run's dialog, if there is one. */
static void end_run(wr_ctx *c)
{
    ws_modeless = NULL;
    if (c->run != NULL)
        DestroyWindow(c->run);
    c->run = NULL;
    if (c->thunk != NULL)
        FreeProcInstance(c->thunk);
    c->thunk = NULL;
}

/* The form dialog's busy bracket: the frame's, the run's dialog, made
   meanwhile, destroyed after it once the form dialog takes input again
   (WRPT.C's header). */
static void busy(void *arg, HWND dlg, int on)
{
    wr_ctx *c;

    c = (wr_ctx *)arg;
    c->dlg = dlg;
    ws_form_busy(arg, dlg, on);
    if (!on)
        end_run(c);
}

/* The form dialog's check (wf_ops): FILE output needs a file name that
   scr_rpt_file_bad takes, and one that exists is replaced only once the
   user agrees; else the focus goes to the file, with that function's
   message or, after No, nothing to say. */
static int check(void *arg, form *f, u16 *mf)
{
    wr_ctx *c;
    char q[MSG_MAXLEN + 1];
    const char *file;
    int h;
    int i;

    c = (wr_ctx *)arg;
    c->out = form_get(f, scr_rpt_k_out)[0];
    if (c->out != 'F')
        return -1;
    i = c->def->nfields - 1;
    file = form_get(f, scr_rpt_k_file);
    *mf = scr_rpt_file_bad(file);
    if (*mf != 0)
        return i;
    if (cf_open(file, CF_READ | CF_DENYNONE, &h) != 0)
        return -1;
    cf_close(h);
    txt_copy(q, msg_sub(MT_REPLACE, file, NULL), (u16)sizeof q);
    OemToAnsi(q, q);
    if (wd_say(GetActiveWindow(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2,
               q) == IDYES)
        return -1;
    *mf = 0;
    return i;
}

/* Open the run's job for its output, then its layout; 1, or 0 with the
   run's end noted and nothing left open. */
static int open_job(wr_ctx *c)
{
    int rc;
    int cferr;

    if (c->out == 'P')
        rc = pr_open(&c->u.p, PR_PRINTER, PR_EPSON, PRN_LPT1, NULL, wait, c,
                     &cferr);
    else if (c->out == 'F')
        rc = pr_open(&c->u.p, PR_FILE, PR_PLAIN, 0, c->file, NULL, NULL,
                     &cferr);
    else
        rc = scr_rpt_screen(&c->u, c->scratch, c->station, &cferr);
    if (rc == PR_ECANCEL) {
        note_prn(c);
    } else if (rc == PR_EFILE && c->out == 'S') {
        note(c, msg_sub(MQ_SCRATCH, c->scratch, NULL), MB_ICONEXCLAMATION);
    } else if (rc == PR_EFILE) {
        note_file(c, MQ_FILEERR, c->file, cferr);
    } else if (rc != PR_OK ||
               scr_rpt_layout(&c->u, c->g, c->path) != PR_OK) {
        pr_close(c->u.p, &cferr);
        if (rc == PR_OK && c->out == 'S')
            cf_remove(c->scratch);
        note(c, msg(MT_NOMEM), MB_ICONEXCLAMATION);
    }
    if (c->u.r != NULL)
        return 1;
    rrun_close(&c->u, RQ_OK);
    return 0;
}

/* The path of the run's location into c->path, as INV's subtitle gives
   it: "" for none or for labels, MO_NOLOC's text for "0", else the one
   LOC GET gives.  RQ_OK, or the failed request's code, res its
   response. */
static int run_path(wr_ctx *c, link *l, rq_res *res)
{
    scr_locinfo x;
    rq q;
    int rc;

    c->path[0] = '\0';
    if (c->u.loc[0] == '\0' ||
        (c->u.kind != RPT_LIST && c->u.kind != RPT_VALUE))
        return RQ_OK;
    if (strcmp(c->u.loc, "0") == 0) {
        msg_get(MO_NOLOC, c->path, (u16)sizeof c->path);
        return RQ_OK;
    }
    memset(&x, 0, sizeof x);
    rq_begin(&q, wp_w_locget);
    rq_word(&q, c->u.loc);
    rc = rq_send(l, &q, scr_loc_info, &x, res);
    if (rc == RQ_OK)
        strcpy(c->path, x.path);
    return rc;
}

/* The form dialog's send (wf_ops): run the report the form describes
   over l, as WRPT.H says.  RQ_OK when it ran, its end noted, whatever
   the output did; else the failed request's code, res its failure, the
   job closed and the scratch file removed. */
static int send(void *arg, form *f, link *l, const char *id, rq_res *res)
{
    char num[TXT_U32LEN + 1];
    wr_ctx *c;
    u32 count;
    int rc;
    int prc;
    int cferr;

    c = (wr_ctx *)arg;
    (void)id;
    scr_rpt_values(&c->u, f, c->file);
    rc = run_path(c, l, res);
    if (rc != RQ_OK) {
        scr_rpt_failed(&c->u, rc, res);
        return rc;
    }
    if (c->u.date[0] == '\0')
        today(c->u.date);
    c->cancel = 0;
    c->prn = PRN_OK;
    if (rrun_open(&c->u) != PR_OK) {
        note(c, msg(MT_NOMEM), MB_ICONEXCLAMATION);
        return RQ_OK;
    }
    if (!open_job(c))
        return RQ_OK;
    wr_cur = c;
    start_run(c);
    progress(c);
    rc = rrun_go(&c->u, l, step, c, res);
    count = rrun_close(&c->u, rc);
    c->lines = pr_lines(c->u.p);
    prc = pr_close(c->u.p, &cferr);
    if (rc != RQ_OK) {
        if (c->out == 'S')
            cf_remove(c->scratch);
        scr_rpt_failed(&c->u, rc, res);
        return rc;
    }
    if (c->out == 'P' && (c->cancel || prc != PR_OK)) {
        note_prn(c);
    } else if (prc == PR_EFILE) {
        note_file(c, MQ_FILEERR, c->out == 'F' ? c->file : c->scratch,
                  cferr);
    } else if (c->u.stopped) {
        note(c, msg(MQ_STOPPED), MB_ICONEXCLAMATION);
    } else if (c->out == 'S' && (count > 0 || c->u.kind < RPT_ILABEL)) {
        c->view = 1;
        return RQ_OK;
    } else {
        txt_u32(num, count);
        note(c, msg_sub(MQ_DONE, num, NULL), MB_ICONINFORMATION);
    }
    if (c->out == 'S')
        cf_remove(c->scratch);
    return RQ_OK;
}

static const wf_ops wr_ops = {
    NULL, NULL, 0, busy, NULL, NULL, check, send
};

/* ---------------------------------------------------------------------
 * The preview.
 * --------------------------------------------------------------------- */

/* The most first or left may be: the last rows or columns shown. */
static u16 last_line(const wr_view *w)
{
    return w->v.n > w->rows ? (u16)(w->v.n - w->rows) : 0;
}

static u16 last_col(const wr_view *w)
{
    return w->width > w->cols ? (u16)(w->width - w->cols) : 0;
}

/* Show the rows from first in the list box, and the scroll bars where
   they are. */
static void fill(HWND dlg, wr_view *w)
{
    char line[RPT_WIDTH + 1];
    HWND box;
    u16 i;

    if (w->first > last_line(w))
        w->first = last_line(w);
    if (w->left > last_col(w))
        w->left = last_col(w);
    box = GetDlgItem(dlg, WR_C_LINES);
    SendMessage(box, WM_SETREDRAW, FALSE, 0L);
    SendMessage(box, LB_RESETCONTENT, 0, 0L);
    for (i = 0; i < w->rows; i++) {
        if (scr_rv_line(&w->v, (u16)(w->first + i), line) == SCR_RV_END)
            break;
        SendMessage(box, LB_ADDSTRING, 0, (LPARAM)(LPCSTR)line);
    }
    SendMessage(box, WM_SETREDRAW, TRUE, 0L);
    InvalidateRect(box, NULL, TRUE);
    w->filling = 1;
    SetScrollRange(dlg, SB_VERT, 0, last_line(w), FALSE);
    SetScrollPos(dlg, SB_VERT, w->first, TRUE);
    SetScrollRange(dlg, SB_HORZ, 0, last_col(w), FALSE);
    SetScrollPos(dlg, SB_HORZ, w->left, TRUE);
    w->filling = 0;
}

/* Fit the list box, the rows and the columns to the client area, and
   fill it, again while the scroll bars change the area. */
static void layout(HWND dlg, wr_view *w)
{
    RECT r;
    int k;

    for (k = 0; k < WV_FITS; k++) {
        w->resized = 0;
        GetClientRect(dlg, &r);
        MoveWindow(GetDlgItem(dlg, WR_C_LINES), 0, 0, r.right, r.bottom,
                   TRUE);
        w->rows = (u16)(r.bottom > w->lineh ? r.bottom / w->lineh : 1);
        w->cols = (u16)(r.right / w->cw);
        fill(dlg, w);
        if (!w->resized)
            break;
    }
}

/* Place the preview where its owner, the frame, is, in as much of it as
   the screen shows, or on the whole screen when none of it is: it takes
   the frame's place, its caption over the frame's, as INV's viewer takes
   the screen.  Over the frame's client area alone it would sit under a
   second caption and menu bar, and show fewer rows and columns. */
static void place(HWND dlg, HWND owner)
{
    RECT f;
    RECT s;
    RECT r;

    GetWindowRect(owner, &f);
    SetRect(&s, 0, 0, GetSystemMetrics(SM_CXSCREEN),
            GetSystemMetrics(SM_CYSCREEN));
    if (!IntersectRect(&r, &f, &s))
        r = s;
    MoveWindow(dlg, r.left, r.top, r.right - r.left, r.bottom - r.top,
               FALSE);
}

/* Move the rows (bar SB_VERT) or the columns (SB_HORZ) as the scroll
   bar's code says, pos the thumb's place, as INV's viewer moves. */
static void scroll(HWND dlg, wr_view *w, int bar, UINT code, u16 pos)
{
    u16 *at;
    u16 page;
    u16 max;

    at = bar == SB_VERT ? &w->first : &w->left;
    page = bar == SB_VERT ? w->rows : w->cols;
    max = bar == SB_VERT ? last_line(w) : last_col(w);
    switch (code) {
    case SB_LINEUP:
        if (*at > 0)
            (*at)--;
        break;
    case SB_LINEDOWN:
        if (*at < max)
            (*at)++;
        break;
    case SB_PAGEUP:
        *at = *at > page ? (u16)(*at - page) : 0;
        break;
    case SB_PAGEDOWN:
        *at = (u16)(*at + page) < max ? (u16)(*at + page) : max;
        break;
    case SB_TOP:
        *at = 0;
        break;
    case SB_BOTTOM:
        *at = max;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        *at = pos;
        break;
    default:
        return;
    }
    fill(dlg, w);
}

/* The preview's keys, the scroll bar and code each makes. */
static const struct {
    u8   vk;
    u8   bar;
    u8   code;
} wv_keys[] = {
    { VK_UP, SB_VERT, SB_LINEUP }, { VK_DOWN, SB_VERT, SB_LINEDOWN },
    { VK_PRIOR, SB_VERT, SB_PAGEUP }, { VK_NEXT, SB_VERT, SB_PAGEDOWN },
    { VK_HOME, SB_VERT, SB_TOP }, { VK_END, SB_VERT, SB_BOTTOM },
    { VK_LEFT, SB_HORZ, SB_LINEUP }, { VK_RIGHT, SB_HORZ, SB_LINEDOWN }
};

/* Draw n characters of text at x, y on dc in OEM_FIXED_FONT and the
   window's colours, clipped to r and filled behind them. */
static void text_out(HDC dc, int x, int y, const RECT FAR *r,
                     const char *text, int n)
{
    SelectObject(dc, GetStockObject(OEM_FIXED_FONT));
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
    ExtTextOut(dc, x, y, ETO_OPAQUE | ETO_CLIPPED, r, text, n, NULL);
}

/* Draw the row of d from column left, its selection and focus left out.
   A bin label's code is drawn again on a bitmap and stretched twice as
   wide over the row, as Windows 3.0 makes no OEM font twice as wide;
   without the memory for it, it stays as wide as the other rows. */
static void draw(const wr_view *w, const DRAWITEMSTRUCT FAR *d)
{
    char text[RPT_WIDTH + 1];
    RECT r;
    HDC mem;
    HBITMAP bm;
    HGDIOBJ old;
    int x;
    int n;

    if (d->itemID == (UINT)-1 || d->itemAction == ODA_FOCUS)
        return;
    n = (int)SendMessage(d->hwndItem, LB_GETTEXT, d->itemID,
                         (LPARAM)(LPSTR)text);
    x = d->rcItem.left - (int)w->left * w->cw;
    text_out(d->hDC, x, d->rcItem.top, &d->rcItem, text, n);
    if (w->pitch == 0 || (w->first + d->itemID) % w->pitch != 0)
        return;
    SetRect(&r, 0, 0, n * w->cw, w->lineh);
    mem = CreateCompatibleDC(d->hDC);
    bm = CreateCompatibleBitmap(d->hDC, r.right, r.bottom);
    if (mem != NULL && bm != NULL) {
        old = SelectObject(mem, bm);
        text_out(mem, 0, 0, &r, text, n);
        StretchBlt(d->hDC, x, d->rcItem.top, 2 * r.right, r.bottom, mem, 0,
                   0, r.right, r.bottom, SRCCOPY);
        SelectObject(mem, old);
    }
    if (bm != NULL)
        DeleteObject(bm);
    if (mem != NULL)
        DeleteDC(mem);
}

BOOL WCALLBACK wr_viewproc(HWND dlg, UINT wm, WPARAM wp, LPARAM lp)
{
    wr_view *w;
    int i;

    if (WD_LOOKS(wm))
        return wd_look(dlg, wm, wp, lp);
    w = wv_cur;
    switch (wm) {
    case WM_INITDIALOG:
        SetWindowText(dlg, w->title);
        place(dlg, w->owner);
        w->ready = 1;
        layout(dlg, w);
        return TRUE;
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lp)->itemHeight = (UINT)w->lineh;
        return TRUE;
    case WM_DRAWITEM:
        draw(w, (const DRAWITEMSTRUCT FAR *)lp);
        return TRUE;
    case WM_SIZE:
        if (w->filling)
            w->resized = 1;
        else if (w->ready && wp != SIZE_MINIMIZED)
            layout(dlg, w);
        return TRUE;
    case WM_VSCROLL:
    case WM_HSCROLL:
        scroll(dlg, w, wm == WM_VSCROLL ? SB_VERT : SB_HORZ, wp,
               (u16)LOWORD(lp));
        return TRUE;
    case WM_VKEYTOITEM:
        for (i = 0; i < (int)(sizeof wv_keys / sizeof wv_keys[0]); i++)
            if (wv_keys[i].vk == wp)
                scroll(dlg, w, wv_keys[i].bar, wv_keys[i].code, 0);
        return -2;
    case WM_COMMAND:
        if (wp == IDOK || wp == IDCANCEL)
            EndDialog(dlg, 0);
        else if (HIWORD(lp) == LBN_SELCHANGE)
            SendMessage((HWND)LOWORD(lp), LB_SETCURSEL, (WPARAM)-1, 0L);
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

/* Show the SCREEN output of run c in the preview over its frame. */
static void preview(wr_ctx *c)
{
    char cap[MSG_MAXLEN + 1];
    char num[TXT_U32LEN + 1];
    wr_view w;
    TEXTMETRIC tm;
    HINSTANCE inst;
    FARPROC thunk;
    HDC dc;
    int rc;

    memset(&w, 0, sizeof w);
    title(c, cap);
    w.title = cap;
    w.owner = c->owner;
    w.width = c->u.kind < RPT_ILABEL ? RPT_WIDTH : c->g->labelw;
    rc = scr_rv_open(&w.v, c->scratch, c->lines);
    if (rc == CF_ENOMEM)
        note(c, msg(MT_NOMEM), MB_ICONEXCLAMATION);
    else if (rc != 0)
        note_file(c, MQ_FILEERR, c->scratch, rc);
    if (rc != 0)
        return;
    if (w.v.cut) {
        txt_u32(num, w.v.n);
        ws_say_oem(c->owner, MB_ICONINFORMATION,
                   msg_sub(MQ_VSHOWN, num, NULL));
    }
    dc = GetDC(c->owner);
    SelectObject(dc, GetStockObject(OEM_FIXED_FONT));
    GetTextMetrics(dc, &tm);
    ReleaseDC(c->owner, dc);
    w.lineh = tm.tmHeight;
    w.cw = tm.tmAveCharWidth;
    if (c->u.kind == RPT_BLABEL)
        w.pitch = c->g->labelh;
    inst = (HINSTANCE)GetWindowWord(c->owner, GWW_HINSTANCE);
    thunk = MakeProcInstance((FARPROC)wr_viewproc, inst);
    rc = -1;
    if (thunk != NULL) {
        wv_cur = &w;
        rc = DialogBox(inst, MAKEINTRESOURCE(WR_R_VIEW), c->owner,
                       (DLGPROC)thunk);
        wv_cur = NULL;
        FreeProcInstance(thunk);
    }
    if (rc == -1)
        note(c, msg(MT_NOMEM), MB_ICONEXCLAMATION);
    scr_rv_close(&w.v);
}

/* ---------------------------------------------------------------------
 * The interface.
 * --------------------------------------------------------------------- */

int wrpt_run(HWND owner, link *l, u8 level, u8 cmd, const rpt_geom *g,
             const char *station)
{
    char cap[MSG_MAXLEN + 1];
    wr_ctx c;
    wf_ops ops;
    form_def def;
    form_field fields[SCR_RP_MAXF];
    form *f;
    int r;

    memset(&c, 0, sizeof c);
    c.u.kind = (u8)(cmd - SCMD_RPT_LIST + RPT_LIST);
    c.g = g;
    c.station = station;
    c.owner = owner;
    c.def = &def;
    scr_rpt_form(&def, fields, c.u.kind);
    memcpy(&ops, &wr_ops, sizeof ops);
    ops.loc = c.u.kind == RPT_BLABEL ? &scr_rpt_bloc : &scr_rpt_loc;
    if (wf_open(&f, &def, FORM_EDIT) != 0)
        return WF_EFAIL;
    scr_rpt_defaults(f);
    for (;;) {
        c.view = 0;
        c.note[0] = '\0';
        r = wf_run(f, owner, l, level, &ops, &c, NULL, 0);
        if (r != WF_OK)
            break;
        if (c.view) {
            preview(&c);
            cf_remove(c.scratch);
        }
        if (c.note[0] != '\0') {
            title(&c, cap);
            MessageBox(owner, c.note, cap, MB_OK | c.icon);
        }
    }
    form_close(f);
    return r == WF_GONE ? WF_CANCEL : r;
}
