/*
 * SCRRPT.C - the report and label screens of the full-screen client
 * (SCR.H).
 *
 * Each command opens the options form of its kind (scr_rpt_form of
 * SCRRFORM.H): the filters of its report or label run, and the output,
 * PRINTER, FILE (the file named in the form) or SCREEN.  Enter runs it;
 * the form stays open for another run until Esc.  The values are not a
 * record, so the form leaves on Esc and the global keys at once, without
 * asking (form_noask).
 *
 * The run is RPTRUN.H's, the filters read as SCRRFORM.H says and the
 * date, when none is typed, today (scr_date).  Between the requests the
 * status line shows the progress, and an Esc typed meanwhile
 * (tui_esc_waiting) stops the run there.  An ITEM GET or LOC GET whose
 * record does not exist is reported on the field that named it.
 *
 * The output goes through a pr job (PRINT.H): the printer and its type
 * from the settings, a printer that does not take a byte asked about
 * with Retry or Cancel; or a file, whose name must be one
 * scr_rpt_file_bad takes, created after asking when it exists; or, for
 * SCREEN, the station's scratch file (scr_rpt_screen), shown by the
 * viewer when the run is complete and removed afterwards.
 *
 * The viewer reads the file through scr_rv_open, the count of its lines
 * taken from the job (pr_lines), a page end showing as a rule.  When
 * memory is short it shows the first lines only, saying so; below that
 * it says memory has run out.
 */
#define SCRSHR_LOCF
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "FORM.H"
#include "PRINT.H"
#include "RPT.H"
#include "RPTRUN.H"
#include "SCR.H"
#include "SCRSHR.H"
#include "SCRRFORM.H"

#define MQ_HINT         1005    /* hints of the form                    */
#define MQ_HINTV        1006    /* hints of the viewer                  */
#define MQ_Q_NODEV      1020    /* printer questions: PRN_ENODEV        */
#define MQ_Q_PAPER      1021    /* PRN_EPAPER                           */
#define MQ_Q_NOTREADY   1022    /* the other PRN_E codes                */

/* Today's date fits the run's. */
extern char rr_date_fits[1];
extern char rr_date_fits[(SCR_DATESZ <= RRUN_DATESZ) ? 1 : 2];

/* A report line fits a screen row of the viewer. */
extern char rv_line_fits[1];
extern char rv_line_fits[(RPT_WIDTH <= VID_COLS) ? 1 : 2];

/* One run of the screen: the form's values the run takes, and those
   the screen keeps. */
typedef struct {
    const form_def *def;
    rrun u;
    u8   leave;                 /* a pick list ended the session: the
                                   command to leave with                */
    char file[SCR_RP_FILEW + 1];
    char scratch[SCR_RP_SCRSZ]; /* the SCREEN output's file             */
} rr_ctx;

/* ---------------------------------------------------------------------
 * Small helpers.
 * --------------------------------------------------------------------- */

/* Message m on the hint row, as an error. */
static void fail(u16 m)
{
    tui_msg(msg(m), 1);
}

/* A message naming a file and a DOS error. */
static void file_msg(u16 m, const char *path, int cferr)
{
    char num[TXT_U32LEN + 1];

    txt_u32(num, (u32)cferr);
    tui_msg(msg_sub(m, path, num), 1);
}

/* The pages asked for and the records laid out, on the status line. */
static void progress(rr_ctx *c)
{
    char a[TXT_U32LEN + 1];
    char b[TXT_U32LEN + 1];

    txt_u32(a, c->u.pages);
    txt_u32(b, rpt_count(c->u.r));
    tui_info(msg_sub(MQ_PROGRESS, a, b));
    vid_flush();
}

/* The printer did not take a byte: ask, then show the progress again
   over what the question left. */
static int rr_wait(void *arg, int status)
{
    rr_ctx *c;
    int ok;

    c = (rr_ctx *)arg;
    ok = tui_confirm(msg((u16)(status == PRN_ENODEV ? MQ_Q_NODEV :
                               status == PRN_EPAPER ? MQ_Q_PAPER :
                               MQ_Q_NOTREADY)));
    if (c->u.r != NULL)
        progress(c);
    return ok;
}

/* The step of the run: the progress, and while more pages are to come
   an Esc typed meanwhile stops it. */
static int step(void *arg, int more)
{
    progress((rr_ctx *)arg);
    return more && tui_esc_waiting();
}

/* The location field of the form of run u. */
static const scr_locf *rr_loc(const rrun *u)
{
    return u->kind == RPT_BLABEL ? &scr_rpt_bloc : &scr_rpt_loc;
}

/* F4 on a category, location or tags field, or a location typed: the
   pick list or the path, then the form's hints again; a failure that
   ends the screen stops the form. */
static int rr_pick(void *arg, form *f, u16 field, char *val)
{
    rr_ctx *c;
    const form_field *d;

    c = (rr_ctx *)arg;
    d = &c->def->fields[field];
    if (d->name == rrun_k_cat)
        return scr_pick_field(f, SCR_CK_CAT, val, MQ_HINT, &c->leave);
    if (d->name == rrun_k_tag)
        return scr_pick_tag(f, val, d->width, MQ_HINT, &c->leave);
    return scr_loc_field(f, rr_loc(&c->u), NULL, val, MQ_HINT, &c->leave);
}

/* ---------------------------------------------------------------------
 * The viewer.
 * --------------------------------------------------------------------- */

/* Lines first.. of the file of v under title, a page end as a rule. */
static void rv_draw(scr_rview *v, u16 first, const char *title)
{
    char line[VID_COLS + 1];
    u16 i;
    int kind;

    memset(line, TF_SH, VID_COLS);
    line[VID_COLS] = '\0';
    tui_textn(TUI_TOP, 0, line, VID_COLS, TA_FRAME);
    tui_center(TUI_TOP, 0, VID_COLS, title, TA_TITLE);
    for (i = 0; i < SCR_RV_ROWS; i++) {
        kind = scr_rv_line(v, (u16)(first + i), line);
        tui_textn((u8)(TUI_TOP + 1 + i), 0, line, VID_COLS,
                  (u8)(kind == SCR_RV_RULE ? TA_FRAME : TA_NORMAL));
    }
}

/* Show the file path under title until Esc, lines being the count of
   its lines (pr_lines of the job that wrote it); the command to leave
   with. */
static u8 view(const char *path, const char *title, u32 lines)
{
    char t[FORM_TITLEMAX + 3];
    char num[TXT_U32LEN + 1];
    const char *oldtopic;
    scr_rview v;
    u16 first;
    u16 last;
    u16 k;
    int rc;
    int used;
    u8 cmd;

    first = 0;
    t[0] = ' ';
    strncpy(t + 1, title, FORM_TITLEMAX);
    t[FORM_TITLEMAX + 1] = '\0';
    strcat(t, " ");
    rc = scr_rv_open(&v, path, lines);
    if (rc != 0) {
        if (rc == CF_ENOMEM)
            fail(MT_NOMEM);
        else
            file_msg(MQ_FILEERR, path, rc);
        return TUI_CMD_NONE;
    }
    last = v.n > SCR_RV_ROWS ? (u16)(v.n - SCR_RV_ROWS) : 0;
    oldtopic = tui_topic("VIEWER");
    tui_hint(msg(MQ_HINTV));
    if (v.cut) {
        txt_u32(num, v.n);
        tui_msg(msg_sub(MQ_VSHOWN, num, NULL), 0);
    }
    cmd = TUI_CMD_NONE;
    for (;;) {
        rv_draw(&v, first, t);
        k = tui_key();
        if (k == KEY_ESC)
            break;
        if (k == KEY_UP) {
            if (first > 0)
                first--;
        } else if (k == KEY_DOWN) {
            if (first < last)
                first++;
        } else if (k == KEY_PGUP) {
            first = first > SCR_RV_ROWS ? (u16)(first - SCR_RV_ROWS) : 0;
        } else if (k == KEY_PGDN) {
            first = (u16)(first + SCR_RV_ROWS) < last ?
                    (u16)(first + SCR_RV_ROWS) : last;
        } else if (k == KEY_HOME) {
            first = 0;
        } else if (k == KEY_END) {
            first = last;
        } else {
            cmd = tui_global(k, &used);
            if (used)
                break;
            cmd = TUI_CMD_NONE;
        }
    }
    tui_topic(oldtopic);
    scr_rv_close(&v);
    tui_clear(TUI_TOP, TUI_BOTTOM);
    return cmd;
}

/* ---------------------------------------------------------------------
 * A run from the form.
 * --------------------------------------------------------------------- */

/* The path of the run's location for a report's subtitle into path
   (LINK_LOCPATHLEN + 1 bytes): "" for none or for labels, MO_NOLOC's
   text for "0", else that of LOC GET.  RQ_OK, or the code of the failed
   request with res its response. */
static int run_path(const rrun *u, char *path, rq_res *res)
{
    scr_locinfo x;
    rq q;
    int rc;

    path[0] = '\0';
    if (u->loc[0] == '\0' || (u->kind != RPT_LIST && u->kind != RPT_VALUE))
        return RQ_OK;
    if (strcmp(u->loc, "0") == 0) {
        msg_get(MO_NOLOC, path, LINK_LOCPATHLEN + 1);
        return RQ_OK;
    }
    memset(&x, 0, sizeof x);
    rq_begin(&q, "LOC GET");
    rq_word(&q, u->loc);
    rc = rq_send(tui_s.l, &q, scr_loc_info, &x, res);
    if (rc == RQ_OK)
        strcpy(path, x.path);
    return rc;
}

/* Enter: run the report or labels the form describes, into the job of its
   output: for 'P' the printer, for 'F' c->file, for 'S' the station's
   scratch file, whose name goes into c->scratch.  The command to
   return. */
static u8 go(rr_ctx *c, form *f)
{
    char path[LINK_LOCPATHLEN + 1];
    char num[TXT_U32LEN + 1];
    rq_res res;
    rpt_geom g;
    u32 count;
    u32 lines;
    int rc;
    int prc;
    int cferr;
    int h;
    u16 bad;
    char out;
    u8 cmd;

    scr_rpt_values(&c->u, f, c->file);
    if (c->u.date[0] == '\0')
        scr_date(c->u.date);
    out = form_get(f, scr_rpt_k_out)[0];
    if (out == 'F') {
        bad = scr_rpt_file_bad(c->file);
        if (bad != 0) {
            form_field_error(f, scr_rpt_k_file, msg(bad));
            return TUI_CMD_NONE;
        }
        if (cf_open(c->file, CF_READ | CF_DENYNONE, &h) == 0) {
            cf_close(h);
            if (!tui_confirm(msg_sub(MT_REPLACE, c->file, NULL)))
                return TUI_CMD_NONE;
        }
    }
    rc = run_path(&c->u, path, &res);
    if (rc != RQ_OK) {
        scr_rpt_failed(&c->u, rc, &res);
        return form_error(f, &res);
    }
    if (rrun_open(&c->u) != PR_OK) {
        fail(MT_NOMEM);
        return TUI_CMD_NONE;
    }
    g.pagelen = scr_cfg.pagelen;
    g.labelw = scr_cfg.labelw;
    g.labelh = scr_cfg.labelh;
    if (out == 'P')
        rc = pr_open(&c->u.p, PR_PRINTER, scr_cfg.prtype, scr_cfg.prport,
                     NULL, rr_wait, c, &cferr);
    else if (out == 'F')
        rc = pr_open(&c->u.p, PR_FILE, PR_PLAIN, 0, c->file, NULL, NULL,
                     &cferr);
    else
        rc = scr_rpt_screen(&c->u, c->scratch, tui_s.station, &cferr);
    if (rc == PR_ECANCEL) {
        fail(MQ_CANCELLED);
    } else if (rc == PR_EFILE && out == 'S') {
        tui_msg(msg_sub(MQ_SCRATCH, c->scratch, NULL), 1);
    } else if (rc == PR_EFILE) {
        file_msg(MQ_FILEERR, c->file, cferr);
    } else if (rc != PR_OK || scr_rpt_layout(&c->u, &g, path) != PR_OK) {
        pr_close(c->u.p, &cferr);
        if (rc == PR_OK && out == 'S')
            cf_remove(c->scratch);
        fail(MT_NOMEM);
    }
    if (c->u.r == NULL) {
        tui_hint_keep(msg(MQ_HINT));
        rrun_close(&c->u, RQ_OK);
        return TUI_CMD_NONE;
    }
    progress(c);
    rc = rrun_go(&c->u, tui_s.l, step, c, &res);
    /* Drop the keys typed during the run: an Esc typed on the last page
       must not also close the form. */
    tui_esc_waiting();
    count = rrun_close(&c->u, rc);
    lines = pr_lines(c->u.p);
    prc = pr_close(c->u.p, &cferr);
    tui_info("");
    tui_hint(msg(MQ_HINT));
    txt_u32(num, count);
    cmd = TUI_CMD_NONE;
    if (rc != RQ_OK) {
        scr_rpt_failed(&c->u, rc, &res);
        cmd = form_error(f, &res);
    } else if (prc == PR_ECANCEL) {
        fail(MQ_CANCELLED);
    } else if (prc == PR_EFILE) {
        file_msg(MQ_FILEERR, out == 'F' ? c->file : c->scratch, cferr);
    } else if (c->u.stopped) {
        fail(MQ_STOPPED);
    } else if (out == 'S' && (count > 0 || c->u.kind < RPT_ILABEL)) {
        cmd = view(c->scratch, msg(c->def->title), lines);
        tui_hint_keep(msg(MQ_HINT));
    } else {
        tui_msg(msg_sub(MQ_DONE, num, NULL), 0);
    }
    if (out == 'S')
        cf_remove(c->scratch);
    return cmd;
}

u8 scr_report(u8 cmd)
{
    rr_ctx c;
    form_def def;
    form_field fields[SCR_RP_MAXF];
    form *f;
    u16 k;
    int used;

    if (cmd < SCMD_RPT_LIST || cmd > SCMD_LBL_BIN)
        return TUI_CMD_NONE;
    memset(&c, 0, sizeof c);
    c.u.kind = (u8)(cmd - SCMD_RPT_LIST + RPT_LIST);
    scr_rpt_form(&def, fields, c.u.kind);
    c.def = &def;
    f = form_open(&def, FORM_EDIT, rr_pick, NULL, &c);
    if (f == NULL) {
        fail(MT_NOMEM);
        return TUI_CMD_NONE;
    }
    scr_rpt_defaults(f);
    form_noask(f);
    tui_hint(msg(MQ_HINT));
    /* The path of no location given, which needs no request. */
    if (c.u.kind != RPT_LOANS)
        scr_loc_show(f, rr_loc(&c.u), "", 0, &cmd);
    cmd = TUI_CMD_NONE;
    for (;;) {
        k = form_run(f);
        if (c.leave != TUI_CMD_NONE) {
            cmd = c.leave;
            break;
        }
        if (k == KEY_ESC)
            break;
        if (k == KEY_ENTER) {
            cmd = go(&c, f);
            if (cmd != TUI_CMD_NONE)
                break;
            continue;
        }
        cmd = tui_global(k, &used);
        if (used)
            break;
        cmd = TUI_CMD_NONE;
    }
    form_close(f);
    return cmd;
}
