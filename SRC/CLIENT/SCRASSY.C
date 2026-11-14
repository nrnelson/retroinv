/*
 * SCRASSY.C - the assembly screens of the full-screen client (SCR.H):
 * the list of assemblies, the Parts tab of the item form, and the
 * assembly verbs of the item lists.
 *
 * The assemblies are the item list over ASSY LIST TOP=1, the top-level
 * items with parts, shown by scr_item_list with the item list's columns
 * and keys.
 *
 * The Parts tab is a list view over the parts list of SCRAFORM.H for
 * one item, in the tab's panel from SCR_TAB_TOP to TUI_BOTTOM.  Enter
 * hands the selected part back to the item form, which shows it
 * instead; Ins (the Record menu's Install part) asks for the ID of an
 * item and installs it in this one (ASSY INSTALL), Take out takes the
 * selected part out of its assembly to a location chosen from the
 * location pick list, or to none (ASSY REMOVE).  Both need level 2 and
 * are refused with tui_allow below it.
 *
 * The Record menu verbs of the item lists act on the selected item the
 * same way: Move moves it with all its parts to a location (ASSY MOVE),
 * Install in installs it in an item asked for (scr_ask_item: its ID, or
 * one chosen from the item pick list), Take out takes it out of its
 * assembly to a location.
 *
 * The server checks every change; its reason is shown as it gives it,
 * and an ID typed by the user that names no item as scr_assy_send says
 * rather than "the record no longer exists".  As elsewhere, requests are
 * built and sent only in a small leaf function, so no request buffer is
 * held while a prompt or a pick list is open.
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
#include "SCR.H"
#include "SCRSHR.H"
#include "SCRVERB.H"
#include "SCRAFORM.H"
#include "SCRSFORM.H"

#define MA_T_TAB        802     /* the tab panel's title                */
#define MA_HINTTAB      805     /* hints of the tab                     */

/* The Parts tab. */
typedef struct {
    lv  *v;
    const char *item;           /* the item whose parts are shown       */
} ab_ctx;

/* ---------------------------------------------------------------------
 * Locations.
 * --------------------------------------------------------------------- */

/* rq_fn of ask_loc: the item's LOC_ID into the buffer arg points at
   (LINK_IDLEN + 1 bytes). */
static int loc_kline(void *arg, const rq_line *ln)
{
    if (ln->kind == 'K' && ln->n >= 2 && strcmp(ln->v[0], "LOC_ID") == 0)
        txt_copy((char *)arg, ln->v[1], LINK_IDLEN + 1);
    return 0;
}

/* Choose a location, or none, for item from the pick list, which opens
   on the item's own (ITEM GET; a part lies where its assembly does),
   into *x (its id "0" for none), the item's LOC_ID into cur
   (LINK_IDLEN + 1 bytes); 1 when chosen, else 0 with *cmd the command a
   failed request asked to leave with and res->code RQ_ENOTFOUND when
   the item has gone (reported). */
static int ask_loc(const char *item, char *cur, scr_locinfo *x,
                   rq_res *res, u8 *cmd)
{
    strcpy(cur, "0");
    res->code = RQ_OK;
    if (!tui_req("ITEM GET", item, loc_kline, cur, res, cmd))
        return 0;
    if (scr_pick_loc(MO_PICK, MO_NOLOC, cur, NULL, x))
        return 1;
    *cmd = scr_pickleave;
    return 0;
}

/* ---------------------------------------------------------------------
 * The Parts tab.
 * --------------------------------------------------------------------- */
static void ab_draw(void)
{
    tui_box(SCR_TAB_TOP, 0, SCR_TAB_H, VID_COLS, TB_SINGLE, msg(MA_T_TAB));
    tui_hint(msg(MA_HINTTAB));
}

/* After a change: fetch the rows again and show the note, but only when
   the fetch succeeded: a failure's message stays instead. */
static u8 ab_reload(ab_ctx *c)
{
    lv_reload(c->v);
    return lv_leave(c->v);
}

/* Ins: install in this one an item asked for (scr_ask_item). */
static u8 ab_install(ab_ctx *c)
{
    char child[LINK_IDLEN + 1];
    rq_res res;
    u8 cmd;

    if (!tui_allow(TUI_LVL_OPER))
        return TUI_CMD_NONE;
    if (!scr_ask_item(msg_sub(MA_Q_CHILD, c->item, NULL), SCR_SI_ALL, child,
                      &cmd))
        return cmd;
    if (scr_assy_send(tui_s.l, scr_assy_w_install, child, c->item, NULL,
                      SCR_AT_CHILD, &res) != RQ_OK)
        return tui_fail(&res);
    lv_note(MA_INSTALLED, child, c->item);
    return ab_reload(c);
}

/* Take out: take the selected part out to a location from the pick list, which
   opens on its assembly's.  A
   part that has gone, or that another session has already taken out
   (202 naming CHILD), makes the rows come again. */
static u8 ab_remove(ab_ctx *c)
{
    char part[LV_KEYSZ];
    char cur[LINK_IDLEN + 1];
    scr_locinfo x;
    rq_res res;
    u8 cmd;
    int ok;

    if (!lv_selected(c->v, part) || !tui_allow(TUI_LVL_OPER))
        return TUI_CMD_NONE;
    ok = ask_loc(part, cur, &x, &res, &cmd);
    tui_hint_keep(msg(MA_HINTTAB));
    if (!ok)
        return cmd == TUI_CMD_NONE && res.code == RQ_ENOTFOUND ?
               ab_reload(c) : cmd;
    if (scr_assy_send(tui_s.l, scr_assy_w_remove, part, NULL, x.id,
                      SCR_AT_NONE, &res) != RQ_OK) {
        cmd = tui_fail(&res);
        if (cmd == TUI_CMD_NONE && (res.code == RQ_ENOTFOUND ||
            (res.code == RQ_ECONSTR &&
             strcmp(res.field, scr_assy_k_child) == 0)))
            cmd = ab_reload(c);
        return cmd;
    }
    lv_note(MA_REMOVED, part, x.name);
    return ab_reload(c);
}

static u8 ab_run(ab_ctx *c, scr_tab *t)
{
    char key[LV_KEYSZ];
    u16 k;
    u8 cmd;
    int used;

    ab_draw();
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
            if (lv_selected(c->v, key)) {
                txt_copy(t->open, key, (u16)sizeof t->open);
                return TUI_CMD_NONE;
            }
            break;
        case KEY_INS:
            cmd = ab_install(c);
            break;
        case SK(SV_OUT):
            cmd = ab_remove(c);
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

#define AS_VERBS        (SV_BIT(SV_OPEN) | SV_BIT(SV_PART) | SV_BIT(SV_OUT) | \
                         SV_BIT(SV_BACK))

void scr_assy_tab(const char *id, scr_tab *t)
{
    const char *oldtopic;
    u32 oldverbs;
    char item[LINK_IDLEN + 1];
    lv_src src;
    ab_ctx c;

    oldtopic = tui_topic("PARTS");
    oldverbs = tui_record(AS_VERBS);
    txt_copy(item, id, (u16)sizeof item);
    t->cmd = TUI_CMD_NONE;
    t->reload = 0;
    t->open[0] = '\0';
    lv_note(0, NULL, NULL);
    memcpy(&src, &scr_assy_src, sizeof src);
    src.arg = item;
    c.item = item;
    c.v = lv_open(tui_s.l, &src, (u8)(SCR_TAB_TOP + 1), 1,
                  (u8)(SCR_TAB_H - 2), VID_COLS - 2);
    if (c.v == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
    } else {
        t->cmd = ab_run(&c, t);
        lv_close(c.v);
    }
    tui_record(oldverbs);
    tui_topic(oldtopic);
}

/* ---------------------------------------------------------------------
 * The assemblies and the keys of item lists.
 * --------------------------------------------------------------------- */
/* arg is the value of TOP=. */
static void top_args(void *arg, rq *q)
{
    rq_pair(q, "TOP", (const char *)arg);
}

u8 scr_assy(u8 cmd)
{
    if (cmd != SCMD_ASSY)
        return TUI_CMD_NONE;
    return scr_item_list("ITEMS", "ASSY LIST", top_args, "1",
                         msg(MA_T_LIST), MA_EMPTY, NULL, 0, 0);
}

/* Move and Take out of item lists: send words for id with a location from the
   pick list, and say done (a message of two arguments, id and the
   location's name) when it worked; a move (move 1) to where the item is
   already sends nothing.  A 200 can only mean that item id has gone (an
   unknown location is a 202). */
static int to_loc(const char *words, const char *id, int move, u16 done,
                  u8 *cmd)
{
    char cur[LINK_IDLEN + 1];
    scr_locinfo x;
    rq_res res;

    *cmd = TUI_CMD_NONE;
    if (!tui_allow(TUI_LVL_OPER))
        return 0;
    if (!ask_loc(id, cur, &x, &res, cmd))
        return res.code == RQ_ENOTFOUND;
    if (move && strcmp(x.id, cur) == 0)
        return 0;
    if (scr_assy_send(tui_s.l, words, id, NULL, x.id, SCR_AT_NONE,
                      &res) != RQ_OK) {
        *cmd = tui_fail(&res);
        return res.code == RQ_ENOTFOUND;
    }
    tui_msg(msg_sub(done, id, x.name), 0);
    return 1;
}

int scr_assy_move(const char *id, u8 *cmd)
{
    return to_loc(scr_assy_w_move, id, 1, MA_MOVED, cmd);
}

int scr_assy_out(const char *id, u8 *cmd)
{
    return to_loc(scr_assy_w_remove, id, 0, MA_REMOVED, cmd);
}

int scr_assy_into(const char *id, u8 *cmd)
{
    char parent[LINK_IDLEN + 1];
    rq_res res;

    *cmd = TUI_CMD_NONE;
    if (!tui_allow(TUI_LVL_OPER) ||
        !scr_ask_item(msg_sub(MA_Q_PARENT, id, NULL), SCR_SI_ALL, parent,
                      cmd))
        return 0;
    /* A 200, the typed parent's given as a 202, means that item id has
       gone. */
    if (scr_assy_send(tui_s.l, scr_assy_w_install, id, parent, NULL,
                      SCR_AT_PARENT, &res) != RQ_OK) {
        *cmd = tui_fail(&res);
        return res.code == RQ_ENOTFOUND;
    }
    tui_msg(msg_sub(MA_INSTALLED, id, parent), 0);
    return 1;
}
