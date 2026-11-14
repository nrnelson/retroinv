/*
 * SCRZFORM.C - the tables of the Admin menu's screens (SCRZFORM.H).
 *
 * Field and column names are taken from the tables wherever a row needs
 * them, so each is kept once.  Compile-time checks here are pairs of
 * declarations of one array, the second of size 2 when the condition
 * fails: they then disagree, which every compiler refuses.
 */
#include <stddef.h>
#include <string.h>
#include "COMPAT.H"
#include "LINK.H"
#include "RQ.H"
#include "TUI.H"
#include "PAGER.H"
#include "FMODEL.H"
#include "SCRZFORM.H"

/* INV's form box, and its columns: labels, values, and the status
   beside the user name. */
#define UF_TOP          (TUI_TOP + 2)
#define UF_LEFT         9
#define UF_H            9
#define UF_W            62
#define UF_LCOL         2
#define UF_VCOL         19
#define UF_SCOL         34
#define UF_SVCOL        42

/* Indexes into scr_user_fields beside SCRZFORM.H's. */
#define F_GROUP         3

/* The label and value columns of the filter's fields in INV's box. */
#define AU_C1           2
#define AU_V1           8
#define AU_C2           16
#define AU_V2           23
#define AU_C3           34
#define AU_V3           42

/* The sessions' columns not sized by LINK.H. */
#define SS_SLOTW        4
#define SS_TYPEW        4
#define SS_LEVELW       5
#define SS_STATEW       15
#define SS_IDLEW        6

const char scr_audit_k_sta[] = "STATION";
static const char k_action[] = "ACTION";

/* ---------------------------------------------------------------------
 * The users.
 * --------------------------------------------------------------------- */

const char scr_user_w_user[] = "USER";
const char scr_user_k_group[] = "GROUPNAME";

const form_field scr_user_fields[] = {
    { "STATUS",   MU_L_STATUS, 0, UF_SCOL, UF_SVCOL, FT_CHOICE,
      LINK_STATUSLEN, 0, FF_REQ, 0, "A D" },
    { "USERNAME", MU_L_USER,   0, UF_LCOL, UF_VCOL, FT_CHAR, LINK_USERLEN,
      0, FF_KEY | FF_NEWONLY | FF_UPPER | FF_REQ, 0, NULL },
    { "FULLNAME", MU_L_NAME,   1, UF_LCOL, UF_VCOL, FT_CHAR,
      LINK_FULLNAMELEN, 0, 0, 0, NULL },
    { "GROUP",    MU_L_GROUP,  2, UF_LCOL, UF_VCOL, FT_CHAR, LINK_GROUPLEN,
      0, FF_UPPER, 0, NULL },
    { "LEVEL",    MU_L_LEVEL,  3, UF_LCOL, UF_VCOL, FT_CHOICE,
      LINK_LEVELLEN, 0, FF_REQ, 0, "1 2 3" },
    { "PASSWORD", MU_L_PW,     5, UF_LCOL, UF_VCOL, FT_SECRET,
      SCR_UF_PWW, 0, 0, LINK_PWMAX, NULL },
    { "REPEAT",   MU_L_REPEAT, 6, UF_LCOL, UF_VCOL, FT_SECRET,
      SCR_UF_PWW, 0, FF_LOCAL, LINK_PWMAX, NULL }
};

/* The table has SCR_UF_COUNT fields, STATUS the first, which a new
   user's form leaves out. */
extern char user_f_count[1];
extern char user_f_count[(sizeof scr_user_fields /
                          sizeof scr_user_fields[0] == SCR_UF_COUNT &&
                          SCR_UF_STATUS == 0) ? 1 : 2];

const form_def scr_user_def = {
    MU_NEWTITLE, scr_user_w_user, UF_TOP, UF_LEFT, UF_H, UF_W,
    SCR_UF_COUNT, scr_user_fields
};

/* ---------------------------------------------------------------------
 * The list.
 * --------------------------------------------------------------------- */

const lv_src scr_user_src = {
    "USER LIST", 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MU_HDR, MU_HDR, MU_HDR, MU_HDR },
    MU_EMPTY, NULL, NULL, scr_user_fmt, (void *)scr_user_w_user,
    scr_user_mark
};

/* arg is never NULL: the test keeps the parameter, which lv_mark_fn
   requires, referenced. */
int scr_user_mark(void *arg, const lv_rec *r)
{
    return arg != NULL &&
           strcmp(lv_val(r, scr_user_fields[SCR_UF_STATUS].name),
                  "D") == 0;
}

void scr_user_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    const char *name;
    u16 at;

    /* The list always passes an arg, so this test never fires; it keeps
       the parameter, which lv_fmt_fn requires, referenced. */
    if (arg == NULL)
        return;
    name = lv_val(r, scr_user_fields[SCR_UF_USER].name);
    txt_copy(key, name, LV_KEYSZ);
    text[0] = ' ';
    text[1] = '\0';
    at = 1;
    lv_col(text, &at, name, LINK_USERLEN, 0);
    lv_col(text, &at, lv_val(r, scr_user_fields[SCR_UF_NAME].name),
           LINK_FULLNAMELEN, 0);
    lv_col(text, &at, lv_val(r, scr_user_k_group), LINK_GROUPLEN, 0);
    lv_col(text, &at, lv_val(r, scr_user_fields[SCR_UF_LEVEL].name), UC_LEVEL,
           1);
    lv_col(text, &at, lv_val(r, scr_user_fields[SCR_UF_STATUS].name),
           UC_STATUS, 0);
    lv_col(text, &at, lv_val(r, "MUSTCHG"), UC_MUSTCHG, 0);
    lv_datecol(text, &at, lv_val(r, "LASTLOGIN"));
}

/* ---------------------------------------------------------------------
 * The form's requests.
 * --------------------------------------------------------------------- */

int scr_user_kline(void *arg, const rq_line *ln)
{
    if (ln->kind == 'K' && ln->n == 2 &&
        strcmp(ln->v[0], scr_user_k_group) == 0)
        form_load((form *)arg, scr_user_fields[F_GROUP].name, ln->v[1]);
    return form_kline(arg, ln);
}

int scr_user_reset(link *l, const char *name, const char *pw,
                   rq_res *res)
{
    rq q;

    rq_begin(&q, "USER RESET");
    rq_word(&q, name);
    rq_pair(&q, scr_user_fields[SCR_UF_PW].name, pw);
    return rq_send(l, &q, NULL, NULL, res);
}

/* ---------------------------------------------------------------------
 * The operations on the data files.
 * --------------------------------------------------------------------- */

const char scr_adm_w_admin[] = "ADMIN";
const char scr_adm_k_dir[] = "DIR";

const scr_adm_op scr_adm_ops[] = {
    { "BACKUP", MZ_T_BACKUP, 0, MZ_BACKEDUP, MZ_NODIR },
    { "RESTORE", MZ_T_RESTORE, MZ_RESTOREQ, MZ_RESTORED, MZ_NOBACKUP },
    { "PACK", 0, MZ_PACKQ, MZ_PACKED, 0 },
    { "REINDEX", 0, 0, MZ_REINDEXED, 0 }
};

/* ---------------------------------------------------------------------
 * The audit log.
 * --------------------------------------------------------------------- */

const char scr_audit_w_list[] = "AUDIT LIST";
const char scr_audit_k_id[] = "AUD_ID";
const char scr_audit_k_tbl[] = "TBL";
const char scr_audit_k_rec[] = "REC";

const form_field scr_audit_fields[] = {
    { scr_audit_k_tbl, MZ_L_TBL, 0, AU_C1, AU_V1, FT_CHAR, AU_TBLW, 0,
      FF_LOCAL | FF_UPPER, 0, NULL },
    { scr_audit_k_rec, MZ_L_REC, 0, AU_C2, AU_V2, FT_NUM, LINK_IDLEN, 0,
      FF_LOCAL | FF_NONNEG | FF_ID, 0, NULL },
    { scr_audit_k_sta, MZ_L_STA, 0, AU_C3, AU_V3, FT_CHAR, LINK_STALEN, 0,
      FF_LOCAL | FF_UPPER, 0, NULL }
};

/* The table has SCR_AF_COUNT fields, each value fitting SCR_AF_VALSZ
   bytes. */
extern char audit_f_count[1];
extern char audit_f_count[(sizeof scr_audit_fields /
                           sizeof scr_audit_fields[0] == SCR_AF_COUNT &&
                           AU_TBLW < SCR_AF_VALSZ &&
                           LINK_IDLEN < SCR_AF_VALSZ) ? 1 : 2];

const form_def scr_audit_def = {
    MZ_T_AUDIT, "AUDIT", TUI_TOP, 0, SCR_AF_H, VID_COLS, SCR_AF_COUNT,
    scr_audit_fields
};

/* The list's columns after the date. */
const scr_col scr_audit_cols[SCR_AU_NCOLS] = {
    { "ATIME", AU_TIMEW, 0 },
    { "USERNAME", LINK_USERLEN, 0 },
    { scr_audit_k_tbl, AU_TBLW, 0 },
    { "REC_ID", LINK_IDLEN, 1 },
    { k_action, 1, 0 },
    { "FLD", LINK_NAMELEN, 0 },
    { "OLDVAL", AU_VALW, 0 },
    { "NEWVAL", AU_VALW, 0 }
};

/* A row, a blank and the date then these columns, each but the last
   with a blank after it, fits LV_TEXTLEN, past which lv_col leaves a
   column out. */
extern char au_row_fits[1];
extern char au_row_fits[(1 + MSG_DATEW + 1 + AU_TIMEW + 1 + LINK_USERLEN + 1 +
                         AU_TBLW + 1 + LINK_IDLEN + 1 + 1 + 1 +
                         LINK_NAMELEN + 1 + AU_VALW + 1 + AU_VALW <=
                         LV_TEXTLEN) ? 1 : 2];

const lv_src scr_audit_src = {
    scr_audit_w_list, 0,
    { NULL, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL },
    { MZ_HDR, MZ_HDR, MZ_HDR, MZ_HDR },
    MZ_EMPTY, scr_audit_k_id, scr_audit_args, scr_audit_fmt, NULL,
    scr_audit_mark
};

void scr_audit_args(void *arg, rq *q)
{
    const char *v;
    u16 i;

    for (i = 0; i < SCR_AF_COUNT; i++) {
        v = form_get((form *)arg, scr_audit_fields[i].name);
        if (v[0] != '\0')
            rq_pair(q, scr_audit_fields[i].name, v);
    }
}

void scr_audit_fmt(void *arg, const lv_rec *r, char *text, char *key)
{
    u16 at;
    u16 i;

    /* The list always passes an arg, so this test never fires; it keeps
       the parameter, which lv_fmt_fn requires, referenced. */
    if (arg == NULL)
        return;
    txt_copy(key, lv_val(r, scr_audit_k_id), LV_KEYSZ);
    text[0] = ' ';
    text[1] = '\0';
    at = 1;
    lv_datecol(text, &at, lv_val(r, "ADATE"));
    for (i = 0; i < SCR_AU_NCOLS; i++)
        lv_col(text, &at, lv_val(r, scr_audit_cols[i].name),
               scr_audit_cols[i].w, scr_audit_cols[i].right);
}

int scr_audit_mark(void *arg, const lv_rec *r)
{
    const char *a;

    a = lv_val(r, k_action);
    return arg != NULL && a[0] != '\0' && strchr("FX", a[0]) != NULL;
}

/* ---------------------------------------------------------------------
 * The sessions.
 * --------------------------------------------------------------------- */

const scr_col scr_sess_cols[] = {
    { "SLOT", SS_SLOTW, 1 },
    { "TYPE", SS_TYPEW, 0 },
    { scr_audit_k_sta, LINK_STALEN, 0 },
    { "USER", LINK_USERLEN, 0 },
    { "LEVEL", SS_LEVELW, 1 },
    { "STATE", SS_STATEW, 0 },
    { "IDLE", SS_IDLEW, 1 },
    { "SELF", 0, 0 }
};

/* SELF is the last of SCR_SS_NCOLS columns, STATE before it. */
extern char ss_self_last[1];
extern char ss_self_last[(SCR_SS_STATE < SCR_SS_SELF &&
                          SCR_SS_SELF + 1 == SCR_SS_NCOLS &&
                          sizeof scr_sess_cols / sizeof scr_sess_cols[0] ==
                          SCR_SS_NCOLS) ? 1 : 2];
