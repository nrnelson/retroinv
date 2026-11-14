/*
 * SCRSRCH.C - the item search screen of the full-screen client (SCR.H).
 *
 * The search form of SCRSFORM.H, starting on the field of the command,
 * in a box: Enter runs the search its values ask for, whose results are an
 * item list (scr_item_list), a new item there taking the location the
 * search filters on, if any; Esc there comes back to the form with its values
 * kept.  A search the form's values cannot make is refused on the hint
 * row (scr_search_why), the form staying.
 * The values are no record, so the form leaves on Esc and the global keys
 * at once, without asking (form_noask).
 */
#define SCRSHR_LOCF
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "MSG.H"
#include "TUI.H"
#include "FORM.H"
#include "SCR.H"
#include "SCRSHR.H"
#include "SCRSFORM.H"

#define MS_HINT         621     /* hints of the form                    */

static const char k_items[] = "ITEMS";  /* help topic of the results  */

/* The search screen's state. */
typedef struct {
    const form_def *def;
    scr_query q;                /* the values of the last search        */
    u8   listed;                /* the last search showed a result list */
    u8   leave;                 /* a pick list ended the session: the
                                   command to leave with               */
} sr_ctx;

/* F4 on the category, location or tags, or a location typed: its pick
   list or path (scr_pick_field, scr_pick_tag, scr_loc_field). */
static int sr_pick(void *arg, form *f, u16 field, char *val)
{
    sr_ctx *c;
    const form_field *d;

    c = (sr_ctx *)arg;
    d = &c->def->fields[field];
    if (strcmp(d->name, scr_search_fields[SCR_SF_CAT].name) == 0)
        return scr_pick_field(f, SCR_CK_CAT, val, MS_HINT, &c->leave);
    if (strcmp(d->name, scr_search_fields[SCR_SF_TAG].name) == 0)
        return scr_pick_tag(f, val, d->width, MS_HINT, &c->leave);
    return scr_loc_field(f, &scr_search_loc, NULL, val, MS_HINT,
                         &c->leave);
}

/* Run the search the form's values ask for; the command to return. */
static u8 sr_run(form *f, sr_ctx *c)
{
    u8 k;

    k = scr_search_get(&c->q, f);
    c->listed = (u8)(k != SCR_SQ_NONE);
    if (k == SCR_SQ_NONE) {
        tui_msg(msg(scr_search_why(&c->q)), 1);
        return TUI_CMD_NONE;
    }
    return scr_item_list(k_items, scr_search_cmds[k], scr_search_args,
                         &c->q, scr_search_title(&c->q), MS_NONE,
                         k != SCR_SQ_SERIAL && c->q.loc[0] != '\0' &&
                         strcmp(c->q.loc, "0") != 0 ? c->q.loc : NULL, 0, 0);
}

u8 scr_search(u8 cmd)
{
    sr_ctx c;
    form_def def;
    form *f;
    u16 k;
    int used;

    scr_search_form(&def, cmd);
    c.def = &def;
    c.leave = TUI_CMD_NONE;
    f = form_open(&def, FORM_EDIT, sr_pick, NULL, &c);
    if (f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return TUI_CMD_NONE;
    }
    cmd = TUI_CMD_NONE;
    tui_hint(msg(MS_HINT));
    form_noask(f);
    for (;;) {
        k = form_run(f);
        if (c.leave != TUI_CMD_NONE) {
            cmd = c.leave;
            break;
        }
        if (k == KEY_ESC)
            break;
        if (k == KEY_ENTER) {
            cmd = sr_run(f, &c);
            if (cmd != TUI_CMD_NONE)
                break;
            if (c.listed) {
                tui_clear(TUI_TOP, TUI_BOTTOM);
                tui_hint_keep(msg(MS_HINT));
            }
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
