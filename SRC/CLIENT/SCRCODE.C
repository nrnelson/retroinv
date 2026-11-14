/*
 * SCRCODE.C - the code list screens of the full-screen client: the
 * categories and the location types (SCR.H).
 *
 * A list is held whole and shown as its pick list is (scr_code_load and
 * scr_code_label of SCRSHR.H), in the box of tui_pick_keys, whose keys
 * act on the selected entry and which stays drawn under what they
 * open; the list is fetched again after each change.  An entry's form
 * edits its name and sort order (SORTORD, which orders the list;
 * SCRKFORM.H's tables); a new entry's code is given once, as the
 * positional word of ADD, and never changes.
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
#include "FORM.H"
#include "SCR.H"
#include "SCRSHR.H"
#include "SCRVERB.H"

/* The form of an entry of kind, open while its form_loop runs. */
typedef struct {
    u8   kind;
    char code[LINK_LTYPELEN + 1];
} ce_ctx;

/* Save f (form_ops), noting it on the hint row with the entry's name. */
static int ce_save(void *arg, form *f, u8 *cmd)
{
    ce_ctx *c;
    rq_res res;
    int isnew;
    int rc;

    c = (ce_ctx *)arg;
    isnew = form_mode_get(f) == FORM_NEW;
    rc = form_store(f, tui_s.l, scr_code_tables[c->kind], c->code,
                    (u16)sizeof c->code, NULL, (u16)(ME_FCAT + c->kind),
                    &res, cmd);
    if (rc > 0)
        tui_msg(msg_sub((u16)(isnew ? ME_ADDED : ME_SAVED),
                        form_get(f, scr_code_defs[c->kind]->
                                    fields[SCR_CF_NAME].name), NULL),
                0);
    return rc;
}

static const form_ops ce_ops = { ce_save, NULL, ME_HINTEDIT };

/* The form of entry r of kind, or a new one when r is NULL; the command
   to return. */
static u8 ce_form(u8 kind, const ct_row *r)
{
    const form_field *fl;
    ce_ctx c;
    form *f;
    u8 cmd;

    fl = scr_code_defs[kind]->fields;
    c.kind = kind;
    c.code[0] = '\0';
    f = form_open(scr_code_defs[kind], (u8)(r != NULL ? FORM_EDIT : FORM_NEW),
                  NULL, NULL, &c);
    if (f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return TUI_CMD_NONE;
    }
    if (r != NULL) {
        strcpy(c.code, r->code);
        form_load(f, fl[SCR_CF_CODE].name, r->code);
        form_load(f, fl[SCR_CF_NAME].name, r->name);
        form_load(f, fl[SCR_CF_ORDER].name, r->order);
        form_set_title(f, msg_sub((u16)(ME_FCAT + kind), r->code, NULL));
    }
    tui_hint(msg(ME_HINTEDIT));
    cmd = form_loop(f, &ce_ops, &c, SV_BIT(SV_BACK));
    form_close(f);
    return cmd;
}

/* Del on entry r of kind: "<table> DEL code" after asking; the server
   refuses a code still in use. */
static u8 ce_del(u8 kind, const ct_row *r)
{
    rq q;
    rq_res res;

    if (!tui_confirm(msg_sub(ME_DELQ, r->name, NULL)))
        return TUI_CMD_NONE;
    rq_begin(&q, scr_code_tables[kind]);
    rq_word(&q, "DEL");
    rq_word(&q, r->code);
    if (rq_send(tui_s.l, &q, NULL, NULL, &res) != RQ_OK)
        return tui_fail(&res);
    tui_msg(msg_sub(ME_DELETED, r->name, NULL), 0);
    return TUI_CMD_NONE;
}

#define CD_VERBS        (SV_BIT(SV_EDIT) | SV_BIT(SV_NEW) | SV_BIT(SV_DEL) | \
                         SV_BIT(SV_BACK))

u8 scr_codes(u8 cmd)
{
    const char *oldtopic;
    u32 oldverbs;
    ctab t;
    ct_row r;
    u8 kind;
    u16 sel;
    u16 n;
    u16 k;
    int used;

    kind = (u8)(cmd == SCMD_CATS ? SCR_CK_CAT : SCR_CK_LTYPE);
    oldtopic = tui_topic(scr_code_defs[kind]->topic);
    oldverbs = tui_record(CD_VERBS);
    sel = 0;
    cmd = TUI_CMD_NONE;
    while (cmd == TUI_CMD_NONE && scr_code_load(&t, kind, &cmd)) {
        /* Under the list nothing of the one before it or its form. */
        tui_clear(TUI_TOP, TUI_BOTTOM);
        k = tui_pick_keys(msg((u16)(ME_CATS + kind)), msg(ME_HDR), t.n,
                          scr_code_label, &t, &sel, ME_HINT);
        n = t.n;
        if (n > 0)
            ct_get(&t, sel, &r);
        ct_close(&t);
        if (k == KEY_ESC)
            break;
        if (k == KEY_INS) {
            if (tui_allow(TUI_LVL_OPER))
                cmd = ce_form(kind, NULL);
        } else if (k == KEY_ENTER || k == KEY_CTRL('E') || k == KEY_DEL) {
            if (n > 0 && tui_allow(TUI_LVL_OPER))
                cmd = k == KEY_DEL ? ce_del(kind, &r) : ce_form(kind, &r);
        } else {
            cmd = tui_global(k, &used);
            if (used)
                break;
            cmd = TUI_CMD_NONE;
        }
    }
    tui_record(oldverbs);
    tui_topic(oldtopic);
    return cmd;
}
