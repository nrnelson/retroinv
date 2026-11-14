/*
 * SCRLOAN.C - the loan screens of the full-screen client (SCR.H): the
 * list of loans and holds, the Loans tab of the item form and the forms
 * of their rows.
 *
 * A row lends units of an item, reserves them or marks them in use
 * (SCRNFORM.H).  The list over the whole work area shows a view of every
 * item's rows (SCR_LV_), Ctrl-PgDn going on to the next and Ctrl-PgUp back to
 * the one before: the open rows, those of each kind, the overdue ones and
 * every row.  The tab, in the panel from SCR_TAB_TOP to TUI_BOTTOM, shows the
 * item's own rows, open and ended, or, opened as Used with it, the open rows
 * of the items reserved for it or in use with it, their host; Ctrl-PgDn and
 * Ctrl-PgUp switch between the two.  A late row is drawn TA_ALERT by the view
 * beside the '!' of its first column, so the mark shows on every palette.
 *
 * Enter shows the selected row in its kind's form and Ctrl-E edits it;
 * the Record menu's Return or release asks for the date it ended, as
 * YYYY-MM-DD with or without the dashes (empty for today), and returns
 * or releases it (LOAN RETURN); Ins chooses a kind (lend, reserve, use
 * with), and Ctrl-L, Ctrl-R and Ctrl-U take one, for a new row's form:
 * in the tab for the tab's item, or, beside the rows it hosts, for an
 * item asked for with the tab's item as its host (not a loan); in the
 * list for an item asked for among the available ones (scr_ask_item).
 * The Record menu's Open item shows the row's item: the list opens its
 * item form, the tab hands it to the item form, which shows it in place
 * of the tab's item.  Ctrl-E, Return or release and the new rows need
 * level 2 and are refused below it with tui_allow.  A change to a
 * row changes the counts of its item and its host, so the tab tells the
 * item form to load the item again after one.  The keys read the
 * selected row's kind and item back from its text (scr_loan_rowkind,
 * scr_loan_rowitem), so they need no request.
 *
 * A form is a pop-up over the list: what it covers is saved when it
 * opens and put back when it closes.  A new row is sent through
 * form_save as "LOAN OUT|RESERVE|USE <item> ...", the item being the
 * form's key field, preset and read-only; the quantity (shown as 1, the
 * server's default, and sent only when changed, so a form left at once
 * does not ask to discard anything) and the date out can be set only
 * then, and later saves are LOAN SET.  The item's maker and model show
 * below its ID, the host's after its own (ITEM GET); F4 on the host
 * lists the active items to choose it from (scr_find_item), an ID typed
 * is looked up, and text typed, or digits that name no item, is chosen
 * from the items holding it with F4 or when the form is saved.
 *
 * As in SCRMAINT.C, requests are built and sent only in small leaf
 * functions, and a form that closed after a save leaves a note the list
 * shows once it has fetched its rows again.
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
#include "ITEMLS.H"
#include "SCRSFORM.H"
#include "SCRNFORM.H"

#define MB_T_TAB        870     /* the tab panel's title                */
#define MB_HINT         871     /* hints of the list, ^1 the next view  */
#define MB_HINTTAB      872     /* ... of the tab's own rows            */
#define MB_HINTVIEW     873     /* hints of a form in view mode         */
#define MB_HINTEDIT     874     /* ... when editing                     */
#define MB_HINTHOST     875     /* ... when editing a row with a host   */
#define MB_W_VIEW       876     /* the views' names, by SCR_LV_         */
#define MB_W_VLOAN      877
#define MB_W_VRES       878
#define MB_W_VUSE       879
#define MB_W_VOVER      880
#define MB_W_VALL       881
#define MB_W_OWN        882     /* the tab's rows: the item's own       */
#define MB_W_HOST       883     /* ... those of the items it hosts      */
#define MB_INFOOWN      884     /* status info: the item's own rows     */
#define MB_INFOHOST     885     /* ... those of the items it hosts      */
#define MB_BADDATE      886     /* the date ended is not a date         */
#define MB_K_NEW        888     /* the kinds' choice, by SCR_K_         */
#define MB_K_NEWRES     889
#define MB_K_NEWUSE     890

/* An open list: every item's rows (item "") or the Loans tab of item. */
typedef struct {
    lv  *v;
    lv_src src;                 /* the view's source (the view keys
                                   change it)                           */
    scr_lq q;                   /* its arg                              */
    char item[LINK_IDLEN + 1];  /* the tab's item, or ""                */
    u8   view;                  /* the list: the SCR_LV_ shown          */
    u8   host;                  /* the tab: 1 for the rows it hosts     */
    u8   changed;               /* the tab: a row was changed           */
    u8   kind;                  /* the form's row's kind                */
    char id[LINK_IDLEN + 1];    /* the row in the form                  */
    const char *preset;         /* a new row's preset host, or NULL     */
    char open[LV_KEYSZ];        /* the tab: an item Open item asked to
                                   show                                 */
} ln_ctx;

/* The Record menus of the list, of the tab's own rows and of the rows
   the tab's item hosts. */
#define LN_VERBS        (SV_BIT(SV_OPEN) | SV_BIT(SV_EDIT) | SV_BIT(SV_NEW) | \
                         SV_BIT(SV_END) | SV_BIT(SV_ITEM) | SV_BIT(SV_LEND) | \
                         SV_BIT(SV_RES) | SV_BIT(SV_USE) | SV_BIT(SV_NEXT) | \
                         SV_BIT(SV_PREV) | SV_BIT(SV_BACK))
#define LN_TVERBS       (LN_VERBS & ~SV_BIT(SV_ITEM))
#define LN_HVERBS       (LN_VERBS & ~SV_BIT(SV_LEND))

/* ---------------------------------------------------------------------
 * Requests (leaf functions).
 * --------------------------------------------------------------------- */

/* Send "LOAN RETURN id [DATE=date]" (date "": none); the outcome in
   *res, nothing reported.  1 on success. */
static int end_row(const char *id, const char *date, rq_res *res)
{
    rq q;

    rq_begin(&q, scr_loan_w_loan);
    rq_word(&q, "RETURN");
    rq_word(&q, id);
    if (date[0] != '\0')
        rq_pair(&q, "DATE", date);
    return rq_send(tui_s.l, &q, NULL, NULL, res) == RQ_OK;
}

/* ITEM GET id into field of f through fn (scr_loan_kitem or
   scr_loan_khost), the field cleared first; a failure (an item deleted
   since) leaves it blank and is not reported: the row is shown all the
   same. */
static void item_name(form *f, const char *field, const char *id,
                      rq_fn fn)
{
    rq q;
    rq_res res;

    form_load(f, field, "");
    rq_begin(&q, "ITEM GET");
    rq_word(&q, id);
    rq_send(tui_s.l, &q, fn, f, &res);
}

/* ---------------------------------------------------------------------
 * The forms.
 * --------------------------------------------------------------------- */

/* The edit-mode hints of a form of kind: with F4 for a host's list. */
static u16 edit_hint(u8 kind)
{
    return (u16)(kind == SCR_K_LOAN ? MB_HINTEDIT : MB_HINTHOST);
}

/* Save f (form_ops) into c->id, marking in c->changed a row it added or
   changed.  The host typed is first found (scr_find_item): an item's ID
   as it is, else one chosen from the active items holding the text,
   digits that name no item included, the save going on only once one
   is.  A row of an item that cannot be lent (gone, disposed or
   installed: the response names ITEM, the form's key, which cannot be
   changed) also leaves the form.  A host preset for a new row was
   loaded as its original value, so that the untouched form is not
   changed; it is made a change here, for the command to carry it. */
static int loan_save(void *arg, form *f, u8 *cmd)
{
    char id[LINK_IDLEN + 1];
    ln_ctx *c;
    rq_res res;
    const char *v;
    int isnew;
    int rc;

    c = (ln_ctx *)arg;
    isnew = form_mode_get(f) == FORM_NEW;
    v = form_get(f, scr_loan_k_host);
    if (v != NULL && v[0] != '\0') {
        rc = scr_find_item(v, SCR_SI_ACTIVE, 0, id, cmd);
        if (!rc)
            return *cmd != TUI_CMD_NONE ? -1 : 0;
        if (strcmp(id, v) != 0) {
            form_put(f, scr_loan_k_host, id);
            item_name(f, scr_loan_k_hwhat, id, scr_loan_khost);
        }
    }
    if (isnew && c->preset != NULL && v != NULL &&
        strcmp(v, c->preset) == 0) {
        form_load(f, scr_loan_k_host, "");
        form_put(f, scr_loan_k_host, c->preset);
    }
    rc = form_store(f, tui_s.l, scr_loan_w_loan, c->id, (u16)sizeof c->id,
                    scr_loan_k_id, (u16)(MH_TITLE + c->kind), &res, cmd);
    if (rc > 0 || (isnew && form_mode_get(f) == FORM_EDIT))
        c->changed = 1;
    if (rc > 0)
        lv_note((u16)(isnew ? MH_ADDED : MH_SAVED), NULL,
                msg_sub((u16)(MH_TITLE + c->kind), c->id, NULL));
    else if (rc == 0 && strcmp(res.field, scr_loan_k_item) == 0)
        rc = -1;
    return rc;
}

/* F4 on the host field, the form's only code field (val not NULL): the
   host chosen from the pick list of the active items holding the text
   typed (all of them for an ID or nothing) into val; or a host typed
   (val NULL), whose ID, when it may be one (scr_search_item), is looked
   up, text being left for F4 or the save.  Then the host's maker and
   model (none for text or no such item).  The form always passes its
   ln_ctx and the host's field, so the first test never fires; it keeps
   the parameters, which form_pick_fn requires, referenced. */
static int loan_pick(void *arg, form *f, u16 field, char *val)
{
    char typed[LINK_MAXFIND + 1];
    char id[LINK_IDLEN + 1];
    scr_query q;
    u8 cmd;
    int got;

    if (arg == NULL || field >= SCR_NF_COUNT)
        return 0;
    form_get_text(f, scr_loan_k_host, typed, (u16)sizeof typed);
    if (val == NULL) {
        if (scr_search_item(&q, typed, SCR_SI_ALL))
            item_name(f, scr_loan_k_hwhat, scr_search_id(&q),
                      scr_loan_khost);
        else
            form_load(f, scr_loan_k_hwhat, "");
        return 0;
    }
    got = scr_find_item(typed, SCR_SI_ACTIVE, 1, id, &cmd);
    if (cmd != TUI_CMD_NONE) {
        form_stop(f, cmd);
        return 0;
    }
    if (got) {
        strcpy(val, id);
        item_name(f, scr_loan_k_hwhat, id, scr_loan_khost);
    }
    return got;
}

#define LE_VERBS        (SV_BIT(SV_EDIT) | SV_BIT(SV_BACK))

/* Run form f over list c, its row of kind c->kind, with its Record
   menu, until it is left; the command to return. */
static u8 loan_edit(form *f, ln_ctx *c)
{
    form_ops ops;

    ops.save = loan_save;
    ops.key = NULL;
    ops.hint = edit_hint(c->kind);
    tui_hint(msg((u16)(form_mode_get(f) == FORM_VIEW ? MB_HINTVIEW :
                       ops.hint)));
    return form_loop(f, &ops, c, LE_VERBS);
}

/* The form of kind for row id in mode over list c, or, with id NULL and
   mode FORM_NEW, a new row of item, its host preset to host unless that
   is NULL. */
static u8 loan_form(ln_ctx *c, const char *id, u8 kind, const char *item,
                    const char *host, u8 mode)
{
    form_field fields[SCR_NF_COUNT];
    form_def def;
    form *f;
    cmem_blk *save;
    const char *v;
    u8 cmd;

    cmd = TUI_CMD_NONE;
    c->kind = kind;
    c->preset = host;
    txt_copy(c->id, id, (u16)sizeof c->id);
    scr_loan_form(kind, &def, fields);
    save = vid_save(def.top, def.left, def.h, def.w);
    f = form_open(&def, mode, loan_pick, NULL, c);
    if (save == NULL || f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
    } else if (id == NULL) {
        form_load(f, itl_k_itemid, item);
        scr_loan_new(f, kind);
        item_name(f, scr_loan_k_what, item, scr_loan_kitem);
        if (host != NULL) {
            form_load(f, scr_loan_k_host, host);
            item_name(f, scr_loan_k_hwhat, host, scr_loan_khost);
        }
        cmd = loan_edit(f, c);
    } else if (tui_req("LOAN GET", c->id, form_kline, f, NULL, &cmd)) {
        scr_loan_loaded(f);
        form_set_title(f, msg_sub((u16)(MH_TITLE + kind), c->id, NULL));
        item_name(f, scr_loan_k_what, form_get(f, itl_k_itemid),
                  scr_loan_kitem);
        v = form_get(f, scr_loan_k_host);
        if (v != NULL && v[0] != '\0')
            item_name(f, scr_loan_k_hwhat, v, scr_loan_khost);
        cmd = loan_edit(f, c);
    }
    form_close(f);
    vid_restore(save);
    return cmd;
}

/* ---------------------------------------------------------------------
 * The lists.
 * --------------------------------------------------------------------- */

/* Make the source show what c says: view c->view of every item's rows,
   or the tab item's own rows or those it hosts. */
static void ln_source(ln_ctx *c)
{
    if (c->item[0] == '\0') {
        scr_loan_view(&c->src, &c->q, c->view);
        return;
    }
    memset(&c->q, 0, sizeof c->q);
    c->q.kind = SCR_NKINDS;
    strcpy(c->host ? c->q.host : c->q.item, c->item);
    c->src.header[0] = (u16)(c->host ? MH_HDR : MH_HDRTAB);
    c->src.empty = (u16)(c->host ? MH_EMPTYHOST : MH_EMPTY);
}

/* Draw the frame, the hints (kept under a message still up when keep
   is 1) and, in the tab, which rows it shows; and give the Record menu
   the verbs of the rows shown. */
static void ln_draw(ln_ctx *c, int keep)
{
    const char *h;

    if (c->item[0] != '\0') {
        tui_box(SCR_TAB_TOP, 0, SCR_TAB_H, VID_COLS, TB_SINGLE,
                msg(MB_T_TAB));
        tui_info(msg((u16)(c->host ? MB_INFOHOST : MB_INFOOWN)));
        h = msg_sub(MB_HINTTAB, msg((u16)(c->host ? MB_W_OWN : MB_W_HOST)),
                    NULL);
        tui_record(c->host ? LN_HVERBS : LN_TVERBS);
    } else {
        tui_box(TUI_TOP, 0, TUI_WORKROWS, VID_COLS, TB_SINGLE,
                msg((u16)(MH_T_VIEW + c->view)));
        h = msg_sub(MB_HINT,
                    msg((u16)(MB_W_VIEW + (c->view + 1) % SCR_NVIEWS)),
                    NULL);
        tui_record(LN_VERBS);
    }
    if (keep)
        tui_hint_keep(h);
    else
        tui_hint(h);
}

/* Fetch the rows again after a change and show the pending note
   (lv_reload); the command a failure asks to leave with. */
static u8 ln_reload(ln_ctx *c)
{
    lv_reload(c->v);
    return lv_leave(c->v);
}

/* After a form or the item form: draw the list again (leaving a message
   that screen showed until the next key), fetch its rows again and
   show the note it left. */
static u8 ln_back(ln_ctx *c, u8 cmd)
{
    if (cmd != TUI_CMD_NONE)
        return cmd;
    ln_draw(c, 1);
    return ln_reload(c);
}

/* tui_label_fn of the kinds' choice, arg the first kind offered: entry
   i, the action of that kind + i. */
static void kind_label(void *arg, u16 i, char *buf)
{
    msg_get((u16)(MB_K_NEW + *(const u8 *)arg + i), buf, TUI_PICKW + 1);
}

/* The kind (SCR_K_) of a new row of key k: Ctrl-L, Ctrl-R or Ctrl-U;
   SCR_NKINDS for Ins, which asks for it. */
static u8 key_kind(u16 k)
{
    if (k == KEY_CTRL('L'))
        return SCR_K_LOAN;
    if (k == KEY_CTRL('R'))
        return SCR_K_RES;
    return (u8)(k == KEY_CTRL('U') ? SCR_K_USE : SCR_NKINDS);
}

/* Ins, Ctrl-L, Ctrl-R or Ctrl-U (key k): a new row, of the kind chosen
   or of k's, for the tab's item; beside the rows the tab's item hosts,
   for an item asked for, reserved for it or in use with it (a loan
   there does nothing); in the list, for an item asked for. */
static u8 ln_new(ln_ctx *c, u16 k)
{
    char item[LINK_IDLEN + 1];
    u16 sel;
    u8 first;
    u8 kind;
    u8 cmd;

    first = (u8)(c->host ? SCR_K_RES : SCR_K_LOAN);
    kind = key_kind(k);
    if (kind < first || !tui_allow(TUI_LVL_OPER))
        return TUI_CMD_NONE;
    sel = 0;
    if (kind == SCR_NKINDS) {
        if (!tui_pick(msg(MH_Q_NEW), NULL, (u16)(SCR_NKINDS - first),
                      kind_label, &first, &sel))
            return TUI_CMD_NONE;
        kind = (u8)(first + sel);
    }
    if (c->item[0] != '\0' && !c->host)
        return ln_back(c, loan_form(c, NULL, kind, c->item, NULL,
                                    FORM_NEW));
    if (!scr_ask_item(msg((u16)(MH_T_NEW + kind)), SCR_SI_AVAIL, item, &cmd))
        return cmd;
    return ln_back(c, loan_form(c, NULL, kind, item,
                                c->host ? c->item : NULL, FORM_NEW));
}

/* The typed date t (YYYY-MM-DD, the dashes optional) in wire form into
   w (MSG_DATELEN + 1 bytes); 1 when t is empty or gives MSG_DATELEN
   digits with nothing but dashes between them, else 0.  The server
   checks that the digits make a real date. */
static int wire_date(const char *t, char *w)
{
    u16 n;

    n = 0;
    for (; *t != '\0'; t++) {
        if (*t == '-')
            continue;
        if (*t < '0' || *t > '9' || n >= MSG_DATELEN)
            return 0;
        w[n++] = *t;
    }
    w[n] = '\0';
    return n == 0 || n == MSG_DATELEN;
}

/* Return or release: row key, of kind, on a date asked for (empty:
   today). */
static u8 ln_end(ln_ctx *c, const char *key, u8 kind)
{
    char typed[FORM_DATEW + 1];
    char date[MSG_DATELEN + 1];
    rq_res res;
    u8 cmd;

    if (!tui_allow(TUI_LVL_OPER))
        return TUI_CMD_NONE;
    typed[0] = '\0';
    if (!tui_prompt(msg_sub((u16)(MH_Q_END + kind), key, NULL),
                    msg(MH_L_RETDATE), typed, FORM_DATEW, 0))
        return TUI_CMD_NONE;
    if (!wire_date(typed, date)) {
        tui_msg(msg(MB_BADDATE), 1);
        return TUI_CMD_NONE;
    }
    if (!end_row(key, date, &res)) {
        cmd = tui_fail(&res);
        if (cmd == TUI_CMD_NONE && res.code == RQ_ENOTFOUND)
            cmd = ln_reload(c);
        return cmd;
    }
    c->changed = 1;
    lv_note((u16)(kind == SCR_K_LOAN ? MH_ENDED : MH_RELEASED), NULL,
            msg_sub((u16)(MH_TITLE + kind), key, NULL));
    return ln_reload(c);
}

/* Open the view of list c, in the work area or the tab's panel; NULL,
   after saying so, when memory is short. */
static lv *ln_view(ln_ctx *c)
{
    lv *v;

    if (c->item[0] != '\0')
        v = lv_open(tui_s.l, &c->src, (u8)(SCR_TAB_TOP + 1), 1,
                    (u8)(SCR_TAB_H - 2), VID_COLS - 2);
    else
        v = lv_open(tui_s.l, &c->src, (u8)(TUI_TOP + 1), 1,
                    (u8)(TUI_WORKROWS - 2), VID_COLS - 2);
    if (v == NULL)
        tui_msg(msg(MT_NOMEM), 1);
    return v;
}

/* Ctrl-PgDn or Ctrl-PgUp (key k): show the next view or the one before,
   or the tab's other rows, in the same view.  When its rows cannot be
   fetched and the screen stays, it is shown empty under the failure's
   message, in a view opened again (the old one still holds the rows
   before; c->v is NULL when that fails).  The command to leave with. */
static u8 ln_swap(ln_ctx *c, u16 k)
{
    if (c->item[0] != '\0')
        c->host ^= 1;
    else
        c->view = (u8)((c->view + (k == KEY_CPGDN ? 1 : SCR_NVIEWS - 1)) %
                       SCR_NVIEWS);
    ln_source(c);
    ln_draw(c, 0);
    if (lv_home(c->v) != RQ_OK && lv_leave(c->v) == TUI_CMD_NONE) {
        lv_close(c->v);
        c->v = ln_view(c);
        if (c->v == NULL)
            return TUI_CMD_NONE;
        lv_paint(c->v);
    }
    return lv_leave(c->v);
}

/* Enter, Ctrl-E, SV_END or SV_ITEM (key k) on the selected row key: its
   form, its end, or its item (none in the tab of its own rows), shown by
   the list or, from the rows the tab's item hosts, handed to the item
   form in c->open with *leave 1.  The command to leave with. */
static u8 ln_row(ln_ctx *c, u16 k, const char *key, int *leave)
{
    char text[LV_ROWTEXT + 1];
    char id[LINK_IDLEN + 1];
    u8 kind;

    lv_seltext(c->v, text);
    kind = scr_loan_rowkind(text);
    if (k == KEY_ENTER)
        return ln_back(c, loan_form(c, key, kind, NULL, NULL, FORM_VIEW));
    if (k == KEY_CTRL('E'))
        return tui_allow(TUI_LVL_OPER) ?
               ln_back(c, loan_form(c, key, kind, NULL, NULL, FORM_EDIT)) :
               TUI_CMD_NONE;
    if (k == SK(SV_END))
        return ln_end(c, key, kind);
    if ((c->item[0] != '\0' && !c->host) || !scr_loan_rowitem(text, id))
        return TUI_CMD_NONE;
    if (c->item[0] == '\0')
        return ln_back(c, scr_item_open(id, FORM_VIEW));
    txt_copy(c->open, id, (u16)sizeof c->open);
    *leave = 1;
    return TUI_CMD_NONE;
}

static u8 ln_run(ln_ctx *c)
{
    char key[LV_KEYSZ];
    u16 k;
    u8 cmd;
    int used;
    int leave;

    ln_draw(c, 0);
    lv_home(c->v);
    cmd = lv_leave(c->v);
    leave = 0;
    while (cmd == TUI_CMD_NONE && !leave) {
        k = lv_run(c->v);
        cmd = lv_leave(c->v);
        if (cmd != TUI_CMD_NONE)
            break;
        switch (k) {
        case KEY_ESC:
            return TUI_CMD_NONE;
        case KEY_CPGDN:
        case KEY_CPGUP:
            cmd = ln_swap(c, k);
            if (c->v == NULL)
                return TUI_CMD_NONE;
            break;
        case KEY_INS:
        case KEY_CTRL('L'):
        case KEY_CTRL('R'):
        case KEY_CTRL('U'):
            cmd = ln_new(c, k);
            break;
        case KEY_ENTER:
        case KEY_CTRL('E'):
        case SK(SV_END):
        case SK(SV_ITEM):
            if (lv_selected(c->v, key))
                cmd = ln_row(c, k, key, &leave);
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

/* Open and run every item's rows (item "") or the Loans tab of item,
   on the rows it hosts when host is 1, with *changed set when the tab
   changed a row, and open (LV_KEYSZ bytes) the item Open item asked the
   item form to show, or ""; the command to leave with. */
static u8 ln_open(const char *item, u8 host, u8 *changed, char *open)
{
    const char *oldtopic;
    u32 oldverbs;
    ln_ctx c;
    u8 cmd;

    oldtopic = tui_topic("LOANS");
    oldverbs = tui_record(0);
    lv_note(0, NULL, NULL);
    memcpy(&c.src, &scr_loan_src, sizeof c.src);
    c.src.arg = &c.q;
    txt_copy(c.item, item, (u16)sizeof c.item);
    c.view = SCR_LV_OPEN;
    c.host = host;
    c.changed = 0;
    c.open[0] = '\0';
    ln_source(&c);
    cmd = TUI_CMD_NONE;
    c.v = ln_view(&c);
    if (c.v != NULL) {
        cmd = ln_run(&c);
        lv_close(c.v);
    }
    tui_info("");
    tui_record(oldverbs);
    tui_topic(oldtopic);
    *changed = c.changed;
    strcpy(open, c.open);
    return cmd;
}

u8 scr_loans(u8 cmd)
{
    char open[LV_KEYSZ];
    u8 changed;

    if (cmd != SCMD_LOANS)
        return TUI_CMD_NONE;
    return ln_open("", 0, &changed, open);
}

void scr_loan_tab(const char *id, scr_tab *t)
{
    t->cmd = ln_open(id, 0, &t->reload, t->open);
}

void scr_loan_used(const char *id, scr_tab *t)
{
    t->cmd = ln_open(id, 1, &t->reload, t->open);
}

u8 scr_loan_add(const char *item, u16 k)
{
    ln_ctx c;

    return loan_form(&c, NULL, key_kind(k), item, NULL, FORM_NEW);
}
