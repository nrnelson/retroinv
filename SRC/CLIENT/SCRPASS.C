/*
 * SCRPASS.C - changing the own password (SCR.H).
 *
 * A form of three password fields (typed without echo, bytes 21h..7Eh)
 * and the user name.  Enter hands them to sn_passwd, which checks them
 * locally and sends PASSWD.  Its local refusals show their message; a
 * wrong old password (SN_PW_OLDWRONG) is reported as a field error on
 * the old password (form_field_error), which puts the cursor there with
 * MP_OLDWRONG; any other failure goes to form_error as it came.
 * form_close overwrites the password values, also when the program ends
 * while the form is open (form_wipe).
 * The values are not a record: Esc and the global keys act at once,
 * also after typing; the form never asks to discard (form_noask).
 *
 * As SCMD_MUSTCHG (the forced change after a LOGIN that answered
 * MUSTCHG=1) the session accepts nothing but the change: the menus are
 * not offered, F3 still asks to quit, and Esc offers to log out
 * instead (TUI_CMD_LOGIN), as a form that finds no memory logs out.
 */
#include <stddef.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "SESS.H"
#include "MSG.H"
#include "TUI.H"
#include "FORM.H"
#include "SCR.H"

#define MP_TITLE        740     /* form title                           */
#define MP_MUSTTITLE    741     /* form title of the forced change      */
#define MP_USER         742     /* field labels                         */
#define MP_OLD          743
#define MP_NEW          744
#define MP_REPEAT       745
#define MP_HINT         746     /* hints                                */
#define MP_NOMATCH      747     /* new and repeated differ              */
#define MP_LENGTH       748     /* not valid: ^1, ^2 the length limits  */
#define MP_SAME         749     /* new equals old                       */
#define MP_CHANGED      750     /* success                              */
#define MP_OLDWRONG     751     /* 401                                  */
#define MP_LOGOUTQ      752     /* Esc in the forced change             */
#define MP_DONETITLE    753     /* title of the success box             */
#define MP_HINTMUST     754     /* hints of the forced change           */

/* scr_passwd shows sn_passwd's local refusal st as MP_NOMATCH -
   SN_PW_NOMATCH + st (the build fails otherwise). */
extern char mp_refusals_fit[1];
extern char mp_refusals_fit[(MP_LENGTH - MP_NOMATCH ==
                             SN_PW_INVALID - SN_PW_NOMATCH &&
                             MP_SAME - MP_NOMATCH ==
                             SN_PW_SAME - SN_PW_NOMATCH) ? 1 : 2];

/* The form's box, and the label and value columns of its fields. */
#define PW_TOP          8
#define PW_LEFT         18
#define PW_H            6
#define PW_W            45
#define PW_LCOL         2
#define PW_VCOL         23

/* Field names, local to this form; each literal is kept once. */
static const char k_old[] = "OLD";
static const char k_new[] = "NEW";
static const char k_rep[] = "REPEAT";
static const char k_user[] = "USER";

/* The cursor starts on the first field, so the user name comes last. */
static const form_field pw_fields[] = {
    { k_old, MP_OLD, 1, PW_LCOL, PW_VCOL, FT_SECRET, LINK_PWMAX, 0,
      FF_LOCAL, 0, NULL },
    { k_new, MP_NEW, 2, PW_LCOL, PW_VCOL, FT_SECRET, LINK_PWMAX + 1, 0,
      FF_LOCAL, LINK_PWMAX, NULL },
    { k_rep, MP_REPEAT, 3, PW_LCOL, PW_VCOL, FT_SECRET, LINK_PWMAX + 1, 0,
      FF_LOCAL, LINK_PWMAX, NULL },
    { k_user, MP_USER, 0, PW_LCOL, PW_VCOL, FT_CHAR, LINK_USERLEN, 0,
      FF_RO | FF_LOCAL, 0, NULL }
};

static const form_def pw_form = {
    MP_TITLE, "PASSWD", PW_TOP, PW_LEFT, PW_H, PW_W,
    (u8)(sizeof pw_fields / sizeof pw_fields[0]), pw_fields
};

u8 scr_passwd(u8 cmd)
{
    char lo[TXT_U32LEN + 1];
    char hi[TXT_U32LEN + 1];
    form *f;
    rq_res res;
    int must;
    int used;
    int st;
    u16 k;
    u8 r;

    must = cmd == SCMD_MUSTCHG;
    r = TUI_CMD_NONE;
    f = form_open(&pw_form, FORM_EDIT, NULL, NULL, NULL);
    if (f == NULL) {
        tui_msg(msg(MT_NOMEM), 1);
        return must ? TUI_CMD_LOGIN : TUI_CMD_NONE;
    }
    form_noask(f);
    if (must)
        form_set_title(f, msg(MP_MUSTTITLE));
    form_load(f, k_user, tui_s.user);
    tui_hint(msg(must ? MP_HINTMUST : MP_HINT));
    for (;;) {
        k = form_run(f);
        if (k == KEY_ESC) {
            if (!must)
                break;
            if (tui_confirm(msg(MP_LOGOUTQ))) {
                r = TUI_CMD_LOGIN;
                break;
            }
            continue;
        }
        if (k != KEY_ENTER) {
            if (must) {
                if (k == KEY_F3 && tui_confirm(msg(MT_QUIT))) {
                    r = TUI_CMD_QUIT;
                    break;
                }
                continue;
            }
            r = tui_global(k, &used);
            if (used)
                break;
            continue;
        }
        st = sn_passwd(tui_s.l, form_get(f, k_old), form_get(f, k_new),
                       form_get(f, k_rep), &res);
        if (st == SN_PW_DONE) {
            tui_msgbox(msg(MP_DONETITLE), msg(MP_CHANGED));
            r = TUI_CMD_NONE;
            break;
        }
        if (st == SN_PW_OLDWRONG) {
            form_field_error(f, k_old, msg(MP_OLDWRONG));
            continue;
        }
        if (st != SN_PW_FAILED) {
            txt_u32(lo, LINK_PWMIN);
            txt_u32(hi, LINK_PWMAX);
            tui_msg(msg_sub((u16)(MP_NOMATCH - SN_PW_NOMATCH + st), lo, hi),
                    1);
            continue;
        }
        r = form_error(f, &res);
        if (r != TUI_CMD_NONE)
            break;
    }
    form_close(f);
    return r;
}
