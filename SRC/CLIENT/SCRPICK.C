/*
 * SCRPICK.C - the pick lists and location fields the screens of the
 * full-screen client share, the list view over the location tree that
 * the tree screen and the location pick list both show, the item prompt
 * and its pick list, and the Del key's request (SCRSHR.H).
 *
 * A code list (categories, location types, tags) is fetched whole into
 * far memory (CODETAB.H) and shown by tui_pick; the tags come in pages,
 * with their progress on the status line between them, where Esc stops
 * the load and the list is shown as incomplete.  The location pick list
 * is a list view over the tree's rows, their name and type columns, in a
 * pop-up box, after a lead row for no location or the top level; a
 * letter moves to the next location whose name starts with it, as in
 * the tree.  A location field shows the path of the location its value
 * names in the read-only field below it, after F4 and after a value is
 * typed.
 *
 * The item prompt (scr_ask_item) and an item field (scr_find_item) take
 * an item's ID, or text to find the item by, digits that name no item
 * included: the item pick list then shows the items found (SCRSFORM.H,
 * scr_search_item) in a box over the work area, Enter choosing one, Esc
 * going back to the prompt or the field.
 */
#define SCRSHR_LOCF
#define SCRSHR_TREE
#define MSG_PATHS
#define TUI_POPS
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
#include "SCRIFORM.H"

/* The location pick list's box: the frame around the name and type
   columns. */
#define PK_H            16
#define PK_W            (SCR_LT_PICKW + 2)
#define PK_TOP          (TUI_TOP + 3)
#define PK_LEFT         ((VID_COLS - PK_W) / 2)

/* The item pick list's box: across the screen, as its rows are, and
   low, as the cells it covers and its two pages of rows are far blocks
   that grow with its height, and it opens over the deepest screens. */
#define PI_TOP          (TUI_TOP + 4)
#define PI_H            12

/* A code list's entry as its pick list shows it: the code, the name and
   the sort order right-aligned, a blank after each but the last. */
#define CL_W            (LINK_LTYPELEN + 1 + LINK_CODENAMELEN + 1 + CT_ORDERW)
extern char cl_fits[1];
extern char cl_fits[(CL_W <= TUI_PICKW) ? 1 : 2];

static const char k_zero[] = "0";

u8 scr_pickleave;

/* 1 once scr_pick_item's box could not open, which scr_ask_item ends
   its prompt for, so that the message stays on the hint row. */
static u8 pk_nobox;

int scr_del(const char *words, const char *id, u16 note, u8 *cmd)
{
    rq_res res;

    if (tui_req(words, id, NULL, NULL, &res, cmd)) {
        lv_note(note, id, NULL);
        return 1;
    }
    return res.code == RQ_ENOTFOUND;
}

/* ---------------------------------------------------------------------
 * Code lists.
 * --------------------------------------------------------------------- */

const char *const scr_code_tables[] = { "CAT", "LTYPE" };

void scr_code_label(void *arg, u16 i, char *buf)
{
    ct_row r;
    u16 n;

    ct_get((ctab *)arg, i, &r);
    memset(buf, ' ', CL_W);
    memcpy(buf, r.code, strlen(r.code));
    memcpy(buf + LINK_LTYPELEN + 1, r.name, strlen(r.name));
    n = (u16)strlen(r.order);
    memcpy(buf + CL_W - n, r.order, n);
    buf[CL_W] = '\0';
}

/* ct_wait_fn of the tags' load, arg their ctab: the tags found so far
   on the status line, a flush, then 1 when Esc was typed. */
static int tags_wait(void *arg)
{
    char num[TXT_U32LEN + 1];

    txt_u32(num, ((ctab *)arg)->n);
    tui_info(msg_sub(ME_TAGWAIT, num, NULL));
    vid_flush();
    return tui_esc_waiting();
}

int scr_code_load(ctab *t, u8 kind, u8 *cmd)
{
    char info[TUI_ST_INFOW + 1];
    rq_res res;
    int rc;

    *cmd = TUI_CMD_NONE;
    if (!ct_open(t)) {
        tui_msg(msg(MT_NOMEM), 1);
        return 0;
    }
    if (kind == SCR_CK_TAG) {
        txt_copy(info, tui_info_text(), (u16)sizeof info);
        rc = ct_load_tags(t, tui_s.l, tags_wait, t, &res);
        tui_info(info);
    } else {
        rc = ct_load(t, tui_s.l, scr_code_tables[kind], &res);
    }
    if (rc == RQ_OK)
        return 1;
    *cmd = tui_fail(&res);
    ct_close(t);
    return 0;
}

int scr_pick_code(u8 kind, char *code)
{
    ctab own;
    ctab *t;
    ct_row r;
    u16 sel;
    int ok;

    /* The location types are those the open views of the tree hold. */
    t = &own;
    if (kind == SCR_CK_LTYPE)
        t = scr_lt_types(&scr_pickleave);
    else if (!scr_code_load(&own, kind, &scr_pickleave))
        t = NULL;
    if (t == NULL)
        return 0;
    for (sel = 0; sel < t->n; sel++) {
        ct_get(t, sel, &r);
        if (strcmp(r.code, code) == 0)
            break;
    }
    if (sel == t->n)
        sel = 0;
    if (kind != SCR_CK_TAG) {
        ok = tui_pick(msg((u16)(ME_CATS + kind)), msg(ME_HDR), t->n,
                      scr_code_label, t, &sel);
    } else if (t->n > 0) {
        ok = tui_pick(msg((u16)(t->part ? ME_TAGSPART : ME_TAGS)), NULL,
                      t->n, scr_code_label, t, &sel);
    } else {
        /* No tags, or Esc before any came, which needs no word. */
        ok = 0;
        if (!t->part)
            tui_msg(msg(ME_NOTAGS), 0);
    }
    if (ok) {
        ct_get(t, sel, &r);
        strcpy(code, r.code);
    }
    if (kind == SCR_CK_LTYPE)
        scr_lt_types_done();
    else
        ct_close(&own);
    return ok;
}

/* The end of a pick on form f that ok tells: the hints again, and the
   form stopped when a failed request ends the screen.  Returns ok. */
static int pick_done(form *f, int ok, u16 hint, u8 *leave)
{
    tui_hint_keep(msg(hint));
    if (!ok && scr_pickleave != TUI_CMD_NONE) {
        if (leave != NULL)
            *leave = scr_pickleave;
        form_stop(f, scr_pickleave);
    }
    return ok;
}

int scr_pick_field(form *f, u8 kind, char *val, u16 hint, u8 *leave)
{
    if (val == NULL)
        return 0;
    return pick_done(f, scr_pick_code(kind, val), hint, leave);
}

int scr_pick_tag(form *f, char *val, u16 width, u16 hint, u8 *leave)
{
    char tag[LINK_ITAGLEN + 1];
    int ok;

    if (val == NULL)
        return 0;
    tag[0] = '\0';
    ok = scr_pick_code(SCR_CK_TAG, tag);
    if (ok && !ct_tags_add(val, width, tag)) {
        tui_msg(msg(ME_TAGFULL), 1);
        ok = 0;
    }
    return pick_done(f, ok, hint, leave);
}

/* ---------------------------------------------------------------------
 * The location tree's view.
 * --------------------------------------------------------------------- */

/* The location types' list of the views open, loaded by the first. */
static ctab lt_types;
static u8 lt_users;

ctab *scr_lt_types(u8 *cmd)
{
    rq_res res;

    *cmd = TUI_CMD_NONE;
    if (lt_users == 0) {
        if (!ct_open(&lt_types)) {
            tui_msg(msg(MT_NOMEM), 1);
            return NULL;
        }
        if (ct_load(&lt_types, tui_s.l, scr_code_tables[SCR_CK_LTYPE],
                    &res) != RQ_OK) {
            *cmd = tui_fail(&res);
            ct_close(&lt_types);
            return NULL;
        }
    }
    lt_users++;
    return &lt_types;
}

void scr_lt_types_done(void)
{
    if (--lt_users == 0)
        ct_close(&lt_types);
}

static void lt_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    scr_loc_row(r, ((scr_lt *)arg)->types, text, key);
}

/* 1 when the row's name starts with the letter *arg (a u16 key, upper
   case), for the jump of lv_seek. */
static int starts_with(void *arg, const char *key, const char *text)
{
    char c;

    if (key[0] == '\0')
        return 0;
    while (*text == ' ')
        text++;
    c = *text;
    return (u16)(u8)TXT_UPPER(c) == *(const u16 *)arg;
}

int scr_lt_open(scr_lt *t, u8 top, u8 left, u8 h, u8 w, u8 *cmd)
{
    memcpy(&t->src, &scr_loc_src, sizeof t->src);
    t->src.fmt = lt_fmt;
    t->src.arg = t;
    t->types = scr_lt_types(cmd);
    if (t->types == NULL)
        return 0;
    t->v = lv_open(tui_s.l, &t->src, top, left, h, w);
    if (t->v == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        scr_lt_types_done();
        return 0;
    }
    return 1;
}

void scr_lt_close(scr_lt *t)
{
    lv_close(t->v);
    scr_lt_types_done();
}

int scr_lt_letter(scr_lt *t, u16 k)
{
    if (k <= ' ' || k >= 0x80)
        return 0;
    k = (u16)TXT_UPPER(k);
    lv_seek(t->v, starts_with, &k, LV_SEEKMAX);
    return 1;
}

/* Of text t, the w cells at at, blanks cut at both ends, into d (size
   bytes). */
static void lt_cell(char *d, u16 size, char *t, u16 at, u16 w)
{
    u16 e;

    t[at + w] = '\0';
    while (t[at] == ' ')
        at++;
    for (e = (u16)strlen(t); e > at && t[e - 1] == ' '; e--)
        ;
    t[e] = '\0';
    txt_copy(d, t + at, size);
}

void scr_lt_selrow(scr_lt *t, const char *key, scr_locinfo *x)
{
    char s[LV_ROWTEXT + 1];
    u16 n;

    memset(x, 0, sizeof *x);
    txt_copy(x->id, key, (u16)sizeof x->id);
    lv_seltext(t->v, s);
    for (n = 1; s[n] == ' '; n++)
        ;
    x->depth = (u16)((n - 1) / SCR_LT_INDENT + 1);
    lt_cell(x->code, (u16)sizeof x->code, s, SCR_LT_PICKW + 1,
            LINK_LOCLEN);
    lt_cell(x->name, (u16)sizeof x->name, s, 0, SCR_LT_NAMEW + 1);
}

int scr_loc_get(const char *loc, scr_locinfo *x, u8 *cmd)
{
    memset(x, 0, sizeof *x);
    return tui_req("LOC GET", loc, scr_loc_info, x, NULL, cmd);
}

/* ---------------------------------------------------------------------
 * The location pick list and location fields.
 * --------------------------------------------------------------------- */

/* The seek of the current value (is_cur): the location's LOC_ID or
   code, and 1 once a row was found to be it. */
typedef struct {
    const char *cur;
    int hit;
} pk_seek;

/* 1, noted in the pk_seek *arg, when the row is the location it names,
   by its LOC_ID or its code. */
static int is_cur(void *arg, const char *key, const char *text)
{
    char t[LV_ROWTEXT + 1];
    char code[LINK_LOCLEN + 1];
    pk_seek *s;

    s = (pk_seek *)arg;
    if (strcmp(key, s->cur) != 0) {
        strcpy(t, text);
        lt_cell(code, (u16)sizeof code, t, SCR_LT_PICKW + 1, LINK_LOCLEN);
        if (code[0] == '\0' || strcmp(code, s->cur) != 0)
            return 0;
    }
    s->hit = 1;
    return 1;
}

int scr_loc_within(const char *loc, const char *outside, u8 *cmd)
{
    char up[LINK_IDLEN + 1];
    scr_locinfo i;
    u16 n;

    *cmd = TUI_CMD_NONE;
    if (strcmp(loc, outside) == 0)
        return 1;
    txt_copy(up, loc, (u16)sizeof up);
    for (n = 0; n <= LINK_MAXLOCDEPTH && strcmp(up, k_zero) != 0; n++) {
        if (!scr_loc_get(up, &i, cmd))
            return 0;
        if (strcmp(i.id, outside) == 0)
            return 1;
        strcpy(up, i.parent);
    }
    return 0;
}

int scr_pick_loc(u16 title, u16 lead, const char *cur, const char *outside,
                 scr_locinfo *x)
{
    char text[LV_TEXTLEN + 1];
    char key[LV_KEYSZ];
    char miss[LV_KEYSZ];
    pk_seek sk;
    scr_lt t;
    cmem_blk *save;
    u16 k;
    u8 cmd;
    int ok;

    scr_pickleave = TUI_CMD_NONE;
    ok = 0;
    save = vid_save(PK_TOP, PK_LEFT, PK_H, PK_W);
    if (save == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return 0;
    }
    if (scr_lt_open(&t, (u8)(PK_TOP + 1), (u8)(PK_LEFT + 1),
                    (u8)(PK_H - 2), (u8)(PK_W - 2), &cmd)) {
        text[0] = ' ';
        msg_get(lead, text + 1, (u16)(sizeof text - 1));
        lv_lead(t.v, k_zero, text);
        tui_box(PK_TOP, PK_LEFT, PK_H, PK_W, TB_SINGLE, msg(title));
        tui_hint(msg(MT_HINTPICK));
        lv_home(t.v);
        /* The row a seek that missed stopped on, which Enter takes only
           once asked: the seek gives up after LV_SEEKMAX rows. */
        miss[0] = '\0';
        if (cur != NULL && cur[0] != '\0' && strcmp(cur, k_zero) != 0) {
            sk.cur = cur;
            sk.hit = 0;
            lv_seek(t.v, is_cur, &sk, LV_SEEKMAX);
            if (!sk.hit)
                lv_selected(t.v, miss);
        }
        cmd = lv_leave(t.v);
        while (cmd == TUI_CMD_NONE) {
            k = lv_run(t.v);
            cmd = lv_leave(t.v);
            if (cmd != TUI_CMD_NONE || k == KEY_ESC)
                break;
            if (k == KEY_ENTER && lv_selected(t.v, key)) {
                if (strcmp(key, k_zero) == 0) {
                    memset(x, 0, sizeof *x);
                    strcpy(x->id, k_zero);
                    txt_copy(x->name, text + 1, (u16)sizeof x->name);
                } else if (outside != NULL &&
                           scr_loc_within(key, outside, &cmd)) {
                    tui_msg(msg(MO_INSELF), 1);
                    continue;
                } else if (cmd != TUI_CMD_NONE ||
                           !scr_loc_get(key, x, &cmd)) {
                    break;
                }
                if (strcmp(key, miss) != 0 ||
                    tui_confirm(msg_sub(MO_PICKQ, x->name, NULL))) {
                    ok = 1;
                    break;
                }
                continue;
            }
            if (scr_lt_letter(&t, k))
                cmd = lv_leave(t.v);
        }
        scr_lt_close(&t);
    }
    scr_pickleave = cmd;
    vid_restore(save);
    return ok;
}

int scr_loc_show(form *f, const scr_locf *d, const char *val, int load,
                 u8 *cmd)
{
    scr_locinfo i;
    char t[FORM_MAXVAL + 1];
    rq q;
    rq_res res;
    u16 w;
    int rc;

    *cmd = TUI_CMD_NONE;
    w = d->path->dispw != 0 ? d->path->dispw : d->path->width;
    t[0] = '\0';
    if ((val[0] == '\0' && !d->filter) || strcmp(val, k_zero) == 0) {
        msg_get(d->none, t, (u16)(w + 1));
    } else if (val[0] != '\0') {
        memset(&i, 0, sizeof i);
        rq_begin(&q, "LOC GET");
        rq_word(&q, val);
        rc = rq_send(tui_s.l, &q, scr_loc_info, &i, &res);
        if (rc == RQ_ENOTFOUND) {
            tui_msg(msg_sub(MO_NOSUCH, val, NULL), 1);
        } else if (rc != RQ_OK) {
            *cmd = tui_fail(&res);
        } else {
            msg_path(t, i.path, w);
            if (load && i.code[0] != '\0')
                form_load(f, d->loc->name, i.code);
        }
    }
    form_load(f, d->path->name, t);
    return *cmd == TUI_CMD_NONE;
}

int scr_loc_field(form *f, const scr_locf *d, const char *outside,
                  char *val, u16 hint, u8 *leave)
{
    scr_locinfo x;
    u8 cmd;
    int ok;

    ok = 0;
    if (val == NULL) {
        scr_loc_show(f, d, form_get(f, d->loc->name), 0, &cmd);
    } else if (scr_pick_loc(MO_PICK, d->none, val, outside, &x)) {
        ok = 1;
        strcpy(val, x.code[0] != '\0' ? x.code :
                    !d->filter && strcmp(x.id, k_zero) == 0 ? "" : x.id);
        scr_loc_show(f, d, val, 0, &cmd);
    } else {
        cmd = scr_pickleave;
    }
    if (val != NULL)
        tui_hint_keep(msg(hint));
    if (cmd != TUI_CMD_NONE) {
        if (leave != NULL)
            *leave = cmd;
        form_stop(f, cmd);
    }
    return ok;
}

/* ---------------------------------------------------------------------
 * The item prompt and pick list.
 * --------------------------------------------------------------------- */
int scr_pick_item(scr_query *q, char *id)
{
    tui_pop pop;
    lv_src src;
    const char *oldtopic;
    lv *v;
    u16 k;
    u8 cmd;
    int ok;

    scr_pickleave = TUI_CMD_NONE;
    pk_nobox = 1;
    if (!tui_pop_open(&pop, PI_TOP, 0, PI_H, VID_COLS, MT_HINTPICK))
        return 0;
    scr_search_src(&src, q);
    v = lv_open(tui_s.l, &src, (u8)(PI_TOP + 1), 1, (u8)(PI_H - 2),
                VID_COLS - 2);
    if (v == NULL) {
        tui_pop_close(&pop);
        tui_msg(msg(MT_NOMEM), 1);
        return 0;
    }
    pk_nobox = 0;
    ok = 0;
    oldtopic = tui_topic("PICKITEM");
    tui_box(PI_TOP, 0, PI_H, VID_COLS, TB_SINGLE,
            msg((u16)(q->avail ? MI_PICKAVL : MI_PICK)));
    lv_home(v);
    cmd = lv_leave(v);
    while (cmd == TUI_CMD_NONE) {
        k = lv_run(v);
        cmd = lv_leave(v);
        if (cmd != TUI_CMD_NONE || k == KEY_ESC)
            break;
        if (k == KEY_ENTER && lv_selected(v, id)) {
            ok = 1;
            break;
        }
    }
    lv_close(v);
    tui_topic(oldtopic);
    tui_pop_close(&pop);
    scr_pickleave = cmd;
    return ok;
}

/* ITEM GET id: RQ_OK when item id exists; RQ_ENOTFOUND, or RQ_EARG for
   an ID the server refuses (0), when it does not, as the user may have
   typed a model or serial number of digits; any other failure reported,
   *cmd the command it asks to leave with. */
static int item_find(const char *id, u8 *cmd)
{
    rq q;
    rq_res res;
    int rc;

    *cmd = TUI_CMD_NONE;
    rq_begin(&q, "ITEM GET");
    rq_word(&q, id);
    rc = rq_send(tui_s.l, &q, NULL, NULL, &res);
    if (rc != RQ_OK && rc != RQ_ENOTFOUND && rc != RQ_EARG)
        *cmd = tui_fail(&res);
    return rc;
}

int scr_find_item(const char *typed, u8 only, u8 list, char *id, u8 *cmd)
{
    scr_query q;
    int rc;

    *cmd = TUI_CMD_NONE;
    if (scr_search_item(&q, typed, only)) {
        if (list) {
            scr_search_item(&q, "", only);
        } else {
            rc = item_find(q.text, cmd);
            if (rc == RQ_OK) {
                strcpy(id, scr_search_id(&q));
                return 1;
            }
            if (*cmd != TUI_CMD_NONE ||
                (rc != RQ_ENOTFOUND && rc != RQ_EARG))
                return 0;
        }
    }
    if (scr_pick_item(&q, id))
        return 1;
    *cmd = scr_pickleave;
    return 0;
}

int scr_ask_item(const char *title, u8 only, char *id, u8 *cmd)
{
    char t[MSG_MAXLEN + 1];
    char typed[LINK_MAXFIND + 1];

    txt_copy(t, title, (u16)sizeof t);
    *cmd = TUI_CMD_NONE;
    typed[0] = '\0';
    while (tui_prompt(t, msg(MI_L_ITEMID), typed, LINK_MAXFIND, 0)) {
        pk_nobox = 0;
        if (scr_find_item(typed, only, 0, id, cmd))
            return 1;
        if (*cmd != TUI_CMD_NONE || pk_nobox)
            return 0;
    }
    return 0;
}
