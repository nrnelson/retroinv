/*
 * SCRUSER.C - the user list and the user form of the full-screen client
 * (SCR.H), for level 3 only.
 *
 * The list is a list view over USER LIST with the rows of SCRZFORM.H; a
 * disabled user's row is drawn TA_ALERT.  Enter or Ctrl-E edits the
 * selected user, Ins adds one, Del deletes one after asking (the server
 * refuses to delete, disable or demote the last active administrator,
 * 202).
 *
 * The form, over SCRZFORM.H's table, edits what USER SET takes: the
 * full name, the group, the level and the status, the last one only
 * for a saved user; the user name is the key, given once.  It also
 * holds a password typed twice, without echo, which USER ADD carries
 * or, for a saved user, USER RESET sends before the USER SET of the
 * other changes; when that SET is refused, a box says that the password
 * was saved before the failure is reported.  Left empty, the password
 * stays.  form_close wipes both, also when the program ends while the
 * form is open (form_wipe), and rq_send wipes the request lines.
 *
 * A form that closed after a save leaves a note the list shows once it
 * has redrawn itself, as in SCRLOC.C.
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
#include "SCRZFORM.H"

#define MU_HINT         763     /* hints of the list                    */
#define MU_HINTEDIT     770     /* hints of the form                    */

/* ---------------------------------------------------------------------
 * The save.
 * --------------------------------------------------------------------- */

/* Save f (form_ops; arg is the user name, LINK_USERLEN + 1 bytes): for
   a saved user the password typed twice first, then the rest through
   form_save, so that a box can say the password was saved before the
   failure is reported (form_store adds only the retitling after an ADD,
   which a saved user's form never sends). */
static int user_save(void *arg, form *f, u8 *cmd)
{
    char *name;
    const char *pw;
    rq_res res;
    int isnew;
    int rc;

    name = (char *)arg;
    pw = form_get(f, scr_user_fields[SCR_UF_PW].name);
    if (strcmp(pw, form_get(f, scr_user_fields[SCR_UF_REPEAT].name)) != 0) {
        form_field_error(f, scr_user_fields[SCR_UF_REPEAT].name,
                         msg(MU_NOMATCH));
        return 0;
    }
    isnew = form_mode_get(f) == FORM_NEW;
    if (isnew || pw[0] == '\0') {
        rc = form_store(f, tui_s.l, scr_user_w_user, name, LINK_USERLEN + 1,
                        NULL, MU_FTITLE, &res, cmd);
    } else {
        if (scr_user_reset(tui_s.l, name, pw, &res) != RQ_OK)
            return form_fail(f, &res, cmd);
        /* Wiped whole, the SCR_UF_PWW + 1 bytes of each slot, not cut
           at the first byte as by a load of "", so no tail of the
           password stays; both stay unchanged. */
        txt_wipe((char *)pw, SCR_UF_PWW + 1);
        txt_wipe((char *)form_get(f, scr_user_fields[SCR_UF_REPEAT].name),
                 SCR_UF_PWW + 1);
        *cmd = TUI_CMD_NONE;
        rc = 1;
        if (form_save(f, tui_s.l, scr_user_w_user, name, LINK_USERLEN + 1,
                      &res) != RQ_OK) {
            tui_msgbox(msg(MT_ERRTITLE), msg(MU_PWONLY));
            rc = form_fail(f, &res, cmd);
        }
    }
    if (rc > 0)
        lv_note((u16)(isnew ? MU_ADDED : MU_SAVED), name, NULL);
    return rc;
}

/* ---------------------------------------------------------------------
 * The user form.
 * --------------------------------------------------------------------- */
static const form_ops user_ops = { user_save, NULL, MU_HINTEDIT };

/* The form for user name, or a new one when name is NULL. */
static u8 user_form(const char *name)
{
    char cur[LINK_USERLEN + 1];
    form_def def;
    form *f;
    u8 cmd;

    /* Copied, not assigned: Watcom C 6.5 refuses to assign a const
       struct. */
    memcpy(&def, &scr_user_def, sizeof def);
    if (name == NULL) {
        def.fields++;
        def.nfields--;
    }
    txt_copy(cur, name, (u16)sizeof cur);
    f = form_open(&def, (u8)(name != NULL ? FORM_EDIT : FORM_NEW), NULL,
                  NULL, NULL);
    if (f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return TUI_CMD_NONE;
    }
    if (name == NULL ||
        tui_req("USER GET", cur, scr_user_kline, f, NULL, &cmd)) {
        if (name != NULL)
            form_set_title(f, msg_sub(MU_FTITLE, cur, NULL));
        tui_hint(msg(MU_HINTEDIT));
        cmd = form_loop(f, &user_ops, cur, SV_BIT(SV_BACK));
    }
    form_close(f);
    return cmd;
}

/* ---------------------------------------------------------------------
 * The user list.
 * --------------------------------------------------------------------- */

/* Draw the frame and the hints, kept under a message still up when keep
   is 1. */
static void us_draw(int keep)
{
    tui_box(TUI_TOP, 0, TUI_WORKROWS, VID_COLS, TB_SINGLE, msg(MU_TITLE));
    if (keep)
        tui_hint_keep(msg(MU_HINT));
    else
        tui_hint(msg(MU_HINT));
}

/* After the form: draw the list again (leaving a message the form
   showed until the next key), fetch its rows again and show the note
   the form left (lv_reload). */
static u8 us_back(lv *v, u8 cmd)
{
    if (cmd != TUI_CMD_NONE)
        return cmd;
    us_draw(1);
    lv_reload(v);
    return lv_leave(v);
}

static u8 us_run(lv *v)
{
    char key[LV_KEYSZ];
    u16 k;
    u8 cmd;
    int used;

    us_draw(0);
    lv_home(v);
    cmd = lv_leave(v);
    while (cmd == TUI_CMD_NONE) {
        k = lv_run(v);
        cmd = lv_leave(v);
        if (cmd != TUI_CMD_NONE)
            break;
        switch (k) {
        case KEY_ESC:
            return TUI_CMD_NONE;
        case KEY_ENTER:
        case KEY_CTRL('E'):
            if (lv_selected(v, key))
                cmd = us_back(v, user_form(key));
            break;
        case KEY_INS:
            cmd = us_back(v, user_form(NULL));
            break;
        case KEY_DEL:
            if (lv_selected(v, key) &&
                tui_confirm(msg_sub(MU_DELQ, key, NULL)) &&
                scr_del("USER DEL", key, MU_DELETED, &cmd)) {
                lv_reload(v);
                cmd = lv_leave(v);
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

#define US_VERBS        (SV_BIT(SV_EDIT) | SV_BIT(SV_NEW) | SV_BIT(SV_DEL) | \
                         SV_BIT(SV_BACK))

u8 scr_users(u8 cmd)
{
    const char *oldtopic;
    u32 oldverbs;
    lv *v;

    if (!tui_allow(TUI_LVL_ADMIN))
        return TUI_CMD_NONE;
    oldtopic = tui_topic("USERS");
    lv_note(0, NULL, NULL);
    v = lv_open(tui_s.l, &scr_user_src, TUI_TOP + 1, 1, TUI_WORKROWS - 2,
                VID_COLS - 2);
    if (v == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        cmd = TUI_CMD_NONE;
    } else {
        oldverbs = tui_record(US_VERBS);
        cmd = us_run(v);
        tui_record(oldverbs);
        lv_close(v);
    }
    tui_topic(oldtopic);
    return cmd;
}
