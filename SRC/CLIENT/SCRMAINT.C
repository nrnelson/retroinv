/*
 * SCRMAINT.C - the maintenance screens of the full-screen client
 * (SCR.H): the recent service list, the Service tab of the item form and
 * the service record form.
 *
 * Both lists are list views over the rows of SCRMFORM.H: the recent
 * list over the whole work area, an item's records in the tab's panel
 * from SCR_TAB_TOP to TUI_BOTTOM.  Enter shows the selected record in
 * the record form, Ctrl-E edits it and Del deletes it after asking; in
 * the tab Ins adds a record for the item.  Ctrl-E, Del and Ins need
 * level 2 and are refused below it with tui_allow.
 *
 * The record form is a pop-up over the list: what it covers is saved
 * when it opens and put back when it closes, so in the tab the rows of
 * the item form above the panel come back as they were.  Its Notes
 * field is the record's DETAIL memo, shown and edited with the memo
 * editor on the table MAINT.  Opened from the recent list, its Record
 * menu also offers Open item, which shows the record's item in the item
 * form;
 * since that form can change this record through its own Service tab,
 * the record is loaded again afterwards.
 *
 * As in SCRITEM.C, requests are built and sent only in small leaf
 * functions, and a form that closed after a save leaves a note the list
 * shows once it has redrawn itself.  The form's table is SCRMFORM.H's,
 * and field names are taken from it wherever a request needs them, so
 * each is kept once.
 */
#define TUI_VERBS
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "TUI.H"
#include "LISTV.H"
#include "FORM.H"
#include "SCR.H"
#include "SCRSHR.H"
#include "SCRVERB.H"
#include "SCRMFORM.H"

#define MV_T_TAB        831     /* the tab panel's title                */
#define MV_HINTLIST     835     /* hints of the recent list             */
#define MV_HINTTAB      836     /* hints of the tab                     */
#define MV_HINTVIEW     837     /* hints of the form in view mode       */
#define MV_HINTEDIT     839     /* hints of the form when editing       */
#define MV_NOTES        846     /* Notes of service record ^1           */

static const char w_maint[] = "MAINT";

/* An open list: the recent list (item NULL) or the Service tab. */
typedef struct {
    lv  *v;
    const char *item;           /* the tab's item, or NULL              */
    u8   top;                   /* the frame's first row and height     */
    u8   h;
} mc_ctx;

/* The current record form. */
typedef struct {
    char id[LINK_IDLEN + 1];
    mc_ctx *list;               /* the list under the form              */
} mf_ctx;

/* ---------------------------------------------------------------------
 * Rows.
 * --------------------------------------------------------------------- */

/* The tab's args: its item. */
static void mc_args(void *arg, rq *q)
{
    mc_ctx *c;

    c = (mc_ctx *)arg;
    rq_word(q, c->item);
}

static void mc_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    mc_ctx *c;

    c = (mc_ctx *)arg;
    scr_maint_row(r, c->item == NULL, text, key);
}

/* ---------------------------------------------------------------------
 * Requests (leaf functions).
 * --------------------------------------------------------------------- */

/* Load record id into f (MAINT GET), then the memo's line count, each
   request in a frame of its own.  1, or 0 with the failure reported. */
static int rec_get(form *f, const char *id, u8 *cmd)
{
    if (!tui_req("MAINT GET", id, form_kline, f, NULL, cmd))
        return 0;
    return memo_load_count(f, scr_maint_fields[SCR_MF_NOTES].name, tui_s.l,
                           w_maint, id, cmd);
}

/* ---------------------------------------------------------------------
 * The record form.
 * --------------------------------------------------------------------- */
static void mc_draw(mc_ctx *c, int keep);

/* The hints of the form in its current mode. */
static u16 rec_hint(form *f)
{
    return (u16)(form_mode_get(f) == FORM_VIEW ? MV_HINTVIEW : MV_HINTEDIT);
}

static u16 rec_memo(void *arg, form *f, u16 field)
{
    mf_ctx *m;
    u16 lines;

    m = (mf_ctx *)arg;
    lines = memo_field(f, field, tui_s.l, w_maint, m->id,
                       tui_s.level < TUI_LVL_OPER ||
                       form_mode_get(f) == FORM_VIEW,
                       msg_sub(MV_NOTES, m->id, NULL));
    tui_hint_keep(msg(rec_hint(f)));
    return lines;
}

/* Save f (form_ops). */
static int rec_save(void *arg, form *f, u8 *cmd)
{
    mf_ctx *m;
    rq_res res;
    int isnew;
    int rc;

    m = (mf_ctx *)arg;
    isnew = form_mode_get(f) == FORM_NEW;
    rc = form_store(f, tui_s.l, w_maint, m->id, (u16)sizeof m->id,
                    scr_maint_k_id, MV_TITLE, &res, cmd);
    if (rc > 0)
        lv_note((u16)(isnew ? MV_ADDED : MV_SAVED), m->id, NULL);
    return rc;
}

/* The form's own key (form_ops): Open item in view mode, from the recent
   list, shows the record's item in the item form, then the list and the
   record again, loaded anew; leaving when the record no longer loads,
   or the item form asked to leave. */
static int rec_item(void *arg, form *f, u16 k, u8 *cmd)
{
    mf_ctx *m;
    char item[LINK_IDLEN + 1];

    m = (mf_ctx *)arg;
    if (k != SK(SV_ITEM) || form_mode_get(f) != FORM_VIEW ||
        m->list->item != NULL)
        return -1;
    form_get_text(f, scr_maint_fields[SCR_MF_ITEM].name, item,
                  (u16)sizeof item);
    *cmd = scr_item_open(item, FORM_VIEW);
    if (*cmd != TUI_CMD_NONE)
        return 0;
    mc_draw(m->list, 1);
    lv_paint(m->list->v);
    tui_hint_keep(msg(rec_hint(f)));
    return rec_get(f, m->id, cmd);
}

static const form_ops rec_ops = { rec_save, rec_item, MV_HINTEDIT };

#define RE_VERBS        (SV_BIT(SV_EDIT) | SV_BIT(SV_ITEM) | SV_BIT(SV_BACK))
#define RE_TVERBS       (SV_BIT(SV_EDIT) | SV_BIT(SV_BACK))

/* Run the form, with its Record menu, until it is left; the command to
   return. */
static u8 rec_edit(form *f, mf_ctx *m)
{
    tui_hint(msg(rec_hint(f)));
    return form_loop(f, &rec_ops, m,
                     m->list->item == NULL ? RE_VERBS : RE_TVERBS);
}

/* The form for record id in mode over list c, or, with id NULL, a new
   record of the tab's item. */
static u8 rec_form(mc_ctx *c, const char *id, u8 mode)
{
    mf_ctx m;
    form *f;
    cmem_blk *save;
    u8 cmd;

    cmd = TUI_CMD_NONE;
    txt_copy(m.id, id, (u16)sizeof m.id);
    m.list = c;
    save = vid_save(scr_maint_def.top, scr_maint_def.left, scr_maint_def.h,
                    scr_maint_def.w);
    f = form_open(&scr_maint_def, id != NULL ? mode : (u8)FORM_NEW, NULL,
                  rec_memo, &m);
    if (save == NULL || f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
    } else if (id == NULL) {
        form_load(f, scr_maint_fields[SCR_MF_ITEM].name, c->item);
        cmd = rec_edit(f, &m);
    } else if (rec_get(f, m.id, &cmd)) {
        form_set_title(f, msg_sub(MV_TITLE, m.id, NULL));
        cmd = rec_edit(f, &m);
    }
    form_close(f);
    vid_restore(save);
    return cmd;
}

/* ---------------------------------------------------------------------
 * The lists.
 * --------------------------------------------------------------------- */
/* Draw the frame and the hints (kept under a message still up when keep
   is 1). */
static void mc_draw(mc_ctx *c, int keep)
{
    u16 h;

    h = (u16)(c->item != NULL ? MV_HINTTAB : MV_HINTLIST);
    tui_box(c->top, 0, c->h, VID_COLS, TB_SINGLE,
            msg((u16)(c->item != NULL ? MV_T_TAB : MV_T_RECENT)));
    if (keep)
        tui_hint_keep(msg(h));
    else
        tui_hint(msg(h));
}

/* After the record form: draw the list again (leaving a message the
   form showed until the next key), fetch its rows again and show the
   note the form left (lv_reload). */
static u8 mc_back(mc_ctx *c, u8 cmd)
{
    if (cmd != TUI_CMD_NONE)
        return cmd;
    mc_draw(c, 1);
    lv_reload(c->v);
    return lv_leave(c->v);
}

static u8 mc_run(mc_ctx *c)
{
    char key[LV_KEYSZ];
    u16 k;
    u8 cmd;
    int used;

    mc_draw(c, 0);
    lv_home(c->v);
    cmd = lv_leave(c->v);
    while (cmd == TUI_CMD_NONE) {
        k = lv_run(c->v);
        cmd = lv_leave(c->v);
        if (cmd != TUI_CMD_NONE)
            break;
        switch (k) {
        case KEY_ESC:
            return TUI_CMD_NONE;
        case KEY_ENTER:
            if (lv_selected(c->v, key))
                cmd = mc_back(c, rec_form(c, key, FORM_VIEW));
            break;
        case KEY_CTRL('E'):
            if (lv_selected(c->v, key) && tui_allow(TUI_LVL_OPER))
                cmd = mc_back(c, rec_form(c, key, FORM_EDIT));
            break;
        case KEY_INS:
            if (c->item != NULL && tui_allow(TUI_LVL_OPER))
                cmd = mc_back(c, rec_form(c, NULL, FORM_NEW));
            break;
        case KEY_DEL:
            if (!lv_selected(c->v, key) || !tui_allow(TUI_LVL_OPER) ||
                !tui_confirm(msg_sub(MV_DELQ, key, NULL)))
                break;
            if (tui_req("MAINT DEL", key, NULL, NULL, NULL, &cmd))
                lv_note(MV_DELETED, key, NULL);
            if (cmd == TUI_CMD_NONE) {
                lv_reload(c->v);
                cmd = lv_leave(c->v);
            }
            break;
        default:
            cmd = tui_global(k, &used);
            if (used)
                return cmd;
            cmd = TUI_CMD_NONE;
            break;
        }
    }
    return cmd;
}

#define MC_VERBS        (SV_BIT(SV_OPEN) | SV_BIT(SV_EDIT) | SV_BIT(SV_NEW) | \
                         SV_BIT(SV_DEL) | SV_BIT(SV_BACK))
#define MC_RVERBS       (SV_BIT(SV_OPEN) | SV_BIT(SV_EDIT) | SV_BIT(SV_DEL) | \
                         SV_BIT(SV_BACK))

/* Open and run the recent list (item NULL) or the Service tab of item;
   the command to leave with. */
static u8 mc_open(const char *item)
{
    const char *oldtopic;
    u32 oldverbs;
    lv_src src;
    mc_ctx c;
    u8 cmd;

    oldtopic = tui_topic("SERVICE");
    oldverbs = tui_record(item != NULL ? MC_VERBS : MC_RVERBS);
    cmd = TUI_CMD_NONE;
    lv_note(0, NULL, NULL);
    memcpy(&src, item != NULL ? &scr_maint_isrc : &scr_maint_src,
           sizeof src);
    src.fmt = mc_fmt;
    src.arg = &c;
    c.item = item;
    c.top = TUI_TOP;
    c.h = TUI_WORKROWS;
    if (item != NULL) {
        src.args = mc_args;
        c.top = SCR_TAB_TOP;
        c.h = SCR_TAB_H;
    }
    c.v = lv_open(tui_s.l, &src, (u8)(c.top + 1), 1, (u8)(c.h - 2),
                  VID_COLS - 2);
    if (c.v == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
    } else {
        cmd = mc_run(&c);
        lv_close(c.v);
    }
    tui_record(oldverbs);
    tui_topic(oldtopic);
    return cmd;
}

u8 scr_maint(u8 cmd)
{
    if (cmd != SCMD_MAINT)
        return TUI_CMD_NONE;
    return mc_open(NULL);
}

void scr_maint_tab(const char *id, scr_tab *t)
{
    char item[LINK_IDLEN + 1];

    txt_copy(item, id, (u16)sizeof item);
    t->reload = 0;
    t->open[0] = '\0';
    t->cmd = mc_open(item);
}
