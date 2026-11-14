/*
 * SCRADMIN.C - the administration screens of the full-screen client
 * (SCR.H), for level 3 only: backup, restore, pack and reindex of the
 * data files, the audit log and the server's sessions.
 *
 * The tables, the texts of an operation's success and refusal and the
 * sessions' rows are SCRZFORM.H's.  An operation asks for its directory
 * and its question first, and the request may take minutes, which the
 * hint row says while it runs.  No screen keeps rows across a menu
 * command (the screen open when one is chosen returns first, and the
 * next one fetches what it shows), so nothing shown after a restore or
 * a pack comes from before it.
 *
 * The audit log is the filter form above a list view of AUDIT LIST.
 * The list is shown unfiltered when the screen opens; Enter shows it with
 * the form's values and moves into it, Esc there goes back to the form,
 * and Esc in the form leaves.
 *
 * The sessions are drawn as the rows of ADMIN CLIENTS arrive, the
 * requester's own in TA_BOLD, those beyond the box left out; F5 fetches
 * them again.  In local mode the protocol answers 104, which tui_fail
 * reports.
 *
 * Requests are built and sent only in the leaf function adm_send, and
 * the audit list's by the list view.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "RQERR.H"
#include "TUI.H"
#include "LISTV.H"
#include "FORM.H"
#include "SCR.H"
#include "SCRZFORM.H"

#define MZ_WAIT         585     /* an operation is running              */
#define MZ_HINTFORM     597     /* hints of the form and of the list    */
#define MZ_HINTLIST     598
#define MZ_SHINT        604     /* the sessions' hints                  */

/* The audit list's box below the filter's, which fills the work area. */
#define AU_LTOP         (TUI_TOP + SCR_AF_H)
#define AU_LH           (TUI_WORKROWS - SCR_AF_H)

/* The last row the sessions are drawn on, and a column the F line does
   not name. */
#define SS_LAST         (TUI_BOTTOM - 1)
#define SS_NONE         0xFF

/* ---------------------------------------------------------------------
 * The operations on the data files.
 * --------------------------------------------------------------------- */

/* Send "ADMIN word", with DIR=dir when dir is not NULL, its data lines
   to fn. */
static int adm_send(const char *word, const char *dir, rq_fn fn, void *arg,
                    rq_res *res)
{
    rq q;

    rq_begin(&q, scr_adm_w_admin);
    rq_word(&q, word);
    if (dir != NULL)
        rq_pair(&q, scr_adm_k_dir, dir);
    return rq_send(tui_s.l, &q, fn, arg, res);
}

/* Run operation op after its prompt and question; the command the
   screen must return. */
static u8 run_op(const scr_adm_op *op)
{
    char dir[LINK_MAXDIR + 1];
    char num[TXT_U32LEN + 1];
    rq_res res;
    int c;

    dir[0] = '\0';
    if (op->title != 0 &&
        (!tui_prompt(msg(op->title), msg(MZ_L_DIR), dir, LINK_MAXDIR,
                     TI_UPPER) || dir[0] == '\0'))
        return TUI_CMD_NONE;
    if (op->ask != 0 && !tui_confirm(msg_sub(op->ask, dir, NULL)))
        return TUI_CMD_NONE;
    tui_msg(msg(MZ_WAIT), 0);
    vid_flush();
    if (adm_send(op->word, op->title != 0 ? dir : NULL, NULL, NULL,
                 &res) == RQ_OK) {
        rq_okval(&res, "REMOVED", num, (u16)sizeof num);
        tui_msg(msg_sub(op->done, op->title != 0 ? dir : num, NULL), 0);
        return TUI_CMD_NONE;
    }
    c = res.code;
    if (res.closed || strcmp(res.field, scr_adm_k_dir) != 0)
        return tui_fail(&res);
    if (c == RQ_ENOTFOUND)
        tui_msg(msg_sub(op->nodir, dir, NULL), 1);
    else if (c >= RQ_EIO && c <= RQ_EINUSE)
        tui_msg(msg_sub(MZ_DIRERR, dir, rqe_text(c)), 1);
    else
        return tui_fail(&res);
    return TUI_CMD_NONE;
}

/* ---------------------------------------------------------------------
 * The audit log.
 * --------------------------------------------------------------------- */

/* Enter in the form: the list with its values, until Esc there; the
   command the screen must return. */
static u8 au_list(form *f, lv *v)
{
    u16 k;
    u8 cmd;
    int used;

    if (form_get(f, scr_audit_k_rec)[0] != '\0' &&
        form_get(f, scr_audit_k_tbl)[0] == '\0') {
        form_field_error(f, scr_audit_k_tbl, msg(MZ_NEEDTBL));
        return TUI_CMD_NONE;
    }
    tui_hint(msg(MZ_HINTLIST));
    lv_home(v);
    cmd = lv_leave(v);
    while (cmd == TUI_CMD_NONE) {
        k = lv_run(v);
        cmd = lv_leave(v);
        if (cmd != TUI_CMD_NONE || k == KEY_ESC)
            break;
        cmd = tui_global(k, &used);
        if (used)
            break;
        cmd = TUI_CMD_NONE;
    }
    return cmd;
}

static u8 audit(void)
{
    lv_src src;
    form *f;
    lv *v;
    u16 k;
    u8 cmd;
    int used;

    f = form_open(&scr_audit_def, FORM_EDIT, NULL, NULL, NULL);
    memcpy(&src, &scr_audit_src, sizeof src);
    src.arg = f;
    v = f != NULL ? lv_open(tui_s.l, &src, (u8)(AU_LTOP + 1), 1,
                            (u8)(AU_LH - 2), VID_COLS - 2) : NULL;
    if (v == NULL) {
        form_close(f);
        tui_msg(msg(MT_NOMEM), 1);
        return TUI_CMD_NONE;
    }
    form_noask(f);
    form_paint(f);
    tui_box(AU_LTOP, 0, AU_LH, VID_COLS, TB_SINGLE, NULL);
    tui_hint(msg(MZ_HINTFORM));
    lv_home(v);
    cmd = lv_leave(v);
    while (cmd == TUI_CMD_NONE) {
        tui_hint_keep(msg(MZ_HINTFORM));
        k = form_run(f);
        if (k == KEY_ESC)
            break;
        if (k == KEY_ENTER) {
            cmd = au_list(f, v);
            continue;
        }
        cmd = tui_global(k, &used);
        if (used)
            break;
        cmd = TUI_CMD_NONE;
    }
    lv_close(v);
    form_close(f);
    return cmd;
}

/* ---------------------------------------------------------------------
 * The sessions.
 * --------------------------------------------------------------------- */

/* The rows being drawn: the place of each column in the F line, and
   the next row. */
typedef struct {
    u8   at[SCR_SS_NCOLS];
    u8   row;
} ss_ctx;

/* The value of column i in the R line ln, or "". */
static const char *ss_val(const ss_ctx *c, const rq_line *ln, u16 i)
{
    return c->at[i] < ln->n ? ln->v[c->at[i]] : "";
}

/* rq_fn: the columns' places from the F line, then each R line drawn
   on the next row while one is left. */
static int ss_line(void *arg, const rq_line *ln)
{
    ss_ctx *c;
    char t[LV_ROWTEXT + 1];
    const char *v;
    u16 at;
    u16 i;
    u16 k;

    c = (ss_ctx *)arg;
    if (ln->kind == 'F') {
        for (i = 0; i < SCR_SS_NCOLS; i++)
            for (k = 0; k < ln->n; k++)
                if (strcmp(ln->v[k], scr_sess_cols[i].name) == 0)
                    c->at[i] = (u8)k;
        return 0;
    }
    if (ln->kind != 'R' || c->row > SS_LAST)
        return 0;
    t[0] = ' ';
    t[1] = '\0';
    at = 1;
    for (i = 0; i < SCR_SS_SELF; i++) {
        v = ss_val(c, ln, i);
        if (i == SCR_SS_STATE && v[0] >= '0' && v[0] <= '3' && v[1] == '\0')
            v = msg((u16)(MZ_STATE0 + (v[0] - '0')));
        lv_col(t, &at, v, scr_sess_cols[i].w, scr_sess_cols[i].right);
    }
    tui_textn(c->row++, 1, t, VID_COLS - 2,
              (u8)(ss_val(c, ln, SCR_SS_SELF)[0] == '1' ? TA_BOLD :
                                                          TA_NORMAL));
    return 0;
}

/* Draw the box and fetch and draw the rows; 1 when they came, else 0
   with *cmd the command tui_fail gave. */
static int ss_load(u8 *cmd)
{
    ss_ctx c;
    rq_res res;

    tui_box(TUI_TOP, 0, TUI_WORKROWS, VID_COLS, TB_SINGLE, msg(MZ_T_SESS));
    tui_textn(TUI_TOP + 1, 1, msg(MZ_SHDR), VID_COLS - 2, TA_TITLE);
    memset(c.at, SS_NONE, sizeof c.at);
    c.row = (u8)(TUI_TOP + 2);
    if (adm_send("CLIENTS", NULL, ss_line, &c, &res) == RQ_OK)
        return 1;
    *cmd = tui_fail(&res);
    return 0;
}

static u8 sessions(void)
{
    u16 k;
    u8 cmd;
    int used;

    while (ss_load(&cmd)) {
        tui_hint(msg(MZ_SHINT));
        do {
            k = tui_key();
            if (k == KEY_ESC)
                return TUI_CMD_NONE;
            cmd = tui_global(k, &used);
            if (used)
                return cmd;
        } while (k != KEY_F5);
    }
    return cmd;
}

/* ---------------------------------------------------------------------
 * The screen.
 * --------------------------------------------------------------------- */
u8 scr_admin(u8 cmd)
{
    const char *oldtopic;

    if (!tui_allow(TUI_LVL_ADMIN))
        return TUI_CMD_NONE;
    if (cmd == SCMD_AUDIT) {
        oldtopic = tui_topic(scr_audit_def.topic);
        cmd = audit();
    } else if (cmd == SCMD_SESSIONS) {
        oldtopic = tui_topic("SESSIONS");
        cmd = sessions();
    } else {
        oldtopic = tui_topic(scr_adm_w_admin);
        cmd = run_op(&scr_adm_ops[cmd - SCMD_BACKUP]);
    }
    tui_topic(oldtopic);
    return cmd;
}
