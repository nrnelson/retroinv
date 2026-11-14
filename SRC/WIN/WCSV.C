/*
 * WCSV.C - the export and import of INVWIN (WCSV.H).
 *
 * The dialog sends through wx_send, its wf_ops' send, which gets no
 * window: the busy note (wx_busy) keeps the dialog's for the questions'
 * boxes.  A step that leaves the dialog open but sends nothing more, a
 * question answered No, a stop or a refusal shown in a box, comes back
 * to the dialog as a refusal of FILE (stay), so that it shows its text,
 * or none, on the message line and puts the focus on the file's name.
 */
#include <stdlib.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "RQERR.H"
#include "TUI.H"
#include "SCR.H"
#include "FMODEL.H"
#include "SCRCFORM.H"
#include "WFORM.H"
#include "WFILE.H"
#include "WSCR.H"
#include "WCSV.H"

#define WX_ESCHIT       0x8001  /* GetAsyncKeyState: down now, or pressed
                                   since the last call                  */
#define WX_LINESZ       (SCR_CX_REFW + 1)   /* a refused line kept      */
/* The refused lines' box: MX_T_REFUSED and a blank line, the lines, a
   blank line, MX_REFCOUNT and MX_REFERR, each ended by a line break or
   the NUL. */
#define WX_REFSZ        (LINK_IMPERRS * WX_LINESZ + 2 + 3 * (MSG_MAXLEN + 1))

/* The file's path fits the file name dialog (WFILE.H). */
extern char wx_file_fits[1];
extern char wx_file_fits[(LINK_MAXDIR + 1 <= DOS_PATHMAX) ? 1 : 2];

static const char k_spec[] = "*.CSV";

/* One export or import. */
typedef struct {
    scr_csv_req r;              /* what it sends                        */
    HWND dlg;                   /* the dialog sending it                */
    u32  rows;                  /* an export's records written          */
    int  code;                  /* an import's error after some records
                                   were added, else RQ_OK               */
    char added[SCR_CX_NUMSZ];   /* an import's records added            */
    u8   isexp;                 /* an export, else an import            */
} wx_run;

/* The dialog's busy note (wf_ops): the dialog kept for the questions. */
static void wx_busy(void *arg, HWND dlg, int on)
{
    ((wx_run *)arg)->dlg = dlg;
    ws_form_busy(arg, dlg, on);
}

/* 1 when the user answers Yes to text (CP437), asked over the dialog,
   which is not held busy meanwhile. */
static int ask(wx_run *w, const char *text)
{
    int r;

    ws_form_busy(w, w->dlg, 0);
    r = ws_say_oem(w->dlg, MB_YESNO | MB_ICONQUESTION, text);
    ws_form_busy(w, w->dlg, 1);
    return r == IDYES;
}

/* Leave the dialog open with text (CP437, "" for none) on its message
   line and the focus on the file's name, as a refusal of FILE shows. */
static int stay(rq_res *res, const char *text)
{
    res->code = RQ_EARG;
    res->closed = 0;
    strcpy(res->field, scr_csv_k_file);
    txt_copy(res->text, text, (u16)sizeof res->text);
    return RQ_EARG;
}

/* 1 when Esc is down, or was pressed since the last call, while a window
   of the program is active; else 0. */
static int esc_pressed(void)
{
    return (GetAsyncKeyState(VK_ESCAPE) & WX_ESCHIT) != 0 && ws_active();
}

/* The records written so far, on the frame's help panel (ws_info). */
static void progress(u32 rows)
{
    char num[SCR_CX_NUMSZ];

    txt_u32(num, rows);
    ws_info(msg_sub(MX_PROGRESS, num, NULL));
}

/* ---------------------------------------------------------------------
 * Export.
 * --------------------------------------------------------------------- */

/* Run the export w asks for over l (WCSV.H): RQ_OK once the file is
   written, else the dialog's refusal in res. */
static int run_export(wx_run *w, link *l, rq_res *res)
{
    char cursor[LINK_MAXCURSOR + 1];
    int first;
    int rc;

    esc_pressed();              /* forget an Esc pressed before */
    for (;;) {
        w->rows = 0;
        first = 1;
        progress(w->rows);
        for (;;) {
            rc = scr_csv_send(l, &w->r, 1, first ? NULL : cursor,
                              scr_csv_rows, &w->rows, res);
            if (rc != RQ_OK)
                break;
            progress(w->rows);
            if (res->more[0] == '\0')
                break;
            strcpy(cursor, res->more);
            first = 0;
            if (esc_pressed()) {
                ws_info(NULL);
                return stay(res, msg_sub(MX_STOPPED, w->r.file, NULL));
            }
        }
        ws_info(NULL);
        if (rc == RQ_OK)
            return RQ_OK;
        if (first && !w->r.replace && !res->closed && rc == RQ_EDUP &&
            strcmp(res->field, scr_csv_k_file) == 0) {
            if (!ask(w, msg_sub(MT_REPLACE, w->r.file, NULL)))
                return stay(res, "");
            w->r.replace = 1;
            continue;
        }
        break;
    }
    if (!first && !res->closed && rc == RQ_ECURSOR)
        return stay(res, msg(MX_LOST));
    scr_csv_ferr(res);
    return res->code;
}

/* ---------------------------------------------------------------------
 * Import.
 * --------------------------------------------------------------------- */

/* The refused lines of x, then the error code that followed them unless
   it is RQ_OK, in a message box over the dialog (WCSV.H). */
static void refused(wx_run *w, scr_csv_imp *x, int code)
{
    char *t;
    u16 n;
    u16 i;

    t = (char *)malloc(WX_REFSZ);
    if (t == NULL) {
        ws_say_oem(w->dlg, MB_ICONSTOP, msg(MT_NOMEM));
        return;
    }
    msg_get(MX_T_REFUSED, t, MSG_MAXLEN + 1);
    n = (u16)strlen(t);
    t[n++] = '\n';
    t[n++] = '\n';
    for (i = 0; i < x->n; i++) {
        cmem_get(x->lines, (u16)(i * WX_LINESZ), t + n, WX_LINESZ);
        n = (u16)(n + strlen(t + n));
        t[n++] = '\n';
    }
    t[n++] = '\n';
    if (x->errors[0] == '\0')
        txt_u32(x->errors, x->nr);
    strcpy(t + n, msg_sub(MX_REFCOUNT, x->errors, NULL));
    if (code != RQ_OK) {
        n = (u16)(n + strlen(t + n));
        t[n++] = '\n';
        strcpy(t + n, msg_sub(MX_REFERR, rqe_text(code), NULL));
    }
    OemToAnsi(t, t);
    ws_form_busy(w, w->dlg, 0);
    ws_say(w->dlg, MB_ICONEXCLAMATION, t);
    ws_form_busy(w, w->dlg, 1);
    free(t);
}

/* Run the import w asks for over l, once the user agrees (WCSV.H):
   RQ_OK once records were added, every one or some (w->code), else the
   dialog's refusal in res. */
static int run_import(wx_run *w, link *l, rq_res *res)
{
    scr_csv_imp x;
    int rc;

    if (!ask(w, msg_sub(MX_IMPORTQ, w->r.file, w->r.table)))
        return stay(res, "");
    memset(&x, 0, sizeof x);
    x.lines = cmem_alloc((u16)(LINK_IMPERRS * WX_LINESZ));
    if (x.lines == NULL)
        return stay(res, msg(MT_NOMEM));
    ws_info(msg(MX_WAIT));
    rc = scr_csv_send(l, &w->r, 0, NULL, scr_csv_imp_line, &x, res);
    ws_info(NULL);
    w->code = rc;
    if (!res->closed && x.refused) {
        refused(w, &x, rc);
        rc = stay(res, "");
    } else if (!res->closed && x.partial && rc != RQ_OK) {
        txt_copy(w->added, x.added, (u16)sizeof w->added);
        rc = RQ_OK;
    } else if (rc == RQ_OK) {
        rq_okval(res, scr_csv_k_added, w->added, (u16)sizeof w->added);
    } else {
        scr_csv_ferr(res);
        rc = res->code;
    }
    cmem_free(x.lines);
    return rc;
}

/* The dialog's request (wf_ops' send): the export or import of the
   values of f. */
static int wx_send(void *arg, form *f, link *l, const char *id,
                   rq_res *res)
{
    wx_run *w;

    w = (wx_run *)arg;
    (void)id;
    scr_csv_get(&w->r, f);
    return w->isexp ? run_export(w, l, res) : run_import(w, l, res);
}

static const wf_ops wx_ops = {
    NULL, NULL, 0, wx_busy, NULL, NULL, NULL, wx_send
};

/* ---------------------------------------------------------------------
 * The command.
 * --------------------------------------------------------------------- */

int wcsv_run(HWND owner, link *l, u8 level, int local, u8 cmd)
{
    char file[LINK_MAXDIR + 1];
    char num[SCR_CX_NUMSZ];
    form_field fl[SCR_CX_NFIELDS];
    form_def def;
    wx_run w;
    form *f;
    int r;

    w.isexp = (u8)(cmd == SCMD_EXPORT);
    scr_csv_form(&def, fl, w.isexp);
    if (local) {
        r = wfile_run(owner, def.title, k_spec, file, (u16)sizeof file);
        if (r != WFILE_OK)
            return r == WFILE_CANCEL ? WF_CANCEL : WF_EFAIL;
    }
    if (wf_open(&f, &def, FORM_EDIT) != 0)
        return WF_EFAIL;
    scr_csv_preset(f);
    if (local)
        form_load(f, scr_csv_k_file, file);
    r = wf_run(f, owner, l, level, &wx_ops, &w, NULL, 0);
    form_close(f);
    if (r != WF_OK)
        return r;
    if (w.isexp) {
        txt_u32(num, w.rows);
        ws_say_oem(owner, MB_ICONINFORMATION,
                   msg_sub(MX_EXPORTED, num, w.r.file));
    } else if (w.code == RQ_OK) {
        ws_say_oem(owner, MB_ICONINFORMATION,
                   msg_sub(MX_ADDED, w.added, w.r.table));
    } else {
        ws_say_oem(owner, MB_ICONEXCLAMATION,
                   msg_sub(MX_PARTIAL, w.added, rqe_text(w.code)));
    }
    return WF_OK;
}
