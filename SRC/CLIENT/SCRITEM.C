/*
 * SCRITEM.C - item lists and the item form of the full-screen client
 * (SCR.H).
 *
 * An item list is a list view in a frame over the whole work area, with
 * the item keys: Enter opens the item form in view mode, Ctrl-E in edit
 * mode, Ins a new item, Del deletes after asking, naming the item from
 * its row, Ctrl-D duplicates it and opens the copy in edit mode, and
 * Ctrl-L, Ctrl-R and Ctrl-U add a loan, a reservation or a use of it
 * (scr_loan_add).  The Record menu holds those and Move, Install in and
 * Take out, which move the selected item, install it in another or take
 * it out of its assembly (SCRASSY.C).  Ins, Del, Ctrl-E and Ctrl-D need
 * level 2 and are refused with tui_allow below it, as SCRASSY.C and
 * SCRLOAN.C refuse their verbs.  The status line shows the sort order;
 * in a location's contents it says which items show: the Record menu's
 * Not installed only switches between all the items and the ones not
 * installed in another (TOP=1), and Everything below between the
 * location's own items and those of every location below it too
 * (ALL=1).  A location's contents open on the location's own items, or,
 * when it holds other locations, on every item below it.
 *
 * The item form takes SCR_ITEMFORM_H rows; the strip below it names the
 * item's tabs, which the Record menu's Parts, Service records, Loans and
 * holds, and Used with it open (SCRASSY.C, SCRMAINT.C, SCRLOAN.C) when
 * the record is saved and unchanged, as its Split units then splits
 * units off into a new item (ITEM SPLIT).  An installed item's Parent
 * field shows its parent's maker and model after the ID, from ASSY
 * PATH, and the Location field has the location's path below it
 * (scr_loc_show).  A part chosen in the Parts tab is shown in a new form
 * for that item; the IDs and modes of up to SCR_ITEMBACK items shown
 * before are kept, and Esc (or a save) goes back through them, the
 * oldest being dropped when more are opened.
 *
 * Every request is built and sent in a small leaf function that keeps
 * its rq and rq_res on its own stack, so none is held while another
 * screen (a form, the memo editor, a pick list) is open.
 *
 * A form that closed after a save leaves a note ("Item 12 added",
 * lv_note) that the list shows on the hint row once it has redrawn
 * itself; the form an item form goes back to shows it itself, and so
 * do scr_item_open and the New item command, which have no list of
 * their own.  A copy (Ctrl-D) left with Esc before any save was sent (even
 * one the server refused), and before its notes, tabs or a split could
 * change it, is deleted again, so a copy begun by mistake leaves
 * nothing behind.
 */
#define TUI_VERBS
#define SCRSHR_LOCF
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
#include "ITEMLS.H"
#include "SCRIFORM.H"

#define MI_HINTLIST     457     /* hints of an item list                */
#define MI_ORDER        459     /* Order: ^1                            */
#define MI_HINTVIEW     463     /* hints of the form in view mode       */
#define MI_HINTEDIT     464     /* hints of the form when editing       */
#define MI_NOTES        469     /* Notes of item ^1                     */
#define MI_T_TABS       492     /* the strip's title: the tabs' names   */
#define MI_TABTXT1      493     /* the strip's two lines of text        */
#define MI_TABTXT2      494

/* The list's frame covers the work area; the view sits inside it. */
#define IL_W            (VID_COLS - 2)
#define IL_H            (TUI_WORKROWS - 2)

/* The strip below the item form.  Compile-time checks here are pairs of
   declarations of one array, the second of size 2 when the condition
   fails: they then disagree, which every compiler refuses (a negative
   array size is accepted by some period compilers). */
#define IS_TOP          (TUI_TOP + SCR_ITEMFORM_H)
#define IS_H            (TUI_BOTTOM - IS_TOP + 1)
extern char strip_fits[1];
extern char strip_fits[(IS_H >= 4) ? 1 : 2];

static const char w_item[] = "ITEM";
static const char w_itemdel[] = "ITEM DEL";
static const char k_top[] = "TOP";
static const char k_all[] = "ALL";

/* An open item list. */
typedef struct {
    lv  *v;
    const lv_src *src;
    const char *title;          /* fixed title, or NULL: by sort order  */
    const char *loc;            /* LOC_ID of a new item, or NULL        */
    lv_args_fn args;            /* the caller's own request parts       */
    void *arg;
    u16  tab;                   /* MO_SHOW in a location's contents
                                   (scr_item_list), else 0              */
    u8   top;                   /* 1: items not installed only, TOP=1   */
    u8   below;                 /* 1: every location below too, ALL=1   */
} il_ctx;

/* The tabs the verbs SV_PARTS to SV_USED open, in their order. */
typedef void (*it_tab_fn)(const char *id, scr_tab *t);
static const it_tab_fn item_tabs[] = {
    scr_assy_tab, scr_maint_tab, scr_loan_tab, scr_loan_used
};
extern char tabs_fit[1];
extern char tabs_fit[(sizeof item_tabs / sizeof item_tabs[0] ==
                      SV_USED - SV_PARTS + 1) ? 1 : 2];

/* The Record menus of an item list, a location's contents and the item
   form. */
#define IL_VERBS        (SV_BIT(SV_OPEN) | SV_BIT(SV_EDIT) | SV_BIT(SV_NEW) | \
                         SV_BIT(SV_DEL) | SV_BIT(SV_DUP) | SV_BIT(SV_MOVE) | \
                         SV_BIT(SV_INTO) | SV_BIT(SV_OUT) | \
                         SV_BIT(SV_LEND) | SV_BIT(SV_RES) | \
                         SV_BIT(SV_USE) | SV_BIT(SV_BACK))
#define IL_CVERBS       (IL_VERBS | SV_BIT(SV_TOP) | SV_BIT(SV_BELOW))
#define IT_VERBS        (SV_BIT(SV_EDIT) | SV_BIT(SV_PARTS) | \
                         SV_BIT(SV_SVC) | SV_BIT(SV_LOANS) | \
                         SV_BIT(SV_USED) | SV_BIT(SV_SPLIT) | \
                         SV_BIT(SV_BACK))

/* What an item form that closed without a command leads to. */
#define GO_BACK         0       /* the item shown before, or the caller */
#define GO_SAVED        1       /* the same after a save, with its note */
#define GO_OPEN         2       /* the part in next                     */

/* The current item form. */
typedef struct {
    char id[LINK_IDLEN + 1];
    char loc[LINK_LOCLEN + 1];  /* location preset of a new item: its
                                   code, else its LOC_ID; or ""        */
    char next[LINK_IDLEN + 1];  /* GO_OPEN: the item to show            */
    u8   go;                    /* GO_                                  */
    u8   mode;                  /* GO_OPEN: the mode the form was in    */
    u8   keep;                  /* 1: a message on the hint row stays
                                   when the form is drawn               */
    u8   shownote;              /* 1: the form shows the pending note   */
    const char *copied;         /* a copy's ID while its MI_COPIED note
                                   is due (no save since), else NULL    */
    u8   touched;               /* 1 once a tab, a split or the notes
                                   for editing have been opened, or a
                                   save sent has failed                 */
} it_ctx;

/* ---------------------------------------------------------------------
 * Rows.
 * --------------------------------------------------------------------- */

static void il_args(void *arg, rq *q)
{
    il_ctx *c;

    c = (il_ctx *)arg;
    if (c->args != NULL)
        c->args(c->arg, q);
    if (c->top)
        rq_pair(q, k_top, "1");
    if (c->below)
        rq_pair(q, k_all, "1");
}

static void item_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    itl_row(r, lv_order_index(((il_ctx *)arg)->v), text, key);
}

/* ---------------------------------------------------------------------
 * Requests (leaf functions).
 * --------------------------------------------------------------------- */

/* ITEM COPY id (qty NULL) or ITEM SPLIT id QTY=qty, the new item's ID=
   into newid (LINK_IDLEN + 1 bytes; "" when the +OK has none): RQ_OK,
   or the failure's code with the failure reported and *cmd as tui_req
   sets it. */
static int item_make(const char *words, const char *id, const char *qty,
                     char *newid, u8 *cmd)
{
    rq q;
    rq_res res;

    newid[0] = '\0';
    *cmd = TUI_CMD_NONE;
    rq_begin(&q, words);
    rq_word(&q, id);
    if (qty != NULL)
        rq_pair(&q, itl_k_qty, qty);
    if (rq_send(tui_s.l, &q, NULL, NULL, &res) != RQ_OK) {
        *cmd = tui_fail(&res);
        return res.code;
    }
    rq_okval(&res, itl_k_id, newid, LINK_IDLEN + 1);
    return RQ_OK;
}

/* ITEM GET id into f (scr_item_loaded), ASSY PATH id for an installed
   item's Parent field (scr_item_parent), the location's path, then the
   memo's line count, each request in a frame of its own; 1, or 0 with
   the failure reported.  A failed ASSY PATH that does not end the
   session is reported and leaves the bare ID, the item still shown. */
static int item_get(form *f, const char *id, u8 *cmd)
{
    if (!tui_req("ITEM GET", id, scr_item_kline, f, NULL, cmd))
        return 0;
    scr_item_loaded(f);
    if (form_get(f, scr_item_fields[SCR_IF_PARENT].name)[0] != '\0' &&
        !tui_req("ASSY PATH", id, scr_item_parent, f, NULL, cmd) &&
        *cmd != TUI_CMD_NONE)
        return 0;
    return scr_loc_show(f, &scr_item_loc,
                        form_get(f, scr_item_fields[SCR_IF_LOC].name), 1,
                        cmd) &&
           memo_load_count(f, scr_item_fields[SCR_IF_NOTES].name, tui_s.l,
                           w_item, id, cmd);
}

/* Save f (form_ops), with c->go GO_SAVED once saved, and c->touched
   set by a save sent that failed, as a copy is then kept.  A LOC_ID
   preset for a new item was loaded as its original value, so that the
   untouched form is not changed; it is made a change here, for the ADD
   to carry it. */
static int item_save(void *arg, form *f, u8 *cmd)
{
    it_ctx *c;
    rq_res res;
    const char *v;
    int isnew;
    int rc;

    c = (it_ctx *)arg;
    isnew = form_mode_get(f) == FORM_NEW;
    v = form_get(f, scr_item_fields[SCR_IF_LOC].name);
    if (isnew && c->loc[0] != '\0' && v != NULL && strcmp(v, c->loc) == 0) {
        form_load(f, scr_item_fields[SCR_IF_LOC].name, "");
        form_put(f, scr_item_fields[SCR_IF_LOC].name, c->loc);
    }
    rc = form_store(f, tui_s.l, w_item, c->id, (u16)sizeof c->id,
                    itl_k_itemid, MI_TITLE, &res, cmd);
    if (res.code != RQ_OK)
        c->touched = 1;
    if (rc > 0) {
        lv_note((u16)(isnew ? MI_ADDED : MI_SAVED), c->id, NULL);
        c->copied = NULL;
        c->go = GO_SAVED;
    }
    return rc;
}

/* ---------------------------------------------------------------------
 * The item form.
 * --------------------------------------------------------------------- */
/* F4 on the category, location or tags field, or a location typed
   (form_pick_fn); form_loop has the command to leave with from
   form_stop. */
static int item_pick(void *arg, form *f, u16 field, char *val)
{
    /* The form always passes its it_ctx, so the arg test never fires;
       it keeps the parameter, which form_pick_fn requires, referenced. */
    if (arg == NULL)
        return 0;
    if (field == SCR_IF_CAT)
        return scr_pick_field(f, SCR_CK_CAT, val, MI_HINTEDIT, NULL);
    if (field == SCR_IF_TAGS)
        return scr_pick_tag(f, val, scr_item_fields[SCR_IF_TAGS].width,
                            MI_HINTEDIT, NULL);
    return scr_loc_field(f, &scr_item_loc, NULL, val, MI_HINTEDIT, NULL);
}

/* The hints of the form in its current mode. */
static u16 item_hint(form *f)
{
    return (u16)(form_mode_get(f) == FORM_VIEW ? MI_HINTVIEW : MI_HINTEDIT);
}

/* The memo editor saves to the server itself, so a copy whose notes it
   opened for editing is touched, and kept. */
static u16 item_memo(void *arg, form *f, u16 field)
{
    it_ctx *c;
    u16 lines;
    int ro;

    c = (it_ctx *)arg;
    ro = tui_s.level < TUI_LVL_OPER || form_mode_get(f) == FORM_VIEW;
    if (!ro)
        c->touched = 1;
    lines = memo_field(f, field, tui_s.l, w_item, c->id, ro,
                       msg_sub(MI_NOTES, c->id, NULL));
    tui_hint_keep(msg(item_hint(f)));
    return lines;
}

/* Clear the work area and draw the strip, the hints (kept under a
   message still up when c->keep is 1) and the status information; the
   form paints itself. */
static void item_draw(form *f, it_ctx *c)
{
    const char *h;

    h = msg(item_hint(f));
    tui_clear(TUI_TOP, TUI_BOTTOM);
    tui_info("");
    if (c->keep)
        tui_hint_keep(h);
    else
        tui_hint(h);
    tui_box(IS_TOP, 0, IS_H, VID_COLS, TB_SINGLE, msg(MI_T_TABS));
    tui_text((u8)(IS_TOP + 1), 2, msg(MI_TABTXT1), TA_NORMAL);
    tui_text((u8)(IS_TOP + 2), 2, msg(MI_TABTXT2), TA_NORMAL);
}

/* Split units of the item form: the units asked for split off into a new item
   (ITEM SPLIT, level 2), then the item again with the new one's ID on
   the hint row; as item_key returns, leaving when the item has gone, as
   a save does. */
static int item_split(it_ctx *c, form *f, u8 *cmd)
{
    char qty[LINK_QTYLEN + 1];
    char newid[LINK_IDLEN + 1];
    int rc;

    qty[0] = '\0';
    if (!tui_allow(TUI_LVL_OPER) ||
        !tui_prompt(msg_sub(MI_SPLITQ, c->id, NULL), msg(MI_L_QTY), qty,
                    LINK_QTYLEN, TI_DIGITS) || qty[0] == '\0')
        return 1;
    rc = item_make("ITEM SPLIT", c->id, qty, newid, cmd);
    if (rc != RQ_OK)
        return *cmd == TUI_CMD_NONE && rc != RQ_ENOTFOUND;
    if (!item_get(f, c->id, cmd))
        return 0;
    tui_msg(msg_sub(MI_SPLIT, newid, NULL), 0);
    return 1;
}

/* The form's own keys (form_ops), the verbs of its Record menu, for a
   saved record without changes (else MT_SAVEFIRST): SV_PARTS to SV_USED
   (key k) open their tab, afterwards the item again when the tab changed
   it or was answered 200 (another session may have deleted the item,
   which the form then leaves, as a save does); SV_SPLIT splits
   (item_split).  Leaving with TUI_CMD_NONE sets c->go. */
static int item_key(void *arg, form *f, u16 k, u8 *cmd)
{
    it_ctx *c;
    scr_tab t;

    c = (it_ctx *)arg;
    if ((k < SK(SV_PARTS) || k > SK(SV_USED)) && k != SK(SV_SPLIT))
        return -1;
    *cmd = TUI_CMD_NONE;
    if (form_mode_get(f) == FORM_NEW || form_dirty(f)) {
        tui_msg(msg(MT_SAVEFIRST), 1);
        return 1;
    }
    c->touched = 1;
    if (k == SK(SV_SPLIT))
        return item_split(c, f, cmd);
    tui_s.gone = 0;
    item_tabs[k - SK(SV_PARTS)](c->id, &t);
    *cmd = t.cmd;
    if (*cmd != TUI_CMD_NONE)
        return 0;
    if (t.open[0] != '\0') {
        txt_copy(c->next, t.open, (u16)sizeof c->next);
        c->mode = form_mode_get(f);
        c->go = GO_OPEN;
        return 0;
    }
    c->keep = 1;
    item_draw(f, c);
    if ((t.reload || tui_s.gone) && !item_get(f, c->id, cmd))
        return 0;
    return 1;
}

static const form_ops item_ops = { item_save, item_key, MI_HINTEDIT };

/* Run the form, with its Record menu, until it is left; the command to
   return. */
static u8 item_edit(form *f, it_ctx *c)
{
    item_draw(f, c);
    if (c->shownote)
        lv_note_show();
    c->shownote = 0;
    return form_loop(f, &item_ops, c, IT_VERBS);
}

/* One form for item c->id in mode; the command to return, with c->go
   saying where to go when it is TUI_CMD_NONE.  An item that does not
   load drops the pending note. */
static u8 item_show(it_ctx *c, u8 mode)
{
    form *f;
    u8 cmd;

    c->go = GO_BACK;
    f = form_open(&scr_item_def, mode == FORM_EDIT ? FORM_EDIT : FORM_VIEW,
                  item_pick, item_memo, c);
    if (f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return TUI_CMD_NONE;
    }
    if (item_get(f, c->id, &cmd)) {
        form_set_title(f, msg_sub(MI_TITLE, c->id, NULL));
        cmd = item_edit(f, c);
    } else {
        lv_note(0, NULL, NULL);
        c->copied = NULL;
    }
    form_close(f);
    return cmd;
}

/* Go on from the form just left with *cmd (c->go saying where to when it
   is TUI_CMD_NONE): the part chosen in a Parts tab in a form of its
   own, then back through the items shown before, as each is left; the
   command to return.  The note of the last save is left for the
   caller. */
static u8 item_walk(it_ctx *c, u8 cmd)
{
    char back[SCR_ITEMBACK][LINK_IDLEN + 1];
    u8 bmode[SCR_ITEMBACK];
    u16 nback;
    u8 mode;

    nback = 0;
    for (;;) {
        if (cmd != TUI_CMD_NONE)
            return cmd;
        if (c->go == GO_OPEN) {
            if (nback == SCR_ITEMBACK) {
                memmove(back[0], back[1], sizeof back - sizeof back[0]);
                memmove(bmode, bmode + 1, sizeof bmode - 1);
                nback--;
            }
            strcpy(back[nback], c->id);
            bmode[nback++] = c->mode;
            strcpy(c->id, c->next);
            mode = FORM_VIEW;
        } else if (nback > 0) {
            strcpy(c->id, back[--nback]);
            mode = bmode[nback];
            c->shownote = (u8)(c->go == GO_SAVED);
        } else {
            return TUI_CMD_NONE;
        }
        c->keep = 1;
        cmd = item_show(c, mode);
    }
}

/* The item form for id in mode, then the parts chosen in its Parts tab
   (item_walk).  copied is the ID of a copy just made, or NULL.  When the
   walk ends with TUI_CMD_NONE, without a save or a failed load, the copy
   is deleted again, or, after its notes, one of its tabs, a split or a
   save sent that failed, kept with its MI_COPIED note. */
static u8 item_open(const char *id, u8 mode, const char *copied)
{
    it_ctx c;
    u8 cmd;

    txt_copy(c.id, id, (u16)sizeof c.id);
    c.loc[0] = '\0';
    c.keep = 0;
    c.shownote = 0;
    c.copied = copied;
    c.touched = 0;
    cmd = item_walk(&c, item_show(&c, mode));
    if (cmd == TUI_CMD_NONE && c.copied != NULL) {
        if (c.touched)
            lv_note(MI_COPIED, c.copied, NULL);
        else
            tui_req(w_itemdel, c.copied, NULL, NULL, NULL, &cmd);
    }
    return cmd;
}

u8 scr_item_open(const char *id, u8 mode)
{
    lv_note(0, NULL, NULL);
    return lv_note_end(item_open(id, mode, NULL));
}

u8 scr_item_new(const char *loc)
{
    it_ctx c;
    form *f;
    u8 cmd;

    if (!tui_allow(TUI_LVL_OPER))
        return TUI_CMD_NONE;
    c.id[0] = '\0';
    txt_copy(c.loc, loc, (u16)sizeof c.loc);
    c.next[0] = '\0';
    c.go = GO_BACK;
    c.mode = FORM_NEW;
    c.keep = 0;
    c.shownote = 0;
    c.copied = NULL;
    c.touched = 0;
    f = form_open(&scr_item_def, FORM_NEW, item_pick, item_memo, &c);
    if (f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return TUI_CMD_NONE;
    }
    scr_item_preset(f);
    form_load(f, scr_item_fields[SCR_IF_LOC].name, c.loc);
    if (scr_loc_show(f, &scr_item_loc, c.loc, 1, &cmd)) {
        form_get_text(f, scr_item_fields[SCR_IF_LOC].name, c.loc,
                      (u16)sizeof c.loc);
        cmd = item_edit(f, &c);
    }
    form_close(f);
    /* Once its ADD has made the record, the form may have opened a part
       through the Parts tab; going back then shows the new item. */
    return item_walk(&c, cmd);
}

/* ---------------------------------------------------------------------
 * Item lists.
 * --------------------------------------------------------------------- */
/* What a location's contents c show, for the status line: MO_SHOW with
   the words of its two states, in SCRLFORM.H's order of the contents'
   texts. */
static const char *il_shows(il_ctx *c)
{
    char a[MSG_MAXLEN + 1];

    msg_get((u16)(c->tab + 1 + c->top), a, (u16)sizeof a);
    return msg_sub(c->tab, a, msg((u16)(c->tab + 3 + c->below)));
}

/* Draw the frame, the hints (kept under a message still up when keep
   is 1) and the status information: the order, or what a location's
   contents show. */
static void il_draw(il_ctx *c, int keep)
{
    const char *h;

    tui_box(TUI_TOP, 0, TUI_WORKROWS, VID_COLS, TB_SINGLE,
            c->title != NULL ? c->title :
            msg((u16)(MI_T_ID + lv_order_index(c->v))));
    h = msg(MI_HINTLIST);
    if (keep)
        tui_hint_keep(h);
    else
        tui_hint(h);
    tui_info(c->src->norders > 0 ?
             msg_sub(MI_ORDER, lv_order_name(c->v), NULL) :
             c->tab != 0 ? il_shows(c) : "");
}

/* After another screen: draw the list again (leaving a message that
   screen showed until the next key), fetch its rows again and show the
   note that screen left (lv_reload). */
static u8 il_back(il_ctx *c, u8 cmd)
{
    if (cmd != TUI_CMD_NONE)
        return cmd;
    il_draw(c, 1);
    lv_reload(c->v);
    return lv_leave(c->v);
}

/* The key k of SV_MOVE, SV_INTO or SV_OUT on the selected item key: the
   assembly change (SCRASSY.C), then the frame and hints again, under the
   message it left, and the rows again when it was made or the item has
   gone. */
static u8 il_assy(il_ctx *c, u16 k, const char *key)
{
    u8 cmd;
    int done;

    if (k == SK(SV_MOVE))
        done = scr_assy_move(key, &cmd);
    else if (k == SK(SV_INTO))
        done = scr_assy_into(key, &cmd);
    else
        done = scr_assy_out(key, &cmd);
    if (cmd != TUI_CMD_NONE)
        return cmd;
    il_draw(c, 1);
    if (!done)
        return TUI_CMD_NONE;
    lv_reload(c->v);
    return lv_leave(c->v);
}

/* Ask before deleting the selected item, named by its ID, maker and
   model as its row shows them (itl_row), every run of blanks made one
   and none at either end; 1 for yes. */
static int il_delq(il_ctx *c)
{
    char t[LV_ROWTEXT + 1];
    u16 e;
    u16 i;
    u16 n;

    lv_seltext(c->v, t);
    e = ITL_IDW + 1 + 2 * (ITL_NAMEW + 1);
    if (lv_order_index(c->v) != ITL_ORDSER) {
        memset(t + ITL_IDW + 1, ' ', LINK_CATLEN);
        e += LINK_CATLEN + 1;
    }
    n = 0;
    for (i = 0; i < e; i++)
        if (t[i] != ' ' || (n > 0 && t[n - 1] != ' '))
            t[n++] = t[i];
    if (n > 0 && t[n - 1] == ' ')
        n--;
    t[n] = '\0';
    return tui_confirm(msg_sub(MI_DELQ, t, NULL));
}

/* Key k of the list when it is Enter, Ctrl-E, Ins, Del, Ctrl-D, Ctrl-L,
   Ctrl-R, Ctrl-U, SV_MOVE, SV_INTO or SV_OUT, acted on: 1, with the
   command to leave with (TUI_CMD_NONE to stay) in *cmd.  0 for any other
   key. */
static int il_key(il_ctx *c, u16 k, u8 *cmd)
{
    char key[LV_KEYSZ];
    char newid[LINK_IDLEN + 1];

    *cmd = TUI_CMD_NONE;
    switch (k) {
    case KEY_ENTER:
        if (lv_selected(c->v, key))
            *cmd = il_back(c, item_open(key, FORM_VIEW, NULL));
        break;
    case KEY_CTRL('E'):
        if (lv_selected(c->v, key) && tui_allow(TUI_LVL_OPER))
            *cmd = il_back(c, item_open(key, FORM_EDIT, NULL));
        break;
    case KEY_INS:
        if (tui_allow(TUI_LVL_OPER))
            *cmd = il_back(c, scr_item_new(c->loc));
        break;
    case KEY_DEL:
        if (!lv_selected(c->v, key) || !tui_allow(TUI_LVL_OPER) ||
            !il_delq(c))
            break;
        if (scr_del(w_itemdel, key, MI_DELETED, cmd)) {
            lv_reload(c->v);
            *cmd = lv_leave(c->v);
        }
        break;
    case KEY_CTRL('D'):
        if (lv_selected(c->v, key) && tui_allow(TUI_LVL_OPER) &&
            item_make("ITEM COPY", key, NULL, newid, cmd) == RQ_OK &&
            newid[0] != '\0')
            *cmd = il_back(c, item_open(newid, FORM_EDIT, newid));
        break;
    case KEY_CTRL('L'):
    case KEY_CTRL('R'):
    case KEY_CTRL('U'):
        if (lv_selected(c->v, key) && tui_allow(TUI_LVL_OPER))
            *cmd = il_back(c, scr_loan_add(key, k));
        break;
    case SK(SV_MOVE):
    case SK(SV_INTO):
    case SK(SV_OUT):
        if (lv_selected(c->v, key))
            *cmd = il_assy(c, k, key);
        break;
    default:
        return 0;
    }
    return 1;
}

static u8 il_run(il_ctx *c)
{
    u16 k;
    u8 cmd;
    int used;

    il_draw(c, 0);
    lv_home(c->v);
    cmd = lv_leave(c->v);
    while (cmd == TUI_CMD_NONE) {
        k = lv_run(c->v);
        cmd = lv_leave(c->v);
        if (cmd != TUI_CMD_NONE)
            break;
        if ((k == SK(SV_TOP) || k == SK(SV_BELOW)) && c->tab != 0) {
            if (k == SK(SV_TOP))
                c->top ^= 1;
            else
                c->below ^= 1;
            il_draw(c, 1);
            lv_home(c->v);
            cmd = lv_leave(c->v);
            continue;
        }
        if (k == KEY_ESC)
            return TUI_CMD_NONE;
        if (!il_key(c, k, &cmd)) {
            cmd = tui_global(k, &used);
            if (used)
                return cmd;
            cmd = TUI_CMD_NONE;
        }
    }
    return cmd;
}

/* Open a list over src, the caller's copy of itl_src, whose fmt, args
   and arg are set here, and run it with help topic topic; tab and below
   as for scr_item_list. */
static u8 il_open(lv_src *src, const char *topic, const char *order,
                  const char *title, const char *loc, lv_args_fn args,
                  void *arg, u16 tab, u8 below)
{
    const char *oldtopic;
    u32 oldverbs;
    il_ctx c;
    u8 cmd;

    oldtopic = tui_topic(topic);
    lv_note(0, NULL, NULL);
    src->fmt = item_fmt;
    src->args = il_args;
    src->arg = &c;
    c.src = src;
    c.title = title;
    c.loc = loc;
    c.args = args;
    c.arg = arg;
    c.tab = tab;
    c.top = 0;
    c.below = below;
    c.v = lv_open(tui_s.l, src, TUI_TOP + 1, 1, IL_H, IL_W);
    if (c.v == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        tui_topic(oldtopic);
        return TUI_CMD_NONE;
    }
    if (order != NULL)
        lv_set_order(c.v, order);
    oldverbs = tui_record(tab != 0 ? IL_CVERBS : IL_VERBS);
    cmd = il_run(&c);
    tui_record(oldverbs);
    lv_close(c.v);
    tui_info("");
    tui_topic(oldtopic);
    return cmd;
}

u8 scr_items(u8 cmd)
{
    lv_src src;
    const char *order;

    if (cmd == SCMD_ITEM_NEW) {
        lv_note(0, NULL, NULL);
        return lv_note_end(scr_item_new(NULL));
    }
    if (cmd >= SCMD_ITEMS_ID && cmd <= SCMD_ITEMS_SER)
        order = itl_src.orders[cmd - SCMD_ITEMS_ID];
    else
        order = scr_cfg.itemord;
    memcpy(&src, &itl_src, sizeof src);
    return il_open(&src, "ITEMS", order, NULL, NULL, NULL, NULL, 0, 0);
}

u8 scr_item_list(const char *topic, const char *cmd, lv_args_fn args,
                 void *arg, const char *title, u16 empty,
                 const char *locpreset, u16 tab, u8 below)
{
    char t[SCR_IL_TITLEW + 1];
    lv_src src;

    txt_copy(t, title, (u16)sizeof t);
    memcpy(&src, &itl_src, sizeof src);
    src.cmd = cmd;
    src.norders = 0;
    src.empty = empty;
    return il_open(&src, topic, NULL, t, locpreset, args, arg, tab,
                   below);
}
