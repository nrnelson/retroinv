/*
 * WROWS.C - the rows of INVWIN's lists and their columns (WROWS.H).
 */
#define MSG_LETTER
#include <string.h>
#include "COMPAT.H"
#include "CWIN.H"
#include "LINK.H"
#include "MSG.H"
#include "PAGER.H"
#include "CODETAB.H"
#include "INTL.H"
#include "ITEMLS.H"
#include "SCRAFORM.H"
#include "SCRIFORM.H"
#include "SCRLFORM.H"
#include "SCRMFORM.H"
#include "SCRNFORM.H"
#include "SCRZFORM.H"
#include "WCOLS.H"
#include "WFORM.H"
#include "WROWS.H"

/* Fields the shared sources name only in a literal of their own code,
   which an exported name would move in INV's image. */
static const char k_avail[] = "AVAIL";
static const char k_level[] = "LEVEL";
static const char k_mustchg[] = "MUSTCHG";
static const char k_login[] = "LASTLOGIN";
static const char k_adate[] = "ADATE";
static const char k_late[] = "!";
static const char k_none[] = "";

/* The orders of itl_src that the item list's headings name, as
   ITEMLS.H titles them: orders[i] is titled MI_T_ID + i. */
#define WR_ID           WCOL_ORDER(0)
#define WR_LOC          WCOL_ORDER(MI_T_LOC - MI_T_ID)
#define WR_MFR          WCOL_ORDER(MI_T_MFR - MI_T_ID)
#define WR_SER          WCOL_ORDER(ITL_ORDSER)

static const wcol item_col[] = {
    { LINK_IDLEN, WCOL_RIGHT | WR_ID, 11 }, { WROW_CATW, 0, 16 },
    { LINK_MFRLEN, WR_MFR, 20 }, { LINK_MFRLEN, 0, 20 }, { IC_WORKS, 0, 8 },
    { WROW_QTYW, WCOL_RIGHT, 8 }, { LINK_LOCNAMELEN, WR_LOC, 20 },
    { IC_WHY, 0, 11 }
};

static const wcol ser_col[] = {
    { LINK_IDLEN, WCOL_RIGHT | WR_ID, 11 }, { LINK_MFRLEN, WR_MFR, 20 },
    { LINK_MFRLEN, 0, 20 }, { LINK_SERIALLEN, WR_SER, 20 },
    { LINK_LOCNAMELEN, WR_LOC, 20 }
};

static const wcol part_col[] = {
    { LINK_IDLEN, WCOL_RIGHT, 11 }, { WROW_CATW, 0, 16 },
    { WROW_PARTW, WCOL_INDENT, 24 }, { LINK_MFRLEN, 0, 20 },
    { WROW_QTYW, WCOL_RIGHT, 8 }, { LINK_LOCNAMELEN, 0, 20 },
    { WROW_STATW, 0, 8 }
};

static const wcol tree_col[] = {
    { WROW_TREEW, WCOL_INDENT, 30 }, { LINK_CODENAMELEN, 0, 16 },
    { LINK_LOCLEN, 0, 11 }, { SCR_LT_CAPW, WCOL_RIGHT, 11 }
};

static const wcol svc_col[] = {
    { LINK_IDLEN, WCOL_RIGHT, 11 }, { LINK_IDLEN, WCOL_RIGHT, 11 },
    { MSG_DATEW, 0, 13 }, { WROW_MTYPEW, 0, 7 },
    { WROW_COSTW, WCOL_RIGHT, 11 }, { MAINT_SUMW, 0, 60 }
};

/* An item's service records: svc_col but its item. */
static const wcol svci_col[] = {
    { LINK_IDLEN, WCOL_RIGHT, 11 }, { MSG_DATEW, 0, 13 },
    { WROW_MTYPEW, 0, 7 }, { WROW_COSTW, WCOL_RIGHT, 11 },
    { MAINT_SUMW, 0, 60 }
};

static const wcol loan_col[] = {
    { 1, 0, 3 }, { LC_ID, WCOL_RIGHT, 11 }, { LC_KIND, 0, 11 },
    { LC_ID, WCOL_RIGHT, 11 }, { WROW_QTYW, WCOL_RIGHT, 8 },
    { LOAN_NAMEW, 0, 20 }, { MSG_DATEW, 0, 13 }, { MSG_DATEW, 0, 13 },
    { LINK_MFRLEN, 0, 20 }, { LINK_MFRLEN, 0, 20 }
};

/* An item's own loans: loan_col but its item, maker and model. */
static const wcol loani_col[] = {
    { 1, 0, 3 }, { LC_ID, WCOL_RIGHT, 11 }, { LC_KIND, 0, 11 },
    { WROW_QTYW, WCOL_RIGHT, 8 }, { LOAN_NAMEW, 0, 20 },
    { MSG_DATEW, 0, 13 }, { MSG_DATEW, 0, 13 }
};

static const wcol user_col[] = {
    { LINK_USERLEN, 0, 0 }, { LINK_FULLNAMELEN, 0, 24 },
    { LINK_GROUPLEN, 0, 0 }, { UC_LEVEL, WCOL_RIGHT, 7 },
    { UC_STATUS, 0, 8 }, { UC_MUSTCHG, 0, 6 }, { MSG_DATEW, 0, 13 }
};

static const wcol audit_col[] = {
    { MSG_DATEW, 0, 13 }, { WROW_TIMEW, 0, 10 }, { LINK_USERLEN, 0, 0 },
    { LINK_STALEN, 0, 14 }, { WROW_TBLW, 0, 9 },
    { LINK_IDLEN, WCOL_RIGHT, 11 }, { 1, 0, 3 }, { LINK_NAMELEN, 0, 0 },
    { WROW_AUDVALW, 0, 30 }, { WROW_AUDVALW, 0, 30 }
};

static const wcol code_col[] = {
    { LINK_CATLEN, 0, 9 }, { LINK_CODENAMELEN, 0, 20 },
    { WROW_ORDW, WCOL_RIGHT, 8 }
};

#define WROW_N(c)       (sizeof c / sizeof c[0])

/* The bytes of a WIN.INI name or value read: a short date picture
   longer than any supported is read whole, never cut into one. */
#define WROW_INTLSZ     32

/* The station's place among scr_audit_cols: after the user. */
#define AU_STAAT        2

const wcol_tab wrow_item_cols[ITL_ORDSER + 1] = {
    { WROW_N(item_col), WROW_K_ITEM, item_col, WROW_S_ITEM },
    { WROW_N(item_col), WROW_K_ITEM, item_col, WROW_S_ITEM },
    { WROW_N(item_col), WROW_K_ITEM, item_col, WROW_S_ITEM },
    { WROW_N(ser_col), WROW_K_SER, ser_col, WROW_S_SER }
};
const wcol_tab wrow_ipick_cols = {
    WROW_PICKCOLS, WROW_K_ITEM, item_col, WROW_S_ITEM
};
const wcol_tab wrow_part_cols = {
    WROW_N(part_col), WROW_K_PART, part_col, WROW_S_PART
};
const wcol_tab wrow_tree_cols = {
    WROW_N(tree_col), WROW_K_TREE, tree_col, WROW_S_TREE
};
const wcol_tab wrow_tpick_cols = {
    WROW_TPICKCOLS, WROW_K_TREE, tree_col, WROW_S_TREE
};
const wcol_tab wrow_svc_cols[2] = {
    { WROW_N(svc_col), WROW_K_SVC, svc_col, WROW_S_SVC },
    { WROW_N(svci_col), WROW_K_ISVC, svci_col, WROW_S_SVCI }
};
const wcol_tab wrow_loan_cols[2] = {
    { WROW_N(loan_col), WROW_K_LOAN, loan_col, WROW_S_LOAN },
    { WROW_N(loani_col), WROW_K_ILOAN, loani_col, WROW_S_LOANI }
};
const wcol_tab wrow_user_cols = {
    WROW_N(user_col), WROW_K_USER, user_col, WROW_S_USER
};
const wcol_tab wrow_audit_cols = {
    WROW_N(audit_col), WROW_K_AUDIT, audit_col, WROW_S_AUDIT
};
const wcol_tab wrow_code_cols = {
    WROW_N(code_col), 0, code_col, WROW_S_CODE
};

/* The item form's page of each field of SCRIFORM.H's table, in its
   order: WF_PG_TOP above the tabs, then Overview (0), Technical (1),
   Acquisition (2) and Notes (3). */
static const u8 item_page[] = {
    WF_PG_TOP, 0, 0, 0, 0,      /* ID, category, status, type, cond   */
    WF_PG_TOP, WF_PG_TOP, 1, 1, /* maker, model, serial, description  */
    0, 0, 0, 0, 0, 0,           /* works, quantity, held, location,
                                   parent, path                         */
    2, 2, 2, 2,                 /* acquired, source, price, value      */
    0, 2, 2, 3                  /* tags, created, updated, notes       */
};

/* A page for every field of the item form. */
extern char wrow_page_count[1];
extern char wrow_page_count[(sizeof item_page == SCR_IF_COUNT) ? 1 : 2];

const wf_pages wrow_item_pages = {
    WROW_S_IPAGES, WROW_S_IACT, WROW_IACTS, item_page
};

/* Every row ends within LV_ROWTEXT: an item's in either order, a part's,
   a location's, a service record's and a loan's with their item's
   column, a user's and an audit entry's; the loans' kind and item start
   where scr_loan_rowkind and scr_loan_rowitem read them, a capacity
   holds its separator, num_col's buffer holds every number column, a
   category's row ends within LV_ROWTEXT too, its sort order fits under
   its word, and its headings' string follows the record buttons'. */
extern char wrow_fits[1];
extern char wrow_fits[(LINK_IDLEN + WROW_CATW + 2 * LINK_MFRLEN + IC_WORKS +
                       WROW_QTYW + LINK_LOCNAMELEN + IC_WHY + 7 <=
                       LV_ROWTEXT &&
                       LINK_IDLEN + 2 * LINK_MFRLEN + LINK_SERIALLEN +
                       LINK_LOCNAMELEN + 4 <= LV_ROWTEXT &&
                       LINK_IDLEN + WROW_CATW + WROW_PARTW + LINK_MFRLEN +
                       WROW_QTYW + LINK_LOCNAMELEN + WROW_STATW + 6 <=
                       LV_ROWTEXT &&
                       WROW_TREEW + LINK_CODENAMELEN + LINK_LOCLEN +
                       SCR_LT_CAPW + 3 <= LV_ROWTEXT &&
                       2 * LINK_IDLEN + MSG_DATEW + WROW_MTYPEW +
                       WROW_COSTW + MAINT_SUMW + 5 <= LV_ROWTEXT &&
                       1 + 2 * LC_ID + LC_KIND + WROW_QTYW + LOAN_NAMEW +
                       2 * MSG_DATEW + 2 * LINK_MFRLEN + 9 <= LV_ROWTEXT &&
                       LINK_USERLEN + LINK_FULLNAMELEN + LINK_GROUPLEN +
                       UC_LEVEL + UC_STATUS + UC_MUSTCHG + MSG_DATEW + 6 <=
                       LV_ROWTEXT &&
                       MSG_DATEW + WROW_TIMEW + LINK_USERLEN +
                       LINK_STALEN + WROW_TBLW + LINK_IDLEN + 1 +
                       LINK_NAMELEN + 2 * WROW_AUDVALW + 9 <= LV_ROWTEXT &&
                       1 + 1 + LC_ID + 1 == LC_KINDAT &&
                       LC_KINDAT + LC_KIND + 1 == LC_ITEMAT &&
                       WROW_PICKCOLS <= WROW_N(item_col) &&
                       WROW_N(audit_col) == SCR_AU_NCOLS + 2 &&
                       WROW_TPICKCOLS <= WROW_N(tree_col) &&
                       WROW_QTYW <= WROW_COSTW &&
                       INTL_NUMW(SCR_LF_CAPW, 0) <= SCR_LT_CAPW &&
                       SCR_LT_CAPW <= WROW_COSTW &&
                       LINK_CATLEN + LINK_CODENAMELEN + WROW_ORDW + 2 <=
                       LV_ROWTEXT &&
                       CT_ORDERW <= WROW_ORDW &&
                       WROW_S_CODE == WROW_S_IACT + WROW_IACTS) ? 1 : 2];

intl_fmt wrow_intl;

/* v, its leading blanks skipped. */
static const char *trim(const char *v)
{
    while (*v == ' ')
        v++;
    return v;
}

/* The value of field name in r as a column of w cells and flags. */
static void col(char *text, u16 *at, const lv_rec *r, const char *name,
                u16 w, u8 flags)
{
    lv_col(text, at, lv_val(r, name), w, flags);
}

/* The value of field name in r, a number, with wrow_intl's separators
   as a column of w cells, right-aligned. */
static void num_col(char *text, u16 *at, const lv_rec *r, const char *name,
                    u16 w)
{
    char v[WROW_COSTW + 1];

    intl_num(&wrow_intl, v, lv_val(r, name), w);
    lv_col(text, at, v, w, LV_RIGHT);
}

/* The value of field name in r, a wire date, in wrow_intl's short date
   as a column of MSG_DATEW cells. */
static void date_col(char *text, u16 *at, const lv_rec *r, const char *name)
{
    char d[MSG_DATEW + 1];

    intl_date(&wrow_intl, d, lv_val(r, name));
    lv_col(text, at, d, MSG_DATEW, 0);
}

/* The row's key: the value of field name, its leading blanks skipped. */
static void row_key(const lv_rec *r, const char *name, char *key)
{
    txt_copy(key, trim(lv_val(r, name)), LV_KEYSZ);
}

void wrow_cat(ctab *cats, char *c)
{
    char name[LINK_CODENAMELEN + 1];
    u16 n;

    if (c[0] == '\0')
        return;
    ct_name(cats, c, name);
    if (strcmp(name, c) != 0) {
        n = (u16)strlen(c);
        c[n++] = ' ';
        txt_copy(c + n, name, (u16)(WROW_CATW + 1 - n));
    }
}

/* The category of r as wrow_cat names it. */
static void cat_col(char *text, u16 *at, const lv_rec *r, ctab *cats)
{
    char c[WROW_CATW + 1];

    txt_copy(c, trim(lv_val(r, itl_k_cat)), (u16)sizeof c);
    wrow_cat(cats, c);
    lv_col(text, at, c, WROW_CATW, 0);
}

/* r's value of the depth field name, from 1 to max, as a count of
   blanks of indent per level below the first. */
static u16 indent(const lv_rec *r, const char *name, u16 max, u16 per)
{
    const char *s;
    u16 d;

    s = trim(lv_val(r, name));
    d = msg_dec(&s, max);
    if (d < 1)
        d = 1;
    if (d > max)
        d = max;
    return (u16)(per * (d - 1));
}

/* A column of w cells holding the value of field name after ind
   blanks. */
static void indented(char *text, u16 *at, const lv_rec *r, const char *name,
                     u16 ind, u16 w)
{
    memset(text + *at, ' ', ind);
    *at = (u16)(*at + ind);
    col(text, at, r, name, (u16)(w - ind), 0);
}

void wrow_item(const lv_rec *r, u8 order, ctab *cats, char *text,
               char *key)
{
    const char *v;
    const char *w;
    u16 at;

    row_key(r, itl_k_itemid, key);
    at = 0;
    lv_col(text, &at, key, LINK_IDLEN, LV_RIGHT);
    if (order == ITL_ORDSER) {
        col(text, &at, r, itl_k_mfr, LINK_MFRLEN, 0);
        col(text, &at, r, itl_k_model, LINK_MFRLEN, 0);
        col(text, &at, r, itl_k_serial, LINK_SERIALLEN, 0);
        col(text, &at, r, itl_k_loc, LINK_LOCNAMELEN, 0);
        return;
    }
    cat_col(text, &at, r, cats);
    col(text, &at, r, itl_k_mfr, LINK_MFRLEN, 0);
    col(text, &at, r, itl_k_model, LINK_MFRLEN, 0);
    v = lv_val(r, itl_k_works);
    w = msg_letter(MT_WK_Q, itl_works, v);
    lv_col(text, &at, w != NULL ? w : v, IC_WORKS, 0);
    num_col(text, &at, r, itl_k_qty, WROW_QTYW);
    col(text, &at, r, itl_k_loc, LINK_LOCNAMELEN, 0);
    w = msg_letter(MH_K_NAME, itl_whys, lv_val(r, k_avail));
    if (w != NULL)
        lv_col(text, &at, w, IC_WHY, 0);
}

void wrow_part(const lv_rec *r, ctab *cats, char *text, char *key)
{
    u16 at;

    row_key(r, itl_k_itemid, key);
    at = 0;
    lv_col(text, &at, key, LINK_IDLEN, LV_RIGHT);
    cat_col(text, &at, r, cats);
    indented(text, &at, r, itl_k_mfr,
             indent(r, k_level, WROW_BOMLEVELS, AC_INDENT), WROW_PARTW);
    col(text, &at, r, itl_k_model, LINK_MFRLEN, 0);
    num_col(text, &at, r, itl_k_qty, WROW_QTYW);
    col(text, &at, r, itl_k_loc, LINK_LOCNAMELEN, 0);
    col(text, &at, r, itl_k_status, WROW_STATW, 0);
}

void wrow_tree(const lv_rec *r, ctab *types, char *text, char *key)
{
    char tname[LINK_CODENAMELEN + 1];
    u16 at;

    row_key(r, scr_loc_k_id, key);
    at = 0;
    indented(text, &at, r, scr_loc_fields[SCR_LF_NAME].name,
             indent(r, scr_loc_k_depth, LINK_MAXLOCDEPTH, SCR_LT_INDENT),
             WROW_TREEW);
    ct_name(types, lv_val(r, scr_loc_fields[SCR_LF_TYPE].name), tname);
    lv_col(text, &at, tname, LINK_CODENAMELEN, 0);
    col(text, &at, r, scr_loc_fields[SCR_LF_CODE].name, LINK_LOCLEN, 0);
    num_col(text, &at, r, scr_loc_fields[SCR_LF_CAP].name, SCR_LT_CAPW);
}

void wrow_svc(const lv_rec *r, int withitem, char *text, char *key)
{
    u16 at;

    row_key(r, scr_maint_k_id, key);
    at = 0;
    lv_col(text, &at, key, LINK_IDLEN, LV_RIGHT);
    if (withitem)
        col(text, &at, r, itl_k_itemid, LINK_IDLEN, LV_RIGHT);
    date_col(text, &at, r, scr_maint_fields[SCR_MF_DATE].name);
    col(text, &at, r, scr_maint_fields[SCR_MF_TYPE].name, WROW_MTYPEW, 0);
    num_col(text, &at, r, scr_maint_fields[SCR_MF_COST].name, WROW_COSTW);
    col(text, &at, r, scr_maint_fields[SCR_MF_SUM].name, MAINT_SUMW, 0);
}

void wrow_loan(scr_lq *q, const lv_rec *r, char *text, char *key)
{
    char host[LOAN_NAMEW + 1];
    const char *v;
    u16 at;
    u16 n;
    u8 kind;
    int every;

    every = q->item[0] == '\0';
    row_key(r, scr_loan_k_id, key);
    kind = scr_loan_kind(lv_val(r, scr_loan_k_kind));
    at = 0;
    lv_col(text, &at, scr_loan_mark(q, r) ? k_late : k_none, 1, 0);
    lv_col(text, &at, key, LC_ID, LV_RIGHT);
    lv_col(text, &at, msg((u16)(MH_K_NAME + kind)), LC_KIND, 0);
    if (every)
        col(text, &at, r, itl_k_itemid, LC_ID, LV_RIGHT);
    num_col(text, &at, r, itl_k_qty, WROW_QTYW);
    v = lv_val(r, scr_loan_fields[SCR_NF_HOLDER].name);
    if (v[0] == '\0' && q->host[0] == '\0') {
        n = msg_get((u16)(kind == SCR_K_USE ? MH_L_HOSTUSE : MH_L_HOST), host,
                    (u16)(sizeof host - LINK_IDLEN - 1));
        host[n++] = ' ';
        txt_copy(host + n, trim(lv_val(r, scr_loan_k_host)),
                 (u16)(sizeof host - n));
        v = host;
    }
    lv_col(text, &at, v, LOAN_NAMEW, 0);
    date_col(text, &at, r, scr_loan_fields[SCR_NF_DUE].name);
    date_col(text, &at, r, scr_loan_fields[SCR_NF_RET].name);
    if (every) {
        col(text, &at, r, itl_k_mfr, LINK_MFRLEN, 0);
        col(text, &at, r, itl_k_model, LINK_MFRLEN, 0);
    }
}

void wrow_user(const lv_rec *r, char *text, char *key)
{
    u16 at;

    row_key(r, scr_user_fields[SCR_UF_USER].name, key);
    at = 0;
    lv_col(text, &at, key, LINK_USERLEN, 0);
    col(text, &at, r, scr_user_fields[SCR_UF_NAME].name, LINK_FULLNAMELEN,
        0);
    col(text, &at, r, scr_user_k_group, LINK_GROUPLEN, 0);
    col(text, &at, r, scr_user_fields[SCR_UF_LEVEL].name, UC_LEVEL,
        LV_RIGHT);
    col(text, &at, r, scr_user_fields[SCR_UF_STATUS].name, UC_STATUS, 0);
    col(text, &at, r, k_mustchg, UC_MUSTCHG, 0);
    date_col(text, &at, r, k_login);
}

void wrow_audit(const lv_rec *r, char *text, char *key)
{
    const wcol *c;
    u16 at;
    u16 i;

    row_key(r, scr_audit_k_id, key);
    at = 0;
    date_col(text, &at, r, k_adate);
    /* The shared columns in audit_col's cells, the station after the
       user (AU_STAAT). */
    c = audit_col + 1;
    for (i = 0; i < SCR_AU_NCOLS; i++, c++) {
        if (i == AU_STAAT)
            col(text, &at, r, scr_audit_k_sta, (c++)->cells, 0);
        col(text, &at, r, scr_audit_cols[i].name, c->cells,
            (u8)(c->flags & WCOL_RIGHT ? LV_RIGHT : 0));
    }
}

/* CAT LIST's value i of r, the code, the name and the sort order in
   that order, as CODETAB.H reads them; "" for a line without it. */
static const char *code_val(const lv_rec *r, u16 i)
{
    return i < r->n ? r->vals[i] : k_none;
}

void wrow_code(const lv_rec *r, char *text, char *key)
{
    u16 at;

    txt_copy(key, trim(code_val(r, 0)), LV_KEYSZ);
    at = 0;
    lv_col(text, &at, key, LINK_CATLEN, 0);
    lv_col(text, &at, code_val(r, 1), LINK_CODENAMELEN, 0);
    lv_col(text, &at, code_val(r, 2), WROW_ORDW, LV_RIGHT);
}

/* The w cells of a row at s into out (w + 1 bytes), without the blanks
   before and after their text. */
static void cell(char *out, const char *s, u16 w)
{
    for (; w > 0 && *s == ' '; s++)
        w--;
    while (w > 0 && s[w - 1] == ' ')
        w--;
    memcpy(out, s, w);
    out[w] = '\0';
}

void wrow_code_get(const char *text, ct_row *r)
{
    cell(r->code, text, LINK_CATLEN);
    text += LINK_CATLEN + 1;
    cell(r->name, text, LINK_CODENAMELEN);
    /* The sort order ends its column, which is wider than it. */
    cell(r->order, text + LINK_CODENAMELEN + 1 + WROW_ORDW - CT_ORDERW,
         CT_ORDERW);
}

/* Into out (size bytes), in CP437, the value of [intl]'s key named by
   string number key of inst, or string key + 1 when WIN.INI has none
   (WROWSTR.H). */
static void profile(HINSTANCE inst, const char *sec, UINT key, char *out,
                    int size)
{
    char k[WROW_INTLSZ];
    char d[WROW_INTLSZ];

    LoadString(inst, key, k, (int)sizeof k);
    LoadString(inst, key + 1, d, (int)sizeof d);
    GetProfileString(sec, k, d, out, size);
    AnsiToOem(out, out);
}

void wrow_intl_read(HINSTANCE inst)
{
    char sec[WROW_INTLSZ];
    char pic[WROW_INTLSZ];
    char dec[WROW_INTLSZ];
    char thou[WROW_INTLSZ];
    char key[WROW_INTLSZ];

    LoadString(inst, WROW_S_INTL, sec, (int)sizeof sec);
    profile(inst, sec, WROW_S_DATE, pic, (int)sizeof pic);
    profile(inst, sec, WROW_S_DEC, dec, (int)sizeof dec);
    profile(inst, sec, WROW_S_THOU, thou, (int)sizeof thou);
    LoadString(inst, WROW_S_LZERO, key, (int)sizeof key);
    intl_set(&wrow_intl, pic, dec, thou, GetProfileInt(sec, key, 0));
}
