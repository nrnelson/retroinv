/*
 * WUSER.C - the users screen of INVWIN (WUSER.H).
 *
 * The check and the save are those of INV's user form (SCRUSER.C), in
 * the form dialog's hooks (wf_ops).
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "RQ.H"
#include "MSG.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "SCRZFORM.H"
#include "WFORM.H"
#include "WCOLS.H"
#include "WROWS.H"
#include "WSCR.H"
#include "WPANE.H"
#include "WUSER.H"

/* wf_ops's check: -1 when the password and its repetition are equal,
   else the repetition's index, one less in a new user's form, which
   lacks STATUS, with *mf MU_NOMATCH. */
static int check(void *arg, form *f, u16 *mf)
{
    (void)arg;
    if (strcmp(form_get(f, scr_user_fields[SCR_UF_PW].name),
               form_get(f, scr_user_fields[SCR_UF_REPEAT].name)) == 0)
        return -1;
    *mf = MU_NOMATCH;
    return SCR_UF_REPEAT -
           (form_get(f, scr_user_fields[SCR_UF_STATUS].name) == NULL);
}

/* wf_ops's save: a new user, or a saved one without a password typed,
   by form_save; else the password first (scr_user_reset), then, it and
   its repetition wiped whole, the SCR_UF_PWW + 1 bytes of each slot,
   the other changes by form_save, whose failure gets the note
   MU_PWONLY. */
static int save(form *f, link *l, const char *table, char *id, u16 idsz,
                rq_res *res, u16 *note)
{
    char *pw;
    int rc;

    pw = (char *)form_get(f, scr_user_fields[SCR_UF_PW].name);
    if (form_mode_get(f) == FORM_NEW || pw[0] == '\0')
        return form_save(f, l, table, id, idsz, res);
    rc = scr_user_reset(l, id, pw, res);
    if (rc != RQ_OK)
        return rc;
    txt_wipe(pw, SCR_UF_PWW + 1);
    txt_wipe((char *)form_get(f, scr_user_fields[SCR_UF_REPEAT].name),
             SCR_UF_PWW + 1);
    rc = form_save(f, l, table, id, idsz, res);
    if (rc != RQ_OK)
        *note = MU_PWONLY;
    return rc;
}

static const u16 user_notes[] = { MU_ADDED, MU_SAVED, MU_DELETED };

/* The list's format function (WROWS.H). */
static void user_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    (void)arg;
    wrow_user(r, text, key);
}

static const wf_ops user_ops = {
    scr_user_w_user, NULL, MU_FTITLE, ws_form_busy, NULL, NULL, check, NULL,
    save
};

const ws_scr wuser_scr = {
    "USERS", &scr_user_src, user_fmt, &wrow_user_cols, MU_TITLE,
    &scr_user_def, &user_ops, "USER GET", "USER DEL", MU_DELQ, NULL, NULL,
    WS_NEWREST, NULL, NULL, NULL, NULL, scr_user_kline, user_notes, NULL,
    NULL, &wpane_user
};
